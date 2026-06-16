# Scale Change Event

> **Status: IMPLEMENTED & ACTIVE (visual/skybox only — WIP).** `event_scale_change.c/h`
> are registered as `CUSTOM_EVKIND_SCALE_CHANGE` (18) in `custom_events.c` with all
> three handlers (`ScaleChange_Start` / `ScaleChange_Active` / `ScaleChange_End2`)
> in the dispatch table, weight 20, BGM `0x32`, sky preset 3 ("Dusk 2"), HUD text
> "The world is growing!". It is reachable via the natural extended roll
> (`CustomEvents_ExtendedRoll`) and via the API `Do(18)`. There is **no** dedicated
> debug-menu wire.
>
> **Caveat:** the code scales the wrong JObj for "the stage." It scales
> `GrObj->backdrop_jobj` (offset `+0xF4`) believing it to be the collision model,
> but `+0xF4` is the **distant skybox/backdrop mesh**, not collision and not the
> playable terrain. See "What the code actually scales" below. The playable terrain
> (the visible ground) is the JObj attached to the stage GOBJ and is **not** touched
> by `ScaleChange_*` — only the stage_node->scale field and the backdrop mesh are.

## Concept

A custom City Trial event intended to scale the entire stage larger or smaller for
the duration of the event. As implemented it is visual-only and, in practice, only
affects the skybox/backdrop mesh plus the `stage_node->scale` field and the OOB
boundary box; collision (and the playable terrain mesh) does not scale.

## Stage Scale System

### Data Location

The stage scale is stored in `GrData.stage_node->scale` (offset 0x8 within stage_node). Accessed at runtime via `grGetStageScale()` (0x800d3058):

```c
// grGetStageScale reads: stc_grobj_ptr -> gr_data (+0x8) -> stage_node (+0x4) -> scale (+0x8)
// Returns float, with a fallback default if stage_node is NULL.
```

### stage_node Layout (Known Fields)

```
+0x00  int    x0
+0x04  float  machine_accel
+0x08  float  scale
+0x0C  float  gravity_unk
+0x10  Vec3   gravity_force        ((0, -1, 0) for City Trial)
+0x1C  int    fog_flags
...
+0xCC  float  oob_min_x            (OOB death boundary AABB)
+0xD0  float  oob_min_y
+0xD4  float  oob_min_z
+0xD8  float  oob_max_x
+0xDC  float  oob_max_y
+0xE0  float  oob_max_z
```

### Visual Model Scaling

The stage model is a JOBJ hierarchy. At init, `3D_CreateStageModel` (0x800dcbf0,
size 0x318; symbol `CreateStageModel_3D` in `link.ld`) reads `grGetStageScale()` and
applies it to the JObjs it instantiates from `GrData.model_section` (a `ModelSection`,
see `stage.h`):

1. **Terrain JOBJ** (`model_section->terrain`, slot +0x00) — the **playable
   ground geometry**. Attached to the stage GOBJ via the object-add call
   `0x80429c14`. This is the JObj the engine treats as "the stage model."
2. **Backdrop JOBJ** (`model_section->backdrop`, slot +0x04) — the distant
   skybox/horizon mesh. Stored at extended GrObj offset 0xF4 (`stw r3,244(r30)`
   at 0x800dce3c). NULL if the stage ships no backdrop. (Field `GrObj.backdrop_jobj`
   in `stage.h`; see `docs/sky-backdrop-system.md`.)

Each instantiated JOBJ has its `JOBJ.scale` (Vec3 at JOBJ+0x2C) set to the scale
value (stores into JOBJ+0x2c/+0x30/+0x34), followed conditionally by
`JObj_SetMtxDirtySub()` (map symbol `HSD_JObjSetMtxDirtySub`, 0x8040d92c) to recompute
transforms.

**Note:** There is **no** separate "collision model JOBJ" at +0xF4 — that slot
is the backdrop. Collision uses pre-baked spatial data (see "Collision System"
below), not a scalable JObj.

JOBJ scale struct (from obj.h):
```c
struct JOBJ {
    ...
    Vec3 scale;     // 0x2C
    Vec3 trans;     // 0x38
    ...
};
```

### What the code actually scales

`event_scale_change.c` defines `SCALE_MULTIPLIER 1.5f` (the stage grows to 1.5×, matching
the "The world is growing!" HUD text). On `ScaleChange_Start` it saves the original
scale + OOB box, then applies the new scale via two helpers:

- `GetVisualJObj()` → `grobj->gobj->hsd_object` — **intended** to be the visual model.
- `GetCollisionJObj()` → `grobj->backdrop_jobj` (GrObj +0xF4) — **named** "collision"
  in the code but is actually the **backdrop/skybox** mesh (see above).

```c
// Paraphrase of ScaleChange_Start / _Active / _End2
GrObj *grobj = *stc_grobj;
float new_scale = grobj->gr_data->stage_node->scale * SCALE_MULTIPLIER;

ApplyJObjScale(GetVisualJObj(), new_scale);      // grobj->gobj->hsd_object
ApplyJObjScale(GetCollisionJObj(), new_scale);   // grobj->backdrop_jobj (+0xF4) — actually the skybox
grobj->gr_data->stage_node->scale = new_scale;
// + scale the 6 OOB floats at stage_node+0xCC by SCALE_MULTIPLIER
```

`ApplyJObjScale` writes `jobj->scale.{X,Y,Z}` then calls `JObj_SetMtxDirtySub(jobj)`.

`ScaleChange_Active` re-applies the scale + OOB every frame as insurance.
`ScaleChange_End2` **restores** the saved original scale + OOB box. State is tracked
via a static `scale_modified` flag.

Whether `grobj->gobj->hsd_object` is the playable terrain JObj (and therefore whether
the visible ground actually changes size, or only the skybox + stage_node->scale field)
is an open question — see below.

### grGetStageScale Callers

`grGetStageScale()` is called by many systems at init and some at runtime:
- `3D_CreateStageModel` (3 calls — visual, collision, secondary JOBJ)
- `Sky_InitFog`
- `fn_grSetupNullpos`
- `PlyCam_SetStartingPosition?`
- Various unnamed functions in camera, fog, and stage setup

## Collision System — The Blocker

### Architecture

All ground/environment collision routes through `EnvColl_Raycast` (0x800d1ac4), a thin wrapper:

```c
int EnvColl_Raycast(float *ray_origin, float *ray_endpoint, float *out_normal)
{
    Raycast_Start();   // stubbed (NOP)
    int result = Raycast_Do(stc_grobj_ptr + 0x54, ray_origin, ray_endpoint, 1, 0, out_normal);
    Raycast_End();     // stubbed (NOP)
    return result;
}
```

### Pre-Baked Collision Data

`Raycast_Do` (0x800d9958) uses pre-computed spatial data:
- `stc_grobj_ptr + 0x700` — collision face/vertex data pointer
- `stc_grobj_ptr + 0x704` — spatial partitioning structure (BVH/octree)

This data includes pre-baked bounding boxes at world-scale coordinates (offsets +0x14/0x18/0x1c for center, +0x20/0x24/0x28 for extents) used for broad-phase culling before per-triangle intersection tests via `zz_800d95dc_`.

**This collision data is computed once at stage load and is NOT affected by JOBJ scale changes.** Scaling the JOBJ only changes the visual model; the collision mesh stays at original scale.

### OOB Death Boundary

`Machine_CheckFallDeath` (0x801e6464, size 0xBC) calls `calcDistanceFromOOB`
(0x800d4f20, size 0x98) with the ray position at `r30+1000`, and that routine reads
the AABB at `stage_node+0xCC..0xE0` (X uses +0xCC/+0xD8, Y uses +0xD0/+0xDC, Z uses
+0xD4/+0xE0). These CAN be modified at runtime (just floats in the
stage_node struct). `ScaleChange_Start`/`_Active` scale all 6 by `SCALE_MULTIPLIER` and
`ScaleChange_End2` restores them, but this only adjusts the kill boundary — it doesn't
fix ground collision.

### EnvColl_Raycast Callers (28+)

All collision in the game flows through this single function:
- Machine ground/wall collision (`Machine_ProcessEnvColl`, `Machine_EnvCollThink`)
- Item physics (`Item_Raycast`, `Item_GenericEnvColl`, `ItemColl_HandleLand`)
- Enemy/actor ground snap (`EventActor_GroundSnap`, `EventActor_SetupVelocity`)
- Camera collision
- Various rider/player collision checks

## Approaches to Fix Collision

### Option A: Hook EnvColl_Raycast (REPLACEFUNC)

Replace `EnvColl_Raycast` to transform ray coordinates from scaled world space back to original collision space:

```c
int ScaleChange_RaycastHook(float *origin, float *endpoint, float *out_normal)
{
    if (!scale_active)
        return Raycast_Do(stc_grobj_ptr + 0x54, origin, endpoint, 1, 0, out_normal);

    float inv = 1.0f / scale_multiplier;
    float so[3] = { origin[0]*inv, origin[1]*inv, origin[2]*inv };
    float se[3] = { endpoint[0]*inv, endpoint[1]*inv, endpoint[2]*inv };
    return Raycast_Do(stc_grobj_ptr + 0x54, so, se, 1, 0, out_normal);
}
```

**Unresolved**: Callers use the raycast result (hit distance, hit normal) to position objects. If inputs are in original space, the computed hit position will be in original space too. Callers would place objects at original-scale positions, not scaled positions. The output transform is the open piece:
- `out_normal` (Vec3) — direction vector, scale-invariant, probably fine
- Hit distance — returned somehow (possibly in f1 register or via the int return value), would need to be multiplied by S
- Hit position — likely computed by callers from origin + distance * direction; if distance is wrong, position is wrong

**Needs**: Full understanding of how callers interpret the raycast return value and output normal to determine what else needs transforming.

### Option B: Scale Collision Vertices Directly

Find the actual collision vertex positions in the pre-baked data at `stc_grobj_ptr+0x700` and multiply all coordinates by S at event start, restore at event end.

**Pros**: Cleanest result — collision matches visuals perfectly, no output transform issues.
**Cons**: Need to reverse-engineer the collision vertex storage format. Must find all vertex arrays and their counts. Risk of missing some data.

### Option C: Visual-Only Effect

Accept that scaling is visual only. The event would create a surreal visual effect where the stage appears to grow/shrink but physics stay the same.

## Current Implementation Status

- `event_scale_change.c/h` — **implemented & active**, registered as
  `CUSTOM_EVKIND_SCALE_CHANGE` (18) in `custom_events.c`. All three handlers
  (`ScaleChange_Start`, `ScaleChange_Active`, `ScaleChange_End2`) wired into
  `custom_functions[]`; params: duration 900 (~15s), is_siren=1, sky preset 3,
  BGM `0x32`, weight 20, label "Scale Change", HUD "The world is growing!".
- Scale factor is fixed at `SCALE_MULTIPLIER = 1.5f` (grow only; no shrink).
- Applies scale to `gobj->hsd_object`, `grobj->backdrop_jobj` (+0xF4 — **the
  skybox, not collision**), and `stage_node->scale`, plus the OOB box.
- Restore on event end is implemented (`ScaleChange_End2` restores scale + OOB).
- Ground collision does NOT scale — machines fall through any visually-scaled floor.
- The playable-terrain JObj is likely **not** the one being scaled (see caveat at
  top); the most visible effect is the skybox growing + OOB box growing.
- Event has **no** dedicated debug-menu wire; reachable only via natural roll / `Do(18)`.

## Symbols

| Symbol | Address | Size | Notes |
|--------|---------|------|-------|
| `grGetStageScale` | 0x800d3058 | 0x24 | Reads stage_node->scale (stc_grobj→+0x8→+0x4→lfs +0x8) |
| `3D_CreateStageModel` (`CreateStageModel_3D` in link.ld) | 0x800dcbf0 | 0x318 | Instantiates terrain + backdrop JObjs, applies scale at init |
| `EnvColl_Raycast` | 0x800d1ac4 | 0x70 | Thin wrapper, single entry point for all collision |
| `Raycast_Do` | 0x800d9958 | 0x4DC | Core raycast implementation (stc_grobj+0x54) |
| `calcDistanceFromOOB` | 0x800d4f20 | 0x98 | Reads OOB bounds from stage_node+0xCC..0xE0 |
| `Machine_CheckFallDeath` | 0x801e6464 | 0xBC | Calls calcDistanceFromOOB |
| `JObj_SetMtxDirtySub` (`HSD_JObjSetMtxDirtySub` in map) | 0x8040d92c | 0x334 | Recompute JObj transform after scale write |
| `zz_800d95dc_` | 0x800d95dc | ? | Per-triangle intersection test (unverified) |
