# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""HSD archive (.dat) parser.

File layout (all big-endian):
    +0x00  u32 file_size
    +0x04  u32 data_size
    +0x08  u32 nb_reloc
    +0x0C  u32 nb_public
    +0x10  u32 nb_extern
    +0x14  char[4] version
    +0x18  reserved (8 bytes)
    +0x20  data[data_size]
    ...    reloc table   (nb_reloc * u32 source offsets within data)
    ...    public table  (nb_public * {u32 data_off, u32 name_off})
    ...    extern table  (nb_extern * {u32 data_off, u32 name_off})
    ...    string table  (null-terminated names referenced by the tables above)

Pointers stored in `data` are file-offsets-relative-to-data-start. They
appear in `relocs`; entries not in the reloc set are literal integers.
"""

import struct
from collections import OrderedDict

HSD_HEADER = 0x20


def u32(b, o):
    return struct.unpack(">I", b[o : o + 4])[0]


def s32(b, o):
    return struct.unpack(">i", b[o : o + 4])[0]


def u16(b, o):
    return struct.unpack(">H", b[o : o + 2])[0]


def s16(b, o):
    return struct.unpack(">h", b[o : o + 2])[0]


def f32(b, o):
    return struct.unpack(">f", b[o : o + 4])[0]


def cstr(b, o):
    end = b.find(b"\0", o)
    return b[o:end].decode("ascii", errors="replace") if end >= 0 else ""


ARCHIVE_VERSION = b"001B"  # the tag every retail KAR archive carries


class NotAnHSDArchive(ValueError):
    """Raised when a file's header doesn't look like a real HSD archive."""


class Archive:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.blob = f.read()
        self.path = path
        self.file_size = u32(self.blob, 0)
        self.data_size = u32(self.blob, 4)
        self.nb_reloc = u32(self.blob, 8)
        self.nb_public = u32(self.blob, 0xC)
        self.nb_extern = u32(self.blob, 0x10)
        self.version = self.blob[0x14:0x18]

        # Sanity-check the header. Several files under iso/files/ (A2Item.dat,
        # menu/audio packs, etc.) use formats that aren't HSDRawFile and would
        # otherwise silently produce garbage publics/externs.
        disk_size = len(self.blob)
        rel_off = HSD_HEADER + self.data_size
        pub_off = rel_off + self.nb_reloc * 4
        ext_off = pub_off + self.nb_public * 8
        str_off = ext_off + self.nb_extern * 8
        if self.file_size == 0 or self.file_size != disk_size or str_off > disk_size:
            raise NotAnHSDArchive(
                f"{path}: header is not a valid HSD archive "
                f"(file_size={self.file_size:#x}, on-disk={disk_size:#x}, "
                f"data_size={self.data_size:#x}, nb_reloc={self.nb_reloc}, "
                f"nb_public={self.nb_public}, nb_extern={self.nb_extern})"
            )

        self.data = self.blob[HSD_HEADER : HSD_HEADER + self.data_size]
        self.relocs = [u32(self.blob, rel_off + i * 4) for i in range(self.nb_reloc)]
        self.reloc_set = set(self.relocs)

        # Symbol-table entries must resolve inside the data and string
        # sections. A file whose header happens to be self-consistent but
        # isn't HSDRawFile (A2Window_new.dat) fails here rather than
        # yielding thousands of binary-garbage symbol names.
        str_size = disk_size - str_off

        def sym_at(entry_off):
            doff = u32(self.blob, entry_off)
            noff = u32(self.blob, entry_off + 4)
            if doff >= self.data_size or noff >= str_size:
                raise NotAnHSDArchive(
                    f"{path}: symbol entry at {entry_off:#x} is out of range "
                    f"(data_off={doff:#x}/{self.data_size:#x}, "
                    f"name_off={noff:#x}/{str_size:#x})"
                )
            return doff, cstr(self.blob, str_off + noff)

        self.publics = OrderedDict()
        for i in range(self.nb_public):
            doff, name = sym_at(pub_off + i * 8)
            self.publics[name] = doff

        self.externs = []
        for i in range(self.nb_extern):
            doff, name = sym_at(ext_off + i * 8)
            self.externs.append((doff, name))

    def ptr(self, off):
        """The pointer stored at data[off], or 0 when the slot carries no
        relocation. Matches `builder.Builder.ptr`, so the graph traversals in
        `builder` run over a read-only archive as well."""
        return u32(self.data, off) if off in self.reloc_set else 0

    def deref(self, off):
        """Read the pointer-as-offset stored at data[off]. Returns 0 if the
        slot isn't a reloc and its raw value is 0 (legal NULL), or None
        if it's a non-reloc non-zero value (suspicious)."""
        if off not in self.reloc_set:
            v = u32(self.data, off)
            return 0 if v == 0 else None
        return u32(self.data, off)

    def name_at(self, off):
        """Return the public/extern symbol name at `off`, or None."""
        for name, o in self.publics.items():
            if o == off:
                return name
        for o, name in self.externs:
            if o == off:
                return name
        return None


def build_archive(data, relocs, publics, version=ARCHIVE_VERSION, externs=()):
    """Serialize an HSD archive (the inverse of `Archive`).

    data:    the data section (bytes/bytearray).
    relocs:  iterable of u32 source offsets within data (sorted here).
    publics: iterable of (name, data_off).
    externs: iterable of (name, data_off).
    version: 4-byte version tag; pass an existing `arc.version` when rewriting
             a donor, and leave it alone when authoring a fresh archive.

    Names are interned into a shared string table (identical names share
    one slot). Returns the complete archive as bytes.
    """
    relocs = sorted(relocs)
    strings = bytearray()
    str_off = {}

    def intern(name):
        if name not in str_off:
            str_off[name] = len(strings)
            strings.extend(name.encode("ascii") + b"\0")
        return str_off[name]

    pub_entries = [(doff, intern(name)) for name, doff in publics]
    ext_entries = [(doff, intern(name)) for name, doff in externs]

    data = bytes(data)
    reloc_bytes = b"".join(struct.pack(">I", r) for r in relocs)
    pub_bytes = b"".join(struct.pack(">II", d, n) for d, n in pub_entries)
    ext_bytes = b"".join(struct.pack(">II", d, n) for d, n in ext_entries)
    file_size = (
        HSD_HEADER
        + len(data)
        + len(reloc_bytes)
        + len(pub_bytes)
        + len(ext_bytes)
        + len(strings)
    )
    header = struct.pack(
        ">IIIII4s8x",
        file_size,
        len(data),
        len(relocs),
        len(pub_entries),
        len(ext_entries),
        version,
    )
    return header + data + reloc_bytes + pub_bytes + ext_bytes + bytes(strings)


class Blob:
    """A data section under construction, with its relocation list."""

    def __init__(self):
        self.data = bytearray()
        self.relocs = []
        self.interned = {}
        self.pointed = set()

    def append(self, payload, alignment=4):
        self.data.extend(b"\0" * ((-len(self.data)) & (alignment - 1)))
        off = len(self.data)
        self.data.extend(payload)
        return off

    def intern(self, payload, alignment=4):
        """Append `payload` once; identical payloads share one offset."""
        key = (bytes(payload), alignment)
        if key not in self.interned:
            self.interned[key] = self.append(payload, alignment)
        return self.interned[key]

    def set_u32(self, off, value):
        struct.pack_into(">I", self.data, off, value & 0xFFFFFFFF)

    def set_f32(self, off, value):
        struct.pack_into(">f", self.data, off, value)

    def ptr(self, at, target):
        """Write a pointer at `at` and register it for relocation.

        Interned structures are pointed at once per bank that shares them, and a
        slot relocated twice is relocated twice by the engine, so each is
        registered only the first time."""
        struct.pack_into(">I", self.data, at, target)
        if at not in self.pointed:
            self.pointed.add(at)
            self.relocs.append(at)
