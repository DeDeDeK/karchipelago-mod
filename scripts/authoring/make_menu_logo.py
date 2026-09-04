# SPDX-License-Identifier: GPL-3.0-only
"""Author the custom main-menu logo pieces into a loadable HSD archive.

The archipelago mod's main_menu subsystem keeps the vanilla title's "KIRBY" logo
and blue swoosh and hides only the vanilla "AIR RIDE" subtitle (foreground joint
14). This script builds the pieces that go back in its place, each a single RGBA8
textured quad:

  - ap-banner.png - the "AIRRIDE / ARCHIPELAGO" subtitle, where the vanilla
                    "AIR RIDE" text was.
  - ap-kirbs.png  - the six-Kirby cluster, lower-left.

Keeping the vanilla Kirby+swoosh (already resident) and shipping only these two
small textures keeps the title-heap footprint low. All pieces are packed into one
standalone .dat exporting one public:

  karchiTitleFg_scene_models  - HSDNullPointerArrayAccessor<HSD_JOBJDesc>
                                (a NULL-terminated array of one JOBJSet)

The mod loads this file (Gm_LoadGameFile "MnTitleKarchi") on title load and
instantiates it with MenuElement_Create(set[0]->jobj), which renders it in the
title foreground scene's coordinate space (an XY plane at Z=0, +Y up, camera
looking down -Z). Each piece is cropped to its opaque content box, so the quad
bounds the visible art exactly and its world center/width place it directly.

The model tree is a root JOBJDesc (ROOT_XLU) with one child JOBJDesc (XLU) per
piece, each carrying:

  DObjDesc -> MObjDesc (render 0x60002011: unlit CONSTANT, TEX0, XLU, alpha from
                        material * texture) -> MaterialDesc (white, opaque) +
                        TObjDesc (COORD_UV, MODULATE) -> ImageDesc (RGBA8)
           -> POBJDesc -> VtxDescList (POS index8 f32, TEX0 index8 f32) + a
                          4-vertex TRIANGLESTRIP display list over private arrays.

Run from the repo root:
    uv run --with pillow python scripts/authoring/make_menu_logo.py
"""

import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hsd.archive import Blob, build_archive
from hsd.gx import GX_TF_RGBA8, encode_rgba8
from hsd.quad_model import (
    JOBJ_CLASSICAL_SCALING,
    JOBJ_ROOT_XLU,
    JOBJ_XLU,
    QUAD_DL,
    QUAD_UVS,
    SZ_JOBJ,
    SZ_JOBJSET,
    quad_positions,
    reserve_quad,
    write_jobj,
    write_quad,
)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ART = os.path.join(ROOT, "art")
ASSETS = os.path.join(ROOT, "mods", "archipelago", "assets")
OUT_DAT = os.path.join(ASSETS, "MnTitleKarchi.dat")

PUBLIC = "karchiTitleFg_scene_models"

# Piece placement is derived from where each piece sits in karchipelago-logo.png:
# the combined logo is composited from these exact pieces at native size, so each
# piece's pixel box within it (found by edge template-matching, scale 1.0) fixes the
# relative layout. That layout is mapped into the title foreground scene (XY plane
# at Z=0, +Y up) by one transform: the combined logo's opaque content box maps to a
# world rect centered at WORLD_CENTER with width WORLD_CONTENT_W. These two are the
# global tuning knobs (position + overall scale); the relative layout stays locked
# to the png. WORLD_CONTENT_W=51.3 matches the vanilla logo's on-screen size.
LOGO_CONTENT_BOX = (144, 237, 4834, 2649)  # opaque content box of karchipelago-logo.png
WORLD_CENTER = (-4.0, 4.5)  # world point the content-box center maps to
WORLD_CONTENT_W = 45.0  # world width of the content box

# Each piece: source png, its pixel box (x0,y0,x1,y1) within karchipelago-logo.png,
# and texture width in texels (RGBA8; height derived from the cropped aspect).
PIECES = [
    dict(src="ap-banner.png", px=(900, 1860, 4830, 2645), tex_w=384),
    dict(src="ap-kirbs.png", px=(145, 1290, 1335, 2505), tex_w=160),
]


def map_to_world(px):
    """Map a pixel box in karchipelago-logo.png to a quad (world cx, cy, width)."""
    x0, y0, x1, y1 = px
    lx0, ly0, lx1, ly1 = LOGO_CONTENT_BOX
    lw, lh = lx1 - lx0, ly1 - ly0
    world_w_total = WORLD_CONTENT_W
    world_h_total = world_w_total * lh / lw
    ccx, ccy = (lx0 + lx1) / 2.0, (ly0 + ly1) / 2.0
    cx = WORLD_CENTER[0] + ((x0 + x1) / 2.0 - ccx) / lw * world_w_total
    cy = WORLD_CENTER[1] - ((y0 + y1) / 2.0 - ccy) / lh * world_h_total  # y flips
    return cx, cy, (x1 - x0) / lw * world_w_total


# Struct sizes.
def build_model_archive(pieces):
    """pieces: list of dict(texels, tex_w, tex_h, cx, cy, w, h)."""
    blob = Blob()

    # Fixed-size structs first, so every offset is known before writing.
    public_arr = blob.append(b"\0" * 8)  # [JOBJSet*, NULL]
    jobjset = blob.append(b"\0" * SZ_JOBJSET)
    root = blob.append(b"\0" * SZ_JOBJ)
    for p in pieces:
        p["jobj"] = blob.append(b"\0" * SZ_JOBJ)
        p["quad"] = reserve_quad(blob)

    # 32-aligned GX blobs, per piece.
    for p in pieces:
        hw, hh = p["w"] / 2.0, p["h"] / 2.0
        p["pos_off"] = blob.append(
            quad_positions(p["cx"] - hw, p["cy"] - hh, p["cx"] + hw, p["cy"] + hh), 32
        )
        p["uv_off"] = blob.append(QUAD_UVS, 32)
        p["dl_off"] = blob.append(QUAD_DL, 32)
        p["tex_off"] = blob.append(p["texels"], 32)

    blob.ptr(public_arr + 0x00, jobjset)
    blob.ptr(jobjset + 0x00, root)
    write_jobj(
        blob, root, JOBJ_CLASSICAL_SCALING | JOBJ_ROOT_XLU, child=pieces[0]["jobj"]
    )

    for i, p in enumerate(pieces):
        write_jobj(
            blob,
            p["jobj"],
            JOBJ_CLASSICAL_SCALING | JOBJ_XLU,
            dobj=p["quad"].dobj,
            next_sibling=pieces[i + 1]["jobj"] if i + 1 < len(pieces) else None,
        )
        write_quad(
            blob,
            p["quad"],
            p["pos_off"],
            p["uv_off"],
            p["dl_off"],
            p["tex_off"],
            p["tex_w"],
            p["tex_h"],
            GX_TF_RGBA8,
        )

    return build_archive(blob.data, blob.relocs, [(PUBLIC, public_arr)])


def round4(n):
    return max(4, int(round(n / 4.0)) * 4)


def main():
    pieces = []
    total_tex = 0
    for cfg in PIECES:
        cx, cy, w = map_to_world(cfg["px"])
        src = Image.open(os.path.join(ART, cfg["src"])).convert("RGBA")
        box = src.getbbox()  # opaque content box; drop the transparent margin
        if box:
            src = src.crop(box)
        aspect = src.width / src.height
        tex_w = round4(cfg["tex_w"])
        tex_h = round4(tex_w / aspect)
        world_h = w * tex_h / tex_w
        src = src.resize((tex_w, tex_h), Image.LANCZOS)
        texels = encode_rgba8(src)
        total_tex += len(texels)
        pieces.append(
            dict(texels=texels, tex_w=tex_w, tex_h=tex_h, cx=cx, cy=cy, w=w, h=world_h)
        )
        print(
            f"{cfg['src']:28s} {tex_w}x{tex_h} RGBA8 ({len(texels) / 1024:.0f} KB)  "
            f"quad {w:.1f}x{world_h:.1f} @ ({cx:.2f}, {cy:.2f})"
        )

    archive = build_model_archive(pieces)
    with open(OUT_DAT, "wb") as f:
        f.write(archive)
    print(f"textures total {total_tex / 1024:.0f} KB")
    print(f"wrote {OUT_DAT} ({len(archive) / 1024:.1f} KB, public '{PUBLIC}')")


if __name__ == "__main__":
    main()
