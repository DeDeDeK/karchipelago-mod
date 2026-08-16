#!/usr/bin/env python3
"""Convert a PNG into a GameCube memory-card tile (RGB5A3) raw .dat blob.

hoshi's save tile is two standard GX RGB5A3 textures - a 32x32 icon and a 96x32
banner - emitted as raw big-endian u16 blobs that ship on the disc and are read
straight into the card tile at runtime, so no image is baked into the mod's
code. The output names carry no underscore or period on purpose: the game's file
loader appends ".dat" only to names without one (others are taken as already
complete).

The source is scaled to fit inside the tile with its aspect ratio preserved and
centered on transparency - a wide source simply gets transparent bars.

Run from the repo root:
    uv run --with pillow python scripts/utility/card_tile.py icon
    uv run --with pillow python scripts/utility/card_tile.py banner
"""

import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hsd.gx import encode_rgb5a3

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ART = os.path.join(ROOT, "art")
ASSETS = os.path.join(ROOT, "mods", "archipelago", "assets")

TILES = {
    "icon": ("ap-icon.png", "ApIcon.dat", 32, 32),
    "banner": ("ap-banner.png", "ApBanner.dat", 96, 32),
}


def fit_centered(img, width, height):
    """Scale img to fit within width x height, centered on transparency."""
    scale = min(width / img.width, height / img.height)
    size = (max(1, round(img.width * scale)), max(1, round(img.height * scale)))
    canvas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    canvas.paste(img.resize(size, Image.LANCZOS),
                 ((width - size[0]) // 2, (height - size[1]) // 2))
    return canvas


def main():
    which = sys.argv[1] if len(sys.argv) == 2 else None
    if which not in TILES:
        sys.exit(f"usage: card_tile.py {{{'|'.join(TILES)}}}")

    src, out, width, height = TILES[which]
    img = Image.open(os.path.join(ART, src)).convert("RGBA")
    data = encode_rgb5a3(fit_centered(img, width, height))
    path = os.path.join(ASSETS, out)
    with open(path, "wb") as f:
        f.write(data)
    print(f"Wrote {path} ({width}x{height}, {len(data)} bytes)")


if __name__ == "__main__":
    main()
