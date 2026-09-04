#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check BackdropManifest.dat by replaying what the mod does at runtime.

Rebuilds each backdrop's payload the way custom_weather does - read the manifest's
ranges out of the donor file, add the buffer base to every listed pointer - and then
holds the result to the same standard a shipped asset was held to:

  * every range is 32-byte aligned in offset, length and destination, and lands
    inside the donor file
  * every relocated pointer lands inside the payload
  * walking the rebuilt tree reaches the same object graph, type for type and size
    for size, as walking the donor's own subtree

The last one is the real check: it proves the reconstruction is the donor subtree and
not merely a well-formed buffer.

Run from the repo root:
    uv run python scripts/authoring/verify_backdrop_manifest.py
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsd.archive import ARCHIVE_VERSION, Archive, cstr, u32
from hsd.walker import Walker

from authoring.make_backdrop_manifest import (
    ALIGN,
    ENTRY_SIZE,
    MANIFEST_MAGIC,
    OUTPUT,
    grmodel_symbol,
    skybox_slot,
)

BASE = 0x80800000  # stand-in for the runtime allocation


def rebuild(entry, data):
    """Replay the runtime: read ranges out of the donor, relocate, return the payload."""
    key = cstr(data, u32(data, entry + 0x00))
    donor = cstr(data, u32(data, entry + 0x04))
    size = u32(data, entry + 0x08)
    (scale,) = struct.unpack_from(">f", data, entry + 0x0C)
    root = u32(data, entry + 0x10)
    rng_off, rng_num = u32(data, entry + 0x14), u32(data, entry + 0x18)
    rel_off, rel_num = u32(data, entry + 0x1C), u32(data, entry + 0x20)

    errs = []
    buf = bytearray(size)
    written = []
    raw = open(os.path.join("iso/files", donor), "rb").read()

    for i in range(rng_num):
        o = rng_off + i * 12
        d_off, dst, length = u32(data, o), u32(data, o + 4), u32(data, o + 8)
        if d_off % ALIGN or dst % ALIGN or length % ALIGN:
            errs.append(
                f"range {i} not {ALIGN}-aligned: {d_off:#x}/{dst:#x}/{length:#x}"
            )
        if d_off + length > len(raw):
            errs.append(
                f"range {i} runs past {donor} ({d_off + length:#x} > {len(raw):#x})"
            )
            continue
        if dst + length > size:
            errs.append(f"range {i} runs past payload ({dst + length:#x} > {size:#x})")
            continue
        buf[dst : dst + length] = raw[d_off : d_off + length]

    for i in range(rel_num):
        o = rel_off + i * 8
        dst, val = u32(data, o), u32(data, o + 4)
        if dst + 4 > size or val >= size:
            errs.append(f"reloc {i} out of payload: {dst:#x} -> {val:#x}")
            continue
        struct.pack_into(">I", buf, dst, BASE + val)
        written.append(dst)

    struct.pack_into(">I", buf, 0, BASE + root)
    written.append(0)
    return key, donor, scale, root, buf, written, errs


class Payload:
    """Archive-shaped view of a rebuilt payload, so Walker can run over it.

    `relocs` is exactly the set the manifest relocated. Walker only follows a field
    that appears there, so a pointer the manifest failed to list goes unfollowed and
    the graph comparison catches it."""

    def __init__(self, buf, relocs):
        self.data = bytes(buf)
        self.relocs = sorted(relocs)
        self.reloc_set = set(relocs)
        self.publics = {}
        self.version = ARCHIVE_VERSION


def main():
    arc = Archive(OUTPUT)
    data = arc.data
    if u32(data, 0) != MANIFEST_MAGIC:
        print(f"bad magic {u32(data, 0):#x}")
        return 1
    n = u32(data, 8)
    entries = u32(data, 0x0C)
    print(f"{OUTPUT}: {n} entries, version {u32(data, 4)}\n")

    failed = 0
    for i in range(n):
        entry = entries + i * ENTRY_SIZE
        key, donor, scale, root, buf, written, errs = rebuild(entry, data)

        # The donor's own subtree, walked in donor coordinates.
        d_arc = Archive(os.path.join("iso/files", donor))
        sym = grmodel_symbol(d_arc)
        d_root = u32(d_arc.data, skybox_slot(d_arc, sym))
        want = Walker(d_arc).walk(d_root, "JOBJDesc")

        # The rebuilt payload, walked in payload coordinates. The runtime leaves
        # absolute addresses behind, so undo exactly the words the relocation pass
        # wrote - sweeping for in-range values instead would also catch float
        # constants that happen to land in the window.
        shifted = bytearray(buf)
        for j in written:
            struct.pack_into(">I", shifted, j, u32(shifted, j) - BASE)
        pay = Payload(shifted, written)
        try:
            got = Walker(pay).walk(root, "JOBJDesc")
        except Exception as e:
            errs.append(f"walk failed: {e}")
            got = {}

        want_types = sorted((t, sz) for _, (t, sz) in want.items())
        got_types = sorted((t, sz) for _, (t, sz) in got.items())
        if want_types != got_types:
            errs.append(f"graph differs: donor {len(want)} objects, rebuilt {len(got)}")

        status = "OK" if not errs else "FAIL"
        print(
            f"  {key:12s} {donor:24s} {len(buf) / 1024:8.1f} KB  "
            f"x{scale:.4f}  {len(got):5d} objects  {status}"
        )
        for e in errs:
            print(f"       {e}")
        if errs:
            failed += 1

    print(f"\n{n - failed}/{n} verified")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
