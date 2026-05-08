#!/usr/bin/env python3
"""Verify a carved HSD .dat: parse header, walk JObj tree from the
public symbol's ms[1], assert every followed pointer is internal."""
import struct, sys

HSD_HEADER = 0x20
def u32(b, o): return struct.unpack(">I", b[o:o+4])[0]

def main():
    path = sys.argv[1]
    sym = sys.argv[2] if len(sys.argv) > 2 else None
    with open(path, 'rb') as f:
        blob = f.read()
    file_size = u32(blob, 0)
    data_size = u32(blob, 4)
    nb_reloc = u32(blob, 8)
    nb_public = u32(blob, 0xC)
    nb_extern = u32(blob, 0x10)
    print(f"file={file_size:#x} data={data_size:#x} relocs={nb_reloc} "
          f"publics={nb_public} externs={nb_extern}")
    data = blob[HSD_HEADER:HSD_HEADER+data_size]
    rel_off = HSD_HEADER + data_size
    relocs = [u32(blob, rel_off + i*4) for i in range(nb_reloc)]
    pub_off = rel_off + nb_reloc * 4
    ext_off = pub_off + nb_public * 8
    str_off = ext_off + nb_extern * 8
    publics = {}
    for i in range(nb_public):
        do = u32(blob, pub_off + i*8)
        no = u32(blob, pub_off + i*8 + 4)
        end = blob.find(b'\0', str_off + no)
        name = blob[str_off + no:end].decode('ascii')
        publics[name] = do
        print(f"  public {name} @ {do:#x}")

    # Bounds check relocs
    bad = 0
    for src in relocs:
        if not (0 <= src < data_size - 3):
            print(f"  RELOC SRC OUT OF BOUNDS: {src:#x}")
            bad += 1
            continue
        tgt = u32(data, src)
        if tgt == 0:
            continue  # zeroed (intentionally dropped)
        if not (0 <= tgt < data_size):
            print(f"  RELOC TGT OUT OF BOUNDS: src={src:#x} tgt={tgt:#x}")
            bad += 1
    print(f"  {bad} bad relocs out of {nb_reloc}")

    # Walk from chosen symbol's ms[1]
    if sym is None:
        sym = next(iter(publics))
    ms_off = publics[sym]
    print(f"\nWalking {sym} @ {ms_off:#x}:")
    for i in range(4):
        v = u32(data, ms_off + i*4)
        is_rel = (ms_off + i*4) in set(relocs)
        print(f"  ms[{i}]: {v:#x} {'(reloc)' if is_rel else ''}")
    pp_off = u32(data, ms_off + 4)
    if pp_off == 0:
        print("  ms[1] is NULL!"); return
    print(f"  ms[1] -> {pp_off:#x}")
    if (ms_off + 4) not in set(relocs):
        print("  WARN: ms[1] not in reloc table")
    jobj_off = u32(data, pp_off)
    if pp_off not in set(relocs):
        print("  WARN: pp slot not in reloc table")
    print(f"  pp -> JOBJDesc @ {jobj_off:#x}")
    if not (0 <= jobj_off < data_size):
        print("  ERROR: JOBJDesc offset out of data section")
        return

    # BFS from JOBJDesc using same field info as carve_backdrop
    fields = {
        'JOBJDesc': [(0x08,'JOBJDesc'),(0x0C,'JOBJDesc'),(0x10,'DObjDesc'),
                     (0x38,'Mtx'),(0x3C,'RObjDesc')],
        'DObjDesc': [(0x04,'DObjDesc'),(0x08,'MObjDesc'),(0x0C,'POBJDesc')],
        'MObjDesc': [(0x08,'TObjDesc'),(0x0C,'MaterialDesc'),(0x10,'PEDesc'),(0x14,'LightTable')],
        'TObjDesc': [(0x04,'TObjDesc'),(0x4C,'ImageDesc'),(0x50,'TlutDesc'),
                     (0x54,'TexLODDesc'),(0x58,'TObjTev')],
        'ImageDesc': [(0x00,'image_blob')],
        'TlutDesc': [(0x00,'palette_blob')],
        'POBJDesc': [(0x04,'POBJDesc'),(0x08,'VtxDescList'),(0x10,'dl_blob'),
                     (0x14,'envelope_or_joint')],
        'RObjDesc': [(0x0C,'JOBJDesc')],
    }
    rel_set = set(relocs)
    visited = {}
    work = [(jobj_off, 'JOBJDesc')]
    bad_targets = 0
    while work:
        off, t = work.pop(0)
        if off == 0 or off in visited: continue
        visited[off] = t
        for foff, ftyp in fields.get(t, []):
            slot = off + foff
            if slot in rel_set:
                tgt = u32(data, slot)
                if tgt == 0:
                    continue
                if not (0 <= tgt < data_size):
                    print(f"  BAD POINTER: {t}@{off:#x}+{foff:#x} -> {tgt:#x}")
                    bad_targets += 1
                    continue
                work.append((tgt, ftyp))
    print(f"  reached {len(visited)} objects, {bad_targets} bad pointers")
    by_type = {}
    for off, t in visited.items():
        by_type.setdefault(t, 0)
        by_type[t] += 1
    for t, n in sorted(by_type.items()):
        print(f"    {t:18s} count={n}")

if __name__ == '__main__':
    main()
