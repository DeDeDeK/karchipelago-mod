"""Backdrop subtree carver.

Walks the JOBJDesc tree at grModel<X>[<slot>] in a stage Model archive,
computes the exact byte length of every reachable HSD object (image
data sized from ImageDesc, palettes from TlutDesc, etc.), and emits a
minimal HSD archive containing only those bytes plus a synthesized
ModelSection-style pp slot exposed under <new_symbol>.

The carved file ships as a `mods/custom_weather/assets/Backdrop*.dat`
mod asset. Runtime code does:

    HSD_Archive *donor = Archive_LoadFile("BackdropSpace.dat");
    void **donor_ms   = Archive_GetPublicAddress(donor, "backdropSpace");
    ct_modelsection->backdrop = (JOBJDesc **)donor_ms[1];

which is the same shape as a vanilla stage's grModel<X>[1] slot.

Relocation table is rebuilt: only relocs with both source AND target
inside the kept ranges are translated; out-of-range relocs are dropped
and their source dword is zeroed (only happens to dangling pointers
inside slop bytes that snuck into a kept range during merging).
"""

import os
import struct
import sys

# Allow direct invocation (`uv run python scripts/hsd/carve_backdrop.py ...`)
# by making `scripts/` importable; harmless when imported as a package.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, build_archive, u32
from hsd.geom_bounds import measure_root, scale_geometry
from hsd.walker import Walker, carve_ranges


def carve(input_path, src_symbol, slot_index, output_path, new_symbol,
          target_radius=None):
    arc = Archive(input_path)
    if src_symbol not in arc.publics:
        raise SystemExit(f"public symbol {src_symbol!r} not found")
    ms_off = arc.publics[src_symbol]
    pp_off = u32(arc.data, ms_off + slot_index * 4)
    if pp_off == 0:
        raise SystemExit(f"slot [{slot_index}] is NULL")
    root_jobj = u32(arc.data, pp_off)
    if root_jobj == 0:
        raise SystemExit("backdrop JOBJDesc pointer is NULL")
    print(
        f"  source {src_symbol}[{slot_index}] pp={pp_off:#x} -> JOBJDesc={root_jobj:#x}"
    )

    # Normalize the backdrop's on-screen size. 3D_CreateStageModel stamps
    # the host stage's grStageScale onto the instantiated root joint,
    # overwriting the donor's own. City Trial's scale is 0.70, so a donor
    # whose geometry radius differs from City's renders its sky dome at a
    # different distance (too close -> obscures the map; too far -> tiny).
    # Pre-scaling the geometry to City's reference radius makes every
    # carved backdrop render at the same distance as vanilla City Trial.
    gdata = arc.data
    if target_radius is not None:
        rad = measure_root(arc, root_jobj)["radius"]
        if rad <= 0:
            print(f"  WARN: measured radius {rad:.1f}; skipping size normalization")
        else:
            f = target_radius / rad
            gdata = scale_geometry(arc, root_jobj, f)
            print(f"  geometry radius {rad:.1f} -> x{f:.4f} -> {target_radius:.1f} "
                  f"(City reference)")

    visited = Walker(arc).walk(root_jobj, "JOBJDesc")
    print(f"  reached {len(visited)} objects")

    # Layout: a ModelSection (only [+0x04] populated) + a pp slot mirroring
    # a vanilla stage's grModel<X>[1], then the kept ranges. carve_ranges
    # concatenates the reachable bytes after this prefix and rebuilds the
    # reloc table; base_relocs seeds the two synthetic prefix pointers.
    PP_OFFSET = 0x10
    PREFIX = 0x24  # ModelSection (0x10) + pp slot (0x14)
    res = carve_ranges(arc, visited, bytearray(PREFIX),
                       base_relocs=(0x04, PP_OFFSET), source=gdata)
    print(
        f"  kept {len(res.intervals)} ranges, {len(res.remap) / 1024:.1f} KB total "
        f"(source data was {len(arc.data) / 1024:.1f} KB)"
    )
    if res.dropped:
        print(f"  dropped {res.dropped} relocs to out-of-range targets (zeroed in slop)")

    new_data = res.data
    # ms[1] -> pp slot; pp slot[+0x00] -> JOBJDesc root (both relocated via
    # base_relocs). pp[+0x04..+0x13] stays zero (no animation).
    struct.pack_into(">I", new_data, 0x04, PP_OFFSET)
    struct.pack_into(">I", new_data, PP_OFFSET, res.remap[root_jobj])

    out = build_archive(new_data, res.relocs, [(new_symbol, 0)], arc.version)
    with open(output_path, "wb") as f:
        f.write(out)
    print(
        f"  wrote {output_path} ({len(out) / 1024:.1f} KB, "
        f"{len(out) * 100 / arc.file_size:.1f}% of source)"
    )


def main(argv):
    if len(argv) not in (6, 7):
        print(__doc__)
        print("usage: carve_backdrop.py <in.dat> <symbol> <slot> <out.dat> "
              "<new_symbol> [target_radius]")
        return 1
    in_path, sym, slot_s, out_path, new_sym = argv[1:6]
    target = float(argv[6]) if len(argv) == 7 else None
    print("Carving backdrop:")
    print(f"  in  = {in_path}")
    print(f"  out = {out_path}")
    carve(in_path, sym, int(slot_s), out_path, new_sym, target_radius=target)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
