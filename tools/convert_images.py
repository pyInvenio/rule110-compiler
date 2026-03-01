#!/usr/bin/env python3
"""Convert all PGM files in a run output directory to PNG with crops."""

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
    Image.MAX_IMAGE_PIXELS = None  # Disable limit entirely
except ImportError:
    print("Run with: uv run --with Pillow python3 tools/convert_images.py <dir>")
    sys.exit(1)


CROP_WIDTH = 5000  # pixels for edge crops
CENTER_WIDTH = 4000  # pixels for center crop


def convert_pgm(pgm_path: Path, outdir: Path):
    img = Image.open(pgm_path)
    w, h = img.size
    stem = pgm_path.stem
    print(f"\n{pgm_path.name}: {w} x {h} ({w * h / 1e6:.0f} Mpx)")

    # For very wide images, skip full PNG (multi-GB), just do crops
    if w * h > 200_000_000:
        print(f"  Skipping full PNG (too large), saving crops only")
    else:
        full_png = outdir / f"{stem}.png"
        img.save(full_png)
        print(f"  {full_png.name}")

    # Quarters
    qw = w // 4
    for i, label in enumerate(["q1", "q2", "q3", "q4"]):
        x0, x1 = i * qw, min((i + 1) * qw, w)
        crop = img.crop((x0, 0, x1, h))
        fname = outdir / f"{stem}_{label}.png"
        crop.save(fname)
        print(f"  {fname.name} ({x1 - x0}x{h})")

    # Center
    mid = w // 2
    cw = min(CENTER_WIDTH, w)
    x0, x1 = max(0, mid - cw // 2), min(w, mid + cw // 2)
    crop = img.crop((x0, 0, x1, h))
    fname = outdir / f"{stem}_center.png"
    crop.save(fname)
    print(f"  {fname.name} ({x1 - x0}x{h})")

    # Left edge
    lw = min(CROP_WIDTH, w)
    crop = img.crop((0, 0, lw, h))
    fname = outdir / f"{stem}_left.png"
    crop.save(fname)
    print(f"  {fname.name} ({lw}x{h})")

    # Right edge
    rw = min(CROP_WIDTH, w)
    x0 = max(0, w - rw)
    crop = img.crop((x0, 0, w, h))
    fname = outdir / f"{stem}_right.png"
    crop.save(fname)
    print(f"  {fname.name} ({w - x0}x{h})")


def main():
    parser = argparse.ArgumentParser(description="Convert R110 output PGMs to PNG")
    parser.add_argument("dir", help="Output directory containing PGM files")
    args = parser.parse_args()

    d = Path(args.dir)
    if not d.is_dir():
        print(f"Not a directory: {d}")
        sys.exit(1)

    pgms = sorted(d.glob("*.pgm"))
    if not pgms:
        print(f"No PGM files in {d}")
        sys.exit(1)

    print(f"Processing {len(pgms)} PGM file(s) in {d}")
    for pgm in pgms:
        convert_pgm(pgm, d)

    print(f"\nDone. All images saved to {d}/")


if __name__ == "__main__":
    main()
