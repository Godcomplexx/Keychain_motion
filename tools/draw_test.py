#!/usr/bin/env python3
"""
Draw a test pattern on the keychain from a PC, without the phone.

    pip install bleak
    python tools/draw_test.py

Useful because the drawing pad has three separate places to fail - the app, the
GATT transport and the firmware - and this removes the first one from the
picture. If the pattern appears, the keychain and the protocol are fine and the
problem is in the app.

It also prints the discovered characteristics, which is the quickest way to see
whether the firmware on the board is old enough to lack the drawing
characteristic entirely.
"""

import argparse
import asyncio
import math

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "KeychainSync"
SERVICE_UUID = "11223344-5566-7788-9a49-315b10371342"
COMMAND_UUID = "11223344-5566-7788-9a49-315b10371343"
DRAW_UUID = "11223344-5566-7788-9a49-315b10371344"

# Shared with components/draw_pad/include/draw_pad.h.
OP_CONTINUE = 0x10
OP_BEGIN = 0x11
OP_CLEAR = 0x20
OP_PEN = 0x21

WIDTH = 128
HEIGHT = 64
# Two bytes per point plus the opcode, kept inside the default 20-byte payload
# so the pattern arrives even if MTU negotiation does not happen.
POINTS_PER_PACKET = 60


async def send_stroke(client, points):
    """One stroke: the first packet puts the pen down, the rest continue it."""
    first = True
    for start in range(0, len(points), POINTS_PER_PACKET):
        chunk = points[start : start + POINTS_PER_PACKET]
        payload = bytearray([OP_BEGIN if first else OP_CONTINUE])
        for x, y in chunk:
            payload += bytes([x, y])
        await client.write_gatt_char(DRAW_UUID, bytes(payload), response=False)
        first = False


def wave():
    return [
        (
            x,
            max(
                0,
                min(
                    HEIGHT - 1,
                    int(HEIGHT / 2 + 24 * math.sin(x / WIDTH * 4 * math.pi)),
                ),
            ),
        )
        for x in range(WIDTH)
    ]


def border():
    return (
        [(x, 4) for x in range(4, WIDTH - 4)]
        + [(WIDTH - 5, y) for y in range(4, HEIGHT - 4)]
        + [(x, HEIGHT - 5) for x in range(WIDTH - 5, 3, -1)]
        + [(4, y) for y in range(HEIGHT - 5, 3, -1)]
    )


async def main(hold_seconds, pen_radius):
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=25.0)
    if device is None:
        raise SystemExit(
            "%s not found. The keychain advertises whenever it is not playing "
            "Breakout, so check that it is powered and not mid-game."
            % DEVICE_NAME
        )

    print("found %s at %s" % (DEVICE_NAME, device.address))
    async with BleakClient(device) as client:
        has_draw = False
        for service in client.services:
            for characteristic in service.characteristics:
                print(
                    "  %s  %s"
                    % (
                        characteristic.uuid,
                        ",".join(characteristic.properties),
                    )
                )
                if characteristic.uuid.lower() == DRAW_UUID:
                    has_draw = True

        if not has_draw:
            raise SystemExit(
                "No drawing characteristic. This firmware predates the "
                "drawing pad; reflash with tools/flash/usb_update.py."
            )

        await client.write_gatt_char(
            COMMAND_UUID, b"DRAW:START", response=True
        )
        await asyncio.sleep(0.3)
        await client.write_gatt_char(
            DRAW_UUID, bytes([OP_CLEAR]), response=False
        )
        await client.write_gatt_char(
            DRAW_UUID, bytes([OP_PEN, pen_radius]), response=False
        )

        await send_stroke(client, wave())
        # A second stroke, so a missing pen lift shows up as a line joining the
        # end of the wave to the corner of the box.
        await send_stroke(client, border())
        print("pattern sent; holding for %g s" % hold_seconds)

        await asyncio.sleep(hold_seconds)
        await client.write_gatt_char(COMMAND_UUID, b"DRAW:STOP", response=True)
        print("done")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--hold",
        type=float,
        default=10.0,
        help="seconds to leave the pattern on screen",
    )
    parser.add_argument(
        "--pen",
        type=int,
        default=1,
        choices=range(0, 4),
        help="pen radius in pixels",
    )
    arguments = parser.parse_args()
    asyncio.run(main(arguments.hold, arguments.pen))
