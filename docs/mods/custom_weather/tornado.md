# Tornado

The tornado layer of `custom_weather`: a funnel that touches down a few times per City Trial
round, wanders the city, sweeps loose items, breakable props and parked machines into an orbit
around its core, drags at riders who stray inside, and shakes nearby cameras. Nothing takes
damage - it is purely kinetic. It ships on the Tornado preset and can be forced onto any preset
from its menu. It is also the clearest example of driving an engine effect, game objects and the
camera from mod code without patching their code.

## Schedule and path

`TornadoDef` holds `count` (touchdowns per round, default 2; the Tornado preset uses 3),
`duration` (default 1500 frames), `size` (scales the core, reach, height and model), `strength`
(spin, pull and drag) and `speed` (wander speed, default 2.2 units/frame). Touchdowns are spread
over the round by match progress, one per equal slice with jitter inside it; a preset change
mid-round re-plans from the current progress and skips touchdowns already behind it, so none
replays.

The funnel roams a disc centred on the out-of-bounds box, its radius 68% of the box's shorter
half-extent (884 units on City Trial). It steers toward waypoints drawn uniformly over the disc's
area, blending its heading toward the current one and adding a small random turn, and draws a new
one on arrival. Waypoints are what keep it crossing the city: a heading walk that only turns at
the edge slides along the boundary for the whole tornado. The heading is a unit vector rotated
with `cosf`/`sinf`, since the freestanding libm has no `atan2f`. The base height follows the ground
by raycast. Nothing in the path reads the wind.

## Two entry points

The weather tick runs inside the stage think at GObj proc priority 1, and decides where the
funnel is: schedule, touchdown, wander, model placement and claiming targets. What the funnel
does to the world - the orbit's position writes, the rider push and the camera shake - cannot run
there: item physics (`CityItem_PhysicsThink`) and `Machine_PhysicsThink` at priority 4, the item
ground snap at 5 and `PlyCamGObj_Think` at 13 all run later and would overwrite it. So that work
is the proc of a GObj of the mod's own at priority 23, the last `GObj_UpdateAll` runs. It is
created at the round's first touchdown and freed with the scene, and it sits on p_link 1, so the
match pause, match end and hitstops freeze the orbit along with everything it moves.

## The funnel model

The funnel is the inhale whirlwind, effect `0x3a982`, spawned detached:
`Effect_SpawnSync(NULL, 0x3a982, efgroup, 1, callback)`. Anchor mode 1 takes one vararg, a
callback handed the spawn node, and skips the joint-attach path, so no follow proc moves the model
and its root belongs to the mod. The efgroup asserts on -1 and is borrowed from a live rider's
`RiderData.efgroup2`, the group the inhale itself uses. A spawn the callback rejects is retried
every 30 frames rather than every frame.

The whirlwind is authored along its local +Z, so the mod writes the root's world matrix
(`JOBJ+0x44` with `JOBJ_USER_DEFINED_MTX`) rather than its SRT: local +Z maps to world +Y,
length to the funnel height and cross-section to 66% of the capture reach, independently, with a
roll about the axis matching the orbit. Mode 1 also skips the effect's one-shot init, so the mod
arms the animation loop itself (`JObj_SetAllAOBJLoopByFlags(root, ANIMBYFLAGS_ALL)`) and calls
`JObj_AnimAll` each frame. That setup waits for the first frame the root is reachable rather than
running in the spawn callback.

The model is never destroyed by hand. The spawn node still points at the GObj, and when the
efgroup is retired the engine destroys it again - a double free that surfaces later as an assert
in some unrelated `GObj_AddUserData`. A lifting tornado hides the tree with
`JObj_SetFlagsAll(root, JOBJ_HIDDEN)`, and the same model is reused for the rest of the round,
re-validated against the effect GObj list each frame and respawned if a group kill took it.

The funnel forms and ropes out over 120 frames at each end. The ramp narrows the column - the
matrix's radial scale, floored so the matrix never degenerates - while the height stays, so it
thins to a thread instead of blinking out. It also scales each material's authored alpha, which
dims this funnel alone because every MObj owns its own copied material. The width ramp is the half
that shows: the whirlwind's TEV alpha takes only the texture's alpha, so the material write may
have no visible effect.

## The orbit

A claimed target's polar position around the axis is re-derived from its live position each
frame, so anything nudged by another system corrects itself. It is rotated about the axis at a
constant tangential speed (angular speed `9 * strength / r`, capped), pulled 5% of the way toward
its ring each frame, and lifted until it reaches the funnel's top. Because that is a pure function
of position, everything at one radius would converge on one rigid spiral, so each claim also draws
a fixed variation at claim time: a spin multiplier (applied to its cap too), the ring it settles
into (0.55-2.4 times the core radius), a climb multiplier and a slow radial wobble. The core
radius is floored, since the orbit divides by it.

| Target | Handling |
|---|---|
| Items | Claimed by `ItemData` pointer, skipping boxes (`item_category == 0`). Position overridden, `vel` zeroed, `is_airborne = -1` and `ITEM_X35A_GROUNDED` cleared so the ground snap stops fighting it. Re-validated each frame, so a collected item drops out. |
| Breakable props | Claimed per scene-instance record, not per GObj, since one break-family GObj owns many props. Star poles, forest pitfalls, coral, trees, rocks, volcano walls and holes, and houses qualify. Collision is retired at claim and `JOBJ_USER_DEFINED_MTX` set so the skeleton families stop rebuilding the joint; the record's world translation is orbited. After 110 frames the prop is broken. |
| Unridden machines | Claimed by `MachineData` pointer (`rider_gobj == NULL`). Position overridden with `accel` and `velocity` zeroed, since `Machine_PhysicsThink` integrates both. |
| Ridden machines | Pushed, never possessed: an inward, tangential and upward acceleration is added to `MachineData.velocity`, falling off to nothing at the reach. Velocity is an accumulator the controllers only damp, so the push decays and a fast machine can drive out. |

When the funnel lifts, props still in flight are broken, items are released to vanilla physics
and machines drop.

## Breaking a prop

A prop breaks through its own family's `coll_func`, by way of `collideWithObject`, so debris, item
drops, break credit and broken state all follow vanilla. The collider is synthesized: a zeroed
`CollData` with `radius` set to 1e9 (break force is the radius times the impact speed squared, so
any prop falls to one hit), `coll.g` NULL so the break is credited to nobody, `contact_tri_id = -1`,
and `pos_delta` set to -100 times the prop's surface normal. The delta must point into the surface:
`grScene_GetImpactSpeed` negates its projection onto the outward normal and clamps anything
non-positive to zero. The record's collision is re-armed just before the call so the family's "still
collidable" check passes, and the triangle's `GRCOLL_FLAG_MOVING` bit is cleared for the call so the
moving-record path cannot rewrite the delta.

After a successful break, the weak families (coral, trees, rocks) get `JOBJ_USER_DEFINED_MTX`
cleared again, since they never hide their dragged mesh and would leave it frozen in mid-air.
Their debris still spawns at the prop's original ground spot.

## Camera shake

Writing `CamData.eye_pos` / `interest_pos` does nothing; they are not what reaches the camera
object. Instead the mod drives the engine's own per-view shake record, `PlayerCamData.shake`
(+0x74, a `CamShakeRec`), which `PlyCamGObj_Think` applies after building the camera basis while
its `gate` is positive:

```
eye += right * eye_right * scale_right  +  up * eye_up * scale_up
```

Both scales start at 1.0, so writing `eye_right` / `eye_up` in world units and raising `gate` is
the whole mechanism, with no code patch to collide with other mods that patch the camera solve.
Amplitude falls off with distance from the view's own aim point out to 1.6 times the reach. The
pointer is checked to be an aligned MEM1 address before use, and the gate is sticky, so it is
lowered explicitly when the funnel lifts.
