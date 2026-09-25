"""Particle bank generators: walking a generator descriptor's program.

A descriptor is a 0x3c-byte header followed by its program, a byte-coded list of
opcodes that ends on 0xfe or 0xff. Nothing inside a descriptor is a pointer - loops
store offsets from the program base, and textures, child generators and tables are
named by index - so the bytes move anywhere intact.
"""

PROGRAM = 0x3C

# Opcodes whose operands are a fixed number of bytes.
_FIXED = {
    **{op: 0 for op in (0xA1, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB4, 0xB5, 0xE2, 0xE6, 0xE7,
                        0xF5, 0xF6, 0xF7, 0xFB, 0xFC, 0xFD)},
    **{op: 1 for op in (0xA7, 0xB7, 0xBF, 0xE1, 0xE3, 0xE4, 0xE5, 0xFA)},
    **{op: 2 for op in (0xA4, 0xA5, 0xB9, 0xBC, 0xF1, 0xF2)},
    **{op: 3 for op in (0xEF, 0xF0)},
    **{op: 4 for op in (0xA2, 0xA3, 0xA6, 0xA9, 0xAA, 0xAB, 0xBA, 0xBB, 0xE0, 0xE8)},
    **{op: 5 for op in (0xEC,)},
    **{op: 8 for op in (0xBD,)},
    **{op: 9 for op in (0xB8, 0xED)},
    **{op: 12 for op in (0xA8, 0xBE)},
    **{op: 16 for op in (0xF4,)},
}

TERMINATORS = (0xFE, 0xFF)


def _v16(p, i):
    """Bytes a variable-length count takes: one, or two when the top bit is set."""
    return 2 if p[i] & 0x80 else 1


def walk(desc):
    """Yield (offset, opcode) for every instruction of the descriptor's program, the
    terminator last. Raises ValueError on an opcode with no known operand shape."""
    p = desc
    i = PROGRAM
    while i < len(p):
        at, op = i, p[i]
        i += 1
        if op < 0x80:
            if op & 0x20:
                i += 1
            if op & 0xC0 == 0x40:
                i += 1
        elif op < 0xA0:
            i += 4 * bin(op & 7).count("1")
        elif op in _FIXED:
            i += _FIXED[op]
        elif op in (0xA0, 0xB6):
            i += _v16(p, i) + 4
        elif op == 0xAC:
            i += _v16(p, i) + 8
        elif op == 0xB3:
            i += _v16(p, i) + 3
        elif 0xC0 <= op < 0xE0:
            i += _v16(p, i) + bin(op & 0xF).count("1")
        elif op in (0xEA, 0xEB):
            i += _v16(p, i)
            flag = p[i]
            i += 1 + (flag & 1) + (1 if flag & 8 else 0)
        elif op == 0xF3:
            i += 9
            i += _v16(p, i)
        elif op in TERMINATORS:
            yield at, op
            return
        else:
            raise ValueError(f"unknown particle opcode {op:#04x} at {at:#x}")
        yield at, op
    raise ValueError("program runs off the end without a terminator")


def length(desc):
    """Bytes the descriptor takes, header through the terminator."""
    last = None
    for last, _ in walk(desc):
        pass
    return last + 1


def color_offsets(desc):
    """Offsets of the RGB triples a color opcode sets with a one-byte duration."""
    out = set()
    for at, op in walk(desc):
        if 0xC0 <= op < 0xE0 and op & 0x7 == 0x7 and desc[at + 1] < 0x80:
            out.add(at + 2)
    return out
