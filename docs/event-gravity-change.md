# Gravity Change Event

A custom City Trial event (`CUSTOM_EVKIND_GRAVITY_CHANGE`, kind 17) that scales the global stage gravity for its duration, affecting machine, item, enemy and camera physics alike. Implemented in `mods/custom_events/src/event_gravity_change.c`.

The load-bearing fact: **gravity is a strength scalar plus a direction vector, in two adjacent `StageNode` fields.** To change how strong gravity feels, scale the *strength* (`gravity_strength`, +0x0C) and leave the *direction* (`gravity_dir`, +0x10) a unit vector. Scaling the direction instead denormalizes the machine's derived up vector and breaks air control.

Like every custom event it is a row in `custom_params[]` and `custom_functions[]` in `custom_events.c`; the mod's wrappers on the event state table call the kind's callbacks in place of the vanilla per-kind dispatch. Its parameters: 900-frame duration (~15 s), siren intro, sky preset 8 (Pink Sky), BGM file 0x31 (`event_meteo`), roll weight 20. No `check` callback is registered, so the event is always eligible, and there is no per-frame `end`.

## Implementation

`GravityChange_Start` picks `GRAVITY_MULT_LOW` (0.5, floaty) or `GRAVITY_MULT_HIGH` (2.0, heavy) with `HSD_Randi(2)`, holds the choice in a static, captures the original `gravity_strength` and writes the scaled value. `GravityChange_Active` re-applies it every frame in case something overwrites the field; `GravityChange_End2` writes the original back. `gravity_dir` is never touched.

The field is reached through `GetStageGravityStrength`, which walks `*stc_grobj -> gr_data (+0x8) -> stage_node (+0x4)` and returns `&stage_node->gravity_strength`, NULL-guarded at every step - if the chain is not up (no stage loaded) the event no-ops rather than writing through a null pointer, and `gravity_modified` stays 0 so `Active`/`End2` skip too.

Low gravity gives floaty machines, longer and higher jumps, drift and extended air time; high gravity makes machines hug the ground with short jumps and snappy landings.

## Gravity System

The single entry point for "which way is down, and how hard, at this position" is `Gm_GetDownVector` (0x800ceb18): it writes the unit down direction into an out-param `Vec3` and returns the strength scalar. It consults the stage's gravity zones first, and if no zone applies at the queried position it falls back to the global stage gravity - copying `StageNode.gravity_dir` out and returning `StageNode.gravity_strength`. Machines, items, enemies and the camera all source their down/up from this one function, which is why one field write changes the whole world's physics.

City Trial's `StageNode` holds `gravity_strength` = 0.025 and `gravity_dir` = (0, -1, 0). Those same two values are the hard-coded fallback `Gm_GetDownVector` returns when there is no `stage_node` at all (float constants at 0x805df5dc / 0x805df5e4).

### How gravity reaches a machine

Every machine refreshes a cached copy each frame in `Machine_RefreshGravity` (0x801c98c4, also run once from `Machine_Create`). Given the `MachineData`, it calls `Gm_GetDownVector(&md->pos /*+0x3E8*/, &md->down /*+0x768*/)`, stores the returned strength at `MachineData+0x764`, and writes the component-wise negation of the down vector to `MachineData+0x774` as the machine's up vector. (An early-out on a flag bit at `MachineData+0xC3A` skips the refresh entirely for some machine states.)

So the downward pull is `down_direction * strength` and the **up vector is the negated raw down direction**, which is exactly why the choice of field matters:

- Scaling `gravity_strength` cleanly scales fall acceleration while the direction - and therefore the unit up vector - stays correct. Ground and air both behave like real low/high gravity.
- Scaling `gravity_dir` *also* changes the felt pull, since consumers multiply by it, but it leaves the vector non-unit, so the derived up (`-down`) is no longer unit length and the orientation and air-control matrices built from it go haywire.

`Machine_PhysicsThink` (0x801c6368) is what accumulates the resulting acceleration into velocity and position.

### Gravity zones (localized overrides)

`Gm_GetDownVector` checks two kinds of localized gravity before falling back to the global vector (module `grgravity.c`):

- **Point zones** (`grGravity_GetPointZoneDown`, 0x800e6834): pull toward the nearest zone point. Zone positions live at `stc_grobj+0x13C` (stride 0x24), radii at `stage_node+0x70`. The result is normalized with `VEC_NormalizeAndSnap`.
- **Spline zones** (`grGravity_GetSplineZoneDown`, 0x800e69b0): pull toward the nearest point on a gravity spline (`gr_data+0x1C` spline data, via `splArcLengthPoint`).

These produce "pull toward a center/curve" gravity for curved or orbital stages. `grGetGravityposNum` (0x800d0dcc) and `loadGravityLocations?` (0x800d0de4) are a debug count/loader pair called only from `debug_Race3D_loadLocations`, but the zone data they describe is consumed by `Gm_GetDownVector` for real. City Trial's stage defines no zones, so it always falls through to the global (0,-1,0) x 0.025 - and therefore to the field this event scales.

### Actor and item gravity

Enemies cache their down/up from `Gm_GetDownVector` too, but carry a private fall scalar at `EnemyData+0x3A4` (default 0.02, from per-type actor data at `*actor_data+0x3C`) applied as `vel += accel` with GroundSnap for Y. Scaling stage `gravity_strength` changes the down/up they orient to but not that private scalar, so enemies fall at an unchanged rate during the event.

Items resolve their down direction through `Gm_GetDownVector` as well (`CityItem_GetDownVector?` 0x80254c50, `ItemColl_HandleLand`, `ItemColl_BounceLand`, `CityItem_Throw`, `shootPowerUps?`, `Box_SpawnContents`). An item's `fall_dir` (Vec3 at `ItemData+0x1C8`) is the down direction used for ground raycasting, and throw elevation angles are built around it, but fall speed comes from the item's own `gravity` scalar at `ItemData+0x44` - so items follow the global gravity direction without following its strength.
