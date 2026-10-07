# SPDX-License-Identifier: GPL-3.0-only
"""Author the AP Patch custom item into mods/archipelago/assets/items/ApPatch.dat.

A carve of ITKIND_OFFENSE's own model (flat billboard, state script, pickup
reaction and SFX) through scripts/hsd/carve_custom_item.py, with three
overrides:

  * the stat glyph texture is replaced by art/ap-patch.png;
  * the stat grant is an empty PatchEffectInfo, so a pickup applies nothing
    and the archipelago mod's pickup handler does the rest;
  * every spawn weight is zero and the render scale is 0.9, so it never enters
    a spawn pool and sits a little smaller than a stat patch.

Run from the repo root:
    uv run --with pillow python scripts/authoring/make_ap_patch.py
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hsd import carve_custom_item

ITEM_DAT = "iso/files/Item.dat"
SOURCE_KIND = 7  # ITKIND_OFFENSE
OUT_DAT = "mods/archipelago/assets/items/ApPatch.dat"
NAME = "AP Patch"  # AP_PATCH_ITEM_NAME in ap_patches.h
TEXTURE = "art/ap-patch.png"


def main():
    return carve_custom_item.main([
        "carve_custom_item.py",
        ITEM_DAT,
        str(SOURCE_KIND),
        OUT_DAT,
        NAME,
        "--scale", "0.9",
        "--no-effect",
        "--weight-blue", "0",
        "--texture", TEXTURE,
    ])


if __name__ == "__main__":
    sys.exit(main())
