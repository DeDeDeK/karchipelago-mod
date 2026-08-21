# SPDX-License-Identifier: GPL-3.0-only
"""Author ApStarShot.dat, the model the Archipelago Star's fired sphere wears.

The shot is a plasma-spread projectile with its model swapped out at spawn, so
this archive holds nothing but a joint tree: a two-joint model whose leaf carries
one lit, untextured UV sphere. It is the same mesh and the same material setup
the six assembly spheres use, at a smaller radius - the spheres are pickups
sitting still on the ground, this is a bullet.

The material ships white. The mod writes the ambient and diffuse of the loaded
copy per shot, taking the color of whichever pod launched it, which reaches a
pixel because the material renders CONSTANT with no texture stage over it.

`Projectile_Create` reads the model through a two-word block at the kind's
`ProjKindData+0x08`: word 0 is this tree's root, and the top byte of word 1 is
how many joints the tree has. The joint walker at 0x80221914 asserts on a
mismatch and on any count above 10, which is why the tree is kept to two.

Run from the repo root:
    uv run python scripts/hsd/make_ap_star_shot.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hsd.archive import build_archive
from hsd.gx import align32
from hsd.make_ap_star_pieces import (GX_F32, GX_INDEX8, GX_NRM_XYZ, GX_POS_XYZ,
                                     GX_VA_NRM, GX_VA_POS,
                                     JOBJ_CLASSICAL_SCALING, JOBJ_LIGHTING,
                                     JOBJ_OPA, JOBJ_ROOT_OPA,
                                     MOBJ_RENDER_MODE, POBJ_CULLBACK, SZ_DOBJ,
                                     SZ_JOBJ, SZ_MAT, SZ_MOBJ, SZ_POBJ,
                                     SZ_VTX_ENTRY, display_list, sphere_mesh)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "mods", "ap_star", "assets", "ApStarShot.dat")

ARCHIVE_VERSION = b"001B"
PUBLIC = "apStarShot_model"

# Smaller and coarser than an assembly sphere: it is seen in motion, briefly.
SPHERE_RADIUS = 3.0
SPHERE_SEGMENTS = 12
SPHERE_RINGS = 8

WHITE = 0xFFFFFFFF


def build():
    positions, normals, prims = sphere_mesh(SPHERE_RADIUS, SPHERE_SEGMENTS, SPHERE_RINGS)
    if len(positions) > 255:
        raise SystemExit("index8 vertex arrays hold 255 entries; lower the resolution")

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
        w32(off, target)
        relocs.append(off)

    root = reserve(SZ_JOBJ)
    child = reserve(SZ_JOBJ)
    dobj = reserve(SZ_DOBJ)
    mobj = reserve(SZ_MOBJ)
    mat = reserve(SZ_MAT)
    pobj = reserve(SZ_POBJ)
    vtx = reserve(3 * SZ_VTX_ENTRY)

    pos_off = blob(b"".join(struct.pack(">3f", *p) for p in positions))
    nrm_off = blob(b"".join(struct.pack(">3f", *n) for n in normals))
    dl = display_list(prims)
    dl_off = blob(dl)

    w32(root + 0x04, JOBJ_ROOT_OPA | JOBJ_CLASSICAL_SCALING)
    ptr(root + 0x08, child)
    wf32(root + 0x20, 1.0); wf32(root + 0x24, 1.0); wf32(root + 0x28, 1.0)

    w32(child + 0x04, JOBJ_OPA | JOBJ_LIGHTING | JOBJ_CLASSICAL_SCALING)
    ptr(child + 0x10, dobj)
    wf32(child + 0x20, 1.0); wf32(child + 0x24, 1.0); wf32(child + 0x28, 1.0)

    ptr(dobj + 0x08, mobj)
    ptr(dobj + 0x0C, pobj)

    w32(mobj + 0x04, MOBJ_RENDER_MODE)
    ptr(mobj + 0x0C, mat)

    w32(mat + 0x00, WHITE)  # ambient
    w32(mat + 0x04, WHITE)  # diffuse
    w32(mat + 0x08, WHITE)  # specular, unused without the bit
    wf32(mat + 0x0C, 1.0)   # alpha
    wf32(mat + 0x10, 50.0)  # shininess

    ptr(pobj + 0x08, vtx)
    struct.pack_into(">HH", data, pobj + 0x0C, POBJ_CULLBACK, len(dl) // 32)
    ptr(pobj + 0x10, dl_off)

    def vtx_entry(base, attr, comp_cnt, stride, vptr):
        w32(base + 0x00, attr)
        w32(base + 0x04, GX_INDEX8)
        w32(base + 0x08, comp_cnt)
        w32(base + 0x0C, GX_F32)
        data[base + 0x10] = 0  # frac
        struct.pack_into(">H", data, base + 0x12, stride)
        ptr(base + 0x14, vptr)

    vtx_entry(vtx + 0 * SZ_VTX_ENTRY, GX_VA_POS, GX_POS_XYZ, 12, pos_off)
    vtx_entry(vtx + 1 * SZ_VTX_ENTRY, GX_VA_NRM, GX_NRM_XYZ, 12, nrm_off)
    w32(vtx + 2 * SZ_VTX_ENTRY + 0x00, 0xFF)  # terminator

    tris = sum(len(v) - 2 for _, v in prims)
    print(f"sphere r={SPHERE_RADIUS} {SPHERE_SEGMENTS}x{SPHERE_RINGS}: "
          f"{len(positions)} verts, {tris} triangles, 2 joints")
    return build_archive(data, relocs, [(PUBLIC, root)], ARCHIVE_VERSION)


def main():
    archive = build()
    with open(OUT, "wb") as f:
        f.write(archive)
    print(f"  {os.path.relpath(OUT, ROOT)} ({len(archive)} bytes, public '{PUBLIC}')")


if __name__ == "__main__":
    main()
