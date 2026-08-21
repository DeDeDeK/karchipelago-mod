"""Read the hoshi headers.

Three pure extractors over `externals/hoshi/include`:

    strip_bodies(src, dst)   mirror the tree with function bodies removed, so
                             Ghidra's C parser sees only declarations
    extract_protos(root)     prototypes carrying a `// 0xADDR` comment
    extract_globals(root)    data declarations pinned to a literal address
"""

import os
import re

# Names whose object at the address is really an array of the base type (the
# decl only encodes a single element). Guessing counts from prose comments risks
# overrunning into an adjacent global, so this stays curated.
ARRAY_SIZES = {
    "stc_playerdata": 5,      # PlayerData[5] (slots 0-4); the headline indexed global
    "psGeneratorCount": 64,   # one per particle bank; Ptcl_Alloc checks bank < 0x40
    "psGeneratorDesc": 64,
}

# Exotic declarators a cast regex can't parse: a pointer-to-array and a
# function-pointer table, both sized by EVKIND_NUM == 16.
SPECIALS = [
    # static EventFunction (*stc_event_function)[EVKIND_NUM] = (void *)0x804a5410;
    ("0x804a5410", "EventFunction", 0, 16, "stc_event_function"),
    # static void (**stc_event_state_table)(EventCheckData *) = (...)0x804a5604;
    # 16 function pointers, typed as void*[16] (a named, sized pointer table).
    ("0x804a5604", "void", 1, 16, "stc_event_state_table"),
]


def iter_headers(root):
    """Yield (abs_path, rel_path) for every .h file under `root`, sorted."""
    for dirpath, _dirs, files in os.walk(root):
        for fn in sorted(files):
            if fn.endswith(".h"):
                path = os.path.join(dirpath, fn)
                yield path, os.path.relpath(path, root)


def _read(path):
    with open(path, errors="replace") as f:
        return f.read()


def strip_bodies(src_root, dst_root):
    """Mirror `src_root` into `dst_root` with function bodies and file-scope
    initializers replaced by `;`. Returns the number of headers written."""
    count = 0
    for dirpath, _dirs, files in os.walk(src_root):
        rel = os.path.relpath(dirpath, src_root)
        outdir = dst_root if rel == "." else os.path.join(dst_root, rel)
        os.makedirs(outdir, exist_ok=True)
        for fn in files:
            if not fn.endswith(".h"):
                continue
            text = _read(os.path.join(dirpath, fn))
            with open(os.path.join(outdir, fn), "w") as f:
                f.write(strip_text(text))
            count += 1
    return count


def strip_text(text):
    """Drop function bodies and file-scope initializers from C source text.
    Preprocessor directives, comments, and record bodies survive verbatim.

    Discriminator for a `{` at brace-depth 0, by preceding significant char:
      `)` -> function body      -> replace `{...}` with `;`
      `=` -> initializer        -> drop `= {...}`
      else (struct/union/enum)  -> record body -> keep
    """
    out = []
    i = 0
    n = len(text)
    depth = 0
    last_sig = ""     # last significant char emitted, for the `{` discriminator
    line_start = True

    while i < n:
        c = text[i]

        if line_start:
            j = i
            while j < n and text[j] in " \t":
                j += 1
            if j < n and text[j] == "#":
                k = i
                while k < n:
                    if text[k] == "\\" and k + 1 < n and text[k + 1] == "\n":
                        k += 2
                        continue
                    if text[k] == "\n":
                        break
                    k += 1
                out.append(text[i:k])
                i = k
                continue
        if c == "\n":
            out.append(c)
            i += 1
            line_start = True
            continue
        if c in " \t":
            out.append(c)
            i += 1
            continue
        line_start = False

        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j == -1 else j
            out.append(text[i:j])
            i = j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j == -1 else j + 2
            out.append(text[i:j])
            i = j
            continue

        if c in "\"'":
            j = _skip_literal(text, i, n)
            out.append(text[i:j])
            last_sig = c
            i = j
            continue

        if c == "{":
            if depth == 0 and last_sig == ")":
                i = _skip_block(text, i, n)
                out.append(";")
                last_sig = ";"
                continue
            if depth == 0 and last_sig == "=":
                _pop_equals(out)
                i = _skip_block(text, i, n)
                last_sig = ""  # the declaration now ends at the following ';'
                continue
            depth += 1
        elif c == "}" and depth > 0:
            depth -= 1

        out.append(c)
        last_sig = c
        i += 1

    return "".join(out)


def _skip_literal(text, i, n):
    """i points at a quote. Return the index just past the closing quote."""
    quote = text[i]
    i += 1
    while i < n:
        if text[i] == "\\":
            i += 2
            continue
        if text[i] == quote:
            return i + 1
        i += 1
    return i


def _skip_block(text, i, n):
    """i points at `{`. Return the index just past the matching `}`, respecting
    nested braces, string/char literals, and comments."""
    depth = 0
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            i = n if j == -1 else j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            i = n if j == -1 else j + 2
            continue
        if c in "\"'":
            i = _skip_literal(text, i, n)
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return i


def _pop_equals(out):
    """Remove the trailing `=` (and whitespace) already emitted before an
    initializer block."""
    s = "".join(out).rstrip()
    out.clear()
    out.append(s[:-1].rstrip() if s.endswith("=") else s)


# RET ... NAME ( PARAMS ) ; // 0xADDR  -- PARAMS may span lines.
_PROTO_RE = re.compile(
    r"^[ \t]*(?P<sig>[A-Za-z_][^;{}\n]*?\b(?P<name>[A-Za-z_]\w*)[ \t]*"
    r"\((?P<params>[^;{}]*?)\))[ \t]*;[ \t]*//[ \t]*(?P<addr>0x[0-9a-fA-F]{6,8})",
    re.MULTILINE,
)

_NON_FUNC = {"if", "while", "for", "switch", "sizeof", "return", "else"}


def extract_protos(root):
    """Return [(addr, name, signature, 'relpath:lineno')] for every prototype
    annotated with a `// 0xADDR` comment. First address wins.

    Those comments are the authoritative map from a hoshi name + signature to a
    runtime address. Function definitions end in `{`, so `static inline` bodies
    are naturally excluded.
    """
    seen = set()
    rows = []
    for path, rel in iter_headers(root):
        text = _read(path)
        for m in _PROTO_RE.finditer(text):
            name = m.group("name")
            if name in _NON_FUNC:
                continue
            if "//" in m.group("params"):
                continue  # a comment inside the params means we over-matched
            addr = m.group("addr").lower()
            if addr in seen:
                continue
            seen.add(addr)
            lineno = text.count("\n", 0, m.start()) + 1
            rows.append((addr, name, " ".join(m.group("sig").split()),
                         f"{rel}:{lineno}"))
    return rows


# A literal address, or an SDA base+offset sum: (0xBASE + 0xOFF | DEC)
_ADDR = (r"(?:0x[0-9a-fA-F]+"
         r"|\(\s*0x[0-9a-fA-F]+\s*\+\s*(?:0x[0-9a-fA-F]+|\d+)\s*\))")

# static PlayerData *stc_playerdata = (PlayerData *)0x8055a9f0;
# static CityItemMgr **stc_city_item_mgr = (CityItemMgr **)(0x805dd0e0 + 0x7EC);
_STATIC_RE = re.compile(
    r"^\s*static\s+"
    r"(?P<decl>[A-Za-z_][\w\s]*?)\s*"
    r"(?P<stars>\*+)\s*(?:const\s+)?"
    r"(?P<name>[A-Za-z_]\w*)\s*=\s*"
    r"\(\s*[^)]*\*+\s*\)\s*"           # a pointer cast (no nested parens)
    r"(?P<addr>" + _ADDR + r")\s*;"
)

# #define stc_actor_data_table  ((int *)0x804b22b4)
# #define stc_enemy_param_table (*(void **)0x805dd878)
_DEFINE_RE = re.compile(
    r"^\s*#\s*define\s+(?P<name>\w+)\s+"
    r"\(?\s*\*?\s*\(\s*"
    r"(?P<decl>[A-Za-z_][\w\s]*?)\s*(?P<stars>\*+)\s*\)\s*"
    r"(?P<addr>" + _ADDR + r")"
)

_QUALS = re.compile(r"\b(?:volatile|const)\b")
_TAG = re.compile(r"^\s*(?:struct|union|enum)\s+")


def extract_globals(root):
    """Return [(addr, base, pointee_stars, array_count, name)] for every global
    hoshi pins to a literal address. First address wins; sorted by address.

    The declared type is a *pointer to* the object, so the object living at the
    address is that type with one `*` removed: `PlayerData *` -> a `PlayerData`,
    `CityItemMgr **` -> a `CityItemMgr *` (a pointer slot).
    """
    seen = set()
    rows = []

    def add(addr, base, stars, name):
        if addr in seen:
            return
        seen.add(addr)
        rows.append((f"0x{addr:08x}", base, stars,
                     ARRAY_SIZES.get(name, 1), name))

    for path, _rel in iter_headers(root):
        for line in _read(path).splitlines():
            parsed = _parse_global(line)
            if parsed:
                add(*parsed)

    for addr, base, stars, count, name in SPECIALS:
        value = int(addr, 0)
        if value not in seen:
            seen.add(value)
            rows.append((f"0x{value:08x}", base, stars, count, name))

    rows.sort(key=lambda r: int(r[0], 16))
    return rows


def _parse_global(line):
    """(addr_int, base, pointee_stars, name) for a global decl on `line`."""
    for rx in (_STATIC_RE, _DEFINE_RE):
        m = rx.match(line)
        if not m:
            continue
        base = " ".join(_TAG.sub("", _QUALS.sub("", m.group("decl"))).split())
        stars = len(m.group("stars")) - 1
        if base == "void" and stars == 0:
            return None  # a code/vtable address, not data
        return _eval_addr(m.group("addr")), base, stars, m.group("name")
    return None


def _eval_addr(s):
    s = s.strip()
    if s.startswith("("):
        lhs, rhs = s[1:-1].split("+")
        return int(lhs.strip(), 0) + int(rhs.strip(), 0)
    return int(s, 0)
