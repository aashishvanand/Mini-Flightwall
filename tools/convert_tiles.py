#!/usr/bin/env python3
"""Convert airline logo images (webp/png/jpg) into raw RGB565 icon tiles
for the Aircraft Overhead Display firmware (matrix64/03_aircraft_display).

Cropping logic is ported from the Ulanzi Feeder repo's 8x8 AWTRIX converter
(emblem-focus detection, unsharp masking, two-pass downscale, no-dither
quantization) -- built to make a logo's actual emblem mark (bird, crane,
flag, kapok flower, ...) fill most of the tile instead of sitting tiny in
the middle of a plain letterboxed square. At this repo's 24x24 (vs. AWTRIX's
8x8) there's more room to work with, not less, so the same crop should read
at least as well here.

Each source image is smart-cropped and centered on a black canvas (matching
the panel's "off" color), resized to --size x --size, and written out as a
headerless raw RGB565 dump -- exactly what preloadIcons() in the firmware
expects to read straight into a uint16_t buffer.

Usage:
    python3 tools/convert_tiles.py --input logos/ --size 24 --format raw565 --output icons

Requires Pillow and numpy:
    pip install pillow numpy
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    import numpy as np
    from PIL import Image, ImageEnhance, ImageFilter
except ImportError:
    sys.exit("Pillow and numpy are required: pip install pillow numpy")

SOURCE_EXTENSIONS = {".webp", ".png", ".jpg", ".jpeg", ".bmp"}


def rgb565_bytes(img: Image.Image) -> bytes:
    """Pack an RGB image into little-endian RGB565, row-major, top-to-bottom --
    matches how preloadIcons() reads the file straight into a uint16_t[] on
    the (little-endian) ESP32."""
    out = bytearray()
    pixels = img.load()
    width, height = img.size
    for y in range(height):
        for x in range(width):
            r, g, b = pixels[x, y][:3]
            value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out += struct.pack("<H", value)
    return bytes(out)


def smart_crop_and_scale(src: Path, size: int) -> Image.Image:
    """Emblem-focus crop + multi-stage downscale onto a size x size black
    canvas. See module docstring -- ported from Ulanzi Feeder's convert_tiles.py
    (mode='emblem'), only the output size and the caller's pixel-packing
    differ."""
    img = Image.open(src).convert("RGBA")
    arr = np.array(img)
    alpha = arr[:, :, 3]
    rgb = arr[:, :, :3]

    # 1. Base content bounding box (non-white, opaque)
    nonwhite = np.any(rgb < 245, axis=2)
    content = (alpha > 15) & nonwhite
    ys, xs = np.where(content)
    if len(xs) == 0:
        bbox = (0, 0, img.width, img.height)
    else:
        pad = 1
        bbox = (
            int(max(xs.min() - pad, 0)),
            int(max(ys.min() - pad, 0)),
            int(min(xs.max() + pad, img.width)),
            int(min(ys.max() + pad, img.height)),
        )
    cropped = img.crop(bbox)

    # 2. Smart emblem-detail crop (zoom into the actual logo mark, not just
    # the tailfin's outer silhouette)
    if cropped.width > 20 and cropped.height > 20:
        c_arr = np.array(cropped)
        c_rgb = c_arr[:, :, :3]
        c_alpha = c_arr[:, :, 3]

        gray = Image.fromarray(c_rgb).convert("L")
        edges = np.array(gray.filter(ImageFilter.FIND_EDGES))

        # Erode inner alpha mask to ignore the fin's outer boundary against
        # a transparent background.
        alpha_img = Image.fromarray(c_alpha)
        inner_alpha = np.array(alpha_img.filter(ImageFilter.MinFilter(19))) > 150
        detail_edges = np.where(inner_alpha, edges, 0)

        e_ys, e_xs = np.where(detail_edges > 20)
        if len(e_xs) > 30:
            d_min_x, d_max_x = e_xs.min(), e_xs.max()
            d_min_y, d_max_y = e_ys.min(), e_ys.max()

            px = max(2, int((d_max_x - d_min_x) * 0.08))
            py = max(2, int((d_max_y - d_min_y) * 0.08))

            crop_box = (
                max(0, d_min_x - px),
                max(0, d_min_y - py),
                min(cropped.width, d_max_x + px + 1),
                min(cropped.height, d_max_y + py + 1),
            )
            cropped = cropped.crop(crop_box)

    # 3. Composite onto pure black background (matches the panel's "off"
    # color -- transparent logo backgrounds become "off" pixels, not noise)
    black_bg = Image.new("RGBA", cropped.size, (0, 0, 0, 255))
    flat = Image.alpha_composite(black_bg, cropped).convert("RGB")

    # 4. Aspect-ratio preserving target dimensions (fit within size x size)
    w, h = flat.size
    scale = min(size / w, size / h)
    target_w = max(1, int(round(w * scale)))
    target_h = max(1, int(round(h * scale)))

    # 5. Pre-scaling contrast, color saturation boost & edge sharpening
    flat = ImageEnhance.Color(flat).enhance(1.9)
    flat = ImageEnhance.Contrast(flat).enhance(1.4)
    flat = flat.filter(ImageFilter.UnsharpMask(radius=2.2, percent=190, threshold=2))

    # 6. Multi-stage downscale: high-res -> 4x target intermediate ->
    # unsharp -> target size (softens LANCZOS ringing before the final
    # BOX-filter reduction, keeps edges crisp instead of blurry)
    mid = flat.resize((target_w * 4, target_h * 4), Image.Resampling.LANCZOS)
    mid = mid.filter(ImageFilter.UnsharpMask(radius=1.1, percent=150, threshold=1))
    small = mid.resize((target_w, target_h), Image.Resampling.BOX)

    # 7. Center on the size x size black canvas
    canvas = Image.new("RGB", (size, size), (0, 0, 0))
    ox = (size - target_w) // 2
    oy = (size - target_h) // 2
    canvas.paste(small, (ox, oy))

    # 8. Final contrast polish
    canvas = ImageEnhance.Contrast(canvas).enhance(1.2)

    # 9. Clean palette quantization WITHOUT dithering (prevents noisy
    # single-pixel dot artifacts on the LED matrix), then straight back to
    # RGB for rgb565_bytes() -- the firmware has no palette/GIF support,
    # this is purely a quantize-for-clean-color-boundaries step.
    quantized = canvas.convert("P", palette=Image.ADAPTIVE, colors=256, dither=Image.Dither.NONE)
    return quantized.convert("RGB")


def convert_one(src: Path, size: int) -> bytes:
    return rgb565_bytes(smart_crop_and_scale(src, size))


def output_name(src: Path) -> str:
    stem = src.stem.lower()
    if not stem.endswith("_logo"):
        stem += "_logo"
    return stem + ".bin"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", required=True, type=Path, help="Directory of source logo images (webp/png/jpg/bmp)")
    parser.add_argument("--size", type=int, default=24, help="Output tile size in pixels (default: 24, matches ICON_SIZE in the firmware)")
    parser.add_argument("--format", choices=["raw565"], default="raw565", help="Output pixel format (only raw565 is implemented -- what the firmware reads)")
    parser.add_argument("--output", required=True, type=Path, help="Directory to write <name>_logo.bin files into")
    args = parser.parse_args()

    if not args.input.is_dir():
        sys.exit(f"--input {args.input} is not a directory")
    args.output.mkdir(parents=True, exist_ok=True)

    sources = sorted(p for p in args.input.iterdir() if p.suffix.lower() in SOURCE_EXTENSIONS)
    if not sources:
        sys.exit(f"No source images ({', '.join(sorted(SOURCE_EXTENSIONS))}) found in {args.input}")

    converted = 0
    for src in sources:
        try:
            data = convert_one(src, args.size)
        except Exception as exc:
            print(f"skip {src.name}: {exc}", file=sys.stderr)
            continue
        out_path = args.output / output_name(src)
        out_path.write_bytes(data)
        expected = args.size * args.size * 2
        print(f"{src.name} -> {out_path.name} ({len(data)} bytes)")
        assert len(data) == expected, f"unexpected size for {src.name}"
        converted += 1

    print(f"\nConverted {converted}/{len(sources)} icon(s) into {args.output}")


if __name__ == "__main__":
    main()
