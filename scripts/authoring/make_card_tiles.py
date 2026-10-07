# SPDX-License-Identifier: GPL-3.0-only
"""Author the archipelago mod's memory-card tile, ApIcon.dat and ApBanner.dat.

hoshi's save tile is two standard GX RGB5A3 textures - a 32x32 icon and a 96x32
banner - emitted as raw big-endian u16 blobs that ship on the disc and are read
straight into the card tile at runtime, so no image is baked into the mod's
code. The output names carry no underscore or period on purpose: the game's file
loader appends ".dat" only to names without one (others are taken as already
complete).

The source is scaled to fit inside the tile with its aspect ratio preserved and
centered on transparency - a wide source simply gets transparent bars.

Run from the repo root (both tiles, or just `icon` / `banner`):
    uv run --with pillow python scripts/authoring/make_card_tiles.py
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
    canvas.paste(
        img.resize(size, Image.LANCZOS),
        ((width - size[0]) // 2, (height - size[1]) // 2),
    )
    return canvas


def write_tile(which):
    src, out, width, height = TILES[which]
    img = Image.open(os.path.join(ART, src)).convert("RGBA")
    data = encode_rgb5a3(fit_centered(img, width, height))
    path = os.path.join(ASSETS, out)
    with open(path, "wb") as f:
        f.write(data)
    print(f"Wrote {path} ({width}x{height}, {len(data)} bytes)")


def main():
    which = sys.argv[1:] or list(TILES)
    for name in which:
        if name not in TILES:
            sys.exit(f"usage: make_card_tiles.py [{'|'.join(TILES)}]")
    for name in which:
        write_tile(name)


if __name__ == "__main__":
    main()
