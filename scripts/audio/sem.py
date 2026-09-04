"""airride.sem - the FGM script map - and adfgmnametable.dat, which names its scripts.

An FGM id is (bank << 16) | index_within_bank. `bank_start[bank] + index` is the
global script index; a script is a run of 4-byte commands, one opcode byte and a
24-bit operand. Opcode 0x01 selects the sample to play, by global sound index -
the running total of every earlier bank's .ssm sound count, which is also what
each .ssm header stores at 0x0c.

The file is five count-prefixed u32 tables back to back, which FGM_InitSEM
(0x80444208) splits into r13 globals. Tables 0, 1 and 4 are empty in
airride.sem. Entries in tables 1, 3 and 4 are file offsets, relocated in place
to pointers as the file is installed.

    u32 count0; u32 table0[count0]
    u32 count1; u32 table1[count1]
    u32 bank_count;  u32 bank_start[bank_count]
    u32 script_count; u32 script_offset[script_count]
    u32 count4; u32 table4[count4]
    the script bodies
"""

import os
import struct
import sys

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hsd.archive import Archive, cstr, u32

OP_SOUND = 0x01
OP_END = 0x0E

DEFAULT_SEM = "iso/files/audio/jp/airride.sem"
DEFAULT_NAMES = "iso/files/audio/adfgmnametable.dat"

BANK_STAR = 3


class SEM:
    def __init__(self, path=DEFAULT_SEM):
        with open(path, "rb") as f:
            b = f.read()
        self.blob = b
        off = 0
        tables = []
        for _ in range(5):
            n = struct.unpack_from(">I", b, off)[0]
            tables.append(list(struct.unpack_from(">%dI" % n, b, off + 4)))
            off += 4 + n * 4
        self.tables = tables
        self.bank_start = tables[2]
        self.bank_count = len(self.bank_start)
        ptrs = tables[3]
        self.script_end = off
        self.scripts = [
            b[ptrs[i] : (ptrs[i + 1] if i + 1 < len(ptrs) else len(b))]
            for i in range(len(ptrs))
        ]

    def script(self, fgm_id):
        bank, idx = (fgm_id >> 16) & 0xFFFF, fgm_id & 0xFFFF
        return self.scripts[self.bank_start[bank] + idx]

    @staticmethod
    def commands(script):
        return [
            (script[i], int.from_bytes(script[i + 1 : i + 4], "big"))
            for i in range(0, len(script) - 3, 4)
        ]

    def sounds(self, fgm_id):
        """Global sound indices a script plays."""
        return [arg for op, arg in self.commands(self.script(fgm_id)) if op == OP_SOUND]


class NameTable:
    """smSoundTestFGMGroupTable: per bank, its .ssm index and its script names."""

    def __init__(self, path=DEFAULT_NAMES):
        a = Archive(path)
        off = a.publics["smSoundTestFGMGroupTable"]
        self.banks = {}
        while u32(a.data, off) != 0xFFFFFFFF:
            arr, count = u32(a.data, off + 0x14), u32(a.data, off + 0x10)
            self.banks[u32(a.data, off)] = dict(
                name=cstr(a.data, u32(a.data, off + 0x0C)),
                ssm_index=u32(a.data, off + 8),
                names=[cstr(a.data, u32(a.data, arr + i * 4)) for i in range(count)],
            )
            off += 0x18

    def name(self, fgm_id):
        if fgm_id in (-1, 0xFFFFFFFF):
            return "-1"
        bank, idx = (fgm_id >> 16) & 0xFFFF, fgm_id & 0xFFFF
        b = self.banks.get(bank)
        if b and idx < len(b["names"]):
            return b["names"][idx]
        return f"bank{bank}:{idx}"

    def id(self, name):
        for bank, b in self.banks.items():
            if name in b["names"]:
                return (bank << 16) | b["names"].index(name)
        raise KeyError(name)
