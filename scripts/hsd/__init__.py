# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""HSD archive (.dat) parsing helpers for Kirby Air Ride.

Lightweight Python port of the bits of HSDLib we actually need:
- HSDRawFile-style header / reloc / public-symbol parsing (`Archive`),
  and the inverse serializer (`build_archive` / `Blob`).
- A declarative layout table for the HSD struct types (`schema`), shared
  by the walker, the tree printer and the carve verifier.
- A type-aware reachability walker with exact size computation
  (`Walker`), plus the `merge_intervals` / `carve_ranges` steps that turn
  a walked subtree into a minimal carved data section.
- In-place graph editing over a parsed archive (`builder`), and the
  textured-quad leaf a HUD or menu image is drawn as (`quad_model`).
- AirRide public-symbol -> root-type classifier (`classify_symbol`).
- GX texture-format tables and texel encoders (`gx`), FObj keyframe
  streams (`fobj`), and the character-indexed UI banks built on both
  (`ui_banks`, `ui_art`).
- Flag/enum decoding and per-record one-liners (`format`), and the
  KAR stage-data decoder (`grdata`).

Tools in this package work on any archive; the scripts that author one
mod's shipped assets live in `scripts/authoring/`.

- `explore.py` - CLI front-end (ls / tree / grdata / find).
- `geom_bounds.py` - measure a model subtree's bounding radius / rescale it.
- `carve_custom_item.py` - carve an Item.dat model into a customItem .dat.
- `clone_machine.py` - copy a Vc*.dat machine under a new name and public.
- `machine_preview.py` - re-export a machine archive under the public names
  HSDraw types, so the model can be opened in a viewer.
- `make_machine_art.py` - build a machine's .art UI side-car from two renders.
- `verify_carved.py` - sanity-check a carved .dat for in-bounds relocs and pointers.
"""

from .archive import (
    ARCHIVE_VERSION,
    HSD_HEADER,
    Archive,
    Blob,
    build_archive,
    cstr,
    f32,
    u16,
    u32,
)
from .builder import Builder
from .format import describe
from .gx import FORMAT_BLOCK, FORMAT_NAME, image_size
from .schema import SCHEMA, resolved_fields
from .symbols import classify_symbol
from .walker import CarveResult, Walker, carve_ranges, merge_intervals

__all__ = [
    "ARCHIVE_VERSION",
    "FORMAT_BLOCK",
    "FORMAT_NAME",
    "HSD_HEADER",
    "SCHEMA",
    "Archive",
    "Blob",
    "Builder",
    "CarveResult",
    "Walker",
    "build_archive",
    "carve_ranges",
    "classify_symbol",
    "cstr",
    "describe",
    "f32",
    "image_size",
    "merge_intervals",
    "resolved_fields",
    "u16",
    "u32",
]
