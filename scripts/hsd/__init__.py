# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""HSD archive (.dat) parsing helpers for Kirby Air Ride.

Lightweight Python port of the bits of HSDLib we actually need:
- HSDRawFile-style header / reloc / public-symbol parsing (`Archive`),
  and the inverse serializer (`build_archive`).
- A type-aware reachability walker over JOBJDesc / DObjDesc / MObjDesc /
  TObjDesc / POBJDesc trees with exact size computation (`Walker`), plus
  the `merge_intervals` / `carve_ranges` steps that turn a walked subtree
  into a minimal carved data section.
- AirRide public-symbol -> root-type classifier (`classify_symbol`).
- GX texture-format tables and texel encoders (`gx`).

Library modules: `archive`, `walker`, `symbols`, `gx`.

Sister scripts (all inside this package):
- `explore.py` - CLI front-end (ls / tree / grdata / find).
- `geom_bounds.py` - measure a backdrop subtree's bounding radius / rescale it.
- `carve_backdrop.py` - single-backdrop carve CLI.
- `carve_all_backdrops.py` - bulk carve over iso/files/Gr*Model.dat.
- `carve_custom_item.py` - carve an Item.dat model into a customItem .dat.
- `make_checklist_textures.py` - author the AP checklist texture .dat.
- `probe_backdrops.py` - read-only survey of backdrop presence per stage.
- `verify_carved.py` - sanity-check a carved .dat for in-bounds relocs and pointers.
- `dump_lights.py` - dump stage LObjDesc chains (City Trial layout by default).
"""

from .archive import HSD_HEADER, Archive, build_archive, cstr, f32, u16, u32
from .gx import FORMAT_BLOCK, FORMAT_NAME, image_size
from .symbols import classify_symbol
from .walker import CarveResult, Walker, carve_ranges, merge_intervals

__all__ = [
    "Archive",
    "build_archive",
    "Walker",
    "carve_ranges",
    "CarveResult",
    "classify_symbol",
    "image_size",
    "merge_intervals",
    "FORMAT_BLOCK",
    "FORMAT_NAME",
    "HSD_HEADER",
    "u16",
    "u32",
    "f32",
    "cstr",
]
