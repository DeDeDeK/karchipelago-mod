#!/usr/bin/env python3
"""Survey all Gr*Model.dat archives in iso/files and list which
ones expose a backdrop subtree (public grModel<X>[1] != NULL).

Usage: python3 scripts/probe_backdrops.py
"""
import os, struct, sys, glob
from collections import OrderedDict

HSD_HEADER = 0x20

def u32(b, o): return struct.unpack(">I", b[o:o+4])[0]
def cstr(b, o):
    end = b.find(b'\0', o)
    return b[o:end].decode('ascii', errors='replace') if end >= 0 else ''

def survey(path):
    with open(path, 'rb') as f:
        blob = f.read()
    file_size = u32(blob, 0)
    data_size = u32(blob, 4)
    nb_reloc = u32(blob, 8)
    nb_public = u32(blob, 0xC)
    nb_extern = u32(blob, 0x10)
    data = blob[HSD_HEADER:HSD_HEADER+data_size]
    rel_off = HSD_HEADER + data_size
    pub_off = rel_off + nb_reloc * 4
    ext_off = pub_off + nb_public * 8
    str_off = ext_off + nb_extern * 8
    publics = OrderedDict()
    for i in range(nb_public):
        do = u32(blob, pub_off + i*8)
        no = u32(blob, pub_off + i*8 + 4)
        publics[cstr(blob, str_off + no)] = do

    # Find grModel<X> public (the ModelSection), excluding grModelMotion variants.
    sym = None
    for n in publics:
        if n.startswith('grModel') and 'Motion' not in n:
            sym = n
            break
    if not sym:
        return None

    ms_off = publics[sym]
    if ms_off + 16 > len(data):
        return (sym, None, None, file_size)
    ms = [u32(data, ms_off + i*4) for i in range(4)]
    pp1 = ms[1]
    jobj1 = u32(data, pp1) if 0 < pp1 < len(data) - 4 else 0
    return (sym, ms, jobj1, file_size)

def main():
    files = sorted(glob.glob('iso/files/Gr*Model.dat'))
    print(f"{'File':32s} {'Symbol':24s} {'ms[1]':>10s} {'JOBJ':>10s} {'size_kb':>10s}")
    for p in files:
        info = survey(p)
        name = os.path.basename(p)
        if info is None:
            print(f"{name:32s} {'(no grModel pub)':24s}")
            continue
        sym, ms, jobj1, sz = info
        if ms is None:
            print(f"{name:32s} {sym:24s} (bad)")
            continue
        ms1 = ms[1]
        marker = "yes" if jobj1 else "NO"
        print(f"{name:32s} {sym:24s} {ms1:#10x} {jobj1:#10x} {sz/1024:>10.1f}  {marker}")

if __name__ == '__main__':
    main()
