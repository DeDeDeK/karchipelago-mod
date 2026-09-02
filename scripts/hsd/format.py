# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""Human-readable rendering of HSD records.

Holds the GX / HSD flag and enum tables (ported from HSDLib's
HSDRaw/Common/HSD_*.cs and HSDRaw/GX/Enums.cs) and `describe`, which
turns one record into the short suffix the tree printer and the grData
decoder append to a node line.
"""

import struct

from .archive import cstr, f32, s16, s32, u16, u32
from .gx import FORMAT_NAME, image_size
from .schema import array_length

# Single-bit JOBJ flags; bits 9-11 are the billboard mode and 21-22 the
# joint type, both decoded as fields below.
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
JOBJ_BILLBOARD = {1: "BILLBOARD", 2: "VBILLBOARD", 3: "HBILLBOARD", 4: "RBILLBOARD"}
JOBJ_JOINT = {1: "JOINT1", 2: "JOINT2", 3: "EFFECTOR"}

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
MOBJ_ALPHA = {1: "ALPHA_MAT", 2: "ALPHA_VTX", 3: "ALPHA_BOTH"}

POBJ_FLAGS = [
    (1 << 0, "SHAPESET_AVERAGE"),
    (1 << 1, "SHAPESET_ADDITIVE"),
    (1 << 3, "ANIM"),
    (1 << 12, "SHAPEANIM"),
    (1 << 13, "ENVELOPE"),
    (1 << 14, "CULLFRONT"),
    (1 << 15, "CULLBACK"),
]

TOBJ_FLAGS_SINGLE = [
    (1 << 4, "LIGHTMAP_DIFFUSE"),
    (1 << 5, "LIGHTMAP_SPECULAR"),
    (1 << 6, "LIGHTMAP_AMBIENT"),
    (1 << 7, "LIGHTMAP_EXT"),
    (1 << 8, "LIGHTMAP_SHADOW"),
    (1 << 24, "BUMP"),
    (1 << 31, "MTX_DIRTY"),
]
TOBJ_COORD = {
    0: "UV",
    1: "REFLECTION",
    2: "HILIGHT",
    3: "SHADOW",
    4: "TOON",
    5: "GRADATION",
}
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
TOBJ_ALPHAMAP = {
    1: "AM_ALPHA_MASK",
    2: "AM_BLEND",
    3: "AM_MODULATE",
    4: "AM_REPLACE",
    5: "AM_PASS",
    6: "AM_ADD",
    7: "AM_SUB",
}

# GX enums (HSDRaw/GX/Enums.cs).
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
GX_TEX_FILTER = {
    0: "NEAR",
    1: "LINEAR",
    2: "NEAR_MIP_NEAR",
    3: "LIN_MIP_NEAR",
    4: "NEAR_MIP_LIN",
    5: "LIN_MIP_LIN",
}
GX_BRIGHTNESS_DIST = {0: "GENTLE", 1: "MEDIUM", 2: "STEEP", 3: "SHARP"}
GX_SPOT_FUNC = {
    0: "OFF",
    1: "FLAT",
    2: "COS",
    3: "COS2",
    4: "SHARP",
    5: "RING1",
    6: "RING2",
}

PE_FLAGS = [
    (1 << 0, "COLOR_UPDATE"),
    (1 << 1, "ALPHA_UPDATE"),
    (1 << 2, "DST_ALPHA"),
    (1 << 3, "BEFORE_TEX"),
    (1 << 4, "COMPARE"),
    (1 << 5, "ZUPDATE"),
    (1 << 6, "DITHER"),
]

# LOBJ_Flags (HSDLib HSD_LOBJ.cs). Low 2 bits are the light type.
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

CAMERA_PROJ = {1: "PERSPECTIVE", 2: "FRUSTRUM", 3: "ORTHO"}

# AOBJ_Flags (HSDLib HSD_AOBJ.cs). Low 24 bits hold the runtime frame
# counter; only the named flags are stored on disc.
AOBJ_FLAGS = [
    (1 << 26, "REWINDED"),
    (1 << 27, "FIRST_PLAY"),
    (1 << 28, "NO_UPDATE"),
    (1 << 29, "LOOP"),
    (1 << 30, "NO_ANIM"),
]

# GXAnimDataFormat (HSDLib HSD_FOBJ.cs): top 3 bits of the value/tangent
# flag byte; the bottom 5 are log2 of the scale.
ANIM_FMT = {0x00: "FLOAT", 0x20: "S16", 0x40: "U16", 0x60: "S8", 0x80: "U8"}

# HSD_Spline.Type (HSDLib HSD_Spline.cs).
SPLINE_TYPES = {0: "LINEAR", 1: "BEZIER", 2: "BSPLINE", 3: "TENSION"}

# KAR_CollisionJoint.Kind (HSDLib KdMeshKind).
COLL_JOINT_KINDS = {
    0: "BASIC",
    1: "CONVEYOR_UNUSED",
    2: "CONVEYOR",
    3: "BREAKABLE",
    4: "MOVING_TRANSLATION",
    5: "MOVING_ROTATION",
}

# KAR_CollisionTriangle +0x0C low nibble (HSDLib KCCollFlag).
COLL_TRI_FLAGS = [
    (1 << 0, "FLOOR"),
    (1 << 1, "WALL"),
    (1 << 2, "CEILING"),
    (1 << 3, "UNK"),
]

# KAR_ZoneCollisionTriangle.Kind (HSDLib KdZone.ZoneKind), the low 25 bits
# of the triangle's +0x10 word.
ZONE_KINDS = [
    "GroundedBoost",
    "GroundedBoostForce",
    "DashGate1",
    "DashGate2",
    "DashRing",
    "WarpIn",
    "WarpOut",
    "SuperJump",
    "SuperJumpCameraTrailer",
    "Leap",
    "Spin",
    "Airflow",
    "SwitchGrounded",
    "SwitchRing",
    "SwitchArea",
    "RandomAbility",
    "FreeMovement",
    "DownForce",
    "ClawStart",
    "ClawEnd",
    "Unknown20",
    "Unknown21",
    "Canon",
    "ClawTarget",
    "Unknown24",
    "DeathPlane",
    "Unknown26",
    "Unknown27",
    "Unknown28",
    "Unknown29",
    "Unknown30",
    "PlaySound",
    "LightArea",
    "Heal",
    "Reverb",
    "Unknown35",
]


def zone_kind_name(kind):
    return ZONE_KINDS[kind] if 0 <= kind < len(ZONE_KINDS) else f"Kind{kind}"


def _bits(value, table):
    names = [n for bit, n in table if value & bit]
    return " ".join(names) if names else "-"


def decode_jobj_flags(v):
    names = [n for bit, n in JOBJ_FLAGS_SINGLE if v & bit]
    bb = (v >> 9) & 0x7
    if bb in JOBJ_BILLBOARD:
        names.append(JOBJ_BILLBOARD[bb])
    jt = (v >> 21) & 0x3
    if jt in JOBJ_JOINT:
        names.append(JOBJ_JOINT[jt])
    return " ".join(names) if names else "-"


def decode_mobj_flags(v):
    names = [n for bit, n in MOBJ_RENDER_FLAGS if v & bit]
    al = (v >> 13) & 0x3
    if al in MOBJ_ALPHA:
        names.append(MOBJ_ALPHA[al])
    return " ".join(names) if names else "-"


def decode_tobj_flags(v):
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


def decode_lobj_flags(v):
    parts = [LOBJ_TYPES.get(v & 0x3, "?")]
    parts.extend(n for bit, n in LOBJ_FLAGS if v & bit)
    return " ".join(parts)


def decode_anim_flag(b):
    """(format_name, scale_pow2) for a FOBJ value/tangent flag byte."""
    return ANIM_FMT.get(b & 0xE0, f"?{b & 0xE0:#x}"), b & 0x1F


def rgba(arc, off):
    d = arc.data
    return f"#{d[off]:02X}{d[off + 1]:02X}{d[off + 2]:02X}{d[off + 3]:02X}"


def vec3(arc, off):
    return (
        f"({f32(arc.data, off):.2f}, {f32(arc.data, off + 4):.2f}, "
        f"{f32(arc.data, off + 8):.2f})"
    )


def name_at(arc, struct_off, name_field_off=0x00):
    """Deref the class-name pointer at struct_off+name_field_off, if any."""
    if (struct_off + name_field_off) not in arc.reloc_set:
        return ""
    p = u32(arc.data, struct_off + name_field_off)
    return cstr(arc.data, p) if p else ""


def _named(arc, off, *bits):
    nm = name_at(arc, off)
    parts = ([f'"{nm}"'] if nm else []) + [b for b in bits if b]
    return (" " + " ".join(parts)) if parts else ""


def _count_at(arc, off, width=4):
    return (u16 if width == 2 else u32)(arc.data, off)


def _has_ptr(arc, off):
    return off in arc.reloc_set and u32(arc.data, off) != 0


def describe(arc, typ, off):
    """One-line summary of the record of type `typ` at data offset `off`,
    or "" for types with nothing worth saying. The leading space is part
    of the return value so callers can append it directly."""
    fn = _DESCRIBERS.get(typ)
    if fn is None or off + 4 > len(arc.data):
        return ""
    try:
        return fn(arc, off)
    except (struct.error, IndexError):
        return " (truncated)"


def _d_jobj(arc, off):
    flags = u32(arc.data, off + 0x04)
    return _named(arc, off, f"flags={flags:#010x} [{decode_jobj_flags(flags)}]")


def _d_mobj(arc, off):
    rf = u32(arc.data, off + 0x04)
    return _named(arc, off, f"render={rf:#010x} [{decode_mobj_flags(rf)}]")


def _d_tobj(arc, off):
    flags = u32(arc.data, off + 0x40)
    return _named(
        arc,
        off,
        f"map=TEXMAP{u32(arc.data, off + 0x08)}",
        f"flags={flags:#010x} [{decode_tobj_flags(flags)}]",
    )


def _d_pobj(arc, off):
    flags = u16(arc.data, off + 0x0C)
    return (
        f" flags={flags:#06x} [{_bits(flags, POBJ_FLAGS)}]"
        f" dl={u16(arc.data, off + 0x0E) * 32}B"
    )


def _d_image(arc, off):
    w, h = u16(arc.data, off + 4), u16(arc.data, off + 6)
    fmt = u32(arc.data, off + 8)
    return f" {w}x{h} {FORMAT_NAME.get(fmt, fmt)} ({image_size(w, h, fmt)} B)"


def _d_iobj(arc, off):
    w, h = u16(arc.data, off + 0x00), u16(arc.data, off + 0x02)
    fmt = u32(arc.data, off + 0x04)
    return f" {w}x{h} {FORMAT_NAME.get(fmt, fmt)} ({image_size(w, h, fmt)} B)"


def _d_tlut(arc, off):
    n = u16(arc.data, off + 0x0C)
    return f" n={n} ({n * 2} B)"


def _d_material(arc, off):
    return (
        f" amb={rgba(arc, off + 0x00)} dif={rgba(arc, off + 0x04)}"
        f" spc={rgba(arc, off + 0x08)} alpha={f32(arc.data, off + 0x0C):.2f}"
        f" shine={f32(arc.data, off + 0x10):.2f}"
    )


def _d_pe(arc, off):
    d = arc.data
    return (
        f" [{_bits(d[off], PE_FLAGS)}]"
        f" blend={GX_BLEND_MODE.get(d[off + 4], d[off + 4])}"
        f" src={GX_BLEND_FACTOR.get(d[off + 5], d[off + 5])}"
        f" dst={GX_BLEND_FACTOR.get(d[off + 6], d[off + 6])}"
        f" depth={GX_COMPARE.get(d[off + 8], d[off + 8])}"
        f" aref=({d[off + 1]},{d[off + 2]}) dstA={d[off + 3]}"
    )


def _d_robj(arc, off):
    flags = u32(arc.data, off + 0x04)
    rt_name = ROBJ_REFTYPES.get((flags >> 28) & 0x7, str((flags >> 28) & 0x7))
    bits = [f"ref={rt_name}"]
    if rt_name == "LIMIT":
        limit_id = flags & 0xFFFFFF
        bits.append(f"limit={ROBJ_LIMIT_NAMES.get(limit_id, str(limit_id))}")
    return " " + " ".join(bits) + f" flags={flags:#010x}"


def _d_lobj(arc, off):
    flags = u16(arc.data, off + 0x08)
    attn = u16(arc.data, off + 0x0A)
    out = f" color={rgba(arc, off + 0x0C)} [{decode_lobj_flags(flags)}"
    out += " ATTN]" if attn & 1 else "]"
    return out + _d_lobj_attn(arc, off, flags, attn)


def _d_lobj_attn(arc, off, flags, attn):
    """The +0x18 attenuation block: a raw 6-float GX attenuation record
    when LOBJ_RAW_PARAM is set, otherwise the point/spot parameters for
    the light type in the low two flag bits (HSDLib HSD_LOBJ.cs)."""
    if not _has_ptr(arc, off + 0x18):
        return ""
    p = u32(arc.data, off + 0x18)
    if flags & (1 << 6):  # LOBJ_RAW_PARAM
        a = [f32(arc.data, p + i * 4) for i in range(6)]
        return (
            f" attn a=({a[0]:.3g},{a[1]:.3g},{a[2]:.3g})"
            f" k=({a[3]:.3g},{a[4]:.3g},{a[5]:.3g})"
        )
    kind = flags & 0x3
    if kind == 1:  # INFINITE: a single float
        return f" attn={f32(arc.data, p):.3g}"
    if kind == 2:  # POINT
        return (
            f" point ref_br={f32(arc.data, p):.3g}"
            f" ref_dist={f32(arc.data, p + 4):.3g}"
            f" dist={GX_BRIGHTNESS_DIST.get(u32(arc.data, p + 8), '?')}"
        )
    if kind == 3:  # SPOT
        return (
            f" spot cutoff={f32(arc.data, p):.3g}"
            f" func={GX_SPOT_FUNC.get(u32(arc.data, p + 4), '?')}"
            f" ref_br={f32(arc.data, p + 8):.3g}"
            f" ref_dist={f32(arc.data, p + 0x0C):.3g}"
            f" dist={GX_BRIGHTNESS_DIST.get(u32(arc.data, p + 0x10), '?')}"
        )
    return ""


def _d_wobj(arc, off):
    return f" pos={vec3(arc, off + 0x04)}"


def _d_fog(arc, off):
    ft = u32(arc.data, off + 0x00)
    return (
        f" type={FOG_TYPES.get(ft, str(ft))}"
        f" near={f32(arc.data, off + 0x08):.1f}"
        f" far={f32(arc.data, off + 0x0C):.1f}"
        f" color={rgba(arc, off + 0x10)}"
    )


def _d_texlod(arc, off):
    minf = u32(arc.data, off + 0x00)
    return f" min={GX_TEX_FILTER.get(minf, minf)} bias={f32(arc.data, off + 0x04):+.2f}"


def _d_tev(arc, off):
    return (
        f" color_op={arc.data[off]} alpha_op={arc.data[off + 1]}"
        f" active={u32(arc.data, off + 0x1C):#x}"
    )


def _d_spline(arc, off):
    t = arc.data[off]
    return (
        f" type={SPLINE_TYPES.get(t, t)} pts={u16(arc.data, off + 0x02)}"
        f" tension={f32(arc.data, off + 0x04):.2f}"
        f" len={f32(arc.data, off + 0x0C):.2f}"
    )


def _d_sobj(arc, off):
    def n(slot):
        return (
            array_length(arc, u32(arc.data, off + slot))
            if _has_ptr(arc, off + slot)
            else 0
        )

    return f" models={n(0x00)} cameras={n(0x04)} lights={n(0x08)}"


def _d_camera(arc, off):
    proj = u16(arc.data, off + 0x06)
    vl, vr, vt, vb = struct.unpack(">4h", arc.data[off + 0x08 : off + 0x10])
    bits = [
        f"proj={CAMERA_PROJ.get(proj, str(proj))}",
        f"view=({vl},{vt})-({vr},{vb})",
        f"clip={f32(arc.data, off + 0x28):.1f}..{f32(arc.data, off + 0x2C):.1f}",
    ]
    if proj == 1:  # PERSPECTIVE
        bits.append(
            f"fov={f32(arc.data, off + 0x30):.1f}"
            f" aspect={f32(arc.data, off + 0x34):.3f}"
        )
    return _named(arc, off, *bits)


def _d_mainmodel(arc, off):
    return (
        f" jobj={u32(arc.data, off + 0x04)} dobj={u32(arc.data, off + 0x08)}"
        f" pobj={u32(arc.data, off + 0x0C)}"
    )


def _d_bounding(arc, off):
    return (
        f" viewregions={u16(arc.data, off + 0x04)}"
        f" dynbbox={u16(arc.data, off + 0x0C)}"
        f" statbbox={u16(arc.data, off + 0x14)}"
        f" indices={u16(arc.data, off + 0x1C)}"
    )


def _d_fog_anim(arc, off):
    return " [anim]" if _has_ptr(arc, off + 0x04) else ""


def _d_aobj(arc, off):
    flags = u32(arc.data, off + 0x00)
    n = 0
    cur = u32(arc.data, off + 0x08) if _has_ptr(arc, off + 0x08) else 0
    while cur and n < 256:  # cap in case of a malformed cycle
        n += 1
        if cur not in arc.reloc_set:
            break
        cur = u32(arc.data, cur)
    return (
        f" flags={flags:#010x} [{_bits(flags, AOBJ_FLAGS)}]"
        f" end={f32(arc.data, off + 0x04):.1f} tracks={n}"
    )


def _d_fobjdesc(arc, off):
    # The track-type byte's enum is context-dependent (Fog / Joint / Mat /
    # Tex / Light / Shape track), so it prints raw.
    vfmt, vscale = decode_anim_flag(arc.data[off + 0x0D])
    tfmt, tscale = decode_anim_flag(arc.data[off + 0x0E])
    return (
        f" track={arc.data[off + 0x0C]} start={f32(arc.data, off + 0x08):.1f}"
        f" len={u32(arc.data, off + 0x04)}B"
        f" v={vfmt}/2^{vscale} t={tfmt}/2^{tscale}"
    )


def _d_fobj(arc, off):
    vfmt, vscale = decode_anim_flag(arc.data[off + 0x01])
    tfmt, tscale = decode_anim_flag(arc.data[off + 0x02])
    return f" track={arc.data[off]} v={vfmt}/2^{vscale} t={tfmt}/2^{tscale}"


def _d_animjoint(arc, off):
    flags = u32(arc.data, off + 0x10)
    return f" flags={flags:#010x}{' [aobj]' if _has_ptr(arc, off + 0x08) else ''}"


def _d_texanim(arc, off):
    return (
        f" map=TEXMAP{u32(arc.data, off + 0x04)}"
        f" images={u16(arc.data, off + 0x14)} tluts={u16(arc.data, off + 0x16)}"
    )


def _d_figatree(arc, off):
    nodes = tracks = 0
    if _has_ptr(arc, off + 0x0C):
        tbl = u32(arc.data, off + 0x0C)
        while tbl + nodes < len(arc.data) and arc.data[tbl + nodes] != 0xFF:
            tracks += arc.data[tbl + nodes]
            nodes += 1
    return (
        f" type={u32(arc.data, off)} frames={f32(arc.data, off + 0x08):.1f}"
        f" nodes={nodes} tracks={tracks}"
    )


def _d_shapeset(arc, off):
    return (
        f" flags={u16(arc.data, off):#06x} shapes={u16(arc.data, off + 0x02)}"
        f" v_idx={u32(arc.data, off + 0x04)} n_idx={u32(arc.data, off + 0x10)}"
    )


def _d_ptclgroup(arc, off):
    return (
        f" unk=({u16(arc.data, off)},{u16(arc.data, off + 0x02)})"
        f" effect_id={u32(arc.data, off + 0x04):#x}"
        f" generators={u32(arc.data, off + 0x08)}"
    )


def _d_envelope(arc, off):
    n = 0
    while off + n * 8 + 8 <= len(arc.data) and (off + n * 8) in arc.reloc_set:
        n += 1
    return f" entries={n}"


def _d_collision(arc, off):
    return (
        f" verts={_count_at(arc, off + 0x04)} tris={_count_at(arc, off + 0x0C)}"
        f" joints={_count_at(arc, off + 0x14)}"
        f" zone_verts={_count_at(arc, off + 0x1C)}"
        f" zone_tris={_count_at(arc, off + 0x24)}"
        f" zone_joints={_count_at(arc, off + 0x2C)}"
    )


def _d_colljoint(arc, off):
    return (
        f" bone={s32(arc.data, off):d} kind={u32(arc.data, off + 0x14):d}"
        f" verts={u32(arc.data, off + 0x04)}+{u32(arc.data, off + 0x08)}"
        f" faces={u32(arc.data, off + 0x0C)}+{u32(arc.data, off + 0x10)}"
    )


def _d_zonejoint(arc, off):
    return (
        f" bone={s32(arc.data, off):d} link={s32(arc.data, off + 0x14):d}"
        f" verts={u32(arc.data, off + 0x04)}+{u32(arc.data, off + 0x08)}"
        f" faces={u32(arc.data, off + 0x0C)}+{u32(arc.data, off + 0x10)}"
    )


def _d_colltree(arc, off):
    return (
        f" buckets={u16(arc.data, off + 0x04)}"
        f" coll_tris={u16(arc.data, off + 0x10)}"
        f" zone_idx={u16(arc.data, off + 0x1C)}"
        f" rough={u16(arc.data, off + 0x28)}"
    )


def _d_bucket(arc, off):
    c1, c2 = s16(arc.data, off + 0x18), s16(arc.data, off + 0x1A)
    kind = "leaf" if c1 < 0 else f"branch({c1},{c2})"
    return (
        f" {kind} depth={arc.data[off + 0x4C]}"
        f" tris={u16(arc.data, off + 0x1C)}+{u16(arc.data, off + 0x1E)}"
        f" zones={u16(arc.data, off + 0x24)}+{u16(arc.data, off + 0x26)}"
    )


def _d_splinelist(arc, off):
    return f" count={u32(arc.data, off + 0x04)}"


def _d_rangesetup(arc, off):
    return f" count={u32(arc.data, off + 0x04)}"


def _d_rangespline(arc, off):
    return f" flags={u32(arc.data, off + 0x14):#010x} x10={u32(arc.data, off + 0x10)}"


def _d_poslist(arc, off):
    kind = "joints" if _has_ptr(arc, off) else "inline"
    return f" count={u32(arc.data, off + 0x08)} ({kind})"


def _d_railcollnode(arc, off):
    return f" count={u32(arc.data, off + 0x04)}"


def _d_railcoll(arc, off):
    return (
        f" spline={s32(arc.data, off + 0x04):d}"
        f" len_spline={s32(arc.data, off + 0x08):d}"
        f" subanim={s32(arc.data, off + 0x0C):d}"
        f" next=({s32(arc.data, off + 0x10):d},{s32(arc.data, off + 0x14):d},"
        f"{s32(arc.data, off + 0x18):d}) prev={s32(arc.data, off + 0x1C):d}"
    )


def _d_railparam(arc, off):
    return (
        f" flags={u32(arc.data, off + 0x04):#x} data={u32(arc.data, off + 0x18)}"
        f" dash={u32(arc.data, off + 0x20)}/{u32(arc.data, off + 0x28)}"
        f" leap={u32(arc.data, off + 0x30)}"
    )


def _d_fgmnode(arc, off):
    return (
        f" positional={u32(arc.data, off + 0x04)} triggered={u32(arc.data, off + 0x0C)}"
    )


def _d_fgmentry(arc, off):
    return (
        f" sounds={u32(arc.data, off + 0x04)} type={u32(arc.data, off + 0x0C)}"
        f" dist={f32(arc.data, off + 0x10):.1f}"
    )


def _d_yakumononode(arc, off):
    return f" entries={u32(arc.data, off + 0x04)}"


def _d_yakumonoaudio(arc, off):
    return (
        f" sounds={u32(arc.data, off + 0x04)} kind={u32(arc.data, off + 0x08)}"
        f" prox={u32(arc.data, off + 0x0C)} vol={f32(arc.data, off + 0x10):.2f}"
    )


def _d_hurt(arc, off):
    return f" hurtboxes={u32(arc.data, off + 0x04)}"


def _d_respawn(arc, off):
    return f" count={u32(arc.data, off + 0x04)}"


def _d_subanim(arc, off):
    return f" count={u32(arc.data, off + 0x04)}"


def _d_itemnode(arc, off):
    have = [
        n
        for n, s in (
            ("timing", 0x04),
            ("citytrial", 0x08),
            ("airride", 0x0C),
            ("coliseum", 0x10),
        )
        if _has_ptr(arc, off + s)
    ]
    return f" [{' '.join(have) or '-'}]"


def _d_typedata(arc, off):
    return f" entries={u32(arc.data, off + 0x04)}"


def _d_lightgroup(arc, off):
    lights = 0
    for i in range(3):
        node = u32(arc.data, off + i * 4) if _has_ptr(arc, off + i * 4) else 0
        if node:
            lights += sum(1 for j in range(4) if _has_ptr(arc, node + j * 4))
    return f" lights={lights}"


def _d_splinenode(arc, off):
    def deref(p):
        return u32(arc.data, p) if _has_ptr(arc, p) else 0

    def count(p):
        # Both grSplineList and grRangeSplineSetup keep their count at +0x04.
        return u32(arc.data, p + 0x04) if p else 0

    setup = deref(off + 0x00)
    return (
        f" course={count(deref(setup)) if setup else 0}"
        f" cpu_range={count(deref(off + 0x04))}"
        f" conveyor={'y' if deref(off + 0x10) else 'n'}"
        f" rail={count(deref(off + 0x14))}"
        f" heavy={count(deref(off + 0x18))}"
    )


def _d_positionnode(arc, off):
    total = 0
    used = 0
    for slot in range(0x04, 0x38, 4):
        lst = u32(arc.data, off + slot) if _has_ptr(arc, off + slot) else 0
        if lst:
            used += 1
            total += u32(arc.data, lst + 0x08)
    return f" lists={used} positions={total}"


def _d_subanimnode(arc, off):
    counts = []
    for i in range(6):
        sa = u32(arc.data, off + i * 4) if _has_ptr(arc, off + i * 4) else 0
        counts.append(str(u32(arc.data, sa + 0x04)) if sa else "-")
    return " anims=[" + ",".join(counts) + "]"


def _d_fognode(arc, off):
    types = u32(arc.data, off + 0x04) if _has_ptr(arc, off + 0x04) else 0
    return f" fogtypes={u32(arc.data, types + 0x04) if types else 0}"


def _d_treenode(arc, off):
    tree = u32(arc.data, off) if _has_ptr(arc, off) else 0
    return _d_colltree(arc, tree) if tree else " (no tree)"


def _d_stagenode(arc, off):
    return (
        f" scale={f32(arc.data, off + 0x08):.3f}"
        f" accel={f32(arc.data, off + 0x04):.3f}"
        f" gravity={f32(arc.data, off + 0x0C):.4f} {vec3(arc, off + 0x10)}"
    )


_DESCRIBERS = {
    "JOBJDesc": _d_jobj,
    "DObjDesc": lambda arc, off: _named(arc, off),
    "MObjDesc": _d_mobj,
    "TObjDesc": _d_tobj,
    "POBJDesc": _d_pobj,
    "ImageDesc": _d_image,
    "IOBJDesc": _d_iobj,
    "TlutDesc": _d_tlut,
    "MaterialDesc": _d_material,
    "PEDesc": _d_pe,
    "RObjDesc": _d_robj,
    "LObjDesc": _d_lobj,
    "WObjDesc": _d_wobj,
    "FogDesc": _d_fog,
    "TexLODDesc": _d_texlod,
    "TObjTev": _d_tev,
    "Spline": _d_spline,
    "SOBJ": _d_sobj,
    "Camera": _d_camera,
    "MainModel": _d_mainmodel,
    "ModelBounding": _d_bounding,
    "FogAnim": _d_fog_anim,
    "AOBJ": _d_aobj,
    "FOBJDesc": _d_fobjdesc,
    "FOBJ": _d_fobj,
    "AnimJoint": _d_animjoint,
    "TexAnim": _d_texanim,
    "FigaTree": _d_figatree,
    "ShapeSet": _d_shapeset,
    "ParticleGroup": _d_ptclgroup,
    "Envelope": _d_envelope,
    "grSubAnim": _d_subanim,
    "grSubAnimNode": _d_subanimnode,
    "grStageNode": _d_stagenode,
    "LightGroup": _d_lightgroup,
    "grSplineNode": _d_splinenode,
    "grPositionNode": _d_positionnode,
    "grFogNode": _d_fognode,
    "grCollisionTreeNode": _d_treenode,
    "grCollisionNode": _d_collision,
    "grCollisionJoint": _d_colljoint,
    "grZoneCollisionJoint": _d_zonejoint,
    "grCollisionTree": _d_colltree,
    "grPartitionBucket": _d_bucket,
    "grSplineList": _d_splinelist,
    "grRangeSplineSetup": _d_rangesetup,
    "grRangeSpline": _d_rangespline,
    "grPositionList": _d_poslist,
    "grAreaPositionList": _d_poslist,
    "grRailCollNode": _d_railcollnode,
    "grRailColl": _d_railcoll,
    "grRailParam": _d_railparam,
    "grFGMNode": _d_fgmnode,
    "grFGMNodeEntry": _d_fgmentry,
    "grItemNode": _d_itemnode,
    "grTypeData": _d_typedata,
    "YakumonoNode": _d_yakumononode,
    "YakumonoAudio": _d_yakumonoaudio,
    "HurtCollision": _d_hurt,
    "grRespawnNode": _d_respawn,
}
