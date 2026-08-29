#!/usr/bin/env python3
"""Generate the AppEnglish launcher icon.

Same house style as tools/hanzi_pipeline/make_icon.py and
tools/math_pipeline/make_icon_math.py -- black background, a glowing rounded
plate, a white symbol on top -- in mint green, the third hue, so the three
icons stay tellable apart by colour alone on a scrolling launcher a
pre-reader drives.

The symbol is a single capital A drawn in LXGW WenKai, the same face the
device UI fonts are cut from. One big letter is the direct parallel of the
hanzi icon's one big 永: the alphabet's first character standing in for the
whole writing system. "ABC" was tried and rejected -- three glyphs across the
same box land at roughly a third of the stroke weight, which goes muddy at
launcher size next to the chunky 永 and +/- next to it.

Usage: python3 tools/english_pipeline/make_icon_english.py
"""

import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT_C = os.path.join(REPO, "main", "assets", "images", "icon_english.c")
CACHE = os.path.join(REPO, "tools", "english_pipeline", ".cache")
FONT = os.path.join(REPO, "tools", "hanzi_pipeline", ".cache", "fonts",
                   "LXGWWenKai-Regular.ttf")

SIZE = 200
SS = 4                     # supersampling
GLYPH = "A"
RADIUS_FRAC = 0.30         # corner radius as a fraction of the plate
PLATE_FRAC = 0.76          # plate size as a fraction of the icon
GLYPH_FRAC = 0.74          # symbol box as a fraction of the plate
TOP = (64, 224, 156)       # mint, top of the gradient
BOTTOM = (20, 146, 100)    # deeper jade, bottom
GLOW = (42, 200, 134)


def rounded_plate(size, radius, top, bottom):
    """Vertical gradient clipped to a rounded square, as RGB + alpha mask."""
    grad = Image.new("RGB", (size, size))
    px = grad.load()
    for y in range(size):
        t = y / max(1, size - 1)
        row = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(size):
            px[x, y] = row
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size - 1, size - 1], radius=radius,
                                           fill=255)
    return grad, mask


def glyph_mask(size):
    """White coverage mask of the glyph, fitted to its own ink rather than to
    the font's metrics -- a lone A carries a full line's worth of leading and
    sidebearings, which would shrink it and push it off centre."""
    if not os.path.exists(FONT):
        raise SystemExit(f"font not found: {FONT}\n"
                         "it comes down with the hanzi pipeline; fetch that first")
    # Render oversized, then scale the ink down: keeps the fit exact whatever
    # the face's metrics say, and the downscale is one more resample on top of
    # the supersampling the caller already does.
    probe = size * 2
    font = ImageFont.truetype(FONT, probe)
    sheet = Image.new("L", (probe * 4, probe * 3), 0)
    ImageDraw.Draw(sheet).text((probe, probe // 2), GLYPH, font=font, fill=255)

    box = sheet.getbbox()
    if box is None:
        raise SystemExit(f"the font rendered {GLYPH!r} as nothing")
    ink = sheet.crop(box)
    k = min(size / ink.width, size / ink.height)
    ink = ink.resize((max(1, int(ink.width * k)), max(1, int(ink.height * k))),
                     Image.LANCZOS)

    mask = Image.new("L", (size, size), 0)
    mask.paste(ink, ((size - ink.width) // 2, (size - ink.height) // 2))
    return mask


def build():
    big = SIZE * SS
    canvas = Image.new("RGB", (big, big), (0, 0, 0))

    plate_size = int(big * PLATE_FRAC)
    radius = int(plate_size * RADIUS_FRAC)
    plate, plate_mask = rounded_plate(plate_size, radius, TOP, BOTTOM)
    offset = (big - plate_size) // 2

    # Outer bloom: the plate silhouette, blurred and added under the plate.
    glow = Image.new("RGB", (big, big), (0, 0, 0))
    glow.paste(Image.new("RGB", (plate_size, plate_size), GLOW), (offset, offset),
               plate_mask)
    glow = glow.filter(ImageFilter.GaussianBlur(radius=big * 0.055))
    canvas = Image.blend(canvas, glow, 0.85)

    canvas.paste(plate, (offset, offset), plate_mask)

    # The glyph sits slightly smaller than the plate so the strokes breathe.
    gsize = int(plate_size * GLYPH_FRAC)
    gmask = glyph_mask(gsize)
    goff = (big - gsize) // 2
    white = Image.new("RGB", (gsize, gsize), (255, 255, 255))
    canvas.paste(white, (goff, goff), gmask)

    return canvas.resize((SIZE, SIZE), Image.LANCZOS)


def emit_c(img, path):
    px = img.convert("RGB").load()
    data = bytearray()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b = px[x, y]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            data += bytes((v & 0xFF, (v >> 8) & 0xFF))

    with open(path, "w", encoding="utf-8") as f:
        f.write("""#ifdef __has_include
#if __has_include("lvgl.h")
#ifndef LV_LVGL_H_INCLUDE_SIMPLE
#define LV_LVGL_H_INCLUDE_SIMPLE
#endif
#endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMAGE_ICON_ENGLISH
#define LV_ATTRIBUTE_IMAGE_ICON_ENGLISH
#endif

/* Generated by tools/english_pipeline/make_icon_english.py -- do not edit. */

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMAGE_ICON_ENGLISH uint8_t icon_english_map[] = {
""")
        for i in range(0, len(data), 19):
            f.write("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 19]) + ",\n")
        f.write("""};

const lv_image_dsc_t icon_english = {
    .header.cf    = LV_COLOR_FORMAT_RGB565,
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.w     = %d,
    .header.h     = %d,
    .data_size    = %d * 2,
    .data         = icon_english_map,
};
""" % (SIZE, SIZE, SIZE * SIZE))
    return len(data)


def main():
    img = build()
    os.makedirs(CACHE, exist_ok=True)
    preview = os.path.join(CACHE, "icon_english.png")
    img.save(preview)
    size = emit_c(img, OUT_C)
    print(f"icon: {SIZE}x{SIZE} RGB565, {size} bytes -> {OUT_C}")
    print(f"preview: {preview}")


if __name__ == "__main__":
    main()
