#!/usr/bin/env python3
"""Convert a PNG into a GameCube memory-card icon (32x32 RGB5A3) C array.

The hoshi card-tile icon is a standard GX RGB5A3 texture: 16-bit big-endian pixels stored in
4x4 tiles (tile row-major, pixels row-major within a tile). Emitted as a u16[] whose values the
PowerPC (big-endian) compiler stores in the byte order the card manager expects.

Run from the repo root:
    uv run --with pillow python scripts/convert_save_icon.py
"""
import os
from PIL import Image

ICON_SIZE = 32
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "mods", "archipelago", "assets", "ap-icon.png")
OUT = os.path.join(ROOT, "mods", "archipelago", "src", "save_icon.c")

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

    lines = []
    for i in range(0, len(vals), 8):
        lines.append("    " + ", ".join(f"0x{v:04x}" for v in vals[i:i + 8]) + ",")

    body = "\n".join(lines)
    out = (
        "// Auto-generated from mods/archipelago/assets/ap-icon.png by scripts/convert_save_icon.py.\n"
        "// 32x32 RGB5A3, GX 4x4-tiled, big-endian. Do not edit by hand - re-run the script.\n"
        "#include \"os.h\"\n"
        "#include \"save_icon.h\"\n\n"
        "const u16 ap_save_icon[AP_SAVE_ICON_W * AP_SAVE_ICON_H] =\n{\n"
        f"{body}\n"
        "};\n"
    )
    with open(OUT, "w") as f:
        f.write(out)
    print(f"Wrote {OUT} ({len(vals)} pixels)")


if __name__ == "__main__":
    main()
