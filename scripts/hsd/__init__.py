# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""HSD archive (.dat) parsing helpers for Kirby Air Ride.

Lightweight Python port of the bits of HSDLib we actually need:
- HSDRawFile-style header / reloc / public-symbol parsing (`Archive`).
- A type-aware reachability walker over JOBJDesc / DObjDesc / MObjDesc /
  TObjDesc / POBJDesc trees with exact size computation (`Walker`).
- AirRide public-symbol -> root-type classifier (`classify_symbol`).
- A backdrop-specific subtree carver (`carve_backdrop`) used by the
  custom_weather mod's BackdropX.dat assets.

Sister scripts (all inside this package):
- `explore.py` — CLI front-end (ls / tree / find).
- `carve_backdrop.py` — single-backdrop carve CLI.
- `carve_all_backdrops.py` — bulk carve over iso/files/Gr*Model.dat.
- `probe_backdrops.py` — read-only survey of backdrop presence per stage.
- `verify_carved.py` — sanity-check a carved .dat for in-bounds relocs and pointers.
- `dump_lights.py` — dump stage LObjDesc chains (City Trial layout by default).
"""

from .archive import Archive, u16, u32, cstr, HSD_HEADER
from .walker import Walker, image_size, merge_intervals, GX_FORMATS
from .symbols import classify_symbol

__all__ = [
    "Archive",
    "Walker",
    "classify_symbol",
    "image_size",
    "merge_intervals",
    "GX_FORMATS",
    "HSD_HEADER",
    "u16",
    "u32",
    "cstr",
]
