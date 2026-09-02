# SPDX-License-Identifier: GPL-3.0-only
"""In-place editing of an HSD archive's object graph.

`Builder` holds a mutable copy of a parsed archive's data section with a bump
allocator on the end and the relocation set alongside it, so reading a pointer
field, repointing it, appending a record or duplicating one all stay consistent
without rebuilding the archive. The walkers on top of it are the traversals an
edit needs: joint and material-animation preorder (the order LOD tables and
animation trees index by), and the singly-linked chains hanging off a joint.

Emit the result with `archive.build_archive(b.data, sorted(b.relocs), publics)`.
"""

import struct

from .archive import u32


class Builder:
    """A mutable copy of an archive's data section with a bump allocator on
    the end, tracking the relocation set as pointers are written."""

    def __init__(self, src):
        """`src` is anything carrying a data section and its relocation
        offsets - a parsed `Archive` or a `CarveResult`."""
        self.data = bytearray(src.data)
        self.relocs = set(src.relocs)

    def get_u32(self, off):
        return u32(self.data, off)

    def set_u32(self, off, value):
        struct.pack_into(">I", self.data, off, value & 0xFFFFFFFF)

    def set_f32(self, off, value):
        struct.pack_into(">f", self.data, off, value)

    def ptr(self, off):
        return u32(self.data, off) if off in self.relocs else 0

    def set_ptr(self, off, target):
        """Point `off` at `target`, or NULL it with `target=None`. NULL is the
        absence of a relocation, not a zero value, so offset 0 is a legal
        target and only `None` clears the slot."""
        self.set_u32(off, target or 0)
        if target is None:
            self.relocs.discard(off)
        else:
            self.relocs.add(off)

    def alloc(self, size, align=4):
        self.data.extend(b"\0" * (-len(self.data) % align))
        off = len(self.data)
        self.data.extend(b"\0" * size)
        return off

    def blob(self, payload, align=4):
        off = self.alloc(len(payload), align)
        self.data[off : off + len(payload)] = payload
        return off

    def copy(self, src, size, align=4):
        """Duplicate `size` bytes, carrying every relocation inside them. The
        copied pointers still name their originals, so the duplicate shares
        whatever the source pointed at."""
        off = self.alloc(size, align)
        self.data[off : off + size] = self.data[src : src + size]
        for r in range(src, src + size, 4):
            if r in self.relocs:
                self.relocs.add(off + r - src)
        return off


def walk_joints(b, root):
    """(offset, parent index) per joint, in the preorder the LOD tables and
    the animation trees index by."""
    out = []

    def rec(off, parent):
        i = len(out)
        out.append((off, parent))
        child, nxt = b.ptr(off + 0x08), b.ptr(off + 0x0C)
        if child:
            rec(child, i)
        if nxt:
            rec(nxt, parent)

    rec(root, -1)
    return out


def walk_matanim(b, root):
    """The same preorder over an HSD_MatAnimJoint tree."""
    out = []

    def rec(off, parent):
        i = len(out)
        out.append((off, parent))
        child, nxt = b.ptr(off + 0x00), b.ptr(off + 0x04)
        if child:
            rec(child, i)
        if nxt:
            rec(nxt, parent)

    rec(root, -1)
    return out


def chain(b, head, next_off):
    out = []
    cur = head
    while cur:
        out.append(cur)
        cur = b.ptr(cur + next_off)
    return out


def dobjs_of(b, jobj):
    head = b.ptr(jobj + 0x10)
    return chain(b, head, 0x04) if head else []


def flat_dobjs(b, joints):
    out = []
    for off, _ in joints:
        out.extend(dobjs_of(b, off))
    return out


def set_vec3(b, off, values):
    struct.pack_into(">3f", b.data, off, *values)


def set_diffuse(b, material, color):
    b.data[material + 0x04 : material + 0x07] = bytes(
        min(255, int(round(c * 255))) for c in color
    )
