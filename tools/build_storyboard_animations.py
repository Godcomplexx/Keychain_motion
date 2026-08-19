"""Build nine animated GIF previews from the supplied character storyboard.

The source image contains a baked-in checkerboard background, so this script
also extracts that checkerboard into a transparent background.
"""

from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter


@dataclass(frozen=True)
class AnimationSpec:
    """Coordinates and playback timing for one storyboard row."""

    name: str
    y_range: tuple[int, int]
    x_ranges: tuple[tuple[int, int], ...]
    duration_ms: int


# The boxes follow the nine visible rows in the supplied 1136 x 1385 image.
ANIMATIONS = (
    AnimationSpec("01_idle", (22, 180), ((25, 160), (173, 303), (315, 445), (458, 590), (603, 732)), 350),
    AnimationSpec("02_run_right", (195, 343), ((25, 154), (160, 292), (298, 430), (444, 570), (585, 712), (725, 852)), 160),
    AnimationSpec("03_run_left", (358, 500), ((40, 162), (183, 310), (325, 452), (466, 594), (610, 734), (751, 877)), 160),
    AnimationSpec("04_wave", (519, 669), ((22, 151), (166, 300), (312, 443), (456, 586)), 240),
    AnimationSpec("05_jump", (677, 823), ((28, 148), (174, 302), (317, 447), (463, 580), (604, 720)), 200),
    AnimationSpec(
        "06_board_reaction",
        (841, 970),
        ((30, 145), (184, 300), (326, 460), (475, 628), (629, 735), (748, 864), (876, 990), (1005, 1120)),
        260,
    ),
    AnimationSpec("07_board_work", (983, 1105), ((28, 145), (181, 300), (329, 443), (471, 586), (617, 740), (754, 875)), 260),
    AnimationSpec("08_soldering", (1118, 1239), ((19, 151), (177, 308), (318, 449), (465, 594), (610, 738), (746, 877)), 220),
    AnimationSpec("09_inspection", (1250, 1368), ((29, 143), (184, 298), (328, 441), (470, 581), (618, 731), (756, 885)), 260),
)


def remove_checkerboard(frame: Image.Image) -> Image.Image:
    """Make the pale neutral checkerboard connected to the crop edge transparent."""

    rgba = frame.convert("RGBA")
    pixels = rgba.load()
    visited = bytearray(rgba.width * rgba.height)
    queue: deque[tuple[int, int]] = deque()

    def is_background(x: int, y: int) -> bool:
        red, green, blue, _ = pixels[x, y]
        # The original checkerboard is bright and almost neutral.
        # Keep pale green/white goggle pixels; the baked checkerboard itself is
        # brighter and much more neutral than the translucent plastic.
        return min(red, green, blue) > 238 and max(red, green, blue) - min(red, green, blue) < 10

    def add_if_background(x: int, y: int) -> None:
        index = y * rgba.width + x
        if not visited[index] and is_background(x, y):
            visited[index] = 1
            queue.append((x, y))

    # Flood-filling from every edge avoids deleting enclosed white highlights.
    for x in range(rgba.width):
        add_if_background(x, 0)
        add_if_background(x, rgba.height - 1)
    for y in range(rgba.height):
        add_if_background(0, y)
        add_if_background(rgba.width - 1, y)

    while queue:
        x, y = queue.popleft()
        pixels[x, y] = (*pixels[x, y][:3], 0)
        if x > 0:
            add_if_background(x - 1, y)
        if x + 1 < rgba.width:
            add_if_background(x + 1, y)
        if y > 0:
            add_if_background(x, y - 1)
        if y + 1 < rgba.height:
            add_if_background(x, y + 1)
    return rgba


def remove_small_neutral_islands(frame: Image.Image) -> Image.Image:
    """Remove tiny bright checkerboard crumbs without deleting colored effects."""

    cleaned = frame.copy()
    pixels = cleaned.load()
    width, height = cleaned.size
    visited = bytearray(width * height)

    for start_y in range(height):
        for start_x in range(width):
            start_index = start_y * width + start_x
            if visited[start_index] or pixels[start_x, start_y][3] == 0:
                continue

            visited[start_index] = 1
            queue: deque[tuple[int, int]] = deque([(start_x, start_y)])
            component: list[tuple[int, int]] = []
            red_sum = green_sum = blue_sum = 0
            while queue:
                x, y = queue.popleft()
                component.append((x, y))
                red, green, blue, _ = pixels[x, y]
                red_sum += red
                green_sum += green
                blue_sum += blue
                for neighbor_y in range(max(0, y - 1), min(height, y + 2)):
                    for neighbor_x in range(max(0, x - 1), min(width, x + 2)):
                        neighbor_index = neighbor_y * width + neighbor_x
                        if not visited[neighbor_index] and pixels[neighbor_x, neighbor_y][3] > 0:
                            visited[neighbor_index] = 1
                            queue.append((neighbor_x, neighbor_y))

            if len(component) > 32:
                continue
            average = (
                red_sum // len(component),
                green_sum // len(component),
                blue_sum // len(component),
            )
            if min(average) > 225 and max(average) - min(average) < 12:
                for x, y in component:
                    red, green, blue, _ = pixels[x, y]
                    pixels[x, y] = (red, green, blue, 0)
    return cleaned


def add_white_outline(frame: Image.Image) -> Image.Image:
    """Add a subtle one-pixel sticker edge that hides GIF's hard alpha boundary."""

    original_alpha = frame.getchannel("A")
    # A five-pixel median removes small background teeth while retaining
    # silhouette details that are wider than two pixels.
    alpha = original_alpha.filter(ImageFilter.MedianFilter(5))
    smoothed = frame.copy()
    smoothed_pixels = smoothed.load()
    original_pixels = frame.load()
    alpha_pixels = alpha.load()
    for y in range(frame.height):
        for x in range(frame.width):
            new_alpha = alpha_pixels[x, y]
            red, green, blue, old_alpha = original_pixels[x, y]
            if old_alpha == 0 and new_alpha > 0:
                # Filled one-pixel gaps become part of the white edge, not checkerboard.
                smoothed_pixels[x, y] = (255, 255, 255, new_alpha)
            else:
                smoothed_pixels[x, y] = (red, green, blue, new_alpha)

    expanded = alpha.filter(ImageFilter.MaxFilter(3))
    # A small blur affects PNG edges; GIF later reduces it to its one-bit alpha.
    expanded = expanded.filter(ImageFilter.GaussianBlur(0.35))
    outline_alpha = ImageChops.subtract(expanded, alpha)
    outline = Image.new("RGBA", frame.size, (255, 255, 255, 0))
    outline.putalpha(outline_alpha)
    return Image.alpha_composite(outline, smoothed)


def body_anchor_x(frame: Image.Image) -> int:
    """Find the green body center so poses do not jitter between frames."""

    body_x: list[int] = []
    scan_height = int(frame.height * 0.72)
    for y in range(scan_height):
        for x in range(frame.width):
            red, green, blue, alpha = frame.getpixel((x, y))
            if alpha > 0 and green - red > 18 and green - blue > 7 and green > 75:
                body_x.append(x)
    return (min(body_x) + max(body_x)) // 2 if body_x else frame.width // 2


def visible_bbox(frame: Image.Image) -> tuple[int, int, int, int]:
    """Return the bounding box for character details, shadows, smoke, and sparks."""

    return frame.getchannel("A").getbbox() or (0, 0, frame.width, frame.height)


def align_frames(frames: list[Image.Image]) -> list[Image.Image]:
    """Place every frame on a shared canvas while preserving vertical motion."""

    anchors = [body_anchor_x(frame) for frame in frames]
    boxes = [visible_bbox(frame) for frame in frames]
    padding = 8
    left_extent = max(anchor - box[0] for anchor, box in zip(anchors, boxes))
    right_extent = max(box[2] - anchor for anchor, box in zip(anchors, boxes))
    canvas_width = left_extent + right_extent + padding * 2
    canvas_height = max(frame.height for frame in frames) + padding * 2
    common_anchor = left_extent + padding

    aligned: list[Image.Image] = []
    for frame, anchor in zip(frames, anchors):
        canvas = Image.new("RGBA", (canvas_width, canvas_height), (0, 0, 0, 0))
        # Keeping the same y offset preserves the jump height from the storyboard.
        canvas.paste(frame, (common_anchor - anchor, padding))
        aligned.append(canvas)
    return aligned


def save_gif(frames: list[Image.Image], destination: Path, duration_ms: int) -> None:
    """Save a looping GIF with a shared palette and reserved transparent index."""

    palette_source = Image.new("RGB", (frames[0].width, frames[0].height * len(frames)))
    for index, frame in enumerate(frames):
        white = Image.new("RGB", frame.size, "white")
        white.paste(frame, (0, 0), frame)
        palette_source.paste(white, (0, index * frame.height))
    palette = palette_source.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    paletted: list[Image.Image] = []
    for frame in frames:
        # Avoid animated edge grain; a stable palette looks cleaner frame to frame.
        indexed = frame.convert("RGB").quantize(palette=palette, dither=Image.Dither.NONE)
        indexed_palette = indexed.getpalette()
        # White is a safe fallback for viewers that ignore GIF transparency.
        indexed_palette[255 * 3 : 255 * 3 + 3] = [255, 255, 255]
        indexed.putpalette(indexed_palette)
        alpha = frame.getchannel("A")
        indexed_pixels = indexed.load()
        alpha_pixels = alpha.load()
        for y in range(frame.height):
            for x in range(frame.width):
                if alpha_pixels[x, y] < 128:
                    indexed_pixels[x, y] = 255
        paletted.append(indexed)
    paletted[0].save(
        destination,
        save_all=True,
        append_images=paletted[1:],
        duration=duration_ms,
        loop=0,
        disposal=2,
        transparency=255,
        optimize=False,
    )


def build_preview(all_frames: list[tuple[AnimationSpec, list[Image.Image]]], destination: Path) -> None:
    """Create one 3 x 3 animated overview for quick visual checking."""

    tile_width = max(frame.width for _, frames in all_frames for frame in frames) + 20
    tile_height = max(frame.height for _, frames in all_frames for frame in frames) + 34
    preview_frames: list[Image.Image] = []
    for tick in range(8):
        sheet = Image.new("RGBA", (tile_width * 3, tile_height * 3), (238, 242, 245, 255))
        draw = ImageDraw.Draw(sheet)
        for index, (spec, frames) in enumerate(all_frames):
            frame = frames[tick % len(frames)]
            column, row = index % 3, index // 3
            x = column * tile_width + (tile_width - frame.width) // 2
            y = row * tile_height + 22
            sheet.paste(frame, (x, y), frame)
            draw.text((column * tile_width + 7, row * tile_height + 5), spec.name, fill=(25, 40, 55))
        preview_frames.append(sheet)
    save_gif(preview_frames, destination, 150)


def build_contact_sheet(all_frames: list[tuple[AnimationSpec, list[Image.Image]]], destination: Path) -> None:
    """Lay out every extracted frame so cropping mistakes are easy to spot."""

    tile_width = max(frame.width for _, frames in all_frames for frame in frames) + 8
    tile_height = max(frame.height for _, frames in all_frames for frame in frames) + 28
    max_frame_count = max(len(frames) for _, frames in all_frames)
    sheet = Image.new("RGB", (tile_width * max_frame_count, tile_height * len(all_frames)), (27, 32, 43))
    draw = ImageDraw.Draw(sheet)
    for row, (spec, frames) in enumerate(all_frames):
        draw.text((6, row * tile_height + 5), spec.name, fill=(235, 240, 245))
        for column, frame in enumerate(frames):
            x = column * tile_width + (tile_width - frame.width) // 2
            y = row * tile_height + 22
            sheet.paste(frame, (x, y), frame)
    sheet.save(destination)


def build_outline_preview(all_frames: list[tuple[AnimationSpec, list[Image.Image]]], destination: Path) -> None:
    """Show the first encoded GIF frame on a dark edge-check background."""

    encoded_frames: list[tuple[AnimationSpec, Image.Image]] = []
    for spec, _ in all_frames:
        with Image.open(destination.parent / f"{spec.name}.gif") as gif:
            encoded_frames.append((spec, gif.convert("RGBA").copy()))

    tile_width = max(frame.width for _, frame in encoded_frames) + 20
    tile_height = max(frame.height for _, frame in encoded_frames) + 34
    sheet = Image.new("RGB", (tile_width * 3, tile_height * 3), (27, 32, 43))
    draw = ImageDraw.Draw(sheet)
    for index, (spec, frame) in enumerate(encoded_frames):
        column, row = index % 3, index // 3
        x = column * tile_width + (tile_width - frame.width) // 2
        y = row * tile_height + 22
        sheet.paste(frame, (x, y), frame)
        draw.text((column * tile_width + 7, row * tile_height + 5), spec.name, fill=(235, 240, 245))
    sheet.save(destination)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="Path to the supplied storyboard PNG")
    parser.add_argument("--output", type=Path, default=Path("assets/animations/storyboard"))
    args = parser.parse_args()

    source = Image.open(args.source).convert("RGB")
    if source.size != (1136, 1385):
        raise ValueError(f"Expected a 1136 x 1385 storyboard, got {source.size}")

    args.output.mkdir(parents=True, exist_ok=True)
    built: list[tuple[AnimationSpec, list[Image.Image]]] = []
    for spec in ANIMATIONS:
        raw_frames = [
            add_white_outline(
                remove_small_neutral_islands(
                    remove_checkerboard(source.crop((x_start, spec.y_range[0], x_end, spec.y_range[1])))
                )
            )
            for x_start, x_end in spec.x_ranges
        ]
        frames = align_frames(raw_frames)
        frame_dir = args.output / spec.name
        frame_dir.mkdir(exist_ok=True)
        for index, frame in enumerate(frames, start=1):
            frame.save(frame_dir / f"frame_{index:02d}.png")
        save_gif(frames, args.output / f"{spec.name}.gif", spec.duration_ms)
        built.append((spec, frames))

    build_preview(built, args.output / "00_all_animations_preview.gif")
    build_contact_sheet(built, args.output / "00_contact_sheet.png")
    build_outline_preview(built, args.output / "00_outline_preview.png")
    print(f"Built {len(built)} animations in {args.output.resolve()}")


if __name__ == "__main__":
    main()
