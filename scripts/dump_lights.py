#!/usr/bin/env python3
"""Dump LObjDescs from the LightGroup** chains in a stage .dat file.

HSD .dat layout: 0x20-byte header, then data section. On-disk pointers
in the data section are 32-bit big-endian file-offset values relative to
the *start of data section* (i.e., real file offset = stored_value + 0x20).
A stored value of 0 with no reloc entry means NULL.
"""

import struct
import sys
from pathlib import Path


HEADER_SIZE = 0x20


def u32(buf, off):
    return struct.unpack(">I", buf[off : off + 4])[0]


def u16(buf, off):
    return struct.unpack(">H", buf[off : off + 2])[0]


def f32(buf, off):
    return struct.unpack(">f", buf[off : off + 4])[0]


def ptr(buf, off):
    """Read a stored pointer at file offset `off` and return the file
    offset it points to, or None for NULL."""
    v = u32(buf, off)
    if v == 0:
        return None
    return v + HEADER_SIZE


def vec3(buf, off):
    return (f32(buf, off), f32(buf, off + 4), f32(buf, off + 8))


def dump_wobj_desc(buf, off, label):
    """WObjDesc layout (verified by reading 0xC1718 in GrCity1.dat):
        +0x00 char *class_name
        +0x04 Vec3 position (X, Y, Z)
        +0x10 (next field — observed 0)
    """
    cn = ptr(buf, off + 0x00)
    print(f"      {label} @ 0x{off:X}: class={f'0x{cn:X}' if cn else 'NULL'} pos={vec3(buf, off + 0x04)}")


def dump_lobj_desc(buf, off, idx):
    class_name_p = ptr(buf, off + 0x00)
    next_p = ptr(buf, off + 0x04)
    flags = u16(buf, off + 0x08)
    attnflags = u16(buf, off + 0x0A)
    r, g, b, a = buf[off + 0x0C], buf[off + 0x0D], buf[off + 0x0E], buf[off + 0x0F]
    pos_p = ptr(buf, off + 0x10)
    int_p = ptr(buf, off + 0x14)
    u_p = ptr(buf, off + 0x18)

    print(f"  LObjDesc[{idx}] @ 0x{off:X}")
    print(f"    flags     = 0x{flags:04X}")
    print(f"    attnflags = 0x{attnflags:04X}")
    print(f"    color     = ({r:02X},{g:02X},{b:02X},{a:02X})")
    print(f"    position  = {f'0x{pos_p:X}' if pos_p else 'NULL'}")
    if pos_p:
        dump_wobj_desc(buf, pos_p, "pos")
    print(f"    interest  = {f'0x{int_p:X}' if int_p else 'NULL'}")
    if int_p:
        dump_wobj_desc(buf, int_p, "int")
    print(f"    u (light) = {f'0x{u_p:X}' if u_p else 'NULL'}")
    if u_p:
        # Dump 24 bytes of union content. LightSpot / LightPoint are
        # 0x11 bytes; LightAttn is 0x18 bytes (6 floats).
        # u.spot computed: 3 words. u.attn raw: 6 floats.
        print(f"      raw 24B: " + " ".join(f"{b:02X}" for b in buf[u_p : u_p + 24]))
        if attnflags & 1:
            # LOBJ_RAW_PARAM: 6-float attn block
            a0, a1, a2, k0, k1, k2 = (f32(buf, u_p + 4 * i) for i in range(6))
            print(f"      attn raw: a=({a0},{a1},{a2}) k=({k0},{k1},{k2})")
        else:
            # Computed spot/point: cutoff, ref_br, ref_dist (best effort)
            cutoff = f32(buf, u_p + 0x00)
            func = buf[u_p + 0x04]
            ref_br = f32(buf, u_p + 0x05)
            ref_dist = f32(buf, u_p + 0x09)
            dist_func = buf[u_p + 0x0D]
            print(f"      spot/point: cutoff={cutoff} func={func} ref_br={ref_br} ref_dist={ref_dist} dist_func={dist_func}")


def dump_chain(buf, chain_off, name):
    print(f"\n=== {name} chain @ 0x{chain_off:X} ===")
    # Chain is an array of LightGroup* (each 4 bytes), NULL-terminated.
    i = 0
    idx = 0
    while True:
        lg_p = ptr(buf, chain_off + i * 4)
        if lg_p is None:
            print(f"  [end] entry[{i}] = NULL")
            break
        # LightGroup: {LObjDesc *desc, LightAnim *anim} — 8 bytes
        desc_p = ptr(buf, lg_p + 0x00)
        anim_p = ptr(buf, lg_p + 0x04)
        print(f"  LightGroup[{i}] @ 0x{lg_p:X}: desc=0x{desc_p or 0:X} anim={f'0x{anim_p:X}' if anim_p else 'NULL'}")
        if desc_p:
            dump_lobj_desc(buf, desc_p, idx)
            idx += 1
        i += 1


def main():
    dat_path = Path(sys.argv[1] if len(sys.argv) > 1 else "iso/files/GrCity1.dat")
    buf = dat_path.read_bytes()
    print(f"File: {dat_path} ({len(buf)} bytes)")

    # From sky-lighting-system.md "Concrete CT chain contents":
    #   light_chains (small struct at stage_resource[+0x14]):
    #     +0x00 → LightGroup** primary    @ file 0xC1790
    #     +0x04 → LightGroup** tertiary   @ file 0xC1870
    #     +0x08 → LightGroup** secondary  @ file 0xC1910
    dump_chain(buf, 0xC1790, "PRIMARY")
    dump_chain(buf, 0xC1870, "TERTIARY")
    dump_chain(buf, 0xC1910, "SECONDARY")


if __name__ == "__main__":
    main()
