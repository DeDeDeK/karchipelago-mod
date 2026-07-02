#!/usr/bin/env python3
"""Extract documented function prototypes from the hoshi headers.

A "documented" prototype is a single-line C prototype that ends in `);`
immediately followed by a `// 0xADDR` comment, e.g.

    void Rider_StartInhale(RiderData *rd); // 0x801ad2c4, force action-state ...

Those address comments are the authoritative map from a hoshi function name +
signature to its runtime address, so they can be applied verbatim to the Ghidra
program (see apply_protos.py). Function bodies end in `{`, so `static inline`
definitions are naturally excluded.

Usable both as a CLI (prints TSV: address<TAB>name<TAB>signature<TAB>location)
and as a module (`from extract_protos import extract`).
"""
import os
import re
import sys

# RET ... NAME ( PARAMS ) ; // 0xADDR
PROTO_RE = re.compile(
    r'^\s*(?P<sig>[A-Za-z_].*?\b(?P<name>[A-Za-z_]\w*)\s*\([^;{}]*\))\s*;\s*//\s*(?P<addr>0x[0-9a-fA-F]{6,8})'
)

_NON_FUNC = {'if', 'while', 'for', 'switch', 'sizeof', 'return', 'else'}


def extract(root):
    """Walk `root` for .h files and return a list of
    (address, name, signature, 'relpath:lineno') tuples, first address wins."""
    seen = set()
    rows = []
    for dirpath, _dirs, files in os.walk(root):
        for fn in sorted(files):
            if not fn.endswith('.h'):
                continue
            path = os.path.join(dirpath, fn)
            with open(path, errors='replace') as f:
                for lineno, line in enumerate(f, 1):
                    m = PROTO_RE.match(line)
                    if not m:
                        continue
                    name = m.group('name')
                    if name in _NON_FUNC:
                        continue
                    addr = m.group('addr').lower()
                    if addr in seen:
                        continue
                    seen.add(addr)
                    sig = ' '.join(m.group('sig').split())
                    loc = f'{os.path.relpath(path, root)}:{lineno}'
                    rows.append((addr, name, sig, loc))
    return rows


def main():
    if len(sys.argv) != 2:
        sys.exit('usage: extract_protos.py <hoshi_include_dir>')
    rows = extract(sys.argv[1])
    for addr, name, sig, loc in rows:
        print(f'{addr}\t{name}\t{sig}\t{loc}')
    print(f'# extracted {len(rows)} prototypes', file=sys.stderr)


if __name__ == '__main__':
    main()
