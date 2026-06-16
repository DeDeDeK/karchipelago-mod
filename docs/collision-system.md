# Map Collision System (mpColl)

The map collision system handles all entity-vs-environment interactions in Kirby Air Ride — ground detection, wall/ceiling collisions, raycasting, and surface response. This is distinct from the **HitColl** system (documented in `hurtdata-system.md`), which handles entity-vs-entity combat collisions.

## System Overview

There are three collision approaches used by different entity types:

| Approach | Used by | CollData allocated? | Description |
|----------|---------|---------------------|-------------|
| **Full mpColl** | Machines, Riders | Yes | Full 0x400-byte CollData with sphere collision, floor/wall/ceiling detection |
| **Item mpColl (coll_kind=1)** | Boxes landing | Yes | CollData with bounce physics, transitions to point coll on landing |
| **Point collision** | Most items (coll_kind≥2) | Optional | Simple downward raycast for ground detection |
| **Raw raycast** | Items (coll_kind=0) | No | Initial spawn raycast, transitions to point coll (coll_kind=3) |

## CollData Struct (0x400 bytes) — `collision.h`

Allocated by `mpColl_Create` (0x80245b4c) from a freelist pool at `0x8056dbc8`. All active CollData objects are linked via `next` into a global list headed at `r13+0x7E4` (`0x805DD8C4`).

### Key Fields

| Offset | Type | Name | Description |
|--------|------|------|-------------|
| 0x000 | CollData* | next | Linked list pointer (global list at 0x805DD8C4) |
| 0x004 | GOBJ* | gobj | Owner GObj |
| 0x008 | Vec3 | pos | Current world position |
| 0x014 | Vec3 | pos_delta | Frame delta (pos - prev_pos), computed by mpColl_Update |
| 0x020 | Vec3 | prev_pos | Previous frame position |
| 0x02C–0x040 | | | Position backup / computed fields |
| 0x044 | mpCollInfo* | coll_info | **Floor/wall/ceiling collision results** (see below). Allocated by mpColl_Create |
| 0x048 | mpCollInfo* | coll_info2 | Second collision info (alternative collision set). Freed in mpColl_Destroy |
| 0x33C | CollShapeKind | coll_shape_kind | Collision shape type (0 = sphere) |
| 0x340 | CollShapeData* | shape_data | Shape parameters. Allocated from pool at 0x8056dbf4 |
| 0x344 | float | radius | Collision sphere radius (set during mpColl_Init) |
| 0x348 | int | param | Mode/flag parameter from mpColl_Init |
| 0x34C | u8 | flags | Bit flags. Bit 7 (0x80) set by mpColl_SetFlag (0x80247e2c) |

### CollShapeData (at CollData+0x340)

| Offset | Type | Name | Description |
|--------|------|------|-------------|
| 0x00–0x0B | | | Unknown |
| 0x0C | Vec3 | direction | Direction/orientation vector |
| 0x30 | float | radius | Sphere radius |
| 0x38 | Vec3 | scale | Scale vector, set during mpColl_Init |

## mpCollInfo Sub-Struct (at CollData+0x44)

Contains the results of floor/wall/ceiling collision checks. Allocated internally by `mpColl_Create` via `mpColl_AllocCollInfo` (0x802416cc). Has three collision "slots" (floor, wall, ceiling), each with validity flags and result data.

### Status Fields

| Offset | Type | Name | Description |
|--------|------|------|-------------|
| 0x118 | int | floor_count | Number of floor collision entries found |
| 0x11C | void* | floor_entry | Pointer to floor collision entry data |
| 0x144 | int | floor_valid | Non-zero when floor collision result is available |
| 0x148 | void* | wall_entry | Pointer to wall collision entry data |
| 0x170 | int | wall_valid | Non-zero when wall collision result is available |
| 0x174 | void* | ceil_entry | Pointer to ceiling collision entry data |
| 0x19C | int | ceil_valid | Non-zero when ceiling collision result is available |

### Collision Entry Layout (0x1C bytes)

Each entry contains a triangle reference and result data:

| Offset | Type | Description |
|--------|------|-------------|
| 0x00 | u8 | Flags |
| 0x01 | u8 | Type flags (bit 0=floor, bit 1=wall, bit 2=ceiling) |
| 0x04 | void* | Vertex data pointer (contains triangle ID at +0x00, normal Vec3 at +0x08) |
| 0x08–0x18 | | Additional collision result data |

## Item Collision — coll_kind Dispatch

Items use the `coll_kind` field (3-bit field in ItemData+0x359, **bits 2-4, mask 0x1C**) to select their collision strategy. This is set during `Item_Create` from the `coll_kind` parameter of `Item_InitDesc`, and stored by `CityItem_AllocCollData` (0x80254318) via `rlwimi r0,kind,2,27,29` (insert 3 bits at byte position 2). It is read back in `Item_GenericEnvColl` via `rlwinm. r0,byte,30,29,31` (rotate right 2, mask low 3 bits). **Note:** the `item.h` bitfield models `coll_kind : 3` at the low 3 bits (mask 0x07), but the `rlwimi`/`rlwinm` place the field at bits 2-4 (mask 0x1C); the header bitfield/mask comment is a known discrepancy.

### coll_kind Values

| Value | CollData allocated? | Behavior | Used by |
|-------|---------------------|----------|---------|
| **0** | No | **Raw raycast / initial spawn.** Iterative ground search (up to 10 raycast iterations), then **transitions to 3** once grounded. Only valid during the first frame — requires `is_airborne != -1`. If CollData is NULL and code takes this path, the floor/wall/ceiling reads in `CityItem_GetGroundInfo` crash. |
| **1** | **Yes** (via mpColl_Create) | **Full CollData collision.** Updates mpColl each frame, reads floor/wall/ceiling from mpCollInfo. On wall/ceiling hit: zeroes velocity. On floor hit: calls `ItemColl_BounceLand` for bounce response. After landing (bounce settles): **destroys CollData, transitions to point coll (3)**. |
| **2** | No | **Point collision with HandleLand.** Uses `ItemColl_HandleLand` (0x80255aa4) for ground detection via raycast. If CollData exists (optional), updates it but uses a different parameter function. |
| **3** | No | **Point collision with HandleLand.** Same as 2. This is the steady-state for most items after landing. **Most items use this.** |
| **4–7** | No | Same as 2/3. No behavioral distinction between values 2–7. |

### Lifecycle: Box-Spawned Item

1. `Item_Create` → `CityItem_AllocCollData(ItemData, 1)` → allocates CollData, stores coll_kind=1
2. Each frame: `Item_GenericEnvColl` → coll_kind=1 path → updates mpColl, checks floor/wall/ceiling
3. On bounce: `ItemColl_BounceLand` handles physics response
4. When bounce settles: **destroys CollData** (`mpColl_Destroy`), sets `coll_data = NULL`, writes coll_kind=3
5. Steady state: coll_kind=3, no CollData, uses `ItemColl_HandleLand` for simple ground tracking

### Lifecycle: Sky-Spawned / Mod-Spawned Item

1. `Item_Create` with `coll_kind=3, is_airborne=1` → no CollData allocated
2. Initial raycast at spawn to find ground (controlled by `is_airborne` / ItemDesc[0x50])
3. Each frame: `Item_GenericEnvColl` → coll_kind≥2 path → `ItemColl_HandleLand`

### The coll_kind=0 Crash Bug

If `coll_kind` is 0 (either intentionally as initial spawn state, or from garbage stack data), the code path at `Item_GenericEnvColl` falls through to the raw raycast handler. This path:
1. Checks `ItemData->is_airborne` — if -1, exits immediately (safe)
2. Otherwise, calls `ItemColl_GetGenericCollFlags` on `ItemData->point_coll.raycast_idx`
3. If flags indicate no collision needed, attempts raycast
4. **The danger:** `CityItem_GetGroundInfo` (0x80254464) unconditionally dereferences `ItemData->coll_data` (offset 0x1A4) to read `coll_info->floor_valid` at +0x44→+0x144. If `coll_data` is NULL (which it is when `CityItem_AllocCollData` was called with coll_kind=0), this crashes with a DSI at address 0x00000044.

`coll_kind=0` is used by the game's debug item spawner (`3DDebug_CheckToSpawnItem`), which presumably handles this case specially or always transitions before `Item_GenericEnvColl` runs.

## Point Collision (ItemData Fields)

Items using point collision (coll_kind ≥ 2) track their ground state in ItemData directly:

| Offset | Type | Name | Description |
|--------|------|------|-------------|
| 0x1A4 | CollData* | coll_data | NULL for point-collision-only items |
| 0x1A8 | int | point_coll.raycast_idx | Triangle ID from last raycast (-1 = no ground) |
| 0x1AC | Vec3 | point_coll.land_pos | Ground position from last raycast |
| 0x1C8 | Vec3 | fall_dir | Gravity/down direction for raycasting |
| 0x1D4 | int | is_airborne | If not -1, ground raycast is performed each frame |
| 0x35A | u8 | flags | Bit 4 = grounded (set by Item_SetGroundedFlag) |

## Raycast System

### EnvColl_Raycast (0x800d1ac4)

Wrapper around `Raycast_Do` (0x800d9958). Takes start position, end position, and output buffer. Uses the global map collision data at `r13+0x5EC` (+0x54 sub-offset).

### Item_Raycast (0x802546e4, size 0xe8)

Iterative raycast for items. Given position + direction + iteration count, calls `EnvColl_Raycast` in a loop (up to N steps), accumulating position. Returns the final triangle ID.

### CityItem_FindGroundBelow (0x802547cc, size 0x24c)

Ground detection for items. Raycasts downward from item position (offset along the stored ground normal at ItemData+0x1C8). Stores result in `ItemData->point_coll.raycast_idx` (+0x1A8) and `point_coll.land_pos` (+0x1AC).

### PointCollision_EnsureIDValid (0x800d1838)

Validates a triangle ID against the global map collision manager. Returns 0 if valid, 1 if invalid/out-of-range.

### PointCollision_GetNormalByID (0x800d1860)

Given a triangle ID and output Vec3 pointer, looks up the triangle (stride 0x40 per entry in map data) and copies the 12-byte surface normal.

## Item_InitDesc — Full Parameter List

`Item_InitDesc` (0x802509a0) takes 13 parameters: 8 GPR (r3-r10) + 1 FPR (f1) + 4 stack. The GC EABI does **not** shadow floats in GPRs.

```c
void Item_InitDesc(
    ItemDesc *desc,       // r3: output struct
    ItemKind kind,        // r4: item kind
    float scale,          // f1: item scale
    int spawn_type,       // r5: spawn context (0 = default)
    Vec3 *pos,            // r6: spawn position
    Vec3 *up,             // r7: up vector (NULL for default)
    Vec3 *forward,        // r8: forward vector (NULL for default)
    int x40,              // r9: maps to ItemData[0x3C], usually -1
    int x44,              // r10: maps to ItemData[0x40], usually -1
    int is_airborne,      // stack: -1=skip raycast, other=do raycast
    int coll_kind,        // stack: 0/1/2/3 (see table above), 3 for most items
    int x38,              // stack: maps to ItemData[0x34], usually -1
    int x3c               // stack: maps to ItemData[0x38], usually -1
);
```

### Vanilla Caller Values

| Caller | is_airborne | coll_kind | x38 | x3c |
|--------|-------------|-----------|-----|-----|
| 3DDebug_CheckToSpawnItem | 1 | 0 | -1 | -1 |
| PowerUp_SpawnFromSky | 1 | 0 | variable | variable |
| Box_SpawnContents | 1 | 1 or 2 | -1 | -1 |
| zz_80253ad0_ (item spawn) | 1 | 1 | -1 | -1 |
| **Mod code (recommended)** | **1** | **3** | **-1** | **-1** |

(`3DDebug_CheckToSpawnItem` = `zz_80081600_`, `Box_SpawnContents` = `zz_80253610_` in the Ghidra project. The map symbol for `Item_InitDesc` is `CityItem_InitDesc` at 0x802509a0 — `Item_InitDesc` is the link.ld export name used by mod code.)

## Item Environment Collision Pipeline

### Per-Frame Flow (GObj proc)

```
CityItem_EnvColl (0x8024f814)          — GObj proc callback
  └─ ItemData->envcoll_callback()       — per-kind callback, usually:
       └─ Item_GenericEnvColl (0x80255438, via wrapper)
            ├─ Extract coll_kind from ItemData+0x359 bits 2-4
            ├─ if coll_kind == 0: raw raycast path
            │    ├─ iterative EnvColl_Raycast (up to 10 steps)
            │    ├─ store triangle ID → ItemData+0x1A8
            │    └─ transition coll_kind → 3
            ├─ if coll_kind == 1: full CollData path
            │    ├─ mpColl_Update (pos/direction/scale)
            │    ├─ mpColl_SetDefaultParams
            │    ├─ mpColl_UpdateShapeExtents
            │    ├─ CityItem_GetGroundInfo → read floor/wall/ceiling
            │    ├─ if wall/ceiling hit: zero velocity
            │    ├─ if floor hit: ItemColl_BounceLand
            │    └─ if settled: mpColl_Destroy, coll_kind → 3
            └─ if coll_kind >= 2: point collision path
                 ├─ (optional) mpColl_Update if coll_data exists
                 ├─ ItemColl_HandleLand (raycast + surface response)
                 └─ if landed: mpColl_Destroy (if exists), coll_kind stays
```

### Key Item Collision Functions

| Address | Size | Name | Description |
|---------|------|------|-------------|
| 0x8024F814 | 0x34 | CityItem_EnvColl | GObj proc: dispatches to envcoll_callback |
| 0x80254318 | 0x6C | CityItem_AllocCollData | Allocates CollData if coll_kind==1; writes coll_kind bits |
| 0x80254384 | 0x80 | CityItem_InitCollData | Sets up CollData with position/scale from ItemData |
| 0x80254444 | 0x20 | CityItem_ValidatePointCollID | Wrapper: PointCollision_EnsureIDValid |
| 0x80254464 | 0x1DC | CityItem_GetGroundInfo | Reads CollData→coll_info floor/wall/ceiling results. **Crashes if coll_data is NULL** |
| 0x802546E4 | 0xE8 | Item_Raycast | Iterative raycast (N steps along direction) |
| 0x802547CC | 0x24C | CityItem_FindGroundBelow | Downward raycast, stores to ItemData point_coll |
| 0x80255438 | 0x370 | Item_GenericEnvColl | Main dispatch: coll_kind → collision path |
| 0x802557A8 | 0x14 | Item_SetGroundedFlag | Sets bit 4 of ItemData+0x35A |
| 0x802557BC | 0x2E8 | ItemColl_BounceLand | Bounce/wall collision response for coll_kind=1 |
| 0x80255AA4 | 0x4EC | ItemColl_HandleLand | Point-collision ground landing (coll_kind≥2) |

### Core mpColl Functions

| Address | Size | Name | Description |
|---------|------|------|-------------|
| 0x80241228 | 0x5C | mpColl_Alloc | Allocates CollData from pool, links to global list |
| 0x802412D8 | 0x74 | mpColl_InitSubsystems | Initializes coll_info entries |
| 0x802414D4 | 0x08 | mpColl_GetFirstCollObj | Returns head of global CollData linked list |
| 0x802415A8 | 0xF0 | mpColl_GetSphereRadius | Gets collision sphere radius |
| 0x802416CC | 0x40 | mpColl_AllocCollInfo | Allocates mpCollInfo sub-struct |
| 0x8024178C | 0x144 | mpColl_AllocCollInfoEntries | Allocates floor/wall/ceiling entry arrays |
| 0x802418D0 | 0xE8 | mpColl_InitCollInfo | Zeros all collision result fields |
| 0x802433C4 | 0xB8 | mpColl_GetUnkPos | Position computation helper |
| 0x80245B4C | 0xC4 | mpColl_Create | Full creation: alloc + shape + init chain |
| 0x80245C10 | 0x1A0 | mpColl_Init | Sets position/direction/scale, inits subsystems |
| 0x80245DB0 | 0x120 | mpColl_Reinit | Re-initializes with new position/direction |
| 0x80245ED0 | 0xA0 | mpColl_Destroy | Frees coll_info, coll_info2, shape_data, unlinks |
| 0x80245F70 | 0x164 | mpColl_Update | Per-frame: update pos, compute delta, update shape |
| 0x802460D4 | 0x38 | mpColl_SetDefaultParams | Sets default collision parameters |
| 0x802461B4 | 0x38 | mpColl_SetDefaultParams2 | Alternative parameter set (for coll_kind≥2) |
| 0x8024625C | 0xA4 | mpColl_UpdateShapeExtents | Updates shape extents from scale |
| 0x80247E2C | 0x2C | mpColl_SetFlag | Sets/clears bit 7 of flags byte |
| 0x80247FAC | 0xC4 | Machine_GetGroundHandle | Searches collision entries for ground type 0x19 |
| 0x802485E0 | 0x5D4 | mpColl_UpdateCollision | Large collision processing update |

### Map/Ground Functions

| Address | Size | Name | Description |
|---------|------|------|-------------|
| 0x800CEC28 | 0x1C | grGetGroundTypeFromTriangleID | Returns ground type from triangle ID |
| 0x800CECD4 | 0x98 | grGetUnkFromTriangleID | Returns float value for a triangle |
| 0x800CEE08 | 0x1C | ItemColl_GetGenericCollFlags | Returns collision flags for an item |
| 0x800D1838 | 0x28 | PointCollision_EnsureIDValid | Validates triangle ID against map data |
| 0x800D1860 | 0x90 | PointCollision_GetNormalByID | Looks up triangle normal (stride 0x40 per entry) |
| 0x800D1AC4 | 0x70 | EnvColl_Raycast | Wrapper around Raycast_Do |
| 0x800D9958 | 0x4DC | Raycast_Do | Core raycast against map geometry |
| 0x800D4F20 | 0x98 | calcDistanceFromOOB | Minimum signed clearance from the stage OoB death box |

## Stage Out-of-Bounds Death Box

The playfield is bounded by an axis-aligned box stored in the stage file,
separate from the triangle-mesh collision above. It lives in the
`StageNode` sub-block at `GrData+0x04` (`externals/hoshi/include/stage.h`):

```
StageNode.oob_min   // +0xCC  Vec3 (minX, minY, minZ)
StageNode.oob_max   // +0xD8  Vec3 (maxX, maxY, maxZ)
```

For City Trial (`GrCity1.dat`) these are `(-1300, -300, -1300)` /
`(1300, 1500, 1300)`.

`calcDistanceFromOOB(Vec3 *pos)` (0x800d4f20) reads the box from
`(*stc_grobj)->gr_data->stage_node` and returns the minimum signed distance to
any of the six planes: positive while `pos` is inside the box, negative once
it has crossed a wall. Out-of-bounds death/fall logic uses this clearance.
The box is plain spatial data (not a JObj), so scaling the stage visuals does
not move it — `event_scale_change.c` rescales `oob_min`/`oob_max` explicitly
to keep the kill box matched to the resized stage.

## Who Uses What

### Machines (MachineData)

Machines use full mpColl through their own collision processing:
- `Machine_EnvCollThink` (0x801c65a8) — GObj proc for environment collision
- `Machine_ProcessEnvColl` (0x801e5108) — main collision processing
- CollData stored at an offset in MachineData (not yet fully mapped)

### Riders (RiderData)

- `Rider_EnvColl` (0x8018f734) — GObj proc
- `Rider_EnvColl_Grounded` (0x801b8ec4) — ground state handler
- CollData at RiderData+0x670

### Enemies (EnemyData)

- `EventActor_EnvCollRaycastDown` (0x80204e24) — downward raycast
- `EventActor_EnvCollRaycastUp` (0x80204e44) — upward raycast
- `EventActor_GroundSnap` (0x80204fac) — snap to ground surface
- `EnemyActor_GroundFollowMovement` (0x80208bd4) — movement along ground
- `Enemy_GroundPhysicsVelocity` (0x80209104) — ground physics
- `Enemy_GroundPhysicsSurface` (0x802096b4) — surface physics
- `Enemy_GroundAttach` (0x8020a664) — ground attachment
- Map collision object at EnemyData+0x594

### Items (ItemData)

- `CityItem_EnvColl` (0x8024f814) — GObj proc
- `Item_GenericEnvColl` (0x80255438) — main dispatch
- CollData at ItemData+0x1A4 (may be NULL for point-collision-only items)
- coll_kind at ItemData+0x359 bits 2-4

## Practical Guide for Mod Code

### Spawning Items Safely

Always pass all 13 parameters to `Item_InitDesc`. For most items:

```c
ItemDesc desc;
Item_InitDesc(&desc, item_kind, scale, 0,
              &pos, NULL, NULL, -1, -1,
              1,    // is_airborne: 1 = do initial ground raycast
              3,    // coll_kind: 3 = point collision (safe, no CollData needed)
              -1,   // x38: default
              -1);  // x3c: default
GOBJ *item = Item_Create(&desc);
// Item_Create returns NULL if raycast validation fails (coll_kind=3 + bad position)
```

### coll_kind Selection Guide

| Scenario | coll_kind | is_airborne | Notes |
|----------|-----------|-------------|-------|
| Item at known good position | 3 | 1 | Standard. Raycast finds ground, point coll tracks it. |
| Item that should bounce on landing | 1 | 1 | Allocates CollData. Bounces, then transitions to 3. |
| Item placed exactly (no raycast) | 3 | -1 | Skips initial ground raycast. Item stays at spawn pos. |
| **AVOID** | 0 | any | Crashes if CollData is NULL (which it will be). |

### Destroying Items with CollData

If you create an item with coll_kind=1 (has CollData), the system handles cleanup automatically — when the bounce settles, `Item_GenericEnvColl` calls `mpColl_Destroy` and NULLs the pointer. If you destroy the item GObj directly, the item's destructor should handle freeing CollData.
