#!/usr/bin/env python3
"""Batch-carve every backdrop subtree in iso/files/Gr*Model.dat
into mods/custom_weather/assets/Backdrop<X>.dat.

Naming convention:
    grModel<X>      -> Backdrop<X>.dat   public "backdrop<X>"

Skips archives where ms[1] is NULL (no backdrop subtree).

Usage: uv run python scripts/hsd/carve_all_backdrops.py
"""
import glob
import os
import struct
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, NotAnHSDArchive
from hsd.carve_backdrop import carve

INPUT_GLOB = 'iso/files/Gr*Model.dat'
OUTPUT_DIR = 'mods/custom_weather/assets'


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    archives = sorted(glob.glob(INPUT_GLOB))
    summary = []
    for path in archives:
        name = os.path.basename(path)
        try:
            arc = Archive(path)
        except (NotAnHSDArchive, Exception) as e:
            summary.append((name, None, f"open failed: {e}"))
            continue

        sym = None
        for n in arc.publics:
            if n.startswith('grModel') and 'Motion' not in n:
                sym = n
                break
        if sym is None:
            summary.append((name, None, "no grModel<X> public"))
            continue

        suffix = sym[len('grModel'):]
        out_path = os.path.join(OUTPUT_DIR, f"Backdrop{suffix}.dat")
        new_sym = f"backdrop{suffix}"

        ms_off = arc.publics[sym]
        ms1 = struct.unpack(">I", arc.data[ms_off+4:ms_off+8])[0]
        if ms1 == 0:
            summary.append((name, suffix, "ms[1] NULL — no backdrop"))
            continue

        print(f"\n=== {name} ({sym}) ===")
        try:
            carve(path, sym, 1, out_path, new_sym)
            sz = os.path.getsize(out_path)
            summary.append((name, suffix, f"OK -> Backdrop{suffix}.dat ({sz/1024:.1f} KB)"))
        except SystemExit as e:
            summary.append((name, suffix, f"carve aborted: {e}"))
        except Exception as e:
            traceback.print_exc()
            summary.append((name, suffix, f"carve failed: {e}"))

    print("\n=== Summary ===")
    for name, suffix, status in summary:
        print(f"  {name:32s} {status}")


if __name__ == '__main__':
    main()
