#!/usr/bin/env python3
"""
Build both images for the E73 keychain board: MCUboot and the signed
application.

    python tools/flash/build_e73.py
    python tools/flash/build_e73.py --pristine

Why a script rather than two documented CMake lines: the MCUboot signing key
lives in this repository but is not committed, and MCUboot only resolves a
relative key path against a file passed as CONF_FILE - not EXTRA_CONF_FILE.
Passing an absolute path computed here avoids both a hardcoded machine path in
a committed file and a silent fallback to MCUboot's published default key.

Needs ZEPHYR_BASE set, or a Zephyr workspace next to it.
"""
import argparse
import os
import shutil
import subprocess
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PORT_DIR = os.path.join(REPO_ROOT, "xiao_nrf52840")
MCUBOOT_CONF = os.path.join(PORT_DIR, "mcuboot", "e73.conf")
MCUBOOT_OVERLAY = os.path.join(PORT_DIR, "mcuboot", "e73.overlay")
APP_CONF = os.path.join(PORT_DIR, "gerber.conf")
APP_MCUBOOT_CONF = os.path.join(PORT_DIR, "mcuboot", "app.conf")
APP_OVERLAY = os.path.join(PORT_DIR, "boards", "nrf52840dk_nrf52840.overlay")
SIGNING_KEY = os.path.join(PORT_DIR, "mcuboot", "keychain-dev-rsa-2048.pem")

BOARD = "nrf52840dk/nrf52840"
BOOT_BUILD = os.path.join(PORT_DIR, "build-mcuboot")
APP_BUILD = os.path.join(PORT_DIR, "build-e73-mcuboot")


def posix(path):
    return path.replace("\\", "/")


def zephyr_base():
    base = os.environ.get("ZEPHYR_BASE")
    if not base:
        raise SystemExit("ZEPHYR_BASE is not set")
    return os.path.abspath(base)


def mcuboot_source():
    path = os.path.join(os.path.dirname(zephyr_base()), "bootloader", "mcuboot",
                        "boot", "zephyr")
    if not os.path.isdir(path):
        raise SystemExit("MCUboot not found at %s - run `west update mcuboot`"
                         % path)
    return path


def cmake():
    found = shutil.which("cmake")
    if found:
        return found
    raise SystemExit("cmake is not on PATH")


def run(command, what):
    print("\n--- %s" % what)
    result = subprocess.call(command)
    if result != 0:
        raise SystemExit("%s failed with %d" % (what, result))


def configure_and_build(build_dir, source_dir, defines, what, pristine):
    if pristine and os.path.isdir(build_dir):
        shutil.rmtree(build_dir, ignore_errors=True)

    command = [cmake(), "-B", build_dir, "-S", source_dir, "-GNinja",
               "-DBOARD=" + BOARD] + defines
    run(command, "configure " + what)
    run([cmake(), "--build", build_dir], "build " + what)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pristine", action="store_true",
                       help="delete the build directories first")
    args = parser.parse_args()

    if not os.path.isfile(SIGNING_KEY):
        raise SystemExit(
            "signing key missing: %s\ngenerate it with:\n"
            "  python <mcuboot>/scripts/imgtool.py keygen -k %s -t rsa-2048\n"
            "then reinstall the bootloader over SWD, because the public half "
            "is compiled into it" % (SIGNING_KEY, SIGNING_KEY))

    configure_and_build(
        BOOT_BUILD, mcuboot_source(),
        ["-DEXTRA_CONF_FILE=" + posix(MCUBOOT_CONF),
         "-DDTC_OVERLAY_FILE=" + posix(MCUBOOT_OVERLAY),
         "-DCONFIG_BOOT_SIGNATURE_KEY_FILE=\"" + posix(SIGNING_KEY) + "\""],
        "MCUboot", args.pristine)

    configure_and_build(
        APP_BUILD, PORT_DIR,
        ["-DDTC_OVERLAY_FILE=" + posix(APP_OVERLAY),
         "-DEXTRA_CONF_FILE=" + posix(APP_CONF) + ";" + posix(APP_MCUBOOT_CONF)],
        "application", args.pristine)

    print("\nbootloader : %s" % os.path.join(BOOT_BUILD, "zephyr", "zephyr.hex"))
    print("application: %s"
          % os.path.join(APP_BUILD, "zephyr", "zephyr.signed.hex"))
    print("\nnext: python tools/flash/flash_nrf52840.py bootstrap")


if __name__ == "__main__":
    main()
