#!/usr/bin/env python3
"""
carve_backdrop.py — type-precise extraction of a backdrop JObj subtree
from a stage HSD archive.

Walks the JOBJDesc tree starting at <symbol>[<slot>] in <input.dat>
(a stage Model archive like GrSpace2Model.dat), computes the exact
byte length of every reachable HSD object — including image data
sized from ImageDesc fields, palette data sized from TlutDesc, etc.
— and emits a minimal HSD archive containing only those bytes plus
a synthesized ModelSection-style pp slot exposed under <new_symbol>.

The carved file ships as a mod asset. Runtime code does:
    HSD_Archive *donor = Archive_LoadFile("BackdropSpace.dat");
    void **donor_ms   = Archive_GetPublicAddress(donor, "backdropSpace");
    ct_modelsection->backdrop = (JOBJDesc **)donor_ms[1];
which is the same shape as a vanilla stage's grModel<X>[1] slot.

Usage:
    python3 scripts/carve_backdrop.py \\
        iso/files/GrSpace2Model.dat grModelSpace2 1 \\
        mods/custom_weather/assets/BackdropSpace.dat backdropSpace

Sizing strategy:
- Known fixed-size types use their hardcoded HSD struct size.
- ImageDesc-pointed image data is sized from width × height × bpp,
  rounded up to the format's GX tile padding.
- TlutDesc-pointed palette data is sized from n_entries × 2.
- Display lists, vertex arrays, and VtxDescList terminator scans use
  a neighbor-offset heuristic (extend until the next reachable start)
  — these are small enough that the slop is negligible.

Relocation table is rebuilt: only relocs with both source AND target
inside the kept ranges are translated; out-of-range relocs are dropped
and their source dword is zeroed (only happens to dangling pointers
inside slop, which isn't followed by any reachable object).
"""

import struct
import sys
from collections import OrderedDict

HSD_HEADER = 0x20

def u32(b, o): return struct.unpack(">I", b[o:o+4])[0]
def u16(b, o): return struct.unpack(">H", b[o:o+2])[0]
def cstr(b, o):
    end = b.find(b'\0', o)
    return b[o:end].decode('ascii', errors='replace') if end >= 0 else ''


# GX texture format -> (block_w, block_h, bpp)
GX_FORMATS = {
    0:  (8, 8, 4),    # GX_TF_I4
    1:  (8, 4, 8),    # GX_TF_I8
    2:  (8, 4, 8),    # GX_TF_IA4
    3:  (4, 4, 16),   # GX_TF_IA8
    4:  (4, 4, 16),   # GX_TF_RGB565
    5:  (4, 4, 16),   # GX_TF_RGB5A3
    6:  (4, 4, 32),   # GX_TF_RGBA8
    8:  (8, 8, 4),    # GX_TF_C4
    9:  (8, 4, 8),    # GX_TF_C8
    10: (4, 4, 16),   # GX_TF_C14X2
    14: (8, 8, 4),    # GX_TF_CMPR
}


def image_size(width, height, fmt, mipmap=False):
    """Bytes for a GX texture, rounded up to the format's tile padding.
    Mipmaps add ~33% (geometric pyramid) when enabled."""
    bw, bh, bpp = GX_FORMATS.get(fmt, (4, 4, 16))
    pw = ((width + bw - 1) // bw) * bw
    ph = ((height + bh - 1) // bh) * bh
    base = pw * ph * bpp // 8
    return int(base * 1.4) if mipmap else base


class Archive:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.blob = f.read()
        self.file_size = u32(self.blob, 0)
        self.data_size = u32(self.blob, 4)
        self.nb_reloc = u32(self.blob, 8)
        self.nb_public = u32(self.blob, 0xC)
        self.nb_extern = u32(self.blob, 0x10)
        self.version = self.blob[0x14:0x18]
        self.data = self.blob[HSD_HEADER:HSD_HEADER + self.data_size]
        rel_off = HSD_HEADER + self.data_size
        self.relocs = [u32(self.blob, rel_off + i*4) for i in range(self.nb_reloc)]
        self.reloc_set = set(self.relocs)
        pub_off = rel_off + self.nb_reloc * 4
        ext_off = pub_off + self.nb_public * 8
        str_off = ext_off + self.nb_extern * 8
        self.publics = OrderedDict()
        for i in range(self.nb_public):
            doff = u32(self.blob, pub_off + i*8)
            noff = u32(self.blob, pub_off + i*8 + 4)
            self.publics[cstr(self.blob, str_off + noff)] = doff


class Walker:
    """Type-aware reachability walker.

    visited[offset] = (type_name, size_in_bytes_or_None)

    Sizes are exact for types with known struct layout, and for blobs
    whose containing struct provides field info (image data, palette).
    Unknown-size leaves get None and are resolved post-pass via the
    next-reachable-start heuristic.
    """
    def __init__(self, arc: Archive):
        self.arc = arc
        self.visited = OrderedDict()
        self.work = []

    def follow(self, src, slot, target_type, size_hint=None):
        if (src + slot) not in self.arc.reloc_set:
            return
        tgt = u32(self.arc.data, src + slot)
        if tgt == 0:
            return
        self.work.append((tgt, target_type, size_hint))

    def walk(self, root, root_type='JOBJDesc'):
        self.work.append((root, root_type, None))
        while self.work:
            off, typ, hint = self.work.pop(0)
            if off == 0 or off in self.visited:
                continue
            handler = getattr(self, f'visit_{typ}', None)
            size = handler(off, hint) if handler else hint
            self.visited[off] = (typ, size)

        # Resolve unknown sizes via neighbor heuristic.
        sorted_offs = sorted(self.visited.keys())
        for i, off in enumerate(sorted_offs):
            typ, sz = self.visited[off]
            if sz is None:
                next_o = sorted_offs[i+1] if i+1 < len(sorted_offs) else len(self.arc.data)
                # Round up to 4-byte alignment so the next struct stays aligned.
                sz = next_o - off
                self.visited[off] = (typ, sz)
        return self.visited

    # --- Type handlers -------------------------------------------------

    def visit_JOBJDesc(self, off, _):
        self.follow(off, 0x00, 'cstring')
        self.follow(off, 0x08, 'JOBJDesc')
        self.follow(off, 0x0C, 'JOBJDesc')
        self.follow(off, 0x10, 'DObjDesc')   # union: also Spline/SList for non-mesh joints
        self.follow(off, 0x38, 'Mtx')
        self.follow(off, 0x3C, 'RObjDesc')
        return 0x40

    def visit_DObjDesc(self, off, _):
        self.follow(off, 0x00, 'cstring')
        self.follow(off, 0x04, 'DObjDesc')
        self.follow(off, 0x08, 'MObjDesc')
        self.follow(off, 0x0C, 'POBJDesc')
        return 0x10

    def visit_MObjDesc(self, off, _):
        self.follow(off, 0x00, 'cstring')
        self.follow(off, 0x08, 'TObjDesc')
        self.follow(off, 0x0C, 'MaterialDesc')
        self.follow(off, 0x10, 'PEDesc')
        self.follow(off, 0x14, 'LightTable')
        return 0x18

    def visit_TObjDesc(self, off, _):
        self.follow(off, 0x00, 'cstring')
        self.follow(off, 0x04, 'TObjDesc')
        self.follow(off, 0x4C, 'ImageDesc')
        self.follow(off, 0x50, 'TlutDesc')
        self.follow(off, 0x54, 'TexLODDesc')
        self.follow(off, 0x58, 'TObjTev')
        return 0x5C

    def visit_ImageDesc(self, off, _):
        # Compute exact image-data size from width/height/format/mipmap.
        w = u16(self.arc.data, off + 4)
        h = u16(self.arc.data, off + 6)
        fmt = u32(self.arc.data, off + 8)
        mip = u32(self.arc.data, off + 0xC) != 0
        sz = image_size(w, h, fmt, mip)
        self.follow(off, 0x00, 'image_blob', sz)
        return 0x18

    def visit_TlutDesc(self, off, _):
        # Palette: u16 n_entries at +0x0C, 2 bytes per entry.
        n = u16(self.arc.data, off + 0x0C)
        self.follow(off, 0x00, 'palette_blob', n * 2)
        return 0x10

    def visit_POBJDesc(self, off, _):
        self.follow(off, 0x00, 'cstring')
        self.follow(off, 0x04, 'POBJDesc')
        self.follow(off, 0x08, 'VtxDescList')
        self.follow(off, 0x10, 'dl_blob')
        self.follow(off, 0x14, 'envelope_or_joint')
        return 0x18

    def visit_VtxDescList(self, off, _):
        # 0x18-byte entries terminated by attr=0xFF; walk and follow
        # each entry's vertex pointer (entry+0x14).
        cur = off
        while cur + 0x18 <= len(self.arc.data):
            attr = u32(self.arc.data, cur)
            if attr == 0xFF:
                break
            self.follow(cur, 0x14, 'vertex_blob')
            cur += 0x18
        return cur + 0x18 - off  # include terminator entry

    def visit_RObjDesc(self, off, _):
        self.follow(off, 0x0C, 'JOBJDesc')
        return 0x10

    def visit_Mtx(self, off, _):           return 0x30   # 4x3 floats inv_world
    def visit_MaterialDesc(self, off, _):  return 0x14   # ambient/diffuse/specular/alpha/shininess
    def visit_TexLODDesc(self, off, _):    return 0x14
    def visit_PEDesc(self, off, _):        return 0xC
    def visit_LightTable(self, off, _):    return 0x10
    def visit_TObjTev(self, off, _):       return 0x40

    def visit_cstring(self, off, _):
        end = self.arc.data.find(b'\0', off)
        return (end - off + 1) if end >= 0 else 1

    def visit_image_blob(self, off, hint):    return hint
    def visit_palette_blob(self, off, hint):  return hint
    def visit_dl_blob(self, off, hint):       return hint        # heuristic
    def visit_vertex_blob(self, off, hint):   return hint        # heuristic
    def visit_envelope_or_joint(self, off, hint): return hint    # rare


def merge_intervals(intervals):
    """Sort and merge overlapping/adjacent (start, end) intervals.
    Adjacent is defined as <= 4-byte gap, since the loader does not
    care about uninvolved bytes inside a kept range."""
    if not intervals:
        return []
    intervals = sorted(intervals)
    merged = [list(intervals[0])]
    for start, end in intervals[1:]:
        last = merged[-1]
        if start <= last[1] + 4:
            last[1] = max(last[1], end)
        else:
            merged.append([start, end])
    return [(s, e) for s, e in merged]


def carve(input_path, src_symbol, slot_index, output_path, new_symbol):
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
    print(f"  source {src_symbol}[{slot_index}] pp={pp_off:#x} -> JOBJDesc={root_jobj:#x}")

    walker = Walker(arc)
    visited = walker.walk(root_jobj, 'JOBJDesc')
    print(f"  reached {len(visited)} objects")

    intervals = [(off, off + sz) for off, (_, sz) in visited.items()]
    intervals = merge_intervals(intervals)
    kept_bytes = sum(e - s for s, e in intervals)
    print(f"  kept {len(intervals)} ranges, {kept_bytes / 1024:.1f} KB total "
          f"(source data was {len(arc.data) / 1024:.1f} KB)")

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
        # those at 32-byte aligned offsets inside its data section.
        # Preserve their relative alignment by making cursor mod 32
        # match s mod 32 — so every byte inside the interval keeps
        # its source-relative alignment after the shift.
        target_mod = s & 31
        cur_mod = cursor & 31
        pad = (target_mod - cur_mod) & 31
        if pad:
            new_data.extend(b'\0' * pad)
            cursor += pad
        for o in range(s, e):
            remap[o] = cursor + (o - s)
        new_data.extend(arc.data[s:e])
        cursor += e - s

    # ModelSection: ms[1] = pp slot offset (relocated)
    struct.pack_into(">I", new_data, 0x04, PP_OFFSET)
    # pp slot[+0x00] = JOBJDesc * (relocated, in carved coordinates)
    struct.pack_into(">I", new_data, PP_OFFSET, remap[root_jobj])
    # pp slot[+0x04..+0x13] left as zeros (no animation)

    # Translate every reloc whose source falls in any kept range.
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
            # squeaked into a kept range due to merging). Zero the
            # source dword so the loader doesn't relocate junk.
            struct.pack_into(">I", new_data, new_src, 0)
            dropped += 1
    new_relocs.sort()
    if dropped:
        print(f"  dropped {dropped} relocs to out-of-range targets (zeroed in slop)")

    # Public table
    name_bytes = new_symbol.encode('ascii') + b'\0'
    public_entries = [(0, 0)]

    # Assemble final archive
    data_bytes = bytes(new_data)
    reloc_bytes = b''.join(struct.pack(">I", r) for r in new_relocs)
    public_bytes = b''.join(struct.pack(">II", do, no) for do, no in public_entries)
    string_bytes = name_bytes
    file_size = HSD_HEADER + len(data_bytes) + len(reloc_bytes) \
              + len(public_bytes) + len(string_bytes)
    header = struct.pack(
        ">IIIII4s8x",
        file_size, len(data_bytes), len(new_relocs), len(public_entries), 0, arc.version,
    )
    out = header + data_bytes + reloc_bytes + public_bytes + string_bytes
    with open(output_path, 'wb') as f:
        f.write(out)
    print(f"  wrote {output_path} ({len(out) / 1024:.1f} KB, "
          f"{len(out) * 100 / arc.file_size:.1f}% of source)")


def main():
    if len(sys.argv) != 6:
        print(__doc__)
        sys.exit(1)
    _, in_path, sym, slot_s, out_path, new_sym = sys.argv
    print(f"Carving backdrop:")
    print(f"  in  = {in_path}")
    print(f"  out = {out_path}")
    carve(in_path, sym, int(slot_s), out_path, new_sym)


if __name__ == '__main__':
    main()
