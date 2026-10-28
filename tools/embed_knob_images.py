#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 singularity098
"""Regenerates src/ui/KnobImages.cpp from resources/knobs/.

knob.png is the knob drawn pointing up (12 o'clock) on a black background; sheen.png is its lighting layer, a lit
sphere on black. Both are cut out to the knob's circle (found from the pixels brighter than the background) and
embedded as SIZE px 8-bit RGBA PNGs: the knob keeps its colours, the sheen becomes white whose alpha is its
brightness, so drawing it over the knob screens the light on (the UI sets its strength).

A real knob's reflections don't turn with it, so only the pointer does: each knob is split into its body, with the
pointer painted out (filled from the same radius either side; the brushed metal is concentric), drawn unrotated
with its lighting, and the pointer alone (RGBA), which the UI rotates to the value over it. The sheen is drawn over
both, unrotated.

rim.png is the knob with a glowing blue rim (the owner's, 2026-10-28), on black. Each pedal tints the rim its own
colour, so it's split like the LEDs (embed_led_images.py): the neutral part (each pixel's smallest channel) and the
colour part (its largest minus its smallest), which the UI adds tinted. Both are cropped around the rim (found from
the saturated pixels) with RIM_GLOW of its radius around it for the glow: the rim's outer edge is 1 / RIM_GLOW of the
image's half-width, which drawFaceKnob() relies on.

Needs Pillow and NumPy. Run from the repository root after changing the images: python3 tools/embed_knob_images.py
"""
import io
import pathlib

import numpy as np
from PIL import Image, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "resources" / "knobs"
OUT = ROOT / "src" / "ui" / "KnobImages.cpp"
RIM_GLOW = 1.08  # the rim knob images' half-width, in rim radii
RIM_GLOW_BOOST = 2.5  # how much blurred rim is added to the rim, as glow
SIZE = 256  # face knobs are at most ~60 logical px across, so this covers 2x scaling with room to spare


def circle_crop(img):
    """Crops to the bounding square of the pixels brighter than the background"""
    lum = np.asarray(img.convert("L"))
    ys, xs = np.nonzero(lum > 6)
    cx, cy = (xs.min() + xs.max()) / 2, (ys.min() + ys.max()) / 2
    r = max(xs.max() - xs.min(), ys.max() - ys.min()) / 2 + 0.5
    return img.crop((round(cx - r), round(cy - r), round(cx + r), round(cy + r))).resize((SIZE, SIZE), Image.LANCZOS)


def circle_mask():
    """An antialiased disc filling the image"""
    c = (SIZE - 1) / 2
    y, x = np.mgrid[0:SIZE, 0:SIZE]
    d = np.hypot(x - c, y - c)
    return np.clip(SIZE / 2 - 1.0 - d, 0.0, 1.0)


def split_pointer(rgba):
    """Splits a knob (RGBA, pointing up, centred) into its body with the pointer painted out and the pointer alone"""
    c = (SIZE - 1) / 2
    rgb = rgba[..., :3]
    lum = rgb.mean(axis=2)
    sat = rgb.max(axis=2) - rgb.min(axis=2)
    y, x = np.mgrid[0:SIZE, 0:SIZE]
    # the pointer: the bright, grey pixels in a narrow band above the centre
    core = (np.abs(x - c) < SIZE * 0.05) & (y < c) & (lum > 140) & (sat < 40)
    ys, xs = np.nonzero(core)
    pad = SIZE * 0.008
    x0, x1, y0, y1 = xs.min() - pad, xs.max() + pad, ys.min() - pad, ys.max() + pad
    # a soft-edged rounded box around it
    dx = np.maximum(np.maximum(x0 - x, x - x1), 0.0)
    dy = np.maximum(np.maximum(y0 - y, y - y1), 0.0)
    alpha = np.clip(1.0 - np.hypot(dx, dy) / (SIZE * 0.01), 0.0, 1.0)
    # the body there: the same radius either side of it, averaged, so the light around the knob carries on across it
    img = Image.fromarray(np.round(rgba).astype(np.uint8), "RGBA")
    turned = np.mean([np.asarray(img.rotate(a, Image.BICUBIC))[..., :3].astype(np.float64) for a in (-18, 18, -30, 30)],
                     axis=0)
    body = rgba.copy()
    body[..., :3] = rgb * (1 - alpha[..., None]) + turned * alpha[..., None]
    pointer_alpha = np.minimum(alpha, rgba[..., 3] / 255)
    pointer = np.dstack([rgb * (pointer_alpha > 0)[..., None], pointer_alpha * 255])
    return body, pointer


mask = circle_mask()
knob = np.asarray(circle_crop(Image.open(SOURCE / "knob.png").convert("RGB"))).astype(np.float64)
knob = np.dstack([knob, mask * 255])
knob, knob_pointer = split_pointer(knob)
sheen = np.asarray(circle_crop(Image.open(SOURCE / "sheen.png").convert("L"))).astype(np.float64)
sheen = np.dstack([np.full((SIZE, SIZE, 3), 255.0), sheen * mask])


def rim_layers():
    """The rim knob's neutral part (RGBA, cut out at the rim) and its colour part (grey, glow included)"""
    rgb = np.asarray(Image.open(SOURCE / "rim.png").convert("RGB")).astype(np.float64)
    ys, xs = np.nonzero(rgb.max(axis=2) - rgb.min(axis=2) > 60)
    cx, cy = (xs.min() + xs.max()) / 2, (ys.min() + ys.max()) / 2
    half = max(xs.max() - xs.min(), ys.max() - ys.min()) / 2 * RIM_GLOW
    box = (round(cx - half), round(cy - half), round(cx + half), round(cy + half))
    img = np.asarray(Image.fromarray(rgb.astype(np.uint8)).crop(box).resize((SIZE, SIZE), Image.LANCZOS))
    img = img.astype(np.float64)
    c = (SIZE - 1) / 2
    y, x = np.mgrid[0:SIZE, 0:SIZE]
    d = np.hypot(x - c, y - c)
    disc = np.clip(SIZE / 2 / RIM_GLOW - d, 0.0, 1.0)  # antialiased, out to the rim's outer edge
    fade = np.clip((SIZE / 2 - 1.0 - d) / 4.0, 0.0, 1.0)  # the glow, faded out before the square's edge
    lo = img.min(axis=2)
    colour = np.clip(img.max(axis=2) - lo - 4.0, 0.0, 255.0)  # no colour noise on the brushed metal
    # a stronger glow, so the rim still reads on a knob a few pixels across
    glow = np.asarray(Image.fromarray(colour.astype(np.uint8)).filter(ImageFilter.GaussianBlur(SIZE / 80)))
    colour = np.clip(colour + glow.astype(np.float64) * RIM_GLOW_BOOST, 0.0, 255.0) * fade
    neutral = np.dstack([np.repeat(lo[..., None], 3, axis=2), disc * 255])
    return neutral, colour


rim, rim_colour = rim_layers()
rim, rim_pointer = split_pointer(rim)

lines = [
    "// SPDX-License-Identifier: MIT",
    "// Copyright (c) 2026 singularity098",
    "",
    "// Generated by tools/embed_knob_images.py from resources/knobs/ - do not edit by hand.",
    "",
    '#include "KnobImages.hpp"',
    "",
    "namespace namsayin {",
    "",
    "namespace {",
    "",
]
for name, pixels, mode in (("kKnobPng", knob, "RGBA"), ("kKnobPointerPng", knob_pointer, "RGBA"),
                           ("kSheenPng", sheen, "RGBA"), ("kRimPng", rim, "RGBA"), ("kRimPointerPng", rim_pointer, "RGBA"),
                           ("kRimColourPng", rim_colour, "L")):
    buf = io.BytesIO()
    Image.fromarray(np.round(pixels).astype(np.uint8), mode).save(buf, "PNG", optimize=True)
    data = buf.getvalue()
    lines.append("const unsigned char %s[] = {" % name)
    for i in range(0, len(data), 20):
        lines.append("    " + ", ".join(str(b) for b in data[i:i + 20]) + ",")
    lines += ["};", ""]
    print("%s: %dx%d, %d bytes" % (name, SIZE, SIZE, len(data)))

lines += [
    "} // namespace",
    "",
    "BuiltinImage knobImage() { return { kKnobPng, sizeof(kKnobPng) }; }",
    "",
    "BuiltinImage knobPointerImage() { return { kKnobPointerPng, sizeof(kKnobPointerPng) }; }",
    "",
    "BuiltinImage knobSheenImage() { return { kSheenPng, sizeof(kSheenPng) }; }",
    "",
    "BuiltinImage rimKnobImage() { return { kRimPng, sizeof(kRimPng) }; }",
    "",
    "BuiltinImage rimKnobPointerImage() { return { kRimPointerPng, sizeof(kRimPointerPng) }; }",
    "",
    "BuiltinImage rimKnobColourImage() { return { kRimColourPng, sizeof(kRimColourPng) }; }",
    "",
    "} // namespace namsayin",
    "",
]
OUT.write_text("\n".join(lines))
