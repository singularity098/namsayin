#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 singularity098
"""Makes the built-in speaker cabinet images, which IR blocks show (src/ui/FileLooks.hpp, defaultCabImage()).

python3 tools/prepare_cab_images.py <folder>

The folder holds one subfolder per cab size, named as the owner sorted the renders: 1x10, 2x10, 4x10 (bass),
1x12, 2x12, 4x12 (guitar). Each subfolder's images, sorted by file name, become <size>-<n>.webp (n from 1):
  resources/cabs/<size>-<n>.webp         at most FULL px on the long side, for the block panel
  resources/cabs/thumbs/<size>-<n>.webp  at most THUMB px on the long side, for the tiles, lists and the picker
The photos keep their aspect ratio (most are square). The build embeds every WebP there (cmake/EmbedBlockImages.cmake).

An image's id (its file name without .webp) is stored in the user's settings when they pick one for an IR, so
re-running this with other photos must keep the old ones' numbers: add new photos with names that sort last.

Needs Pillow (with WebP support).
"""
import argparse
import pathlib

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "resources" / "cabs"
SIZES = ("1x10", "2x10", "4x10", "1x12", "2x12", "4x12")
FULL = 896    # the panel shows it at most ~450 logical px high, so this stays sharp at 2x scaling
THUMB = 320   # tiles show it 58 logical px high, the picker 140
QUALITY = 96  # as the pedal photos (tools/prepare_block_image.py)


def fit(img, longest):
    scale = longest / max(img.width, img.height)
    return img.resize((round(img.width * scale), round(img.height * scale)), Image.LANCZOS)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder")
    args = parser.parse_args()

    (OUT / "thumbs").mkdir(parents=True, exist_ok=True)
    for size in SIZES:
        photos = sorted(p for p in (pathlib.Path(args.folder) / size).iterdir()
                        if p.suffix.lower() in (".png", ".jpg", ".jpeg", ".webp"))
        for n, photo in enumerate(photos, 1):
            img = Image.open(photo).convert("RGB")
            for longest, path in ((FULL, OUT / f"{size}-{n}.webp"), (THUMB, OUT / "thumbs" / f"{size}-{n}.webp")):
                out = fit(img, longest)
                out.save(path, "WEBP", quality=QUALITY, method=6, use_sharp_yuv=True)
            print(f"{photo.name} -> {size}-{n}: {path.stat().st_size} bytes thumb, "
                  f"{(OUT / f'{size}-{n}.webp').stat().st_size} bytes full")


if __name__ == "__main__":
    main()
