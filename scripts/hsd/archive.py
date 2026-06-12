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


def u32(b, o): return struct.unpack(">I", b[o:o+4])[0]
def u16(b, o): return struct.unpack(">H", b[o:o+2])[0]
def cstr(b, o):
    end = b.find(b'\0', o)
    return b[o:end].decode('ascii', errors='replace') if end >= 0 else ''


class NotAnHSDArchive(ValueError):
    """Raised when a file's header doesn't look like a real HSD archive."""


class Archive:
    def __init__(self, path):
        with open(path, 'rb') as f:
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
        expected_tail = (HSD_HEADER + self.data_size
                         + self.nb_reloc * 4
                         + self.nb_public * 8
                         + self.nb_extern * 8)
        if self.file_size == 0 or self.file_size != disk_size or expected_tail > disk_size:
            raise NotAnHSDArchive(
                f"{path}: header is not a valid HSD archive "
                f"(file_size={self.file_size:#x}, on-disk={disk_size:#x}, "
                f"data_size={self.data_size:#x}, nb_reloc={self.nb_reloc}, "
                f"nb_public={self.nb_public}, nb_extern={self.nb_extern})")

        self.data = self.blob[HSD_HEADER:HSD_HEADER + self.data_size]

        rel_off = HSD_HEADER + self.data_size
        self.relocs = [u32(self.blob, rel_off + i*4) for i in range(self.nb_reloc)]
        self.reloc_set = set(self.relocs)

        pub_off = rel_off + self.nb_reloc * 4
        ext_off = pub_off + self.nb_public * 8
        str_off = ext_off + self.nb_extern * 8

        self.publics = OrderedDict()
        for i in range(self.nb_public):
            doff = u32(self.blob, pub_off + i*8)
            noff = u32(self.blob, pub_off + i*8 + 4)
            self.publics[cstr(self.blob, str_off + noff)] = doff

        self.externs = []
        for i in range(self.nb_extern):
            doff = u32(self.blob, ext_off + i*8)
            noff = u32(self.blob, ext_off + i*8 + 4)
            self.externs.append((doff, cstr(self.blob, str_off + noff)))

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
