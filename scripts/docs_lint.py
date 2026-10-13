#!/usr/bin/env python3
"""Mechanical checks over the reference docs under `docs/`.

Every hit is a candidate for a reviewer to confirm, not a verdict.

    uv run python scripts/docs_lint.py              every doc
    uv run python scripts/docs_lint.py <path> ...   the named docs or folders
    uv run python scripts/docs_lint.py --long 250   flag docs over 250 lines

Checks:
    cross-ref     a pointer at another doc or at the docs folder
    file-ref      a repo file or path, which drifts when the code moves
    narration     history, RE process or unfinished-work framing
    non-ascii     anything outside ASCII
    stale-ident   a backticked identifier no source, header, script or map row
                  contains (renamed, deleted, or a typo)
    addr-name     a `Name (0xADDR)` pair the symbol map disagrees with
    structure     no H1 title, or more than one
    long          over the line limit
"""

import argparse
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DOCS = os.path.join(ROOT, "docs")
# The apworld in a sibling Archipelago checkout, read when present for the client-side names.
CLIENT = os.path.join(os.path.dirname(ROOT), "KARchipelago", "worlds", "kirby_air_ride")
sys.path.insert(0, HERE)

from kar import SymbolMap  # noqa: E402

SOURCE_DIRS = [
    "mods",
    os.path.join("externals", "hoshi", "include"),
    os.path.join("externals", "hoshi", "src"),
    os.path.join("externals", "hoshi", "Lib"),
    os.path.join("externals", "hoshi", "packtool"),
    "scripts",
]
SOURCE_EXT = (".c", ".h", ".s", ".py", ".ld", ".xml", ".toml", ".json")

CROSS_REF = re.compile(r"\bdocs/|[\w-]+\.md\b|\bsister doc\b|\bthe \w+ doc\b", re.I)
PATH_REF = re.compile(r"\b(?:mods|scripts|externals|src|include|art|out|iso)/[\w./<>*-]*")
FILE_NAME = re.compile(r"\b[\w-]+\.(?:c|h|s|py|ld|cpp|mk)\b")
NARRATION = re.compile(
    r"\b(verified live|verified in dolphin|confirmed live|used to\b|previously|originally|"
    r"we tried|tried first|naive|turned out|was wrong|old code|RE gap|"
    r"needs? (?:live )?confirm\w*|unconfirmed|not yet (?:confirmed|known|verified)|"
    r"TODO|TBD|FIXME|as of\b|for now\b)",
    re.I,
)
BACKTICK = re.compile(r"`([^`\n]+)`")
IDENT = re.compile(r"\w+")
NUMBER = re.compile(r"\b0[xX][0-9a-fA-F]+|\b\d[\d_]*\b")
NAME_ADDR = re.compile(r"`?(\w+)`?\s*\(\s*`?(0x8[0-1][0-9a-fA-F]{6})\b")


class Report:
    def __init__(self):
        self.hits = defaultdict(list)

    def add(self, check, where, msg):
        self.hits[check].append((where, msg))

    def print(self):
        total = sum(len(v) for v in self.hits.values())
        if not total:
            print("No hits.")
            return
        for check in sorted(self.hits):
            print(f"\n[{check}] {len(self.hits[check])}")
            for where, msg in self.hits[check]:
                print(f"  {where}  {msg}")
        print(f"\n{total} hits across {len(self.hits)} checks")


def source_files():
    for d in SOURCE_DIRS:
        for dp, dirs, files in os.walk(os.path.join(ROOT, d)):
            dirs[:] = [x for x in dirs if x not in ("assets", "__pycache__", "build", "out")]
            for f in files:
                if f.endswith(SOURCE_EXT):
                    yield os.path.join(dp, f)


def corpus(syms):
    """Identifier tokens in the code, scripts, Makefile and map, and repo file names.

    Mod folder names and disc file stems count as identifiers; source file
    stems are returned as file names, since naming one is a file reference.
    """
    idents = {row[2] for row in syms.rows}
    mods = set(os.listdir(os.path.join(ROOT, "mods")))
    idents.update(mods)
    for dp, _, files in os.walk(os.path.join(ROOT, "iso")):
        idents.update(os.path.splitext(f)[0] for f in files)
    names = set()
    paths = list(source_files()) + [os.path.join(ROOT, "Makefile")]
    for dp, _, files in os.walk(CLIENT):
        paths += [os.path.join(dp, f) for f in files if f.endswith(".py")]
    for path in paths:
        names.add(os.path.basename(path))
        names.add(os.path.splitext(os.path.basename(path))[0])
        with open(path, errors="replace") as f:
            idents.update(IDENT.findall(f.read()))
    return idents, names - mods


def doc_paths(targets):
    out = []
    for t in targets or [DOCS]:
        p = os.path.abspath(t)
        if os.path.isdir(p):
            for dp, _, files in os.walk(p):
                out += [os.path.join(dp, f) for f in files if f.endswith(".md")]
        elif p.endswith(".md"):
            out.append(p)
    return sorted(out)


def looks_like_ident(tok):
    """Code-shaped, not an English word: has an underscore or an inner capital."""
    if len(tok) < 4 or tok.isdigit():
        return False
    return "_" in tok.strip("_") or re.search(r"[a-z][A-Z]|[A-Z]{2}[a-z]", tok) is not None


def check_lines(rel, lines, rep, idents, names):
    in_code = False
    h1 = 0
    for n, line in enumerate(lines, 1):
        where = f"{rel}:{n}"
        if line.startswith("```"):
            in_code = not in_code
            continue
        if any(ord(c) > 0x7F for c in line):
            rep.add("non-ascii", where, line.strip()[:80])
        if in_code:
            continue
        if line.startswith("# "):
            h1 += 1
        for m in CROSS_REF.finditer(line):
            rep.add("cross-ref", where, m.group(0))
        for m in PATH_REF.finditer(line):
            rep.add("file-ref", where, m.group(0))
        for m in FILE_NAME.finditer(line):
            if m.group(0) in names:
                rep.add("file-ref", where, m.group(0))
        for m in NARRATION.finditer(line):
            rep.add("narration", where, line.strip()[:80])
            break
        for m in BACKTICK.finditer(line):
            span = m.group(1)
            if "." in span and re.search(r"\.\w{2,4}$", span) and " " not in span:
                continue
            for tok in IDENT.findall(NUMBER.sub(" ", span)):
                if not looks_like_ident(tok):
                    continue
                if tok in names and "_" in tok and tok.islower():
                    rep.add("file-ref", where, tok)
                elif tok not in idents:
                    rep.add("stale-ident", where, tok)
    if h1 != 1:
        rep.add("structure", rel, f"{h1} H1 titles")


def check_addr_names(rel, lines, rep, syms, idents):
    for n, line in enumerate(lines, 1):
        for m in NAME_ADDR.finditer(line):
            name, addr = m.group(1), int(m.group(2), 16)
            if not looks_like_ident(name):
                continue
            row, off = syms.at(addr)
            named = syms.by_name.get(name)
            if named and (named[0] == addr or (named[1] and named[0] <= addr < named[0] + named[1])):
                continue
            if named:
                rep.add("addr-name", f"{rel}:{n}", f"{name} is 0x{named[0]:08x} in the map, not 0x{addr:08x}")
            elif row and off == 0 and not row[2].startswith("zz_") and name not in idents:
                rep.add("addr-name", f"{rel}:{n}", f"0x{addr:08x} is {row[2]} in the map, not {name}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("paths", nargs="*", help="docs or folders (default: all of docs/)")
    p.add_argument("--long", type=int, default=300, help="line count that counts as long (default 300)")
    args = p.parse_args()

    syms = SymbolMap()
    idents, names = corpus(syms)
    rep = Report()
    for path in doc_paths(args.paths):
        rel = os.path.relpath(path, ROOT)
        with open(path, errors="replace") as f:
            lines = f.read().split("\n")
        check_lines(rel, lines, rep, idents, names)
        check_addr_names(rel, lines, rep, syms, idents)
        if len(lines) > args.long:
            rep.add("long", rel, f"{len(lines)} lines")
    rep.print()


if __name__ == "__main__":
    main()
