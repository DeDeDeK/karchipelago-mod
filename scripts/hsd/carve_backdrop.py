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

from hsd.archive import Archive, HSD_HEADER, u32
from hsd.geom_bounds import measure_root, scale_geometry
from hsd.walker import Walker, merge_intervals


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

    walker = Walker(arc)
    visited = walker.walk(root_jobj, "JOBJDesc")
    print(f"  reached {len(visited)} objects")

    intervals = [(off, off + sz) for off, (_, sz) in visited.items()]
    intervals = merge_intervals(intervals)
    kept_bytes = sum(e - s for s, e in intervals)
    print(
        f"  kept {len(intervals)} ranges, {kept_bytes / 1024:.1f} KB total "
        f"(source data was {len(arc.data) / 1024:.1f} KB)"
    )

    # Build remap: original offset -> new offset within concatenated data.
    # Layout:
    #   new_data[+0x00 .. +0x0F] : ModelSection (only [+0x04] populated)
    #   new_data[+0x10 .. +0x23] : pp slot (JOBJDesc*, motion=NULL, ...)
    #   new_data[+0x24 ..      ] : kept ranges concatenated (4-byte aligned)
    PP_OFFSET = 0x10
    PP_SIZE = 0x14
    PREFIX = PP_OFFSET + PP_SIZE  # 0x24

    remap = {}  # old_offset -> new_offset
    cursor = PREFIX
    new_data = bytearray(PREFIX)
    for s, e in intervals:
        # GX requires 32-byte (cache-line) alignment for textures,
        # display lists, and vertex arrays. The source archive lays
        # those at 32-byte-aligned offsets inside its data section.
        # Preserve their relative alignment by making cursor mod 32
        # match s mod 32 - so every byte inside the interval keeps
        # its source-relative alignment after the shift.
        target_mod = s & 31
        cur_mod = cursor & 31
        pad = (target_mod - cur_mod) & 31
        if pad:
            new_data.extend(b"\0" * pad)
            cursor += pad
        for o in range(s, e):
            remap[o] = cursor + (o - s)
        new_data.extend(gdata[s:e])
        cursor += e - s

    # ModelSection: ms[1] = pp slot offset (relocated)
    struct.pack_into(">I", new_data, 0x04, PP_OFFSET)
    # pp slot[+0x00] = JOBJDesc * (relocated, in carved coordinates)
    struct.pack_into(">I", new_data, PP_OFFSET, remap[root_jobj])
    # pp slot[+0x04..+0x13] left as zeros (no animation)

    new_relocs = [0x04, PP_OFFSET]
    dropped = 0
    for src in arc.relocs:
        if src not in remap:
            continue
        tgt = u32(arc.data, src)
        new_src = remap[src]
        if tgt in remap:
            new_tgt = remap[tgt]
            struct.pack_into(">I", new_data, new_src, new_tgt)
            new_relocs.append(new_src)
        else:
            # Pointer target outside any kept range. This should only
            # happen for relocs in slop bytes (unreachable data that
            # squeaked into a kept range during merging). Zero the
            # source dword so the loader doesn't relocate junk.
            struct.pack_into(">I", new_data, new_src, 0)
            dropped += 1
    new_relocs.sort()
    if dropped:
        print(f"  dropped {dropped} relocs to out-of-range targets (zeroed in slop)")

    name_bytes = new_symbol.encode("ascii") + b"\0"
    public_entries = [(0, 0)]

    data_bytes = bytes(new_data)
    reloc_bytes = b"".join(struct.pack(">I", r) for r in new_relocs)
    public_bytes = b"".join(struct.pack(">II", do, no) for do, no in public_entries)
    string_bytes = name_bytes
    file_size = (
        HSD_HEADER
        + len(data_bytes)
        + len(reloc_bytes)
        + len(public_bytes)
        + len(string_bytes)
    )
    header = struct.pack(
        ">IIIII4s8x",
        file_size,
        len(data_bytes),
        len(new_relocs),
        len(public_entries),
        0,
        arc.version,
    )
    out = header + data_bytes + reloc_bytes + public_bytes + string_bytes
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
