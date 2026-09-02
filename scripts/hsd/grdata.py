# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""Decoder for a stage archive's `KAR_grData` public.

`grData` is the root of everything non-geometry about a stage: physics
scalars, lighting, collision + zones, splines, spawn positions, item
tables, rails, audio triggers, yakumono (gimmick) props and the
collision partition tree. Field offsets follow HSDLib's
HSDRaw/AirRide/Gr/Data accessors.

`print_grdata` prints the root field table with a one-line summary per
sub-node, then the detail for whichever sections the caller asked to
expand.
"""

from .archive import f32, s32, u16, u32
from .format import COLL_JOINT_KINDS, SPLINE_TYPES, describe, rgba, vec3, zone_kind_name
from .schema import array_length

# (offset, name, HSDLib type, kind). kind: 'ptr' relocated reference,
# 'i32' raw integer, 'runtime' a pointer slot the engine fills in at load
# time (never relocated on disc).
GRDATA_FIELDS = [
    (0x00, "Unknown1", "i32", "i32"),
    (0x04, "StageNode", "KAR_grStageNode", "ptr"),
    (0x08, "StageParam", "HSDAccessor", "ptr"),
    (0x0C, "model", "JOBJ* runtime", "runtime"),
    (0x10, "model_anim", "AObj* runtime", "runtime"),
    (0x14, "LightNode", "KAR_grLightGroup", "ptr"),
    (0x18, "CollisionNode", "KAR_grCollisionNode", "ptr"),
    (0x1C, "SplineNode", "KAR_grSplineNode", "ptr"),
    (0x20, "PositionNode", "KAR_grPositionNode", "ptr"),
    (0x24, "SubAnimNode", "KAR_grSubAnimNode", "ptr"),
    (0x28, "EnemyNode", "HSDAccessor", "ptr"),
    (0x2C, "ItemNode", "KAR_grItemNode", "ptr"),
    (0x30, "city_event", "runtime", "runtime"),
    (0x34, "FogNode", "KAR_grFogNode", "ptr"),
    (0x38, "RailCollNode", "KAR_grRailCollNode", "ptr"),
    (0x3C, "FGMNode", "KAR_grFGMNode", "ptr"),
    (0x40, "YakumonoNode", "KAR_YakumonoNode", "ptr"),
    (0x44, "ReplayNode", "HSDAccessor", "ptr"),
    (0x48, "PartitionNode", "KAR_grCollisionTreeNode", "ptr"),
    (0x4C, "RespawnNode", "KAR_grRespawnNode", "ptr"),
    (0x50, "StadiumNode", "HSDAccessor", "ptr"),
]

# grData slot -> (section name, schema type) for the summary column and
# the --expand switch.
_SECTIONS = {
    0x04: ("stage", "grStageNode"),
    0x14: ("lights", "LightGroup"),
    0x18: ("collision", "grCollisionNode"),
    0x1C: ("splines", "grSplineNode"),
    0x20: ("positions", "grPositionNode"),
    0x24: ("subanim", "grSubAnimNode"),
    0x2C: ("items", "grItemNode"),
    0x34: ("fog", "grFogNode"),
    0x38: ("rails", "grRailCollNode"),
    0x3C: ("fgm", "grFGMNode"),
    0x40: ("yakumono", "YakumonoNode"),
    0x48: ("partition", "grCollisionTreeNode"),
    0x4C: ("respawn", "grRespawnNode"),
}

SECTION_NAMES = [name for name, _ in _SECTIONS.values()]

# KAR_grSubAnimNode slot names (HSDLib KAR_grSubAnimNode.cs).
SUBANIM_SLOTS = ["SuperJump", "Leap", "Rail", "x0C", "x10", "EventAnim"]

POSITION_SLOTS = [
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


def ptr(arc, off):
    """(value, is_reloc) at `off`, or (None, False) past the data end."""
    if off + 4 > len(arc.data):
        return None, False
    return u32(arc.data, off), off in arc.reloc_set


def _deref(arc, off):
    """The relocated pointer at `off`, or 0."""
    v, rel = ptr(arc, off)
    return v if (rel and v) else 0


def _fmt_ptr(v, is_rel):
    if v is None:
        return "(past EOF)"
    if not is_rel:
        return f"{v:#x} (no reloc)" if v else "NULL"
    return f"{v:#x}" if v else "NULL"


def _floats(arc, off, n):
    return ", ".join(f"{f32(arc.data, off + i * 4):g}" for i in range(n))


def print_grdata(arc, off, expand=()):
    """Print the grData at `off`. `expand` names sections to detail
    (see SECTION_NAMES), or 'all'."""
    want = set(expand)
    if "all" in want:
        want = set(SECTION_NAMES)

    print("  fields:")
    for foff, name, typ, kind in GRDATA_FIELDS:
        v, rel = ptr(arc, off + foff)
        if v is None:
            disp = "(past EOF)"
        elif kind == "i32":
            disp = f"= {v:#x}"
        elif kind == "runtime":
            disp = f"= {v:#x}" if v else "= 0"
            if rel:
                disp += "  (UNEXPECTED reloc)"
        else:
            disp = f"-> {_fmt_ptr(v, rel)}"
            if v and rel and foff in _SECTIONS:
                disp += describe(arc, _SECTIONS[foff][1], v)
        print(f"    +{foff:02X}  {name:14s} {typ:24s} {disp}")

    for foff, (section, _) in _SECTIONS.items():
        node = _deref(arc, off + foff)
        if node and section in want:
            _PRINTERS[section](arc, node)


def _stage(arc, sn):
    print(f"\n  StageNode @ {sn:#x}:")
    fog = arc.data[sn + 0x1C]
    print(f"    MachineAccel     = {f32(arc.data, sn + 0x04):.4f}")
    print(f"    StageScale       = {f32(arc.data, sn + 0x08):.4f}")
    print(f"    GravityStrength  = {f32(arc.data, sn + 0x0C):.4f}")
    print(f"    GravityDirection = {vec3(arc, sn + 0x10)}")
    print(
        f"    Fog              = map={'on' if fog & 1 else 'off'} "
        f"player={'on' if fog & 2 else 'off'}"
    )
    print(f"    ItemRestitution  = [{_floats(arc, sn + 0x20, 8)}]")
    print(f"    PlayerRestitution= [{_floats(arc, sn + 0x40, 8)}]")
    print(f"    MinimapScale     = {f32(arc.data, sn + 0x60):.4f}")
    print(f"    MinimapPlayer    = {vec3(arc, sn + 0x64)}")
    print(
        f"    AudioFlags       = ({arc.data[sn + 0x80]}, {arc.data[sn + 0x81]}, "
        f"{arc.data[sn + 0x82]})"
    )
    for base, label in (
        (0x84, "BoostPads"),
        (0x9C, "BoostGates"),
        (0xB4, "BoostRings"),
    ):
        pairs = "  ".join(
            f"[{i}] accel=({_floats(arc, sn + base + i * 0xC, 2)}) "
            f"time={f32(arc.data, sn + base + i * 0xC + 8):g}"
            for i in range(2)
        )
        print(f"    {label:17s}= {pairs}")
    print(f"    OoB min          = {vec3(arc, sn + 0xCC)}")
    print(f"    OoB max          = {vec3(arc, sn + 0xD8)}")
    pad = _deref(arc, sn + 0xE4)
    if pad:
        cnt = _deref(arc, pad)
        if cnt:
            print(
                f"    PadCount         = ({u32(arc.data, cnt):d}, "
                f"{u32(arc.data, cnt + 4):d})"
            )


def _lights(arc, lg):
    print(f"\n  LightGroup @ {lg:#x}:")
    for i, lname in enumerate(("Global", "Group1", "Group2")):
        node = _deref(arc, lg + i * 4)
        if not node:
            continue
        print(f"    +{i * 4:02X}  {lname:7s} -> LightNode @ {node:#x}")
        for j in range(4):
            light = _deref(arc, node + j * 4)
            if not light:
                continue
            lobj = _deref(arc, light)
            print(
                f"        Light[{j}] @ {light:#x}"
                + (f" -> LObjDesc @ {lobj:#x}" if lobj else " (no LObj)")
            )
            _lobj_chain(arc, lobj, "          ")


def _lobj_chain(arc, off, indent):
    seen = set()
    cur = off
    while cur and cur not in seen:
        seen.add(cur)
        print(f"{indent}LObjDesc @ {cur:#x}{describe(arc, 'LObjDesc', cur)}")
        for foff, label in ((0x10, "position"), (0x14, "interest")):
            p = _deref(arc, cur + foff)
            if p:
                print(
                    f"{indent}  {label}: WObjDesc @ {p:#x}"
                    f"{describe(arc, 'WObjDesc', p)}"
                )
        cur = _deref(arc, cur + 0x04)


def _collision(arc, cn):
    print(f"\n  CollisionNode @ {cn:#x}:{describe(arc, 'grCollisionNode', cn)}")
    joints = _deref(arc, cn + 0x10)
    for i in range(u32(arc.data, cn + 0x14)):
        j = joints + i * 0x1C
        kind = u32(arc.data, j + 0x14)
        force = _deref(arc, j + 0x18)
        line = (
            f"    joint[{i:3d}] bone={s32(arc.data, j):3d} "
            f"kind={COLL_JOINT_KINDS.get(kind, kind)} "
            f"verts={u32(arc.data, j + 0x04)}+{u32(arc.data, j + 0x08)} "
            f"faces={u32(arc.data, j + 0x0C)}+{u32(arc.data, j + 0x10)}"
        )
        if force:
            line += f" force={vec3(arc, force)}"
        print(line)
    _zone_joints(arc, cn)


def _zone_joints(arc, cn):
    zjoints = _deref(arc, cn + 0x28)
    ztris = _deref(arc, cn + 0x20)
    for i in range(u32(arc.data, cn + 0x2C)):
        j = zjoints + i * 0x4C
        face_start = u32(arc.data, j + 0x0C)
        kind = flags = 0
        if ztris:
            tri = ztris + face_start * 0x18
            kind = u32(arc.data, tri + 0x10) & 0x01FFFFFF
            flags = u32(arc.data, tri + 0x14)
        param, param_rel = ptr(arc, j + 0x18)
        param_s = f"{param:#x}" if param_rel else f"{param:d}"
        print(
            f"    zone[{i:3d}] bone={s32(arc.data, j):3d} "
            f"{zone_kind_name(kind)} flags={flags:#x} "
            f"link={s32(arc.data, j + 0x14):d} "
            f"verts={u32(arc.data, j + 0x04)}+{u32(arc.data, j + 0x08)} "
            f"faces={face_start}+{u32(arc.data, j + 0x10)} param={param_s}"
        )


def _splines(arc, sn):
    print(f"\n  SplineNode @ {sn:#x}:")
    setup = _deref(arc, sn + 0x00)
    if setup:
        print(f"    +00  CourseSetup   @ {setup:#x}  loop={arc.data[setup + 0x10]}")
        _spline_list(arc, _deref(arc, setup), "      course")
        links = _deref(arc, setup + 0x04)
        if links:
            print(f"      keygroups: {u32(arc.data, links + 0x04)} entries")
    rng = _deref(arc, sn + 0x04)
    if rng:
        count = u32(arc.data, rng + 0x04)
        print(f"    +04  CPURangeSpline @ {rng:#x}  count={count}")
        base = _deref(arc, rng)
        for i in range(count):
            r = base + i * 0x18
            print(
                f"      [{i:2d}] flags={u32(arc.data, r + 0x14):#010x} "
                f"left={_deref(arc, r):#x} right={_deref(arc, r + 0x04):#x}"
            )
    conv = _deref(arc, sn + 0x10)
    if conv:
        _spline_list(arc, _deref(arc, conv), "    +10  conveyor")
    _spline_list(arc, _deref(arc, sn + 0x14), "    +14  rail")
    _spline_list(arc, _deref(arc, sn + 0x18), "    +18  heavy")


def _spline_list(arc, lst, label):
    if not lst:
        return
    count = u32(arc.data, lst + 0x04)
    base = _deref(arc, lst)
    print(f"{label}: {count} spline(s) @ {lst:#x}")
    for i in range(count):
        sp = _deref(arc, base + i * 4)
        if not sp:
            continue
        t = arc.data[sp]
        print(
            f"      [{i:2d}] {SPLINE_TYPES.get(t, t)} pts={u16(arc.data, sp + 0x02)} "
            f"len={f32(arc.data, sp + 0x0C):.1f}"
        )


def _positions(arc, pn):
    print(f"\n  PositionNode @ {pn:#x}:")
    joint = _deref(arc, pn)
    if joint:
        print(f"    +00  PositionJoint  -> JOBJDesc @ {joint:#x}")
    for foff, name in POSITION_SLOTS:
        lst = _deref(arc, pn + foff)
        if not lst:
            continue
        count = u32(arc.data, lst + 0x08)
        mode = "joint-indexed" if _deref(arc, lst) else "inline"
        print(f"    +{foff:02X}  {name:14s} -> {lst:#x}  count={count} ({mode})")


def _subanim(arc, sn):
    print(f"\n  SubAnimNode @ {sn:#x}:")
    for i, name in enumerate(SUBANIM_SLOTS):
        sa = _deref(arc, sn + i * 4)
        if not sa:
            continue
        count = u32(arc.data, sa + 0x04)
        arr = _deref(arc, sa)
        print(f"    +{i * 4:02X}  {name:9s} -> grSubAnim @ {sa:#x}  count={count}")
        for j in range(count):
            aj = _deref(arc, arr + j * 4)
            if aj:
                print(
                    f"          [{j:2d}] AnimJoint @ {aj:#x}"
                    f"{describe(arc, 'AnimJoint', aj)}"
                )


def _items(arc, it):
    print(f"\n  ItemNode @ {it:#x}:")
    for foff, name in (
        (0x04, "TimingTable"),
        (0x08, "CityTrial"),
        (0x0C, "AirRide"),
        (0x10, "Coliseum"),
    ):
        p = _deref(arc, it + foff)
        print(
            f"    +{foff:02X}  {name:12s} -> {p:#x}"
            if p
            else f"    +{foff:02X}  {name:12s} -> NULL"
        )
    timing = _deref(arc, it + 0x04)
    if timing:
        print(
            f"      timing entries={u32(arc.data, timing + 0x08)} "
            f"positions={u32(arc.data, timing + 0x10)} "
            f"areas={u32(arc.data, timing + 0x18)}"
        )
    city = _deref(arc, it + 0x08)
    if city:
        print(
            f"      citytrial item_chances={u32(arc.data, city + 0x08)} "
            f"special_timings={u32(arc.data, city + 0x24)}"
        )


def _fog(arc, fn):
    print(f"\n  FogNode @ {fn:#x}:")
    fog_data = _deref(arc, fn)
    if fog_data:
        desc = _deref(arc, fog_data)
        if desc:
            print(f"    +00  FogDesc @ {desc:#x}{describe(arc, 'FogDesc', desc)}")
    types = _deref(arc, fn + 0x04)
    if types:
        count = u32(arc.data, types + 0x04)
        base = _deref(arc, types)
        print(f"    +04  FogTypes @ {types:#x}  count={count}")
        for i in range(count):
            e = base + i * 0x48
            print(
                f"      [{i:2d}] x00={u32(arc.data, e):#x} "
                f"colors={rgba(arc, e + 0x04)} {rgba(arc, e + 0x10)} "
                f"{rgba(arc, e + 0x14)} {rgba(arc, e + 0x20)} "
                f"{rgba(arc, e + 0x24)} flag={arc.data[e + 0x44]}"
            )


def _rails(arc, rn):
    count = u32(arc.data, rn + 0x04)
    base = _deref(arc, rn)
    print(f"\n  RailCollNode @ {rn:#x}:  count={count}")
    for i in range(count):
        r = _deref(arc, base + i * 4)
        if not r:
            continue
        print(f"    rail[{i:2d}] @ {r:#x}{describe(arc, 'grRailColl', r)}")
        param = _deref(arc, r)
        if param:
            print(f"       param @ {param:#x}{describe(arc, 'grRailParam', param)}")


def _fgm(arc, fn):
    print(f"\n  FGMNode @ {fn:#x}:")
    for slot, cnt_slot, label in (
        (0x00, 0x04, "positional"),
        (0x08, 0x0C, "triggered"),
    ):
        count = u32(arc.data, fn + cnt_slot)
        base = _deref(arc, fn + slot)
        print(f"    {label}: {count}")
        for i in range(count):
            e = _deref(arc, base + i * 4)
            if e:
                print(f"      [{i:2d}] @ {e:#x}{describe(arc, 'grFGMNodeEntry', e)}")


def _yakumono(arc, yn):
    count = u32(arc.data, yn + 0x04)
    arr = _deref(arc, yn)
    print(
        f"\n  YakumonoNode @ {yn:#x}:  entries={count} "
        f"(array holds {array_length(arc, arr) if arr else 0})"
    )
    for i in range(array_length(arc, arr) if arr else 0):
        d = _deref(arc, arr + i * 4)
        if not d:
            continue
        coll = _deref(arc, d + 0x0C)
        hurt = _deref(arc, d + 0x10)
        audio = _deref(arc, d + 0x14)
        print(
            f"    [{i:2d}] YakumonoDesc @ {d:#x} models={_deref(arc, d + 0x04):#x} "
            f"anim={_deref(arc, d + 0x08):#x}"
            + (
                f" coll={{{describe(arc, 'grCollisionNode', coll).strip()}}}"
                if coll
                else ""
            )
            + (f"{describe(arc, 'HurtCollision', hurt)}" if hurt else "")
            + (f"{describe(arc, 'YakumonoAudio', audio)}" if audio else "")
        )


def _partition(arc, pn):
    tree = _deref(arc, pn)
    if not tree:
        return
    print(
        f"\n  PartitionNode @ {pn:#x} -> grCollisionTree @ {tree:#x}"
        f"{describe(arc, 'grCollisionTree', tree)}"
    )
    n = u16(arc.data, tree + 0x04)
    base = _deref(arc, tree)
    depths = {}
    leaves = 0
    for i in range(n):
        b = _deref(arc, base + i * 4)
        if not b:
            continue
        depths[arc.data[b + 0x4C]] = depths.get(arc.data[b + 0x4C], 0) + 1
        if u16(arc.data, b + 0x18) == 0xFFFF:
            leaves += 1
    print(
        f"    buckets={n} leaves={leaves} "
        f"depths={{{', '.join(f'{d}: {c}' for d, c in sorted(depths.items()))}}}"
    )
    print(
        f"    bit table: {(u16(arc.data, tree + 0x58) + 7) // 8} B "
        f"({u16(arc.data, tree + 0x58)} triangles)"
    )


def _respawn(arc, rn):
    count = u32(arc.data, rn + 0x04)
    base = _deref(arc, rn)
    idx = [str(u32(arc.data, base + i * 4)) for i in range(count)] if base else []
    print(
        f"\n  RespawnNode @ {rn:#x}:  count={count} "
        f"GlobalDeadPos indices=[{', '.join(idx)}]"
    )


_PRINTERS = {
    "stage": _stage,
    "lights": _lights,
    "collision": _collision,
    "splines": _splines,
    "positions": _positions,
    "subanim": _subanim,
    "items": _items,
    "fog": _fog,
    "rails": _rails,
    "fgm": _fgm,
    "yakumono": _yakumono,
    "partition": _partition,
    "respawn": _respawn,
}
