#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""HSD .dat archive explorer.

Subcommands:
    ls <file.dat>                       Header + publics/externs + type guesses.
    tree <file.dat> [<public>]          Walk the JObj tree at <public> (or first).
    grdata <file.dat> [<public>]        Decode a KAR_grData public's fields.
    find <pattern> [<glob>]             Grep public/extern symbols across .dat files.

Examples:
    uv run python scripts/hsd/explore.py ls iso/files/GrSpace2Model.dat
    uv run python scripts/hsd/explore.py tree iso/files/GrSpace2Model.dat grModelSpace2
    uv run python scripts/hsd/explore.py grdata iso/files/GrCity1.dat
    uv run python scripts/hsd/explore.py find grModel iso/files/Gr*Model.dat
"""

import argparse
import glob
import os
import re
import struct
import sys
from collections import OrderedDict

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hsd import Archive, Walker, classify_symbol, image_size, u16, u32, cstr
from hsd.archive import NotAnHSDArchive


def f32(b, o):
    return struct.unpack(">f", b[o : o + 4])[0]


def _open_or_complain(path):
    """Return Archive(path) or None after printing a clear error."""
    try:
        return Archive(path)
    except NotAnHSDArchive as e:
        print(str(e), file=sys.stderr)
        return None
    except (OSError, ValueError) as e:
        print(f"{path}: {e}", file=sys.stderr)
        return None


# --- Flag / enum decode tables ----------------------------------------
# Ported from HSDLib (HSDRaw/Common/HSD_*.cs). Single-bit flags only;
# multi-bit fields (billboard, joint type) are handled separately.

JOBJ_FLAGS_SINGLE = [
    (1 << 0, "SKELETON"),
    (1 << 1, "SKELETON_ROOT"),
    (1 << 2, "ENVELOPE_MODEL"),
    (1 << 3, "CLASSICAL_SCALING"),
    (1 << 4, "HIDDEN"),
    (1 << 5, "PTCL"),
    (1 << 6, "MTX_DIRTY"),
    (1 << 7, "LIGHTING"),
    (1 << 8, "TEXGEN"),
    (1 << 12, "INSTANCE"),
    (1 << 13, "PBILLBOARD"),
    (1 << 14, "SPLINE"),
    (1 << 15, "FLIP_IK"),
    (1 << 16, "SPECULAR"),
    (1 << 17, "USE_QUATERNION"),
    (1 << 18, "OPA"),
    (1 << 19, "XLU"),
    (1 << 20, "TEXEDGE"),
    (1 << 23, "USER_DEFINED_MTX"),
    (1 << 24, "MTX_INDEPEND_PARENT"),
    (1 << 25, "MTX_INDEPEND_SRT"),
    (1 << 26, "MTX_SCALE_COMPENSATE"),
    (1 << 28, "ROOT_OPA"),
    (1 << 29, "ROOT_XLU"),
    (1 << 30, "ROOT_TEXEDGE"),
]
# Bits 9-11: billboard mode field.
JOBJ_BILLBOARD = {1: "BILLBOARD", 2: "VBILLBOARD", 3: "HBILLBOARD", 4: "RBILLBOARD"}
# Bits 21-22: joint type field.
JOBJ_JOINT = {1: "JOINT1", 2: "JOINT2", 3: "EFFECTOR"}


def _decode_jobj_flags(v):
    names = [n for bit, n in JOBJ_FLAGS_SINGLE if v & bit]
    bb = (v >> 9) & 0x7
    if bb in JOBJ_BILLBOARD:
        names.append(JOBJ_BILLBOARD[bb])
    jt = (v >> 21) & 0x3
    if jt in JOBJ_JOINT:
        names.append(JOBJ_JOINT[jt])
    return " ".join(names) if names else "-"


MOBJ_RENDER_FLAGS = [
    (1 << 0, "CONSTANT"),
    (1 << 1, "VERTEX"),
    (1 << 2, "DIFFUSE"),
    (1 << 3, "SPECULAR"),
    (1 << 4, "TEX0"),
    (1 << 5, "TEX1"),
    (1 << 6, "TEX2"),
    (1 << 7, "TEX3"),
    (1 << 8, "TEX4"),
    (1 << 9, "TEX5"),
    (1 << 10, "TEX6"),
    (1 << 11, "TEX7"),
    (1 << 12, "TOON"),
    (1 << 24, "ZOFST"),
    (1 << 25, "EFFECT"),
    (1 << 26, "SHADOW"),
    (1 << 27, "ZMODE_ALWAYS"),
    (1 << 28, "DF_ALL"),
    (1 << 29, "NO_ZUPDATE"),
    (1 << 30, "XLU"),
    (1 << 31, "USER"),
]
# Bits 13-14: alpha source field.
MOBJ_ALPHA = {1: "ALPHA_MAT", 2: "ALPHA_VTX", 3: "ALPHA_BOTH"}


def _decode_mobj_flags(v):
    names = [n for bit, n in MOBJ_RENDER_FLAGS if v & bit]
    al = (v >> 13) & 0x3
    if al in MOBJ_ALPHA:
        names.append(MOBJ_ALPHA[al])
    return " ".join(names) if names else "-"


POBJ_FLAGS = [
    (1 << 0, "SHAPESET_AVERAGE"),
    (1 << 1, "SHAPESET_ADDITIVE"),
    (1 << 3, "ANIM"),
    (1 << 12, "SHAPEANIM"),
    (1 << 13, "ENVELOPE"),
    (1 << 14, "CULLBACK"),
    (1 << 15, "CULLFRONT"),
]


def _decode_pobj_flags(v):
    names = [n for bit, n in POBJ_FLAGS if v & bit]
    return " ".join(names) if names else "-"


TOBJ_FLAGS_SINGLE = [
    (1 << 4, "LIGHTMAP_DIFFUSE"),
    (1 << 5, "LIGHTMAP_SPECULAR"),
    (1 << 6, "LIGHTMAP_AMBIENT"),
    (1 << 7, "LIGHTMAP_EXT"),
    (1 << 8, "LIGHTMAP_SHADOW"),
    (1 << 24, "BUMP"),
    (1 << 31, "MTX_DIRTY"),
]
# Bits 0-3: tex coord type.
TOBJ_COORD = {
    0: "UV",
    1: "REFLECTION",
    2: "HILIGHT",
    3: "SHADOW",
    4: "TOON",
    5: "GRADATION",
}
# Bits 16-19: colormap blend mode.
TOBJ_COLORMAP = {
    1: "CM_ALPHA_MASK",
    2: "CM_RGB_MASK",
    3: "CM_BLEND",
    4: "CM_MODULATE",
    5: "CM_REPLACE",
    6: "CM_PASS",
    7: "CM_ADD",
    8: "CM_SUB",
}
# Bits 20-23: alphamap blend mode.
TOBJ_ALPHAMAP = {
    1: "AM_ALPHA_MASK",
    2: "AM_BLEND",
    3: "AM_MODULATE",
    4: "AM_REPLACE",
    5: "AM_PASS",
    6: "AM_ADD",
    7: "AM_SUB",
}


def _decode_tobj_flags(v):
    names = [n for bit, n in TOBJ_FLAGS_SINGLE if v & bit]
    coord = v & 0xF
    if coord in TOBJ_COORD:
        names.insert(0, f"COORD_{TOBJ_COORD[coord]}")
    cm = (v >> 16) & 0xF
    if cm in TOBJ_COLORMAP:
        names.append(TOBJ_COLORMAP[cm])
    am = (v >> 20) & 0xF
    if am in TOBJ_ALPHAMAP:
        names.append(TOBJ_ALPHAMAP[am])
    return " ".join(names) if names else "-"


# PEDesc enums (GX values) from HSDRaw/GX/Enums.cs.
GX_BLEND_MODE = {0: "NONE", 1: "BLEND", 2: "LOGIC", 3: "SUBTRACT"}
GX_BLEND_FACTOR = {
    0: "ZERO",
    1: "ONE",
    2: "SRCCOLOR",
    3: "INVSRCCOLOR",
    4: "SRCALPHA",
    5: "INVSRCALPHA",
    6: "DSTALPHA",
    7: "INVDSTALPHA",
}
GX_COMPARE = {
    0: "NEVER",
    1: "LESS",
    2: "EQUAL",
    3: "LEQUAL",
    4: "GREATER",
    5: "NEQUAL",
    6: "GEQUAL",
    7: "ALWAYS",
}
GX_ALPHA_OP = {0: "AND", 1: "OR", 2: "XOR", 3: "XNOR"}

PE_FLAGS = [
    (1 << 0, "COLOR_UPDATE"),
    (1 << 1, "ALPHA_UPDATE"),
    (1 << 2, "DST_ALPHA"),
    (1 << 3, "BEFORE_TEX"),
    (1 << 4, "COMPARE"),
    (1 << 5, "ZUPDATE"),
    (1 << 6, "DITHER"),
]


def _decode_pe_flags(v):
    names = [n for bit, n in PE_FLAGS if v & bit]
    return " ".join(names) if names else "-"


# LOBJ_Flags from HSDLib HSD_LOBJ.cs. Low 2 bits = LObjType (AMBIENT /
# INFINITE / POINT / SPOT); higher bits are independent feature flags.
LOBJ_TYPES = {0: "AMBIENT", 1: "INFINITE", 2: "POINT", 3: "SPOT"}
LOBJ_FLAGS = [
    (1 << 2, "DIFFUSE"),
    (1 << 3, "SPECULAR"),
    (1 << 4, "ALPHA"),
    (1 << 5, "HIDDEN"),
    (1 << 6, "RAW_PARAM"),
    (1 << 7, "DIFF_DIRTY"),
    (1 << 8, "SPEC_DIRTY"),
    (1 << 10, "SHADOW"),
]


def _decode_lobj_flags(v):
    parts = [LOBJ_TYPES.get(v & 0x3, "?")]
    parts.extend(n for bit, n in LOBJ_FLAGS if v & bit)
    return " ".join(parts)


# FogType enum (HSDLib HSD_Fog.cs).
FOG_TYPES = {
    0x00: "NONE",
    0x02: "PERSP_LINEAR",
    0x04: "PERSP_EXP",
    0x05: "PERSP_EXP2",
    0x06: "PERSP_REVEXP",
    0x07: "PERSP_REVEXP2",
    0x0A: "ORTHO_LINEAR",
    0x0C: "ORTHO_EXP",
    0x0D: "ORTHO_EXP2",
    0x0E: "ORTHO_REVEXP",
    0x0F: "ORTHO_REVEXP2",
}

# ROBJ ref type (top nibble of flags).
ROBJ_REFTYPES = {0x0: "EXP", 0x1: "JOBJ", 0x2: "LIMIT", 0x3: "BYTECODE", 0x4: "IKHINT"}
ROBJ_LIMIT_NAMES = {
    1: "MIN_ROTX",
    2: "MAX_ROTX",
    3: "MIN_ROTY",
    4: "MAX_ROTY",
    5: "MIN_ROTZ",
    6: "MAX_ROTZ",
    7: "MIN_TRAX",
    8: "MAX_TRAX",
    9: "MIN_TRAY",
    10: "MAX_TRAY",
    11: "MIN_TRAZ",
    12: "MAX_TRAZ",
}

# GXTexFilter (HSDLib HSDRaw.GX.Enums).
GX_TEX_FILTER = {
    0: "NEAR",
    1: "LINEAR",
    2: "NEAR_MIP_NEAR",
    3: "LIN_MIP_NEAR",
    4: "NEAR_MIP_LIN",
    5: "LIN_MIP_LIN",
}

# CameraProjection (HSDLib HSD_COBJ.cs).
CAMERA_PROJ = {1: "PERSPECTIVE", 2: "FRUSTRUM", 3: "ORTHO"}

# GXTexFmt (HSDLib HSDRaw.GX.Enums) -- used by ImageDesc + IOBJDesc.
GX_TEX_FMT = {
    0: "I4",
    1: "I8",
    2: "IA4",
    3: "IA8",
    4: "RGB565",
    5: "RGB5A3",
    6: "RGBA8",
    8: "C4",
    9: "C8",
    10: "C14X2",
    14: "CMPR",
}

# AOBJ_Flags (HSDLib HSD_AOBJ.cs). Low 24 bits are reserved for the
# anim-frame counter at runtime; only the named flags are stored on disc.
AOBJ_FLAGS = [
    (1 << 26, "REWINDED"),
    (1 << 27, "FIRST_PLAY"),
    (1 << 28, "NO_UPDATE"),
    (1 << 29, "LOOP"),
    (1 << 30, "NO_ANIM"),
]


def _decode_aobj_flags(v):
    names = [n for bit, n in AOBJ_FLAGS if v & bit]
    return " ".join(names) if names else "-"


# GXAnimDataFormat (HSDLib HSD_FOBJ.cs). Top 3 bits of value/tangent flag
# byte. Bottom 5 bits are log2 of the scale (`scale = 1 << (flag & 0x1F)`).
ANIM_FMT = {
    0x00: "FLOAT",
    0x20: "S16",
    0x40: "U16",
    0x60: "S8",
    0x80: "U8",
}


def _decode_anim_flag(b):
    """(format_name, scale_pow2) for a FOBJ value/tangent flag byte."""
    return ANIM_FMT.get(b & 0xE0, f"?{b & 0xE0:#x}"), b & 0x1F


# --- ls ----------------------------------------------------------------


def cmd_ls(args):
    arc = _open_or_complain(args.path)
    if arc is None:
        return 1
    print(f"{args.path}")
    print(f"  file_size  = {arc.file_size:#x} ({arc.file_size / 1024:.1f} KB)")
    print(f"  data_size  = {arc.data_size:#x} ({arc.data_size / 1024:.1f} KB)")
    print(f"  nb_reloc   = {arc.nb_reloc}")
    print(f"  nb_public  = {arc.nb_public}")
    print(f"  nb_extern  = {arc.nb_extern}")
    version = arc.version.rstrip(b"\0").decode("ascii", errors="replace")
    print(f"  version    = {version!r}")

    if arc.publics:
        print("\n  publics:")
        for name, off in arc.publics.items():
            klass = classify_symbol(name)
            tag = f"  [{klass}]" if klass else "  [?]"
            print(f"    {off:#08x}  {name}{tag}")
    if arc.externs:
        print("\n  externs:")
        for off, name in arc.externs:
            print(f"    {off:#08x}  {name}")
    return 0


# --- tree --------------------------------------------------------------

# Per-type children: list of either
#   (field_offset, child_type)           - single pointer
#   (field_offset, child_type, 'array')  - NULL-terminated pointer array
# Some JObj/RObj/POBJ slots are routed dynamically below in _tree_children
# based on flag bits (SPLINE/PTCL on JObj, REFTYPE on ROBJ,
# ENVELOPE/SHAPEANIM on POBJ).
TREE_FIELDS = {
    "JOBJDesc": [
        (0x08, "JOBJDesc"),  # child
        (0x10, "DObjDesc"),  # dobj (may be Spline/ParticleJoint)
        (0x3C, "RObjDesc"),  # robj
        (0x0C, "JOBJDesc"),
    ],  # next  (printed last)
    "DObjDesc": [(0x08, "MObjDesc"), (0x0C, "POBJDesc"), (0x04, "DObjDesc")],
    "MObjDesc": [(0x08, "TObjDesc"), (0x0C, "MaterialDesc"), (0x14, "PEDesc")],
    "TObjDesc": [
        (0x4C, "ImageDesc"),
        (0x50, "TlutDesc"),
        (0x54, "TexLODDesc"),
        (0x58, "TObjTev"),
        (0x04, "TObjDesc"),
    ],
    "POBJDesc": [(0x04, "POBJDesc")],  # +0x14 routed dynamically
    "RObjDesc": [
        (0x08, "JOBJDesc"),  # only when REFTYPE == JOBJ
        (0x00, "RObjDesc"),
    ],  # next
    "LObjDesc": [(0x10, "WObjDesc"), (0x14, "WObjDesc"), (0x04, "LObjDesc")],
    "WObjDesc": [(0x10, "RObjDesc")],
    "FogDesc": [(0x04, "FogAdjDesc")],
    "FogAnim": [(0x00, "FogDesc"), (0x04, "AOBJ")],
    "AOBJ": [(0x08, "FOBJDesc"), (0x0C, "JOBJDesc")],  # ObjectReference (often NULL)
    "FOBJDesc": [(0x00, "FOBJDesc")],  # linked list; buffer at +0x10 is leaf
    "FOBJ": [],
    "LightGroup": [(0x00, "LightNode"), (0x04, "LightNode"), (0x08, "LightNode")],
    "LightNode": [(0x00, "Light"), (0x04, "Light"), (0x08, "Light"), (0x0C, "Light")],
    "Light": [(0x00, "LObjDesc")],
    # SOBJ: three NULL-terminated pointer arrays + an inline FogAnim.
    "SOBJ": [
        (0x00, "ModelGroup", "array"),
        (0x04, "Camera", "array"),
        (0x08, "Light", "array"),
        (0x0C, "FogAnim"),
    ],
    "ModelGroup": [(0x00, "JOBJDesc")],
    "Camera": [
        (0x18, "WObjDesc"),  # eye
        (0x1C, "WObjDesc"),
    ],  # target
    "ShapeSet": [],  # variable-length tables; leaf for tree purposes
    "IOBJDesc": [],  # image-buffer blob; leaf
    "ParticleGroup": [],  # generator table is byte-embedded; leaf
    "Envelope": [],  # variable-length JOBJ-weight table; leaf
    "ImageDesc": [],
    "TlutDesc": [],
    "MaterialDesc": [],
    "PEDesc": [],
    "TexLODDesc": [],
    "TObjTev": [],
    "FogAdjDesc": [],
    "Spline": [],
    "ParticleJoint": [],
}

FIELD_LABEL = {
    ("JOBJDesc", 0x08): "child",
    ("JOBJDesc", 0x0C): "next",
    ("JOBJDesc", 0x10): "dobj",
    ("JOBJDesc", 0x3C): "robj",
    ("DObjDesc", 0x04): "next",
    ("DObjDesc", 0x08): "mobj",
    ("DObjDesc", 0x0C): "pobj",
    ("MObjDesc", 0x08): "tobj",
    ("MObjDesc", 0x0C): "material",
    ("MObjDesc", 0x14): "pedesc",
    ("TObjDesc", 0x04): "next",
    ("TObjDesc", 0x4C): "image",
    ("TObjDesc", 0x50): "tlut",
    ("TObjDesc", 0x54): "lod",
    ("TObjDesc", 0x58): "tev",
    ("POBJDesc", 0x04): "next",
    ("RObjDesc", 0x00): "next",
    ("RObjDesc", 0x08): "target",
    ("LObjDesc", 0x04): "next",
    ("LObjDesc", 0x10): "position",
    ("LObjDesc", 0x14): "interest",
    ("WObjDesc", 0x10): "robj",
    ("FogDesc", 0x04): "adj",
    ("LightGroup", 0x00): "global",
    ("LightGroup", 0x04): "group1",
    ("LightGroup", 0x08): "group2",
    ("LightNode", 0x00): "light1",
    ("LightNode", 0x04): "light2",
    ("LightNode", 0x08): "light3",
    ("LightNode", 0x0C): "light4",
    ("Light", 0x00): "lobj",
    ("SOBJ", 0x00): "models",
    ("SOBJ", 0x04): "cameras",
    ("SOBJ", 0x08): "lights",
    ("SOBJ", 0x0C): "fog",
    ("ModelGroup", 0x00): "root",
    ("Camera", 0x18): "eye",
    ("Camera", 0x1C): "target",
    ("FogAnim", 0x00): "fog",
    ("FogAnim", 0x04): "anim",
    ("AOBJ", 0x08): "tracks",
    ("AOBJ", 0x0C): "object",
    ("FOBJDesc", 0x00): "next",
    ("POBJDesc", 0x14): "bound",
}


# JOBJ flag bits we route on (mirror walker.py).
_JOBJ_FLAG_PTCL = 1 << 5
_JOBJ_FLAG_SPLINE = 1 << 14
_ROBJ_REFTYPE_JOBJ = 0x10000000
_POBJ_FLAG_SHAPEANIM = 1 << 12
_POBJ_FLAG_ENVELOPE = 1 << 13


def _tree_children(arc: Archive, typ: str, off: int):
    """Resolve child-list with dynamic type routing for union fields."""
    if typ == "JOBJDesc":
        flags = u32(arc.data, off + 0x04)
        if flags & _JOBJ_FLAG_SPLINE:
            slot10 = "Spline"
        elif flags & _JOBJ_FLAG_PTCL:
            slot10 = "ParticleJoint"
        else:
            slot10 = "DObjDesc"
        return [
            (0x08, "JOBJDesc"),
            (0x10, slot10),
            (0x3C, "RObjDesc"),
            (0x0C, "JOBJDesc"),
        ]
    if typ == "RObjDesc":
        flags = u32(arc.data, off + 0x04)
        children = [(0x00, "RObjDesc")]
        if (flags & 0x70000000) == _ROBJ_REFTYPE_JOBJ:
            children.insert(0, (0x08, "JOBJDesc"))
        return children
    if typ == "POBJDesc":
        flags = u16(arc.data, off + 0x0C)
        children = [(0x04, "POBJDesc")]
        if flags & _POBJ_FLAG_SHAPEANIM:
            children.insert(0, (0x14, "ShapeSet"))
        elif flags & _POBJ_FLAG_ENVELOPE:
            children.insert(0, (0x14, "Envelope", "array"))
        else:
            children.insert(0, (0x14, "JOBJDesc"))  # SingleBoundJOBJ
        return children
    return TREE_FIELDS.get(typ, [])


def _name_at(arc: Archive, struct_off: int, name_field_off: int) -> str:
    """If data[struct_off + name_field_off] is a reloc, deref and read its cstring."""
    if (struct_off + name_field_off) not in arc.reloc_set:
        return ""
    p = u32(arc.data, struct_off + name_field_off)
    return cstr(arc.data, p) if p else ""


def _detail(arc: Archive, typ: str, off: int) -> str:
    """One-liner extra info per node type."""
    if typ == "JOBJDesc":
        nm = _name_at(arc, off, 0x00)
        flags = u32(arc.data, off + 0x04)
        bits = []
        if nm:
            bits.append(f'"{nm}"')
        bits.append(f"flags={flags:#010x} [{_decode_jobj_flags(flags)}]")
        return " " + " ".join(bits)
    if typ == "DObjDesc":
        nm = _name_at(arc, off, 0x00)
        return f' "{nm}"' if nm else ""
    if typ == "MObjDesc":
        nm = _name_at(arc, off, 0x00)
        rf = u32(arc.data, off + 0x04)
        bits = []
        if nm:
            bits.append(f'"{nm}"')
        bits.append(f"render={rf:#010x} [{_decode_mobj_flags(rf)}]")
        return " " + " ".join(bits)
    if typ == "TObjDesc":
        nm = _name_at(arc, off, 0x00)
        tex_map = u32(arc.data, off + 0x08)  # GX texmap id (0..7)
        flags = u32(arc.data, off + 0x40)  # TOBJ_FLAGS (HSDLib HSD_TOBJ.cs)
        bits = []
        if nm:
            bits.append(f'"{nm}"')
        bits.append(f"map=TEXMAP{tex_map}")
        bits.append(f"flags={flags:#010x} [{_decode_tobj_flags(flags)}]")
        return " " + " ".join(bits)
    if typ == "POBJDesc":
        flags = u16(arc.data, off + 0x0C)
        dl_size = u16(arc.data, off + 0x0E) * 32
        return f" flags={flags:#06x} [{_decode_pobj_flags(flags)}] dl={dl_size}B"
    if typ == "ImageDesc":
        w = u16(arc.data, off + 4)
        h = u16(arc.data, off + 6)
        fmt = u32(arc.data, off + 8)
        sz = image_size(w, h, fmt)
        return f" {w}x{h} fmt={fmt} ({sz} B)"
    if typ == "TlutDesc":
        n = u16(arc.data, off + 0x0C)
        return f" n={n} ({n * 2} B)"
    if typ == "MaterialDesc":
        amb = tuple(arc.data[off + i] for i in range(0x00, 0x04))
        dif = tuple(arc.data[off + i] for i in range(0x04, 0x08))
        spc = tuple(arc.data[off + i] for i in range(0x08, 0x0C))
        alpha = f32(arc.data, off + 0x0C)
        shine = f32(arc.data, off + 0x10)

        def rgba(t):
            return f"#{t[0]:02X}{t[1]:02X}{t[2]:02X}{t[3]:02X}"

        return f" amb={rgba(amb)} dif={rgba(dif)} spc={rgba(spc)} alpha={alpha:.2f} shine={shine:.2f}"
    if typ == "PEDesc":
        peflags = arc.data[off + 0x00]
        aref0 = arc.data[off + 0x01]
        aref1 = arc.data[off + 0x02]
        dalpha = arc.data[off + 0x03]
        bmode = arc.data[off + 0x04]
        srcf = arc.data[off + 0x05]
        dstf = arc.data[off + 0x06]
        depth = arc.data[off + 0x08]
        return (
            f" [{_decode_pe_flags(peflags)}]"
            f" blend={GX_BLEND_MODE.get(bmode, bmode)}"
            f" src={GX_BLEND_FACTOR.get(srcf, srcf)}"
            f" dst={GX_BLEND_FACTOR.get(dstf, dstf)}"
            f" depth={GX_COMPARE.get(depth, depth)}"
            f" aref=({aref0},{aref1}) dstA={dalpha}"
        )
    if typ == "RObjDesc":
        flags = u32(arc.data, off + 0x04)
        rt = (flags >> 28) & 0x7
        rt_name = ROBJ_REFTYPES.get(rt, str(rt))
        bits = [f"ref={rt_name}"]
        if rt_name == "LIMIT":
            limit_id = flags & 0xFFFFFF
            limit_name = ROBJ_LIMIT_NAMES.get(limit_id, str(limit_id))
            bits.append(f"limit={limit_name}")
        return " " + " ".join(bits) + f" flags={flags:#010x}"
    if typ == "LObjDesc":
        flags = u16(arc.data, off + 0x08)
        attn = u16(arc.data, off + 0x0A)
        r = arc.data[off + 0x0C]
        g = arc.data[off + 0x0D]
        b = arc.data[off + 0x0E]
        a = arc.data[off + 0x0F]
        attn_tag = " ATTN" if attn & 1 else ""
        return (
            f" color=#{r:02X}{g:02X}{b:02X}{a:02X}"
            f" [{_decode_lobj_flags(flags)}{attn_tag}]"
        )
    if typ == "WObjDesc":
        return (
            f" pos=({f32(arc.data, off + 0x04):.2f},"
            f" {f32(arc.data, off + 0x08):.2f},"
            f" {f32(arc.data, off + 0x0C):.2f})"
        )
    if typ == "FogDesc":
        ft = u32(arc.data, off + 0x00)
        start = f32(arc.data, off + 0x08)
        end = f32(arc.data, off + 0x0C)
        r = arc.data[off + 0x10]
        g = arc.data[off + 0x11]
        b = arc.data[off + 0x12]
        a = arc.data[off + 0x13]
        return (
            f" type={FOG_TYPES.get(ft, str(ft))}"
            f" near={start:.1f} far={end:.1f}"
            f" color=#{r:02X}{g:02X}{b:02X}{a:02X}"
        )
    if typ == "TexLODDesc":
        minf = u32(arc.data, off + 0x00)
        bias = f32(arc.data, off + 0x04)
        return f" min={GX_TEX_FILTER.get(minf, minf)} bias={bias:+.2f}"
    if typ == "TObjTev":
        cop = arc.data[off + 0x00]
        aop = arc.data[off + 0x01]
        active = u32(arc.data, off + 0x1C)
        return f" color_op={cop} alpha_op={aop} active={active:#x}"
    if typ == "Spline":
        spt = arc.data[off + 0x00]
        n = u16(arc.data, off + 0x02)
        tension = f32(arc.data, off + 0x04)
        length = f32(arc.data, off + 0x0C)
        return f" type={spt} pts={n} tension={tension:.2f} len={length:.2f}"
    if typ == "SOBJ":
        # Pre-count the three null-ptr arrays so the header line is useful.
        def _arr_len(slot):
            if (off + slot) not in arc.reloc_set:
                return 0
            arr = u32(arc.data, off + slot)
            if not arr:
                return 0
            i = 0
            while True:
                e = arr + i * 4
                if e + 4 > len(arc.data):
                    break
                if e not in arc.reloc_set:
                    break
                if u32(arc.data, e) == 0:
                    break
                i += 1
            return i

        return (
            f" models={_arr_len(0x00)} cameras={_arr_len(0x04)} lights={_arr_len(0x08)}"
        )
    if typ == "Camera":
        nm = _name_at(arc, off, 0x00)
        proj = u16(arc.data, off + 0x06)
        vl = struct.unpack(">h", arc.data[off + 0x08 : off + 0x0A])[0]
        vr = struct.unpack(">h", arc.data[off + 0x0A : off + 0x0C])[0]
        vt = struct.unpack(">h", arc.data[off + 0x0C : off + 0x0E])[0]
        vb = struct.unpack(">h", arc.data[off + 0x0E : off + 0x10])[0]
        near = f32(arc.data, off + 0x28)
        far = f32(arc.data, off + 0x2C)
        bits = []
        if nm:
            bits.append(f'"{nm}"')
        bits.append(f"proj={CAMERA_PROJ.get(proj, str(proj))}")
        bits.append(f"view=({vl},{vt})-({vr},{vb})")
        bits.append(f"clip={near:.1f}..{far:.1f}")
        if proj == 1:  # PERSPECTIVE
            fov = f32(arc.data, off + 0x30)
            aspect = f32(arc.data, off + 0x34)
            bits.append(f"fov={fov:.1f} aspect={aspect:.3f}")
        return " " + " ".join(bits)
    if typ == "ModelGroup":
        return ""
    if typ == "FogAnim":
        anim_set = (off + 0x04) in arc.reloc_set and u32(arc.data, off + 0x04) != 0
        return " [anim]" if anim_set else ""
    if typ == "AOBJ":
        flags = u32(arc.data, off + 0x00)
        end_frame = f32(arc.data, off + 0x04)
        # Count tracks in the FOBJDesc linked list.
        n = 0
        slot = off + 0x08
        cur = u32(arc.data, slot) if slot in arc.reloc_set else 0
        while cur and n < 256:  # cap in case of cycles
            n += 1
            nxt = cur + 0x00
            if nxt not in arc.reloc_set:
                break
            cur = u32(arc.data, nxt)
        return (
            f" flags={flags:#010x} [{_decode_aobj_flags(flags)}]"
            f" end={end_frame:.1f} tracks={n}"
        )
    if typ == "FOBJDesc":
        data_len = u32(arc.data, off + 0x04)
        start = f32(arc.data, off + 0x08)
        track = arc.data[off + 0x0C]
        vfmt, vscale = _decode_anim_flag(arc.data[off + 0x0D])
        tfmt, tscale = _decode_anim_flag(arc.data[off + 0x0E])
        # track-type byte is context-dependent (Fog/Joint/Mat/Tex/Light/Shape);
        # print raw + cross-ref the HSDLib enums in HSD_FOBJ.cs.
        return (
            f" track={track} start={start:.1f} len={data_len}B"
            f" v={vfmt}/2^{vscale} t={tfmt}/2^{tscale}"
        )
    if typ == "FOBJ":
        track = arc.data[off + 0x00]
        vfmt, vscale = _decode_anim_flag(arc.data[off + 0x01])
        tfmt, tscale = _decode_anim_flag(arc.data[off + 0x02])
        return f" track={track} v={vfmt}/2^{vscale} t={tfmt}/2^{tscale}"
    if typ == "ShapeSet":
        flags = u16(arc.data, off + 0x00)
        n = u16(arc.data, off + 0x02)
        vic = u32(arc.data, off + 0x04)
        nic = u32(arc.data, off + 0x10)
        return f" flags={flags:#06x} shapes={n} v_idx={vic} n_idx={nic}"
    if typ == "IOBJDesc":
        w = u16(arc.data, off + 0x00)
        h = u16(arc.data, off + 0x02)
        fmt = u32(arc.data, off + 0x04)
        sz = image_size(w, h, fmt)
        return f" {w}x{h} fmt={GX_TEX_FMT.get(fmt, fmt)} ({sz} B)"
    if typ == "ParticleGroup":
        u1 = u16(arc.data, off + 0x00)
        u2 = u16(arc.data, off + 0x02)
        eff = u32(arc.data, off + 0x04)
        cnt = u32(arc.data, off + 0x08)
        return f" unk=({u1},{u2}) effect_id={eff:#x} generators={cnt}"
    if typ == "Envelope":
        # Count JOBJ-weight entries until reloc tail.
        n = 0
        while True:
            e = off + n * 8
            if e + 8 > len(arc.data) or e not in arc.reloc_set:
                break
            n += 1
        return f" entries={n}"
    return ""


def _walk_print(arc: Archive, root: int, root_type: str, max_depth):
    """DFS print with indent. Re-entry of a visited offset is shown as a backref."""
    visited = {}  # offset -> first-print depth
    counts = {}  # type -> count
    sizes_known = {
        "JOBJDesc": 0x40,
        "DObjDesc": 0x10,
        "MObjDesc": 0x18,
        "TObjDesc": 0x5C,
        "POBJDesc": 0x18,
        "RObjDesc": 0xC,
        "ImageDesc": 0x18,
        "TlutDesc": 0x10,
        "MaterialDesc": 0x14,
        "PEDesc": 0xC,
        "TexLODDesc": 0x10,
        "TObjTev": 0x20,
        "LObjDesc": 0x1C,
        "WObjDesc": 0x14,
        "FogDesc": 0x18,
        "FogAdjDesc": 0x44,
        "LightGroup": 0xC,
        "LightNode": 0x10,
        "Light": 0x08,
        "Spline": 0x18,
        "ParticleJoint": 0x08,
        "SOBJ": 0x10,
        "Camera": 0x40,
        "ModelGroup": 0x10,
        "FogAnim": 0x08,
        "ShapeSet": 0x1C,
        "IOBJDesc": 0x0C,
        "AOBJ": 0x10,
        "FOBJDesc": 0x14,
        "FOBJ": 0x08,
    }

    def line(depth, label, typ, off, suffix=""):
        indent = "  " * depth
        sz = sizes_known.get(typ)
        sztag = f" [{sz:#x}]" if sz else ""
        print(f"{indent}{label}{typ} @ {off:#x}{sztag}{suffix}")

    def go(depth, label, typ, off):
        if off == 0:
            return
        counts[typ] = counts.get(typ, 0) + 1
        if off in visited:
            line(
                depth, label, typ, off, f" (cycle, first seen at depth {visited[off]})"
            )
            return
        visited[off] = depth
        line(depth, label, typ, off, _detail(arc, typ, off))
        if max_depth is not None and depth >= max_depth:
            return
        for child in _tree_children(arc, typ, off):
            # child is either (foff, ftyp) or (foff, ftyp, 'array').
            if len(child) == 3 and child[2] == "array":
                foff, ftyp, _ = child
                slot = off + foff
                if slot not in arc.reloc_set:
                    continue
                arr = u32(arc.data, slot)
                if not arr:
                    continue
                lab = FIELD_LABEL.get((typ, foff), f"+{foff:#x}=")
                indent = "  " * (depth + 1)
                print(f"{indent}{lab}: {ftyp}[] @ {arr:#x}")
                # Walk entries until terminator (non-reloc'd or NULL slot).
                i = 0
                while True:
                    entry = arr + i * 4
                    if entry + 4 > len(arc.data) or entry not in arc.reloc_set:
                        break
                    tgt = u32(arc.data, entry)
                    if tgt == 0:
                        break
                    go(depth + 2, f"[{i}]: ", ftyp, tgt)
                    i += 1
            else:
                foff, ftyp = child
                slot = off + foff
                if slot in arc.reloc_set:
                    tgt = u32(arc.data, slot)
                    lab = FIELD_LABEL.get((typ, foff), f"+{foff:#x}=")
                    go(depth + 1, f"{lab}: ", ftyp, tgt)

    go(0, "", root_type, root)
    return counts


# HSDLib class -> our walker's root type. Used by cmd_tree to dispatch
# non-JOBJDesc publics to the right walker entry. Missing entries fall
# back to JOBJDesc (user can override via --root-type).
CLASS_TO_ROOT = {
    "HSD_SOBJ": "SOBJ",
    "HSD_MOBJ": "MObjDesc",
    "HSD_IOBJ": "IOBJDesc",
    "HSD_Camera": "Camera",
    "HSD_JOBJ": "JOBJDesc",  # our "JOBJDesc" name == HSDLib HSD_JOBJ
    "HSD_JOBJDesc": "ModelGroup",  # the 0x10 wrapper
    "HSD_ModelGroup": "ModelGroup",  # same layout as HSD_JOBJDesc
    "HSD_FogDesc": "FogDesc",
    "HSD_ParticleGroup": "ParticleGroup",
}

# Roots whose backing storage IS a NULL-terminated pointer array. Each
# value is the per-entry element type our walker knows. cmd_tree walks
# these entries inline (no wrapping struct exists on disc).
ARRAY_ROOTS = {
    "HSDNullPointerArrayAccessor<HSD_Light>": "Light",  # _scene_lights / map_plit
    "HSDNullPointerArrayAccessor<HSD_JOBJDesc>": "ModelGroup",  # _scene_models
}


def cmd_tree(args):
    arc = _open_or_complain(args.path)
    if arc is None:
        return 1
    sym = args.symbol
    if sym is None:
        if not arc.publics:
            print("no public symbols")
            return 1
        sym = next(iter(arc.publics))
        print(f"# defaulting to first public: {sym}")
    if sym not in arc.publics:
        print(f"public symbol {sym!r} not found. Available: {list(arc.publics)}")
        return 1

    off = arc.publics[sym]
    klass = classify_symbol(sym)
    print(f"# {sym} @ {off:#x}  type={klass or '?'}")

    # Pick a root type: explicit override wins, else map from classify_symbol.
    # Array-root publics (e.g. _scene_models, _scene_lights) get walked
    # entry-by-entry without a wrapping struct.
    counts_total = {}
    if not args.root_type and klass in ARRAY_ROOTS:
        elem = ARRAY_ROOTS[klass]
        print(f"# auto root-type: NullPtrArray<{elem}> (from {klass})")
        print(f"{elem}[] @ {off:#x}")
        i = 0
        while True:
            entry = off + i * 4
            if entry + 4 > len(arc.data) or entry not in arc.reloc_set:
                break
            tgt = u32(arc.data, entry)
            if tgt == 0:
                break
            c = _walk_print(arc, tgt, elem, args.max_depth)
            for t, n in c.items():
                counts_total[t] = counts_total.get(t, 0) + n
            i += 1
        if counts_total:
            print("\n# type counts:")
            for t in sorted(counts_total):
                print(f"  {t:12s} {counts_total[t]}")
        return 0

    root_type = args.root_type or CLASS_TO_ROOT.get(klass) or "JOBJDesc"
    if not args.root_type and root_type != "JOBJDesc":
        print(f"# auto root-type: {root_type} (from {klass})")

    if klass == "KAR_grModel":
        # ModelSection: JOBJDesc**[4]. Each non-NULL slot points at a pp
        # slot whose first dword is a JOBJDesc*. Walk each slot.
        for i in range(4):
            slot = off + i * 4
            in_reloc = slot in arc.reloc_set
            pp = u32(arc.data, slot) if in_reloc else 0
            if pp == 0:
                print(f"\nms[{i}] @ {slot:#x}: NULL")
                continue
            jobj = u32(arc.data, pp) if pp in arc.reloc_set else 0
            print(f"\nms[{i}] @ {slot:#x} -> pp {pp:#x} -> JOBJDesc {jobj:#x}")
            if jobj:
                c = _walk_print(arc, jobj, args.root_type or "JOBJDesc", args.max_depth)
                for t, n in c.items():
                    counts_total[t] = counts_total.get(t, 0) + n
    else:
        c = _walk_print(arc, off, root_type, args.max_depth)
        for t, n in c.items():
            counts_total[t] = counts_total.get(t, 0) + n

    if counts_total:
        print("\n# type counts:")
        for t in sorted(counts_total):
            print(f"  {t:12s} {counts_total[t]}")

    # If we reached any ImageDesc, also print the standard Walker summary
    # so users get a precise byte-budget of the reachable subtree.
    if args.summary and "ImageDesc" in counts_total:
        print("\n# full reachable summary (sized):")
        if klass == "KAR_grModel":
            roots = []
            for i in range(4):
                slot = off + i * 4
                if slot in arc.reloc_set:
                    pp = u32(arc.data, slot)
                    if pp and pp in arc.reloc_set:
                        jobj = u32(arc.data, pp)
                        if jobj:
                            roots.append(jobj)
        else:
            roots = [off]
        all_visited = OrderedDict()
        for r in roots:
            w = Walker(arc)
            v = w.walk(r, args.root_type or "JOBJDesc")
            for k, val in v.items():
                all_visited.setdefault(k, val)
        by_type = {}
        for o, (t, sz) in all_visited.items():
            by_type.setdefault(t, []).append(sz or 0)
        for t in sorted(by_type):
            sizes = by_type[t]
            print(f"  {t:18s} count={len(sizes):4d}  total={sum(sizes)} B")

    return 0


# --- grdata ------------------------------------------------------------

# Field layout for KAR_grData (HSDLib KAR_grData.cs). Each row:
#   (offset, name, type-string, kind)
# kind = 'ptr' (relocated reference), 'i32' (raw integer), 'runtime' (zeroed
# pointer slot the engine fills in at load time -- never relocated on disc).
GRDATA_FIELDS = [
    (0x00, "unk1", "i32", "i32"),
    (0x04, "StageNode", "KAR_grStageNode", "ptr"),
    (0x08, "unk2", "i32", "i32"),
    (0x0C, "model", "JOBJ* runtime", "runtime"),
    (0x10, "model_anim", "AObj* runtime", "runtime"),
    (0x14, "LightGroup", "KAR_grLightGroup", "ptr"),
    (0x18, "CollisionNode", "KAR_grCollisionNode", "ptr"),
    (0x1C, "SplineNode", "KAR_grSplineNode", "ptr"),
    (0x20, "PositionNode", "KAR_grPositionNode", "ptr"),
    (0x24, "SubAnimNode", "KAR_grSubAnimNode", "ptr"),
    (0x28, "EnemyNode", "HSDAccessor", "ptr"),
    (0x2C, "ItemNode", "HSDArray<KAR_grItemNode>", "ptr"),
    (0x30, "city_event", "runtime", "runtime"),
    (0x34, "FogNode", "KAR_grFogNode", "ptr"),
    (0x38, "RailCollNode", "KAR_grRailCollNode", "ptr"),
    (0x3C, "FGMNode", "KAR_grFGMNode", "ptr"),
    (0x40, "YakumonoNode", "HSDAccessor", "ptr"),
    (0x44, "ReplayNode", "HSDAccessor", "ptr"),
    (0x48, "PartitionNode", "KAR_grCollisionTreeNode", "ptr"),
    (0x4C, "RespawnNode", "KAR_grRespawnNode", "ptr"),
    (0x50, "StadiumNode", "HSDAccessor", "ptr"),
]


def _ptr(arc: Archive, off: int):
    """Return (value, is_reloc) at `off` in arc.data, or (None, False) past EOF."""
    if off + 4 > len(arc.data):
        return None, False
    return u32(arc.data, off), off in arc.reloc_set


def _fmt_ptr(v, is_rel):
    if v is None:
        return "(past EOF)"
    if not is_rel:
        return f"{v:#x} (no reloc)" if v else "NULL"
    return f"{v:#x}" if v else "NULL"


def _print_stage_node(arc: Archive, sn: int):
    print(f"\n  StageNode @ {sn:#x}:")
    print(f"    MachineAccel = {f32(arc.data, sn + 0x04):.4f}")
    print(f"    StageScale   = {f32(arc.data, sn + 0x08):.4f}")
    print(f"    UnkGravity   = {f32(arc.data, sn + 0x0C):.4f}")
    print(
        f"    Gravity      = ({f32(arc.data, sn + 0x10):.4f}, "
        f"{f32(arc.data, sn + 0x14):.4f}, {f32(arc.data, sn + 0x18):.4f})"
    )
    print(f"    FogFlags     = {u32(arc.data, sn + 0x1C):#x}")
    print(f"    Flags        = {u32(arc.data, sn + 0x80):#x}")
    print(f"    MinimapScale = {f32(arc.data, sn + 0x60):.4f}")
    print(
        f"    OoB min      = ({f32(arc.data, sn + 0xCC):.1f}, "
        f"{f32(arc.data, sn + 0xD0):.1f}, {f32(arc.data, sn + 0xD4):.1f})"
    )
    print(
        f"    OoB max      = ({f32(arc.data, sn + 0xD8):.1f}, "
        f"{f32(arc.data, sn + 0xDC):.1f}, {f32(arc.data, sn + 0xE0):.1f})"
    )


def _print_lobj(arc: Archive, off: int, indent: str):
    """Print a single LObjDesc node and recurse into its `next` chain."""
    seen = set()
    cur = off
    idx = 0
    while cur and cur not in seen:
        seen.add(cur)
        print(f"{indent}LObjDesc @ {cur:#x}{_detail(arc, 'LObjDesc', cur)}")
        for foff, label in [(0x10, "position"), (0x14, "interest")]:
            slot = cur + foff
            if slot in arc.reloc_set:
                p = u32(arc.data, slot)
                if p:
                    print(
                        f"{indent}  {label}: WObjDesc @ {p:#x}"
                        f"{_detail(arc, 'WObjDesc', p)}"
                    )
        nxt_slot = cur + 0x04
        if nxt_slot not in arc.reloc_set:
            break
        cur = u32(arc.data, nxt_slot)
        idx += 1


def _print_light_group(arc: Archive, lg: int):
    print(f"\n  LightGroup @ {lg:#x}:")
    for i, lname in enumerate(["Global", "Group1", "Group2"]):
        slot = lg + i * 4
        rel = slot in arc.reloc_set
        v = u32(arc.data, slot) if rel else 0
        if not v:
            print(f"    +{i * 4:02X}  {lname:7s} -> {_fmt_ptr(v, rel)}")
            continue
        print(f"    +{i * 4:02X}  {lname:7s} -> LightNode @ {v:#x}")
        # LightNode is a fixed array of four HSD_Light pointers.
        for j in range(4):
            light_slot = v + j * 4
            light_rel = light_slot in arc.reloc_set
            lp = u32(arc.data, light_slot) if light_rel else 0
            if not lp:
                continue
            lobj_slot = lp + 0x00
            lobj = u32(arc.data, lobj_slot) if lobj_slot in arc.reloc_set else 0
            print(
                f"        Light[{j}] @ {lp:#x} -> LObjDesc @ {lobj:#x}"
                if lobj
                else f"        Light[{j}] @ {lp:#x} (no LObj)"
            )
            if lobj:
                _print_lobj(arc, lobj, "          ")


def _print_fog_node(arc: Archive, fn: int):
    print(f"\n  FogNode @ {fn:#x}:")
    fog_data_slot = fn + 0x00
    fog_data_p = u32(arc.data, fog_data_slot) if fog_data_slot in arc.reloc_set else 0
    print(
        f"    +00  FogData  -> {_fmt_ptr(fog_data_p, fog_data_slot in arc.reloc_set)}"
    )
    if fog_data_p:
        # KAR_grFogData wraps an HSD_FogDesc at +0x00.
        desc_slot = fog_data_p + 0x00
        desc = u32(arc.data, desc_slot) if desc_slot in arc.reloc_set else 0
        if desc:
            print(f"      FogDesc @ {desc:#x}{_detail(arc, 'FogDesc', desc)}")
    types_slot = fn + 0x04
    types_p = u32(arc.data, types_slot) if types_slot in arc.reloc_set else 0
    print(f"    +04  FogTypes -> {_fmt_ptr(types_p, types_slot in arc.reloc_set)}")


def _print_position_node(arc: Archive, pn: int):
    POS_FIELDS = [
        (0x00, "PositionJoint"),
        (0x04, "Startpos"),
        (0x08, "Enemypos"),
        (0x0C, "Gravitypos"),
        (0x10, "Airflowpos"),
        (0x14, "Conveyorpos"),
        (0x18, "ItemPos"),
        (0x1C, "Eventpos"),
        (0x20, "Vehiclepos"),
        (0x24, "GlobalDeadPos"),
        (0x28, "LocalDeadPos"),
        (0x2C, "Yakumonopos"),
        (0x30, "ItemAreaPos"),
        (0x34, "VehicleAreapos"),
    ]
    nonnull = []
    for foff, fname in POS_FIELDS:
        v, rel = _ptr(arc, pn + foff)
        if v:
            nonnull.append((foff, fname, v, rel))
    if not nonnull:
        print(f"\n  PositionNode @ {pn:#x}: (all NULL)")
        return
    print(f"\n  PositionNode @ {pn:#x}: (NULL slots omitted)")
    for foff, fname, v, rel in nonnull:
        print(f"    +{foff:02X}  {fname:14s} -> {_fmt_ptr(v, rel)}")


def cmd_grdata(args):
    arc = _open_or_complain(args.path)
    if arc is None:
        return 1

    # Find a grData* public if one wasn't specified.
    sym = args.symbol
    if sym is None:
        for name in arc.publics:
            if classify_symbol(name) in ("KAR_grData", "KAR_grDataCommon"):
                sym = name
                print(f"# defaulting to {sym}")
                break
        if sym is None:
            print("no grData* public found", file=sys.stderr)
            return 1
    elif sym not in arc.publics:
        print(
            f"public symbol {sym!r} not found. Available: {list(arc.publics)}",
            file=sys.stderr,
        )
        return 1

    off = arc.publics[sym]
    klass = classify_symbol(sym)
    print(f"# {sym} @ {off:#x}  type={klass or '?'}")
    print("  fields:")
    for foff, name, typ, kind in GRDATA_FIELDS:
        v, rel = _ptr(arc, off + foff)
        if v is None:
            disp = "(past EOF)"
        elif kind == "i32":
            disp = f"= {v:#x}"
        elif kind == "runtime":
            disp = f"= {v:#x}" if v else "= 0"
            if rel:
                disp += "  (UNEXPECTED reloc)"
        else:  # ptr
            disp = f"-> {_fmt_ptr(v, rel)}"
        print(f"    +{foff:02X}  {name:18s} {typ:36s} {disp}")

    # Expand the few sub-nodes worth seeing inline.
    sn, sn_rel = _ptr(arc, off + 0x04)
    if sn and sn_rel:
        _print_stage_node(arc, sn)
    lg, lg_rel = _ptr(arc, off + 0x14)
    if lg and lg_rel:
        _print_light_group(arc, lg)
    fn, fn_rel = _ptr(arc, off + 0x34)
    if fn and fn_rel:
        _print_fog_node(arc, fn)
    pn, pn_rel = _ptr(arc, off + 0x20)
    if pn and pn_rel:
        _print_position_node(arc, pn)
    return 0


# --- find --------------------------------------------------------------


def cmd_find(args):
    pat = re.compile(args.pattern)
    paths = []
    for g in args.globs:
        paths.extend(glob.glob(g))
    if not paths:
        print(f"no files matched: {args.globs}")
        return 1
    paths.sort()

    want_pub = not args.externs_only
    want_ext = not args.publics_only

    hits = 0
    skipped = 0
    for path in paths:
        try:
            arc = Archive(path)
        except NotAnHSDArchive:
            skipped += 1
            continue
        except Exception as e:
            print(f"{path}: open failed: {e}", file=sys.stderr)
            continue
        if want_pub:
            for name, off in arc.publics.items():
                if pat.search(name):
                    klass = classify_symbol(name) or "?"
                    print(f"{path}  pub  {off:#08x}  {name}  [{klass}]")
                    hits += 1
        if want_ext:
            for off, name in arc.externs:
                if pat.search(name):
                    print(f"{path}  ext  {off:#08x}  {name}")
                    hits += 1
    suffix = f" ({skipped} skipped as non-HSD)" if skipped else ""
    print(f"\n# {hits} hit(s) across {len(paths)} file(s){suffix}")
    return 0


# --- entrypoint --------------------------------------------------------


def main(argv):
    p = argparse.ArgumentParser(
        prog="hsd/explore.py", description="HSD .dat archive explorer."
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    pls = sub.add_parser("ls", help="list header, publics, externs")
    pls.add_argument("path")
    pls.set_defaults(func=cmd_ls)

    ptr_ = sub.add_parser("tree", help="walk JObj tree from a public symbol")
    ptr_.add_argument("path")
    ptr_.add_argument("symbol", nargs="?", default=None)
    ptr_.add_argument(
        "--root-type",
        default=None,
        help="Type at the root (default: JOBJDesc, or auto for grModel*).",
    )
    ptr_.add_argument("--max-depth", type=int, default=None)
    ptr_.add_argument(
        "--no-summary",
        dest="summary",
        action="store_false",
        help="Skip the type/size summary footer.",
    )
    ptr_.set_defaults(func=cmd_tree, summary=True)

    pgd = sub.add_parser("grdata", help="decode a KAR_grData public")
    pgd.add_argument("path")
    pgd.add_argument("symbol", nargs="?", default=None)
    pgd.set_defaults(func=cmd_grdata)

    pfn = sub.add_parser("find", help="grep public/extern symbols across .dat files")
    pfn.add_argument("pattern")
    pfn.add_argument(
        "globs",
        nargs="*",
        default=["iso/files/*.dat"],
        help="File globs (default: iso/files/*.dat).",
    )
    grp = pfn.add_mutually_exclusive_group()
    grp.add_argument(
        "--publics-only", action="store_true", help="Skip extern symbol matches."
    )
    grp.add_argument(
        "--externs-only", action="store_true", help="Skip public symbol matches."
    )
    pfn.set_defaults(func=cmd_find)

    args = p.parse_args(argv[1:])
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
