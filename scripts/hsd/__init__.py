# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""HSD archive (.dat) parsing helpers for Kirby Air Ride.

Lightweight Python port of the bits of HSDLib we actually need:
- HSDRawFile-style header / reloc / public-symbol parsing (`Archive`),
  and the inverse serializer (`build_archive`).
- A declarative layout table for the HSD struct types (`schema`), shared
  by the walker, the tree printer and the carve verifier.
- A type-aware reachability walker with exact size computation
  (`Walker`), plus the `merge_intervals` / `carve_ranges` steps that turn
  a walked subtree into a minimal carved data section.
- AirRide public-symbol -> root-type classifier (`classify_symbol`).
- GX texture-format tables and texel encoders (`gx`).
- Flag/enum decoding and per-record one-liners (`format`), and the
  KAR stage-data decoder (`grdata`).

Sister scripts (all inside this package):
- `explore.py` - CLI front-end (ls / tree / grdata / find).
- `geom_bounds.py` - measure a model subtree's bounding radius / rescale it.
- `carve_backdrop.py` - single-backdrop carve CLI.
- `carve_all_backdrops.py` - bulk carve over iso/files/Gr*Model.dat.
- `carve_custom_item.py` - carve an Item.dat model into a customItem .dat.
- `make_checklist_textures.py` - author the AP checklist texture .dat.
- `make_menu_logo.py` - author the title-screen logo model .dat.
- `menu_logo_bounds.py` - measure the vanilla title logo joints it replaces.
- `verify_carved.py` - sanity-check a carved .dat for in-bounds relocs and pointers.
"""

from .archive import HSD_HEADER, Archive, build_archive, cstr, f32, u16, u32
from .format import describe
from .gx import FORMAT_BLOCK, FORMAT_NAME, image_size
from .schema import SCHEMA, resolved_fields
from .symbols import classify_symbol
from .walker import CarveResult, Walker, carve_ranges, merge_intervals

__all__ = [
    "Archive",
    "build_archive",
    "Walker",
    "carve_ranges",
    "CarveResult",
    "classify_symbol",
    "describe",
    "image_size",
    "merge_intervals",
    "resolved_fields",
    "FORMAT_BLOCK",
    "FORMAT_NAME",
    "HSD_HEADER",
    "SCHEMA",
    "u16",
    "u32",
    "f32",
    "cstr",
]
