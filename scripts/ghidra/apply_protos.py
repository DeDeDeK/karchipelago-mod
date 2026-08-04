#!/usr/bin/env python3
"""Apply hoshi's documented function prototypes to the Ghidra program.

Phase 2 of the type-import workflow (phase 1 = import_types.sh, which loads the
struct/enum/typedef definitions). This reads every `// 0xADDR`-annotated
prototype from the hoshi headers (via extract_protos.extract) and sets each
function's signature in Ghidra by address, so params and return values pick up
the real hoshi types.

It talks to Ghidra through the `ghidra` CLI (the ghidra-cli bridge), one
`function set-signature` per prototype. Nothing is written to disk until you run
`ghidra analyze` afterward (the bridge does not auto-save).

Rules applied:
  * Address with no function in Ghidra  -> skipped (e.g. data-region symbols).
  * Header name matches Ghidra name       -> applied as-is.
  * Ghidra name is FUN_/undefined/zz_     -> applied (names the function).
  * Ghidra already has a different real   -> keep Ghidra's name, apply hoshi
    name (e.g. HSD_-prefixed)                types only (name token swapped out).

CParser/set-signature syntax fixes applied to every signature up front:
  * pointer return `T *name(` -> `T * name(`   (so `*` binds to the return type)
  * array param `name[N]`     -> `*name`        (parser rejects `[]` params)
  * `const ` qualifiers dropped                 (parser can't resolve `const T *`)

Also sets return types for the global getters in RETURN_TYPE_OVERRIDES, which
Ghidra propagates to every caller automatically.

Usage:
    uv run python scripts/ghidra/apply_protos.py --project kar-decomp [--dry-run]
    # then, to persist:
    ghidra analyze --project kar-decomp
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from extract_protos import extract  # noqa: E402

HOSHI_INCLUDE = os.path.normpath(os.path.join(HERE, '..', '..', 'externals', 'hoshi', 'include'))

# Global getters whose return type, once set, propagates to all callers.
RETURN_TYPE_OVERRIDES = {
    'gmGetGlobalP': 'GameData *',
}


def is_unnamed(name):
    return name.startswith(('FUN_', 'undefined', 'zz_'))


def fix_syntax(sig):
    sig = sig.replace('const ', '')
    # pointer return: bind '*' to the return type by spacing it off the name
    sig = re.sub(r'\*([A-Za-z_]\w*)\s*\(', r'* \1(', sig, count=1)
    # array params decay to pointers; the parser rejects `[]`
    sig = re.sub(r'([A-Za-z_]\w*)\s*\[\s*\d*\s*\]', r'*\1', sig)
    return sig


def ghidra(args, project):
    return subprocess.run(
        ['ghidra', *args, '--project', project, '--json'],
        capture_output=True, text=True)


def ghidra_func_map(project):
    r = ghidra(['function', 'list', '--fields', 'address,name', '--limit', '60000'], project)
    if r.returncode != 0:
        sys.exit(f'ghidra function list failed:\n{r.stderr or r.stdout}')
    out = {}
    for f in json.loads(r.stdout):
        out[f['address'].lower().replace('0x', '')] = f['name']
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', required=True)
    ap.add_argument('--include-dir', default=HOSHI_INCLUDE,
                    help='hoshi include dir (default: externals/hoshi/include)')
    ap.add_argument('--dry-run', action='store_true',
                    help='print what would be applied without calling ghidra')
    args = ap.parse_args()

    protos = extract(args.include_dir)
    print(f'extracted {len(protos)} documented prototypes from {args.include_dir}')
    gmap = ghidra_func_map(args.project)

    plan = []  # (addr, final_sig, note)
    skipped = []
    for addr, name, sig, _loc in protos:
        a = addr.replace('0x', '')
        if a not in gmap:
            skipped.append((addr, name, 'no function at address'))
            continue
        gn = gmap[a]
        if gn == name or is_unnamed(gn):
            final = sig
            note = 'match' if gn == name else f'named ({gn} -> {name})'
        else:
            final = re.sub(r'\b' + re.escape(name) + r'\s*\(', gn + '(', sig, count=1)
            note = f'kept ghidra name ({gn})'
        plan.append((addr, fix_syntax(final), note))

    # global getter return-type overrides (propagate to callers)
    getters = []
    for gname, rtype in RETURN_TYPE_OVERRIDES.items():
        getters.append((gname, rtype))

    if args.dry_run:
        for gname, rtype in getters:
            print(f'[getter] {gname} -> return {rtype}')
        for addr, sig, note in plan:
            print(f'[{note:22}] {addr}  {sig}')
        print(f'\nwould apply {len(getters)} getter return-types + {len(plan)} signatures; '
              f'{len(skipped)} skipped')
        return

    ok = fail = 0
    failures = []
    for gname, rtype in getters:
        r = ghidra(['function', 'set-return-type', gname, '--type', rtype], args.project)
        if r.returncode == 0 and 'return_type_set' in r.stdout:
            ok += 1
        else:
            fail += 1
            failures.append((gname, rtype, (r.stderr or r.stdout).strip()))
    for addr, sig, _note in plan:
        r = ghidra(['function', 'set-signature', addr, '--signature', sig], args.project)
        if r.returncode == 0 and re.search(r'"status"\s*:\s*"(signature_set|updated)"', r.stdout):
            ok += 1
        else:
            fail += 1
            failures.append((addr, sig, (r.stderr or r.stdout).strip()))

    print(f'\napplied ok={ok} fail={fail} skipped={len(skipped)}')
    for tgt, sig, err in failures:
        print(f'  FAIL {tgt}  {sig}\n       {err}')
    if not failures:
        print('\nAll applied. Persist with:  ghidra analyze --project ' + args.project)


if __name__ == '__main__':
    main()
