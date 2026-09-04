# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""Declarative layout of the HSD struct types we traverse.

`SCHEMA` maps a type name to its on-disc size and its outgoing pointer
fields. It is the single source of truth for three consumers:
  - `walker.Walker` - reachability + byte sizing for carving.
  - `explore.py tree` - the printed tree and its `[size]` tags.
  - `verify_carved.py` - bounds-checking a carved archive.

Types whose size or child set cannot be expressed declaratively (image
buffers sized from their descriptor, NULL-terminated inline arrays,
embedded record containers) carry a `visit_*` handler on `Walker` that
runs in addition to their schema fields.

Field kinds:
  ptr      one relocated pointer at `off`.
  array    pointer to a NULL-terminated pointer array (HSDNullPointerArray).
  count    pointer to a contiguous pointer array whose length lives in a
           sibling field at `cnt_off` (`cnt_w` bytes wide).
  run      pointer to a run of pointers delimited by the reloc table (no
           count is stored on disc).
  records  pointer to `count` embedded records of `type`, back to back.
  buffer   pointer to a raw blob of `count * stride` bytes; `type` names
           the blob for display only.

A field with `label=None` is followed but not printed - class-name
strings, inverse-bind matrices, and raw blobs that would only add noise.
Four slots are flag-tagged unions (JOBJ+0x10, POBJ+0x14, ROBJ+0x08,
grFGMNodeEntry+0x14); `resolved_fields` applies the routing so every
consumer agrees.
"""

from collections import namedtuple

from .archive import u16, u32

Field = namedtuple("Field", "off type label kind cnt_off cnt_w stride")
TypeSpec = namedtuple("TypeSpec", "size fields")


def f(off, type_, label=None, kind="ptr", cnt_off=None, cnt_w=4, stride=4):
    return Field(off, type_, label, kind, cnt_off, cnt_w, stride)


# HSD_JOBJ flags (HSDLib HSD_JOBJ.cs JOBJ_FLAG). Stage data repurposes the
# +0x10 slot when SPLINE or PTCL is set.
JOBJ_FLAG_PTCL = 1 << 5
JOBJ_FLAG_SPLINE = 1 << 14
# HSD_POBJ flags (HSDLib HSD_POBJ.cs POBJ_FLAG).
POBJ_FLAG_SHAPEANIM = 1 << 12
POBJ_FLAG_ENVELOPE = 1 << 13
# HSD_ROBJ ref type lives in flag bits 28-30.
ROBJ_REFTYPE_MASK = 0x70000000
ROBJ_REFTYPE_JOBJ = 0x10000000


SCHEMA = {
    # Render / geometry.
    "JOBJDesc": TypeSpec(
        0x40,
        [
            f(0x00, "cstring"),
            f(0x08, "JOBJDesc", "child"),
            f(0x10, "DObjDesc", "dobj"),  # union: Spline / ParticleJoint
            f(0x38, "Mtx"),  # inverse bind matrix
            f(0x3C, "RObjDesc", "robj"),
            f(0x0C, "JOBJDesc", "next"),
        ],
    ),
    "DObjDesc": TypeSpec(
        0x10,
        [
            f(0x00, "cstring"),
            f(0x08, "MObjDesc", "mobj"),
            f(0x0C, "POBJDesc", "pobj"),
            f(0x04, "DObjDesc", "next"),
        ],
    ),
    # MObj +0x10 is unused (HSDLib HSD_MOBJ.cs).
    "MObjDesc": TypeSpec(
        0x18,
        [
            f(0x00, "cstring"),
            f(0x08, "TObjDesc", "tobj"),
            f(0x0C, "MaterialDesc", "material"),
            f(0x14, "PEDesc", "pedesc"),
        ],
    ),
    "TObjDesc": TypeSpec(
        0x5C,
        [
            f(0x00, "cstring"),
            f(0x4C, "ImageDesc", "image"),
            f(0x50, "TlutDesc", "tlut"),
            f(0x54, "TexLODDesc", "lod"),
            f(0x58, "TObjTev", "tev"),
            f(0x04, "TObjDesc", "next"),
        ],
    ),
    "POBJDesc": TypeSpec(
        0x18,
        [
            f(0x00, "cstring"),
            f(0x08, "VtxDescList"),
            f(0x10, "dl_blob"),
            f(0x14, "JOBJDesc", "bound"),  # union: ShapeSet / Envelope[]
            f(0x04, "POBJDesc", "next"),
        ],
    ),
    "ImageDesc": TypeSpec(0x18, []),  # image buffer sized by the handler
    "TlutDesc": TypeSpec(0x10, []),  # palette sized by the handler
    "IOBJDesc": TypeSpec(0x0C, []),
    "VtxDescList": TypeSpec(None, []),  # 0x18 entries, GX_VA_NULL terminated
    "ShapeSet": TypeSpec(0x1C, []),
    "Envelope": TypeSpec(None, []),
    "Spline": TypeSpec(0x18, []),
    "ParticleGroup": TypeSpec(None, []),
    "ParticleJoint": TypeSpec(0x08, []),
    "Mtx": TypeSpec(0x30, []),  # 4x3 floats, inverse world
    "MaterialDesc": TypeSpec(0x14, []),
    "TexLODDesc": TypeSpec(0x10, []),
    "PEDesc": TypeSpec(0x0C, []),
    "TObjTev": TypeSpec(0x20, []),
    "RObjDesc": TypeSpec(
        0x0C,
        [
            f(0x08, "JOBJDesc", "target"),  # union: only when REFTYPE == JOBJ
            f(0x00, "RObjDesc", "next"),
        ],
    ),
    # Scene / lighting.
    "SOBJ": TypeSpec(
        0x10,
        [
            f(0x00, "ModelGroup", "models", "array"),
            f(0x04, "Camera", "cameras", "array"),
            f(0x08, "Light", "lights", "array"),
            f(0x0C, "FogAnim", "fog"),
        ],
    ),
    "ModelGroup": TypeSpec(
        0x10,
        [
            f(0x00, "JOBJDesc", "root"),
            f(0x04, "AnimJoint", "jointanim", "array"),
            f(0x08, "MatAnimJoint", "matanim", "array"),
            f(0x0C, "ShapeAnimJoint", "shapeanim", "array"),
        ],
    ),
    "Camera": TypeSpec(
        0x40,
        [
            f(0x00, "cstring"),
            f(0x18, "WObjDesc", "eye"),
            f(0x1C, "WObjDesc", "target"),
        ],
    ),
    "LightGroup": TypeSpec(
        0x0C,
        [
            f(0x00, "LightNode", "global"),
            f(0x04, "LightNode", "group1"),
            f(0x08, "LightNode", "group2"),
        ],
    ),
    "LightNode": TypeSpec(
        0x10,
        [
            f(0x00, "Light", "light1"),
            f(0x04, "Light", "light2"),
            f(0x08, "Light", "light3"),
            f(0x0C, "Light", "light4"),
        ],
    ),
    "Light": TypeSpec(
        0x08,
        [
            f(0x00, "LObjDesc", "lobj"),
            f(0x04, "LightAnimPointer", "anim", "array"),
        ],
    ),
    # LObj +0x18 attenuation block is 0x0C or 0x14 by light type; sized
    # by the next-reachable-start heuristic.
    "LObjDesc": TypeSpec(
        0x1C,
        [
            f(0x10, "WObjDesc", "position"),
            f(0x14, "WObjDesc", "interest"),
            f(0x18, "lobj_attn_blob"),
            f(0x04, "LObjDesc", "next"),
        ],
    ),
    "WObjDesc": TypeSpec(
        0x14,
        [
            f(0x00, "cstring"),
            f(0x10, "RObjDesc", "robj"),
        ],
    ),
    "FogDesc": TypeSpec(0x18, [f(0x04, "FogAdjDesc", "adj")]),
    "FogAdjDesc": TypeSpec(0x44, []),
    "FogAnim": TypeSpec(
        0x08,
        [
            f(0x00, "FogDesc", "fog"),
            f(0x04, "AOBJ", "anim"),
        ],
    ),
    # Animation. Every branch bottoms out at an AOBJ (whose FOBJDesc
    # keyframe buffers are sized from dataLength) or, for texture
    # animation, at the ImageDesc / TlutDesc frame buffers.
    "AOBJ": TypeSpec(
        0x10,
        [
            f(0x08, "FOBJDesc", "tracks"),
            f(0x0C, "JOBJDesc", "object"),
        ],
    ),
    "AOBJDesc": TypeSpec(
        0x08,
        [
            f(0x04, "AOBJ", "aobj"),
            f(0x00, "AOBJDesc", "next"),
        ],
    ),
    "FOBJDesc": TypeSpec(0x14, [f(0x00, "FOBJDesc", "next")]),
    "FOBJ": TypeSpec(0x08, [f(0x04, "anim_buffer")]),
    "AnimJoint": TypeSpec(
        0x14,
        [
            f(0x00, "AnimJoint", "child"),
            f(0x08, "AOBJ", "aobj"),
            f(0x04, "AnimJoint", "next"),
        ],
    ),
    "MatAnimJoint": TypeSpec(
        0x0C,
        [
            f(0x00, "MatAnimJoint", "child"),
            f(0x08, "MatAnim", "matanim"),
            f(0x04, "MatAnimJoint", "next"),
        ],
    ),
    "MatAnim": TypeSpec(
        0x10,
        [
            f(0x04, "AOBJ", "aobj"),
            f(0x08, "TexAnim", "texanim"),
            f(0x00, "MatAnim", "next"),
        ],
    ),
    "TexAnim": TypeSpec(
        0x18,
        [
            f(0x08, "AOBJ", "aobj"),
            f(0x0C, "ImageDesc", "images", "count", 0x14, 2),
            f(0x10, "TlutDesc", "tluts", "count", 0x16, 2),
            f(0x00, "TexAnim", "next"),
        ],
    ),
    "ShapeAnimJoint": TypeSpec(
        0x0C,
        [
            f(0x00, "ShapeAnimJoint", "child"),
            f(0x08, "ShapeAnim", "shapeanim"),
            f(0x04, "ShapeAnimJoint", "next"),
        ],
    ),
    "ShapeAnim": TypeSpec(
        0x08,
        [
            f(0x04, "AOBJDesc", "aobjdesc"),
            f(0x00, "ShapeAnim", "next"),
        ],
    ),
    "ROBJAnimJoint": TypeSpec(
        0x08,
        [
            f(0x04, "AOBJ", "aobj"),
            f(0x00, "ROBJAnimJoint", "next"),
        ],
    ),
    "WOBJAnim": TypeSpec(
        0x08,
        [
            f(0x00, "AOBJ", "aobj"),
            f(0x04, "ROBJAnimJoint", "animjoint"),
        ],
    ),
    "LightAnimPointer": TypeSpec(
        0x10,
        [
            f(0x04, "AOBJ", "aobj"),
            f(0x08, "WOBJAnim", "position"),
            f(0x0C, "WOBJAnim", "interest"),
            f(0x00, "LightAnimPointer", "next"),
        ],
    ),
    "FigaTree": TypeSpec(0x14, []),
    # KAR stage model (HSDLib HSDRaw/AirRide/Gr/Model). grModel's two
    # trailing slots are unidentified in HSDLib and hold no model pointer.
    "grModel": TypeSpec(
        0x10,
        [
            f(0x00, "MainModel", "main"),
            f(0x04, "SkyBoxModel", "skybox"),
        ],
    ),
    "MainModel": TypeSpec(
        0x14,
        [
            f(0x00, "JOBJDesc", "root"),
            f(0x10, "ModelBounding", "bounding"),
        ],
    ),
    "SkyBoxModel": TypeSpec(
        0x08,
        [
            f(0x00, "JOBJDesc", "root"),
            f(0x04, "ModelMotion", "motion"),
        ],
    ),
    "ModelBounding": TypeSpec(0x20, []),
    "ModelMotion": TypeSpec(
        0x14,
        [
            f(0x00, "AnimJoint", "jointanim"),
            f(0x04, "MatAnimJoint", "matanim"),
        ],
    ),
    # KAR grData sub-animations (grData+0x24). Six named slots, each a
    # Count-delimited array of HSD_AnimJoint.
    "grSubAnimNode": TypeSpec(
        0x18,
        [
            f(0x00, "grSubAnim", "superjump"),
            f(0x04, "grSubAnim", "x04"),
            f(0x08, "grSubAnim", "rail"),
            f(0x0C, "grSubAnim", "x0c"),
            f(0x10, "grSubAnim", "x10"),
            f(0x14, "grSubAnim", "x14"),
        ],
    ),
    "grSubAnim": TypeSpec(
        0x08,
        [
            f(0x00, "AnimJoint", "anims", "count", 0x04, 4),
        ],
    ),
    # KAR stage data (HSDLib HSDRaw/AirRide/Gr/Data). Slots HSDLib types as
    # a bare HSDAccessor are followed as `opaque` - their bytes are kept and
    # sized by the neighbour heuristic, but no layout is claimed.
    "grData": TypeSpec(
        0x54,
        [
            f(0x04, "grStageNode", "stage"),
            f(0x08, "opaque", "stageparam"),
            f(0x14, "LightGroup", "lights"),
            f(0x18, "grCollisionNode", "collision"),
            f(0x1C, "grSplineNode", "splines"),
            f(0x20, "grPositionNode", "positions"),
            f(0x24, "grSubAnimNode", "subanim"),
            f(0x28, "opaque", "enemy"),
            f(0x2C, "grItemNode", "items"),
            f(0x34, "grFogNode", "fog"),
            f(0x38, "grRailCollNode", "rails"),
            f(0x3C, "grFGMNode", "fgm"),
            f(0x40, "YakumonoNode", "yakumono"),
            f(0x44, "opaque", "replay"),
            f(0x48, "grCollisionTreeNode", "partition"),
            f(0x4C, "grRespawnNode", "respawn"),
            f(0x50, "opaque", "stadium"),
        ],
    ),
    "grDataCommon": TypeSpec(0x04, [f(0x00, "grMaterialNode", "materials")]),
    "grMaterialNode": TypeSpec(0x594, []),
    # 0x70-0x7C are unused test parameters (empty in every retail stage).
    "grStageNode": TypeSpec(
        0xE8,
        [
            f(0x70, "opaque", "airflow1"),
            f(0x74, "opaque", "spline1"),
            f(0x78, "opaque", "airflow2"),
            f(0x7C, "opaque", "spline2"),
            f(0xE4, "grStagePadCountPtr", "padcount"),
        ],
    ),
    "grStagePadCountPtr": TypeSpec(0x04, [f(0x00, "grStagePadCount", "count")]),
    "grStagePadCount": TypeSpec(0x08, []),
    "grCollisionNode": TypeSpec(
        0x30,
        [
            f(0x00, "vertex_buf", "vertices", "buffer", 0x04, 4, 0x0C),
            f(0x08, "tri_buf", "triangles", "buffer", 0x0C, 4, 0x14),
            f(0x10, "grCollisionJoint", "joints", "records", 0x14),
            f(0x18, "vertex_buf", "zone_vertices", "buffer", 0x1C, 4, 0x0C),
            f(0x20, "tri_buf", "zone_triangles", "buffer", 0x24, 4, 0x18),
            f(0x28, "grZoneCollisionJoint", "zone_joints", "records", 0x2C),
        ],
    ),
    "grCollisionJoint": TypeSpec(0x1C, [f(0x18, "Vector3", "force")]),
    # +0x18 is an int for zone kind 30 and a per-kind param record otherwise;
    # only the relocated (pointer) form is followed.
    "grZoneCollisionJoint": TypeSpec(0x4C, [f(0x18, "opaque", "param")]),
    "Vector3": TypeSpec(0x0C, []),
    "grCollisionTreeNode": TypeSpec(0x04, [f(0x00, "grCollisionTree", "tree")]),
    "grCollisionTree": TypeSpec(
        0x5C,
        [
            f(0x00, "grPartitionBucket", "buckets", "count", 0x04, 2),
            f(0x0C, "u16_buf", "coll_triangles", "buffer", 0x10, 2, 2),
            f(0x18, "u16_buf", "zone_indices", "buffer", 0x1C, 2, 2),
            f(0x24, "u16_buf", "rough_indices", "buffer", 0x28, 2, 2),
            # +0x54 is a one-bit-per-collidable-triangle table; sized by handler.
        ],
    ),
    "grPartitionBucket": TypeSpec(0x50, []),
    "grSplineNode": TypeSpec(
        0x1C,
        [
            f(0x00, "grSplineSetup", "course"),
            f(0x04, "grRangeSplineSetup", "cpu_range"),
            f(0x10, "grConveyorPath", "conveyor"),
            f(0x14, "grSplineList", "rail_splines"),
            f(0x18, "grSplineList", "heavy_splines"),
        ],
    ),
    "grSplineSetup": TypeSpec(
        0x38,
        [
            f(0x00, "grSplineList", "splines"),
            f(0x04, "grSplineLinkList", "keygroups"),
            f(0x08, "opaque", "altpath_lookup"),
            f(0x0C, "opaque", "group_lookup"),
            f(0x1C, "opaque", "x1c"),
        ],
    ),
    "grSplineLinkList": TypeSpec(
        0x08,
        [
            f(0x00, "u32_buf", "list", "buffer", 0x04, 4, 4),
        ],
    ),
    "grSplineList": TypeSpec(
        0x08,
        [
            f(0x00, "Spline", "splines", "count", 0x04, 4),
        ],
    ),
    "grRangeSplineSetup": TypeSpec(
        0x0C,
        [
            f(0x00, "grRangeSpline", "splines", "records", 0x04),
            f(0x08, "opaque", "city_param"),
        ],
    ),
    "grRangeSpline": TypeSpec(
        0x18,
        [
            f(0x00, "Spline", "left"),
            f(0x04, "Spline", "right"),
        ],
    ),
    "grConveyorPath": TypeSpec(0x08, [f(0x00, "grSplineList", "splines")]),
    "grPositionNode": TypeSpec(
        0x38,
        [
            f(0x00, "JOBJDesc", "joint"),
            f(0x04, "grPositionList", "start"),
            f(0x08, "grPositionList", "enemy"),
            f(0x0C, "grPositionList", "gravity"),
            f(0x10, "grPositionList", "airflow"),
            f(0x14, "grPositionList", "conveyor"),
            f(0x18, "grPositionList", "item"),
            f(0x1C, "grPositionList", "event"),
            f(0x20, "grPositionList", "vehicle"),
            f(0x24, "grPositionList", "global_dead"),
            f(0x28, "grPositionList", "local_dead"),
            f(0x2C, "grPositionList", "yakumono"),
            f(0x30, "grAreaPositionList", "item_area"),
            f(0x34, "grAreaPositionList", "vehicle_area"),
        ],
    ),
    # Joint-indexed and inline storage are mutually exclusive; whichever
    # slot is NULL simply contributes nothing.
    "grPositionList": TypeSpec(
        0x0C,
        [
            f(0x00, "u32_buf", "joint_indices", "buffer", 0x08, 4, 4),
            f(0x04, "pos_buf", "positions", "buffer", 0x08, 4, 0x24),
        ],
    ),
    "grAreaPositionList": TypeSpec(
        0x0C,
        [
            f(0x00, "u32_buf", "joint_indices", "buffer", 0x08, 4, 4),
            f(0x04, "area_buf", "areas", "buffer", 0x08, 4, 0x18),
        ],
    ),
    "grItemNode": TypeSpec(
        0x14,
        [
            f(0x04, "grItemTimingTable", "timing"),
            f(0x08, "grItemCityTrial", "citytrial"),
            f(0x0C, "grItemSpawnTable", "airride"),
            f(0x10, "grItemSpawnTable", "coliseum"),
        ],
    ),
    "grItemTimingTable": TypeSpec(
        0x1C,
        [
            f(0x04, "item_buf", "timing", "buffer", 0x08, 4, 0x10),
            f(0x0C, "item_buf", "positions", "buffer", 0x10, 4, 0x10),
            f(0x14, "opaque", "areas"),
        ],
    ),
    "grItemCityTrial": TypeSpec(
        0x28,
        [
            f(0x00, "opaque", "box_spawn"),
            f(0x04, "item_buf", "item_chance", "buffer", 0x08, 4, 0x18),
            f(0x0C, "opaque", "legendary"),
            f(0x10, "item_buf", "x10", "buffer", 0x14, 4, 0x10),
            f(0x18, "opaque", "x18"),
            f(0x20, "item_buf", "special_timing", "buffer", 0x24, 4, 0x14),
        ],
    ),
    "grItemSpawnTable": TypeSpec(
        0x0C,
        [
            f(0x00, "opaque", "x00"),
            f(0x04, "item_buf", "items", "buffer", 0x08, 4, 0x10),
        ],
    ),
    "grFogNode": TypeSpec(
        0x08,
        [
            f(0x00, "grFogData", "fogdata"),
            f(0x04, "grTypeData", "fogtypes"),
        ],
    ),
    "grFogData": TypeSpec(0x08, [f(0x00, "FogDesc", "fog")]),
    "grTypeData": TypeSpec(
        0x08,
        [
            f(0x00, "fogtype_buf", "entries", "buffer", 0x04, 4, 0x48),
        ],
    ),
    "grRailCollNode": TypeSpec(
        0x08,
        [
            f(0x00, "grRailColl", "rails", "count", 0x04, 4),
        ],
    ),
    "grRailColl": TypeSpec(0x34, [f(0x00, "grRailParam", "param")]),
    "grRailParam": TypeSpec(
        0x34,
        [
            f(0x14, "rail_buf", "data", "buffer", 0x18, 4, 0x0C),
            f(0x1C, "rail_buf", "dash", "buffer", 0x20, 4, 0x08),
            f(0x24, "rail_buf", "dash2", "buffer", 0x28, 4, 0x08),
            f(0x2C, "rail_buf", "leap", "buffer", 0x30, 4, 0x0C),
        ],
    ),
    "grFGMNode": TypeSpec(
        0x10,
        [
            f(0x00, "grFGMNodeEntry", "positional", "count", 0x04, 4),
            f(0x08, "grFGMNodeEntry", "triggered", "count", 0x0C, 4),
        ],
    ),
    # +0x14 is an inline HSD_Vector3 when Type == 1 and an HSD_Spline
    # pointer when Type == 2.
    "grFGMNodeEntry": TypeSpec(
        0x18,
        [
            f(0x00, "sfx_buf", "sounds", "buffer", 0x04, 4, 0x08),
            f(0x14, "Spline", "spline"),
        ],
    ),
    "YakumonoNode": TypeSpec(
        0x18,
        [
            f(0x00, "YakumonoDesc", "entries", "array"),
            f(0x08, "opaque", "x08"),
            f(0x10, "opaque", "x10"),
        ],
    ),
    # Animation is an HSD_YakumonoState array with no stored count, so its
    # per-state animation joints are not reachable from here.
    "YakumonoDesc": TypeSpec(
        0x18,
        [
            f(0x00, "opaque", "params"),
            f(0x04, "MainModel", "models", "run"),
            f(0x08, "opaque", "animation"),
            f(0x0C, "grCollisionNode", "collision"),
            f(0x10, "HurtCollision", "hurtdata"),
            f(0x14, "YakumonoAudio", "audio"),
        ],
    ),
    "YakumonoAudio": TypeSpec(
        0x18,
        [
            f(0x00, "sfx_buf", "sounds", "buffer", 0x04, 4, 0x08),
        ],
    ),
    "HurtCollision": TypeSpec(
        0x0C,
        [
            f(0x00, "hurt_buf", "hurtboxes", "buffer", 0x04, 4, 0x18),
        ],
    ),
    "grRespawnNode": TypeSpec(
        0x08,
        [
            f(0x00, "u32_buf", "indices", "buffer", 0x04, 4, 4),
        ],
    ),
}


# HSDLib root class (from symbols.classify_symbol) -> the schema type to
# start a walk at. Anything missing falls back to JOBJDesc.
CLASS_TO_ROOT = {
    "HSD_SOBJ": "SOBJ",
    "HSD_MOBJ": "MObjDesc",
    "HSD_TOBJ": "TObjDesc",
    "HSD_IOBJ": "IOBJDesc",
    "HSD_Image": "ImageDesc",
    "HSD_Camera": "Camera",
    "HSD_JOBJ": "JOBJDesc",  # our "JOBJDesc" name == HSDLib HSD_JOBJ
    "HSD_JOBJDesc": "ModelGroup",  # the 0x10 wrapper
    "HSD_ModelGroup": "ModelGroup",  # same layout as HSD_JOBJDesc
    "HSD_FogDesc": "FogDesc",
    "HSD_ParticleGroup": "ParticleGroup",
    "HSD_AnimJoint": "AnimJoint",
    "HSD_MatAnimJoint": "MatAnimJoint",
    "HSD_ShapeAnimJoint": "ShapeAnimJoint",
    "HSD_TexAnim": "TexAnim",
    "HSD_FigaTree": "FigaTree",
    "KAR_grModel": "grModel",
    "HSDArrayAccessor<KAR_grModelMotion>": "ModelMotion",
    "KAR_grData": "grData",
    "KAR_grDataCommon": "grDataCommon",
}

# Roots whose backing storage IS a NULL-terminated pointer array: the
# public's own data is the array, with no wrapping struct on disc.
ARRAY_ROOTS = {
    "HSDNullPointerArrayAccessor<HSD_Light>": "Light",  # _scene_lights / map_plit
    "HSDNullPointerArrayAccessor<HSD_JOBJDesc>": "ModelGroup",  # _scene_models
}


def root_for(klass):
    """(root_type, is_array) for an HSDLib root class name."""
    if klass in ARRAY_ROOTS:
        return ARRAY_ROOTS[klass], True
    return CLASS_TO_ROOT.get(klass, "JOBJDesc"), False


def _union_jobj_dobj(arc, off, fd):
    if off + 0x08 > len(arc.data):
        return fd
    flags = u32(arc.data, off + 0x04)
    if flags & JOBJ_FLAG_SPLINE:
        return fd._replace(type="Spline", label="spline")
    if flags & JOBJ_FLAG_PTCL:
        return fd._replace(type="ParticleJoint", label="ptcl")
    return fd


def _union_pobj_bound(arc, off, fd):
    if off + 0x0E > len(arc.data):
        return fd
    flags = u16(arc.data, off + 0x0C)
    if flags & POBJ_FLAG_SHAPEANIM:
        return fd._replace(type="ShapeSet", label="shapeset")
    if flags & POBJ_FLAG_ENVELOPE:
        return fd._replace(type="Envelope", label="envelope", kind="array")
    return fd  # SingleBoundJOBJ


def _union_robj_ref(arc, off, fd):
    if off + 0x08 > len(arc.data):
        return None
    flags = u32(arc.data, off + 0x04)
    return fd if (flags & ROBJ_REFTYPE_MASK) == ROBJ_REFTYPE_JOBJ else None


def _union_fgm_target(arc, off, fd):
    if off + 0x10 > len(arc.data) or u32(arc.data, off + 0x0C) != 2:
        return None  # Type 1 stores an inline HSD_Vector3, not a pointer
    return fd


_UNIONS = {
    ("JOBJDesc", 0x10): _union_jobj_dobj,
    ("POBJDesc", 0x14): _union_pobj_bound,
    ("RObjDesc", 0x08): _union_robj_ref,
    ("grFGMNodeEntry", 0x14): _union_fgm_target,
}


def resolved_fields(arc, typ, off):
    """Fields of `typ` with its flag-tagged unions resolved against the
    record at `off`. Union arms that are inactive are dropped."""
    spec = SCHEMA.get(typ)
    if spec is None:
        return []
    out = []
    for fd in spec.fields:
        router = _UNIONS.get((typ, fd.off))
        if router is not None:
            fd = router(arc, off, fd)
            if fd is None:
                continue
        out.append(fd)
    return out


def type_size(typ):
    """On-disc size of `typ`, or None when it is computed at walk time."""
    spec = SCHEMA.get(typ)
    return spec.size if spec else None


def array_length(arc, off):
    """Length of the NULL-terminated pointer array at data offset `off`.
    The array ends at the first slot that is not relocated or is NULL."""
    n = 0
    while True:
        entry = off + n * 4
        if entry + 4 > len(arc.data) or entry not in arc.reloc_set:
            return n
        if u32(arc.data, entry) == 0:
            return n
        n += 1
