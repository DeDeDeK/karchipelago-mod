# Scale Change Event

A custom City Trial event (`CUSTOM_EVKIND_SCALE_CHANGE`, kind 18) announced as "The world is growing!". Implemented in `mods/custom_events/src/event_scale_change.c`.

It makes the world feel bigger by **shrinking every player** rather than by scaling the stage:
- the rider model, machine model and machine collision sphere all shrink;
- players slow down;
- each player's camera pulls in to match.

A half-size player in an unchanged world reads exactly like a normal-size player in a doubled world. World collision is never touched, so the floor stays solid.

Like every custom event, it is a row in `events[]` in `custom_events.c`. The mod's wrappers on the event state table call the kind's `start` / `active` / `end` / `end2` / `abort` callbacks in place of the vanilla per-kind dispatch. Scale Change is the only registered custom event that uses the per-frame `end` callback.

Parameters: 900-frame duration (~15 s), siren intro, sky preset 3 (Dusk 2), BGM file 0x32 (`event_monster`), roll weight 20.

## Why Not Scale the World

Collision is pre-baked, world-scale spatial data that nothing rescales at runtime:
- `Raycast_Do` (0x800d9958) reads a triangle array at `(*stc_grobj)+0x5c` (stride `0x40`) and a BVH whose nodes carry baked world-space AABBs.
- Machine **sphere** collision (`mpColl_UpdateCollision`, 0x802485e0) reads the same triangle data directly.
- The stage `scale` field (`StageNode+0x08`, read by `grGetStageScale` 0x800d3058) only feeds visual JObj setup in `3D_CreateStageModel` (0x800dcbf0) at load. Writing it at runtime moves neither the mesh nor the BVH, and machines fall through the floor.

Truly scaling collision would mean rewriting every triangle vertex, every BVH AABB and their plane constants, then reversing it exactly when the event ends. Hooking the ground raycast (`Raycast_Ground` 0x800d1ac4) to transform ray coordinates does not help either, because the machine sphere path does not go through it.

Shrinking the player reduces all of that to scaling a handful of per-object scalars, and leaves collision correctness to the untouched world.

## What Gets Scaled

Every frame, each active player slot (City Trial, up to 4) is driven by an eased `factor`: 1.0 is normal, and it shrinks toward `SCALE_TARGET_FACTOR` = 0.5.

| Lever | Field | Notes |
|-------|-------|-------|
| Rider model | `RiderData.model_scale` (+0x348) | Baked into the rider model matrix every frame by `Rider_ApplyModelMatrix` (0x80190848). Writing the field is the whole mechanism, the same one Big/Small Kirby uses. Visual only. |
| Machine model | `MachineData.model_scale` (+0x310), written as `model_scale_default` (+0x30C) x factor | `Machine_ApplyModelMatrix` (0x801c9074) and its articulated siblings bake `model_scale x model_scale_base` (+0x468) into the model's user matrix every frame. `Machine_StoreVcDataPtr` (0x801c4f98) seeds both +0x30C and +0x310 from the spawn descriptor, and hit-reaction exits restore `model_scale` from +0x30C. Visual only. |
| Machine collision | `MachineData.coll_data` (+0x6F8) -> `shape_data->radius` / `radius2` (+0x30 / +0x34) | The sphere radii `mpColl_GetSphereRadius` reads; see below for what the engine rewrites. |
| World speed | Per-frame delta of `MachineData.pos` (+0x3E8) | Each frame, the position is pulled back by `(1 - factor)` of the distance moved since the last pass, so only `factor` of the displacement survives. |
| Camera | Each player camera's eye->interest distance | A shim on the `bl CObj_SetEyePosition` inside `PlyCam_Think` (call site 0x800b3900) moves the final eye toward the interest along their line by `factor`. |

**The model and collision levers must move together.** The collision system holds the sphere center one radius above the contacting triangle, and the model is drawn around that center, so shrinking the radius lowers the machine's ground-rest height for free. Shrink only the model and it floats inside a full-size sphere; shrink only the sphere and the full-size model clips into the ground.

### What holds on the collision sphere

Every frame, every vehicle state's env-coll callback calls `mpColl_Update` (0x80245f70) at proc priority 5, after the event proc at priority 0. It passes two radii:
- `coll_radius_base` (+0x46C), which is stored into `CollData.radius` (+0x344);
- a second radius, which is stored into `CollShapeData.radius2` unless it is -1. Ground states pass the vehicle's own radius; fly, jump and rail states pass -1.

`CollShapeData.radius` is written only by `mpColl_Init`, at spawn and respawn.

So the event scales only the two shape radii, and of those only `radius` and airborne `radius2` keep its value. `mpColl_GetSphereRadius` (0x802415a8) returns `radius` when the two match and otherwise lerps between them. In the air the sphere is fully scaled; on the ground it sits between the scaled `radius` and the full `radius2`.

### Why the displacement, not a speed stat

Two stats look like the speed lever, but neither works:

- **`StageNode.machine_accel` (+0x04)** (`grGetMachineAccel` 0x800cea80): cruise speed is an equilibrium where thrust balances quadratic drag. `machine_accel` scales both terms, so it cancels out; it changes how fast a machine *reaches* cruise, not the cruise speed.
- **`MachineData.top_speed_current` (+0x398)** (re-derived each `Machine_AdjustAttributes` 0x801c7278): a real per-state cap, but the over-speed drag term is gated behind another stat. Only some vehicles' controllers consult it, so writing it slows nothing on many machines.

Scaling the resulting displacement works because it is downstream of every force:
- **Timing.** The event proc runs at priority 0, before `Machine_PhysicsThink` (priority 4) and `Machine_EnvCollThink` (priority 5). The clamp therefore scales the displacement the previous frame produced, and the engine integrates and resolves collision from the pulled-back position.
- **No desync.** The rider (`RiderData+0x300`) and camera read the machine position downstream and follow.
- **Timing-agnostic.** Run once per frame at any fixed point, the displayed movement converges to `factor` x the engine's.
- **Velocity (+0x324) is left untouched,** so turning, handling and the charge-by-speed read still see the true value.

A single-frame jump larger than `SCALE_TELEPORT_SPEED_MULT` x `top_speed_current` is treated as a teleport and passes through unscaled, so respawns and warps land on target instead of being clamped halfway there.

### Why move the camera, not change FOV

Uniformly scaling a whole scene, geometry **and** camera rig, produces a pixel-identical image. So the exact way to fake "the world grew" from a shrunk player is to move the camera the same `factor` of the way to its target. The player then renders at normal on-screen size, while the unchanged world subtends `1/factor` x the angle and reads as bigger. Using the same number for both is what sells it.

FOV is the wrong lever: a wider FOV magnifies but also warps perspective, whereas a dolly preserves it exactly.

The shim replaces the eye-set call inside `PlyCam_Think` (0x800b3540) instead of poking the COBJ from the event loop, for two reasons:
- The camera is recomputed from scratch every frame *after* most game logic, so an external write would be overwritten.
- `PlyCam_Think`'s own input (`CamData.x14`) is recomputed inside the same function just before it is consumed, so there is nothing to pre-seed.

Intercepting the final `CObj_SetEyePosition` (0x804018ac, map name `HSD_CObjSetEyePosition`) puts the shim downstream of the entire camera pipeline: kind dispatch, C-stick `zoom_amt`, rail/normal transitions. It works regardless of how the eye was produced. The interest was written to the same COBJ by the `bl CObj_SetInterest` one instruction earlier (0x800b38f4), so the shim reads the dolly target straight back off the COBJ, with no capture and no lag.

## Implementation

### Capture and restore

Per player slot, `SlotScale` holds:
- the machine GObj being shrunk;
- its unscaled `CollShapeData.radius` and `radius2`, captured when the machine first appears in that slot. These are absolute sizes that vary by vehicle.
- `last_pos` for the speed lever.

The model lever needs no capture, since it scales `model_scale_default`.

**Restoring a machine the player leaves.** When a slot's machine changes (the player dismounts, swaps machines or loses one), `RestoreMachine` returns the previous machine to full size on the spot: default model scale plus the captured radii. It only does this if the machine is still on the `GAMEPLINK_MACHINE` list. This has two effects:
- a machine left behind never stays shrunk;
- a later mount captures that machine's true originals.

On the first frame of a new machine the speed lever re-seeds `last_pos` without clamping.

The camera lever is stateless: the shim is a verbatim passthrough whenever `factor` is 1.0.

### Easing and lifecycle

`factor` eases between 1.0 and the target by `SCALE_EASE_STEP` (0.02/frame, ~25 frames over the 0.5 swing) instead of snapping. That is gentler on the eye and on the collision sphere, where a sudden radius change risks a ground snap or penetration.

| Callback | Does |
|----------|------|
| `ScaleChange_Start` | Sets `factor = 1.0` |
| `ScaleChange_Active` | Eases toward the target and applies it each frame |
| `ScaleChange_End` | Eases back toward 1.0 each frame of the cleanup phase |
| `ScaleChange_End2` | Sets every rider's scale back to 1.0 and restores every slot's machine |
| `ScaleChange_Abort` | Resets `factor` to 1.0 and drops the slots, without touching the dying objects |

The camera shim is installed once at boot by `ScaleChange_InstallHooks` (called from `CustomEvents_OnBoot`) and reads the live `factor`, so it follows the same ease for free.

The abort matters because the shim outlives the round. `PlyCam_Think` runs in every minor-18 3D scene: Air Ride, City Trial, stadiums and the attract demo. Without the abort, a round that ended mid-event would keep the dolly in the following stadium and beyond.

### Tuning knobs

All `#define`s at the top of `event_scale_change.c`:

| Macro | Value | Meaning |
|-------|-------|---------|
| `SCALE_TARGET_FACTOR` | 0.5 | How small players shrink - and, deliberately the same number, the world-speed and camera-distance factor |
| `SCALE_EASE_STEP` | 0.02 | Ease speed per frame |
| `SCALE_MAX_PLAYERS` | 4 | City Trial player slots |
| `SCALE_TELEPORT_SPEED_MULT` | 5.0 | Per-frame jump above `this x top_speed_current` is passed through unscaled |

`archipelago_debug` fires the event on **D-Pad Up** (no L modifier) during a City Trial round via `ce_api->Do(CUSTOM_EVKIND_SCALE_CHANGE)`, so a test build needs it in `INCLUDE_MODS` alongside `custom_events`. The binding only reads while a round is live, because the settings menu navigates on the same D-Pad.

## Known Limitations

- **The ground-state sphere is only partly shrunk.** `mpColl_Update` rewrites `radius2` to full size every grounded frame, so the effective radius on the ground is a lerp between the scaled and full radii (see above).
- **On-foot riders are not collision-shrunk.** Only the machine collision sphere is scaled. A player who dismounts mid-event keeps a full-size on-foot sphere (`RiderData`-side mpColl), and the shrunk model floats slightly. This is rare in City Trial, where riders are almost always mounted.
- **The camera dolly affects every player view.** `PlyCam_Think` drives every player camera, including all split-screen views, so while the event is active every view dollies in. That is correct for a world-wide event. The shim only adjusts the final eye, not the C-stick `zoom_amt` (`CamData+0x8c`), so a player's manual zoom still applies on top.
- **All players shrink, including CPUs.** A machine shrinks only while someone rides it; a loose city machine stays full size, and a machine a player leaves returns to full size at once.
- **The speed lever scales displacement, not velocity.** A hard knockback exceeding `5 x top_speed_current` in one frame reads as a teleport, and that frame is not slowed. A respawn slides to its target over a few frames instead of snapping. Anything reading raw velocity sees full speed.
- **`RiderData.model_scale` is shared with Big/Small Kirby.** If the archipelago mod's `kirby_scale` is also driving `model_scale`, this event overwrites it for its duration and restores 1.0 at the end, cancelling an active Big/Small Kirby.

## Symbols

| Symbol | Address | Notes |
|--------|---------|-------|
| `Rider_ApplyModelMatrix` | 0x80190848 | Bakes `base x model_scale` into the rider model matrix each frame |
| `Machine_ApplyModelMatrix` | 0x801c9074 | Machine analogue: bakes `model_scale x model_scale_base` into the machine model's user matrix (articulated siblings at 0x801c9308/9464/9694 do the same per sub-joint) |
| `Machine_StoreVcDataPtr` | 0x801c4f98 | Seeds `model_scale` and `model_scale_default` from the spawn descriptor |
| `gmLanMenu_Scale3DObject` | 0x80054414 | Builds an SRT matrix from a scale + 3 vectors and bakes it into a JObj's user matrix (`JObj+0x44`); shared by the rider, machine, item and actor appliers |
| `Machine_PhysicsThink` | 0x801c6368 | Integrates `MachineData.pos` from velocity plus impulse vectors; the displacement the speed lever scales |
| `Machine_AdjustAttributes` | 0x801c7278 | Re-derives `top_speed_current` from `top_speed_ground` (+0x4f0) / airborne (+0x5ac); its value sizes the teleport threshold |
| `mpColl_Update` | 0x80245f70 | Per-frame sphere update; stores `f1` into `CollData.radius` and `f2` into `CollShapeData.radius2` unless -1 |
| `mpColl_GetSphereRadius` | 0x802415a8 | Returns `radius` when it equals `radius2`, else a lerp between them |
| `Machine_ProcessEnvColl` | 0x801e5108 | Per-frame machine env collision; queries the CollData at `MachineData+0x6F8` |
| `PlyCam_Think` | 0x800b3540 | Per-frame player-camera update; the `bl CObj_SetEyePosition` at 0x800b3900 is the camera lever's hook site (`bl CObj_SetInterest` at 0x800b38f4) |
| `CObj_SetEyePosition` | 0x804018ac | Writes a Vec3 into the COBJ's eye WObj (`COBJ+0x24` -> `WObj+0xC`); the call the shim replaces |
| `CObj_SetInterest` | 0x804017d4 | Writes a Vec3 into the COBJ's interest WObj (`COBJ+0x28`); the value the shim reads back as the dolly target |
