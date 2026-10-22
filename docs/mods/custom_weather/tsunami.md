# Tsunami

The tsunami layer of `custom_weather`: a few times per City Trial round a set of translucent waves
rolls in off the sea from one side, crosses the island and runs out the far coast, dissipating as
it goes. Each wave carries ridden and parked machines, riders on foot and loose items along its
travel direction, with no damage. It ships on the Tsunami preset and can be forced onto any preset
from its menu.

## Schedule

`TsunamiDef` holds `color` (packed RGBA; default a blue at alpha 150), `count` (tsunamis per
round, default 2; the Tsunami preset uses 3), `waves` (per tsunami, default 3), `size` (height,
depth and front length), `strength` (how hard the water carries bodies) and `speed` (default 4
units/frame). Tsunamis are spread over the round by match progress, one per equal slice with
jitter inside it; the plan is drawn on the round's first live frame and a later preset change
washes out every live wave without re-planning. Each tsunami draws one heading; its waves launch
150 frames apart along it, each centre scattered up to 160 units sideways.

## The sea

City Trial's island sits in a sea mesh that covers the whole out-of-bounds box: ground type 29,
floor kind, so a downward `Raycast_Ground` hits it from above (the ray test is one-sided, so a ray
starting under the surface misses it). The surface is terraced and depends almost only on Z: -8.4
in the south (Z >= 265), ramping to 20.5 through the middle, and climbing to 49.6 (west) and 56.9
(east) in the north. The collision is baked at the 0.7 stage scale, so these are world units.

A wave's spawn line sits 85% of the box's half-extent (1105 units) from its centre, opposite its
heading; on every heading that whole line, ends included, is over the sea. A wave travels twice
that, which clears the far coast from any heading (the longest crossing, from the north-east, is
about 2070). It swells to full height over its first 90 frames out at sea, and its amplitude then
falls as `sqrt(1 - travelled / travel)`, scaling height, opacity and push together.

## Shape and ground following

The front is a shallow arc 950 units either side of the centre at size 1, its middle leading its
ends by 150, with height tapering as `1 - s^4` to nothing at both ends. The cross-section runs
toe to back over 170 units: a foamy toe, a face rising to the crest (56 units) at 24% of the
depth, and a long clear back. The front is sampled in 17 columns, each riding the ground under it.
At spawn each column is seeded from a full-height cast onto the water. After that, 6 columns a
frame, round robin, re-cast from 45 units above their current base and ease 30% of the way to
what they find, so the wave climbs kerbs and slopes but runs through buildings rather than over
their roofs. Waves from the south climb about 80 units to the centre; from the north they run
downhill.

Each wave is drawn as one triangle strip per pair of cross-section rows, with foam mixed toward
white at the toe and crest and the colour's alpha at the crest, on the world camera's GX link.

## Carrying bodies

Each wave is its own GObj (class 212, p_link 1, so the match pause and hitstops freeze it) with a
proc at priority 2: after the priority-0 clear of the per-frame displacements and before the
priority-4 physics adds them to positions. A body's weight is the cross-section height at its
spot (at least 0.3 in the foam ahead of the face), times the end taper and the amplitude, and 0
off either end of the front, ahead of the toe, behind the back, more than 12 units over the
surface or 30 units under its base. The carry speed is 1.15 times the wave's speed; each frame a
body is moved by the part of it the body's own velocity along the wave does not already cover,
times its weight and the strength. A body driving against the wave is slowed, and one outrunning
it is left alone.

| Body | Channel |
|---|---|
| Ridden and parked machines | `MachineData.ext_disp`, a displacement no vanilla code writes, zeroed at priority 0 and added to `pos` at 4. It moves the machine like a conveyor: grip cannot eat it, and it ends when the wave passes. A parked machine that moves leaves WaitStop for WaitRun and settles under its own friction. Machines on a rail, dead or fall-dead are skipped. |
| Riders on foot | `RiderData.platform_disp`, added to `pos` off the machine at priority 4. The walk physics steers, damps and clamps `self_vel`, so a velocity push would not survive. |
| Resting power-ups | Moved directly: a resting item's physics is a no-op and nothing snaps it. The new spot is re-cast within 12 units up or down; a step it cannot make - a wall, a ledge, the sea - leaves the item where it is. Its raycast index and landing position follow. |

Boxes and items in flight are left alone. Ridden machines carried fast enough arm their ram
hitbox, as any fast machine does.

The first time a wave catches each player it spawns Kirby's water-landing splash (particle
20118) at scale 2.5 and plays `SFX_tyak_water` (0x40031) there. While its amplitude is at least
0.3, every 10 frames a splash also rises from a random column of the toe, scaled by the size and
amplitude.

A spent wave destroys its own GObj from its proc; `GObj_UpdateAll` defers that until the callback
returns. The waves' userdata are static pool slots, given a no-op destructor because `GObj_Destroy`
asserts on a NULL one.

## Settings

The Tsunami submenu carries the Tsunami toggle (Preset / Off / On), Appearances (Preset, 1, 2, 3,
5), Waves (Preset, 1, 3, 5), Size (Preset, Small 0.6x, Large 1.5x, Huge 2.2x), Strength (Preset,
Gentle 0.5x, Strong 1.6x, Violent 2.4x) and Speed (Preset, Slow 0.65x, Fast 1.5x).
