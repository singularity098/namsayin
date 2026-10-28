#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 singularity098
"""Makes a block type's built-in image from a pedal photo (a Krea-2 render, see docs/PedalImagePrompts.md).

python3 tools/prepare_block_image.py <photo> <typeId> --box x0,y0,x1,y1

The box is the pedal's enclosure, as fractions of the photo (left, top, right, bottom). The photo is cropped around it
to a 1:2 portrait (a little floor and cable either side) and written as two baseline JPEGs, which DPF's stb_image
decodes:
  resources/blocks/<typeId>.jpg         HEIGHT px high, for the block panel
  resources/blocks/thumbs/<typeId>.jpg  THUMB_HEIGHT px high, for the chain's tiles and the lists
The build embeds every JPEG there (cmake/EmbedBlockImages.cmake); nothing else is needed. Then place the face's knobs,
power LED and footswitch in src/ui/PedalFaces.cpp, as fractions of the cropped image (--preview draws a grid on a copy
to read them from).

Needs Pillow.
"""
import argparse
import pathlib

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "resources" / "blocks"
HEIGHT = 640        # the panel's image is at most ~300 logical px high, so this covers 2x scaling
THUMB_HEIGHT = 192  # tiles show it 58 logical px high
QUALITY = 88


def crop_box(width, height, box):
    """The 1:2 crop around the enclosure: its height plus 6%, or its width plus 16% (twice over), within the photo"""
    x0, y0, x1, y1 = box[0] * width, box[1] * height, box[2] * width, box[3] * height
    ch = min(max((y1 - y0) * 1.06, (x1 - x0) * 1.16 * 2), height, width * 2)
    cw = ch / 2
    left = min(max((x0 + x1) / 2 - cw / 2, 0), width - cw)
    top = min(max((y0 + y1) / 2 - ch / 2, 0), height - ch)
    return round(left), round(top), round(left + cw), round(top + ch)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("photo")
    parser.add_argument("type_id")
    parser.add_argument("--box", required=True, help="the enclosure: x0,y0,x1,y1 as fractions of the photo")
    parser.add_argument("--preview", help="also write a copy with a 5%% grid here, to place the face's controls")
    args = parser.parse_args()

    box = [float(v) for v in args.box.split(",")]
    photo = Image.open(args.photo).convert("RGB")
    crop = photo.crop(crop_box(photo.width, photo.height, box))
    (OUT / "thumbs").mkdir(parents=True, exist_ok=True)
    for height, path in ((HEIGHT, OUT / f"{args.type_id}.jpg"), (THUMB_HEIGHT, OUT / "thumbs" / f"{args.type_id}.jpg")):
        img = crop.resize((height // 2, height), Image.LANCZOS)
        img.save(path, "JPEG", quality=QUALITY, optimize=True, progressive=False)
        print(f"{path.relative_to(ROOT)}: {img.width}x{img.height}, {path.stat().st_size} bytes")

    if args.preview:
        img = crop.resize((HEIGHT // 2, HEIGHT), Image.LANCZOS)
        draw = ImageDraw.Draw(img)
        for k in range(1, 20):
            colour = (110, 110, 110) if k % 2 == 0 else (55, 55, 55)
            draw.line([(0, img.height * k / 20), (img.width, img.height * k / 20)], fill=colour)
            draw.line([(img.width * k / 20, 0), (img.width * k / 20, img.height)], fill=colour)
        img.save(args.preview)


if __name__ == "__main__":
    main()
