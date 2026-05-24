#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""Verify a carved HSD .dat file.

Parses the header via `Archive`, then:
  1. Bounds-checks every reloc source and target against the data
     section. Zeroed targets (intentionally dropped during carving)
     pass.
  2. BFS-walks the JObj tree starting at `<symbol>[1]`'s pp slot,
     flagging any pointer that lands outside the data section.

Used as a sanity check after carving a new backdrop / subtree.
A clean carved file shows zero bad relocs and zero bad pointers.

Usage:
    uv run python scripts/hsd/verify_carved.py <carved.dat> [<public>]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, u32


# Mirrors Walker.visit_* dispatch from hsd/walker.py. Kept inline so
# this verifier can tolerate bad pointers (Walker assumes well-formed).
# Note: this is intentionally a static table -- no flag-based routing for
# JObj+0x10 / RObj+0x08, since wrong routing would just classify a tail
# pointer with the wrong type, not skip checking it for bounds.
TYPE_CHILDREN = {
    'JOBJDesc':  [(0x08, 'JOBJDesc'), (0x0C, 'JOBJDesc'), (0x10, 'DObjDesc'),
                  (0x38, 'Mtx'),      (0x3C, 'RObjDesc')],
    'DObjDesc':  [(0x04, 'DObjDesc'), (0x08, 'MObjDesc'), (0x0C, 'POBJDesc')],
    'MObjDesc':  [(0x08, 'TObjDesc'), (0x0C, 'MaterialDesc'),
                  (0x14, 'PEDesc')],
    'TObjDesc':  [(0x04, 'TObjDesc'), (0x4C, 'ImageDesc'),  (0x50, 'TlutDesc'),
                  (0x54, 'TexLODDesc'), (0x58, 'TObjTev')],
    'ImageDesc': [(0x00, 'image_blob')],
    'IOBJDesc':  [(0x08, 'image_blob')],
    'TlutDesc':  [(0x00, 'palette_blob')],
    # POBJ +0x14: union of envelope/shapeset/joint. The verifier just
    # bounds-checks the pointer, so a generic 'envelope_or_joint' tag is
    # fine here (the walker does the real flag-based routing).
    'POBJDesc':  [(0x04, 'POBJDesc'), (0x08, 'VtxDescList'),
                  (0x10, 'dl_blob'),  (0x14, 'envelope_or_joint')],
    'RObjDesc':  [(0x00, 'RObjDesc'), (0x08, 'JOBJDesc')],
    'LObjDesc':  [(0x04, 'LObjDesc'), (0x10, 'WObjDesc'), (0x14, 'WObjDesc')],
    'WObjDesc':  [(0x10, 'RObjDesc')],
    'FogDesc':   [(0x04, 'FogAdjDesc')],
    'FogAnim':   [(0x00, 'FogDesc'),  (0x04, 'AOBJ')],
    # Shallow animation pointers (HSDLib HSD_AOBJ.cs / HSD_FOBJDesc.cs).
    # We just bounds-check the chain; the actual keyframe buffer is opaque.
    'AOBJ':      [(0x08, 'FOBJDesc'), (0x0C, 'JOBJDesc')],
    'FOBJDesc':  [(0x00, 'FOBJDesc'), (0x10, 'anim_buffer')],
    'FOBJ':      [(0x04, 'anim_buffer')],
    'LightGroup':[(0x00, 'LightNode'), (0x04, 'LightNode'), (0x08, 'LightNode')],
    'LightNode': [(0x00, 'Light'), (0x04, 'Light'), (0x08, 'Light'), (0x0C, 'Light')],
    'Light':     [(0x00, 'LObjDesc')],
    # SOBJ's three NullPointerArray slots are walked by the verifier as a
    # plain ptr check; the walker enumerates the array contents.
    'SOBJ':      [(0x00, 'NullPtrArray'), (0x04, 'NullPtrArray'),
                  (0x08, 'NullPtrArray'), (0x0C, 'FogAnim')],
    'Camera':    [(0x18, 'WObjDesc'), (0x1C, 'WObjDesc')],
    'ModelGroup':[(0x00, 'JOBJDesc')],
    'ShapeSet':  [(0x08, 'shapeset_blob'), (0x0C, 'shapeset_blob'),
                  (0x14, 'shapeset_blob'), (0x18, 'shapeset_blob')],
}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    sym = sys.argv[2] if len(sys.argv) > 2 else None
    arc = Archive(path)
    data_size = arc.data_size
    print(f"file={arc.file_size:#x} data={data_size:#x} "
          f"relocs={arc.nb_reloc} publics={arc.nb_public} externs={arc.nb_extern}")
    for name, off in arc.publics.items():
        print(f"  public {name} @ {off:#x}")

    bad = 0
    for src in arc.relocs:
        if not (0 <= src < data_size - 3):
            print(f"  RELOC SRC OUT OF BOUNDS: {src:#x}")
            bad += 1
            continue
        tgt = u32(arc.data, src)
        if tgt == 0:
            continue  # zeroed (intentionally dropped during carving)
        if not (0 <= tgt < data_size):
            print(f"  RELOC TGT OUT OF BOUNDS: src={src:#x} tgt={tgt:#x}")
            bad += 1
    print(f"  {bad} bad relocs out of {arc.nb_reloc}")

    if sym is None:
        sym = next(iter(arc.publics))
    ms_off = arc.publics[sym]
    print(f"\nWalking {sym} @ {ms_off:#x}:")
    for i in range(4):
        v = u32(arc.data, ms_off + i*4)
        is_rel = (ms_off + i*4) in arc.reloc_set
        print(f"  ms[{i}]: {v:#x} {'(reloc)' if is_rel else ''}")
    pp_off = u32(arc.data, ms_off + 4)
    if pp_off == 0:
        print("  ms[1] is NULL!")
        return 0
    print(f"  ms[1] -> {pp_off:#x}")
    if (ms_off + 4) not in arc.reloc_set:
        print("  WARN: ms[1] not in reloc table")
    jobj_off = u32(arc.data, pp_off)
    if pp_off not in arc.reloc_set:
        print("  WARN: pp slot not in reloc table")
    print(f"  pp -> JOBJDesc @ {jobj_off:#x}")
    if not (0 <= jobj_off < data_size):
        print("  ERROR: JOBJDesc offset out of data section")
        return 1

    visited = {}
    work = [(jobj_off, 'JOBJDesc')]
    bad_targets = 0
    while work:
        off, t = work.pop(0)
        if off == 0 or off in visited:
            continue
        visited[off] = t
        for foff, ftyp in TYPE_CHILDREN.get(t, []):
            slot = off + foff
            if slot in arc.reloc_set:
                tgt = u32(arc.data, slot)
                if tgt == 0:
                    continue
                if not (0 <= tgt < data_size):
                    print(f"  BAD POINTER: {t}@{off:#x}+{foff:#x} -> {tgt:#x}")
                    bad_targets += 1
                    continue
                work.append((tgt, ftyp))
    print(f"  reached {len(visited)} objects, {bad_targets} bad pointers")
    by_type = {}
    for off, t in visited.items():
        by_type.setdefault(t, 0)
        by_type[t] += 1
    for t, n in sorted(by_type.items()):
        print(f"    {t:18s} count={n}")
    return 0 if (bad == 0 and bad_targets == 0) else 1


if __name__ == '__main__':
    sys.exit(main())
