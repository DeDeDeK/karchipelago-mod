#!/usr/bin/env python3
# Quick probe: walk the JObj tree of grModelSpace2[1] (the backdrop subtree)
# and print a tree of (offset, type_guess, size_known, pointer_targets).
#
# Usage: python3 scripts/dump_backdrop_tree.py iso/files/GrSpace2Model.dat
#
# Type detection is structural — we know the entry is a JOBJDesc, and we
# follow the standard HSD type relationships from there. Variable-size
# leaves (image_data / palette_data / vertex / display list) are reported
# with their declared sizes when computable.

import struct, sys
from collections import OrderedDict

HSD_HEADER = 0x20

def u32(b, o): return struct.unpack(">I", b[o:o+4])[0]
def u16(b, o): return struct.unpack(">H", b[o:o+2])[0]
def cstr(b, o):
    end = b.find(b'\0', o)
    return b[o:end].decode('ascii', errors='replace') if end >= 0 else ''

class Archive:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.blob = f.read()
        self.file_size = u32(self.blob, 0)
        self.data_size = u32(self.blob, 4)
        self.nb_reloc = u32(self.blob, 8)
        self.nb_public = u32(self.blob, 0xC)
        self.nb_extern = u32(self.blob, 0x10)
        self.data = self.blob[HSD_HEADER:HSD_HEADER+self.data_size]
        reloc_off = HSD_HEADER + self.data_size
        self.relocs = set()
        for i in range(self.nb_reloc):
            self.relocs.add(u32(self.blob, reloc_off + i*4))
        public_off = reloc_off + self.nb_reloc * 4
        extern_off = public_off + self.nb_public * 8
        string_off = extern_off + self.nb_extern * 8
        self.publics = OrderedDict()
        for i in range(self.nb_public):
            doff = u32(self.blob, public_off + i*8)
            noff = u32(self.blob, public_off + i*8 + 4)
            self.publics[cstr(self.blob, string_off + noff)] = doff
        self.externs = []
        for i in range(self.nb_extern):
            doff = u32(self.blob, extern_off + i*8)
            noff = u32(self.blob, extern_off + i*8 + 4)
            self.externs.append((doff, cstr(self.blob, string_off + noff)))

    def deref(self, off):
        # Read the pointer-as-offset stored at data[off]. Returns 0 if not in reloc table.
        if off not in self.relocs:
            v = u32(self.data, off)
            if v != 0:
                return None  # not a reloc, suspicious
            return 0
        return u32(self.data, off)

# Fields per HSD type: list of (offset, label, target_type)
TYPE_FIELDS = {
    'JOBJDesc': [(0x00,'name','cstring'),(0x08,'child','JOBJDesc'),(0x0C,'next','JOBJDesc'),
                 (0x10,'dobj','DObjDesc'),(0x38,'mtx','Mtx'),(0x3C,'robj','RObjDesc')],
    'DObjDesc': [(0x00,'name','cstring'),(0x04,'next','DObjDesc'),
                 (0x08,'mobj','MObjDesc'),(0x0C,'pobj','POBJDesc')],
    'MObjDesc': [(0x00,'name','cstring'),(0x08,'tobj','TObjDesc'),(0x0C,'mat','MaterialDesc'),
                 (0x10,'pe','PEDesc'),(0x14,'light','LightTable')],
    'TObjDesc': [(0x00,'name','cstring'),(0x04,'next','TObjDesc'),(0x4C,'image','ImageDesc'),
                 (0x50,'tlut','TlutDesc'),(0x54,'lod','TexLODDesc'),(0x58,'tev','TObjTev')],
    'ImageDesc': [(0x00,'data','image_blob')],
    'TlutDesc': [(0x00,'data','palette_blob')],
    'POBJDesc': [(0x00,'name','cstring'),(0x04,'next','POBJDesc'),(0x08,'verts','VtxDescList'),
                 (0x10,'display','dl_blob'),(0x14,'u','envelope_or_joint')],
    'RObjDesc': [(0x0C,'target','JOBJDesc')],
    'Mtx': [], 'MaterialDesc': [], 'TexLODDesc': [], 'PEDesc': [],
    'LightTable': [], 'TObjTev': [],
    'VtxDescList': [],   # variable, special handling
    'cstring': [], 'image_blob': [], 'palette_blob': [], 'dl_blob': [],
    'envelope_or_joint': [],
}

KNOWN_SIZE = {
    'JOBJDesc':0x40, 'DObjDesc':0x10, 'MObjDesc':0x18, 'TObjDesc':0x5C,
    'ImageDesc':0x18, 'TlutDesc':0x10, 'POBJDesc':0x18, 'RObjDesc':0x10,
    'Mtx':0x30, 'MaterialDesc':0x14, 'TexLODDesc':0x14, 'PEDesc':0xC,
    'LightTable':0x10, 'TObjTev':0x40,
}

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    arc = Archive(sys.argv[1])
    print(f"Archive: file={arc.file_size:#x} data={arc.data_size:#x} "
          f"relocs={arc.nb_reloc} publics={arc.nb_public} externs={arc.nb_extern}")
    for name, off in arc.publics.items():
        print(f"  public {name} @ data+{off:#x}")
    for off, name in arc.externs:
        print(f"  extern {name} @ data+{off:#x}")

    # Guess donor symbol from base filename (Gr<X>Model.dat -> grModel<X>)
    sym = None
    for name in arc.publics:
        if name.startswith('grModel') and not name.startswith('grModelMotion'):
            sym = name
            break
    if sym is None:
        print("Couldn't auto-detect grModel<X> public symbol")
        sys.exit(1)
    ms_off = arc.publics[sym]
    print(f"\nModelSection {sym} @ data+{ms_off:#x}:")
    for i in range(4):
        slot = ms_off + i*4
        v = u32(arc.data, slot)
        is_reloc = slot in arc.relocs
        print(f"  ms[{i}] @ {slot:#x}: {v:#x} {'(reloc)' if is_reloc else ''}")

    # Backdrop slot is ms[1]. Its dword is a JOBJDesc**, so the dword
    # is a pointer to a slot that contains the JOBJDesc pointer.
    backdrop_pp_off = u32(arc.data, ms_off + 4)
    print(f"\nBackdrop pp slot @ data+{backdrop_pp_off:#x}")
    backdrop_jobjdesc = u32(arc.data, backdrop_pp_off)
    print(f"Backdrop JOBJDesc @ data+{backdrop_jobjdesc:#x}")

    # BFS the tree starting at the backdrop JOBJDesc.
    visited = OrderedDict()  # offset -> type
    worklist = [(backdrop_jobjdesc, 'JOBJDesc')]
    while worklist:
        off, typ = worklist.pop(0)
        if off == 0 or off in visited:
            continue
        if typ == 'cstring':
            # Treat as leaf, just record
            visited[off] = ('cstring', cstr(arc.data, off), len(cstr(arc.data, off)) + 1)
            continue
        if typ in ('image_blob', 'palette_blob', 'dl_blob', 'envelope_or_joint',
                   'VtxDescList'):
            visited[off] = (typ, None, None)
            continue
        size = KNOWN_SIZE.get(typ, None)
        visited[off] = (typ, None, size)
        for foff, label, ftyp in TYPE_FIELDS.get(typ, []):
            slot = off + foff
            if slot in arc.relocs:
                tgt = u32(arc.data, slot)
                if tgt != 0:
                    worklist.append((tgt, ftyp))

    print(f"\nReached {len(visited)} objects:")
    by_type = {}
    for off, (t, val, sz) in visited.items():
        by_type.setdefault(t, []).append((off, sz, val))
    for t in sorted(by_type):
        rows = by_type[t]
        total_known_size = sum((sz or 0) for _, sz, _ in rows)
        print(f"  {t:18s} count={len(rows):4d}  total_known_bytes={total_known_size}")

    # Print sorted offsets to see how scattered they are
    sorted_offs = sorted(visited.keys())
    print(f"\nReachable offsets (sorted): min={sorted_offs[0]:#x} max={sorted_offs[-1]:#x}")
    print(f"Coverage approx span: {sorted_offs[-1] - sorted_offs[0]:#x} bytes "
          f"({(sorted_offs[-1] - sorted_offs[0])/1024:.0f} KB) of {arc.data_size/1024:.0f} KB total")

    # For ImageDesc objects, decode width/height/format, look up image data
    # offset, and check whether the data falls inside the brute-force span.
    print(f"\nImageDesc details (and where their image data lives):")
    span_lo = sorted_offs[0]
    span_hi = sorted_offs[-1]
    bpp_map = {0:4, 1:8, 2:8, 3:16, 4:16, 5:16, 6:32, 8:4, 9:8, 10:16, 14:4}
    image_data_offsets = {}
    for off, (t, _, _) in visited.items():
        if t != 'ImageDesc':
            continue
        w = u16(arc.data, off + 4)
        h = u16(arc.data, off + 6)
        fmt = u32(arc.data, off + 8)
        bpp = bpp_map.get(fmt, 0)
        size_bytes = (w * h * bpp + 7) // 8
        img_data_off = u32(arc.data, off)
        image_data_offsets[img_data_off] = size_bytes
        end = img_data_off + size_bytes
        in_span = span_lo <= img_data_off and end <= span_hi + 0x4000
        print(f"  ImageDesc @ {off:#x}: {w}x{h} fmt={fmt} bpp={bpp} -> "
              f"image_data @ {img_data_off:#x}..{end:#x} ({size_bytes} bytes) "
              f"{'IN-SPAN' if in_span else 'OUTSIDE'}")
    print(f"\nUnique image data ranges: {len(image_data_offsets)}")
    total_img = sum(image_data_offsets.values())
    print(f"Total image bytes (unique): {total_img} ({total_img/1024:.0f} KB)")
    img_max_end = max((off + sz) for off, sz in image_data_offsets.items())
    print(f"Highest image_data end: {img_max_end:#x}")

if __name__ == '__main__':
    main()
