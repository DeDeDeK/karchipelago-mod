#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""HSD .dat archive explorer.

Subcommands:
    ls <file.dat>                  Header + publics/externs + type guesses.
    tree <file.dat> [<public>]     Walk the typed tree at <public> (or first).
    grdata <file.dat> [<public>]   Decode a KAR_grData public's stage data.
    find <pattern> [<glob>...]     Grep public/extern symbols across .dat files.

Examples:
    uv run python scripts/hsd/explore.py ls iso/files/GrSpace2Model.dat
    uv run python scripts/hsd/explore.py tree iso/files/GrSpace2Model.dat grModelSpace2
    uv run python scripts/hsd/explore.py grdata iso/files/GrCity1.dat --expand collision
    uv run python scripts/hsd/explore.py find grModel iso/files/Gr*Model.dat
"""

import argparse
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hsd import Archive, Walker, classify_symbol, u32
from hsd.archive import NotAnHSDArchive
from hsd.format import describe
from hsd.grdata import SECTION_NAMES, print_grdata
from hsd.schema import SCHEMA, array_length, resolved_fields, root_for


def _open_or_complain(path):
    """Return Archive(path) or None after printing a clear error."""
    try:
        return Archive(path)
    except NotAnHSDArchive as e:
        print(str(e), file=sys.stderr)
        return None
    except (OSError, ValueError) as e:
        print(f"{path}: {e}", file=sys.stderr)
        return None


def cmd_ls(args):
    arc = _open_or_complain(args.path)
    if arc is None:
        return 1
    print(f"{args.path}")
    print(f"  file_size  = {arc.file_size:#x} ({arc.file_size / 1024:.1f} KB)")
    print(f"  data_size  = {arc.data_size:#x} ({arc.data_size / 1024:.1f} KB)")
    print(f"  nb_reloc   = {arc.nb_reloc}")
    print(f"  nb_public  = {arc.nb_public}")
    print(f"  nb_extern  = {arc.nb_extern}")
    version = arc.version.rstrip(b"\0").decode("ascii", errors="replace")
    print(f"  version    = {version!r}")

    if arc.publics:
        print("\n  publics:")
        for name, off in arc.publics.items():
            klass = classify_symbol(name)
            print(f"    {off:#08x}  {name}  [{klass or '?'}]")
    if arc.externs:
        print("\n  externs:")
        for off, name in arc.externs:
            print(f"    {off:#08x}  {name}")
    return 0


def _walk_print(arc, root, root_type, max_depth):
    """DFS print with indent. Re-entry of a visited offset shows as a cycle."""
    visited = {}  # offset -> first-print depth
    counts = {}  # type -> count

    def line(depth, label, typ, off, suffix=""):
        size = SCHEMA[typ].size if typ in SCHEMA else None
        sztag = f" [{size:#x}]" if size else ""
        print(f"{'  ' * depth}{label}{typ} @ {off:#x}{sztag}{suffix}")

    def go(depth, label, typ, off):
        if off == 0:
            return
        counts[typ] = counts.get(typ, 0) + 1
        if off in visited:
            line(depth, label, typ, off,
                 f" (cycle, first seen at depth {visited[off]})")
            return
        visited[off] = depth
        line(depth, label, typ, off, describe(arc, typ, off))
        if max_depth is not None and depth >= max_depth:
            return
        for fd in resolved_fields(arc, typ, off):
            if fd.label is None:
                continue  # class names, bind matrices, raw blobs
            slot = off + fd.off
            if slot not in arc.reloc_set:
                continue
            target = u32(arc.data, slot)
            if fd.kind == "ptr":
                go(depth + 1, f"{fd.label}: ", fd.type, target)
                continue
            if not target and fd.kind != "buffer":
                continue
            count = _field_count(arc, off, fd)
            indent = "  " * (depth + 1)
            if fd.kind == "buffer":
                nbytes = count * fd.stride
                print(f"{indent}{fd.label}: {fd.type}[{count}] @ {target:#x} "
                      f"({nbytes} B)")
                continue
            print(f"{indent}{fd.label}: {fd.type}[{count}] @ {target:#x}")
            for i in range(count):
                if fd.kind == "records":
                    go(depth + 2, f"[{i}]: ", fd.type,
                       target + i * SCHEMA[fd.type].size)
                    continue
                entry = target + i * fd.stride
                if entry + 4 > len(arc.data) or entry not in arc.reloc_set:
                    break
                if u32(arc.data, entry):
                    go(depth + 2, f"[{i}]: ", fd.type, u32(arc.data, entry))

    go(0, "", root_type, root)
    return counts


def _field_count(arc, off, fd):
    """Number of entries a non-`ptr` field addresses."""
    if fd.kind == "array":
        return array_length(arc, u32(arc.data, off + fd.off))
    if fd.kind == "run":
        tbl = u32(arc.data, off + fd.off)
        n = 0
        while (tbl + n * 4) in arc.reloc_set:
            n += 1
        return n
    width = fd.cnt_w
    slot = off + fd.cnt_off
    if slot + width > len(arc.data):
        return 0
    return int.from_bytes(arc.data[slot:slot + width], "big")


def _print_counts(counts):
    if not counts:
        return
    print("\n# type counts:")
    width = max(len(t) for t in counts)
    for t in sorted(counts):
        print(f"  {t:{width}s} {counts[t]}")


def cmd_tree(args):
    arc = _open_or_complain(args.path)
    if arc is None:
        return 1
    if args.root_type and args.root_type not in SCHEMA:
        print(f"unknown --root-type {args.root_type!r}. Known types: "
              f"{', '.join(sorted(SCHEMA))}", file=sys.stderr)
        return 1
    sym = args.symbol
    if sym is None:
        if not arc.publics:
            print("no public symbols")
            return 1
        sym = next(iter(arc.publics))
        print(f"# defaulting to first public: {sym}")
    if sym not in arc.publics:
        print(f"public symbol {sym!r} not found. Available: {list(arc.publics)}")
        return 1

    off = arc.publics[sym]
    klass = classify_symbol(sym)
    print(f"# {sym} @ {off:#x}  type={klass or '?'}")

    counts = {}
    auto_type, is_array = root_for(klass)
    if not args.root_type and is_array:
        root_type = auto_type
        print(f"# auto root-type: NullPtrArray<{root_type}> (from {klass})")
        print(f"{root_type}[] @ {off:#x}")
        roots = [u32(arc.data, off + i * 4) for i in range(array_length(arc, off))]
    else:
        root_type = args.root_type or auto_type
        if not args.root_type and root_type != "JOBJDesc":
            print(f"# auto root-type: {root_type} (from {klass})")
        roots = [off]

    for root in roots:
        for t, n in _walk_print(arc, root, root_type, args.max_depth).items():
            counts[t] = counts.get(t, 0) + n
    _print_counts(counts)

    if args.summary:
        print("\n# full reachable summary (sized):")
        walker = Walker(arc)
        for root in roots:
            walker.walk(root, root_type)
        by_type = {}
        for t, sz in walker.visited.values():
            by_type.setdefault(t, []).append(sz or 0)
        total = 0
        for t in sorted(by_type):
            sizes = by_type[t]
            total += sum(sizes)
            print(f"  {t:22s} count={len(sizes):4d}  total={sum(sizes)} B")
        print(f"  {'TOTAL':22s} {total} B ({total / 1024:.1f} KB)")
    return 0


def cmd_grdata(args):
    arc = _open_or_complain(args.path)
    if arc is None:
        return 1

    sym = args.symbol
    if sym is None:
        for name in arc.publics:
            if classify_symbol(name) in ("KAR_grData", "KAR_grDataCommon"):
                sym = name
                print(f"# defaulting to {sym}")
                break
        if sym is None:
            print("no grData* public found", file=sys.stderr)
            return 1
    elif sym not in arc.publics:
        print(f"public symbol {sym!r} not found. Available: {list(arc.publics)}",
              file=sys.stderr)
        return 1

    off = arc.publics[sym]
    print(f"# {sym} @ {off:#x}  type={classify_symbol(sym) or '?'}")
    print_grdata(arc, off, args.expand)
    return 0


def cmd_find(args):
    pat = re.compile(args.pattern)
    paths = sorted(p for g in args.globs for p in glob.glob(g))
    if not paths:
        print(f"no files matched: {args.globs}")
        return 1

    hits = 0
    skipped = 0
    for path in paths:
        try:
            arc = Archive(path)
        except NotAnHSDArchive:
            skipped += 1
            continue
        except OSError as e:
            print(f"{path}: open failed: {e}", file=sys.stderr)
            continue
        if not args.externs_only:
            for name, off in arc.publics.items():
                if pat.search(name):
                    print(f"{path}  pub  {off:#08x}  {name}  "
                          f"[{classify_symbol(name) or '?'}]")
                    hits += 1
        if not args.publics_only:
            for off, name in arc.externs:
                if pat.search(name):
                    print(f"{path}  ext  {off:#08x}  {name}")
                    hits += 1
    suffix = f" ({skipped} skipped as non-HSD)" if skipped else ""
    print(f"\n# {hits} hit(s) across {len(paths)} file(s){suffix}")
    return 0


def main(argv):
    p = argparse.ArgumentParser(prog="hsd/explore.py",
                                description="HSD .dat archive explorer.")
    sub = p.add_subparsers(dest="cmd", required=True)

    pls = sub.add_parser("ls", help="list header, publics, externs")
    pls.add_argument("path")
    pls.set_defaults(func=cmd_ls)

    ptree = sub.add_parser("tree", help="walk the typed tree at a public symbol")
    ptree.add_argument("path")
    ptree.add_argument("symbol", nargs="?", default=None)
    ptree.add_argument("--root-type", default=None,
                       help=f"type at the root (one of: {', '.join(sorted(SCHEMA))})")
    ptree.add_argument("--max-depth", type=int, default=None)
    ptree.add_argument("--no-summary", dest="summary", action="store_false",
                       help="skip the reachable type/size footer")
    ptree.set_defaults(func=cmd_tree, summary=True)

    pgd = sub.add_parser("grdata", help="decode a KAR_grData public")
    pgd.add_argument("path")
    pgd.add_argument("symbol", nargs="?", default=None)
    pgd.add_argument("--expand", action="append", default=[],
                     choices=SECTION_NAMES + ["all"],
                     help="expand a sub-node in full (repeatable)")
    pgd.set_defaults(func=cmd_grdata)

    pfn = sub.add_parser("find", help="grep public/extern symbols across .dat files")
    pfn.add_argument("pattern")
    pfn.add_argument("globs", nargs="*", default=["iso/files/*.dat"],
                     help="file globs (default: iso/files/*.dat)")
    grp = pfn.add_mutually_exclusive_group()
    grp.add_argument("--publics-only", action="store_true",
                     help="skip extern symbol matches")
    grp.add_argument("--externs-only", action="store_true",
                     help="skip public symbol matches")
    pfn.set_defaults(func=cmd_find)

    args = p.parse_args(argv[1:])
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
