#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""Verify a carved HSD .dat file.

  1. Bounds-checks every reloc source and target against the data
     section. Zeroed targets (intentionally dropped during carving) pass.
  2. Walks the typed tree from the carved root, flagging any pointer that
     lands outside the data section.

The root is auto-detected from the carve layout: a `customItem`
descriptor exposes its model at +0x14, and a backdrop ModelSection
exposes its JOBJ root through `ms[1] -> pp -> JOBJDesc`. Override with
--root / --root-type for anything else.

Unlike `Walker`, this tolerates bad pointers - it checks each slot before
descending, so a corrupt archive reports rather than crashes. Exits
nonzero if any bad reloc or pointer is found.

Usage:
    uv run python scripts/hsd/verify_carved.py <carved.dat> [<public>]
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, NotAnHSDArchive, u32
from hsd.schema import SCHEMA, array_length, resolved_fields, root_for
from hsd.symbols import classify_symbol

CUSTOM_ITEM_MAGIC = 0x4349544D  # 'CITM', the CustomItemDesc magic


def _roots(arc, sym):
    """([(offset, type)], description) for the carved public `sym`."""
    base = arc.publics[sym]
    if base + 0x18 <= len(arc.data) and u32(arc.data, base) == CUSTOM_ITEM_MAGIC:
        return [(u32(arc.data, base + 0x14), "JOBJDesc")], "customItem +0x14 -> model"
    typ, is_array = root_for(classify_symbol(sym))
    if is_array:
        n = array_length(arc, base)
        return (
            [(u32(arc.data, base + i * 4), typ) for i in range(n)],
            f"NullPtrArray<{typ}>[{n}]",
        )
    if typ == "JOBJDesc":
        # A carved backdrop exposes its root through a ModelSection-shaped
        # prefix: ms[1] -> pp -> JOBJDesc.
        pp = u32(arc.data, base + 0x04)
        if pp and (base + 0x04) in arc.reloc_set and pp in arc.reloc_set:
            return [(u32(arc.data, pp), typ)], f"ms[1] -> pp {pp:#x} -> JOBJDesc"
    return [(base, typ)], f"public offset as {typ}"


def _sibling_count(arc, off, fd):
    lo = off + fd.cnt_off
    if lo + fd.cnt_w > len(arc.data):
        return 0
    return int.from_bytes(arc.data[lo : lo + fd.cnt_w], "big")


def _entry_offsets(arc, off, fd):
    """[(kind, offset)] a non-`ptr` field addresses: 'slot' for pointer-array
    entries, 'record' for embedded fixed-size records."""
    if fd.kind == "buffer":
        return []
    target = u32(arc.data, off + fd.off)
    if fd.kind == "records":
        size = SCHEMA[fd.type].size
        return [
            ("record", target + i * size) for i in range(_sibling_count(arc, off, fd))
        ]
    if fd.kind == "array":
        n = array_length(arc, target)
    elif fd.kind == "run":
        n = 0
        while (target + n * 4) in arc.reloc_set:
            n += 1
    else:  # count
        n = _sibling_count(arc, off, fd)
    return [("slot", target + i * fd.stride) for i in range(n)]


def verify(path, sym=None, root=None, root_type="JOBJDesc"):
    try:
        arc = Archive(path)
    except NotAnHSDArchive as e:
        print(e, file=sys.stderr)
        return 1
    size = arc.data_size
    print(
        f"file={arc.file_size:#x} data={size:#x} relocs={arc.nb_reloc} "
        f"publics={arc.nb_public} externs={arc.nb_extern}"
    )
    for name, off in arc.publics.items():
        print(f"  public {name} @ {off:#x}")

    bad = 0
    for src in arc.relocs:
        if not (0 <= src < size - 3):
            print(f"  RELOC SRC OUT OF BOUNDS: {src:#x}")
            bad += 1
            continue
        tgt = u32(arc.data, src)
        if tgt and not (0 <= tgt < size):
            print(f"  RELOC TGT OUT OF BOUNDS: src={src:#x} tgt={tgt:#x}")
            bad += 1
    print(f"  {bad} bad relocs out of {arc.nb_reloc}")

    if root is None:
        if sym is None:
            sym = next(iter(arc.publics))
        elif sym not in arc.publics:
            print(f"public {sym!r} not found", file=sys.stderr)
            return 1
        work, how = _roots(arc, sym)
        print(f"\nWalking {sym} @ {arc.publics[sym]:#x} ({how})")
    else:
        work = [(root, root_type)]
        print(f"\nWalking {root_type} @ {root:#x}")
    if any(not (0 <= r < size) for r, _ in work):
        print("  ERROR: root offset outside the data section")
        return 1

    visited = {}
    bad_targets = 0

    def check(where, tgt):
        nonlocal bad_targets
        if 0 <= tgt < size:
            return True
        print(f"  BAD POINTER: {where} -> {tgt:#x}")
        bad_targets += 1
        return False

    while work:
        off, typ = work.pop(0)
        if off in visited or not (0 <= off < size):
            continue
        visited[off] = typ
        for fd in resolved_fields(arc, typ, off):
            slot = off + fd.off
            if slot not in arc.reloc_set:
                continue
            target = u32(arc.data, slot)
            if not check(f"{typ}@{off:#x}+{fd.off:#x}", target):
                continue
            if fd.kind == "ptr":
                work.append((target, fd.type))
                continue
            for kind, entry in _entry_offsets(arc, off, fd):
                if not check(f"{typ}@{off:#x}+{fd.off:#x}[]", entry):
                    continue
                if kind == "record":
                    work.append((entry, fd.type))
                    continue
                if entry not in arc.reloc_set:
                    continue
                elem = u32(arc.data, entry)
                if elem and check(f"{fd.type}[] @ {entry:#x}", elem):
                    work.append((elem, fd.type))

    print(f"  reached {len(visited)} objects, {bad_targets} bad pointers")
    by_type = {}
    for typ in visited.values():
        by_type[typ] = by_type.get(typ, 0) + 1
    for typ, n in sorted(by_type.items()):
        print(f"    {typ:18s} count={n}")
    return 0 if (bad == 0 and bad_targets == 0) else 1


def main(argv):
    p = argparse.ArgumentParser(description="Verify a carved HSD .dat file.")
    p.add_argument("path")
    p.add_argument("symbol", nargs="?", default=None)
    p.add_argument(
        "--root",
        type=lambda s: int(s, 0),
        default=None,
        help="walk from this data offset instead of the carved root",
    )
    p.add_argument("--root-type", default="JOBJDesc", choices=sorted(SCHEMA))
    args = p.parse_args(argv[1:])
    return verify(args.path, args.symbol, args.root, args.root_type)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
