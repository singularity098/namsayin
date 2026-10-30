#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 singularity098
"""Makes the built-in generic pedal images: what NAM pedal captures show (picked per file from its hash, changeable by
right-click, as the cab images for IRs), and what a new pedal shows until the owner sends its own render
(src/ui/FileLooks.hpp, defaultPedalImage()).

python3 tools/prepare_pedal_images.py <folder>

The folder's images (Krea-2 renders), sorted by file name, become pedal-<n>.webp (n from 1):
  resources/pedals/pedal-<n>.webp         FULL px high, for the block panel
  resources/pedals/thumbs/pedal-<n>.webp  THUMB px high, for the tiles, lists and the picker
The photos keep their aspect ratio and are drawn as they are (no face knobs, LED or footswitch). The build embeds every
WebP there (cmake/EmbedBlockImages.cmake).

An image's id (its file name without .webp) is stored in the user's settings when they pick one for a NAM file, and the
defaults depend on how many there are, so add new photos with names that sort last, and never remove one.

Needs Pillow (with WebP support).
"""
import argparse
import pathlib

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "resources" / "pedals"
FULL = 1280   # as the pedal photos (tools/prepare_block_image.py)
THUMB = 384
QUALITY = 96


def fit(img, height):
    return img.resize((round(img.width * height / img.height), height), Image.LANCZOS)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder")
    args = parser.parse_args()

    (OUT / "thumbs").mkdir(parents=True, exist_ok=True)
    photos = sorted(p for p in pathlib.Path(args.folder).iterdir()
                    if p.suffix.lower() in (".png", ".jpg", ".jpeg", ".webp"))
    for n, photo in enumerate(photos, 1):
        img = Image.open(photo).convert("RGB")
        for height, path in ((FULL, OUT / f"pedal-{n}.webp"), (THUMB, OUT / "thumbs" / f"pedal-{n}.webp")):
            fit(img, height).save(path, "WEBP", quality=QUALITY, method=6, use_sharp_yuv=True)
        print(f"{photo.name} -> pedal-{n}: {(OUT / f'pedal-{n}.webp').stat().st_size} bytes full")


if __name__ == "__main__":
    main()
