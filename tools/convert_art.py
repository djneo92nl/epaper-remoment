#!/usr/bin/env python3
"""Converts public-domain artworks into black/white/red bitmaps for the panel.

Downloads each work from Wikimedia Commons (cached in tools/.art_cache/),
fits it into the art area, and dithers it to the panel's three colors:

  * Red is only used where the artwork is actually red-ish (warm hue,
    saturated, not too dark) -- everything else is rendered in black and
    white from its luminance. Mapping straight to the nearest of
    black/white/red instead turns every mid-gray red.
  * Both decisions go through error diffusion (Floyd-Steinberg, serpentine),
    one channel for "how red" and one for brightness, so smooth areas get
    fine dither patterns instead of hard bands.

Writes:
  art/art.bin          all images, each a black plane then a red plane
                       (1 bit per pixel, MSB first, rows of W/8 bytes)
  src/art_catalog.h    titles/artists + image size, generated
  art/previews/*.png   what each image will look like on the panel

Usage (needs Pillow + numpy):
  pip install pillow numpy
  python3 tools/convert_art.py
"""

import json
import os
import sys
import urllib.parse
import urllib.request

import numpy as np
from PIL import Image, ImageFilter, ImageOps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "tools", ".art_cache")
OUT_BIN = os.path.join(ROOT, "art", "art.bin")
OUT_HEADER = os.path.join(ROOT, "src", "art_catalog.h")
OUT_PREVIEWS = os.path.join(ROOT, "art", "previews")

# Panel is 800x480; the bottom strip is kept free for the caption.
WIDTH, HEIGHT = 800, 456

USER_AGENT = "epaper-remoment-art/1.0 (https://github.com/djneo92nl/epaper-remoment)"

# (id, Commons file, artist, title, year, options)
#   gamma < 1 brightens, > 1 darkens; crop = (left, top, right, bottom) as
#   fractions of the source, to focus wide/busy works; red_sat = saturation
#   from which color starts counting as red (lower it for faded prints).
ARTWORKS = [
    ("great-wave", "File:Great Wave off Kanagawa2.jpg",
     "Katsushika Hokusai", "The Great Wave off Kanagawa", "c. 1831", {}),
    ("red-fuji", "File:Red Fuji southern wind clear morning.jpg",
     "Katsushika Hokusai", "Fine Wind, Clear Morning (Red Fuji)", "c. 1831", {}),
    ("milkmaid", "File:Johannes Vermeer - Het melkmeisje - Google Art Project.jpg",
     "Johannes Vermeer", "The Milkmaid", "c. 1658", {"gamma": 0.8}),
    ("night-watch", "File:The Night Watch - HD.jpg",
     "Rembrandt van Rijn", "The Night Watch", "1642",
     {"gamma": 0.55, "crop": (0.02, 0.03, 0.98, 0.97)}),
    ("starry-night", "File:Van Gogh - Starry Night - Google Art Project.jpg",
     "Vincent van Gogh", "The Starry Night", "1889", {"gamma": 0.85}),
    ("mondrian", "File:Piet Mondriaan, 1930 - Mondrian Composition II in Red, Blue, and Yellow.jpg",
     "Piet Mondrian", "Composition II in Red, Blue, and Yellow", "1930", {}),
    ("scream", "File:Edvard Munch, 1893, The Scream, oil, tempera and pastel on cardboard, 91 x 73 cm, National Gallery of Norway.jpg",
     "Edvard Munch", "The Scream", "1893", {"gamma": 0.75}),
    ("moulin-rouge", "File:Moulin Rouge- La Goulue MET DT11780.jpg",
     "Henri de Toulouse-Lautrec", "Moulin Rouge: La Goulue", "1891", {"red_sat": 0.2}),
    ("hare", "File:Albrecht Dürer - Hare, 1502 - Google Art Project.jpg",
     "Albrecht Durer", "Young Hare", "1502", {"gamma": 0.9}),
]

# Dither in DOT x DOT blocks. Black/white/red panels don't render fine
# single-pixel checkerboards cleanly -- the red pigment shows through and
# dense dither areas get a pink/brown cast even with no red pixels at all.
# 2x2 dots stay neutral.
DOT = 1

# How far tones are pushed toward black/white (0 = linear, 1 = full S-curve).
CONTRAST = 0.8

# Atkinson dithering: spreads only 6/8 of the error to these neighbors (the
# rest is dropped), which keeps highlights and shadows solid instead of
# speckled -- much punchier on e-paper than Floyd-Steinberg.
ATKINSON = ((1, 0), (2, 0), (-1, 1), (0, 1), (1, 1), (0, 2))

# Luminance the panel's red reads as, for the brightness error diffusion.
RED_LUMINANCE = 0.35
PREVIEW_RED = (190, 30, 35)


def fetch(commons_title, dest):
    if os.path.exists(dest):
        return
    query = urllib.parse.urlencode({
        "action": "query", "titles": commons_title, "prop": "imageinfo",
        "iiprop": "url", "iiurlwidth": 1600, "format": "json",
    })
    req = urllib.request.Request("https://commons.wikimedia.org/w/api.php?" + query,
                                 headers={"User-Agent": USER_AGENT})
    pages = json.load(urllib.request.urlopen(req, timeout=30))["query"]["pages"]
    info = next(iter(pages.values()))["imageinfo"][0]
    url = info.get("thumburl") or info["url"]
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as resp, open(dest, "wb") as f:
        f.write(resp.read())


def fit(img, opts, width, height):
    if "crop" in opts:
        l, t, r, b = opts["crop"]
        w, h = img.size
        img = img.crop((int(l * w), int(t * h), int(r * w), int(b * h)))
    img = ImageOps.contain(img, (width, height), Image.LANCZOS)
    # Crisp edges survive dithering far better than soft ones.
    img = img.filter(ImageFilter.UnsharpMask(radius=1.5, percent=110, threshold=2))
    canvas = Image.new("RGB", (width, height), "white")
    canvas.paste(img, ((width - img.width) // 2, (height - img.height) // 2))
    return canvas, img.size


def channels(img, opts):
    """Brightness (0..1, tone-mapped) and redness (0..1) per pixel."""
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    lum = 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]

    # Stretch contrast (clipping the extreme 3% at both ends), gamma, then
    # an S-curve: on the panel, mid-tone dither patterns blend into a flat
    # gray, so push tones toward solid black and white.
    lo, hi = np.percentile(lum, [3, 97])
    lum = np.clip((lum - lo) / max(hi - lo, 1e-3), 0, 1) ** opts.get("gamma", 1.0)
    s_curve = lum * lum * (3 - 2 * lum)
    strength = opts.get("contrast", CONTRAST)
    lum = lum + (s_curve - lum) * strength

    hsv = np.asarray(img.convert("HSV"), dtype=np.float32) / 255.0
    hue_deg = hsv[..., 0] * 360
    sat, val = hsv[..., 1], hsv[..., 2]
    # Distance from pure red on the hue wheel: full weight within 10
    # degrees, fading out by 30. Together with the saturation floor this
    # keeps warm skin tones (hue ~15-25, moderate saturation) out of red.
    hue_dist = np.minimum(hue_deg, 360 - hue_deg)
    hue_w = np.clip((30 - hue_dist) / 20, 0, 1)
    red_sat = opts.get("red_sat", 0.45)
    sat_w = np.clip((sat - red_sat) / 0.3, 0, 1)
    val_w = np.clip((val - 0.2) / 0.3, 0, 1)
    red = hue_w * sat_w * val_w
    return lum, red


def dither(lum, red):
    """Two-channel serpentine Atkinson dither. Returns (black, red) masks."""
    h, w = lum.shape
    lum = lum.copy()
    red = red.copy()
    black_mask = np.zeros((h, w), dtype=bool)
    red_mask = np.zeros((h, w), dtype=bool)
    for y in range(h):
        forward = y % 2 == 0
        xs = range(w) if forward else range(w - 1, -1, -1)
        step = 1 if forward else -1
        for x in xs:
            r_old, l_old = red[y, x], lum[y, x]
            if r_old > 0.5:
                red_mask[y, x] = True
                r_err, l_err = r_old - 1.0, l_old - RED_LUMINANCE
            else:
                is_white = l_old >= 0.5
                black_mask[y, x] = not is_white
                r_err, l_err = r_old, l_old - (1.0 if is_white else 0.0)
            r_err /= 8
            l_err /= 8
            for dx, dy in ATKINSON:
                nx, ny = x + dx * step, y + dy
                if 0 <= nx < w and ny < h:
                    red[ny, nx] += r_err
                    lum[ny, nx] += l_err
    return black_mask, red_mask


def pack(mask):
    return np.packbits(mask, axis=1).tobytes()  # MSB first, W/8 bytes per row


def preview(black_mask, red_mask, path):
    out = np.full(black_mask.shape + (3,), 255, dtype=np.uint8)
    out[black_mask] = (0, 0, 0)
    out[red_mask] = PREVIEW_RED
    Image.fromarray(out).save(path, optimize=True)


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    os.makedirs(CACHE, exist_ok=True)
    os.makedirs(OUT_PREVIEWS, exist_ok=True)
    blob = bytearray()
    entries = []
    for art_id, commons, artist, title, year, opts in ARTWORKS:
        src = os.path.join(CACHE, art_id + os.path.splitext(commons)[1].lower())
        print(f"{art_id}: fetching", flush=True)
        fetch(commons, src)
        img, size = fit(Image.open(src).convert("RGB"), opts, WIDTH // DOT, HEIGHT // DOT)
        lum, red = channels(img, opts)
        print(f"{art_id}: dithering {size[0]}x{size[1]} in {DOT}x{DOT} dots", flush=True)
        black_mask, red_mask = dither(lum, red)
        black_mask = black_mask.repeat(DOT, axis=0).repeat(DOT, axis=1)
        red_mask = red_mask.repeat(DOT, axis=0).repeat(DOT, axis=1)
        entries.append((artist, title, year, len(blob)))
        blob += pack(black_mask) + pack(red_mask)
        preview(black_mask, red_mask, os.path.join(OUT_PREVIEWS, art_id + ".png"))

    with open(OUT_BIN, "wb") as f:
        f.write(blob)

    lines = [
        "// Generated by tools/convert_art.py -- do not edit.",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"static const int16_t ART_WIDTH = {WIDTH};",
        f"static const int16_t ART_HEIGHT = {HEIGHT};",
        "",
        "struct ArtInfo {",
        "  const char *artist;",
        "  const char *title;",
        "  const char *year;",
        "  uint32_t offset; // into art.bin: black plane, then red plane",
        "};",
        "",
        "static const ArtInfo ART_CATALOG[] = {",
    ]
    for artist, title, year, offset in entries:
        lines.append(f"    {{{c_string(artist)}, {c_string(title)}, {c_string(year)}, {offset}}},")
    lines += ["};", "", "static const int ART_COUNT = sizeof(ART_CATALOG) / sizeof(ART_CATALOG[0]);", ""]
    with open(OUT_HEADER, "w") as f:
        f.write("\n".join(lines))

    print(f"wrote {len(entries)} artworks, {len(blob) // 1024} KB")


if __name__ == "__main__":
    sys.exit(main())
