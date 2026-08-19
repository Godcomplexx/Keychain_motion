# nRF52840 firmware port

This directory is an independent Zephyr build. It reuses the portable motion,
clock, animation, and game components from the ESP32-C3 firmware while keeping
all nRF52840 hardware access here.

## Why there are three configurations

The USB board currently connected to the computer is a Seeed XIAO nRF52840.
The supplied `Gerber_PCBkey_cat.zip` describes a different final controller:
an EBYTE E73-2G4M08S1C module containing the same nRF52840 SoC.

| Build target | Purpose | I2C pins |
|---|---|---|
| `xiao_ble/nrf52840` | USB prototype that can receive a UF2 image | XIAO `D4/SDA`, `D5/SCL` |
| `xiao_ble/nrf52840` + Gerber overlay | program through XIAO USB before moving the SoC | `P0.08/SDA`, `P0.06/SCL` |
| `nrf52840dk/nrf52840` | final E73 Gerber PCB, flashed through SWD | `P0.08/SDA`, `P0.06/SCL` |

The last target uses the DK SoC definition only as a known-good bare
nRF52840 memory layout. Its application overlay replaces the DK I2C routing
with the E73 routing recovered from the Gerber flying-probe netlist.

The USB+Gerber configuration preserves the XIAO bootloader/application offset
at `0x27000`. It is specifically for programming the nRF52840 over USB before
physically moving that same programmed SoC. It is not a replacement for the
zero-offset SWD image used by a new blank E73 module.

The Gerber does not fit an external 32.768 kHz crystal on the E73 `XL1/XL2`
pins. Both Gerber builds therefore include `gerber.conf`, which selects and
calibrates the nRF52840 internal low-frequency RC oscillator. The E73 module's
onboard 32 MHz crystal remains the high-frequency radio clock.

The USB images also implement a 1200-baud touch reset. After this image is
installed once, opening its COM port at 1200 baud requests the factory USB
bootloader, so later application updates do not require the RESET pads.

## Companion behaviour

The active screen is no longer a fixed animation. A procedural character owns
it: two eyes whose size, lids, gaze and blinking are computed every frame from
"which behaviour is running and for how long". No sprite sheets are involved,
so a new mood costs one switch case instead of a new asset.

The chain is `event -> priority -> behaviour -> animation`, split across two
components so the rules can be reasoned about without touching pixels:

| Component | Responsibility |
|---|---|
| `pet_behavior` | events, P0-P3 priorities, timers, interaction memory. Plain C, no Zephyr |
| `pet_face` | eye geometry, blinking, overlays. Knows nothing about events |

Sources are split on purpose. **Events** happen once and are delivered with
`pet_behavior_post`: shaken, picked up, charger attached, phone synced.
**Context** is level-triggered and passed every tick to `pet_behavior_update`:
sensor present, charging, battery level. Sending a level condition as an event
is the classic bug - the animation restarts every tick and never finishes.

| Event | Reaction | Priority | Length |
|---|---|---:|---:|
| boot finished | `wake_up`, eyes open and glance up | P1 | 1400 ms |
| picked up after stillness | `wake_up` | P1 | 1400 ms |
| tilted left / right | `look_left` / `look_right`, gaze eases across | P1 | 1200 ms |
| shaken | `surprised`, eyes go wide and the face hops | P1 | 1200 ms |
| shaken again within 5 s | `angry`, slanted brows and a short tremble | P1 | 1800 ms |
| charger attached | `happy` arcs and sparkles, then a battery gauge | P1 | 2000 ms |
| phone synced the clock | `acknowledge` nod, then `happy` | P1 | 450 ms |
| BLE window open | `connecting`, the eyes track a scanning line | P1 | max 10 s |
| left alone 30 s | `bored`, smaller eyes, wandering gaze, slow sighs | P2 | while idle |
| battery below 15 % | `sleepy`, half-closed lids | P2 | while low |
| left alone 5 min | `asleep`, eye slits and rising `z` | P2 | while idle |
| left alone longer | one self-directed activity: `act_fluid`, `act_stargaze` or `act_wander` | P3 | 7-12 s |

A higher priority interrupts a lower one immediately and is never pushed aside
by it. Any deliberate interaction cancels a P3 activity in the same tick.

`act_fluid` is the original FLIP particle animation. It was not removed - it
became one of the things the pet does when nobody is holding it.

### Interaction memory

The engine keeps a `bond` value of 0-100. Every deliberate interaction adds 5,
at most once per 5 seconds, and 3 points decay every 10 minutes. A higher bond
widens the eyes slightly, blinks a little more often, delays boredom and makes
self-directed activities start sooner - the pet becomes visibly livelier when
it is handled often, and calms down again when it is not.

The value lives in RAM only. It resets on every power cycle until settings
storage exists.

### Behaviour without an accelerometer

The Gerber PCB carries a LIS2DW12, but the firmware must stay alive before it
is soldered. With `sensor_present = false`:

- deep sleep is disabled, because nothing could wake the face up again and a
  frozen screen reads as a crash;
- self-directed activities are scheduled far more often (20-60 s instead of
  40-120 s), so the character keeps doing something visible;
- BLE advertising starts on its own, and the pet is told about it exactly once
  so the `connecting` animation plays and then gets out of the way.

Motion events simply never arrive. Nothing else in the chain changes, so
soldering the sensor later needs no firmware change.

### Not wired up yet

- `battery_percent` is always `PET_BATTERY_UNKNOWN`. `BAT_SENSE` reaches U4
  pin 7 on the PCB, but no ADC channel is configured, so the charging gauge
  animates instead of showing a real level and `sleepy` can never trigger.
- `ACC_INT1` (U4 pin 22) is routed on the PCB and the LIS2DW12 is configured to
  drive it, but no GPIO interrupt is declared in the devicetree.
- Charger detection uses the USB device status callback. A charger that never
  enumerates still raises VBUS, which is what the nRF USB driver reports, but
  this has not been confirmed against a dumb charger on real hardware.

## E73 pin mapping

Taken from the EBYTE E73-2G4M08S1C user manual and cross-checked against the
flying-probe netlist in `Gerber_PCBkey_cat.zip`. Every entry below agrees with
the netlist, which also confirms the I2C routing that was previously an
assumption.

| Module pin | E73 name | nRF52840 | Net on this PCB |
|---:|---|---|---|
| 7 | `AI0` | `P0.02` / `AIN0` | `BAT_SENSE` |
| 14 | `P0.06` | `P0.06` | `I2C_SCL` |
| 16 | `P0.08` | `P0.08` | `I2C_SDA` |
| 19 | `VDD` | — | `3V3` |
| 21, 24, 5 | `GND` | — | `GND` |
| 22 | `P0.07` | `P0.07` | `ACC_INT1` |
| 23 | `VDH` | `VDDH` | `3V3` |
| 26 | `RST` | `P0.18` / `RESET` | `RESET_N` |
| 27 | `VBS` | `VBUS` | `USB_5V` |
| 29 | `D-` | — | `USB_DN` |
| 31 | `D+` | — | `USB_DP` |
| 37 | `SWD` | `SWDIO` | `SWDIO` |
| 39 | `SWC` | `SWDCLK` | `SWDCLK` |

The two entries that are not used by the firmware yet are the ones worth
remembering: the battery divider lands on `AIN0`, so a battery gauge needs an
SAADC channel on `P0.02`, and the accelerometer interrupt is `P0.07`.

## Sensor behavior

The Gerber PCB contains a LIS2DW12 (`U5`) at address `0x18`, not an MPU-6050.
The firmware probes LIS2DW12 first and retains MPU-6050 (`0x68`) compatibility
for breadboard experiments. If neither device answers, startup continues:

- OLED displays a deterministic demonstration animation;
- BLE advertises immediately because triple-shake input is unavailable;
- OLED and sensor are reprobed every five seconds, so either can be attached
  after boot without reflashing or restarting the board.

The complete application loop is shared with the original product behavior:
FLUID, SLEEP, TIME, phone-started GAME, step counting, and phone time sync.
SLEEP is deliberately disabled while no real acceleration samples are
available, preventing an unplugged sensor from blanking or freezing the UI.

## Prototype wiring

Connect the OLED to the labelled XIAO pins:

| OLED | XIAO nRF52840 |
|---|---|
| `VCC` | `3V3` |
| `GND` | `GND` |
| `SDA` | `D4/SDA` |
| `SCL` | `D5/SCL` |

On the final Gerber PCB, connector `J2` already carries:

| J2 pin | Signal |
|---:|---|
| 1 | `GND` |
| 2 | `3V3` |
| 3 | `I2C_SCL` |
| 4 | `I2C_SDA` |

## Build

From a Zephyr workspace with this repository as the application:

```powershell
west build -p always -b xiao_ble/nrf52840 D:\key_chain\xiao_nrf52840
```

For the final E73 PCB:

```powershell
west build -p always -b nrf52840dk/nrf52840 D:\key_chain\xiao_nrf52840 `
  -- -DEXTRA_CONF_FILE=D:/key_chain/xiao_nrf52840/gerber.conf
```

For USB flashing now with the final Gerber I2C pins already selected:

```powershell
west build -p always -b xiao_ble/nrf52840 `
  D:\key_chain\xiao_nrf52840 -d D:\key_chain\xiao_nrf52840\build-xiao-gerber `
  -- -DDTC_OVERLAY_FILE=D:/key_chain/xiao_nrf52840/boards/xiao_ble_nrf52840_gerber.overlay `
     -DEXTRA_CONF_FILE=D:/key_chain/xiao_nrf52840/gerber.conf
```

## Flashing

For XIAO, double-press RESET and copy `build/zephyr/zephyr.uf2` to the `XIAO-SENSE`
USB drive. The volume can have a similar XIAO bootloader name on non-Sense
boards.

If Windows exposes only a bootloader COM port, make a Serial DFU package for
the factory S140 7.3.0 bootloader and upload it in single-bank mode:

```powershell
adafruit-nrfutil dfu genpkg --dev-type 0x0052 --sd-req 0x0123 `
  --application build-xiao\zephyr\zephyr.hex keychain_xiao_dfu.zip
adafruit-nrfutil dfu serial -pkg keychain_xiao_dfu.zip `
  -p COM16 -b 115200 --singlebank
```

The E73 module is shipped without this XIAO UF2 bootloader. Its first firmware
must be programmed through Gerber connector `J3`: `3V3`, `SWDIO`, `SWDCLK`,
`RESET_N`, and `GND`.

## Updating the E73 board over USB-C

After a one-time SWD install of MCUboot, the board updates through its own
USB-C connector and the debug probe is only needed to change the bootloader or
to recover an unbootable board.

```text
0x000000  mcuboot   64 KB
0x010000  image-0  448 KB   the running application
0x080000  image-1  448 KB
0x0F0000  storage   64 KB
```

The map lives in [dts/e73_partitions.dtsi](dts/e73_partitions.dtsi) and is
included by both the bootloader and the application, because the two must agree
on it exactly. The stock 48 KB boot partition was too small: serial recovery
over USB costs about 58 KB.

This PCB has no RESET button, so DFU is not entered by holding anything. The
application writes a boot-mode flag into `GPREGRET`, which survives the reset,
and MCUboot reads it and starts serial recovery instead of booting. The flag is
set by the same 1200-baud touch used on XIAO, so one gesture works on both
targets - see [usb_dfu_touch.c](src/usb_dfu_touch.c).

Adding the CDC node also gives the E73 build a console it never had: `pet:`
transition logs now come out of the board's own USB-C connector.

Build and install:

```powershell
python tools/flash/flash_nrf52840.py bootstrap   # one time, over SWD
python tools/flash/usb_update.py                 # every update after that
```

The image is signed with MCUboot's published development key. It is loadable,
not trustworthy; generate a private key before shipping. Details and the exact
build commands are in [tools/flash/README.md](../tools/flash/README.md).
