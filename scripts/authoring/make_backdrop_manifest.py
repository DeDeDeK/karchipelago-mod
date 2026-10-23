#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Author BackdropManifest.dat, the recipe custom_weather rebuilds backdrops from.

A City Trial backdrop swap needs another stage's `grModel<X>[1]` subtree in RAM.
Shipping that subtree means shipping retail geometry and textures, and loading the
whole donor `Gr*Model.dat` at runtime is not an option either - the HSD heap is
10.2 MB total and City Trial already holds `GrCity1Model.dat`, so a second 0.5-2.8 MB
stage archive does not fit (GrSimpleModel alone is 14.5 MB).

So nothing is shipped but the recipe. This walks each donor's backdrop subtree, works
out which byte ranges of the file the subtree actually occupies, and writes those
ranges - not their contents - into one small archive. At runtime the mod reads just
those ranges off the retail disc into one allocation and relocates them.

Per backdrop the manifest holds:

  * the donor filename, and the offset/length of each file range the subtree occupies
    (one contiguous range per donor in practice). Ranges are expanded to 32-byte
    boundaries at both ends, because `File_Read` needs a
    32-byte-aligned offset, length and destination - so the runtime copies them
    verbatim with no alignment arithmetic of its own. GX needs the same alignment
    for textures, display lists and vertex arrays, which the expansion also gives.
  * one (dest_off, dest_val) pair per pointer in those ranges. The bytes land raw
    off the disc, so every pointer still holds a donor-relative offset; the runtime
    cannot translate one without the whole byte map, so the translated value is
    precomputed here and the runtime just adds the buffer base.
  * a `scale` factor. The loader stamps City Trial's StageScale over the root joint,
    so donors modelled at 1300-10000 units would otherwise render at wildly different
    distances. Shipping the factor and letting the mod apply it to the root joint
    keeps the geometry byte-identical to the disc.

The payload the runtime builds leads with a 0x20-byte pp slot mirroring a vanilla
stage's `grModel<X>[1]`: word 0 is the backdrop JOBJDesc root, the rest is zero (no
model motion). `ModelSection.backdrop` points at it.

Exports one public:

  backdropManifest  - BackdropManifest {magic, version, entry_num, entries}, each
                      entry {key, donor, payload_size, scale, root_off, ranges,
                      range_num, relocs, reloc_num}

Donors whose rebuilt payload is byte-identical to an earlier donor's are left out,
as are City1 (it would duplicate City Trial's own backdrop) and Simple (a 4 KB
placeholder), so every entry is a distinct look.

Run from the repo root:
    uv run python scripts/authoring/make_backdrop_manifest.py
"""

import argparse
import glob
import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, NotAnHSDArchive, build_archive, u32
from hsd.geom_bounds import backdrop_root, measure_root
from hsd.payload import plan_payload
from hsd.walker import Walker

INPUT_GLOB = "iso/files/Gr*Model.dat"
OUTPUT = "mods/custom_weather/assets/BackdropManifest.dat"
CITY_MODEL = "iso/files/GrCity1Model.dat"
CITY_SYMBOL = "grModelCity1"

PP_SLOT = 0x20  # payload prefix: the grModel<X>[1]-shaped pp slot

MANIFEST_MAGIC = 0x42444D46  # 'BDMF'
MANIFEST_VERSION = 1

SKIP_KEYS = {"City1", "Simple"}

ENTRY_SIZE = 0x24
HEADER_SIZE = 0x10


def plan(arc, root):
    """Lay the backdrop subtree out into a runtime payload.

    Returns (size, ranges, relocs, root_off): `ranges` as (donor_file_off, dest_off,
    length) with every field a multiple of 32, `relocs` as (dest_off, dest_val)
    pairs, and the root JOBJDesc's payload offset.
    """
    visited = Walker(arc).walk(root, "JOBJDesc")
    spans = [(off, off + sz) for off, (_, sz) in visited.items()]
    p = plan_payload(arc, spans, PP_SLOT)
    return p.size, p.ranges, p.relocs, p.remap[root]


def payload_digest(path, size, ranges, relocs, root_off):
    """Hash of the payload the runtime rebuilds, with pointers in payload offsets, so
    two donors carrying the same backdrop at different file offsets compare equal."""
    with open(path, "rb") as f:
        raw = f.read()
    payload = bytearray(size)
    for donor_off, dest_off, length in ranges:
        payload[dest_off : dest_off + length] = raw[donor_off : donor_off + length]
    for dest_off, dest_val in relocs:
        struct.pack_into(">I", payload, dest_off, dest_val)
    struct.pack_into(">I", payload, 0, root_off)
    return hashlib.sha1(payload).digest()


def build(donors, out_path):
    """Serialize the planned donors into the manifest archive."""
    # Layout: header, entry array, then each entry's variable-length blocks.
    data = bytearray(HEADER_SIZE + ENTRY_SIZE * len(donors))
    relocs = []

    def blob(payload, align=4):
        pad = (-len(data)) % align
        data.extend(b"\0" * pad)
        off = len(data)
        data.extend(payload)
        return off

    def cstr(s):
        return blob(s.encode("ascii") + b"\0", 1)

    struct.pack_into(">III", data, 0, MANIFEST_MAGIC, MANIFEST_VERSION, len(donors))
    struct.pack_into(">I", data, 0x0C, HEADER_SIZE)
    relocs.append(0x0C)

    for i, d in enumerate(donors):
        rng = b"".join(struct.pack(">III", *r) for r in d["ranges"])
        rel = b"".join(struct.pack(">II", *r) for r in d["relocs"])
        key_off = cstr(d["key"])
        donor_off = cstr(d["donor"])
        rng_off = blob(rng)
        rel_off = blob(rel)

        e = HEADER_SIZE + i * ENTRY_SIZE
        struct.pack_into(">II", data, e + 0x00, key_off, donor_off)
        struct.pack_into(">If", data, e + 0x08, d["size"], d["scale"])
        struct.pack_into(">I", data, e + 0x10, d["root"])
        struct.pack_into(">II", data, e + 0x14, rng_off, len(d["ranges"]))
        struct.pack_into(">II", data, e + 0x1C, rel_off, len(d["relocs"]))
        # rng_off/rel_off sit at +0x14/+0x1C, key/donor at +0x00/+0x04.
        relocs.extend((e + 0x00, e + 0x04, e + 0x14, e + 0x1C))

    out = build_archive(data, relocs, [("backdropManifest", 0)])
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(out)
    return len(out)


def grmodel_symbol(arc):
    """The archive's `grModel<X>` public, or None. `grModelMotion*` is a separate
    animation public and never the model root."""
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
    p.add_argument("--out", default=OUTPUT)
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="report the plan without writing the manifest",
    )
    args = p.parse_args(argv[1:])

    city = Archive(CITY_MODEL)
    target_radius = measure_root(city, backdrop_root(city, CITY_SYMBOL))["radius"]
    print(f"City reference backdrop radius = {target_radius:.1f}\n")

    donors = []
    seen = {}
    for path in sorted(glob.glob(INPUT_GLOB)):
        name = os.path.basename(path)
        try:
            arc = Archive(path)
        except (NotAnHSDArchive, OSError) as e:
            print(f"  {name:24s} open failed: {e}")
            continue

        sym = grmodel_symbol(arc)
        if sym is None or skybox_slot(arc, sym) == 0:
            continue

        key = sym[len("grModel") :]
        if key in SKIP_KEYS:
            continue
        root = u32(arc.data, skybox_slot(arc, sym))
        size, ranges, relocs, root_off = plan(arc, root)

        twin = seen.setdefault(payload_digest(path, size, ranges, relocs, root_off), key)
        if twin != key:
            print(f"  {key:12s} same backdrop as {twin}; skipped")
            continue

        radius = measure_root(arc, root)["radius"]
        if radius <= 0:
            print(f"  {name:24s} measured radius {radius:.1f}; skipped")
            continue

        donors.append(
            {
                "key": key,
                "donor": name,
                "size": size,
                "root": root_off,
                "scale": target_radius / radius,
                "ranges": ranges,
                "relocs": relocs,
            }
        )
        print(
            f"  {key:12s} {size / 1024:7.1f} KB payload  "
            f"{len(ranges):3d} ranges  {len(relocs):4d} relocs  "
            f"radius {radius:7.1f} -> x{target_radius / radius:.4f}"
        )

    if args.dry_run:
        print(f"\n{len(donors)} donors planned (dry run, nothing written)")
        return 0

    total = build(donors, args.out)
    payload = sum(d["size"] for d in donors)
    print(f"\nwrote {args.out} ({total / 1024:.1f} KB) for {len(donors)} backdrops")
    print(
        f"  runtime payload if every backdrop were loaded: {payload / 1048576:.2f} MB"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
