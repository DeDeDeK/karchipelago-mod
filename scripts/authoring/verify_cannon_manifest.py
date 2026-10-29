#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check CannonManifest.dat by replaying what the mod does at runtime.

Rebuilds both payloads the way custom_events does - read the manifest's ranges out
of the donor file, add the buffer base to every listed pointer, chain the mesh's
DObjs - and then checks that:

  * every range is 32-byte aligned in offset, length and destination, and lands
    inside the donor file
  * every relocated pointer lands inside its payload, or is a rebased vertex array
  * walking each rebuilt DObj, and the rebuilt flight AnimJoint, reaches the same
    object graph, type for type and size for size, as walking the donor's (vertex
    arrays aside, which are cut on purpose)
  * every vertex element any display list indexes reads the same bytes through the
    rebuilt (rebased) array pointer as through the donor's

The last one is what proves the rebasing: the display lists are byte-identical, so
identical elements under identical indices is identical geometry.

Run from the repo root:
    uv run python scripts/authoring/verify_cannon_manifest.py
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import ARCHIVE_VERSION, Archive, cstr, u16, u32
from hsd.geom_bounds import attr_vertex_bytes, parse_vtxdesc
from hsd.payload import ALIGN, GX_INDEX8, GX_INDEX16
from hsd.walker import Walker

from authoring.make_cannon_manifest import (
    DOBJ_NEXT,
    MANIFEST_MAGIC,
    OUTPUT,
    plan,
)

# The replay places each payload this far into its buffer, so a pointer to the
# payload's first byte does not read as NULL to the walker.
ORIGIN = ALIGN


class PayloadView:
    """Archive-shaped view of a rebuilt payload in payload coordinates, so Walker and
    the display-list parsers run over it. `relocs` is exactly the set the runtime
    writes, so a pointer the manifest failed to list goes unfollowed."""

    def __init__(self, buf, relocs):
        self.data = bytes(buf)
        self.relocs = sorted(relocs)
        self.reloc_set = set(relocs)
        self.publics = {}
        self.version = ARCHIVE_VERSION

    def ptr(self, off):
        return u32(self.data, off) if off in self.reloc_set else 0


def rebuild(data, at, errs):
    """Replay one DonorPayload {donor, size, ranges, range_num, relocs, reloc_num}
    with the payload at ORIGIN. Pointers become ORIGIN + value, wrapping below zero
    for a rebased array exactly as base + value does at runtime."""
    donor = cstr(data, u32(data, at))
    size = u32(data, at + 0x04)
    rng_off, rng_num = u32(data, at + 0x08), u32(data, at + 0x0C)
    rel_off, rel_num = u32(data, at + 0x10), u32(data, at + 0x14)

    raw = open(os.path.join("iso/files", donor), "rb").read()
    buf = bytearray(ORIGIN + size)
    for i in range(rng_num):
        o = rng_off + i * 12
        src, dst, length = u32(data, o), u32(data, o + 4), u32(data, o + 8)
        if src % ALIGN or dst % ALIGN or length % ALIGN:
            errs.append(f"{donor} range {i} not {ALIGN}-aligned")
        if src + length > len(raw) or dst + length > size:
            errs.append(f"{donor} range {i} out of bounds")
            continue
        buf[ORIGIN + dst : ORIGIN + dst + length] = raw[src : src + length]

    written = []
    wrapped = set()
    for i in range(rel_num):
        o = rel_off + i * 8
        dst, val = u32(data, o), u32(data, o + 4)
        if dst + 4 > size:
            errs.append(f"{donor} reloc {i} source out of payload")
            continue
        if val >= size:
            wrapped.add(ORIGIN + dst)
        struct.pack_into(">I", buf, ORIGIN + dst, (ORIGIN + val) & 0xFFFFFFFF)
        written.append(ORIGIN + dst)
    return donor, buf, written, wrapped


def check_pointers(donor, buf, written, wrapped, errs):
    for dst in written:
        if u32(buf, dst) >= len(buf) and dst not in wrapped:
            errs.append(f"{donor} pointer at {dst:#x} outside the payload")


def check_vertices(d_arc, d_pobj, pay, p_pobj, wrapped, errs):
    """Every indexed element the POBJ draws reads the same through both views."""
    d_attrs = parse_vtxdesc(d_arc.data, u32(d_arc.data, d_pobj + 0x08))
    p_desc = u32(pay.data, p_pobj + 0x08)
    p_attrs = parse_vtxdesc(pay.data, p_desc)
    dl = u32(pay.data, p_pobj + 0x10)
    end = dl + u16(pay.data, p_pobj + 0x0E) * 32
    vsize = sum(attr_vertex_bytes(a) for a in p_attrs)
    checked = 0
    cur = dl
    while cur < end:
        op = pay.data[cur]
        if op == 0:
            cur += 1
            continue
        if (op & 0xF8) not in (0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8):
            break
        count = u16(pay.data, cur + 1)
        cur += 3
        for _ in range(count):
            o = cur
            for k, (da, pa) in enumerate(zip(d_attrs, p_attrs)):
                if pa["atype"] in (GX_INDEX8, GX_INDEX16):
                    idx = pay.data[o] if pa["atype"] == GX_INDEX8 else u16(pay.data, o)
                    want = da["buf"] + idx * da["stride"]
                    base = pa["buf"]
                    if p_desc + k * 0x18 + 0x14 in wrapped:
                        base -= 1 << 32
                    got = base + idx * pa["stride"]
                    if not 0 <= got <= len(pay.data) - pa["stride"]:
                        errs.append(f"index {idx} of attr {pa['attr']} reads outside the payload")
                        return checked
                    if d_arc.data[want : want + da["stride"]] != pay.data[got : got + pa["stride"]]:
                        errs.append(f"index {idx} of attr {pa['attr']} reads different bytes")
                        return checked
                    checked += 1
                o += attr_vertex_bytes(pa)
            cur += vsize
    return checked


def graph(arc, root, typ, stop=0):
    w = Walker(arc)
    if stop:
        w.visited[stop] = ("stop", 0)
    got = w.walk(root, typ)
    got.pop(stop, None)
    return sorted((t, sz) for t, sz in got.values() if t != "vertex_blob")


def verify_mesh(data, m, errs):
    donor, buf, written, wrapped = rebuild(data, 0x08, errs)
    dobj_off, dobj_num = u32(data, 0x20), u32(data, 0x24)
    dobjs = [ORIGIN + u32(data, dobj_off + 4 * i) for i in range(dobj_num)]
    for a, b in zip(dobjs, dobjs[1:] + [None]):
        struct.pack_into(">I", buf, a + DOBJ_NEXT, b or 0)
        if b:
            written.append(a + DOBJ_NEXT)
    check_pointers(donor, buf, written, wrapped, errs)

    pay = PayloadView(buf, written)
    d_arc = Archive(os.path.join("iso/files", donor))
    by_dest = {ORIGIN + v: k for k, v in m["payload"].remap.items() if ORIGIN + v in set(dobjs)}

    checked = objects = 0
    for p_d in dobjs:
        d_d = by_dest[p_d]
        want = graph(d_arc, d_d, "DObjDesc", d_arc.ptr(d_d + DOBJ_NEXT))
        got = graph(pay, p_d, "DObjDesc", pay.ptr(p_d + DOBJ_NEXT))
        if want != got:
            errs.append(f"DObj {p_d:#x}: graph differs ({len(want)} vs {len(got)} objects)")
        objects += len(got)

        d_pobj, p_pobj = d_arc.ptr(d_d + 0x0C), pay.ptr(p_d + 0x0C)
        while d_pobj and p_pobj:
            checked += check_vertices(d_arc, d_pobj, pay, p_pobj, wrapped, errs)
            d_pobj, p_pobj = d_arc.ptr(d_pobj + 0x04), pay.ptr(p_pobj + 0x04)

    print(
        f"  mesh: {donor}, {(len(buf) - ORIGIN) / 1024:.1f} KB, {len(dobjs)} DObjs, {objects} objects, "
        f"{checked} vertex reads, {len(wrapped)} rebased pointers"
    )


def verify_path(data, m, errs):
    donor, buf, written, wrapped = rebuild(data, 0x54, errs)
    check_pointers(donor, buf, written, wrapped, errs)
    root = ORIGIN + u32(data, 0x6C)
    d_arc = Archive(os.path.join("iso/files", donor))
    d_root = next(k for k, v in m["path"].remap.items() if ORIGIN + v == root)
    want = graph(d_arc, d_root, "AnimJoint")
    got = graph(PayloadView(buf, written), root, "AnimJoint")
    if want != got:
        errs.append(f"flight graph differs ({len(want)} vs {len(got)} objects)")
    print(f"  flight: {donor}, {len(buf) - ORIGIN} B, {len(got)} objects")


def main():
    data = Archive(OUTPUT).data
    if u32(data, 0) != MANIFEST_MAGIC:
        print(f"bad magic {u32(data, 0):#x}")
        return 1

    m = plan()
    errs = []
    print(f"{OUTPUT}:")
    verify_mesh(data, m, errs)
    verify_path(data, m, errs)
    for e in errs:
        print(f"  {e}")
    print("OK" if not errs else "FAIL")
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())
