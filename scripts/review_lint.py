#!/usr/bin/env python3
"""Mechanical review checks for one or more mods under `mods/`.

Every hit is a candidate for a reviewer to confirm, not a verdict. Checks that
need the whole package (prefix collisions, unused identifiers, the Makefile's
INCLUDES list) read every mod but only report on the ones named.

    uv run python scripts/review_lint.py <mod> [<mod> ...]
    uv run python scripts/review_lint.py --all
"""

import argparse
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MODS = os.path.join(ROOT, "mods")
sys.path.insert(0, HERE)

from kar import SymbolMap  # noqa: E402

C_KEYWORDS = {
    "if", "for", "while", "switch", "return", "else", "case", "goto", "sizeof",
    "do", "default", "break", "continue",
}

BANNER = re.compile(r"([-=*/#~_])\1{3,}")
FILE_REF = re.compile(r"\bdocs/|\.md\b|\bscripts/|\b\w+\.(?:c|h|py|s)\b")
NARRATION = re.compile(
    r"\b(verified live|used to\b|previously|originally|we tried|tried first|"
    r"naive|turned out|was wrong|old code)\b",
    re.I,
)
BACKCOMPAT = re.compile(
    r"\b(older (?:client|apworld|seed|save|version)s?|old apworld|backwards?[- ]compat\w*|"
    r"legacy|migrat\w+|kept for (?:old|compat\w*|back\w*))\b",
    re.I,
)
TIMING = re.compile(r"\b(next round|immediately|takes effect)\b", re.I)
GAME_ADDR = re.compile(r"\b0[xX](8[0-1][0-9a-fA-F]{6})\b")
RAW_OFFSET = re.compile(r"\(\s*(?:u8|char|s8)\s*\*\s*\)\s*[\w.>\-\[\]]+\s*\)?\s*\+\s*0[xX][0-9a-fA-F]+")
# A loop bound or an uninitialized array declaration; indexing and sized initializers are not counts.
PLAYER_COUNT = re.compile(
    r"<\s*5\b(?![.\w])|\b(?!(?:return|case)\b)[A-Za-z_]\w*[\s\*]+[A-Za-z_]\w*\s*\[\s*5\s*\]\s*[;,)]"
)
HEX_FMT = re.compile(r"%[0-9]*[xX]")


def scan(text):
    """(comments, strings, nocomment, bare) for one C file.

    `comments` and `strings` are (line, text) lists. `nocomment` blanks
    comments; `bare` also blanks string and char literal contents. Both keep
    every newline so line numbers still line up.
    """
    comments, strings = [], []
    nocomment, bare = [], []
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            comments.append((line, text[i + 2 : j]))
            nocomment.append(" " * (j - i))
            bare.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            body = text[i:j]
            for k, part in enumerate(body[2:-2].split("\n")):
                comments.append((line + k, part))
            blank = re.sub(r"[^\n]", " ", body)
            nocomment.append(blank)
            bare.append(blank)
            line += body.count("\n")
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            lit = text[i:j]
            if c == '"':
                strings.append((line, lit[1:-1]))
            nocomment.append(lit)
            bare.append(c + " " * max(len(lit) - 2, 0) + c)
            i = j
        else:
            if c == "\n":
                line += 1
            nocomment.append(c)
            bare.append(c)
            i += 1
    return comments, strings, "".join(nocomment), "".join(bare)


class Source:
    def __init__(self, mod, path):
        self.mod = mod
        self.path = path
        self.rel = os.path.relpath(path, ROOT)
        with open(path, "rb") as f:
            self.raw = f.read()
        self.text = self.raw.decode("utf-8", errors="replace")
        self.comments, self.strings, self.nocomment, self.bare = scan(self.text)
        self.lines = self.nocomment.split("\n")
        self.bare_lines = self.bare.split("\n")

    def line_of(self, offset):
        return self.nocomment.count("\n", 0, offset) + 1

    def statement_at(self, offset):
        """The statement or preprocessor line containing `offset`."""
        lo = max(self.nocomment.rfind(ch, 0, offset) for ch in ";{}")
        hi_candidates = [self.nocomment.find(ch, offset) for ch in ";{"]
        hi = min([h for h in hi_candidates if h >= 0] or [len(self.nocomment)])
        stmt = self.nocomment[lo + 1 : hi + 1]
        line_start = self.nocomment.rfind("\n", 0, offset) + 1
        line_end = self.nocomment.find("\n", offset)
        line = self.nocomment[line_start : line_end if line_end >= 0 else None]
        return line if line.lstrip().startswith("#") else stmt

    def functions(self):
        """(line, name) for every Allman-style function definition."""
        out = []
        for idx, ln in enumerate(self.bare_lines):
            if not ln or ln[0] in " \t#}" or ln.rstrip().endswith(";"):
                continue
            m = re.match(r"^[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\(", ln)
            if not m or m.group(1) in C_KEYWORDS:
                continue
            nxt = next((l for l in self.bare_lines[idx + 1 :] if l.strip()), "")
            if nxt.strip().startswith("{") or ln.rstrip().endswith("{"):
                out.append((idx + 1, m.group(1)))
        return out

    def enclosing_function(self, line):
        best = None
        for fl, name in self.functions():
            if fl <= line:
                best = name
        return best


def mod_sources(mod):
    out = []
    for sub in ("src", "include"):
        d = os.path.join(MODS, mod, sub)
        for dp, _, files in os.walk(d):
            for f in sorted(files):
                if f.endswith((".c", ".h")):
                    out.append(Source(mod, os.path.join(dp, f)))
    return out


def all_mods():
    return sorted(
        d for d in os.listdir(MODS) if os.path.isdir(os.path.join(MODS, d, "src"))
    )


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


def check_text(src, rep):
    for idx, ln in enumerate(src.raw.split(b"\n"), 1):
        if any(b > 0x7F for b in ln):
            rep.add("non-ascii", f"{src.rel}:{idx}", ln.decode("utf-8", "replace").strip()[:80])
    for line, body in src.comments:
        where = f"{src.rel}:{line}"
        text = body.strip()
        if BANNER.search(text):
            rep.add("comment-banner", where, text[:80])
        if FILE_REF.search(text):
            rep.add("comment-file-ref", where, text[:80])
        if NARRATION.search(text):
            rep.add("comment-narration", where, text[:80])
        if BACKCOMPAT.search(text):
            rep.add("backcompat", where, text[:80])
    for line, lit in src.strings:
        if TIMING.search(lit):
            rep.add("option-timing-wording", f"{src.rel}:{line}", lit[:80])


def check_osreport(src, rep, prefixes):
    for m in re.finditer(r"\bOSReport\s*\(\s*\"((?:[^\"\\\n]|\\.)*)\"", src.nocomment):
        line = src.line_of(m.start())
        where = f"{src.rel}:{line}"
        lit = m.group(1)
        pm = re.match(r"\[([^\]]+)\]", lit)
        if not pm:
            rep.add("osreport-prefix", where, lit[:80])
        else:
            prefixes[pm.group(1)].add(src.mod)
        body = lit.replace("\\n", "").rstrip()
        if body.endswith("...") or (body.endswith(".") and not body.endswith("..")):
            rep.add("osreport-style", where, f"trailing period or ellipsis: {lit[:60]}")
        stmt = src.statement_at(m.start())
        if HEX_FMT.search(lit) and re.search(r"mask|bits|flags", stmt, re.I):
            rep.add("osreport-style", where, f"mask printed as hex, use MaskBits: {lit[:60]}")


def check_code(src, rep, syms, addr_hi):
    rel = src.rel
    text = src.nocomment
    if src.path.endswith(".h"):
        if "#pragma once" in text:
            rep.add("header-guard", rel, "#pragma once")
        if not re.search(r"^\s*#ifndef\s+(\w+)\s*\n\s*#define\s+\1\b", text, re.M):
            rep.add("header-guard", rel, "no #ifndef/#define include guard")
    for m in re.finditer(r"_Static_assert\s*\(", text):
        stmt = text[m.start() : text.find(";", m.start()) + 1]
        counted = re.sub(r"sizeof\s*\([^)]*\)\s*/\s*sizeof\s*\([^)]*\)", "N", stmt)
        if "offsetof" in stmt or re.search(r"sizeof\s*\([^)]*\)\s*==\s*(?:0[xX])?[0-9]", counted):
            rep.add("static-assert-layout", f"{rel}:{src.line_of(m.start())}", " ".join(stmt.split())[:90])
    for m in re.finditer(r"CODEPATCH_REPLACEFUNC\s*\(\s*0[xX]", text):
        rep.add("game-address", f"{rel}:{src.line_of(m.start())}", "REPLACEFUNC takes the function name, not hex")
    for m in GAME_ADDR.finditer(text):
        addr = int(m.group(1), 16)
        if addr < 0x80003000 or addr >= addr_hi:
            continue
        stmt = src.statement_at(m.start())
        if "CODEPATCH_" in stmt:
            continue
        row, off = syms.at(addr)
        # An entry address listed beside mid-function ones is an instruction site, not a call.
        if off == 0 and any(
            syms.at(a)[1] for a in (int(x, 16) for x in GAME_ADDR.findall(stmt)) if a != addr
        ):
            continue
        where = f"{rel}:{src.line_of(m.start())}"
        if row is None:
            rep.add("game-address", where, f"0x{addr:08x} has no map symbol")
        elif off == 0 and row[2].startswith("zz_"):
            rep.add("game-address", where, f"0x{addr:08x} is unnamed {row[2]}: rename and prototype it")
        elif off == 0:
            rep.add("game-address", where, f"0x{addr:08x} is {row[2]}: use the name")
    for m in RAW_OFFSET.finditer(text):
        rep.add("raw-offset", f"{rel}:{src.line_of(m.start())}", " ".join(m.group(0).split()))
    for m in re.finditer(r"\bPAUSEKIND_GAME\b", text):
        rep.add("pause-check", f"{rel}:{src.line_of(m.start())}", "manual match-pause check; a GAMEPLINK_1 proc freezes on its own")
    for m in PLAYER_COUNT.finditer(src.bare):
        rep.add("player-count", f"{rel}:{src.line_of(m.start())}", f"literal 5: PLY_NUM? ({' '.join(m.group(0).split())})")
    for m in re.finditer(r"\bHoshi_ImportMod\s*\(", text):
        line = src.line_of(m.start())
        fn = src.enclosing_function(line)
        if fn and (fn == "OnBoot" or fn.endswith("_OnBoot")):
            rep.add("import-in-onboot", f"{rel}:{line}", f"Hoshi_ImportMod in {fn}")


def token_counts(sources):
    counts = defaultdict(int)
    for s in sources:
        for t in re.findall(r"\b[A-Za-z_]\w*\b", s.bare):
            counts[t] += 1
    return counts


DECL = re.compile(
    r"^\s*(?!(?:return|else|case|goto)\b)(?:[A-Za-z_]\w*[\s\*]+)+([A-Za-z_]\w*)\s*\("
)


def decl_counts(sources):
    """Prototype and definition lines per function name across the package."""
    counts = defaultdict(int)
    for s in sources:
        for ln in s.bare_lines:
            m = DECL.match(ln)
            if m:
                counts[m.group(1)] += 1
    return counts


def check_unused(targets, package, rep):
    counts = token_counts(package)
    decls = decl_counts(package)
    for src in targets:
        for line, name in src.functions():
            if counts[name] - decls[name] <= 0:
                rep.add("unused", f"{src.rel}:{line}", f"function {name} has no caller")
        local = defaultdict(int)
        for t in re.findall(r"\b[A-Za-z_]\w*\b", src.bare):
            local[t] += 1
        for idx, ln in enumerate(src.bare_lines, 1):
            m = re.match(r"^static\s+[^()]*?\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\])*\s*(?:=|;)", ln)
            if m and local[m.group(1)] == 1:
                rep.add("unused", f"{src.rel}:{idx}", f"static {m.group(1)} is never referenced")
            m = re.match(r"^\s*#\s*define\s+([A-Za-z_]\w*)", ln)
            if m and counts[m.group(1)] == 1:
                rep.add("unused", f"{src.rel}:{idx}", f"#define {m.group(1)} is never referenced")
        # A member can hold its siblings' values in place, so only a wholly unread enum is dead.
        for em in re.finditer(r"\benum\b\s*(\w*)[^{;]*\{([^}]*)\}\s*(\w*)", src.bare):
            members = re.findall(r"(?:^|,)\s*([A-Za-z_]\w*)", em.group(2))
            if members and all(counts[m] == 1 for m in members):
                name = em.group(1) or em.group(3) or members[0]
                rep.add("unused", f"{src.rel}:{src.line_of(em.start())}", f"enum {name}: no member is referenced")


def check_package(targets, prefixes, rep):
    for prefix, mods in sorted(prefixes.items()):
        if len(mods) > 1 and mods & set(targets):
            rep.add("osreport-prefix-collision", f"[{prefix}]", "used in " + ", ".join(sorted(mods)))
    with open(os.path.join(ROOT, "Makefile")) as f:
        makefile = f.read()
    for mod in targets:
        if os.path.isdir(os.path.join(MODS, mod, "include")) and f"/{mod}/include" not in makefile:
            rep.add("makefile-includes", f"mods/{mod}/include", "public include dir missing from the Makefile's INCLUDES")


def script_texts():
    out = []
    for dp, _, files in os.walk(HERE):
        for f in files:
            if f.endswith(".py"):
                with open(os.path.join(dp, f), errors="replace") as fh:
                    out.append(fh.read())
    return "\n".join(out)


def referenced(stem, scripts):
    if stem in scripts:
        return True
    words = re.findall(r"[A-Z][a-z0-9]*|[a-z0-9]+", stem)
    return len(words) > 2 and "".join(words[:-1]) in scripts


def check_assets(targets, rep):
    scripts = script_texts()
    for mod in targets:
        d = os.path.join(MODS, mod, "assets")
        for dp, _, files in os.walk(d):
            for f in sorted(files):
                if not referenced(os.path.splitext(f)[0], scripts):
                    rep.add("asset-no-author", os.path.relpath(os.path.join(dp, f), ROOT), "no script under scripts/ names it")
    art = os.path.join(ROOT, "art")
    for dp, _, files in os.walk(art):
        for f in sorted(files):
            if f.endswith(".png") and not referenced(f, scripts) and not referenced(os.path.splitext(f)[0], scripts):
                rep.add("art-no-script", os.path.relpath(os.path.join(dp, f), ROOT), "no script reads it")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("mods", nargs="*", help="folder names under mods/")
    p.add_argument("--all", action="store_true", help="every mod")
    args = p.parse_args()

    known = all_mods()
    targets = known if args.all else args.mods
    if not targets:
        p.error("name a mod or pass --all")
    bad = [m for m in targets if m not in known]
    if bad:
        p.error(f"not a mod: {', '.join(bad)} (have: {', '.join(known)})")

    syms = SymbolMap()
    last = syms.rows[-1]
    addr_hi = last[0] + max(last[1], 4)

    package = [s for m in known for s in mod_sources(m)]
    target_srcs = [s for s in package if s.mod in targets]
    rep = Report()
    prefixes = defaultdict(set)
    for src in package:
        sink = rep if src.mod in targets else Report()
        check_osreport(src, sink, prefixes)
    for src in target_srcs:
        check_text(src, rep)
        check_code(src, rep, syms, addr_hi)
    check_unused(target_srcs, package, rep)
    check_package(targets, prefixes, rep)
    check_assets(targets, rep)
    rep.print()


if __name__ == "__main__":
    main()
