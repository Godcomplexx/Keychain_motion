#!/usr/bin/env python3
"""
Turn the cat artwork into the Android launcher icon set.

    pip install pillow
    python tools/make_app_icon.py

The source art is transparent and sits off-centre in its canvas - its content
occupies roughly the top-left half - so it cannot simply be dropped in as an
icon. This crops to the artwork's own bounds, re-centres it, and writes every
size Android asks for.

Two families come out of it:

  * an adaptive icon (the only one Android 8 and later actually shows), whose
    foreground keeps the artwork inside the 72-of-108 safe zone the launcher
    mask can crop to;
  * legacy square and round PNGs, for launchers and surfaces that still ask
    for them.

Re-run it after replacing the source file to regenerate everything.
"""

import argparse
import os

from PIL import Image, ImageDraw

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SOURCE = os.path.join(REPO_ROOT, "docs", "assets", "icon-512.png")
RES = os.path.join(REPO_ROOT, "android_time_sync", "app", "src", "main", "res")

# The cream of the app's panels, so the icon and the interface agree.
BACKGROUND = (234, 230, 217, 255)

DENSITIES = {
    "mdpi": 1.0,
    "hdpi": 1.5,
    "xhdpi": 2.0,
    "xxhdpi": 3.0,
    "xxxhdpi": 4.0,
}

LEGACY_BASE_DP = 48
ADAPTIVE_BASE_DP = 108
# 72 of 108 is the part of an adaptive icon guaranteed to survive the mask.
# A little under that keeps the ears clear of a circular crop.
ADAPTIVE_CONTENT = 0.60
LEGACY_CONTENT = 0.72


def artwork():
    """The source cropped to its own ink, so padding is ours to decide."""
    image = Image.open(SOURCE).convert("RGBA")
    bounds = image.getchannel("A").getbbox()
    if bounds is None:
        raise SystemExit("%s is fully transparent" % SOURCE)
    return image.crop(bounds)


def centred(content, canvas_size, fraction):
    """Fit the artwork into a square canvas at the given share of its width."""
    target = int(round(canvas_size * fraction))
    scale = target / max(content.size)
    scaled = content.resize(
        (
            max(1, int(round(content.width * scale))),
            max(1, int(round(content.height * scale))),
        ),
        Image.LANCZOS,
    )

    canvas = Image.new("RGBA", (canvas_size, canvas_size), (0, 0, 0, 0))
    canvas.paste(
        scaled,
        (
            (canvas_size - scaled.width) // 2,
            (canvas_size - scaled.height) // 2,
        ),
        scaled,
    )
    return canvas


def rounded_square(size, radius_fraction=0.22):
    plate = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(plate).rounded_rectangle(
        (0, 0, size - 1, size - 1),
        radius=int(size * radius_fraction),
        fill=BACKGROUND,
    )
    return plate


def circle(size):
    plate = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(plate).ellipse((0, 0, size - 1, size - 1), fill=BACKGROUND)
    return plate


def write(image, folder, name):
    directory = os.path.join(RES, folder)
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, name)
    image.save(path, "PNG", optimize=True)
    print(
        "  %s (%dx%d)"
        % (os.path.relpath(path, REPO_ROOT), image.width, image.height)
    )


def main():
    content = artwork()
    print("source content %dx%d" % content.size)

    for density, scale in DENSITIES.items():
        folder = "mipmap-" + density

        adaptive = int(round(ADAPTIVE_BASE_DP * scale))
        write(
            centred(content, adaptive, ADAPTIVE_CONTENT),
            folder,
            "ic_launcher_foreground.png",
        )

        legacy = int(round(LEGACY_BASE_DP * scale))
        square = rounded_square(legacy)
        square.alpha_composite(centred(content, legacy, LEGACY_CONTENT))
        write(square, folder, "ic_launcher.png")

        round_plate = circle(legacy)
        round_plate.alpha_composite(centred(content, legacy, LEGACY_CONTENT))
        write(round_plate, folder, "ic_launcher_round.png")

    print("done - the XML in res/mipmap-anydpi-v26 points at these")


if __name__ == "__main__":
    argparse.ArgumentParser(description=__doc__).parse_args()
    main()
