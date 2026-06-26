#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measure the geometry bounding radius of an HSD backdrop subtree.

Walks the JObj tree at <symbol>[<slot>], accumulates each joint's
T*R*S transform (with the ROOT joint's scale forced to identity, since
3D_CreateStageModel overwrites the runtime root scale with the stage's
grStageScale at load), parses every POBJ's display list to collect the
position indices actually drawn, decodes the indexed F32/S16 position
buffer, and reports the bounding box / radius about the root origin.

The reported `radius` is the geometry radius at root-scale 1.0, i.e. the
factor that grStageScale multiplies to produce the on-screen sphere
radius. Compare `StageScale * radius` across stages to see the true
on-screen backdrop size.

Usage:
    uv run python scripts/hsd/geom_bounds.py iso/files/GrCity1Model.dat grModelCity1 [slot]
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hsd.archive import Archive, u16, u32


def f32(b, o):
    return struct.unpack(">f", b[o:o + 4])[0]


CTYPE_SIZE = {0: 1, 1: 1, 2: 2, 3: 2, 4: 4}  # U8 S8 U16 S16 F32


def read_comp(data, off, ctype, frac):
    if ctype == 4:
        return f32(data, off)
    if ctype == 0:   # U8
        v = data[off]
    elif ctype == 1:  # S8
        v = struct.unpack(">b", data[off:off + 1])[0]
    elif ctype == 2:  # U16
        v = u16(data, off)
    elif ctype == 3:  # S16
        v = struct.unpack(">h", data[off:off + 2])[0]
    else:
        return 0.0
    return v / (1 << frac)


# --- 3x4 affine matrix helpers (row-major rows of length 4) -----------

def mat_identity():
    return [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0]]


def mat_mul(a, b):
    # a,b are 3x4 affine (implicit 4th row 0 0 0 1).
    out = [[0.0] * 4 for _ in range(3)]
    for i in range(3):
        for j in range(4):
            s = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j]
            if j == 3:
                s += a[i][3]
            out[i][j] = s
    return out


def mat_apply(m, x, y, z):
    return (
        m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3],
        m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3],
        m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3],
    )


def jobj_local_mtx(data, off, force_scale_one=False):
    rx, ry, rz = f32(data, off + 0x14), f32(data, off + 0x18), f32(data, off + 0x1C)
    sx, sy, sz = f32(data, off + 0x20), f32(data, off + 0x24), f32(data, off + 0x28)
    tx, ty, tz = f32(data, off + 0x2C), f32(data, off + 0x30), f32(data, off + 0x34)
    if force_scale_one:
        sx = sy = sz = 1.0
    cx, sxr = math.cos(rx), math.sin(rx)
    cy, syr = math.cos(ry), math.sin(ry)
    cz, szr = math.cos(rz), math.sin(rz)
    # R = Rz * Ry * Rx
    r00 = cz * cy
    r01 = cz * syr * sxr - szr * cx
    r02 = cz * syr * cx + szr * sxr
    r10 = szr * cy
    r11 = szr * syr * sxr + cz * cx
    r12 = szr * syr * cx - cz * sxr
    r20 = -syr
    r21 = cy * sxr
    r22 = cy * cx
    # M = T * R * S
    return [
        [r00 * sx, r01 * sy, r02 * sz, tx],
        [r10 * sx, r11 * sy, r12 * sz, ty],
        [r20 * sx, r21 * sy, r22 * sz, tz],
    ]


def parse_vtxdesc(data, off):
    """Return list of attr dicts and total per-vertex byte size."""
    attrs = []
    cur = off
    while cur + 0x18 <= len(data):
        attr = u32(data, cur)
        if attr == 0xFF:
            break
        atype = u32(data, cur + 0x04)   # 1=DIRECT 2=IDX8 3=IDX16
        ccnt = u32(data, cur + 0x08)
        ctype = u32(data, cur + 0x0C)
        frac = data[cur + 0x10]
        stride = u16(data, cur + 0x12)
        buf = u32(data, cur + 0x14)
        attrs.append(dict(attr=attr, atype=atype, ccnt=ccnt, ctype=ctype,
                          frac=frac, stride=stride, buf=buf))
        cur += 0x18
    return attrs


# GXCompType for COLOR attrs -> bytes when DIRECT (RGB565/RGB8/RGBX8/RGBA4/RGBA6/RGBA8)
CLR_DIRECT_SIZE = {0: 2, 1: 3, 2: 4, 3: 2, 4: 3, 5: 4}


def attr_vertex_bytes(a):
    """Bytes this attribute occupies per-vertex in the DL stream."""
    attr = a["attr"]
    if a["atype"] == 2:   # INDEX8
        return 1
    if a["atype"] == 3:   # INDEX16
        return 2
    if a["atype"] == 1:   # DIRECT
        if attr <= 7:                       # PNMTXIDX / TEXnMTXIDX
            return 1
        if attr == 9:                       # POS
            n = 3 if a["ccnt"] == 1 else 2
            return n * CTYPE_SIZE.get(a["ctype"], 2)
        if attr == 10:                      # NRM (NBT9 ignored; backdrops use NRM)
            return 3 * CTYPE_SIZE.get(a["ctype"], 2)
        if attr in (11, 12):                # CLR0 / CLR1
            return CLR_DIRECT_SIZE.get(a["ctype"], 4)
        if 13 <= attr <= 20:                # TEX0..7
            n = 2 if a["ccnt"] == 1 else 1
            return n * CTYPE_SIZE.get(a["ctype"], 2)
        return CTYPE_SIZE.get(a["ctype"], 2)
    return 0  # NONE


def pos_records(data, dl_off, dl_size, attrs):
    """Return (pos_attr, [rec_off, ...]): the POS attr meta and the byte
    offset where each drawn vertex's position components begin (in the
    indexed vertex buffer, or inline in the DL for DIRECT positions).
    Out-of-range records are skipped."""
    pos_attr = next((a for a in attrs if a["attr"] == 9), None)
    if pos_attr is None:
        return None, []
    voff = 0
    pos_byte_off = 0
    for a in attrs:
        if a["attr"] == 9:
            pos_byte_off = voff
        voff += attr_vertex_bytes(a)
    vsize = voff
    ncomp = 3 if pos_attr["ccnt"] == 1 else 2
    buf = pos_attr["buf"]
    stride = pos_attr["stride"]
    csz = CTYPE_SIZE.get(pos_attr["ctype"], 2)
    atype = pos_attr["atype"]

    recs = []
    end = dl_off + dl_size
    cur = dl_off
    while cur < end:
        op = data[cur]
        if op == 0x00:  # NOP / padding
            cur += 1
            continue
        if (op & 0xF8) not in (0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8):
            break  # not a primitive opcode -> end of meaningful DL
        cur += 1
        count = u16(data, cur)
        cur += 2
        for _ in range(count):
            if atype == 2:
                rec = buf + data[cur + pos_byte_off] * stride
            elif atype == 3:
                rec = buf + u16(data, cur + pos_byte_off) * stride
            else:  # DIRECT: components inline in the DL
                rec = cur + pos_byte_off
            if 0 <= rec and rec + ncomp * csz <= len(data):
                recs.append(rec)
            cur += vsize
    return pos_attr, recs


def _iter_pobjs(arc, off):
    """Yield each POBJ offset hung off the JObj at `off` (via its DObj list)."""
    data = arc.data
    flags = u32(data, off + 0x04)
    if (flags & (1 << 14)) or (flags & (1 << 5)):  # spline / ptcl: no geometry
        return
    dobj = u32(data, off + 0x10) if (off + 0x10) in arc.reloc_set else 0
    while dobj:
        pobj = u32(data, dobj + 0x0C) if (dobj + 0x0C) in arc.reloc_set else 0
        while pobj:
            yield pobj
            pobj = u32(data, pobj + 0x04) if (pobj + 0x04) in arc.reloc_set else 0
        dobj = u32(data, dobj + 0x04) if (dobj + 0x04) in arc.reloc_set else 0


def _pobj_pos_records(arc, pobj):
    """(pos_attr, [rec_off,...]) for one POBJ."""
    data = arc.data
    attrs = parse_vtxdesc(data, u32(data, pobj + 0x08))
    dl = u32(data, pobj + 0x10) if (pobj + 0x10) in arc.reloc_set else 0
    dlsz = u16(data, pobj + 0x0E) * 32
    if not (dl and attrs):
        return None, []
    return pos_records(data, dl, dlsz, attrs)


def measure_root(arc, root):
    """Bounding box / radius of the backdrop subtree at `root`, with the
    root joint's scale forced to identity (the runtime overwrites it)."""
    data = arc.data
    minv = [1e30, 1e30, 1e30]
    maxv = [-1e30, -1e30, -1e30]
    maxr = 0.0
    nverts = 0
    npobj = 0
    seen = set()
    stack = [(root, mat_identity(), True)]
    while stack:
        off, parent_m, is_root = stack.pop()
        if off == 0 or off in seen:
            continue
        seen.add(off)
        world = mat_mul(parent_m, jobj_local_mtx(data, off, force_scale_one=is_root))
        for pobj in _iter_pobjs(arc, off):
            npobj += 1
            pa, recs = _pobj_pos_records(arc, pobj)
            if pa is None:
                continue
            ncomp = 3 if pa["ccnt"] == 1 else 2
            ctype, frac = pa["ctype"], pa["frac"]
            csz = CTYPE_SIZE.get(ctype, 2)
            for rec in recs:
                comps = [read_comp(data, rec + k * csz, ctype, frac) for k in range(ncomp)]
                if ncomp == 2:
                    comps.append(0.0)
                wx, wy, wz = mat_apply(world, *comps)
                minv[0] = min(minv[0], wx); maxv[0] = max(maxv[0], wx)
                minv[1] = min(minv[1], wy); maxv[1] = max(maxv[1], wy)
                minv[2] = min(minv[2], wz); maxv[2] = max(maxv[2], wz)
                r = math.sqrt(wx * wx + wy * wy + wz * wz)
                if r > maxr:
                    maxr = r
                nverts += 1
        nxt = u32(data, off + 0x0C) if (off + 0x0C) in arc.reloc_set else 0
        if nxt and not is_root:  # root pp slot holds a single joint, no siblings
            stack.append((nxt, parent_m, False))
        child = u32(data, off + 0x08) if (off + 0x08) in arc.reloc_set else 0
        if child:
            stack.append((child, world, False))

    half = [(maxv[i] - minv[i]) / 2 for i in range(3)]
    center = [(maxv[i] + minv[i]) / 2 for i in range(3)]
    return dict(root=root, nverts=nverts, npobj=npobj, minv=minv, maxv=maxv,
                center=center, half=half, radius=maxr)


def backdrop_root(arc, sym, slot=1):
    pp = u32(arc.data, arc.publics[sym] + slot * 4)
    return u32(arc.data, pp)


def scale_geometry(arc, root, f):
    """Return a bytearray copy of arc.data with the backdrop subtree at
    `root` uniformly scaled by `f`: every JObj translation and every drawn
    POSITION vertex is multiplied by f.

    Scaling all translations and all leaf positions (leaving rotations and
    per-node scales untouched) is an exact uniform scale of the whole
    hierarchy about the root origin -- independent of classical-scaling
    flags or nesting. Visits the same node set as measure_root(), so
    re-measuring the result yields radius == f * original_radius.

    Positions must be F32 (every KAR backdrop is); integer position
    buffers would need re-quantization and raise instead.
    """
    data = bytearray(arc.data)
    seen = set()
    scaled_recs = set()
    stack = [(root, True)]
    while stack:
        off, is_root = stack.pop()
        if off == 0 or off in seen:
            continue
        seen.add(off)
        for toff in (off + 0x2C, off + 0x30, off + 0x34):  # translation X/Y/Z
            v = struct.unpack_from(">f", data, toff)[0]
            struct.pack_into(">f", data, toff, v * f)
        for pobj in _iter_pobjs(arc, off):
            pa, recs = _pobj_pos_records(arc, pobj)
            if pa is None:
                continue
            if pa["ctype"] != 4:
                raise ValueError(
                    f"POBJ {pobj:#x}: POS ctype {pa['ctype']} is not F32; "
                    "integer position re-quantization is not implemented")
            ncomp = 3 if pa["ccnt"] == 1 else 2
            for rec in recs:
                if rec in scaled_recs:        # buffers are shared across POBJs
                    continue
                scaled_recs.add(rec)
                for k in range(ncomp):
                    o = rec + k * 4
                    v = struct.unpack_from(">f", data, o)[0]
                    struct.pack_into(">f", data, o, v * f)
        nxt = u32(arc.data, off + 0x0C) if (off + 0x0C) in arc.reloc_set else 0
        if nxt and not is_root:
            stack.append((nxt, False))
        child = u32(arc.data, off + 0x08) if (off + 0x08) in arc.reloc_set else 0
        if child:
            stack.append((child, False))
    return data


def measure(path, sym, slot=1):
    arc = Archive(path)
    return measure_root(arc, backdrop_root(arc, sym, slot))


def main(argv):
    path, sym = argv[1], argv[2]
    slot = int(argv[3]) if len(argv) > 3 else 1
    r = measure(path, sym, slot)
    print(f"{os.path.basename(path)} {sym}[{slot}]  root={r['root']:#x}  "
          f"pobj={r['npobj']} verts={r['nverts']}")
    print(f"  bbox min=({r['minv'][0]:.1f},{r['minv'][1]:.1f},{r['minv'][2]:.1f}) "
          f"max=({r['maxv'][0]:.1f},{r['maxv'][1]:.1f},{r['maxv'][2]:.1f})")
    print(f"  center=({r['center'][0]:.1f},{r['center'][1]:.1f},{r['center'][2]:.1f}) "
          f"half=({r['half'][0]:.1f},{r['half'][1]:.1f},{r['half'][2]:.1f})")
    print(f"  radius(root-scale=1) = {r['radius']:.2f}")


if __name__ == "__main__":
    sys.exit(main(sys.argv))
