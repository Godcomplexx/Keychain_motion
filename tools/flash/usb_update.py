#!/usr/bin/env python3
"""
Update the keychain over its own USB-C connector, without a debug probe.

    python tools/flash/usb_update.py
    python tools/flash/usb_update.py --build build-e73-mcuboot --confirm

The sequence is:

  1. find the running application's CDC port and open it at 1200 baud;
     the firmware treats that as "please reboot into the bootloader" and asks
     MCUboot for serial recovery through the retained GPREGRET register;
  2. wait for MCUboot's own CDC port to enumerate;
  3. hand the signed image to smpmgr, which uploads it and resets the board.

The two ports are told apart by USB product ID, so nothing has to be guessed:
the application answers on PID 0x8044 and MCUboot on PID 0x8045.

Serial recovery writes slot0 in place, so the upload is permanent immediately
and there is no rollback: a broken image needs SWD to recover.
"""
import argparse
import os
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

TOUCH_BAUD_RATE = 1200


def find_port(pid, timeout_s=0.0):
    """Return the COM port exposing the given product ID, or None."""
    deadline = time.time() + timeout_s
    while True:
        for info in list_ports.comports():
            if info.vid == USB_VID and info.pid == pid:
                return info.device
        if time.time() >= deadline:
            return None
        time.sleep(0.25)


def touch_1200_baud(port):
    """Open and close the port at 1200 baud to request the bootloader."""
    try:
        with serial.Serial(port, TOUCH_BAUD_RATE, timeout=0.2, dsrdtr=False):
            time.sleep(0.15)
    except serial.SerialException:
        # The board reboots while the port is open, so a disconnect error here
        # means the request was delivered rather than that it failed.
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default="build-e73-mcuboot",
                        help="build directory under xiao_nrf52840")
    parser.add_argument("--slot", type=int, default=0, choices=(0, 1),
                        help="0 writes the running slot in place - immediate "
                             "and permanent. 1 uploads to the spare slot and "
                             "lets MCUboot test-swap it, so an image that "
                             "cannot boot is rolled back automatically")
    parser.add_argument("--confirm", action="store_true",
                        help="with --slot 1, mark the image permanent up front "
                             "instead of letting the application confirm itself "
                             "once it has started successfully")
    parser.add_argument("--wait", type=float, default=20.0,
                        help="seconds to wait for the bootloader port")
    args = parser.parse_args()

    image = os.path.join(PORT_DIR, args.build, "zephyr", "zephyr.signed.bin")
    if not os.path.isfile(image):
        raise SystemExit("no signed image at %s - build it first" % image)

    boot_port = find_port(BOOTLOADER_PID)
    if boot_port:
        print("bootloader already waiting on", boot_port)
    else:
        app_port = find_port(APP_PID)
        if app_port is None:
            # A powered-down board and an unplugged cable look identical from
            # here, and SW2 cuts the regulator while the charger stays live, so
            # a charging board can still be invisible. Name both causes.
            raise SystemExit(
                "no keychain found on USB - check that the board's USB-C cable "
                "is connected and that the power switch SW2 is on "
                "(the application enumerates as a COM port)")
        print("application on %s; requesting bootloader ..." % app_port)
        touch_1200_baud(app_port)

        boot_port = find_port(BOOTLOADER_PID, timeout_s=args.wait)
        if boot_port is None:
            raise SystemExit(
                "bootloader did not appear within %.0f s - the board may have "
                "booted the application again" % args.wait)
        print("bootloader on", boot_port)

    command = [sys.executable, "-m", "smpmgr", "--port", boot_port,
               "upgrade", image, "--slot", str(args.slot)]
    if args.confirm:
        command.append("--confirm")
    print("running:", " ".join(command))
    raise SystemExit(subprocess.call(command))


if __name__ == "__main__":
    main()
