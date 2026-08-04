#!/usr/bin/env python3
"""Convert a PNG into a GameCube memory-card banner (96x32 RGB5A3) raw .dat blob.

The hoshi card-tile banner is a standard GX RGB5A3 texture: 16-bit big-endian pixels stored in
4x4 tiles (tile row-major, pixels row-major within a tile). Emitted as a raw big-endian u16 blob
(`ApBanner.dat`) that ships on the disc and is read straight into the save tile at runtime - no
image is baked into the mod's code. The name has no underscore/period on purpose: the game's file
loader appends ".dat" only to names without a "_" or "." (those are taken as already-complete).

The source is scaled to fit entirely inside 96x32 with its aspect ratio preserved and centered on a
transparent canvas (no cropping) - a wide source simply gets transparent bars top and bottom.

Run from the repo root:
    uv run --with pillow python scripts/convert_save_banner.py
"""
import os
import struct
from PIL import Image

BANNER_W = 96
BANNER_H = 32
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "mods", "archipelago", "assets", "ap-banner.png")
OUT = os.path.join(ROOT, "mods", "archipelago", "assets", "ApBanner.dat")

# Alpha >= this is treated as fully opaque (5-bit RGB form); below it uses the 3-bit-alpha form.
OPAQUE_THRESHOLD = 0xE0


def rgb5a3(r, g, b, a):
    if a >= OPAQUE_THRESHOLD:
        # 1RRRRRGGGGGBBBBB
        return 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
    # 0AAARRRRGGGGBBBB
    return ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)


def fit_centered(img):
    """Scale img to fit within BANNER_W x BANNER_H (aspect preserved), centered on transparency."""
    scale = min(BANNER_W / img.width, BANNER_H / img.height)
    new_w = max(1, round(img.width * scale))
    new_h = max(1, round(img.height * scale))
    scaled = img.resize((new_w, new_h), Image.LANCZOS)
    canvas = Image.new("RGBA", (BANNER_W, BANNER_H), (0, 0, 0, 0))
    canvas.paste(scaled, ((BANNER_W - new_w) // 2, (BANNER_H - new_h) // 2))
    return canvas


def main():
    img = fit_centered(Image.open(SRC).convert("RGBA"))
    px = img.load()

    vals = []
    for ty in range(0, BANNER_H, 4):       # tile rows
        for tx in range(0, BANNER_W, 4):   # tile cols
            for iy in range(4):            # rows within tile
                for ix in range(4):        # cols within tile
                    vals.append(rgb5a3(*px[tx + ix, ty + iy]))

    with open(OUT, "wb") as f:
        f.write(struct.pack(f">{len(vals)}H", *vals))
    print(f"Wrote {OUT} ({len(vals)} pixels, {len(vals) * 2} bytes)")


if __name__ == "__main__":
    main()
