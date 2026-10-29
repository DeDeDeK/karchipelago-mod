# SPDX-License-Identifier: GPL-3.0-only
"""Plan a runtime payload rebuilt from byte ranges of a retail archive.

A mod that needs part of a retail `.dat` at runtime, without shipping its bytes,
ships the plan instead: which ranges of the file to read, where each lands in one
allocation, and what every pointer in them must become. The bytes arrive raw off
the disc with every pointer still a donor offset, and translating one needs the
whole byte map, so each pointer's payload offset is precomputed and the runtime
only adds the allocation base.

`File_Read` needs a 32-byte-aligned offset, length and destination, and GX wants
the same of textures, display lists and vertex arrays, so every range is expanded
to 32-byte bounds and keeps its alignment in the payload.
"""

from collections import namedtuple

from .archive import u16, u32
from .geom_bounds import attr_vertex_bytes, parse_vtxdesc
from .walker import merge_intervals

HSD_HEADER = 0x20  # donor file offset of data offset 0
ALIGN = 32

GX_INDEX8 = 2
GX_INDEX16 = 3

Payload = namedtuple("Payload", "size ranges relocs remap")


def _align_down(v):
    return v & ~(ALIGN - 1)


def _align_up(v):
    return (v + ALIGN - 1) & ~(ALIGN - 1)


def plan_payload(arc, spans, prefix=0, rebase=None, skip=()):
    """Lay the data-section intervals in `spans` out into one payload.

    prefix  bytes the payload reserves before the first range (a multiple of 32).
    rebase  {target: kept_start} for an indexed array of which only the elements
            from `kept_start` on are kept: a pointer to `target` is rewritten so
            `target + i * stride` still lands on the same element in the payload.
    skip    reloc sources the runtime rewrites itself.

    Returns Payload(size, ranges, relocs, remap): `ranges` as (donor_file_off,
    dest_off, length), all multiples of 32; `relocs` as (dest_off, dest_val) with
    dest_val a u32 that may wrap below the payload for a rebased array; `remap`
    maps each kept data offset to its payload offset.
    """
    assert prefix % ALIGN == 0
    rebase = rebase or {}
    # Fusing ranges up to one 32-byte block apart trades a little slop for fewer reads.
    intervals = merge_intervals([(_align_down(s), _align_up(e)) for s, e in spans], gap=ALIGN)

    ranges = []
    remap = {}
    cursor = prefix
    for s, e in intervals:
        ranges.append((HSD_HEADER + s, cursor, e - s))
        for o in range(s, e):
            remap[o] = cursor + (o - s)
        cursor += e - s

    skip = set(skip)
    relocs = []
    for src in arc.relocs:
        if src not in remap or src in skip:
            continue
        tgt = u32(arc.data, src)
        if tgt in remap:
            relocs.append((remap[src], remap[tgt]))
        elif tgt in rebase:
            kept = rebase[tgt]
            relocs.append((remap[src], (remap[kept] - (kept - tgt)) & 0xFFFFFFFF))
        # A pointer whose target fell outside the kept ranges is a dangling one in
        # slop the alignment pulled in; it keeps its donor offset, and nothing reads it.

    return Payload(cursor, ranges, relocs, remap)


def indexed_spans(arc, pobjs):
    """{array: (lo, hi)} - the byte extent of each indexed vertex array the display
    lists of `pobjs` actually reference, with `lo` aligned down to 32 so a rebased
    array pointer stays 32-byte aligned."""
    data = arc.data
    out = {}
    for pobj in pobjs:
        attrs = parse_vtxdesc(data, u32(data, pobj + 0x08))
        dl = u32(data, pobj + 0x10) if (pobj + 0x10) in arc.reloc_set else 0
        if not dl:
            continue
        end = dl + u16(data, pobj + 0x0E) * 32
        vsize = sum(attr_vertex_bytes(a) for a in attrs)
        cur = dl
        while cur < end:
            op = data[cur]
            if op == 0x00:
                cur += 1
                continue
            if (op & 0xF8) not in (0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8):
                break
            count = u16(data, cur + 1)
            cur += 3
            for _ in range(count):
                o = cur
                for a in attrs:
                    if a["atype"] in (GX_INDEX8, GX_INDEX16):
                        idx = data[o] if a["atype"] == GX_INDEX8 else u16(data, o)
                        lo = a["buf"] + idx * a["stride"]
                        hi = lo + a["stride"]
                        prev = out.get(a["buf"])
                        out[a["buf"]] = (
                            (min(prev[0], lo), max(prev[1], hi)) if prev else (lo, hi)
                        )
                    o += attr_vertex_bytes(a)
                cur += vsize
    return {buf: (_align_down(lo), hi) for buf, (lo, hi) in out.items()}
