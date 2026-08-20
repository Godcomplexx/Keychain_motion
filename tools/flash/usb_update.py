#!/usr/bin/env python3
"""
Update the keychain over its own USB-C connector, without a debug probe.

    python tools/flash/usb_update.py                    # normal, revertible
    python tools/flash/usb_update.py --via-bootloader   # recovery route
    python tools/flash/usb_update.py --confirm          # skip the trial boot

There are two routes, and they behave differently:

  through the running application (default)
      The application exposes an SMP server on a second CDC port. The image
      goes into the spare slot as a *test* image, MCUboot swaps it in, and the
      application confirms itself once it has started successfully. An image
      that cannot get that far is put back by MCUboot at the next reset.

  through the bootloader (--via-bootloader)
      Opening the console port at 1200 baud asks the application to reboot into
      MCUboot serial recovery. That path keeps no slot state, so whatever is
      uploaded becomes permanent immediately - there is no way back except SWD.
      It exists for when the application no longer runs.

Both CDC ports of the application share one USB VID/PID, so they are told apart
by USB interface number: MI_00 is the console, MI_02 is the SMP server. The
bootloader is a different product ID entirely.
"""
import argparse
import os
import re
import subprocess
import sys
import time

import serial
from serial.tools import list_ports

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PORT_DIR = os.path.join(REPO_ROOT, "xiao_nrf52840")

USB_VID = 0x2886
APP_PID = 0x8044
BOOTLOADER_PID = 0x8045

CONSOLE_INTERFACE = 0
SMP_INTERFACE = 2

TOUCH_BAUD_RATE = 1200


def interface_number(info):
    """
    Which USB interface a port belongs to.

    Windows spells this as MI_02 inside the hardware id, but only once the
    composite device has been enumerated that way; pyserial otherwise exposes
    it as the trailing component of the location, as in "1-1:x.2". Both spellings
    show up on the same machine, so both are accepted.
    """
    match = re.search(r"MI_(\d+)", (info.hwid or "").upper())
    if match:
        return int(match.group(1))
    match = re.search(r"[.:](\d+)$", info.location or "")
    if match:
        return int(match.group(1))
    return None


def find_port(pid, interface=None, timeout_s=0.0):
    """Return a matching COM port, optionally narrowed by USB interface."""
    deadline = time.time() + timeout_s
    while True:
        for info in list_ports.comports():
            if info.vid != USB_VID or info.pid != pid:
                continue
            if interface is not None and interface_number(info) != interface:
                continue
            return info.device
        if time.time() >= deadline:
            return None
        time.sleep(0.25)


def touch_1200_baud(port):
    """Open and close the console port at 1200 baud to request the bootloader."""
    try:
        with serial.Serial(port, TOUCH_BAUD_RATE, timeout=0.2, dsrdtr=False):
            time.sleep(0.15)
    except serial.SerialException:
        # The board reboots while the port is open, so a disconnect error here
        # means the request was delivered rather than that it failed.
        pass


def run_smpmgr(port, image, slot, confirm):
    command = [sys.executable, "-m", "smpmgr", "--port", port,
               "upgrade", image, "--slot", str(slot)]
    if confirm:
        command.append("--confirm")
    print("running:", " ".join(command))
    return subprocess.call(command)


def update_via_application(image, confirm):
    smp_port = find_port(APP_PID, SMP_INTERFACE)
    if smp_port is None:
        return None

    print("application SMP server on", smp_port)
    if confirm:
        print("uploading as a confirmed image - no trial boot, no revert")
    else:
        print("uploading to the spare slot as a test image; "
              "the application confirms itself if it starts")
    return run_smpmgr(smp_port, image, slot=1, confirm=confirm)


def update_via_bootloader(image, wait_s):
    boot_port = find_port(BOOTLOADER_PID)
    if boot_port is None:
        console = find_port(APP_PID, CONSOLE_INTERFACE)
        if console is None:
            raise SystemExit(
                "no keychain found on USB - check that the board's USB-C cable "
                "is connected and that the power switch SW2 is on "
                "(the application enumerates as two COM ports)")
        print("application console on %s; requesting bootloader ..." % console)
        touch_1200_baud(console)

        boot_port = find_port(BOOTLOADER_PID, timeout_s=wait_s)
        if boot_port is None:
            raise SystemExit(
                "bootloader did not appear within %.0f s - the board may have "
                "booted the application again" % wait_s)

    print("bootloader on", boot_port)
    print("NOTE: this route writes in place; the image cannot be reverted")
    return run_smpmgr(boot_port, image, slot=0, confirm=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default="build-e73-mcuboot",
                        help="build directory under xiao_nrf52840")
    parser.add_argument("--via-bootloader", action="store_true",
                        help="use MCUboot serial recovery instead of the "
                             "application; permanent, for when the application "
                             "no longer starts")
    parser.add_argument("--confirm", action="store_true",
                        help="mark the image permanent up front instead of "
                             "letting it prove itself on a trial boot")
    parser.add_argument("--wait", type=float, default=20.0,
                        help="seconds to wait for the bootloader port")
    args = parser.parse_args()

    image = os.path.join(PORT_DIR, args.build, "zephyr", "zephyr.signed.bin")
    if not os.path.isfile(image):
        raise SystemExit("no signed image at %s - build it first" % image)

    if not args.via_bootloader:
        result = update_via_application(image, args.confirm)
        if result is not None:
            raise SystemExit(result)
        print("no SMP port found on the application; "
              "falling back to the bootloader route")

    raise SystemExit(update_via_bootloader(image, args.wait))


if __name__ == "__main__":
    main()
