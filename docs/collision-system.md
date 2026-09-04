# Map Collision System (mpColl)

The map collision system handles entity-vs-environment interaction in Kirby Air Ride: ground detection, wall and ceiling pushback, raycasting, and surface response. Entity-vs-entity combat collision (hitboxes, damage, knockback) is an unrelated system with its own per-entity hurt data and its own per-frame pass.

Two halves meet here. **`grColl`** owns the stage's triangle soup and the queries over it. **`mpColl`** owns the per-body collision object (`CollData`) that riders, machines, enemies and some items carry, runs the sweep each frame, and caches the results.

## Stage Collision Data (GrCollParam)

`GrCollParam` at `GrObj+0x54` is the static triangle mesh a rider or machine is actually stopped by. One vertex pool and one triangle array per stage hold both the baked terrain and every placed prop's triangles; each prop owns a contiguous slice of the triangle array through its `GrCollRecord`. The structs (`GrCollParam`, `GrCollTri`, `GrCollRecord`, `GrCollVtx`) are in `externals/hoshi/include/collision.h`; `Gr_GetCollParam()` in `stage.h` fetches the live one.

`GrObj` carries three views of the same arrays:

| Field | Offset | Role |
|---|---|---|
| `coll_max` | `0x00C` | Capacity mirror. `grColl_Alloc` counts into this first and sizes every array in `coll` from it; the fill pass then grows the real counts up to (usually short of) these, so `coll_max.X_num - coll.X_num` is genuine allocated slack. |
| `coll` | `0x054` | The live arrays, passed to the query API as `gcp`. |
| `coll_terrain` | `0x09C` | A window into `coll` owned by the terrain model; each prop gets its own at `yaku_data+0x1C`. |

The arrays behind `coll` and `coll_terrain` are shared but their counts are separate words. The per-frame moving rebake walks the *windows*, while queries read `coll` - so a record reachable only through `coll.moving_record` is queried every frame and never rebaked.

### Allocation lifetime

`grColl_Alloc` (`0x800d6dcc`) zeroes `coll_max`, runs `grColl_CountArrays` over the terrain node and both prop lists to accumulate counts, then makes **nine separate `HSD_MemAlloc` calls** (`0x800d6f7c` onward), one per array, each sized exactly `count * stride`. `grColl_Free` (`0x800d7060`) releases them the same way, field by field. Everything is plain writable MEM1 for the life of the stage.

Consequences for mod code:

- Replacing any `coll.X` pointer with a mod-owned buffer both leaks the original and hands a foreign pointer to `OSFreeToHeap`.
- Raising a `coll_max` count *before* the allocation is the supported way to get room: the fill pass leaves the surplus as slack at the tail of that array, and the game still owns and frees the whole block. `zz_800d6774_` asserts that a later prop attach still fits, so anything raising a count must raise `coll_max` too.
- `grColl_Free` frees `coll.zone` and `coll.moving_rough` twice each and never frees `coll.aux` or `coll.moving_zone`. The scene-change heap reset covers for it.

### Query path

Every query runs two passes; there is no linear scan of the whole triangle array anywhere in gameplay code.

- **Moving pass** - a brute-force walk of `GrCollParam.moving_record`, each record AABB-tested and then its whole triangle slice scanned. Moving geometry is deliberately absent from the KD-tree and reachable only this way.
- **Static pass** - a walk of the KD-tree baked into the stage archive (`GrObj.coll_tree`, from `GrData+0x48`), whose leaves hold 16-bit indices into `GrCollParam.tri`.

Both funnel into the two narrowphase primitives, `grColl_RayVsTri` (`0x800d95dc`) and `grColl_SweptSphereVsTri` (`0x802448b0`), which take a triangle **index** - derived in the moving pass as `(tri_ptr - GrCollParam.tri) >> 6`. Triangles must therefore live in that one contiguous array whichever way they are found. Both passes accumulate the nearest hit into a single triangle index that starts at `-1` and comes back in `r3`, with the caller's `out_pos` receiving that hit's point.

Entry points above the primitives:

| Function | Address | Role |
|---|---|---|
| `grColl_SweptSphereQuery` | `0x800d9e34` | Moving sweep + tree walk; moving pass gated on arg 5. |
| `Raycast_Do` | `0x800d9958` | Moving sweep + tree walk over a segment, moving pass always. Call it through the `Raycast_*` wrappers, which supply the mask. |
| `mpColl_SweptSphereMapColl` | `0x802454f8` | Rider/machine wall and floor pushback under `mpColl_UpdateCollision`. This is what actually stops a machine. |
| `mpColl_InsertContact` | `0x80241ca8` | Caches the winning triangle id in the under/wall/top slot. |

Four byte-identical raycast wrappers differ only in the kind mask and filter they pass to `Raycast_Do`. Each returns the nearest triangle id along the segment or `-1` and writes the hit point to `out_pos`:

| Wrapper | Address | Mask / filter |
|---|---|---|
| `Raycast_Any` | `0x800d1a54` | mask 7, filter 0 - every surface |
| `Raycast_Ground` | `0x800d1ac4` | mask 1, filter 0 - `GRCOLL_KIND_UNDER` only |
| `Raycast_Wall` | `0x800d1b34` | mask 2, filter 0 - `GRCOLL_KIND_WALL` only |
| `Raycast_AnyTagged` | `0x800d1ba4` | mask 7, filter 1 - additionally drops anything that is neither ground nor flagged `0x8000` |

The mask decides which surfaces exist as far as the caller is concerned: `Raycast_Ground` cannot see a wall, and `Raycast_Wall` cannot see the floor in front of it.

### Gates: kind and state

Both primitives gate first on `kind_mask & GrCollTri.kind`, then on `GrCollTri.state` - bit 6 (`GRCOLL_STATE_COLLIDABLE`) set and bit 7 (`GRCOLL_STATE_DEGENERATE`) clear. `grColl_SweptSphereVsTri` additionally gates on the record AABB and asserts if bit 7 is set once past those checks; it takes `GrCollTri.normal` as the plane normal, so that must be unit length.

`GrCollTri.kind` bits 0..2 are the baked surface category (`GRCOLL_KIND_UNDER` / `WALL` / `TOP`), matching `mpCollRec.best_kind`. Every query ANDs its own mask against them before anything else, so **this** decides whether a triangle can be stood on, walled off, or hit at all - not a runtime normal test. A surface built at runtime must set them to match its own facing or the consumers it belongs to will never look at it.

Clearing state bit 6 hides a triangle from every query, raycasts included. It does **not** reach entities holding a cached triangle id: `ItemData.point_coll` and the `grColl_GetTri*` / `grGetGroundTypeFromTriangleID` family do no such check and keep reading the retired triangle's fields until their next query.

A live City Trial triangle reads state `0x60` and most often flags `0x00008000`; no triangle carries `GRCOLL_STATE_COLLIDABLE` without `GRCOLL_STATE_SURFACE_PARAM`.

### Triangle id validation

Triangle ids are cached at full 32 bits and range-checked in exactly one place: `PointCollision_EnsureIDValid` (`0x800d1838`) rejects anything outside `[0, GrCollParam.tri_num)`, and every ground / landing / shadow consumer runs it. The wall sweep and the surface-property lookups do not - they index `GrCollParam.tri` directly by whatever id they are handed.

## Per-Body Collision (CollData)

`CollData` is the 0x400-byte per-body collision object, declared in `collision.h`. `mpColl_Create` (`0x80245b4c`) takes one from a freelist pool at `0x8056dbc8`, allocates its shape data from a second pool at `0x8056dbf4` and its `mpCollInfo` via `mpColl_AllocCollInfo` (`0x802416cc`), and links it into a global list headed at `r13+0x7E4` (`0x805DD8C4`) that `mpColl_GetFirstCollObj` (`0x802414d4`) walks. `mpColl_Destroy` (`0x80245ed0`) frees the sub-allocations and unlinks.

Only the shape kind `Mp_CollShapeKind_Sphere` exists. The sphere radius is lerped between two endpoints in the shape data by `mpColl_GetSphereRadius` (`0x802415a8`); the collider's own `radius` at `+0x344` is what the yakumono break force is computed from.

Per frame the owner calls `mpColl_Update` (`0x80245f70`) with the new position, direction and extents; it computes `pos_delta = pos - prev_pos` (`+0x14`), which the rest of the system treats as the body's velocity. `mpColl_SetDefaultParams` (`0x802460d4`) then clears `coll_info` and drives `mpColl_UpdateCollision` (`0x802485e0`) for up to 10 pushback substeps.

`mpCollInfo` (at `CollData+0x44`) holds one `mpCollRec` per substep plus three lists of pointers to the substeps that produced an under / wall / top contact. The useful test is the count: **a body is touching a wall this frame exactly when `wall_rec_num` is non-zero**, and `wall_recs[i]->wall` names the triangle it was stopped by and where. `contact_tri_id` at `+0x1d0` caches the last winning triangle id (`-1` = none).

Owners:

| Entity | CollData | Per-frame entry points |
|---|---|---|
| Machines | `MachineData+0x6F8` | `Machine_EnvCollThink` (`0x801c65a8`) GObj proc, `Machine_ProcessEnvColl` (`0x801e5108`). Radius source is `MachineData+0x46C`; `Machine_InitialCollisionCheck` (`0x801cc7a4`) seeds it at spawn. |
| Riders | `RiderData+0x670` | `Rider_EnvColl` (`0x8018f734`) GObj proc, `Rider_EnvColl_Grounded` (`0x801b8ec4`). |
| Enemies | `EnemyData+0x594` | `EventActor_EnvCollRaycastDown` / `Up` (`0x80204e24` / `0x80204e44`), `EventActor_GroundSnap` (`0x80204fac`), `Enemy_GroundPhysicsVelocity` (`0x80209104`), `Enemy_GroundAttach` (`0x8020a664`). |
| Items | `ItemData+0x1A4`, often NULL | `CityItem_EnvColl` (`0x8024f814`) GObj proc into `Item_GenericEnvColl` (`0x80255438`). |

`Machine_GetGroundHandle` (`0x80247fac`) searches a body's collision entries for ground type `0x19`.

## Scene Objects and Breakable Props

The same sweep that resolves map collision also drives City Trial's breakable props (houses, trees, rocks, walls, holes, star pole, pitfall). There is no separate geometry: a prop's triangles are an ordinary slice of `GrCollParam.tri`, owned by a `GrCollRecord` whose `yaku_gobj` (`+0x90`) points at the prop's GObj and whose `desc_kind` (`+0x8c`) is `3` for a breakable. This is the solid collision that stops you driving through a house *and* the path that breaks it.

`mpColl_UpdateCollision` (`0x802485e0`) runs two phases:

1. **Sweep** (`mpColl_SphereSceneObjColl`, `0x8024d414`) finds the geometrically nearest facing triangle **without checking the collidable bit**, then gates a `collideWithObject` break test on `mpColl_SceneObjBreakGate` (`0x80241574`: collider `+0x34c` bit 2 set **and** `record->desc_kind == 3`). If the break fires it recurses; otherwise it records the contact. `mpColl_Create` clears that bit and only the machine's collision init sets it, so machines are the only body that can break a prop; `mpColl_SetSceneObjBreak(cd, 1)` (`0x80247e58`) arms any other collider.
2. **Response** (`mpResponse_DispatchSceneObjColl`, `0x80248bb4`) walks the accumulated under/wall/top contacts and, for each, **reads `GrCollTri.state` bit 6 - if clear, removes the contact** (`zz_80242508_`) so the body is *not* pushed out of it.

That split is the lever. Clearing the collidable bit across a record's triangles - `grScene_SetInstanceColl(record, 0)` (`0x800d7ad0`) - makes the prop pass-through, because the response stops resolving its penetration. Only break and init code writes that bit; nothing re-arms it per frame, so the clear sticks until something sets it back. The vanilla break tail does this on destruction, `mods/hypernova/src/hypernova_vacuum.c` uses it to retire a vacuumed prop so the moved model leaves no invisible wall behind, and `mods/custom_weather/src/tornado.c` does the same for props it picks up.

### Breaking a prop

The break is reached only through the record's family `coll_func`, dispatched by `collideWithObject(yaku_gobj, collider, gcp, tri_idx, contact)` (`0x800f5004`), which reads the descriptor for `record->desc_id` and calls it. The handler computes

```
force = collider->radius (CollData+0x344) * impactSpeed^2
```

and compares it to the prop's HP. `impactSpeed` comes from `grScene_GetImpactSpeed` (`0x800d8edc`), which **normalizes** the collider delta (`CollData+0x14`), scales by `-1.0`, and projects onto the triangle's outward normal, clamping `<= 0` to `0`. So the delta must point *into* the surface to register at all, and **its magnitude is irrelevant** - only the direction and the collider radius scale the force.

On `force > HP` the handler runs the full break tail: retires the record's collision, hides or state-swaps the mesh, spawns debris and drop items, plays SFX, credits the break to a player's checklist stat, and moves the prop to its broken state. Calling `collideWithObject` directly with a fabricated `CollData` synthesizes a break with all of those consequences and no real contact - that is exactly what the Hypernova vacuum does.

## Collision Zones

Alongside the triangle mesh, a stage carries up to 500 authored **collision zones**: boxes (8 vertices, 12 triangles grouped into 6 faces) whose faces each carry a kind tag - dash panel, super jump, area light, reverb, local death and so on. `grZone_BuildRecord` (`0x800dcf08`) expands each into a 0x140-byte runtime record hanging off `GrObj.x00c`, and `GrCollParam.zone` / `zone_num` are the live array. The mpColl sweep fills `CollData.zone_hit[20]` (`+0x48`) and `zone_hit_num` (`+0x98`) with the indices of every zone the body is inside this frame; the per-kind lookups at `0x80246584`-`0x802478c4` scan that list. Zones do not push a body around - they are pure triggers.

## Stage Out-of-Bounds Death Box

The playfield is bounded by an axis-aligned box stored in the stage file, separate from the triangle mesh above. It lives in the `StageNode` sub-block at `GrData+0x04` (`externals/hoshi/include/stage.h`) as `oob_min` (`+0xCC`) and `oob_max` (`+0xD8`). For City Trial (`GrCity1.dat`) these are `(-1300, -300, -1300)` / `(1300, 1500, 1300)`.

`calcDistanceFromOOB(Vec3 *pos)` (`0x800d4f20`) reads the box from `(*stc_grobj)->gr_data->stage_node` and returns the minimum signed distance to any of the six planes: positive while `pos` is inside the box, negative once it has crossed a wall. Out-of-bounds death and fall logic use this clearance. The box is plain spatial data, not a JObj, so scaling the stage visuals does not move it - anything that resizes a stage has to rewrite `oob_min`/`oob_max` itself. Mods also use it as a cheap stage-extent query: `mods/custom_weather/` scatters clouds, hail and puddles across it.

## Item Collision

Items pick one of four strategies, selected by the `coll_kind` field:

| Approach | Used by | CollData? | Behaviour |
|---|---|---|---|
| Full mpColl | Machines, riders | yes | Sphere collision with floor/wall/ceiling pushback |
| Item mpColl (`coll_kind=1`) | Boxes landing | yes | Bounce physics, then transitions to point collision |
| Point collision (`coll_kind>=2`) | Most items | optional | Downward raycast for ground detection |
| Raw raycast (`coll_kind=0`) | Initial spawn only | no | Iterative ground search, transitions to 3 |

### coll_kind dispatch

`coll_kind` is a 3-bit field in `ItemData+0x359` at **bits 2-4 (mask `0x1C`)**. `CityItem_AllocCollData` (`0x80254318`) writes it with `rlwimi r0,kind,2,27,29`; `Item_GenericEnvColl` reads it back with `rlwinm. r0,byte,30,29,31`. The `item.h` bitfield models the byte MSB-first as `x359_hi:3 / coll_kind:3 / x359_lo:2`, which lands `coll_kind` on the same bits.

| Value | CollData | Behaviour |
|---|---|---|
| **0** | no | Raw raycast, initial spawn only. Up to 10 raycast iterations, then **transitions to 3** once grounded. Requires `is_airborne != -1`. |
| **1** | yes | Full CollData. Updates mpColl each frame and reads floor/wall/ceiling. Wall or ceiling hit zeroes velocity; floor hit calls `ItemColl_BounceLand`. When the bounce settles it **destroys the CollData and transitions to 3**. |
| **2** | optional | Point collision via `ItemColl_HandleLand` (`0x80255aa4`). If a CollData exists it is still updated, with a different parameter function. |
| **3** | no | Same as 2. Steady state, and what most items use. |
| **4-7** | no | Same as 2/3; no behavioural distinction above 2. |

### Lifecycles

A box-spawned item is created with `coll_kind=1`, so `Item_Create` allocates a CollData. Each frame `Item_GenericEnvColl` updates mpColl and checks the three contact slots, running `ItemColl_BounceLand` on floor contact. When the bounce settles it calls `mpColl_Destroy`, NULLs `ItemData.coll_data`, and rewrites `coll_kind` to 3 - after which the item tracks the ground through `ItemColl_HandleLand` alone.

A sky-spawned or mod-spawned item is created with `coll_kind=3, is_airborne=1`: no CollData is allocated, an initial raycast at spawn finds the ground, and every frame after that takes the point-collision path directly.

### The coll_kind=0 crash

If `coll_kind` is 0 - deliberately as an initial spawn state, or from uninitialised `ItemDesc` stack data - `Item_GenericEnvColl` takes the raw-raycast handler. That path checks `ItemData->is_airborne` and exits immediately if it is `-1` (safe), otherwise calls `ItemColl_GetGenericCollFlags` (`0x800cee08`) and attempts the raycast. `CityItem_GetGroundInfo` (`0x80254464`) then unconditionally dereferences `ItemData->coll_data` (`+0x1A4`) to reach `coll_info->under_rec_num`. When `coll_data` is NULL - which it always is when `CityItem_AllocCollData` was called with `coll_kind=0` - that is a DSI at address `0x00000044`.

The only vanilla user of `coll_kind=0` is the debug item spawner at `0x80081600` (unnamed in the symbol map), which never reaches `Item_GenericEnvColl` in that state.

### Point-collision state

Items on the point-collision path keep their ground state in `ItemData` rather than in a CollData: `point_coll.raycast_idx` (`+0x1A8`, the triangle id from the last raycast, `-1` for no ground), `point_coll.land_pos` (`+0x1AC`), `fall_dir` (`+0x1C8`, the direction the ground raycast is cast along), and `is_airborne` (`+0x1D4`).

`is_airborne` is the only reliable "is this item on the ground" test: `1` airborne, `0` resting, `-1` airborne with the ground raycast suppressed. The bit 4 flag in `ItemData+0x35A` set by `Item_SetGroundedFlag` (`0x802557a8`) is **not** that test - it means "a ground reference has been acquired", is set the first frame the raycast finds anything below (for a sky drop, while still hundreds of units up), and is never cleared.

Raycast helpers on this path: `Item_Raycast` (`0x802546e4`) walks `Raycast_Ground` up to N steps along a direction, accumulating position and returning the final triangle id; `CityItem_FindGroundBelow` (`0x802547cc`) raycasts down along `fall_dir` and stores into `point_coll`; `CityItem_ValidatePointCollID` (`0x80254444`) wraps `PointCollision_EnsureIDValid`; `PointCollision_GetNormalByID` (`0x800d1860`) copies a triangle's surface normal.

### Spawning items from mod code

`Item_InitDesc` (`0x802509a0`, `CityItem_InitDesc` in the symbol map) takes **13 parameters**: 8 GPR (r3-r10), 1 FPR (f1, the scale), and 4 on the stack. The GC EABI does not shadow floats in GPRs, so the float argument does not consume a GPR slot and the last four arguments genuinely go on the stack. Pass all 13 - a short call leaves the stack four with garbage, and garbage in the `coll_kind` slot is the crash above.

The prototype is in `externals/hoshi/include/item.h`. The four stack arguments are, in order, `is_airborne`, `coll_kind`, `x38` and `x3c` (the last two map to `ItemData[0x34]` / `[0x38]` and are `-1` in every vanilla caller).

| Caller | is_airborne | coll_kind |
|---|---|---|
| debug item spawner (`0x80081600`) | 1 | 0 |
| `PowerUp_SpawnFromSky` (`0x800ecdf4`) | 1 | 0 |
| `Box_SpawnContents` (`0x80253378`) | 1 | 1 or 2 |
| `zz_80253ad0_` (item spawn) | 1 | 1 |
| **mod code (recommended)** | **1** | **3** |

```c
ItemDesc desc;
Item_InitDesc(&desc, kind, 1.0f, 0, &pos, &up, &forward, -1, -1,
              1,    // is_airborne: 1 = do the initial ground raycast
              3,    // coll_kind: point collision, no CollData needed
              -1, -1);
GOBJ *item = Item_Create(&desc);  // NULL if the spawn raycast fails
```

| Scenario | coll_kind | is_airborne |
|---|---|---|
| Item at a known good position | 3 | 1 |
| Item that should bounce on landing | 1 | 1 |
| Item placed exactly, no raycast | 3 | -1 |
| Never | 0 | any |

With `coll_kind=1` the cleanup is automatic: `Item_GenericEnvColl` destroys the CollData when the bounce settles, and destroying the item GObj early leaves the item destructor to free it.

### Per-frame item pipeline

```
CityItem_EnvColl (0x8024f814)            GObj proc callback
  ItemData->envcoll_callback()            per-kind callback, usually:
    Item_GenericEnvColl (0x80255438)
      coll_kind == 0: iterative Raycast_Ground (up to 10 steps),
                      store triangle id -> point_coll.raycast_idx,
                      transition coll_kind -> 3
      coll_kind == 1: mpColl_Update -> mpColl_SetDefaultParams
                      -> mpColl_UpdateShapeExtents
                      -> CityItem_GetGroundInfo (read the contact slots)
                      wall/ceiling: zero velocity
                      floor:        ItemColl_BounceLand (0x802557bc)
                      settled:      mpColl_Destroy, coll_kind -> 3
      coll_kind >= 2: optional mpColl_Update if coll_data exists,
                      then ItemColl_HandleLand (0x80255aa4)
```

`grGetGroundTypeFromTriangleID` (`0x800cec28`) turns a cached triangle id into its surface type, and `grGetUnkFromTriangleID` (`0x800cecd4`) returns a per-triangle float; both index `GrCollParam.tri` without validating the id.
