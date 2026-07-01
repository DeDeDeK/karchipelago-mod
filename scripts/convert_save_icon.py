#!/usr/bin/env python3
"""Convert a PNG into a GameCube memory-card icon (32x32 RGB5A3) raw .dat blob.

The hoshi card-tile icon is a standard GX RGB5A3 texture: 16-bit big-endian pixels stored in
4x4 tiles (tile row-major, pixels row-major within a tile). Emitted as a raw big-endian u16 blob
(`ApIcon.dat`) that ships on the disc and is read straight into the save tile at runtime - no
image is baked into the mod's code. The name has no underscore/period on purpose: the game's file
loader appends ".dat" only to names without a "_" or "." (those are taken as already-complete).

Run from the repo root:
    uv run --with pillow python scripts/convert_save_icon.py
"""
import os
import struct
from PIL import Image

ICON_SIZE = 32
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "mods", "archipelago", "assets", "ap-icon.png")
OUT = os.path.join(ROOT, "mods", "archipelago", "assets", "ApIcon.dat")

# Alpha >= this is treated as fully opaque (5-bit RGB form); below it uses the 3-bit-alpha form.
OPAQUE_THRESHOLD = 0xE0


def rgb5a3(r, g, b, a):
    if a >= OPAQUE_THRESHOLD:
        # 1RRRRRGGGGGBBBBB
        return 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
    # 0AAARRRRGGGGBBBB
    return ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)


def main():
    img = Image.open(SRC).convert("RGBA").resize((ICON_SIZE, ICON_SIZE), Image.LANCZOS)
    px = img.load()

    vals = []
    for ty in range(0, ICON_SIZE, 4):       # tile rows
        for tx in range(0, ICON_SIZE, 4):   # tile cols
            for iy in range(4):             # rows within tile
                for ix in range(4):         # cols within tile
                    vals.append(rgb5a3(*px[tx + ix, ty + iy]))

    with open(OUT, "wb") as f:
        f.write(struct.pack(f">{len(vals)}H", *vals))
    print(f"Wrote {OUT} ({len(vals)} pixels, {len(vals) * 2} bytes)")


if __name__ == "__main__":
    main()
