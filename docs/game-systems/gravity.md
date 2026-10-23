# Gravity

How Kirby Air Ride decides which way is down and how hard things fall. One function answers that
question for machines, items, enemies, weapons and the camera alike, from either a local gravity
zone or the stage's global gravity, so a single field write changes the whole world's physics -
and the choice of field decides whether that change is clean.

## The Stage's Gravity

The global gravity is two adjacent `StageNode` fields: `gravity_strength` (+0x0C), a scalar, and
`gravity_dir` (+0x10), a unit down vector. They are reached as
`(*stc_grobj)->gr_data->stage_node`. City Trial's open map (`GrCity1.dat`) holds 0.025 and
(0, -1, 0).

`Gr_GetDownVector` (0x800ceb18) is the single entry point: it writes the unit down direction at a
position into an out-param and returns the strength. It tries the point zones, then the spline
zones, and falls back to the stage's `gravity_dir` and `gravity_strength`. With no `stage_node` at
all it returns 0.025 and the direction (0, 1, 0) from constants.

## Gravity Zones

Two kinds of localized gravity pull toward a center or a curve, for curved or orbital stages:

- **Point zones** (`grGravity_GetPointZoneDown`, 0x800e6834) pull toward the nearest zone point
  within its radius. The points come from a stage position list, cached at `stc_grobj+0x13C`
  (stride 0x24), with radii at `stage_node+0x70`.
- **Spline zones** (`grGravity_GetSplineZoneDown`, 0x800e69b0) pull toward the nearest point on a
  gravity spline from the stage's spline node.

A point's parameters are three floats at `stage_node+0x70`: radius, strength at the center and
strength at the edge, blended linearly by distance. The direction is away from the point, and a
negative strength flips it and returns its magnitude, so an attracting zone needs negative
strengths. Spline zones take the same parameters from `stage_node+0x74`.

No shipped stage defines either kind: the Gravitypos list, the spline node's gravity list and
both parameter blocks are NULL in every `Gr*.dat`, so every query falls through to the global
values.
`grGetGravityposNum` (0x800d0dcc) and `grGetGravityPosition` (0x800d0de4) are a debug pair called
only from `debug_Race3D_loadLocations`.

## Machines

`Machine_RefreshGravity` (0x801c98c4) calls `Gr_GetDownVector` at the machine's position
(`MachineData+0x3E8`) into `MachineData.down` (+0x768), stores the strength at +0x764, and writes
the negated down vector to +0x774 as the machine's up vector. It runs once from `Machine_Create`
and every frame from `Machine_ModelMatrixThink` (0x801c6668), the priority-6 matrix proc, after the
move, so a frame's physics uses the gravity at the previous frame's position.
`Machine_SetTransform` repeats the same refresh inline rather than calling it. `Machine_PhysicsThink`
(0x801c6368) integrates the resulting acceleration.

A parked machine sleeps. Entering WaitStop sets bit 0x02 of `MachineData+0xC3A`, which skips the
gravity refresh, the model matrix, map collision and the fall-death check until the next state
change clears it; a parked machine keeps the down vector it had when it stopped. Its env-collision
callback still runs, and a move, a retired ground triangle or a hit wakes it.

Nothing in the machine code reads world Y for gravity. In the air `Machine_ApplyGravity`
(0x801d8f7c) adds `down * strength` and `Machine_AlignUpToGravity` (0x801d9064) turns the up
vector toward the gravity up at a limited rate. On the ground `Machine_Star_ApplyGroundGravity`
(0x801ece70) applies only the slope component of gravity, all of it when the floor faces along
gravity, and `Machine_Star_AlignUpToGround` (0x801ed330) turns up toward the probed floor normal.
`Machine_Star_GroundSnap` (0x801ede30) pulls a grounded machine down its own up axis by at most
the machine's `snap_max` each frame, which is what holds it to a convex surface: it stays on
while `speed^2 / (2 * radius)` is under `snap_max`.


## Riders and the Camera

`Rider_RefreshGravity` (0x801916fc) refreshes a rider's down vector every frame, on the machine or
off it. A rider on foot falls by `Rider_ApplyFallAccel` (0x801a2818), `down * (0.025 +
RiderData+0x370)`, a fixed strength rather than the stage's. While the stick is held,
`Rider_WalkTurn` (0x801a2168) forces the rider's up to world (0, 1, 0) and the walk input is built
in world XZ, so a rider can land on a tilted or inverted surface but only walk where it faces up.

The player camera samples `Gr_GetDownVector` at its target's raised position and takes the
negated result as its up, so the view rolls with gravity. `PlyCam_MachineBlend` (0x800c24e8)
blends that up per component without renormalizing, so a down vector that flips in one frame
passes the camera through a short up vector. Two calls at the camera eye only scale the sky fog.

## Enemies and Items

Enemies take their down and up from `Gr_GetDownVector` through `EventActor_UpdateGravity`
(0x802054e4), but fall by a private scalar, `param_gravity` at `EnemyData+0x3A4` (default 0.02
from the actor type's data), applied with a ground snap. Changing the stage strength reorients
enemies without changing how fast they fall.

Items resolve their down direction through `Gr_GetDownVector` too (`CityItem_UpdateGravity`
0x80254c50, the landing and bounce handlers, `CityItem_Throw`, `City_SpawnMiscItemsCone`,
`Box_SpawnContents`). An item's `fall_dir` (`ItemData+0x1C8`) is the direction its ground raycasts
and throw angles use, but its fall speed comes from the `gravity` scalar of its
`ItemCommonParam` (+0x44), so items follow the global direction and not its strength.

An item reads gravity only at its transitions. A resting item keeps the `fall_dir` it landed
with, and both fall entries - `ItemGObj_PowerUpEnterFall` (0x80258698) for power-ups and
`ItemGObj_EnterFall` (0x802578c8) for boxes - set `accel` from the current `fall_dir` once without
refreshing it, so a fall keeps its starting direction until a bounce re-aims it.
`CityItem_UpdateGravity` re-reads it. `Item_LimitFallSpeed` (0x80250400) clamps only the Y
component of velocity, so a fall along any other direction is never capped.

## Changing Gravity

The pull is `down * strength`, and a machine's up vector is the negated **raw** down vector. Scaling
`gravity_strength` therefore scales a machine's fall acceleration cleanly while its up vector
stays unit length. Scaling `gravity_dir` also changes the felt pull, but it leaves the derived up
vector non-unit, and the orientation and air-control matrices built from it break. Any override
of the direction has to stay unit length for the same reason.

A field that varies by position belongs in `Gr_GetDownVector`: replacing it moves every machine,
rider, item, enemy, weapon and camera at once, and its vanilla body is short enough to run first
as the fallback. To change only machines, override `Machine_RefreshGravity`. Machines read their
gravity from `MachineData` rather than calling `Gr_GetDownVector`, so the camera and every other
body keep the stage's gravity at the same spot. The inline copy in `Machine_SetTransform` is not
covered, but the next frame's refresh corrects it.
