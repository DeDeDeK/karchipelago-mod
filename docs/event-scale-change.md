# Scale Change Event

A custom City Trial event (`CUSTOM_EVKIND_SCALE_CHANGE`, kind 18) announced as "The world is growing!". It makes the world feel bigger by **shrinking every player** - rider model, machine model, machine collision sphere - slowing them down and pulling each player's camera in to match, rather than by scaling the stage. A half-size player in an unchanged world reads exactly as a normal-size player in a doubled world, and world collision is never touched, so the floor stays solid. Implemented in `mods/custom_events/src/event_scale_change.c`.

## Registration

`custom_params[CUSTOM_EVKIND_SCALE_CHANGE - EVKIND_NUM]` in `custom_events.c`:

| Field | Value |
|-------|-------|
| `duration` | 900 frames (~15 s) |
| `is_siren` | 1 |
| `sky_preset` | 3 (Dusk 2) |
| `bgm_file` | 0x32 (`event_monster`) |
| `weight` | 20 |
| `label` | `"Scale Change"` |
| `hud_text` | `"The world is growing!"` |

Handlers: `ScaleChange_Start` / `_Active` / `_End` / `_End2`. It is the only registered custom event with a per-frame `end` callback.

## Why Not Scale the World

Growing the stage does not work, because **collision is pre-baked, world-scale spatial data that nothing rescales at runtime**:

- `Raycast_Do` (0x800d9958) reads a triangle array at `(*stc_grobj)+0x5c` (stride `0x40`) and a BVH/octree at `(*stc_grobj)+0x704` whose nodes carry baked world-space AABBs (center at `node+0x14..+0x1c`, extents at `node+0x20..+0x28`). Machine **sphere** collision (`mpColl_UpdateCollision`) reads the same triangle data directly.
- The stage `scale` field (`StageNode+0x08`, read by `grGetStageScale` 0x800d3058) only feeds **visual** JObj setup in `3D_CreateStageModel` at load. Writing it at runtime moves neither the triangle mesh nor the BVH.

So scaling the stage visually leaves the collision floor at its original size and machines fall through it. Genuinely scaling collision would mean rewriting every triangle vertex **and** every BVH AABB **and** their plane constants, then reversing it perfectly on event end - large and fragile. Hooking `EnvColl_Raycast` to transform ray coordinates would not help either: the machine sphere path does not route through it.

Shrinking the player turns "rescale a baked mesh + BVH" into "scale a handful of per-object scalars", and leaves collision correctness entirely to the untouched world.

## What Gets Scaled

Per active player slot (City Trial, up to 4), every frame, by an eased `factor` (1.0 = normal, shrinking toward `SCALE_TARGET_FACTOR` = 0.5):

| Lever | Field | Notes |
|-------|-------|-------|
| Rider model | `RiderData.model_scale` (+0x348) | The engine bakes it into the rider model matrix every frame (`Rider_ApplyModelMatrix` 0x80190848). Writing the field is the whole mechanism - the same one Big/Small Kirby uses. Visual only. |
| Machine model | `MachineData.model_scale` (+0x310) | The machine's exact analogue. The machine model appliers (`Machine_ApplyModelMatrix` 0x801c9074 and its articulated-model siblings) bake `model_scale x model_scale_base` (+0x468) into the model's *user* matrix every frame via the same `gmLanMenu_Scale3DObject` the rider uses. Visual only; rests at 1.0 (per-vehicle size lives in `model_scale_base`). |
| Machine collision | `MachineData.coll_data` (+0x6F8) -> `radius` (+0x344) and `shape_data->radius`/`radius2` (+0x30/+0x34) | The effective sphere radius the engine reads. Shrinking it also **lowers the machine's ground-rest height** automatically: the collision system holds the sphere center a `radius` above the contacting floor, so a smaller sphere sits the (smaller) model correctly on unchanged terrain with no height fix-up. |
| World speed | `MachineData.pos` (+0x3E8) - its per-frame delta | Keep only `factor` of the machine's per-frame world displacement: each frame, pull the position back by `(1 - factor)` of the distance it moved since the last pass. The engine integrates velocity and resolves collision exactly as normal around this, so the machine never ends a frame penetrating, and the rider (`RiderData+0x300`) + camera read the machine position downstream and follow with no desync. A teleport (per-frame jump > `SCALE_TELEPORT_SPEED_MULT` x the machine's `top_speed_current`) passes through unscaled so respawns/warps land on target. |
| Camera | the per-player camera's eye->interest distance, written each frame by `PlyCam_Think` | The `bl CObj_SetEyePosition` inside `PlyCam_Think` (call site `0x800b3900`) is replaced with a shim that moves the final eye toward the interest along their line by `factor` (`eye' = interest + (eye - interest) * factor`), halving the follow distance at 0.5. The interest was set on the same COBJ the instruction before (`bl CObj_SetInterest` @ `0x800b38f4`), so the shim reads it straight back off the COBJ - no lag, no capture. View direction, up and FOV are untouched; only distance changes. |

### Why the rest height follows the radius

The machine's collision sphere is what rests on the floor; its center sits one `radius` above the contacting triangle, and the visual model is drawn around that center. If only the model shrank, the full-size sphere would hold the small model floating half a machine-height up; if only the sphere shrank, the full-size model would clip into the ground. Shrinking **both together** keeps the model resting correctly on the floor - which is why the machine-model and machine-collision levers must move in lockstep.

### Why the displacement, not a speed stat

Two stats look like the obvious speed lever and are not:

- **`StageNode.machine_accel` (+0x04)** (read by `grGetMachineAccel` 0x800cea80). Cruise speed is an equilibrium where forward thrust balances quadratic drag (`drag` proportional to `v^2 * drag_stat * machine_accel`, thrust proportional to `throttle * accel_stat * machine_accel`). `machine_accel` appears in both terms, so it **cancels** out of the equilibrium: scaling it changes how fast a machine *reaches* cruise, not the cruise speed. A half-size player at full cruise speed reads as moving twice as fast.
- **`MachineData.top_speed_current` (+0x398)** (re-derived each `Machine_AdjustAttributes` 0x801c7278). This is a real per-state speed cap, but only some vehicles' controllers consult it - the over-speed drag term is gated behind another stat - so writing it slows nothing on many machines.

Both try to influence an emergent quantity through one of its many inputs. Scaling the **resulting displacement** sidesteps the entire stat/controller web: let the engine produce whatever velocity it wants and resolve collision normally, then keep only `factor` of the frame's actual movement. It is downstream of every force, so it works regardless of how the speed was produced. (`top_speed_current` is still used - its value sizes the teleport threshold.)

The clamp is timing-agnostic: run once per frame at any fixed point, the *displayed* per-frame movement converges to `factor` x the engine's, because each frame the position is re-pulled back by `(1 - factor)` of the last full delta. Velocity (+0x324) is left untouched, so the engine's turning/handling and the charge-by-speed read still see the true velocity - only the world translation is reduced.

### Why move the camera, not change FOV

Uniformly scaling the whole scene up by `1/factor` - geometry **and the camera rig** - produces a pixel-identical image; it is just a change of units. So the exact way to fake "the world grew" with a shrunk player is to also move the camera `factor` of the way to its target: a player at `factor` size viewed from `factor` distance renders at its *normal* on-screen size, while the unchanged world around it now subtends `1/factor` x the angle and reads as bigger. Using the **same factor** for player scale and camera distance is what makes the player look untouched while the world appears to balloon.

The lever is a true **dolly** (move the eye in along the eye->interest line), not a zoom. The eye-set call inside `PlyCam_Think` is replaced rather than the COBJ written from the event loop, because the camera is recomputed from scratch every frame *after* most game logic - an external poke would be overwritten - and `PlyCam_Think`'s own input (`CamData.x14`) is recomputed inside the same function just before it is consumed, so there is no field to pre-seed either. Intercepting the final `CObj_SetEyePosition` is downstream of the entire camera pipeline (kind dispatch, C-stick `zoom_amt`, rail/normal transitions), so it works regardless of how the eye was produced.

**FOV** was rejected as the lever (it is what the C-stick zoom does via `zoom_amt`, max 8.4): a wider FOV magnifies but also warps perspective (foreshortening changes, a fish-eye look), which is not what scaling the world does. A dolly preserves perspective exactly.

## Implementation

### Capture and restore

Only the **collision** originals are captured, **per player slot, on first touch of a machine** (`SlotScale` array): the machine GObj plus its `CollData.radius` and the two `CollShapeData` radii. Those are absolute sizes that vary by vehicle, so capture is assumption-free and exact - `factor` 1.0 writes the captured originals straight back.

The model-scale levers need no capture: both `model_scale` fields are "1.0 = normal" multipliers, so the levers write `factor` directly and ease back to 1.0. The speed lever needs no capture either - it only reads/writes the live position and tracks `last_pos` per slot (re-seeded, no clamp, on the first frame of a new machine). The camera lever is fully stateless: the shim reads each COBJ's just-set interest, moves the live eye, and is a pure passthrough whenever the event is idle (`scale_active == 0` or `factor == 1.0`), so it never needs to restore anything.

When a slot's machine GObj changes (the player grabs a different machine, or respawns into a new one) the originals are re-captured; when a slot empties (the player is on foot) its capture is dropped so a later mount re-captures fresh.

### Easing

`factor` eases between 1.0 and the target by `SCALE_EASE_STEP` (0.02/frame, ~25 frames over the 0.5 swing) rather than snapping - gentler on the eye and on the collision sphere, where a sudden radius change risks a ground snap or penetration. The four callbacks drive it:

- `ScaleChange_Start` (state 1->2): reset per-slot captures, set `factor = 1.0`.
- `ScaleChange_Active` (state 2, each frame): ease toward the target, apply.
- `ScaleChange_End` (state 3, each frame): ease back toward 1.0, apply.
- `ScaleChange_End2` (once, cleanup end): apply `factor` 1.0 for an exact restore (collision originals written back, speed clamp becomes a no-op), clear state.

The camera lever is the exception to the callback model: its `CObj_SetEyePosition` shim is installed **once at boot** by `ScaleChange_InstallHooks` (called from `CustomEvents_OnBoot`) and reads the live `scale_active` / `cur_factor` statics, so it follows the same ease for free and self-disables when the event ends - no per-callback camera code, no teardown.

### Tuning knobs

All `#define`s at the top of `event_scale_change.c`:

| Macro | Value | Meaning |
|-------|-------|---------|
| `SCALE_TARGET_FACTOR` | 0.5 | How small players shrink - and, deliberately the same number, the world-speed and camera-distance factor |
| `SCALE_EASE_STEP` | 0.02 | Ease speed per frame |
| `SCALE_MAX_PLAYERS` | 4 | City Trial player slots |
| `SCALE_TELEPORT_SPEED_MULT` | 5.0 | Per-frame jump above `this x top_speed_current` is treated as a teleport and passed through unscaled |
| `SCALE_AFFECTS_RIDER_MODEL` / `_MACHINE_MODEL` / `_COLLISION` / `_SPEED` / `_CAMERA` | 1 | Each lever independently toggleable so one can be isolated when tuning in-game. `_CAMERA` gates only the scaling math inside the shim; the boot-time call replacement is unconditional but a pure passthrough. |

### Triggering for test

`archipelago_debug` fires Scale Change on **D-Pad Up** (plain, no L modifier) in City Trial, via `ce_api->Do(CUSTOM_EVKIND_SCALE_CHANGE)` in its `main.c`. The Makefile builds no mods unless named, so a test build needs e.g. `make deploy INCLUDE_MODS=archipelago,custom_events,archipelago_debug`.

## Known Limitations

- **On-foot riders are not collision-shrunk.** Only the machine collision sphere is scaled. A player who dismounts mid-event keeps a full-size on-foot sphere (`RiderData`-side mpColl), so a shrunk-model walking Kirby floats slightly. Rare in City Trial, where riders are almost always mounted.
- **The camera dolly affects every player view.** The `CObj_SetEyePosition` shim sits in `PlyCam_Think`, which drives every player camera including all split-screen views, so while the event is active every view dollies in by `factor` - correct for a world-wide event. The replaced call is global (all modes route through `PlyCam_Think`), but outside the event it is a verbatim passthrough. Top Ride uses a separate camera and the event only fires in City Trial. The shim only adjusts the *machine/normal* follow distance via the final eye; it does not touch the C-stick `zoom_amt` (`CamData+0x8c`, saved per player to `stc_plycam_lookup+0x240`), so a player's manual zoom still applies on top of the dolly.
- **All players shrink, including CPUs.** It is a world-wide event. Loose, un-ridden city machines stay full size - they read as big world props, and shrink when a player mounts them.
- **The speed lever scales displacement, not velocity.** `MachineData.velocity` (+0x324) keeps its true value while only the position translation is reduced. Consequences are minor: a hard knockback exceeding `5 x top_speed_current` in one frame is read as a teleport and that frame is not slowed; a respawn slides to its target over a few frames instead of snapping (the teleport guard catches the big initial jump, but the tail is small); and any speedometer/charge read off raw velocity sees full speed.
- **`RiderData.model_scale` is shared with Big/Small Kirby.** If the archipelago mod's `kirby_scale` is also driving `model_scale`, this event overwrites it for its duration and restores 1.0 on end, cancelling an active Big/Small Kirby. The two mods are separate and rarely combined, so this is an accepted edge case.

## Symbols

| Symbol | Address | Size | Notes |
|--------|---------|------|-------|
| `Rider_ApplyModelMatrix` | 0x80190848 | 0x40 | Bakes `base x model_scale` into the rider model matrix each frame |
| `Machine_ApplyModelMatrix` | 0x801c9074 | 0x68 | Machine analogue: bakes `model_scale x model_scale_base` into the machine model's user matrix each frame (articulated-model siblings at 0x801c9308/9464/9694/cb50c do the same per sub-joint) |
| `gmLanMenu_Scale3DObject` | 0x80054414 | 0x168 | Builds an SRT matrix from a scale + 3 orientation/position vectors and bakes it into a JObj's user matrix (`JObj+0x44`); shared by the rider, machine, item and actor model appliers |
| `grGetStageScale` | 0x800d3058 | 0x24 | Reads `StageNode.scale` (+0x08); feeds visual setup only |
| `grGetMachineAccel` | 0x800cea80 | 0x24 | Reads `StageNode.machine_accel` (+0x04). Rejected speed lever - cancels out of the thrust/drag equilibrium. |
| `Machine_PhysicsThink` | 0x801c6368 | 0x240 | Integrates `MachineData.pos` (+0x3E8) from velocity (+0x324) + several impulse vectors; the displacement this produces is what the speed lever scales |
| `Machine_AdjustAttributes` | 0x801c7278 | 0x158 | Re-derives `top_speed_current` (+0x398) from `top_speed_ground` (+0x4f0) / airborne (+0x5ac). Rejected speed lever, but its value sizes the teleport threshold. |
| `3D_CreateStageModel` | 0x800dcbf0 | 0x318 | Instantiates terrain + backdrop JObjs at load, applies stage scale |
| `EnvColl_Raycast` | 0x800d1ac4 | 0x70 | Raycast wrapper (machine sphere collision does **not** route through it) |
| `Raycast_Do` | 0x800d9958 | 0x4DC | Core raycast; reads baked triangles at `grobj+0x5c` + BVH at `grobj+0x704` |
| `mpColl_GetSphereRadius` | 0x802415a8 | 0xF0 | Returns the effective sphere radius, lerping `CollShapeData+0x30` and `+0x34` |
| `mpColl_Update` | 0x80245f70 | 0x164 | Pushes the radius source into the CollData (at spawn/respawn, not per-frame) |
| `Machine_InitialCollisionCheck?` | 0x801cc7a4 | 0x1A4 | Spawn-time mpColl setup; radius from `MachineData.coll_radius_base` (+0x46C), CollData stored at +0x6F8 |
| `Machine_ProcessEnvColl` | 0x801e5108 | 0x520 | Per-frame machine env collision; queries the CollData at `MachineData+0x6F8` |
| `PlyCam_Think` | 0x800b3540 | 0x404 | Per-frame player-camera update; computes the final eye/interest/up/fov and writes them to the COBJ. The `bl CObj_SetEyePosition` at 0x800b3900 is the camera lever's hook site (`bl CObj_SetInterest` is at 0x800b38f4). |
| `CObj_SetEyePosition` | 0x804018ac | 0x6c | Writes a Vec3 into the COBJ's eye WObj (`COBJ+0x24` -> `WObj+0xC`). The call the shim replaces. Map name `HSD_CObjSetEyePosition`. |
| `CObj_SetInterest` | 0x804017d4 | 0x6c | Writes a Vec3 into the COBJ's interest WObj (`COBJ+0x28`); the value the shim reads back as the dolly target. Map name `HSD_CObjSetInterest`. |
| `PlyCam_MachineZoomAdjust` | 0x800b61f4 | 0x5d8 | Builds the eye distance as `abs(xe8.eye - target.pos_high) + zoom_amt` and saves `zoom_amt` (`CamData+0x8c`) to `stc_plycam_lookup+0x240/+0x244` (the per-player **C-stick zoom**, *not* the follow distance). Left untouched by this event. |
