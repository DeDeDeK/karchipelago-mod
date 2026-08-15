"""FOBJ packed keyframe streams - the buffer an HSD_FOBJDesc points at (+0x10).

A stream is a sequence of runs. Each run starts with a packed header whose low
nibble is the interpolation op and whose remaining bits are (key count - 1);
each key then carries a value, a tangent and a time delta to the next key,
according to what its op uses:

    op            value  tangent  delta
    0 NONE        end of stream
    1 CON  step     y                y
    2 LIN  linear   y                y
    3 SPL0 spline   y                y
    4 SPL  spline   y       y        y
    5 SLP  slope            y
    6 KEY  single   y

Packed integers are LEB128. Values and tangents are LITTLE-endian in an
otherwise big-endian file, quantized as (top 3 bits of the flag byte) with a
divisor of 1 << (low 5 bits); FLOAT ignores the divisor.

Verify a decode/encode round trip against the retail archives with:

    uv run python scripts/hsd/fobj.py verify iso/files/*.dat
"""

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from hsd.archive import Archive, NotAnHSDArchive, u32
from hsd.schema import root_for, array_length
from hsd.symbols import classify_symbol
from hsd.walker import Walker

# GXAnimDataFormat, the top 3 bits of a value/tangent flag byte.
FMT_FLOAT = 0x00
FMT_S16 = 0x20
FMT_U16 = 0x40
FMT_S8 = 0x60
FMT_U8 = 0x80

# GXInterpolationType.
OP_NONE = 0
OP_CON = 1
OP_LIN = 2
OP_SPL0 = 3
OP_SPL = 4
OP_SLP = 5
OP_KEY = 6

OP_NAMES = {OP_NONE: "NONE", OP_CON: "CON", OP_LIN: "LIN", OP_SPL0: "SPL0",
            OP_SPL: "SPL", OP_SLP: "SLP", OP_KEY: "KEY"}

HAS_VALUE = frozenset((OP_CON, OP_LIN, OP_SPL0, OP_SPL, OP_KEY))
HAS_TAN = frozenset((OP_SPL, OP_SLP))
HAS_DELTA = frozenset((OP_CON, OP_LIN, OP_SPL0, OP_SPL))

# JointTrackType HSD_A_J_PTCL. Its buffer holds particle-generator ids, not keys.
TRACK_PTCL = 40


@dataclass
class Key:
    frame: float
    value: float = 0.0
    tan: float = 0.0
    op: int = OP_CON

    def __repr__(self):
        return (f"Key(frame={self.frame:g}, value={self.value:g}, "
                f"tan={self.tan:g}, op={OP_NAMES.get(self.op, self.op)})")


class FObjError(ValueError):
    pass


def read_packed(buf, p):
    result = 0
    shift = 0
    while True:
        b = buf[p]
        p += 1
        result |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return result, p


def write_packed(out, v):
    if v < 0:
        raise FObjError(f"packed integers are unsigned, got {v}")
    while v > 0x7F:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    out.append(v & 0xFF)


def read_value(buf, p, flag):
    fmt = flag & 0xE0
    scale = float(1 << (flag & 0x1F))
    if fmt == FMT_FLOAT:
        return struct.unpack_from("<f", buf, p)[0], p + 4
    if fmt == FMT_S16:
        return struct.unpack_from("<h", buf, p)[0] / scale, p + 2
    if fmt == FMT_U16:
        return struct.unpack_from("<H", buf, p)[0] / scale, p + 2
    if fmt == FMT_S8:
        return struct.unpack_from("<b", buf, p)[0] / scale, p + 1
    if fmt == FMT_U8:
        return buf[p] / scale, p + 1
    raise FObjError(f"bad anim data format {fmt:#x}")


def write_value(out, v, flag):
    fmt = flag & 0xE0
    scale = float(1 << (flag & 0x1F))
    if fmt == FMT_FLOAT:
        out += struct.pack("<f", v)
        return
    q = int(round(v * scale))
    if fmt == FMT_S16:
        out += struct.pack("<h", q)
    elif fmt == FMT_U16:
        out += struct.pack("<H", q)
    elif fmt == FMT_S8:
        out += struct.pack("<b", q)
    elif fmt == FMT_U8:
        out += struct.pack("<B", q)
    else:
        raise FObjError(f"bad anim data format {fmt:#x}")


def decode(buf, value_flag, tan_flag):
    """Packed stream -> keys, with absolute frames accumulated from the deltas."""
    keys = []
    p = 0
    clock = 0
    while p < len(buf):
        head, p = read_packed(buf, p)
        op = head & 0x0F
        if op == OP_NONE:
            break
        if op > OP_KEY:
            raise FObjError(f"bad interpolation op {op} at {p}")
        for _ in range((head >> 4) + 1):
            value = tan = 0.0
            delta = 0
            if op in HAS_VALUE:
                value, p = read_value(buf, p, value_flag)
            if op in HAS_TAN:
                tan, p = read_value(buf, p, tan_flag)
            if op in HAS_DELTA:
                delta, p = read_packed(buf, p)
            keys.append(Key(clock, value, tan, op))
            clock += delta
    return keys


def encode(keys, value_flag, tan_flag):
    """Keys -> packed stream. Deltas are derived from the frames, so set those.

    Keys must be in nondecreasing frame order. The last key's delta is 0, which
    is how the engine's state machine recognizes the end of the track.
    """
    out = bytearray()
    i = 0
    while i < len(keys):
        op = keys[i].op
        run = 1
        while i + run < len(keys) and keys[i + run].op == op:
            run += 1
        write_packed(out, ((run - 1) << 4) | op)
        for k in range(i, i + run):
            if op in HAS_VALUE:
                write_value(out, keys[k].value, value_flag)
            if op in HAS_TAN:
                write_value(out, keys[k].tan, tan_flag)
            if op in HAS_DELTA:
                delta = int(keys[k + 1].frame - keys[k].frame) if k + 1 < len(keys) else 0
                write_packed(out, delta)
        i += run
    return bytes(out)


@dataclass
class FObjTrack:
    """One HSD_FOBJDesc: its header fields and its decoded keys."""
    off: int
    track_type: int
    value_flag: int
    tan_flag: int
    start_frame: float
    buffer: bytes

    @property
    def is_ptcl(self):
        return self.track_type == TRACK_PTCL

    @property
    def keys(self):
        if self.is_ptcl:
            raise FObjError(f"track {self.off:#x} is PTCL; its buffer is not keyframes")
        return decode(self.buffer, self.value_flag, self.tan_flag)

    def encode_keys(self, keys):
        return encode(keys, self.value_flag, self.tan_flag)


def read_fobjdesc(arc, off):
    d = arc.data
    length = u32(d, off + 0x04)
    buf_ptr = u32(d, off + 0x10) if (off + 0x10) in arc.reloc_set else 0
    return FObjTrack(
        off=off,
        track_type=d[off + 0x0C],
        value_flag=d[off + 0x0D],
        tan_flag=d[off + 0x0E],
        start_frame=struct.unpack_from(">f", d, off + 0x08)[0],
        buffer=bytes(d[buf_ptr:buf_ptr + length]) if buf_ptr else b"",
    )


def fobjdesc_offsets(arc):
    """Every FOBJDesc reachable from a public, in archive order."""
    walker = Walker(arc)
    for sym, off in arc.publics.items():
        root_type, is_array = root_for(classify_symbol(sym))
        try:
            if is_array:
                for i in range(array_length(arc, off)):
                    walker.walk(u32(arc.data, off + i * 4), root_type)
            else:
                walker.walk(off, root_type)
        except (struct.error, IndexError, RecursionError):
            continue
    return sorted(o for o, (t, _) in walker.visited.items() if t == "FOBJDesc")


def cmd_verify(args):
    """Byte-identical is the strict result; the retail exporter sometimes pads a
    run header with a redundant LEB128 continuation byte, which re-encodes one
    byte shorter and decodes to the same keys."""
    files = 0
    tracks = 0
    shorter = 0
    bad = []
    for path in args.paths:
        try:
            arc = Archive(path)
        except (NotAnHSDArchive, OSError):
            continue
        files += 1
        for off in fobjdesc_offsets(arc):
            t = read_fobjdesc(arc, off)
            if not t.buffer or t.is_ptcl:
                continue
            tracks += 1
            try:
                keys = t.keys
                again = t.encode_keys(keys)
                if again == t.buffer:
                    continue
                if decode(again, t.value_flag, t.tan_flag) == keys:
                    shorter += 1
                    continue
            except (FObjError, struct.error, IndexError) as exc:
                bad.append((path, off, f"{type(exc).__name__}: {exc}"))
                continue
            bad.append((path, off, f"{len(t.buffer)}B -> {len(again)}B"))
    print(f"{tracks} track(s) in {files} archive(s): "
          f"{tracks - shorter - len(bad)} byte-identical, "
          f"{shorter} re-packed smaller, {len(bad)} wrong")
    for path, off, why in bad[:args.show]:
        print(f"  {path} @ {off:#x}: {why}")
    return 1 if bad else 0


def cmd_dump(args):
    arc = Archive(args.path)
    offsets = [int(args.offset, 0)] if args.offset else fobjdesc_offsets(arc)
    for off in offsets:
        t = read_fobjdesc(arc, off)
        print(f"FOBJDesc @ {off:#x} track={t.track_type} start={t.start_frame:g} "
              f"len={len(t.buffer)}B value_flag={t.value_flag:#04x} "
              f"tan_flag={t.tan_flag:#04x}")
        for k in t.keys:
            print(f"    {k}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    v = sub.add_parser("verify", help="decode/encode round trip over archives")
    v.add_argument("paths", nargs="+")
    v.add_argument("--show", type=int, default=10, help="mismatches to print")
    v.set_defaults(func=cmd_verify)

    d = sub.add_parser("dump", help="print decoded keys")
    d.add_argument("path")
    d.add_argument("offset", nargs="?", help="one FOBJDesc offset; default all")
    d.set_defaults(func=cmd_dump)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
