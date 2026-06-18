# Scale Change Event

## Concept

A custom City Trial event — "The world is growing!" — that makes the world feel
bigger by **shrinking every player** (rider model + machine model + machine
collision sphere), slowing them down, and pulling each player's camera in to
match, rather than scaling the stage. A half-size player in an unchanged world
reads exactly as a normal-size player in a doubled world, and the world's
collision is never touched, so the floor stays solid. Registered as
`CUSTOM_EVKIND_SCALE_CHANGE` (18) with handlers
`ScaleChange_Start` / `_Active` / `_End` / `_End2`, BGM `0x32`, sky preset 3
("Dusk 2"), duration 900 (~15 s), HUD text "The world is growing!".

Implementation: `mods/custom_events/src/event_scale_change.c`.

## Why not scale the world

The intuitive implementation — grow the stage so everything gets bigger — does
not work, because **collision is pre-baked, world-scale spatial data that nothing
rescales at runtime**:

- `Raycast_Do` (0x800d9958) reads a triangle array at `(*stc_grobj)+0x5c`
  (stride `0x40`) and a BVH/octree at `(*stc_grobj)+0x704` whose nodes carry
  baked world-space AABBs (center at `node+0x14..+0x1c`, extents at
  `node+0x20..+0x28`). Machine **sphere** collision (`mpColl_UpdateCollision`)
  reads the same triangle data directly.
- The stage `scale` field (`StageNode+0x08`, read by `grGetStageScale`
  0x800d3058) only feeds **visual** JObj setup in `3D_CreateStageModel` at load.
  Writing it at runtime moves neither the triangle mesh nor the BVH.

So scaling the stage visually would leave the collision floor at its original
size and machines would fall through it (the "collision was off" symptom of the
earlier stage-scale attempt). Genuinely scaling collision would mean rewriting
every triangle vertex **and** every BVH AABB **and** their plane constants, then
reversing it perfectly on event end — large and fragile. Hooking
`EnvColl_Raycast` to transform ray coordinates wouldn't help either: the machine
sphere path doesn't route through it.

Shrinking the player turns "rescale a baked mesh + BVH" into "scale a handful of
per-object scalars," and leaves collision correctness entirely to the untouched
world. That is why this event shrinks players instead of growing the world.

## What gets scaled

Per active player slot (City Trial, up to 4), every frame, by an eased `factor`
(1.0 = normal, shrinking toward `SCALE_TARGET_FACTOR` = 0.5):

| Lever | Field | Notes |
|-------|-------|-------|
| Rider model | `RiderData.model_scale` (+0x348) | The engine bakes it into the rider model matrix every frame (`Rider_ApplyModelMatrix` 0x80190848). Writing the field is the whole mechanism — same one Big/Small Kirby uses. Visual only. |
| Machine model | `MachineData.model_scale` (+0x310) | The machine's exact analogue of the rider's field. The machine model appliers (`Machine_ApplyModelMatrix` 0x801c9074 and its articulated-model siblings) bake `model_scale × model_scale_base` (+0x468) into the model's *user* matrix every frame via the same `gmLanMenu_Scale3DObject` the rider uses, so writing the float is the whole mechanism. Visual only; rests at 1.0 (per-vehicle size lives in `model_scale_base`). |
| Machine collision | machine CollData at `MachineData+0x6F8` → `radius` (+0x344) and `shape_data->radius`/`radius2` (+0x30/+0x34) | The effective sphere radius the engine reads. Shrinking it also **lowers the machine's ground-rest height** automatically: the collision system holds the sphere center a `radius` above the contacting floor, so a smaller sphere sits the (smaller) model correctly on the unchanged terrain with no height fix-up. |
| World speed | `MachineData.pos` (+0x3E8) — its per-frame delta | We keep only `factor` of the machine's per-frame world displacement: each frame, pull the position back by `(1 − factor)` of the distance it moved since our last pass. The engine integrates velocity and resolves collision exactly as normal around this, so the machine never ends a frame penetrating, and the rider (`RiderData+0x300`) + camera read the machine position downstream and follow with no desync. A teleport (per-frame jump > `5 ×` the machine's `top_speed_current`) is passed through unscaled so respawns/warps land on target. See **Why the displacement, not a speed stat** below. |
| Camera | the per-player camera's eye→interest distance, written each frame by `PlyCam_Think` | We replace the `bl CObj_SetEyePosition` inside `PlyCam_Think` (call site `0x800b3900`) with a shim that moves the final eye toward the interest along their line by `factor` (`eye' = interest + (eye − interest)·factor`), halving the follow distance at 0.5. The interest was set on the same COBJ the instruction before (`bl CObj_SetInterest` @ `0x800b38f4`), so we read it straight back off the COBJ — no lag, no capture. View direction, up, and FOV are untouched; only distance changes. See **Why move the camera, not change FOV** below. |

### Why the rest height follows the radius

The machine's collision sphere is what rests on the floor; its center sits one
`radius` above the contacting triangle. The visual model is drawn around that
center. If only the model shrank, the full-size sphere would hold the small model
floating half a machine-height up; if only the sphere shrank, the full-size model
would clip into the ground. Shrinking **both together** keeps the model resting
correctly on the floor — which is the entire reason the machine-model and
machine-collision levers must move in lockstep.

### Why the displacement, not a speed stat

The first two attempts scaled a stat and **failed in-game**, which is worth
recording so they aren't retried:

- **`StageNode.machine_accel` (+0x04).** Cruise speed is an equilibrium where
  forward thrust balances quadratic drag (`drag ∝ v² · drag_stat · machine_accel`,
  and thrust `∝ throttle · accel_stat · machine_accel`). `machine_accel` appears in
  both terms, so it **cancels** out of the equilibrium: scaling it changes how fast
  a machine *reaches* cruise, but not the cruise speed itself. A half-size player at
  full cruise speed reads as moving twice as fast — the original bug.
- **`MachineData.top_speed_current` (+0x398).** This is a real per-state speed cap,
  but only some vehicles' controllers consult it (the over-speed drag term is gated
  behind another stat), so writing it slowed nothing on the machines tested.

Both of those try to influence an emergent quantity through one of its many inputs.
Scaling the **resulting displacement** sidesteps the entire stat/controller web:
let the engine produce whatever velocity it wants and resolve collision normally,
then keep only `factor` of the frame's actual movement. It is downstream of every
force, so it works regardless of how the speed was produced.

The clamp is timing-agnostic: run once per frame at any fixed point, the
*displayed* per-frame movement converges to `factor ×` the engine's, because each
frame we re-pull the position back by `(1 − factor)` of the last full delta.
Velocity (`+0x324`) is left untouched, so the engine's turning/handling and the
charge-by-speed read still see the true velocity — only the world translation is
reduced.

### Why move the camera, not change FOV

Uniformly scaling the whole scene up by `1/factor` — geometry **and the camera
rig** — produces a pixel-identical image (it is just a change of units). So the
exact way to fake "the world grew" with a shrunk player is to also move the camera
`factor` of the way to its target: a player at `factor` size viewed from `factor`
distance renders at its *normal* on-screen size, while the unchanged world around
it now subtends `1/factor ×` the angle and reads as bigger. Using the **same
factor** for the player scale and the camera distance is what makes the player
look untouched while the world appears to balloon.

The lever is a true **dolly** (move the eye in along the eye→interest line), not a
zoom. We replace the eye-set call inside `PlyCam_Think` rather than write the COBJ
from the event loop because the camera is recomputed from scratch every frame
*after* most game logic — an external poke to the COBJ would be overwritten, and
`PlyCam_Think`'s own input (`CamData.x14`) is recomputed inside the same function
just before it is consumed, so there is no field to pre-seed either. Intercepting
the final `CObj_SetEyePosition` is downstream of the entire camera pipeline (kind
dispatch, C-stick `zoom_amt`, rail/normal transitions), so it works regardless of
how the eye was produced — the camera analogue of scaling the machine's final
displacement instead of a speed stat.

Changing **FOV** instead was considered (and is what the C-stick zoom does via
`zoom_amt`, max 8.4) but rejected: a wider FOV magnifies but also warps perspective
(foreshortening changes, a fish-eye-ish look), which is *not* what scaling the
world does. A dolly preserves perspective exactly. FOV is therefore left untouched.

## Implementation

### Capture / restore

Only the **collision** originals are captured **per player slot, on first touch
of a machine** (`SlotScale` array): the machine GObj plus its `CollData.radius`
and the two `CollShapeData` radii. Those are absolute sizes that vary by vehicle,
so capture is assumption-free and exact: `factor` 1.0 writes the captured
originals straight back. The model-scale levers need no capture — both
`model_scale` fields are "1.0 = normal" multipliers, so the levers just write the
`factor` directly and ease back to 1.0. The speed lever needs no capture either —
it only ever reads/writes the live position; it just tracks `last_pos` per slot
(re-seeded, no clamp, on the first frame of a new machine). The camera lever is
fully stateless: the shim reads each COBJ's just-set interest, moves the live eye,
and is a pure passthrough whenever the event is idle (`scale_active == 0` or
`factor == 1.0`), so it never needs to restore anything. When a slot's machine
GObj changes (the player grabs a different machine, or respawns into a new one),
the originals are re-captured; when a slot empties (the player is on foot), its
capture is dropped so a later mount re-captures fresh.

### Easing

`factor` eases between 1.0 and the target by `SCALE_EASE_STEP` (0.02/frame,
~0.4 s over the 0.5 swing) rather than snapping — gentler on the eye and on the
collision sphere (a sudden radius change risks a ground snap/penetration). The
event's four callbacks drive it:

- `ScaleChange_Start` (state 1→2): reset per-slot captures, set `factor = 1.0`.
- `ScaleChange_Active` (state 2, each frame): ease toward the target, apply.
- `ScaleChange_End` (state 3, each frame): ease back toward 1.0, apply. This is
  why the event registers an `end` callback (most custom events don't).
- `ScaleChange_End2` (once, cleanup end): apply `factor` 1.0 for an exact restore
  (collision originals written back, speed clamp becomes a no-op), clear state.

The camera lever is the exception to the callback model: its `CObj_SetEyePosition`
shim is installed **once at boot** by `ScaleChange_InstallHooks` (called from
`CustomEvents_OnBoot`) and reads the live `scale_active` / `cur_factor` statics, so
it follows the same ease for free and self-disables when the event ends — no
per-callback camera code, no teardown.

### Tuning knobs (`event_scale_change.c`)

- `SCALE_TARGET_FACTOR` (0.5) — how small players shrink (and the world-speed +
  camera-distance factor; player scale and camera distance deliberately share it).
- `SCALE_EASE_STEP` (0.02) — ease speed.
- `SCALE_TELEPORT_SPEED_MULT` (5.0) — per-frame jump above `this × top_speed_current`
  is treated as a teleport and passed through unscaled.
- `SCALE_AFFECTS_RIDER_MODEL` / `_MACHINE_MODEL` / `_COLLISION` / `_SPEED` /
  `_CAMERA` — each lever is independently toggleable so a single one can be
  isolated when tuning in-game. (`_CAMERA` gates only the scaling math inside the
  shim; the boot-time call replacement is unconditional but a pure passthrough.)

### Triggering for test

`archipelago_debug` fires Scale Change on **D-Pad Up** (plain, no modifier) in
City Trial (`ce_api->Do(CUSTOM_EVKIND_SCALE_CHANGE)` in `main.c`). Build with
`make deploy EXCLUDE_MODS=custom_weather` so `custom_events` + `archipelago_debug`
are included (both are in the default `EXCLUDE_MODS`).

## Open Questions / caveats

- **On-foot riders aren't collision-shrunk.** Only the machine collision sphere
  is scaled. A player who dismounts mid-event keeps a full-size on-foot sphere
  (`RiderData`-side mpColl), so a shrunk-model walking Kirby would float slightly.
  Rare in City Trial (riders are almost always mounted); left out of v1.
- **Camera dolly affects every player view.** The `CObj_SetEyePosition` shim sits
  in `PlyCam_Think`, which drives every player camera (all split-screen views), so
  while the event is active every view dollies in by `factor` — correct for a
  world-wide event. The replaced call is global (all modes route through
  `PlyCam_Think`), but outside the event it is a verbatim passthrough, so no other
  camera is affected. Top Ride uses a separate camera and the event only fires in
  City Trial, so neither is touched. The shim only adjusts the *machine/normal*
  follow distance via the final eye; it does not touch the C-stick `zoom_amt`
  (`CamData+0x8c`, saved per player to `stc_plycam_lookup+0x240`), so a player's
  manual zoom still applies on top of the dolly.
- **Affects all players including CPUs.** It is a world-wide event, so every
  rider shrinks; loose, un-ridden city machines stay full size (they read as big
  world props, and shrink when a player mounts them).
- **Speed lever scales displacement, not velocity.** `MachineData.velocity`
  (+0x324) is left at its true value while only the position translation is
  reduced. Consequences, all minor/cosmetic: a hard knockback that exceeds
  `5 × top_speed_current` in one frame is read as a teleport and that frame isn't
  slowed; a respawn slides to its target over a few frames instead of snapping
  (the teleport guard catches the big initial jump, but the tail is small); and any
  speedometer/charge read off raw velocity sees full speed. Only the machine
  position is scaled — the rider/camera follow it, so they stay in sync.
- **`RiderData.model_scale` is shared with Big/Small Kirby.** If the archipelago
  mod's `kirby_scale` is also driving `model_scale`, this event overwrites it for
  its duration and restores 1.0 on end (cancelling an active Big/Small Kirby).
  The two mods are separate and rarely combined (`custom_events` is excluded from
  the default build), so this is an accepted edge case.

## Symbols

| Symbol | Address | Size | Notes |
|--------|---------|------|-------|
| `Rider_ApplyModelMatrix` | 0x80190848 | 0x40 | Bakes `base × model_scale` into the rider model matrix each frame |
| `Machine_ApplyModelMatrix` | 0x801c9074 | 0x68 | Machine analogue: bakes `model_scale × model_scale_base` into the machine model's user matrix each frame (articulated-model siblings at 0x801c9308/9464/9694/cb50c do the same per sub-joint). Was `zz_801c9074_`. |
| `gmLanMenu_Scale3DObject` | 0x80054414 | 0x14C | Builds an SRT matrix from a scale + 3 orientation/position vectors and bakes it into a JObj's user matrix (`JObj+0x44`); shared by the rider, machine, item, and actor model appliers |
| `grGetStageScale` | 0x800d3058 | 0x24 | Reads `StageNode.scale` (+0x08); feeds visual setup only |
| `grGetMachineAccel` | 0x800cea80 | 0x24 | Reads `StageNode.machine_accel` (+0x04). **Rejected speed lever** — cancels out of the thrust/drag equilibrium (was `zz_800cea80_`). |
| `Machine_PhysicsThink` | 0x801c6368 | 0x240 | Integrates `MachineData.pos` (+0x3E8) from velocity (+0x324) + several impulse vectors; the displacement this produces is what the speed lever scales |
| `Machine_AdjustAttributes` | 0x801c7278 | 0x158 | Re-derives `top_speed_current` (+0x398) from `top_speed_ground`/airborne. **Rejected speed lever** — only some vehicles' controllers consult `top_speed_current`. Still used: its value sizes the teleport threshold. |
| `3D_CreateStageModel` | 0x800dcbf0 | 0x318 | Instantiates terrain + backdrop JObjs at load, applies stage scale |
| `EnvColl_Raycast` | 0x800d1ac4 | 0x70 | Raycast wrapper (machine sphere collision does **not** route through it) |
| `Raycast_Do` | 0x800d9958 | 0x4DC | Core raycast; reads baked triangles at `grobj+0x5c` + BVH at `grobj+0x704` |
| `mpColl_GetSphereRadius` | 0x802415a8 | 0xF0 | Returns the effective sphere radius, lerping `CollShapeData+0x30 ↔ +0x34` |
| `mpColl_Update` | 0x80245f70 | 0x164 | Pushes the radius source into the CollData (at spawn/respawn, not per-frame) |
| `Machine_InitialCollisionCheck` | 0x801cc7a4 | 0x1A4 | Spawn-time mpColl setup; radius from `MachineData+0x46C`, CollData stored at `+0x6F8` |
| `Machine_ProcessEnvColl` | 0x801e5108 | 0x520 | Per-frame machine env collision; queries the CollData at `MachineData+0x6F8` |
| `PlyCam_Think` | 0x800b3540 | 0x404 | Per-frame player-camera update; computes the final eye/interest/up/fov and writes them to the COBJ. The `bl CObj_SetEyePosition` at `0x800b3900` is the camera lever's hook site (`bl CObj_SetInterest` is at `0x800b38f4`). |
| `CObj_SetEyePosition` | 0x804018ac | 0x6c | Writes a Vec3 into the COBJ's eye WObj (`COBJ+0x24` → `WObj+0xC`). The call our shim replaces / wraps. |
| `CObj_SetInterest` | 0x804017d4 | 0x6c | Writes a Vec3 into the COBJ's interest WObj (`COBJ+0x28`); the value the shim reads back as the dolly target. |
| `PlyCam_MachineZoomAdjust` | 0x800b61f4 | 0x5d8 | Builds the eye distance as `\|xe8.eye − target.pos_high\| + zoom_amt` and saves `zoom_amt` (`CamData+0x8c`) to `stc_plycam_lookup+0x240/+0x244` (the per-player **C-stick zoom**, *not* the follow distance). Left untouched by this event. |

## See Also

- `custom-events.md` — the framework that registers and dispatches this event.
- `event-gravity-change.md` — sibling custom event; also modifies a `StageNode`
  field (`gravity_strength`) per frame with the same start/active/end2 shape.
- `collision-system.md` — the mpColl / raycast system, the baked collision data,
  and the machine CollData at `MachineData+0x6F8`.
- `kirby-model-scale.md` — the `RiderData.model_scale` mechanism this reuses for
  the rider; the machine has the exact analogue at `MachineData.model_scale`
  (+0x310), baked by `Machine_ApplyModelMatrix`.
