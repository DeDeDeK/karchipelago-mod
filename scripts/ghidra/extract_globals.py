#!/usr/bin/env python3
"""Extract documented fixed-address global-data declarations from hoshi headers.

hoshi pins engine globals to their runtime addresses several ways; each names an
object living at a literal address, so it can be typed in Ghidra (phase 3, see
ApplyGlobals.java) to fold `*(int*)(ADDR + off)` blobs into `name.field_off`.

Forms handled:

  1. static pointer, literal address
       static PlayerData *stc_playerdata = (PlayerData *)0x8055a9f0;
  2. cast macro (optionally dereffed)
       #define stc_actor_data_table  ((int *)0x804b22b4)
       #define stc_enemy_param_table (*(void **)0x805dd878)
  3. static pointer, r13/r2-relative base+offset address
       static CityItemMgr **stc_city_item_mgr = (CityItemMgr **)(0x805dd0e0 + 0x7EC);

The declared/cast type is a *pointer to* the object; the object that lives at the
address is that type with one `*` removed. `PlayerData *` -> a `PlayerData` at the
address; `CityItemMgr **` -> a `CityItemMgr *` (a pointer slot) at the address.

Two exotic declarators (a pointer-to-array event table and a function-pointer
event table) can't be parsed by a cast regex; they're pinned in SPECIALS.

Usable as a module (`from extract_globals import extract`) and as a CLI that
prints the Ghidra data file consumed by ApplyGlobals.java, one entry per line:
addr<TAB>base<TAB>pointee_stars<TAB>array_count<TAB>name.
"""
import os
import re
import sys

# Names whose object at the address is really an array of the base type (the
# decl only encodes a single element). Keyed by declared name. Kept minimal and
# curated -- guessing counts from prose comments risks overrunning into an
# adjacent global. Add entries here when an indexed global is worth resolving.
ARRAY_SIZES = {
    'stc_playerdata': 5,   # PlayerData[5] (slots 0-4); the headline indexed global
}

# Exotic declarators a cast regex can't parse (pointer-to-array / function-pointer
# table), mirrored from event.h. Tuple: (addr, base, pointee_stars, count, name).
# EVKIND_NUM == 16 (EVKIND_DYNABLADE..EVKIND_FAKEPOWERUPS).
SPECIALS = [
    # static EventFunction (*stc_event_function)[EVKIND_NUM] = (void *)0x804a5410;
    ('0x804a5410', 'EventFunction', 0, 16, 'stc_event_function'),
    # static void (**stc_event_state_table)(EventCheckData *) = (...)0x804a5604;
    # 16 function pointers; typed as void*[16] (a named, sized pointer table).
    ('0x804a5604', 'void', 1, 16, 'stc_event_state_table'),
]

# an address is a literal, or an SDA base+offset sum: (0xBASE + 0xOFF | DEC)
_ADDR = r'(?:0x[0-9a-fA-F]+|\(\s*0x[0-9a-fA-F]+\s*\+\s*(?:0x[0-9a-fA-F]+|\d+)\s*\))'

# form 1 & 3: static <base> ***NAME = ( <cast> *** ) <addr> ;
STATIC_RE = re.compile(
    r'^\s*static\s+'
    r'(?P<decl>[A-Za-z_][\w\s]*?)\s*'
    r'(?P<stars>\*+)\s*'
    r'(?P<name>[A-Za-z_]\w*)\s*=\s*'
    r'\(\s*[^)]*\*+\s*\)\s*'          # a pointer cast (no nested parens)
    r'(?P<addr>' + _ADDR + r')\s*;'
)

# form 2: #define NAME (optional deref) ( <base> *** ) <addr>
DEFINE_RE = re.compile(
    r'^\s*#\s*define\s+(?P<name>\w+)\s+'
    r'\(?\s*\*?\s*\(\s*'
    r'(?P<decl>[A-Za-z_][\w\s]*?)\s*(?P<stars>\*+)\s*\)\s*'
    r'(?P<addr>' + _ADDR + r')'
)

_QUALS = re.compile(r'\b(?:volatile|const)\b')
_TAG = re.compile(r'^\s*(?:struct|union|enum)\s+')


def _norm_base(decl):
    return ' '.join(_TAG.sub('', _QUALS.sub('', decl)).split())


def _eval_addr(s):
    s = s.strip()
    if s.startswith('('):
        a, b = s[1:-1].split('+')
        return int(a.strip(), 0) + int(b.strip(), 0)
    return int(s, 0)


def _rows_from_line(line):
    """Yield (addr_int, base, pointee_stars, name) for any global decl on `line`."""
    for rx in (STATIC_RE, DEFINE_RE):
        m = rx.match(line)
        if not m:
            continue
        base = _norm_base(m.group('decl'))
        pointee_stars = len(m.group('stars')) - 1
        # `void` object with no indirection is a code/vtable address, not data.
        if base == 'void' and pointee_stars == 0:
            return
        yield (_eval_addr(m.group('addr')), base, pointee_stars, m.group('name'))
        return


def extract(root):
    """Walk `root` for .h files and return a list of
    (addr, base, pointee_stars, array_count, name) tuples, first address wins.
    `addr` is a lowercase 0x string; `base` is the bare base-type name."""
    seen = set()
    rows = []

    def add(addr_int, base, stars, name):
        if addr_int in seen:
            return
        seen.add(addr_int)
        count = ARRAY_SIZES.get(name, 1)
        rows.append((f'0x{addr_int:08x}', base, stars, count, name))

    for dirpath, _dirs, files in os.walk(root):
        for fn in sorted(files):
            if not fn.endswith('.h'):
                continue
            with open(os.path.join(dirpath, fn), errors='replace') as f:
                for line in f:
                    for addr_int, base, stars, name in _rows_from_line(line):
                        add(addr_int, base, stars, name)

    for addr, base, stars, count, name in SPECIALS:
        ai = int(addr, 0)
        if ai not in seen:
            seen.add(ai)
            rows.append((f'0x{ai:08x}', base, stars, count, name))

    rows.sort(key=lambda r: int(r[0], 16))
    return rows


def main():
    if len(sys.argv) != 2:
        sys.exit('usage: extract_globals.py <hoshi_include_dir>')
    rows = extract(sys.argv[1])
    for addr, base, stars, count, name in rows:
        print(f'{addr}\t{base}\t{stars}\t{count}\t{name}')
    print(f'# extracted {len(rows)} global declarations', file=sys.stderr)


if __name__ == '__main__':
    main()
