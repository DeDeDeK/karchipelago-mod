# SPDX-License-Identifier: GPL-3.0-only
"""Author the Miracle Fruit custom item into mods/hypernova/assets/items/MiracleFruit.dat.

A carve of the Bomb copy panel's model (ITKIND_COPYBOMB) through
scripts/hsd/carve_custom_item.py with art/miracle-fruit.png as its texture,
cloning the Maxim Tomato's behavior (ITKIND_FOODMAXIMTOMATO) so it reads as
food. Equal weight in all three box pools, plus Tac and destructible drops; the
hypernova mod's pickup handler grants Hypernova to the collector.

Run from the repo root:
    uv run --with pillow python scripts/authoring/make_miracle_fruit.py
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hsd import carve_custom_item

ITEM_DAT = "iso/files/Item.dat"
SOURCE_KIND = 28  # ITKIND_COPYBOMB
BASE_KIND = 39  # ITKIND_FOODMAXIMTOMATO
OUT_DAT = "mods/hypernova/assets/items/MiracleFruit.dat"
NAME = "Miracle Fruit"  # HYPERNOVA_TRIGGER_ITEM_NAME in hypernova's main.c
TEXTURE = "art/miracle-fruit.png"


def main():
    return carve_custom_item.main([
        "carve_custom_item.py",
        ITEM_DAT,
        str(SOURCE_KIND),
        OUT_DAT,
        NAME,
        "--base-kind", str(BASE_KIND),
        "--texture", TEXTURE,
        "--weight-blue", "40",
        "--weight-green", "40",
        "--weight-red", "40",
        "--ev-tac", "40",
        "--ev-destructible", "40",
    ])


if __name__ == "__main__":
    sys.exit(main())
