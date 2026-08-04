# SPDX-License-Identifier: GPL-3.0-only
"""Author the custom main-menu logo pieces into a loadable HSD archive.

The archipelago mod's main_menu subsystem keeps the vanilla title's "KIRBY" logo
and blue swoosh and hides only the vanilla "AIR RIDE" subtitle (foreground joint
14). This script builds the pieces that go back in its place, each a single RGBA8
textured quad:

  - AirRide_Archipelago.png  - the "AIRRIDE / ARCHIPELAGO" subtitle, where the
                               vanilla "AIR RIDE" text was.
  - Archipelago_Kirbs-05.png - the six-Kirby cluster, lower-left.

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
    uv run --with pillow python scripts/hsd/make_menu_logo.py
"""
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hsd.archive import build_archive
from hsd.gx import GX_TF_RGBA8, align32, encode_rgba8

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASSETS = os.path.join(ROOT, "mods", "archipelago", "assets")
OUT_DAT = os.path.join(ASSETS, "MnTitleKarchi.dat")

PUBLIC = "karchiTitleFg_scene_models"
ARCHIVE_VERSION = b"001B"

# Piece placement is derived from where each piece sits in karchipelago-logo.png:
# the combined logo is composited from these exact pieces at native size, so each
# piece's pixel box within it (found by edge template-matching, scale 1.0) fixes the
# relative layout. That layout is mapped into the title foreground scene (XY plane
# at Z=0, +Y up) by one transform: the combined logo's opaque content box maps to a
# world rect centered at WORLD_CENTER with width WORLD_CONTENT_W. These two are the
# global tuning knobs (position + overall scale); the relative layout stays locked
# to the png. WORLD_CONTENT_W=51.3 matches the vanilla logo's on-screen size.
LOGO_CONTENT_BOX = (144, 237, 4834, 2649)   # opaque content box of karchipelago-logo.png
WORLD_CENTER = (-4.0, 4.5)                    # world point the content-box center maps to
WORLD_CONTENT_W = 45.0                        # world width of the content box

# Each piece: source png, its pixel box (x0,y0,x1,y1) within karchipelago-logo.png,
# and texture width in texels (RGBA8; height derived from the cropped aspect).
PIECES = [
    dict(src="AirRide_Archipelago.png", px=(900, 1860, 4830, 2645), tex_w=384),
    dict(src="Archipelago_Kirbs-05.png", px=(145, 1290, 1335, 2505), tex_w=160),
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
SZ_JOBJSET = 0x10
SZ_JOBJ = 0x40
SZ_DOBJ = 0x10
SZ_MOBJ = 0x18
SZ_TOBJ = 0x5C
SZ_MAT = 0x14
SZ_IMG = 0x18
SZ_POBJ = 0x18
SZ_VTX_ENTRY = 0x18

# JOBJ flags.
JOBJ_CLASSICAL_SCALING = 0x00000008
JOBJ_XLU = 0x00080000
JOBJ_ROOT_XLU = 0x20000000

# GX vertex attribute constants.
GX_VA_POS = 9
GX_VA_TEX0 = 13
GX_INDEX8 = 2
GX_POS_XYZ = 1
GX_TEX_ST = 1
GX_F32 = 4

# Vanilla single-texture quad render mode: unlit CONSTANT material, TEX0 enabled,
# NO_ZUPDATE, XLU pass, alpha from material (modulated by the texture via the
# TObj's AM_MODULATE) - so material alpha 1.0 lets the texture's own alpha cut the
# art out of its transparent background.
MOBJ_RENDER_MODE = 0x60002011
# TObj scalar config: id=TEXMAP0, src=TEX0 coords, identity SRT, flags
# COORD_UV|LIGHTMAP_DIFFUSE|CM_MODULATE|AM_MODULATE.
TOBJ_ID = 0
TOBJ_SRC = 4
TOBJ_FLAGS = 0x00340010
TOBJ_BLEND = 1.0
TOBJ_MAGFILTER = 1
POBJ_FLAGS_CULLFRONT = 0x8000


def build_model_archive(pieces):
    """pieces: list of dict(texels, tex_w, tex_h, cx, cy, w, h)."""
    data = bytearray()
    relocs = []

    def reserve(size):
        off = len(data)
        data.extend(b"\0" * size)
        return off

    def blob(payload):
        data.extend(b"\0" * align32(len(data)))
        off = len(data)
        data.extend(payload)
        return off

    def w32(off, val):
        struct.pack_into(">I", data, off, val & 0xFFFFFFFF)

    def wf32(off, val):
        struct.pack_into(">f", data, off, val)

    def ptr(off, target):
        """Store target data-offset at off and record it as a relocation."""
        w32(off, target)
        relocs.append(off)

    # Fixed-size structs first, so every offset is known before writing.
    public_arr = reserve(8)      # [JOBJSet*, NULL]
    jobjset = reserve(SZ_JOBJSET)
    root = reserve(SZ_JOBJ)
    for p in pieces:
        p["jobj"] = reserve(SZ_JOBJ)
        p["dobj"] = reserve(SZ_DOBJ)
        p["mobj"] = reserve(SZ_MOBJ)
        p["tobj"] = reserve(SZ_TOBJ)
        p["mat"] = reserve(SZ_MAT)
        p["img"] = reserve(SZ_IMG)
        p["pobj"] = reserve(SZ_POBJ)
        p["vtx"] = reserve(3 * SZ_VTX_ENTRY)

    # 32-aligned GX blobs, per piece.
    for p in pieces:
        hw, hh = p["w"] / 2.0, p["h"] / 2.0
        x0, x1 = p["cx"] - hw, p["cx"] + hw
        y0, y1 = p["cy"] - hh, p["cy"] + hh
        # BL, TL, BR, TR (matches the vanilla quad topology + UVs).
        positions = struct.pack(">12f", x0, y0, 0.0, x0, y1, 0.0,
                                x1, y0, 0.0, x1, y1, 0.0)
        uvs = struct.pack(">8f", 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0)
        # TRIANGLESTRIP (0x98) | VTXFMT0, 4 verts, each: POS idx8, TEX0 idx8.
        dl = bytes([0x98, 0x00, 0x04, 0, 0, 1, 1, 2, 2, 3, 3])
        dl += b"\0" * align32(len(dl))
        p["pos_off"] = blob(positions)
        p["uv_off"] = blob(uvs)
        p["dl_off"] = blob(dl)
        p["tex_off"] = blob(p["texels"])

    def vtx_entry(base, attr, comp_cnt, stride, vptr):
        w32(base + 0x00, attr)
        w32(base + 0x04, GX_INDEX8)
        w32(base + 0x08, comp_cnt)
        w32(base + 0x0C, GX_F32)
        data[base + 0x10] = 0  # frac
        struct.pack_into(">H", data, base + 0x12, stride)
        ptr(base + 0x14, vptr)

    # public array -> JOBJSet, then NULL terminator.
    ptr(public_arr + 0x00, jobjset)
    # JOBJSet: jobj (no anim arrays).
    ptr(jobjset + 0x00, root)
    # root JOBJDesc: ROOT_XLU, first child, identity SRT.
    w32(root + 0x04, JOBJ_CLASSICAL_SCALING | JOBJ_ROOT_XLU)
    ptr(root + 0x08, pieces[0]["jobj"])
    wf32(root + 0x20, 1.0); wf32(root + 0x24, 1.0); wf32(root + 0x28, 1.0)

    for i, p in enumerate(pieces):
        jobj, dobj, mobj = p["jobj"], p["dobj"], p["mobj"]
        tobj, mat, img = p["tobj"], p["mat"], p["img"]
        pobj, vtx = p["pobj"], p["vtx"]

        # child JOBJDesc: XLU, dobj, next sibling, identity SRT.
        w32(jobj + 0x04, JOBJ_CLASSICAL_SCALING | JOBJ_XLU)
        ptr(jobj + 0x10, dobj)
        if i + 1 < len(pieces):
            ptr(jobj + 0x0C, pieces[i + 1]["jobj"])
        wf32(jobj + 0x20, 1.0); wf32(jobj + 0x24, 1.0); wf32(jobj + 0x28, 1.0)

        # DObjDesc: mobj, pobj.
        ptr(dobj + 0x08, mobj)
        ptr(dobj + 0x0C, pobj)

        # MObjDesc: render mode, tobj, material.
        w32(mobj + 0x04, MOBJ_RENDER_MODE)
        ptr(mobj + 0x08, tobj)
        ptr(mobj + 0x0C, mat)

        # TObjDesc: scalar config, identity SRT/scale, imagedesc.
        w32(tobj + 0x08, TOBJ_ID)
        w32(tobj + 0x0C, TOBJ_SRC)
        wf32(tobj + 0x1C, 1.0); wf32(tobj + 0x20, 1.0); wf32(tobj + 0x24, 1.0)
        data[tobj + 0x3C] = 1  # repeat_s
        data[tobj + 0x3D] = 1  # repeat_t
        w32(tobj + 0x40, TOBJ_FLAGS)
        wf32(tobj + 0x44, TOBJ_BLEND)
        w32(tobj + 0x48, TOBJ_MAGFILTER)
        ptr(tobj + 0x4C, img)

        # MaterialDesc: white + opaque so the texture renders untinted.
        w32(mat + 0x00, 0xFFFFFFFF)  # ambient
        w32(mat + 0x04, 0xFFFFFFFF)  # diffuse
        w32(mat + 0x08, 0xFFFFFFFF)  # specular
        wf32(mat + 0x0C, 1.0)        # alpha
        wf32(mat + 0x10, 50.0)       # shininess

        # ImageDesc: texel ptr, dims, RGBA8.
        ptr(img + 0x00, p["tex_off"])
        struct.pack_into(">HH", data, img + 0x04, p["tex_w"], p["tex_h"])
        w32(img + 0x08, GX_TF_RGBA8)

        # POBJDesc: verts, flags/n_display, display list.
        ptr(pobj + 0x08, vtx)
        struct.pack_into(">HH", data, pobj + 0x0C, POBJ_FLAGS_CULLFRONT, 1)
        ptr(pobj + 0x10, p["dl_off"])

        # VtxDescList: POS (index8, xyz f32), TEX0 (index8, st f32), terminator.
        vtx_entry(vtx + 0 * SZ_VTX_ENTRY, GX_VA_POS, GX_POS_XYZ, 12, p["pos_off"])
        vtx_entry(vtx + 1 * SZ_VTX_ENTRY, GX_VA_TEX0, GX_TEX_ST, 8, p["uv_off"])
        w32(vtx + 2 * SZ_VTX_ENTRY + 0x00, 0xFF)  # terminator

    return build_archive(data, relocs, [(PUBLIC, public_arr)], ARCHIVE_VERSION)


def round4(n):
    return max(4, int(round(n / 4.0)) * 4)


def main():
    pieces = []
    total_tex = 0
    for cfg in PIECES:
        cx, cy, w = map_to_world(cfg["px"])
        src = Image.open(os.path.join(ASSETS, cfg["src"])).convert("RGBA")
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
        pieces.append(dict(texels=texels, tex_w=tex_w, tex_h=tex_h,
                           cx=cx, cy=cy, w=w, h=world_h))
        print(f"{cfg['src']:28s} {tex_w}x{tex_h} RGBA8 ({len(texels)/1024:.0f} KB)  "
              f"quad {w:.1f}x{world_h:.1f} @ ({cx:.2f}, {cy:.2f})")

    archive = build_model_archive(pieces)
    with open(OUT_DAT, "wb") as f:
        f.write(archive)
    print(f"textures total {total_tex/1024:.0f} KB")
    print(f"wrote {OUT_DAT} ({len(archive)/1024:.1f} KB, public '{PUBLIC}')")


if __name__ == "__main__":
    main()
