# Collision Zone System

Every Kirby Air Ride stage ships, alongside its walkable terrain collision, a
set of **collision zones**: axis-aligned boxes that tag a volume of the stage
with a behaviour. Dash panels, super-jump ramps, the ring gates of Air Glider,
reverb regions, area lights and the "you died locally" volumes are all zones.

A zone is not a mesh the player touches; it is a trigger volume whose *faces*
each carry a kind tag, so one box can be a dash panel on top and inert on its
sides.

## Zone Kinds (CZK)

The tag is called a CZK in the engine's asserts (`grGetKindCZK(zoneKind) ==
GrCZK_SuperJump`). It lives packed in each face record's `kind_word`:

| Bits | Meaning |
|---|---|
| 0..24 | the kind (`GrCollZoneKind`) |
| 28..31 | per-kind parameter index; the `index >= 0 && index < GrDashGate_Num` family of asserts bounds it (all these `_Num` limits are 2) |

`Gr_CollZone_NumMax` is 500 zones per stage; `GrCollZone_VtxNum` is 8 and
`GrCollZone_TriNum` is 12, i.e. a zone is always one box.

Kinds the engine names in its own assert strings:

| Kind | Name | Getter |
|---:|---|---|
| 2, 3 | dash gate | `grGetDashGateZoneParam` (0x800d21f8) |
| 4 | `GrCZK_DashRing` | `grGetDashGateZoneParam` |
| 5 | `GrCZK_WarpIn` | none - unimplemented |
| 6 | warp partner (`GrCZK_WarpOut`) | none - unimplemented |
| 7 | `GrCZK_SuperJump` | `grGetSuperJumpZoneParam` (0x800d24fc) |
| 9 | `GrCZK_Jump` | `grGetJumpZoneParam` (0x800d25a8) |
| 10 | `GrCZK_Spin` | `grGetSpinZoneParam` (0x800d2654) |
| 25 | `GrCZK_LocalDead` | `grGetLocalDeadZoneParam` (0x800d50f8) |

The remaining kinds carry no source name. They are reached only through
equality tests in `mpcoll`, and the names in `stage.h` (`GrCZK_FreeMove`,
`GrCZK_Occlusion`, `GrCZK_BoxArea`, `GrCZK_AreaLight`, `GrCZK_Reverb`,
`GrCZK_GateOpen`, `GrCZK_SuperJumpApproach`) come from the Japanese texture
labels `GrSimple` paints on the corresponding zone boxes.

Distribution across all shipped stages, by face count:

| Kind | Faces | Kind | Faces | Kind | Faces |
|---:|---:|---:|---:|---:|---:|
| 0 | 384 | 12 | 240 | 26 | 1776 |
| 1 | 144 | 13 | 24 | 27 | 84 |
| 2 | 108 | 14 | 12 | 28 | 12 |
| 3 | 180 | 15 | 24 | 29 | 24 |
| 4 | 360 | 16 | 12 | 30 | 348 |
| 5 | 24 | 17 | 120 | 31 | 828 |
| 6 | 24 | 18 | 60 | 32 | 9300 |
| 7 | 240 | 19 | 36 | 33 | 12 |
| 8 | 48 | 20 | 12 | 34 | 60 |
| 9 | 60 | 21 | 12 | 35 | 24 |
| 10 | 24 | 22 | 24 | | |
| 11 | 180 | 23 | 36 | | |
| | | 24 | 264 | | |

Kind 32 (area light) dominates because nearly every stage boxes its lighting.

## Data Layout

`GrData.pos_data` (GrData+0x18) points at the collision node:

```
GrCollisionNode
  +0x18 Vec3       *vertices     (zone_num * 8 entries)
  +0x1c int         vertex_num
  +0x20 GrCollFace *faces        (zone_num * 12 entries)
  +0x24 int         face_num
  +0x28 GrCollZone *zones
  +0x2c int         zone_num
```

```
GrCollZone (0x4C)                    GrCollFace (0x18)
  +0x00 int joint      index into      +0x00 int group   box face 0..5
            GrObj.joint_table          +0x04 int vtx[3]  indices into vertices
  +0x04 int vtx_base                   +0x10 u32 kind_word
  +0x08 int vtx_num    == 8            +0x14 int
  +0x0c int face_base
  +0x10 int face_num   == 12
```

The 12 triangles of a zone are partitioned into 6 groups of 2 by
`GrCollFace.group` - the six faces of the box. A group's kind is read off its
first triangle, so all triangles in a group share a kind.

## Runtime Expansion

`grZone_BuildRecord` (0x800dcf08) expands each authored `GrCollZone` into a
0x140-byte runtime record. The record array is allocated by the ground
scene/collision allocator (0x800d6dcc) as `zone_num * 0x140` and hangs off
`GrObj.x00c`.

Record layout: `+0x00` is the zone's `JOBJ *` (resolved through
`GrObj.joint_table` from `GrCollZone.joint`), `+0x04` the vertex base, and the
six face groups start at `+0x08` with a 0x24 stride. Each group holds its three
transformed vertex pointers followed by the kind word, so **face group `g`'s
kind lands at `record + 0x24 + g * 0x24`** - group 0's kind at `+0x24` is what
every consumer reads when it only cares about the zone's primary kind:

```c
kind = *(u32 *)(zone_record + 0x24) & 0x01ffffff;
```

Consumers are all explicit equality compares against that masked value - there
is no jump table anywhere in `main.dol`. A kind that no compare mentions
therefore cannot fire, no matter what the stage data says.

`mpcoll` carries a generated family of per-kind lookups at 0x80246584 through
0x802478c4, one function per queried kind, each scanning a collision result's
zone list for the first zone of its kind. The family covers kinds 2, 3, 7, 8,
9, 10, 12, 13, 14, 15, 16, 17, 18, 21, 22, 23, 24, 27, 28, 31, 33 and 35.

## The Warp Zones

Kinds 5 and 6 are authored but inert. They exist in exactly one file in the
game - `GrSimple.dat`, the collision test map - as two `WarpIn` boxes and two
`WarpOut` boxes, and nothing in `main.dol` ever compares a zone kind against 5
or 6.

What survives of the feature is one orphaned assert string at 0x804a3820,
`"grGetKindCZK(zoneKind) == GrCZK_WarpIn"`. It has zero code references and
zero pointers anywhere in memory, while every sibling string in the same block
(`GrDashZone_Num`, `GrDashGate_Num`, `GrDashRing_Num`, `GrCZK_SuperJump`,
`GrCZK_Jump`, `GrCZK_Spin`) is loaded by a live getter. The compiler kept the
literal and dropped the function. Its position in the string block - after
`GrDashRing_Num` (kind 4) and before `GrCZK_SuperJump` (kind 7) - is what fixes
`GrCZK_WarpIn` at 5, leaving 6 for the paired exit.

In `GrSimple` the four boxes form two warp pairs, each an entrance box wired to
an exit roughly 110 units down +Z:

| Zone | Kind | Centre | Size |
|---:|---:|---|---|
| 9 | 5 (WarpIn) | (-30, 0, 0) | 10 x 10 x 10 |
| 10 | 5 (WarpIn) | (12, 0, -10) | 10 x 10 x 10 |
| 11 | 6 (WarpOut) | (-30, 1, 100) | 10 x 10 x 10 |
| 12 | 6 (WarpOut) | (30, 1, 100) | 14 x 10 x 14 (box rotated 45 degrees) |

The matching geometry is textured with the labels "warp entrance"
(entrances), "warp exit" (exits) and "warp gate" (a thin marker plane laid over
each entrance). A mod that wants working warps has to supply the whole
behaviour: the data is authored, but there is no engine code to hook.

## GrSimple, the Collision Test Map

`GrSimple` (GroundKind 26) is the engine's collision/zone test bed rather than a
playable course, and it is the most direct reference for what a zone kind is
supposed to do: every surface and every zone box is textured with a Japanese
label naming its own property. Labels include floor, wall, ceiling, bouncy wall,
bouncy ceiling, dash, dash rail, dash ring / ring check, super jump (plus
separate top-face, bottom-face and approach markers), free movement, box area,
area light and area-light interpolation, occlusion, reverb, gate open, local
death, and the three warp labels.

It carries 45 zones / 540 faces covering kinds 0, 4, 5, 6, 7, 8, 16, 25, 26, 29,
30, 32, 34 and 35. `GrSimple2` (GroundKind 27) is the companion map and covers
most of the rest: kinds 0, 2, 3, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 20, 21,
22, 23, 24, 27, 28, 31 and 33.

`GrSimple` is also the only stage that exercises the yakumono framework's
generic `entries[]` dispatch at scale: its `YakumonoTable` has 40 entries, five
each of the eight even (non-"ctrl") generic base kinds 0, 2, 4, 6, 8, 10, 12 and
14, versus City Trial's zero.
