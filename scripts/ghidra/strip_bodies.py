#!/usr/bin/env python3
"""Mirror a tree of C headers, replacing function bodies and file-scope
initializers with `;` so Ghidra's C-Source parser sees only type declarations
and prototypes. Preprocessor directives, comments, structs/unions/enums, and
prototypes are preserved verbatim.

Discriminator at brace-depth 0 for a `{`:
  - preceding significant char `)`  -> function body   -> replace `{...}` with `;`
  - preceding significant char `=`  -> initializer     -> drop `= {...}`
  - otherwise (struct/union/enum)   -> record body      -> keep
"""
import sys
import os


def strip(text):
    out = []
    i = 0
    n = len(text)
    depth = 0
    # last significant (non-space, non-comment) char emitted at depth 0 context
    last_sig = ''
    line_start = True  # at start of a logical line (for #directive detection)

    def peek_nonspace(j):
        while j < n and text[j] in ' \t':
            j += 1
        return j

    while i < n:
        c = text[i]

        # Preprocessor directive: copy the whole (possibly continued) line verbatim.
        if line_start:
            j = i
            while j < n and text[j] in ' \t':
                j += 1
            if j < n and text[j] == '#':
                # copy from i to end of logical line (handle backslash-newline)
                k = i
                while k < n:
                    if text[k] == '\\' and k + 1 < n and text[k + 1] == '\n':
                        k += 2
                        continue
                    if text[k] == '\n':
                        break
                    k += 1
                out.append(text[i:k])
                i = k
                line_start = True
                continue
        # not a directive at this position
        if c == '\n':
            out.append(c)
            i += 1
            line_start = True
            continue
        if c in ' \t':
            out.append(c)
            i += 1
            continue
        # From here we have a significant char; no longer at line start.
        line_start = False

        # Comments
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = i
            while j < n and text[j] != '\n':
                j += 1
            out.append(text[i:j])
            i = j
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = (j + 2) if j != -1 else n
            out.append(text[i:j])
            i = j
            continue

        # String / char literals: copy verbatim, they don't affect braces.
        if c == '"' or c == "'":
            q = c
            j = i + 1
            while j < n:
                if text[j] == '\\':
                    j += 2
                    continue
                if text[j] == q:
                    j += 1
                    break
                j += 1
            out.append(text[i:j])
            last_sig = q
            i = j
            continue

        if c == '{':
            if depth == 0 and last_sig == ')':
                # function body -> skip to matching brace, emit ';'
                i = skip_block(text, i, n)
                out.append(';')
                last_sig = ';'
                continue
            if depth == 0 and last_sig == '=':
                # initializer -> remove the emitted '=' (and trailing ws), skip block
                _pop_back_to_equals(out)
                i = skip_block(text, i, n)
                last_sig = ''  # declaration now ends at following ';'
                continue
            # record body (struct/union/enum) -> keep
            depth += 1
            out.append(c)
            last_sig = c
            i += 1
            continue
        if c == '}':
            if depth > 0:
                depth -= 1
            out.append(c)
            last_sig = c
            i += 1
            continue

        out.append(c)
        last_sig = c
        i += 1

    return ''.join(out)


def skip_block(text, i, n):
    """i points at '{'. Return index just past the matching '}', respecting
    nested braces, strings, chars, and comments."""
    depth = 0
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            i = n if j == -1 else j
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            i = n if j == -1 else j + 2
            continue
        if c == '"' or c == "'":
            q = c
            i += 1
            while i < n:
                if text[i] == '\\':
                    i += 2
                    continue
                if text[i] == q:
                    i += 1
                    break
                i += 1
            continue
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return i


def _pop_back_to_equals(out):
    # out is a list of emitted chunks; remove trailing whitespace then the '='.
    s = ''.join(out)
    out.clear()
    s = s.rstrip()
    if s.endswith('='):
        s = s[:-1].rstrip()
    out.append(s)


def main():
    src_root, dst_root = sys.argv[1], sys.argv[2]
    count = 0
    for dirpath, _dirs, files in os.walk(src_root):
        rel = os.path.relpath(dirpath, src_root)
        outdir = os.path.join(dst_root, rel) if rel != '.' else dst_root
        os.makedirs(outdir, exist_ok=True)
        for fn in files:
            if not fn.endswith('.h'):
                continue
            src = os.path.join(dirpath, fn)
            with open(src, 'r', errors='replace') as f:
                text = f.read()
            with open(os.path.join(outdir, fn), 'w') as f:
                f.write(strip(text))
            count += 1
    print(f"stripped {count} headers -> {dst_root}")


if __name__ == '__main__':
    main()
