#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 singularity098
"""Makes the built-in amp head images that NAM amp models show (picked per file from its content hash and the style its
name suggests, changeable by right-click, as the cab images for IRs; src/ui/FileLooks.hpp, defaultAmpImage()).

python3 tools/prepare_amp_images.py <folder> [<folder> ...]

The folders' images (Krea-2 renders), folder by folder in the order given and sorted by file name within each, become
amp-<n>.webp (n from 1, continuing from the highest number already in resources/amps/ with --append):
  resources/amps/amp-<n>.webp         FULL px wide, for the block panel
  resources/amps/thumbs/amp-<n>.webp  THUMB px wide, for the tiles, lists and the picker
The photos keep their framing (the stage around the head, like the cab photos). Each one needs a face in
src/ui/PedalFaces.cpp (ampHeadFaces(): its five knobs, power LED and switch) and a style there; tests/PedalFaceTests.cpp
checks that every image has one. The build embeds every WebP here (cmake/EmbedBlockImages.cmake, -DKIND=Amp).

An image's id is stored in the user's settings when they pick one for a NAM file, and the defaults depend on how many
there are, so add new photos as new numbers (--append) and never remove or renumber one.

Needs Pillow (with WebP support).
"""
import argparse
import pathlib
import re

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "resources" / "amps"
FULL = 1280   # the panel draws it up to ~700 logical px wide, so this stays sharp at about 2x scaling
THUMB = 384   # tiles show it 58 logical px high, the picker ~120
QUALITY = 94


def fit(img, width):
    return img.resize((width, round(img.height * width / img.width)), Image.LANCZOS)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folders", nargs="+")
    parser.add_argument("--append", action="store_true", help="number after the images already there")
    args = parser.parse_args()

    (OUT / "thumbs").mkdir(parents=True, exist_ok=True)
    start = 1
    if args.append:
        numbers = [int(m.group(1)) for p in OUT.glob("amp-*.webp") if (m := re.fullmatch(r"amp-(\d+)\.webp", p.name))]
        start = max(numbers, default=0) + 1
    photos = []
    for folder in args.folders:
        photos += sorted(p for p in pathlib.Path(folder).iterdir()
                         if p.suffix.lower() in (".png", ".jpg", ".jpeg", ".webp"))
    for n, photo in enumerate(photos, start):
        img = Image.open(photo).convert("RGB")
        for width, path in ((FULL, OUT / f"amp-{n}.webp"), (THUMB, OUT / "thumbs" / f"amp-{n}.webp")):
            fit(img, width).save(path, "WEBP", quality=QUALITY, method=6, use_sharp_yuv=True)
        print(f"{photo.name} -> amp-{n}: {(OUT / f'amp-{n}.webp').stat().st_size} bytes full")


if __name__ == "__main__":
    main()
