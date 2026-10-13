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

City Trial's open map defines neither, so it always falls through to the global values.
`grGetGravityposNum` (0x800d0dcc) and `grGetGravityPosition` (0x800d0de4) are a debug pair called
only from `debug_Race3D_loadLocations`.

## Machines

`Machine_RefreshGravity` (0x801c98c4), run every frame and once from `Machine_Create`, calls
`Gr_GetDownVector` at the machine's position (`MachineData+0x3E8`) into `MachineData.down`
(+0x768), stores the strength at +0x764, and writes the negated down vector to +0x774 as the
machine's up vector. A flag bit at `MachineData+0xC3A` skips the refresh in some machine states.
`Machine_PhysicsThink` (0x801c6368) then integrates the resulting acceleration.

The pull is `down * strength`, and the up vector is the negated **raw** down vector. Scaling
`gravity_strength` therefore scales fall acceleration cleanly while the up vector stays unit
length. Scaling `gravity_dir` also changes the felt pull, but it leaves the derived up vector
non-unit, and the orientation and air-control matrices built from it break.

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
