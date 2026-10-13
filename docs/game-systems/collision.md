# Map Collision

How bodies collide with the stage: ground detection, wall and ceiling pushback, raycasts,
collision zones and the out-of-bounds box. Two halves meet here. **grColl** owns the stage's
triangle soup and the queries over it. **mpColl** owns the per-body collision object
(`CollData`) that riders, machines, enemies and some items carry, runs its sweep each frame and
caches the contacts. A modder needs both to move a body, add or retire a surface, or read what a
body is touching. Entity-vs-entity damage (hitboxes, knockback) is a separate system that shares
nothing with this one.

## Stage triangles (GrCollParam)

`GrObj.coll` (`GrObj+0x54`, a `GrCollParam`, fetched with `Gr_GetCollParam()`) is the live mesh.
One vertex pool and one triangle array (`GrCollTri`, 0x40 bytes) hold both the baked terrain and
every placed prop; each prop owns a contiguous slice of the triangle array through a
`GrCollRecord`. Alongside them sit the collision zones, a coarse "rough" list, and three
`moving_*` lists for geometry that animates.

A `GrCollRecord` (0x98 bytes, array `coll.record` at `GrObj+0x64`) names the joint its slice
follows, the slice itself (`tri_begin`, `tri_num`), a bounding box, the joint's world matrix as
of the last bake and the inverse of the previous one, `desc_kind` and the owning prop's GObj.
`grScene_FindInstanceByKey` (0x800d7954) finds a record by joint. The joints themselves are
listed in `GrObj.joint_table` (+0x104), `{JOBJ*, JOBJDesc*}` pairs built from the stage's
terrain model at load, entry 0 the terrain root; zones and stage props refer to joints by their
index there.

`GrObj` carries three views of these arrays:

| Field | Offset | Role |
|---|---|---|
| `coll_max` | 0x00C | Capacity. `grColl_Alloc` counts into it first and sizes every array from it |
| `coll` | 0x054 | The live arrays and counts every query reads |
| `coll_terrain` | 0x09C | A window into `coll` owned by the terrain model; each prop has its own at `yaku_data+0x1C` |

The arrays are shared but the counts are separate words. The per-frame rebake of moving geometry
walks the windows while queries read `coll`, so a record reachable only through
`coll.moving_record` is queried every frame and never rebaked.

### Allocation

`grColl_Alloc` (0x800d6dcc) zeroes `coll_max`, counts the terrain node, every
`YakumonoTable.spawn_data_array` block and every generic yakumono entry into it, then makes one
`HSD_MemAlloc` per array sized exactly `count * stride`. `grColl_Free` (0x800d7060) frees them
field by field; it frees `coll.zone` and `coll.moving_rough` twice each and never frees
`coll.aux` or `coll.moving_zone`, which the scene-change heap reset covers.

So a mod must not swap a `coll.*` pointer for its own buffer - the original leaks and `HSD_Free`
later receives a foreign pointer. The supported way to get room is to raise a `coll_max` count
before the allocation: the fill pass leaves the surplus as slack at the tail of that array,
owned and freed by the game. The later prop-attach path asserts that each attach still fits, so
a raised count must be raised in `coll_max` too.

### Queries

Every query runs two passes; nothing in gameplay scans the whole triangle array.

- **Moving pass** - a brute-force walk of `coll.moving_record`, each record AABB-tested and then
  its triangle slice scanned. Moving geometry is absent from the KD-tree.
- **Static pass** - a walk of the KD-tree baked into the stage archive (`GrObj.coll_tree`, from
  `GrData+0x48`), whose leaves hold 16-bit indices into `coll.tri`.

Both end in two narrowphase primitives, `grColl_RayVsTri` (0x800d95dc) and
`grColl_SweptSphereVsTri` (0x802448b0), which take a triangle **index**; the moving pass derives
it from the triangle's byte offset into `coll.tri` divided by 0x40, so every triangle must live
in that one array. Both passes keep the nearest hit and return its triangle id (-1 for none),
writing the hit point to the caller's `out_pos`.

| Entry point | Address | Role |
|---|---|---|
| `Raycast_Do` | 0x800d9958 | Segment query, moving pass always on |
| `grColl_SweptSphereQuery` | 0x800d9e34 | Swept sphere; argument 5 gates the moving pass |
| `mpColl_SweptSphereMapColl` | 0x802454f8 | Rider and machine wall/floor pushback - what actually stops a machine |
| `mpColl_InsertContact` | 0x80241ca8 | Caches the winning triangle in the under, wall or top slot |

`Raycast_Do` is called through four wrappers that differ only in the kind mask and filter:
`Raycast_Any` (0x800d1a54, mask 7), `Raycast_Ground` (0x800d1ac4, mask 1), `Raycast_Wall`
(0x800d1b34, mask 2) and `Raycast_AnyTagged` (0x800d1ba4, mask 7 with a filter that also drops
any non-ground triangle without flag 0x8000).

### Kind and state gates

`GrCollTri.kind` bits 0-2 are the baked surface category: 1 under (floor), 2 wall, 4 top
(ceiling). Every query ANDs its own mask against them first, so the category - not a runtime
normal test - decides whether a triangle can be stood on, walled off, or hit at all.
`Raycast_Ground` cannot see a wall. A surface built at runtime must set the bits to match its
own facing, or the consumers it belongs to never look at it. Bits 4-11 carry the ground type
`grGetGroundTypeFromTriangleID` (0x800cec28) returns.

The primitives then require `GrCollTri.state` bit 6 (`GRCOLL_STATE_COLLIDABLE`) set and bit 7
(`GRCOLL_STATE_DEGENERATE`) clear. The swept-sphere test uses `GrCollTri.normal` as the plane
normal, so it must be unit length. A live City Trial triangle reads state 0x60.

Clearing bit 6 hides a triangle from every query. It does not reach a body that already cached
the triangle's id: `ItemData.point_coll`, `grColl_GetTriVtx` (0x800d7118) and
`grGetGroundTypeFromTriangleID` keep reading the retired triangle until their next query.

Triangle ids are range-checked in one place, `PointCollision_EnsureIDValid` (0x800d1838), which
every ground, landing and shadow consumer runs. The wall sweep and the surface-property lookups
index `coll.tri` with whatever id they are handed.

## Per-body collision (CollData)

`CollData` is the 0x350-byte per-body object. `mpColl_Create` (0x80245b4c) takes one from a
freelist pool, allocates its shape data and its `mpCollInfo` (the contact results, at
`CollData+0x44`), and links it into a global list `mpColl_GetFirstCollObj` (0x802414d4) walks.
`mpColl_Destroy` (0x80245ed0) undoes all of it. The only shape is a sphere whose radius
`mpColl_GetSphereRadius` (0x802415a8) lerps between two endpoints.

Each frame the owner calls `mpColl_Update` (0x80245f70) with the new position, direction,
extents and two radii; it sets `pos_delta = pos - prev_pos` (`+0x14`), which the rest of the
system treats as the body's velocity. `mpColl_ProcessMapColl` (0x802460d4) then clears the
contact info and drives `mpColl_UpdateCollision` (0x802485e0) for up to 10 pushback substeps.

`mpCollInfo` holds one `mpCollRec` per substep plus three lists pointing at the substeps that
produced an under, wall or top contact. A body is touching a wall this frame exactly when
`wall_rec_num` is non-zero, and `wall_recs[i]->wall` names the triangle that stopped it.
`contact_tri_id` (`+0x1d0`) caches the last winning triangle id.

| Owner | CollData | Per-frame entry |
|---|---|---|
| Machine | `MachineData+0x6F8` | `Machine_EnvCollThink` (0x801c65a8) proc, `Machine_ProcessEnvColl` (0x801e5108) |
| Rider | `RiderData+0x670` | `Rider_EnvColl` (0x8018f734) proc |
| Enemy | `EnemyData+0x594` | `EventActor_GroundSnap` (0x80204fac), `EventActor_GroundAttach` (0x8020a664) and the enemy ground helpers |
| Item | `ItemData+0x1A4`, often NULL | `CityItem_EnvColl` (0x8024f814) into `Item_GenericEnvColl` (0x80255438) |

## Breakable props in the sweep

City Trial's breakable props (houses, trees, rocks, walls, holes, the star pole, the pitfall)
have no geometry of their own outside this mesh: a prop's triangles are an ordinary slice of
`coll.tri`, owned by a `GrCollRecord` whose `yaku_gobj` (`+0x90`) is the prop's yakumono GObj
and whose `desc_kind` (`+0x8c`) is 3 for a breakable. The same sweep that stops a machine at a
house is the path that breaks it. `mpColl_UpdateCollision` runs two phases:

1. **Sweep** (`mpColl_SphereSceneObjColl`, 0x8024d414) finds the nearest facing triangle without
   checking the collidable bit. If the collider may break props (`mpColl_SceneObjBreakGate`,
   0x80241574: `CollData+0x34c` bit 2 set and `desc_kind == 3`), it runs the prop's break
   handler through `collideWithObject` (0x800f5004). `mpColl_Create` clears that bit and only
   the machine's collision init sets it; `mpColl_SetSceneObjBreak` (0x80247e58) arms any other
   collider.
2. **Response** (`mpResponse_DispatchSceneObjColl`, 0x80248bb4) walks the accumulated contacts
   and drops every one whose triangle has state bit 6 clear, so the body is not pushed out.

That split is the lever for passing through a prop: clearing the collidable bit across a
record's triangles with `grScene_SetInstanceColl(record, 0)` (0x800d7ad0) leaves the prop
visible but pass-through. Only break and init code writes the bit, so a clear sticks. The break
tail does exactly this; a mod that moves a prop's model away must do it too, because the
triangles stay where the prop was placed.

## Collision zones

Alongside the solid mesh, every stage ships authored **collision zones**: trigger boxes that tag
a volume with a behavior. Dash panels, super-jump ramps, Air Glider's ring gates, reverb
regions, area lights and local-death volumes are all zones. They never push a body; they only
report that it is inside.

A zone is always one box: 8 vertices and 12 triangles (`GrCollZone.vtx_num` and `face_num` are
asserted), partitioned by `GrCollFace.group` into the box's 6 faces. Each triangle's `kind_word`
packs the kind in bits 0-24 and a per-kind parameter index in bits 28-31; a face's kind is read
off its first triangle. `GrCollZone.joint` names a stage joint, so an animated joint carries its
zone along. The authored arrays hang off `GrData.coll_node` (`GrData+0x18`), at most 500 zones
per stage.

`grZone_BuildRecord` (0x800dcf08) expands each zone into a 0x140-byte runtime record in
`coll.zone`: the JObj at +0x00, then six face groups at a 0x24 stride, each ending in its kind
word. Group 0's kind, at `record+0x24`, is what consumers read:

```c
kind = *(u32 *)(zone_record + 0x24) & 0x01ffffff;
```

The mpColl sweep fills `CollData.zone_hit[20]` (`+0x48`) and `zone_hit_num` (`+0x98`) with the
indices of every zone the body is inside this frame, and moving zones in a parallel list at
`+0x9c`. A family of per-kind lookups (0x80246584-0x802478c4), one function per queried kind,
scans that list for the first zone of its kind. Every consumer is an equality compare against
the masked kind - there is no jump table - so **a kind no compare mentions does nothing**,
whatever the stage data says. A new zone behavior needs both the box and mod code to test for
it.

The kinds the engine names, from its assert strings and the enum `GrCollZoneKind`:

| Kind | Name | Reader |
|---:|---|---|
| 2, 3 | dash gate | `grGetDashGateZoneParam` (0x800d21f8) |
| 4 | `GrCZK_DashRing` | `grGetDashGateZoneParam` |
| 5, 6 | `GrCZK_WarpIn`, `GrCZK_WarpOut` | none - inert |
| 7 | `GrCZK_SuperJump` | `grGetSuperJumpZoneParam` (0x800d24fc) |
| 9 | `GrCZK_Jump` | `grGetJumpZoneParam` (0x800d25a8) |
| 10 | `GrCZK_Spin` | `grGetSpinZoneParam` (0x800d2654) |
| 15 | ground copy panel | gives a random copy ability |
| 25 | `GrCZK_LocalDead` | `grGetLocalDeadZoneParam` (0x800d50f8), `mpColl_GetDeadZoneIndex` (0x80247fac) |
| 32 | area light | the most common kind; nearly every stage boxes its lighting |

Each getter takes a zone index, asserts the zone carries its kind and fills its out-params from
the stage's per-kind parameter block. `mpColl_GetDeadZoneIndex` returns the first kind-25 zone
the body is in, moving zones included, which `Machine_CheckFallDeath` hands to
`Machine_SetFallDead`.

Kinds 5 and 6 exist only in `GrSimple.dat`, as two warp-entrance boxes wired to two exits about
110 units down +Z. No code compares a zone kind against 5 or 6, and the matching assert string
at 0x804a3820, naming `GrCZK_WarpIn`, has no reference; working warps would have to be written
from scratch. `GrSimple` (GroundKind 26) and `GrSimple2` (27) are the engine's collision test
grounds: every surface and zone box is textured with a Japanese label naming its own property
(floor, wall, bouncy wall, dash, super jump, free movement, area light, occlusion, reverb, gate
open, local death, warp), which makes them the best reference for what an unnamed kind is meant
to do.

## Out-of-bounds box and City Trial barriers

The playfield is also bounded by an axis-aligned box in the stage file, separate from the mesh:
`StageNode.oob_min` / `oob_max` (`GrData+0x04` -> `+0xCC` / `+0xD8`). City Trial's
(`GrCity1.dat`) is (-1300, -300, -1300) to (1300, 1500, 1300). `calcDistanceFromOOB`
(0x800d4f20) returns the signed distance to the nearest of its six planes, negative once a
position is outside, and the out-of-bounds death in `Machine_CheckFallDeath` (0x801e6464) uses
it. The box is plain data, not a JObj, so anything that rescales a stage must rewrite it too. It
also serves as a cheap stage-extent query.

In the city the box never fires, because two invisible walls inside it come first. They use
ground type 30 (the sea is 29) and follow the coastline as closed loops: an inner wall about 100
units above the terrain, which a winged machine can clear, and an outer wall from the floor to a
lid at Y 1040.9, which nothing passes.
