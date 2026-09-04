# SPDX-License-Identifier: GPL-3.0-only
"""Flat textured quads in a scene-model archive.

A HUD or menu image is drawn as a screen-facing quad: one DObj whose material
carries a single texture, over a POBJ of four indexed vertices. That leaf is the
same wherever it appears; what differs between callers is the joint tree above
it - a sibling chain under one root, or one root per image - so this covers the
leaf and leaves the JObjDesc and the public array to the caller.

Reserve the structures with `reserve_quad` in the order they are laid out, append
the 32-aligned GX payloads, then fill the structures with `write_quad`.
"""

import struct
from types import SimpleNamespace

from .gx import align32
from .schema import type_size

SZ_JOBJ = type_size("JOBJDesc")
SZ_DOBJ = type_size("DObjDesc")
SZ_MOBJ = type_size("MObjDesc")
SZ_TOBJ = type_size("TObjDesc")
SZ_MAT = type_size("MaterialDesc")
SZ_IMG = type_size("ImageDesc")
SZ_POBJ = type_size("POBJDesc")
SZ_VTX_ENTRY = 0x18  # VtxDescList record; the array ends at a 0xFF attribute
SZ_JOBJSET = 0x10

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
# POBJ flag 0x8000 selects GX_CULL_BACK.
POBJ_CULLBACK = 1 << 15

# Four verts in BL, TL, BR, TR order, each carrying a POS and a TEX0 index.
QUAD_UVS = struct.pack(">8f", 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0)
QUAD_DL = bytes([0x98, 0x00, 0x04, 0, 0, 1, 1, 2, 2, 3, 3])  # TRIANGLESTRIP|VTXFMT0
QUAD_DL += b"\0" * align32(len(QUAD_DL))


def quad_positions(x0, y0, x1, y1):
    """The quad's corners, in the order QUAD_UVS and QUAD_DL index."""
    return struct.pack(">12f", x0, y0, 0.0, x0, y1, 0.0, x1, y0, 0.0, x1, y1, 0.0)


def vtx_entry(blob, off, attr, comp_cnt, stride, vertices):
    """One VtxDescList record: an INDEX8 attribute over an array of f32."""
    blob.set_u32(off + 0x00, attr)
    blob.set_u32(off + 0x04, GX_INDEX8)
    blob.set_u32(off + 0x08, comp_cnt)
    blob.set_u32(off + 0x0C, GX_F32)
    blob.data[off + 0x10] = 0  # frac
    struct.pack_into(">H", blob.data, off + 0x12, stride)
    blob.ptr(off + 0x14, vertices)


def reserve_quad(blob):
    """Reserve one quad's fixed-size structures. Every offset has to be known
    before any of them is written, so callers reserve first and fill later."""
    q = SimpleNamespace()
    for name, size in (
        ("dobj", SZ_DOBJ),
        ("mobj", SZ_MOBJ),
        ("tobj", SZ_TOBJ),
        ("mat", SZ_MAT),
        ("img", SZ_IMG),
        ("pobj", SZ_POBJ),
        ("vtx", 3 * SZ_VTX_ENTRY),
    ):
        setattr(q, name, blob.append(b"\0" * size))
    return q


def write_quad(
    blob,
    q,
    pos_off,
    uv_off,
    dl_off,
    tex_off,
    width,
    height,
    fmt,
    render_mode=MOBJ_RENDER_MODE,
    n_display=1,
):
    """Fill a reserved quad. The payload arguments are offsets of blobs the
    caller has already appended 32-aligned; `fmt` is the texture's GXTexFmt."""
    blob.ptr(q.dobj + 0x08, q.mobj)
    blob.ptr(q.dobj + 0x0C, q.pobj)

    blob.set_u32(q.mobj + 0x04, render_mode)
    blob.ptr(q.mobj + 0x08, q.tobj)
    blob.ptr(q.mobj + 0x0C, q.mat)

    blob.set_u32(q.tobj + 0x08, TOBJ_ID)
    blob.set_u32(q.tobj + 0x0C, TOBJ_SRC)
    for i in range(3):
        blob.set_f32(q.tobj + 0x1C + 4 * i, 1.0)  # identity texture scale
    blob.data[q.tobj + 0x3C] = 1  # repeat_s
    blob.data[q.tobj + 0x3D] = 1  # repeat_t
    blob.set_u32(q.tobj + 0x40, TOBJ_FLAGS)
    blob.set_f32(q.tobj + 0x44, TOBJ_BLEND)
    blob.set_u32(q.tobj + 0x48, TOBJ_MAGFILTER)
    blob.ptr(q.tobj + 0x4C, q.img)

    # White and opaque, so the texture renders untinted.
    for i in range(3):
        blob.set_u32(q.mat + 4 * i, 0xFFFFFFFF)  # ambient, diffuse, specular
    blob.set_f32(q.mat + 0x0C, 1.0)  # alpha
    blob.set_f32(q.mat + 0x10, 50.0)  # shininess

    blob.ptr(q.img + 0x00, tex_off)
    struct.pack_into(">HH", blob.data, q.img + 0x04, width, height)
    blob.set_u32(q.img + 0x08, fmt)

    blob.ptr(q.pobj + 0x08, q.vtx)
    struct.pack_into(">HH", blob.data, q.pobj + 0x0C, POBJ_CULLBACK, n_display)
    blob.ptr(q.pobj + 0x10, dl_off)

    vtx_entry(blob, q.vtx + 0 * SZ_VTX_ENTRY, GX_VA_POS, GX_POS_XYZ, 12, pos_off)
    vtx_entry(blob, q.vtx + 1 * SZ_VTX_ENTRY, GX_VA_TEX0, GX_TEX_ST, 8, uv_off)
    blob.set_u32(q.vtx + 2 * SZ_VTX_ENTRY, 0xFF)  # terminator


def write_jobj(blob, off, flags, child=None, next_sibling=None, dobj=None):
    """A JObjDesc with an identity SRT, which is what a screen-space quad wants."""
    blob.set_u32(off + 0x04, flags)
    if child is not None:
        blob.ptr(off + 0x08, child)
    if next_sibling is not None:
        blob.ptr(off + 0x0C, next_sibling)
    if dobj is not None:
        blob.ptr(off + 0x10, dobj)
    for i in range(3):
        blob.set_f32(off + 0x20 + 4 * i, 1.0)
