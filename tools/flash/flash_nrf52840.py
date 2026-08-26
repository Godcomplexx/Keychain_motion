#!/usr/bin/env python3
"""
Program and inspect the E73 keychain PCB over SWD with an ST-Link.

    python tools/flash/flash_nrf52840.py info
    python tools/flash/flash_nrf52840.py flash [--build build-e73]
    python tools/flash/flash_nrf52840.py soak [--seconds 75]

`info` is read-only and answers the two questions that decide everything else:
is the chip locked, and is there a bootloader. `flash` writes the image and
verifies the reset vector actually changed. `soak` lets the board run and
watches for a Zephyr fatal error.

Never halt the core while the BLE controller is running unless you are about to
reprogram it. The controller services the radio against hard deadlines, and a
debugger halt makes it miss one and assert - which looks exactly like a firmware
crash but is caused by the debugger.
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Imported for its side effect, and it has to run before pyocd is imported.
# Imported for its side effect, so every linter that looks only at names
# calls it unused: it patches pyOCD so an ST-Link can reach Nordic's
# CTRL-AP. Removing it breaks SWD flashing entirely.
import stlink_ctrl_ap_fix  # noqa: E402,F401  (patches pyOCD for CTRL-AP access)

from pyocd.core.helpers import ConnectHelper  # noqa: E402
from pyocd.flash.eraser import FlashEraser  # noqa: E402
from pyocd.flash.file_programmer import FileProgrammer  # noqa: E402

REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..")
)
PORT_DIR = os.path.join(REPO_ROOT, "xiao_nrf52840")

TARGET = "nrf52840"

# nRF52840 registers used below.
FICR_INFO_PART = 0x10000100
FICR_INFO_VARIANT = 0x10000104
FICR_DEVICEID0 = 0x10000060
FICR_DEVICEID1 = 0x10000064
UICR_BOOTLOADERADDR = 0x10001014
POWER_USBREGSTATUS = 0x40000438

# Flash map from xiao_nrf52840/dts/e73_partitions.dtsi.
SLOT0_ADDRESS = 0x00010000
MCUBOOT_HEADER_SIZE = 0x200
MCUBOOT_IMAGE_MAGIC = 0x96F3B83D

# Zephyr fatal-error entry points, resolved from the ELF at run time.
FATAL_SYMBOLS = ("z_fatal_error", "assert_post_action", "z_arm_fault")


def open_session():
    session = ConnectHelper.session_with_chosen_probe(
        target_override=TARGET, blocking=False, options={"auto_unlock": False}
    )
    if session is None:
        raise SystemExit(
            "no debug probe found - connect the ST-Link and check that J3 is "
            "wired (3V3, SWDIO, SWDCLK, RESET_N, GND)"
        )
    return session


def image_path(build_dir, signed=False):
    name = "zephyr.signed.hex" if signed else "zephyr.hex"
    path = os.path.join(PORT_DIR, build_dir, "zephyr", name)
    if not os.path.isfile(path):
        raise SystemExit("no image at %s - build it first" % path)
    return path


def elf_path(build_dir):
    return os.path.join(PORT_DIR, build_dir, "zephyr", "zephyr.elf")


def read_fatal_symbols(build_dir):
    """Resolve fatal-error handler addresses from the ELF symbol table."""
    elf = elf_path(build_dir)
    if not os.path.isfile(elf):
        return {}
    try:
        from elftools.elf.elffile import ELFFile
    except ImportError:
        return {}

    found = {}
    with open(elf, "rb") as handle:
        for section in ELFFile(handle).iter_sections():
            if not hasattr(section, "iter_symbols"):
                continue
            for symbol in section.iter_symbols():
                if symbol.name in FATAL_SYMBOLS:
                    # Clear the Thumb bit so the address matches the breakpoint.
                    found[symbol.name] = symbol["st_value"] & ~1
    return found


def cmd_info(args):
    with open_session() as session:
        target = session.target
        print("target      :", target.part_number)
        print("FICR PART   : 0x%08X" % target.read32(FICR_INFO_PART))
        print("FICR VARIANT: 0x%08X" % target.read32(FICR_INFO_VARIANT))
        print(
            "DEVICEID    : %08X%08X"
            % (target.read32(FICR_DEVICEID1), target.read32(FICR_DEVICEID0))
        )

        # Only SoftDevice-style bootloaders (Adafruit UF2) record themselves
        # here. MCUboot lives at address 0 and leaves this word erased.
        boot = target.read32(UICR_BOOTLOADERADDR)
        print(
            "UICR BOOTLOADERADDR: 0x%08X%s"
            % (boot, "  (no UF2 bootloader)" if boot == 0xFFFFFFFF else "")
        )

        vbus = target.read32(POWER_USBREGSTATUS) & 1
        print("VBUS detected      :", bool(vbus))

        print(
            "reset vector: 0x%08X  initial SP: 0x%08X"
            % (target.read32(0x04), target.read32(0x00))
        )

        # SCB->VTOR says which vector table the core is actually using, which
        # is the only reliable way to tell MCUboot and the application apart.
        vtor = target.read32(0xE000ED08)
        if vtor == 0:
            running = "bootloader (or no MCUboot installed)"
        elif vtor == SLOT0_ADDRESS + MCUBOOT_HEADER_SIZE:
            running = "application in slot0"
        else:
            running = "unknown"
        print("SCB->VTOR   : 0x%08X  -> %s" % (vtor, running))

        magic = target.read32(SLOT0_ADDRESS)
        print(
            "slot0 header: 0x%08X%s"
            % (
                magic,
                "  (MCUboot image)" if magic == MCUBOOT_IMAGE_MAGIC else "",
            )
        )


def cmd_bootstrap(args):
    """One-time install: erase, write MCUboot, write the signed application.

    After this the board updates over USB-C and only needs SWD again if the
    bootloader itself changes or an update leaves the board unbootable.
    """
    boot_hex = image_path(args.boot_build)
    app_hex = image_path(args.app_build, signed=True)

    with open_session() as session:
        target = session.target
        target.reset_and_halt()

        # A chip erase avoids leaving stale bytes in the slot0 image trailer,
        # which MCUboot would read as a half-finished swap.
        print("erasing chip ...")
        FlashEraser(session, FlashEraser.Mode.CHIP).erase()

        print("writing bootloader:", boot_hex)
        FileProgrammer(session).program(boot_hex, file_format="hex")

        print("writing application:", app_hex)
        FileProgrammer(session).program(app_hex, file_format="hex")

        print("bootloader vector : 0x%08X" % target.read32(0x04))
        header = target.read32(SLOT0_ADDRESS)
        print(
            "slot0 image magic : 0x%08X%s"
            % (
                header,
                "  (MCUboot header present)"
                if header == MCUBOOT_IMAGE_MAGIC
                else "  UNEXPECTED",
            )
        )

        target.reset()
        print("reset; device running")


def cmd_flash(args):
    hex_file = image_path(args.build, signed=args.signed)
    # A signed image lands in slot0 behind the MCUboot header; address 4 then
    # belongs to the bootloader and never changes, so watch the slot instead.
    vector_address = (
        (SLOT0_ADDRESS + MCUBOOT_HEADER_SIZE + 4) if args.signed else 0x04
    )
    with open_session() as session:
        target = session.target
        before = target.read32(vector_address)
        print("image  :", hex_file)
        print(
            "reset vector before: 0x%08X (at 0x%08X)"
            % (before, vector_address)
        )

        # The running application drives BLE and USB. It must not execute while
        # the flash algorithm runs from RAM, or the algorithm hard-faults.
        target.reset_and_halt()

        FileProgrammer(session).program(hex_file, file_format="hex")

        after = target.read32(vector_address)
        print("reset vector after : 0x%08X" % after)
        if after == before:
            print("WARNING: reset vector did not change - is this image new?")

        target.reset()
        print("reset; device running")


def cmd_soak(args):
    symbols = read_fatal_symbols(args.build)
    with open_session() as session:
        target = session.target
        target.reset_and_halt()

        if symbols:
            for name, addr in sorted(symbols.items()):
                target.set_breakpoint(addr)
                print("watching %-20s @0x%08X" % (name, addr))
        else:
            print("pyelftools missing or no ELF: watching for halt only")

        target.resume()

        start = time.time()
        crashed = False
        while time.time() - start < args.seconds:
            time.sleep(5.0)
            elapsed = time.time() - start
            if target.get_state().name == "HALTED":
                print(
                    "%5.1fs  FATAL: stopped at PC=0x%08X"
                    % (elapsed, target.read_core_register("pc"))
                )
                crashed = True
                break
            print("%5.1fs  running" % elapsed)

        for addr in symbols.values():
            target.remove_breakpoint(addr)

        if crashed:
            raise SystemExit(1)
        print("no fatal error in %d s" % args.seconds)
        target.reset()


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="command", required=True)

    info = sub.add_parser("info", help="read chip identity and lock state")
    info.set_defaults(func=cmd_info)

    flash = sub.add_parser("flash", help="program an image over SWD")
    flash.add_argument(
        "--build",
        default="build-e73",
        help="build directory under xiao_nrf52840 (default: build-e73)",
    )
    flash.add_argument(
        "--signed",
        action="store_true",
        help="program zephyr.signed.hex instead of zephyr.hex",
    )
    flash.set_defaults(func=cmd_flash)

    boot = sub.add_parser(
        "bootstrap", help="one-time: erase and install MCUboot + signed app"
    )
    boot.add_argument("--boot-build", default="build-mcuboot")
    boot.add_argument("--app-build", default="build-e73-mcuboot")
    boot.set_defaults(func=cmd_bootstrap)

    soak = sub.add_parser(
        "soak", help="run the image and watch for fatal errors"
    )
    soak.add_argument("--build", default="build-e73")
    soak.add_argument("--seconds", type=int, default=75)
    soak.set_defaults(func=cmd_soak)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
