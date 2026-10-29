# Tornado

The tornado layer of `custom_weather`: a funnel that touches down a few times per City Trial
round, wanders the city, sweeps loose items, breakable props and parked machines into an orbit
around its core, drags at ridden machines that stray inside, and shakes nearby cameras. It deals
no damage, but the props it carries off are destroyed. It ships on the Tornado preset and can be
forced onto any preset from its menu.

## Schedule and path

`TornadoDef` holds `count` (touchdowns per round, default 2; the Tornado preset uses 3),
`duration` (default 1500 frames), `size` (scales the core, reach, height and model), `strength`
(spin, pull and drag) and `speed` (wander speed, default 2.2 units/frame). Touchdowns are spread
over the round by match progress, one per equal slice with jitter inside it. The plan is drawn on
the round's first live frame and kept: a later preset change - a legendary machine's sky - ends a
live funnel and may switch the layer off, but never re-plans, so no touchdown replays.

The funnel roams a disc centred on the out-of-bounds box, its radius 68% of the box's shorter
half-extent (884 units on City Trial). It steers toward waypoints drawn uniformly over the disc's
area, blending its heading toward the current one and adding a small random turn, and draws a new
one on arrival. Waypoints keep it crossing the city; a heading walk that only turned at the edge
would slide along the boundary. The heading is a unit vector rotated with `cosf`/`sinf`. The base
height follows the ground by a downward raycast. Nothing in the path reads the wind.

## Three entry points

The weather tick runs inside the stage think at GObj proc priority 1, and decides where the
funnel is: schedule, touchdown, wander, model placement and claiming targets. What the funnel
does to the world - the orbit's position writes and the rider push - cannot run there: item
physics (`CityItem_PhysicsThink`) and `Machine_PhysicsThink` at priority 4, the item ground snap
at 5 and `PlyCamGObj_Think` at 13 all run later and would overwrite it. So that work is a proc at
priority 23, the last `GObj_UpdateAll` runs, on a GObj of the mod's own. The GObj is created at the
round's first touchdown and freed with the scene, and it sits on p_link 1, so the match pause,
match end and hitstops freeze the orbit along with everything it moves.

## The funnel model

The funnel is the inhale whirlwind, effect `0x3a982`, spawned detached:
`Effect_SpawnSync(NULL, 0x3a982, efgroup, 1, callback)`. Anchor mode 1 takes one vararg, a
callback handed the spawn node, and skips the joint-attach path, so no follow proc moves the model
and its root belongs to the mod. The efgroup asserts on -1. The funnel takes a group of its own
from `Effect_AllocEfGroup` once per round: a rider's `efgroup2` is retired on most of that rider's
state changes, which would take the funnel with it. A spawn the callback rejects is retried every
30 frames rather than every frame.

The whirlwind is authored along its local +Z, so the mod writes the root's world matrix
(`JOBJ.rotMtx` with `JOBJ_USER_DEFINED_MTX`) rather than its SRT: local +Z maps to world +Y, length
to the funnel height and cross-section to 66% of the capture reach, independently, with a roll
about the axis matching the orbit. Mode 1 also skips the effect's one-shot init, so the mod arms
the animation loop itself (`JObj_SetAllAOBJLoopByFlags(root, ANIMBYFLAGS_ALL)`) and calls
`JObj_AnimAll` each frame. That setup waits for the first frame the root is reachable rather than
running in the spawn callback.

The model is never destroyed by hand. The spawn node still points at the GObj, and when the
efgroup is retired the engine destroys it again - a double free that surfaces later as an assert
in some unrelated `GObj_AddUserData`. A lifting tornado hides the tree with
`JObj_SetFlagsAll(root, JOBJ_HIDDEN)`, and the same model is reused for the rest of the round. While
a funnel is live the model is re-validated against the effect GObj list each frame and respawned
if a group kill took it.

The funnel forms and ropes out over 120 frames at each end by narrowing the column - the matrix's
radial scale, floored at 2% so the matrix never degenerates - while the height stays, so it thins
to a thread instead of blinking out. The whirlwind's TEV alpha comes from its texture alone, so
the width is the only fade there is.

## The orbit

A claimed target's polar position around the axis is re-derived from its live position each
frame, so anything nudged by another system corrects itself. It is rotated about the axis at a
constant tangential speed (angular speed `9 * strength / r`, capped), pulled `5% * strength` of the
way toward its ring each frame (never inside 0.3 of the core radius), and lifted until it reaches
the funnel's top. Because that is a pure function of position, everything at one radius would
converge on one rigid spiral, so each claim also draws a fixed variation at claim time: a spin
multiplier (applied to its cap too), the ring it settles into (0.55-2.4 times the core radius), a
climb multiplier and a slow radial wobble.

| Target | Handling |
|---|---|
| Items | Claimed by `ItemData` pointer, skipping boxes (`item_category == 0`). Position overridden, `vel` zeroed and `is_airborne = -1` so the ground snap stops fighting it. Re-validated each frame, so a collected item drops out. |
| Breakable props | Claimed per scene-instance record, not per GObj, since one break-family GObj owns many props. Star poles, coral, trees, rocks, volcano walls and holes, and houses qualify; the forest pitfall, which one hit only cracks, does not. Collision is retired at claim and `JOBJ_USER_DEFINED_MTX` set so the skeleton families stop rebuilding the joint; the joint's world matrix is orbited. After 110 frames the prop is broken. |
| Unridden machines | Claimed by `MachineData` pointer (`rider_gobj == NULL`). Position overridden with `accel` and `velocity` zeroed, since `Machine_PhysicsThink` integrates both. |
| Ridden machines | Pushed, never possessed: an inward, tangential and upward acceleration is added to `MachineData.velocity`, falling off to nothing at the reach. Velocity is an accumulator the controllers only damp, so the push decays and a fast machine can drive out. |

When the funnel lifts, props still in flight are broken, items are released to vanilla physics
and machines drop. A released item gets `ItemData.coll_kind = ITEM_COLL_POINT` (3) and
`point_coll.raycast_idx = -1`. Releasing it with `Item_SetAirborne` alone is not enough. A resting
power-up's envcoll (`Item_RestEnvColl`, 0x80254d4c) only re-checks the ground triangle it
remembers, which is still there, so the item would hover at its release height. A falling one
would land on its stale `land_pos`, the spot it was lifted from. With no valid triangle, the rest
envcoll calls the family's fall entry (`ItemGObj_PowerUpEnterFall`, 0x80258698, for power-ups),
and `ItemColl_HandleLand` re-casts the ground from where the item is now.

## Breaking a prop

A prop breaks through its own family's `coll_func`, by way of `collideWithObject` from hoshi's
`Gr_BreakInstance`, so debris, item drops and broken state all follow vanilla. The collider is
synthesized: a zeroed `CollData` with `radius` set to 1e9 (break force is the radius times the
impact speed squared, so the prop falls to one hit), `coll.g` NULL, `contact_tri_id = -1`, and
`pos_delta` set to -100 times the prop's surface normal. The delta must point into the surface: `grScene_GetImpactSpeed` negates its projection onto
the outward normal and clamps anything non-positive to zero. The record's collision is re-armed
just before the call so the family's "still collidable" check passes, its `world` translation is
set to the prop's flight position for the contact point, and the triangle's `GRCOLL_FLAG_MOVING`
bit is cleared for the call so the moving-record path cannot rewrite the delta.

With no attacker, `GrYakuBreak_GetAttackerPly` (0x80105cb0) credits a break to the first present
player, so that player's `PlayerStats.yakumono_break` counts and `fastest_pillar_break` are saved
around the call and put back: a tornado break counts for nobody's checklist. The engine's own
break-count call still runs, so anything hooked onto it sees the break.

After a successful break, the weak families (coral, trees, rocks) get `JOBJ_USER_DEFINED_MTX`
cleared again, since they never hide their dragged mesh and would leave it frozen in mid-air.
Their debris still spawns at the prop's original ground spot. A break that does not fire puts the
prop back where it stood, collidable again.

## Camera shake

A view near the funnel is shaken through the engine's quake system: a constant quake source
(`Quake_AddConstantSource`, 0x8006de8c) on that view's own shake record (`PlayerCamData.shake`,
+0x74), retuned every frame from the weather tick with `Quake_SetConstantOffset` (0x8006df74).
The offsets are world units along the camera's right and up axes and fall off with the distance
from the view's aim point (`CamData.view.interest`) to the funnel axis, reaching nothing at 1.6
times the reach. A view that leaves that range has its source removed, and the funnel lifting
removes them all.

A view applies only the single source of highest priority on it, whatever that source's offset,
so one global source would mute the machine speed shake on every view, near or far. A per-view
source at priority 1 overrides the speed shake (0) only on the views it covers, and yields to the
medium and large event quakes and the earthquake (2). The jitter comes from the frame count rather
than the game RNG, so the shake draws nothing from it.

## Settings

The Tornado submenu carries the Tornado toggle (Preset / Off / On), Appearances (Preset, 1, 2, 3,
5), Duration (Preset, Brief 0.45x, Long 1.8x, Sustained 3x), Size (Preset, Small 0.55x, Large
1.6x, Colossal 2.4x), Strength (Preset, Gentle 0.5x, Violent 1.7x) and Screen Shake (Off / On).
