# Flashing the E73 keychain PCB

The Gerber PCB carries a bare EBYTE E73 module with no bootloader, so SWD
through connector `J3` is the only programming path. `J3` carries `3V3`,
`SWDIO`, `SWDCLK`, `RESET_N` and `GND`.

## Requirements

```powershell
pip install pyocd hidapi pyelftools
```

`pyelftools` is optional; without it `soak` can only detect that the core
stopped, not which handler it stopped in.

Do **not** install the `hid` package alongside `hidapi`. Both provide a module
named `hid`, the `hid` package shadows the working one, and pyOCD then fails at
import with `No USB backend found`. If that happens, `pip uninstall hid`.

## Commands

```powershell
python tools/flash/flash_nrf52840.py info
python tools/flash/flash_nrf52840.py flash --build build-e73
python tools/flash/flash_nrf52840.py soak --seconds 75
```

`info` is read-only. Run it first: it prints the two values that decide
everything else.

| Value | Meaning |
|---|---|
| `UICR BOOTLOADERADDR = 0xFFFFFFFF` | no bootloader; the zero-offset image (`build-e73`) is the correct one |
| any other bootloader address | a bootloader is installed; flashing a zero-offset image would erase it |

## Why the ST-Link needs a patch

pyOCD only opens memory access ports on an ST-Link. Nordic keeps the debug
protection registers in a proprietary CTRL-AP that is not a memory AP, so pyOCD
never opens it and every access returns `STLink error (29): Bad AP`. Target init
then fails on a perfectly healthy chip. [`stlink_ctrl_ap_fix.py`](stlink_ctrl_ap_fix.py)
opens the access port first. It patches pyOCD in memory and does not modify the
installed package.

## Do not halt a running BLE image

The soak test deliberately never halts the core. Zephyr's BLE controller
services the radio against hard deadlines; a debugger halt of a few hundred
milliseconds makes it miss one and assert. The core then sits in
`arch_system_halt` forever, which looks exactly like a firmware crash but was
caused by the debugger. Read memory instead - that goes through the AHB-AP and
leaves the core running.

## Updating over USB-C

Once MCUboot is installed, the debug probe is only needed if the bootloader
itself changes or an update leaves the board unbootable.

```powershell
pip install smpmgr
python tools/flash/usb_update.py
```

The script finds the running application's CDC port, opens it at 1200 baud -
which the firmware treats as "reboot into the bootloader" - waits for MCUboot's
own port, and hands the signed image to `smpmgr`. Application and bootloader are
told apart by USB product ID (`0x8044` and `0x8045`), so no port has to be
guessed.

### There is no rollback in this flow

Verified on hardware: serial recovery writes straight into slot0, so the new
image is live and permanent as soon as it is uploaded. `boot_is_img_confirmed()`
already returns true on the first boot afterwards, and the image survives a
reset without being confirmed by anything.

That means a broken image has to be recovered over SWD. The application does
call `boot_write_img_confirmed()` at startup, which is a no-op today; it exists
so that uploading to the secondary slot instead (`--slot 1`) would give real
test-and-revert behaviour. Switching to that needs the partition map revisited
for MCUboot's swap mechanism, and it has not been tested.

## Installing the bootloader (one time)

```powershell
python tools/flash/flash_nrf52840.py bootstrap
```

This erases the chip, writes MCUboot to 0x0 and the signed application to
slot0. The chip erase matters: leftover bytes in the slot0 image trailer would
be read by MCUboot as a half-finished swap.

Build both images first:

```powershell
# bootloader
cmake -B xiao_nrf52840/build-mcuboot -S $env:ZEPHYR_BASE/../bootloader/mcuboot/boot/zephyr `
  -GNinja -DBOARD=nrf52840dk/nrf52840 `
  -DEXTRA_CONF_FILE=<repo>/xiao_nrf52840/mcuboot/e73.conf `
  -DDTC_OVERLAY_FILE=<repo>/xiao_nrf52840/mcuboot/e73.overlay
cmake --build xiao_nrf52840/build-mcuboot

# application, signed for MCUboot
cmake -B xiao_nrf52840/build-e73-mcuboot -S <repo>/xiao_nrf52840 `
  -GNinja -DBOARD=nrf52840dk/nrf52840 `
  -DDTC_OVERLAY_FILE=<repo>/xiao_nrf52840/boards/nrf52840dk_nrf52840.overlay `
  "-DEXTRA_CONF_FILE=<repo>/xiao_nrf52840/gerber.conf;<repo>/xiao_nrf52840/mcuboot/app.conf"
cmake --build xiao_nrf52840/build-e73-mcuboot
```

`info` tells you what is on the board:

| Field | Meaning |
|---|---|
| `SCB->VTOR = 0x00010200` | the application in slot0 is running |
| `SCB->VTOR = 0x00000000` | MCUboot is running, or no bootloader is installed |
| `slot0 header = 0x96F3B83D` | a valid MCUboot image is present |

## The signing key is a development key

The application is signed with MCUboot's `root-rsa-2048.pem`, which is published
in the MCUboot repository. It makes the image loadable, not trustworthy: anyone
can sign an image that this bootloader accepts. Generate a private key with
`imgtool keygen` and point both `mcuboot/e73.conf` and `mcuboot/app.conf` at it
before shipping anything.
