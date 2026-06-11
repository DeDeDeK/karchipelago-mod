#!/usr/bin/env python3
"""Survey every `iso/files/Gr*Model.dat` and list which expose a
backdrop subtree (`grModel<X>[1] != NULL`).

Cheaper than `carve_all_backdrops.py` - reads only the ModelSection
pointer slots, doesn't walk the tree or write any output.

Usage: uv run python scripts/hsd/probe_backdrops.py
"""

import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, NotAnHSDArchive, u32


def survey(path):
    try:
        arc = Archive(path)
    except NotAnHSDArchive:
        return None

    sym = None
    for n in arc.publics:
        if n.startswith("grModel") and "Motion" not in n:
            sym = n
            break
    if not sym:
        return None

    ms_off = arc.publics[sym]
    if ms_off + 16 > len(arc.data):
        return (sym, None, None, arc.file_size)
    ms = [u32(arc.data, ms_off + i * 4) for i in range(4)]
    pp1 = ms[1]
    jobj1 = u32(arc.data, pp1) if 0 < pp1 < len(arc.data) - 4 else 0
    return (sym, ms, jobj1, arc.file_size)


def main():
    files = sorted(glob.glob("iso/files/Gr*Model.dat"))
    print(f"{'File':32s} {'Symbol':24s} {'ms[1]':>10s} {'JOBJ':>10s} {'size_kb':>10s}")
    for p in files:
        info = survey(p)
        name = os.path.basename(p)
        if info is None:
            print(f"{name:32s} {'(no grModel pub)':24s}")
            continue
        sym, ms, jobj1, sz = info
        if ms is None:
            print(f"{name:32s} {sym:24s} (bad)")
            continue
        ms1 = ms[1]
        marker = "yes" if jobj1 else "NO"
        print(
            f"{name:32s} {sym:24s} {ms1:#10x} {jobj1:#10x} {sz / 1024:>10.1f}  {marker}"
        )


if __name__ == "__main__":
    main()
