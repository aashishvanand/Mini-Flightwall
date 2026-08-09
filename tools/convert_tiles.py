#!/usr/bin/env python3
"""Convert airline logo images (webp/png/jpg) into raw RGB565 icon tiles
for the Aircraft Overhead Display firmware (matrix64/03_aircraft_display).

Each source image is letterboxed onto a square canvas (black background,
matching the panel's "off" color), resized to --size x --size, and written
out as a headerless raw RGB565 dump -- exactly what preloadIcons() in the
firmware expects to read straight into a uint16_t buffer.

Usage:
    python3 tools/convert_tiles.py --input logos/ --size 24 --format raw565 --output icons

Requires Pillow:
    pip install pillow
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip install pillow")

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


def convert_one(src: Path, size: int) -> bytes:
    img = Image.open(src).convert("RGBA")

    # Letterbox: scale to fit inside size x size while preserving aspect
    # ratio, then paste centered onto a black (panel "off") square canvas.
    # Alpha is flattened onto black too, so transparent logo backgrounds
    # come out as "off" pixels rather than noise.
    img.thumbnail((size, size), Image.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 255))
    offset = ((size - img.width) // 2, (size - img.height) // 2)
    canvas.paste(img, offset, img)

    return rgb565_bytes(canvas.convert("RGB"))


def output_name(src: Path) -> str:
    stem = src.stem
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
