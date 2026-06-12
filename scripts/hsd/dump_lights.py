#!/usr/bin/env python3
"""Dump LObjDesc / LightGroup chains from a stage .dat file.

The chain head offsets are stage-specific (this script defaults to
City Trial's `GrCity1.dat` layout - see `docs/sky-lighting-system.md`).
Pointers in the file are u32 file-offset values (`stored_value + 0x20
= real file offset`). NULL is `0` with no reloc entry, but this
dumper just chases the raw values and reports them.

Usage:
    uv run python scripts/hsd/dump_lights.py [<file.dat>]
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import Archive, HSD_HEADER, u16, u32


def f32(buf, off):
    return struct.unpack(">f", buf[off : off + 4])[0]


def ptr(buf, off):
    """Read a stored pointer at file offset `off` and return the file
    offset it points to, or None for NULL."""
    v = u32(buf, off)
    return None if v == 0 else v + HSD_HEADER


def vec3(buf, off):
    return (f32(buf, off), f32(buf, off + 4), f32(buf, off + 8))


def dump_wobj_desc(buf, off, label):
    """WObjDesc:
    +0x00 char *class_name
    +0x04 Vec3 position (X, Y, Z)
    """
    cn = ptr(buf, off + 0x00)
    print(
        f"      {label} @ 0x{off:X}: class={f'0x{cn:X}' if cn else 'NULL'} pos={vec3(buf, off + 0x04)}"
    )


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
        print("      raw 24B: " + " ".join(f"{b:02X}" for b in buf[u_p : u_p + 24]))
        if attnflags & 1:
            # LOBJ_RAW_PARAM: 6-float attn block
            a0, a1, a2, k0, k1, k2 = (f32(buf, u_p + 4 * i) for i in range(6))
            print(f"      attn raw: a=({a0},{a1},{a2}) k=({k0},{k1},{k2})")
        else:
            cutoff = f32(buf, u_p + 0x00)
            func = buf[u_p + 0x04]
            ref_br = f32(buf, u_p + 0x05)
            ref_dist = f32(buf, u_p + 0x09)
            dist_func = buf[u_p + 0x0D]
            print(
                f"      spot/point: cutoff={cutoff} func={func} ref_br={ref_br} ref_dist={ref_dist} dist_func={dist_func}"
            )


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
        # LightGroup: {LObjDesc *desc, LightAnim *anim} - 8 bytes
        desc_p = ptr(buf, lg_p + 0x00)
        anim_p = ptr(buf, lg_p + 0x04)
        print(
            f"  LightGroup[{i}] @ 0x{lg_p:X}: desc=0x{desc_p or 0:X} anim={f'0x{anim_p:X}' if anim_p else 'NULL'}"
        )
        if desc_p:
            dump_lobj_desc(buf, desc_p, idx)
            idx += 1
        i += 1


# Chain head file offsets - verified for GrCity1.dat. Sourced from
# stage_resource[+0x14]: {primary, tertiary, secondary} LightGroup**.
GRCITY1_CHAINS = [
    (0xC1790, "PRIMARY"),
    (0xC1870, "TERTIARY"),
    (0xC1910, "SECONDARY"),
]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "iso/files/GrCity1.dat"
    arc = Archive(path)  # sanity-checks the header, raises NotAnHSDArchive otherwise
    print(f"File: {path} ({len(arc.blob)} bytes)")
    for off, name in GRCITY1_CHAINS:
        dump_chain(arc.blob, off, name)


if __name__ == "__main__":
    main()
