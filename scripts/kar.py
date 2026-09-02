#!/usr/bin/env python3
"""Query the static game image.

Answers questions about Kirby Air Ride from the artifacts that describe it: the
`mem1.raw` RAM snapshot, hoshi's `GKYE01.map` symbol map, `packtool/link.ld`,
and the Ghidra database.

    sym      look a symbol up by address, name, or address range
    disasm   disassemble, annotating call targets and computed addresses
    read     dump memory as hex, words, floats, halfs, or a C string
    find     search for a 32-bit value at every 4-byte-aligned offset
    decomp   Ghidra decompilation as plain C
    rename   name an unnamed symbol in the map and link.ld
    check    cross-check hoshi headers against link.ld and the map
"""

import argparse
import bisect
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MEM1 = os.path.join(HERE, "mem1.raw")
MAP = os.path.join(ROOT, "externals", "hoshi", "GKYE01.map")
LINK_LD = os.path.join(ROOT, "externals", "hoshi", "packtool", "link.ld")
HOSHI_INCLUDE = os.path.join(ROOT, "externals", "hoshi", "include")
OBJDUMP = os.path.join(
    ROOT, "externals", "devkitpro", "devkitPPC", "bin", "powerpc-eabi-objdump"
)

MEM1_BASE = 0x80000000
SDA_BASE = 0x805DD0E0  # r13
SDA2_BASE = 0x805E6700  # r2
GHIDRA_PROGRAM = "kar.dol"


def parse_addr(text):
    """A literal game address, or None if `text` is not one."""
    t = text.strip()
    if re.fullmatch(r"0[xX][0-9a-fA-F]+", t) or re.fullmatch(r"[0-9a-fA-F]{8}", t):
        return int(t, 16)
    return None


def parse_int(text):
    try:
        return int(text, 0)
    except ValueError:
        sys.exit(f"'{text}' is not a number")


# ADDR SIZE ADDR ALIGN NAME [note]
_MAP_LINE = re.compile(
    r"^([0-9a-fA-F]{8}) ([0-9a-fA-F]{6,8}) ([0-9a-fA-F]{8}) (\d+) (.*)$"
)

# The name field runs up to the first space or '(' - many rows carry a trailing
# argument note, either separated ("gmGetGlobalP (GameData)") or glued on
# ("ResultsScreen_Init(r3=ply num)").
_MAP_NAME = re.compile(r"([\w.$][\w.$@]*)\s*(.*)")


class SymbolMap:
    """GKYE01.map, indexed by address and by name."""

    def __init__(self, path=MAP):
        self.rows = []
        self.by_name = {}
        with open(path, errors="replace") as f:
            for line in f:
                m = _MAP_LINE.match(line.rstrip("\n"))
                if not m:
                    continue
                nm = _MAP_NAME.match(m.group(5).strip())
                if not nm:
                    continue
                row = (
                    int(m.group(1), 16),
                    int(m.group(2), 16),
                    nm.group(1),
                    nm.group(2).strip(),
                )
                self.rows.append(row)
                self.by_name.setdefault(row[2], row)
        self.rows.sort()
        self.addrs = [r[0] for r in self.rows]

    def at(self, addr):
        """(row, offset) for the symbol covering `addr`, else (None, 0)."""
        i = bisect.bisect_right(self.addrs, addr) - 1
        if i < 0:
            return None, 0
        row = self.rows[i]
        if row[1] and addr >= row[0] + row[1]:
            return None, 0
        return row, addr - row[0]

    def label(self, addr):
        """`name` or `name+0xoff` for `addr`, else None."""
        row, off = self.at(addr)
        if row is None:
            return None
        return row[2] if off == 0 else f"{row[2]}+0x{off:x}"

    def resolve(self, target):
        """(addr, remaining_size) for a symbol name or literal address."""
        addr = parse_addr(target)
        if addr is not None:
            row, off = self.at(addr)
            return addr, max(row[1] - off, 0) if row and row[1] else 0
        row = self.by_name.get(target)
        return (row[0], row[1]) if row else (None, 0)


_LD_LINE = re.compile(r"^\s*([A-Za-z_]\w*)\s*=\s*(0[xX][0-9a-fA-F]+)\s*;")


def read_link_ld(path=LINK_LD):
    """name -> address for every symbol link.ld exports."""
    out = {}
    with open(path) as f:
        for line in f:
            m = _LD_LINE.match(line)
            if m:
                out[m.group(1)] = int(m.group(2), 16)
    return out


def header_protos():
    """(addr, name, where) for every hoshi prototype with a `// 0xADDR` comment."""
    sys.path.insert(0, os.path.join(HERE, "ghidra"))
    import hoshi_headers

    return [
        (int(a, 16), n, w)
        for a, n, _sig, w in hoshi_headers.extract_protos(HOSHI_INCLUDE)
    ]


def mem1_size():
    if not os.path.exists(MEM1):
        sys.exit(f"{MEM1} not found (it is gitignored; capture one from Dolphin)")
    return os.path.getsize(MEM1)


def read_mem(addr, length):
    """`length` bytes of mem1.raw at game address `addr`."""
    size = mem1_size()
    off = addr - MEM1_BASE
    if off < 0 or length < 0 or off + length > size:
        sys.exit(
            f"0x{addr:08x}+0x{length:x} is outside mem1.raw "
            f"(0x{MEM1_BASE:08x}..0x{MEM1_BASE + size:08x})"
        )
    with open(MEM1, "rb") as f:
        f.seek(off)
        return f.read(length)


def cmd_sym(args):
    syms = SymbolMap()
    ld = read_link_ld()
    hdr = {a: n for a, n, _ in header_protos()}

    lo = hi = None
    if "-" in args.target:
        a, _, b = args.target.partition("-")
        lo, hi = parse_addr(a), parse_addr(b)

    if lo is not None and hi is not None:
        rows = [r for r in syms.rows if lo <= r[0] < hi]
        head = f"{len(rows)} symbol(s) in 0x{lo:08x}..0x{hi:08x}"
    elif (addr := parse_addr(args.target)) is not None:
        row, off = syms.at(addr)
        if row is None:
            sys.exit(f"no symbol covers 0x{addr:08x}")
        rows = [row]
        head = f"0x{addr:08x} is {row[2]}" + (f"+0x{off:x}" if off else "")
    else:
        needle = args.target.lower()
        rows = (
            [syms.by_name[args.target]]
            if args.target in syms.by_name
            else [r for r in syms.rows if needle in r[2].lower()]
        )
        head = f"{len(rows)} symbol(s) matching '{args.target}'"

    print(head, file=sys.stderr)
    for addr, size, name, note in rows[: args.limit]:
        tags = []
        if ld.get(name) == addr:
            tags.append("ld")
        if hdr.get(addr) == name:
            tags.append("hdr")
        cells = f"{addr:08x}  {size:06x}  {name}"
        if tags:
            cells += f"  [{' '.join(tags)}]"
        if note:
            cells += f"  {note}"
        print(cells)
    if len(rows) > args.limit:
        print(f"({len(rows) - args.limit} more; raise --limit)", file=sys.stderr)


_INSN = re.compile(r"^([0-9a-f]{8}):\t([0-9a-f ]+?)\s*\t(\S+)(?:\s+(\S.*))?$")
_TARGET = re.compile(r"0x([0-9a-f]+)$")
_MEMOP = re.compile(r"(-?\d+)\((r\d+)\)")
_REG = re.compile(r"^r\d+$")


def objdump(data, vma):
    """Disassemble a big-endian PowerPC blob, minus objdump's file preamble."""
    if not os.path.exists(OBJDUMP):
        sys.exit(f"{OBJDUMP} not found; run scripts/devkitpro/build.sh")
    fd, path = tempfile.mkstemp(suffix=".bin")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        out = subprocess.run(
            [
                OBJDUMP,
                "-D",
                "-b",
                "binary",
                "-m",
                "powerpc",
                "-EB",
                f"--adjust-vma={vma:#x}",
                path,
            ],
            capture_output=True,
            text=True,
            check=True,
        ).stdout
    finally:
        os.remove(path)
    lines = out.splitlines()
    for i, line in enumerate(lines):
        if _INSN.match(line):
            return lines[i:]
    return []


def _where(addr, syms):
    label = syms.label(addr)
    return f"0x{addr:08x} {label}" if label else f"0x{addr:08x}"


def annotate(lines, syms, start, end):
    """Append `; <fact>` to instructions whose target address is derivable:
    call targets, out-of-range branches, `lis` pairs, and r13/r2 accesses.

    Register tracking is deliberately shallow - a `lis` value survives until
    anything else writes that register - which is enough for the hi/lo pairs
    the compiler emits back to back.
    """
    hi = {}
    for line in lines:
        m = _INSN.match(line)
        if not m:
            yield line
            continue
        mnem, ops = m.group(3), m.group(4) or ""
        parts = [p.strip() for p in ops.split(",")]
        note = None

        target = _TARGET.search(ops)
        mem = _MEMOP.search(ops)
        if mnem.startswith("b") and target:
            dest = int(target.group(1), 16)
            if mnem in ("bl", "bla") or not start <= dest < end:
                note = syms.label(dest) or f"0x{dest:08x}"
        elif mnem == "lis" and len(parts) == 2 and _REG.match(parts[0]):
            hi[parts[0]] = (parse_int(parts[1]) & 0xFFFF) << 16
        elif mem and mem.group(2) == "r13":
            note = "-> " + _where(SDA_BASE + int(mem.group(1)), syms)
        elif mem and mem.group(2) == "r2":
            note = "-> " + _where(SDA2_BASE + int(mem.group(1)), syms)
        elif mem and mem.group(2) in hi:
            note = "-> " + _where(hi[mem.group(2)] + int(mem.group(1)), syms)
        elif mnem in ("addi", "ori", "addis") and len(parts) == 3 and parts[1] in hi:
            base, imm = hi[parts[1]], parse_int(parts[2])
            full = base | (imm & 0xFFFF) if mnem == "ori" else base + imm
            note = "-> " + _where(full & 0xFFFFFFFF, syms)

        if mnem != "lis" and parts and parts[0] in hi:
            del hi[parts[0]]
        yield f"{line}  ; {note}" if note else line


def cmd_disasm(args):
    syms = SymbolMap()
    start, size = syms.resolve(args.target)
    if start is None:
        sys.exit(f"unknown symbol '{args.target}'")
    length = parse_int(args.length) if args.length else size
    if not length:
        sys.exit(f"no size for 0x{start:08x} in the map; pass a length")

    label = syms.label(start) or "unnamed"
    print(
        f"{label}  0x{start:08x}..0x{start + length:08x}  ({length} bytes)",
        file=sys.stderr,
    )
    lines = objdump(read_mem(start, length), start)
    if not args.raw:
        lines = annotate(lines, syms, start, start + length)
    for line in lines:
        print(line)


def fmt_hex(addr, data):
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        cells = " ".join(f"{b:02x}" for b in chunk).ljust(47)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        yield f"{addr + i:08x}  {cells}  |{text}|"


def fmt_words(addr, data, syms):
    top = MEM1_BASE + mem1_size()
    for i in range(0, len(data) - 3, 16):
        words = [
            struct.unpack_from(">I", data, j)[0]
            for j in range(i, min(i + 16, len(data) - 3), 4)
        ]
        cells = " ".join(f"{w:08x}" for w in words).ljust(35)
        names = [n for w in words if MEM1_BASE <= w < top for n in [syms.label(w)] if n]
        line = f"{addr + i:08x}  {cells}"
        yield f"{line}  ; {', '.join(names)}" if names else line


def fmt_floats(addr, data):
    for i in range(0, len(data) - 3, 16):
        vals = [
            struct.unpack_from(">f", data, j)[0]
            for j in range(i, min(i + 16, len(data) - 3), 4)
        ]
        yield f"{addr + i:08x}  " + " ".join(f"{v:>14.6g}" for v in vals)


def fmt_halfs(addr, data):
    for i in range(0, len(data) - 1, 16):
        vals = [
            struct.unpack_from(">H", data, j)[0]
            for j in range(i, min(i + 16, len(data) - 1), 2)
        ]
        yield f"{addr + i:08x}  " + " ".join(f"{v:04x}" for v in vals)


def fmt_string(addr, data):
    for part in data.split(b"\0"):
        if part:
            yield f"{addr:08x}  {part.decode('ascii', 'replace')}"
        addr += len(part) + 1


def cmd_read(args):
    syms = SymbolMap()
    addr, size = syms.resolve(args.addr)
    if addr is None:
        sys.exit(f"unknown symbol '{args.addr}'")
    length = parse_int(args.length) if args.length else (size or 0x40)
    data = read_mem(addr, length)

    if args.format == "words":
        lines = fmt_words(addr, data, syms)
    elif args.format == "floats":
        lines = fmt_floats(addr, data)
    elif args.format == "halfs":
        lines = fmt_halfs(addr, data)
    elif args.format == "string":
        lines = fmt_string(addr, data)
    else:
        lines = fmt_hex(addr, data)
    for line in lines:
        print(line)


def cmd_find(args):
    syms = SymbolMap()
    size = mem1_size()
    lo = parse_int(args.start) if args.start else MEM1_BASE
    hi = parse_int(args.end) if args.end else MEM1_BASE + size
    needle = struct.pack(">I", parse_int(args.value) & 0xFFFFFFFF)
    data = read_mem(lo, hi - lo)

    print(f"searching 0x{lo:08x}..0x{hi:08x} for {needle.hex()}", file=sys.stderr)
    hits = 0
    pos = 0
    while (j := data.find(needle, pos)) >= 0:
        pos = j + 1
        if (lo + j) % 4:
            continue
        hits += 1
        if hits > args.limit:
            continue
        addr = lo + j
        ctx = " ".join(
            (
                f"[{struct.unpack_from('>I', data, k)[0]:08x}]"
                if k == j
                else f"{struct.unpack_from('>I', data, k)[0]:08x}"
            )
            if 0 <= k <= len(data) - 4
            else "........"
            for k in range(j - 8, j + 12, 4)
        )
        label = syms.label(addr)
        print(f"0x{addr:08x}  {ctx}" + (f"  {label}" if label else ""))
    print(
        f"{hits} match(es)"
        + (f", {hits - args.limit} not shown" if hits > args.limit else ""),
        file=sys.stderr,
    )


def cmd_decomp(args):
    for target in args.targets:
        r = subprocess.run(
            ["ghidra", "decompile", target, "--program", GHIDRA_PROGRAM],
            capture_output=True,
            text=True,
            timeout=600,
            check=False,
        )
        try:
            items = json.loads(r.stdout)
        except json.JSONDecodeError:
            print(f"{target}: {(r.stderr or r.stdout).strip()[:400]}", file=sys.stderr)
            continue
        if not items:
            print(f"{target}: no function there", file=sys.stderr)
        for item in items:
            print(f"// 0x{item['address']}  {item['signature']}")
            print(item["code"].strip() + "\n")


def cmd_rename(args):
    addr = parse_addr(args.addr)
    if addr is None:
        sys.exit(f"'{args.addr}' is not an address")

    with open(MAP, errors="replace") as f:
        lines = f.read().split("\n")
    for i, line in enumerate(lines):
        m = _MAP_LINE.match(line)
        if m and int(m.group(1), 16) == addr:
            break
    else:
        sys.exit(f"0x{addr:08x} is not in the map")

    old = _MAP_NAME.match(m.group(5).strip())
    if not old.group(1).startswith("zz_") and not args.force:
        sys.exit(f"0x{addr:08x} is already named {old.group(1)}; pass --force")

    syms = SymbolMap()
    clash = syms.by_name.get(args.name)
    if clash and clash[0] != addr:
        sys.exit(f"{args.name} already names 0x{clash[0]:08x} in the map")
    ld = read_link_ld()
    if args.name in ld and ld[args.name] != addr:
        sys.exit(f"{args.name} already names 0x{ld[args.name]:08x} in link.ld")

    lines[i] = " ".join(m.group(1, 2, 3, 4) + (args.name,))
    if old.group(2).strip():
        lines[i] += f" {old.group(2).strip()}"
    with open(MAP, "w") as f:
        f.write("\n".join(lines))
    print(f"map: 0x{addr:08x} {old.group(1)} -> {args.name}")

    if args.name in ld:
        print(f"link.ld: {args.name} already present")
        return
    with open(LINK_LD) as f:
        ldlines = f.read().split("\n")
    close = max(j for j, line in enumerate(ldlines) if line.strip() == "}")
    ldlines.insert(close, f"  {args.name} = 0x{addr:08x};")
    with open(LINK_LD, "w") as f:
        f.write("\n".join(ldlines))
    print(f"link.ld: added {args.name} = 0x{addr:08x}")


def cmd_check(args):
    syms = SymbolMap()
    ld = read_link_ld()
    by_addr = {r[0]: r[2] for r in syms.rows}
    groups = {}

    for addr, name, where in header_protos():
        mapped = syms.by_name.get(name)
        if mapped is None:
            # A real map row under a different name is one of link.ld's
            # deliberate aliases (Item_Create -> CityItem_Create), which links
            # fine; only a still-`zz_` row is drift.
            row = by_addr.get(addr)
            aliased = (
                row is not None and not row.startswith("zz_") and ld.get(name) == addr
            )
            if not aliased:
                groups.setdefault("header prototype not in the map", []).append(
                    f"0x{addr:08x} {name}  ({where})"
                )
        elif mapped[0] != addr:
            groups.setdefault("header address disagrees with the map", []).append(
                f"0x{addr:08x} {name}  map says 0x{mapped[0]:08x}  ({where})"
            )
        if name not in ld:
            groups.setdefault("header prototype not in link.ld", []).append(
                f"0x{addr:08x} {name}  ({where})"
            )
        elif ld[name] != addr:
            groups.setdefault("header address disagrees with link.ld", []).append(
                f"0x{addr:08x} {name}  link.ld says 0x{ld[name]:08x}  ({where})"
            )

    # Only a still-`zz_` map row counts as drift. An address the map has no row
    # for is the map's coarse sizing, and a differently-named row is usually one
    # of link.ld's deliberate aliases (Gm_Pause -> gmSetFreezeGameFlag).
    for name, addr in sorted(ld.items(), key=lambda kv: kv[1]):
        mapped = by_addr.get(addr)
        if mapped and mapped.startswith("zz_"):
            groups.setdefault("link.ld name still unnamed in the map", []).append(
                f"0x{addr:08x} {name}  (map: {mapped})"
            )

    total = sum(len(v) for v in groups.values())
    for title, items in groups.items():
        print(f"\n{title} ({len(items)})")
        for item in items[: args.limit]:
            print(f"  {item}")
        if len(items) > args.limit:
            print(f"  ({len(items) - args.limit} more; raise --limit)")
    print(f"\n{total} finding(s)")
    return 1 if total else 0


def main():
    p = argparse.ArgumentParser(
        prog="kar.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("sym", help="look up a symbol by address, name, or range")
    s.add_argument("target", help="0xADDR | NAME | substring | 0xLO-0xHI")
    s.add_argument("-n", "--limit", type=int, default=40)
    s.set_defaults(fn=cmd_sym)

    s = sub.add_parser("disasm", help="disassemble from mem1.raw")
    s.add_argument("target", help="0xADDR | NAME")
    s.add_argument("length", nargs="?", help="bytes; from the map if omitted")
    s.add_argument("--raw", action="store_true", help="no annotations")
    s.set_defaults(fn=cmd_disasm)

    s = sub.add_parser("read", help="dump data from mem1.raw")
    s.add_argument("addr", help="0xADDR | NAME")
    s.add_argument("length", nargs="?", help="bytes (default 0x40)")
    s.add_argument(
        "-f",
        "--format",
        default="hex",
        choices=["hex", "words", "floats", "halfs", "string"],
    )
    s.set_defaults(fn=cmd_read)

    s = sub.add_parser("find", help="find a 32-bit value in mem1.raw")
    s.add_argument("value")
    s.add_argument("start", nargs="?")
    s.add_argument("end", nargs="?")
    s.add_argument("-n", "--limit", type=int, default=200)
    s.set_defaults(fn=cmd_find)

    s = sub.add_parser("decomp", help="Ghidra decompilation as plain C")
    s.add_argument("targets", nargs="+", help="0xADDR | NAME")
    s.set_defaults(fn=cmd_decomp)

    s = sub.add_parser("rename", help="name a symbol in the map and link.ld")
    s.add_argument("addr")
    s.add_argument("name")
    s.add_argument(
        "--force", action="store_true", help="replace an already-discovered name"
    )
    s.set_defaults(fn=cmd_rename)

    s = sub.add_parser("check", help="cross-check headers, link.ld, and the map")
    s.add_argument("-n", "--limit", type=int, default=25)
    s.set_defaults(fn=cmd_check)

    args = p.parse_args()
    sys.exit(args.fn(args) or 0)


if __name__ == "__main__":
    main()
