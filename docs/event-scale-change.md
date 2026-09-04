# Scale Change Event

A custom City Trial event (`CUSTOM_EVKIND_SCALE_CHANGE`, kind 18) announced as "The world is growing!". It makes the world feel bigger by **shrinking every player** - rider model, machine model, machine collision sphere - slowing them down and pulling each player's camera in to match, rather than by scaling the stage. A half-size player in an unchanged world reads exactly as a normal-size player in a doubled world, and world collision is never touched, so the floor stays solid. Implemented in `mods/custom_events/src/event_scale_change.c`.

Like every custom event it is a row in `custom_params[]` and `custom_functions[]` in `custom_events.c`; the mod's wrappers on the event state table call the kind's `start` / `active` / `end` / `end2` callbacks in place of the vanilla per-kind dispatch. Scale Change is the only registered custom event that uses the per-frame `end` callback.

Its parameters: 900-frame duration (~15 s), siren intro, sky preset 3 (Dusk 2), BGM file 0x32 (`event_monster`), roll weight 20.

## Why Not Scale the World

Collision is pre-baked, world-scale spatial data that nothing rescales at runtime. `Raycast_Do` (0x800d9958) reads a triangle array at `(*stc_grobj)+0x5c` (stride `0x40`) and a BVH whose nodes carry baked world-space AABBs; machine **sphere** collision (`mpColl_UpdateCollision`, 0x802485e0) reads the same triangle data directly. The stage `scale` field (`StageNode+0x08`, read by `grGetStageScale` 0x800d3058) only feeds visual JObj setup in `3D_CreateStageModel` (0x800dcbf0) at load, so writing it at runtime moves neither the mesh nor the BVH and machines fall through the floor. Truly scaling collision would mean rewriting every triangle vertex, every BVH AABB and their plane constants, then reversing it exactly on event end. Hooking the ground raycast (`Raycast_Ground` 0x800d1ac4) to transform ray coordinates does not help either - the machine sphere path does not route through it.

Shrinking the player turns that into scaling a handful of per-object scalars, and leaves collision correctness to the untouched world.

## What Gets Scaled

Per active player slot (City Trial, up to 4), every frame, by an eased `factor` (1.0 = normal, shrinking toward `SCALE_TARGET_FACTOR` = 0.5):

| Lever | Field | Notes |
|-------|-------|-------|
| Rider model | `RiderData.model_scale` (+0x348) | Baked into the rider model matrix every frame by `Rider_ApplyModelMatrix` (0x80190848); writing the field is the whole mechanism, the same one Big/Small Kirby uses. Visual only. |
| Machine model | `MachineData.model_scale` (+0x310) | `Machine_ApplyModelMatrix` (0x801c9074) and its articulated-model siblings bake `model_scale x model_scale_base` (+0x468) into the model's user matrix every frame. Visual only; rests at 1.0, since per-vehicle size lives in `model_scale_base`. |
| Machine collision | `MachineData.coll_data` (+0x6F8) -> `radius` (+0x344) and `shape_data->radius`/`radius2` (+0x30/+0x34) | The effective sphere radius the engine reads. |
| World speed | the per-frame delta of `MachineData.pos` (+0x3E8) | Each frame, pull the position back by `(1 - factor)` of the distance it moved since the last pass, so only `factor` of the displacement survives. |
| Camera | the per-player camera's eye->interest distance | A shim on the `bl CObj_SetEyePosition` inside `PlyCam_Think` (call site 0x800b3900) moves the final eye toward the interest along their line by `factor`. |

**The model and collision levers must move in lockstep.** The collision system holds the sphere center one `radius` above the contacting triangle and the model is drawn around that center, so shrinking the radius lowers the machine's ground-rest height for free. Shrink only the model and it floats inside a full-size sphere; shrink only the sphere and the full-size model clips into the ground.

### Why the displacement, not a speed stat

Two stats look like the speed lever and are not:

- **`StageNode.machine_accel` (+0x04)** (`grGetMachineAccel` 0x800cea80). Cruise speed is an equilibrium where thrust balances quadratic drag; `machine_accel` scales both terms, so it cancels out - it changes how fast a machine *reaches* cruise, not the cruise speed.
- **`MachineData.top_speed_current` (+0x398)** (re-derived each `Machine_AdjustAttributes` 0x801c7278). A real per-state cap, but the over-speed drag term is gated behind another stat, so only some vehicles' controllers consult it and writing it slows nothing on many machines.

Scaling the resulting displacement is downstream of every force: the engine integrates velocity and resolves collision normally, and the clamp then keeps only `factor` of the frame's actual movement. Because it is applied after collision resolution the machine never ends a frame penetrating, and the rider (`RiderData+0x300`) and camera read the machine position downstream and follow with no desync. The clamp is timing-agnostic - run once per frame at any fixed point, the displayed movement converges to `factor` x the engine's. Velocity (+0x324) is deliberately left untouched so turning/handling and the charge-by-speed read still see the true value.

A single-frame jump larger than `SCALE_TELEPORT_SPEED_MULT` x `top_speed_current` is treated as a teleport and passes through unscaled, so respawns and warps land on target instead of being clamped halfway there.

### Why move the camera, not change FOV

Uniformly scaling a whole scene - geometry **and** the camera rig - produces a pixel-identical image. So the exact way to fake "the world grew" from a shrunk player is to move the camera the same `factor` of the way to its target: the player renders at its normal on-screen size while the unchanged world subtends `1/factor` x the angle and reads as bigger. Using the same number for both is what sells it. FOV is the wrong lever - a wider FOV magnifies but also warps perspective, whereas a dolly preserves it exactly.

The eye-set call inside `PlyCam_Think` (0x800b3540) is replaced rather than poking the COBJ from the event loop, because the camera is recomputed from scratch every frame *after* most game logic (an external write would be overwritten) and `PlyCam_Think`'s own input (`CamData.x14`) is recomputed inside the same function just before it is consumed, so there is nothing to pre-seed. Intercepting the final `CObj_SetEyePosition` (0x804018ac, map name `HSD_CObjSetEyePosition`) is downstream of the entire camera pipeline - kind dispatch, C-stick `zoom_amt`, rail/normal transitions - so it works regardless of how the eye was produced. The interest was written to the same COBJ by the `bl CObj_SetInterest` one instruction earlier (0x800b38f4), so the shim reads the dolly target straight back off the COBJ with no capture and no lag.

## Implementation

### Capture and restore

Only the **collision** originals are captured, per player slot, on first touch of a machine (the `SlotScale` array): the machine GObj plus its `CollData.radius` and the two `CollShapeData` radii. Those are absolute sizes that vary by vehicle, so `factor` 1.0 writes the captured originals straight back.

Nothing else needs capture. Both `model_scale` fields are "1.0 = normal" multipliers, so the levers write `factor` and ease back to 1.0. The speed lever only reads and writes the live position, tracking `last_pos` per slot (re-seeded, with no clamp, on the first frame of a new machine). The camera lever is stateless and is a pure passthrough whenever the event is idle (`scale_active == 0` or `factor == 1.0`).

When a slot's machine GObj changes (a different machine, or a respawn) the originals are re-captured; when a slot empties (rider on foot) its capture is dropped so a later mount re-captures fresh.

### Easing

`factor` eases between 1.0 and the target by `SCALE_EASE_STEP` (0.02/frame, ~25 frames over the 0.5 swing) rather than snapping - gentler on the eye and on the collision sphere, where a sudden radius change risks a ground snap or penetration. The callbacks drive it: `ScaleChange_Start` resets the per-slot captures and sets `factor = 1.0`; `ScaleChange_Active` eases toward the target and applies each frame; `ScaleChange_End` eases back toward 1.0 each frame of the cleanup phase; `ScaleChange_End2` applies `factor` 1.0 once for an exact restore and clears state.

The camera lever is the exception to the callback model: its `CObj_SetEyePosition` shim is installed once at boot by `ScaleChange_InstallHooks` (called from `CustomEvents_OnBoot`) and reads the live `scale_active` / `cur_factor` statics, so it follows the same ease for free and self-disables when the event ends.

### Tuning knobs

All `#define`s at the top of `event_scale_change.c`:

| Macro | Value | Meaning |
|-------|-------|---------|
| `SCALE_TARGET_FACTOR` | 0.5 | How small players shrink - and, deliberately the same number, the world-speed and camera-distance factor |
| `SCALE_EASE_STEP` | 0.02 | Ease speed per frame |
| `SCALE_MAX_PLAYERS` | 4 | City Trial player slots |
| `SCALE_TELEPORT_SPEED_MULT` | 5.0 | Per-frame jump above `this x top_speed_current` is passed through unscaled |
| `SCALE_AFFECTS_RIDER_MODEL` / `_MACHINE_MODEL` / `_COLLISION` / `_SPEED` / `_CAMERA` | 1 | Each lever independently toggleable so one can be isolated when tuning in-game. `_CAMERA` gates only the scaling math inside the shim; the boot-time call replacement is unconditional but a pure passthrough. |

`archipelago_debug` fires the event on **D-Pad Up** (no L modifier) in City Trial via `ce_api->Do(CUSTOM_EVKIND_SCALE_CHANGE)`, so a test build needs it in `INCLUDE_MODS` alongside `custom_events`.

## Known Limitations

- **On-foot riders are not collision-shrunk.** Only the machine collision sphere is scaled, so a player who dismounts mid-event keeps a full-size on-foot sphere (`RiderData`-side mpColl) and the shrunk model floats slightly. Rare in City Trial, where riders are almost always mounted.
- **The camera dolly affects every player view.** `PlyCam_Think` drives every player camera including all split-screen views, so while the event is active every view dollies in - correct for a world-wide event. Outside the event the shim is a verbatim passthrough. It only adjusts the final eye, not the C-stick `zoom_amt` (`CamData+0x8c`), so a player's manual zoom still applies on top.
- **All players shrink, including CPUs.** Loose, un-ridden city machines stay full size - they read as big world props, and shrink when a player mounts them.
- **The speed lever scales displacement, not velocity.** A hard knockback exceeding `5 x top_speed_current` in one frame reads as a teleport and that frame is not slowed; a respawn slides to its target over a few frames instead of snapping; and anything reading raw velocity sees full speed.
- **`RiderData.model_scale` is shared with Big/Small Kirby.** If the archipelago mod's `kirby_scale` is also driving `model_scale`, this event overwrites it for its duration and restores 1.0 on end, cancelling an active Big/Small Kirby.

## Symbols

| Symbol | Address | Notes |
|--------|---------|-------|
| `Rider_ApplyModelMatrix` | 0x80190848 | Bakes `base x model_scale` into the rider model matrix each frame |
| `Machine_ApplyModelMatrix` | 0x801c9074 | Machine analogue: bakes `model_scale x model_scale_base` into the machine model's user matrix (articulated siblings at 0x801c9308/9464/9694 do the same per sub-joint) |
| `gmLanMenu_Scale3DObject` | 0x80054414 | Builds an SRT matrix from a scale + 3 vectors and bakes it into a JObj's user matrix (`JObj+0x44`); shared by the rider, machine, item and actor appliers |
| `Machine_PhysicsThink` | 0x801c6368 | Integrates `MachineData.pos` from velocity plus impulse vectors; the displacement the speed lever scales |
| `Machine_AdjustAttributes` | 0x801c7278 | Re-derives `top_speed_current` from `top_speed_ground` (+0x4f0) / airborne (+0x5ac); its value sizes the teleport threshold |
| `mpColl_GetSphereRadius` | 0x802415a8 | Returns the effective sphere radius, lerping `CollShapeData+0x30` and `+0x34` |
| `Machine_InitialCollisionCheck?` | 0x801cc7a4 | Spawn-time mpColl setup; radius from `MachineData.coll_radius_base` (+0x46C), CollData stored at +0x6F8 |
| `Machine_ProcessEnvColl` | 0x801e5108 | Per-frame machine env collision; queries the CollData at `MachineData+0x6F8` |
| `PlyCam_Think` | 0x800b3540 | Per-frame player-camera update; the `bl CObj_SetEyePosition` at 0x800b3900 is the camera lever's hook site (`bl CObj_SetInterest` at 0x800b38f4) |
| `CObj_SetEyePosition` | 0x804018ac | Writes a Vec3 into the COBJ's eye WObj (`COBJ+0x24` -> `WObj+0xC`); the call the shim replaces |
| `CObj_SetInterest` | 0x804017d4 | Writes a Vec3 into the COBJ's interest WObj (`COBJ+0x28`); the value the shim reads back as the dolly target |
