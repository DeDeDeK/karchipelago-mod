# Gravity Change Event

A custom City Trial event (`CUSTOM_EVKIND_GRAVITY_CHANGE`, kind 17) that scales the global stage gravity for its duration, affecting machine, item, enemy and camera physics alike. Implemented in `mods/custom_events/src/event_gravity_change.c`.

The load-bearing fact: **gravity is a strength scalar plus a direction vector, in two adjacent `StageNode` fields.** To change how strong gravity feels, scale the *strength* (`gravity_strength`, +0x0C) and leave the *direction* (`gravity_dir`, +0x10) a unit vector. Scaling the direction instead denormalizes the machine's derived up vector and breaks air control.

## Entry Points

| Function | Role |
|----------|------|
| `GravityChange_Start` | Pick a multiplier, save the original `gravity_strength`, apply. Called from `CustomEvent_State1Wrapper`. |
| `GravityChange_Active` | Re-apply the scaled strength each frame, in case something overwrites it. Called from `CustomEvent_State2Wrapper`. |
| `GravityChange_End2` | Restore the original `gravity_strength`. Called from `CustomEvent_State3Wrapper`. |
| `GetStageGravityStrength` (static) | Resolves `*stc_grobj -> gr_data -> stage_node -> &gravity_strength`, with NULL guards. |

No `check` callback is registered, so the event is always eligible; no per-frame `end` callback either.

## Registration

`custom_params[CUSTOM_EVKIND_GRAVITY_CHANGE - EVKIND_NUM]` in `custom_events.c`:

| Field | Value |
|-------|-------|
| `duration` | 900 frames (~15 s) |
| `is_siren` | 1 (siren SFX + music fade + sky transition) |
| `sky_preset` | 8 (Pink Sky) |
| `bgm_file` | 0x31 (`event_meteo`) |
| `weight` | 20 |
| `label` | `"Gravity Change"` |
| `hud_text` | `"Gravity is changing!"` |

## Implementation

Each trigger picks `GRAVITY_MULT_LOW` (0.5, floaty) or `GRAVITY_MULT_HIGH` (2.0, heavy) via `HSD_Randi(2)` in `GravityChange_Start`, holding the choice in a static for `Active`/`End2`. The original strength is captured at start, the scaled value re-applied every active frame, and the original written back on cleanup. `gravity_dir` is never touched.

- **Low gravity** (`< 1.0`): floaty machines, longer/higher jumps, drift, extended air time.
- **High gravity** (`> 1.0`): machines hug the ground, jumps cut short, snappier landings.

Possible extensions: rotate `gravity_dir` by a small angle *while keeping it unit-length* for diagonal gravity (machines build their up vector from it, so the world genuinely re-orients); or lerp `gravity_strength` over several frames at start/end instead of switching instantly.

## Gravity System

The single entry point for "which way is down, and how hard, at this position" is `Gm_GetDownVector` (0x800ceb18):

```c
// Writes the unit down direction into *out, returns the gravity strength scalar.
float Gm_GetDownVector(Vec3 *pos, Vec3 *out);
```

It consults the stage's gravity zones first, and if no zone applies at `pos` it falls back to the **global stage gravity**: it copies `StageNode.gravity_dir` into `*out` and returns `StageNode.gravity_strength`. Machines, items, enemies and the camera all source their down/up from this one function.

### Data Location

```c
// Access chain: stc_grobj -> gr_data (+0x8) -> stage_node (+0x4)
GrObj *grobj = *stc_grobj;
StageNode *node = grobj->gr_data->stage_node;
float  strength = node->gravity_strength;  // +0x0C - magnitude
Vec3  *dir      = &node->gravity_dir;      // +0x10 - unit down direction
```

### StageNode layout (relevant fields)

| Offset | Type | Field | City Trial value |
|--------|------|-------|------------------|
| +0x00 | int | x0 | |
| +0x04 | float | `machine_accel` (base machine acceleration scalar) | 1.21 |
| +0x08 | float | `scale` (stage model scale) | 0.70 |
| +0x0C | float | `gravity_strength` (fall-accel scalar - the real "how strong" knob) | 0.025 |
| +0x10 | Vec3 | `gravity_dir` (unit down direction) | (0, -1, 0) |
| +0x1C | int | `fog_flags` | |

`0.025` is also the hard-coded fallback `Gm_GetDownVector` returns when there is no `stage_node` (constant at 0x805df5e4).

### How gravity is applied

Every machine refreshes a cached copy of the gravity each frame in `Machine_RefreshGravity` (0x801c98c4, also run once at `Machine_Create`):

```c
// param = RiderData
float mag = Gm_GetDownVector(&rider->pos /*+0x3E8*/, &rider->down /*+0x768*/);
rider->grav_mag /*+0x764*/ = mag;            // cached strength
rider->up.X /*+0x774*/ = -rider->down.X;     // up vector = -down direction
rider->up.Y /*+0x778*/ = -rider->down.Y;
rider->up.Z /*+0x77C*/ = -rider->down.Z;
```

| RiderData offset | Meaning |
|------------------|---------|
| +0x764 | cached gravity strength (from `gravity_strength`) |
| +0x768 | cached down direction (from `gravity_dir`) |
| +0x774 | up direction = -down (used for orientation / air control) |

The downward pull is `down_direction * strength`, and the **up vector is the negated raw down direction**. That is exactly why the field you scale matters:

- Scaling `gravity_strength` (+0x0C) cleanly scales fall acceleration while the direction - and therefore the unit up vector - stays correct. Ground and air both behave like real low/high gravity.
- Scaling `gravity_dir` (+0x10) *also* changes the felt pull (consumers multiply by it), but it makes the vector non-unit, so the derived up vector (`-down`) is no longer unit length. Orientation and air-control matrices built from a short up vector go haywire.

### Gravity zones (localized overrides)

`Gm_GetDownVector` checks two kinds of localized gravity before falling back to the global vector (module `grgravity.c`):

- **Point zones** (`grGravity_GetPointZoneDown`, 0x800e6834): pull toward the nearest zone point. Zone positions live at `stc_grobj+0x13C` (stride 0x24), radii at `stage_node+0x70`. The resulting direction is normalized (`VEC_NormalizeAndSnap`).
- **Spline zones** (`grGravity_GetSplineZoneDown`, 0x800e69b0): pull toward the nearest point on a gravity spline (`gr_data+0x1C` spline data; `splArcLengthPoint`).

These produce "pull toward a center/curve" gravity for curved/orbital stages. `grGetGravityposNum` (0x800d0dcc) and `loadGravityLocations?` (0x800d0de4) are a debug count/loader pair called only from `debug_Race3D_loadLocations`, but the zone data they describe is consumed by `Gm_GetDownVector` for real. City Trial's stage has no zones, so it always falls through to the global (0,-1,0) x 0.025.

### Actor and item gravity

Enemies cache their down/up from `Gm_GetDownVector` too (the enemy-update cluster around 0x8022xxxx calls it), but they additionally carry a private fall scalar at `EnemyData+0x3A4` (default 0.02, from per-type actor data at `*actor_data+0x3C`) applied as `vel += accel` with GroundSnap for Y. Scaling stage `gravity_strength` changes the down/up they orient to; their private fall scalar is unaffected.

Items resolve their down direction through `Gm_GetDownVector` as well (`CityItem_GetDownVector?` 0x80254c50, `ItemColl_HandleLand`, `ItemColl_BounceLand`, `CityItem_Throw`, `shootPowerUps?`, `Box_SpawnContents?`). An item's `fall_dir` (Vec3 at `ItemData+0x1C8`) is the down direction used for ground raycasting, and throw elevation angles are built around it. So items follow the global gravity direction but use their own `gravity` scalar (`ItemData+0x44`) for fall speed.

## Symbols

| Symbol | Address | Size | Notes |
|--------|---------|------|-------|
| `Gm_GetDownVector` | 0x800ceb18 | 0x110 | Central gravity accessor: returns strength, writes unit down dir. In `link.ld`. |
| `grGravity_GetPointZoneDown` | 0x800e6834 | 0x17C | `grgravity.c` point-zone down-vector lookup |
| `grGravity_GetSplineZoneDown` | 0x800e69b0 | 0x21C | `grgravity.c` spline-zone down-vector lookup |
| `Machine_RefreshGravity` | 0x801c98c4 | 0x68 | Per-machine gravity cache refresh (calls `Gm_GetDownVector`, writes +0x764/+0x768/+0x774) |
| `Machine_PhysicsThink` | 0x801c6368 | 0x240 | Machine position/velocity accumulator |
| `grGetGravityposNum` | 0x800d0dcc | 0x18 | Gravity-zone count (debug accessor) |
| `loadGravityLocations?` | 0x800d0de4 | 0xD4 | Gravity-zone loader (debug accessor) |
| `grGetStageScale` | 0x800d3058 | 0x24 | Reads `stage_node->scale` (sibling field) |
