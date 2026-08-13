#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Batch-carve every backdrop subtree in iso/files/Gr*Model.dat
into mods/custom_weather/assets/Backdrop<X>.dat.

Naming convention:
    grModel<X>      -> Backdrop<X>.dat   public "backdrop<X>"

Archives whose skybox slot (`grModel<X>[1]`) is NULL have no backdrop and
are skipped. `--dry-run` reports that inventory without carving anything.

Usage:
    uv run python scripts/hsd/carve_all_backdrops.py [--dry-run]
"""

import argparse
import glob
import os
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, NotAnHSDArchive, u32
from hsd.carve_backdrop import carve
from hsd.geom_bounds import backdrop_root, measure_root

INPUT_GLOB = "iso/files/Gr*Model.dat"
OUTPUT_DIR = "mods/custom_weather/assets"

# Every backdrop is normalized to City Trial's own backdrop radius so the
# grafted sky dome renders at the same distance as vanilla CT (the host
# scale 3D_CreateStageModel stamps is City's, regardless of the donor).
CITY_MODEL = "iso/files/GrCity1Model.dat"
CITY_SYMBOL = "grModelCity1"


def grmodel_symbol(arc):
    """The archive's `grModel<X>` public, or None. `grModelMotion*` is a
    separate animation public and never the model root."""
    for name in arc.publics:
        if name.startswith("grModel") and "Motion" not in name:
            return name
    return None


def skybox_slot(arc, sym):
    """The `grModel<X>[1]` (SkyboxModel) pointer, or 0 when absent."""
    ms = arc.publics[sym]
    return u32(arc.data, ms + 4) if ms + 8 <= len(arc.data) else 0


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--dry-run", action="store_true",
                   help="report which archives have a backdrop, carve nothing")
    args = p.parse_args(argv[1:])

    if not args.dry_run:
        os.makedirs(OUTPUT_DIR, exist_ok=True)
        city = Archive(CITY_MODEL)
        target_radius = measure_root(city, backdrop_root(city, CITY_SYMBOL))["radius"]
        print(f"City reference backdrop radius = {target_radius:.1f}\n")

    summary = []
    for path in sorted(glob.glob(INPUT_GLOB)):
        name = os.path.basename(path)
        try:
            arc = Archive(path)
        except (NotAnHSDArchive, OSError) as e:
            summary.append((name, f"open failed: {e}"))
            continue

        sym = grmodel_symbol(arc)
        if sym is None:
            summary.append((name, "no grModel<X> public"))
            continue
        if skybox_slot(arc, sym) == 0:
            summary.append((name, f"{sym}: ms[1] NULL - no backdrop"))
            continue
        if args.dry_run:
            size_kb = arc.file_size / 1024
            summary.append((name, f"{sym}: backdrop present ({size_kb:.1f} KB source)"))
            continue

        suffix = sym[len("grModel"):]
        out_path = os.path.join(OUTPUT_DIR, f"Backdrop{suffix}.dat")
        print(f"\n=== {name} ({sym}) ===")
        try:
            carve(path, sym, 1, out_path, f"backdrop{suffix}",
                  target_radius=target_radius)
            sz = os.path.getsize(out_path)
            summary.append((name, f"OK -> Backdrop{suffix}.dat ({sz / 1024:.1f} KB)"))
        except SystemExit as e:
            summary.append((name, f"carve aborted: {e}"))
        except Exception as e:
            traceback.print_exc()
            summary.append((name, f"carve failed: {e}"))

    print("\n=== Summary ===")
    for name, status in summary:
        print(f"  {name:32s} {status}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
