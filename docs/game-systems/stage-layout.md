# Stage Layout

Where things are on a stage: the frames the stage files are authored in, how the load turns them
into world units, and the authored data that places things in that world - the joint table,
the position lists, the spline web and the ground types. A modder needs this to put something
at a spot, to read a coordinate off the stage files, or to compare a live position with the
data. Collision queries and zone behavior are covered with the collision system; this is the
geometry those run over.

## Files and the joint table

Every ground is two archives. `Gr<X>.dat` holds the stage data (`grData<X>`: collision, zones,
positions, splines, props), and `Gr<X>Model.dat` the geometry (`grModel<X>`, with its animation
in `grModelMotion<X>`). `grLoadStageArchive` (0x800ce7a0) loads the model and writes the two
publics into `GrData+0x0C` and `+0x10`, which are zero on disc. Top Ride has no ground file: its
courses are 2D tile maps in `A2a2dBG_*.dat`.

`CreateStageModel_3D` (0x800dcbf0) loads the terrain root (`grModel<X>` slot 0, a record whose
first word is the root `JOBJDesc`) and **replaces** the root's scale with the stage scale,
`grGetStageScale()` (0x800d3058, `StageNode+0x08`, 1.0 when there is no StageNode). The root's
rotation and translation stay; Nebula Belt's root is translated by (0, 0, -50).

`grJointTable_Init` (0x800d8b98) then builds `GrObj.joint_table` (+0x104) as a preorder walk from
the root (`grJointTable_WalkJObjs`, 0x800d8a60, and its desc twin `grJointTable_WalkDescs`,
0x800d87ec): each joint, then its children depth-first, the root's own siblings left out and the
children of an INSTANCE joint skipped. Collision joints, zone joints and stage props name a joint
by its index here. Each joint's world matrix is `parent * T * Rz * Ry * Rx * S`, as
`HSD_JObjMakeMatrix` builds it: the off-diagonal terms are corrected by the parent's accumulated
scale unless the joint sets CLASSICAL_SCALING (0x8). The load-time bake reads the rest pose, so
moving geometry (lifts, gates, the waterwheel's cars) starts where the model's desc puts it.

## World units

The stage files are authored unscaled. Each kind of data reaches world units its own way:

| Data | On disc | At load |
|---|---|---|
| Collision and zone vertices | local to their joint | times the joint's world matrix (`grColl_BakeRecords`, 0x800d6a70; `grZone_BakeRecord`, 0x800dd2a4) |
| Position lists | relative to the stage root | times the stage scale (`fn_grSetupNullpos`, 0x800e3f80) |
| Splines | stage units | CVs and `total_length` times the stage scale, `seg_poly` rebuilt (`grSpline_ScaleNode`, 0x800de514, through `splScale`, 0x8006c66c) |
| Out-of-bounds box | world units | used as stored (`calcDistanceFromOOB`, 0x800d4f20) |

A live machine or rider position, the baked collision pool and every scaled spline are therefore
in one frame. Only the terrain root's collision slice reads as "file vertex times stage scale"; a
prop's slice (a house, a tree, the lighthouse) is meaningless without its joint's matrix. Stage
scales run from 0.7 (City Trial) through 0.85 (Fantasy Meadows) and 0.92 (Machine Passage) to
2.0 (Destruction Derby 2); most Air Ride courses use 1.0.

## Position lists

`GrData.PositionNode` (+0x20) has thirteen lists: start, enemy, gravity, airflow, conveyor, item,
event, vehicle, global dead, local dead and yakumono positions, then item and vehicle areas. Each
list is `{u32 *joint_indices, records, count}`, one of the two pointers set. A record is 0x24
bytes, `{Vec3 pos, Vec3 forward, Vec3 up}`; an area record is 0x18, `{Vec3 pos, Vec3 forward}`.
Headings are measured from +Z toward +X. A joint-indexed list names joints of the node's own
`PositionJoint` tree (`PositionNode+0x00`).

City Trial's 100 event positions are split among the events by the location table in
`GrCity1Event.dat`:

| Positions | Event |
|---|---|
| 0-9 | Dyna Blade |
| 10-19 | Tac |
| 20-29 | Meteor (read in pairs) |
| 30-39 | Pillar |
| 40-59 | Restoration Area (five of them per event) |
| 60-69 | Rail Fire (the five stations, two positions each) |
| 70-79 | Secret Chamber (70 is the item origin) |
| 80-99 | Machine Formation (start and end pairs) |

## Splines

An `HSD_Spline` is `{u8 type, u8, s16 n, float tension, Vec3 *cv, float total_length,
float *seg_length, float (*seg_poly)[5]}` with `n - 1` segments whatever its type.
`splGetSplinePoint` (0x80414fc0) maps a parameter u in [0, 1] uniformly onto the segments, not
by arc length:

| Type | CVs | Segment i |
|---|---|---|
| 0 linear | n | lerp of cv[i] and cv[i+1] |
| 1 Bezier | 3n - 2 | the cubic over cv[3i .. 3i+3] |
| 2 B-spline | n + 2 | uniform cubic B-spline over cv[i .. i+3]; does not pass through cv[0] |
| 3 tension | n + 2 | cardinal spline with tension a, from cv[i+1] to cv[i+2]; the first and last CVs only shape the ends |

`seg_length` holds the cumulative arc length at each knot, normalized to 0..1, and `seg_poly`
each segment's |dP/dt|^2 as a quartic in t. `splArcLengthGetParameter` (0x80415758) finds the
segment from `seg_length` and bisects t against an 8-interval Simpson integral of the square
root of `seg_poly` (`splSegmentArcLength`, 0x80415450). Under a rigid motion plus a uniform
scale f, `seg_length` stays valid, `total_length` grows by f and `seg_poly` by f^2.

`GrData.SplineNode` (+0x1C) holds five families:

- **Course** (`+0x00`, a setup record). `+0x00` lists the course pieces, all linear and one-way;
  `+0x04` is the main route, the ordered piece indices one lap runs; `+0x08` and `+0x0C` map each
  piece to its branch chain and to the main-route pieces a branch stands in for; `+0x10` is the
  loop byte. `GrCourseSpline_BuildJunctions` (0x800ded04) joins piece ends closer than 1 unit,
  and `GrCourseSpline_BuildRoutes` (0x800df4ec) lists every route from the start junction back to
  itself. `GrCourseSpline_ResolveCheckpoint` (0x800e0e04) turns a position into lap progress as
  (main-route length before the piece + piece length times the fraction along it) over the
  main-route length, so a lap is the sum of the main route's lengths. A race's default lap count
  is `Stage.dat`'s `stData` row (0x58 bytes, ground kind at +0x00) at +0x34.
- **CPU ranges** (`+0x04`, `{pairs, count, city_param}`). Each 0x18-byte pair is the left and
  right road edge CPUs steer between, two linear splines, then junction ids filled at load, an
  area id at +0x10 and flags at +0x14; the junction picker never routes into a pair with bit 31
  or 30 set. `grRangeSpline_BuildCenterLine` (0x800e1a04) builds each pair's center line; the
  pairs end up at `GrObj+0x11C`. They sit 2 stage units above the floor.
- **Conveyor** (`+0x10`), **rail** (`+0x14`) and **heavy** (`+0x18`), plain spline lists.

`GrData.RailCollNode` (+0x38) lists the grind rails as 0x34-byte entries: a parameter block at
+0x00 (its +0x08 is 3.0 on a grind rail), the linear collision spline at +0x04 that the rail
sweep tests as capsules, the smooth spline at +0x08 that a rider follows, the rail sub-animation
at +0x0C, three next rails at +0x10, the previous at +0x1C, the opposite-direction twin at
+0x28 and the terrain joint at +0x2C. Spline indices name entries of the rail spline list. A
rail on a moving joint carries its points by the joint's change since load. In City Trial rails
0-9 are the station links, one per direction; 10-13 are one-way grind rails, 10 and 11 in the
volcano; 14-20 are the branching river, whose blocks read 11.0 or 7.5 at +0x08 and link to each
other.

## Ground types

A collision triangle's ground type (`GrCollTri.kind` bits 4-11) indexes the ground table in
`GrCommon.dat` (`grDataCommon` +0x00, cached at `r13+0x5e8`): 0x1C-byte rows with the traction
(+0x00, `grGetTractionFromTriangleID`, 0x800cec80), a second factor (+0x04,
`grGetUnkFromTriangleID`, 0x800ceca8), two rough-ground speed multipliers (+0x08, +0x0C; 0.8 and
0.7 on the rough types), the dust effect (+0x10, `grGetDustFromGroundType`, 0x800cec44), the
sound index (+0x14, `grGetUnkFromGroundType`, 0x800cec58) and a machine bob animation (+0x18).
The sound index picks a 0x10-byte row from +0x44 of the machine sound table every class shares
(`vcDataCommon` +0x08): touchdown, ground rumble, rail loop and landing. Those sound names name
the materials:

| Types | Material |
|---|---|
| 0, 1, 2 | dirt, gravel, sand |
| 3, 4 | grass, slick grass |
| 5, 6 | asphalt, grippy asphalt |
| 7, 8 | ice, snow |
| 9, 10 | checkered tile, slick tile |
| 11, 12, 13 | wood, stone, rock |
| 14, 15, 16 | lava, leaves, crystal |
| 17, 18, 19, 20 | metal, flowing water, wire mesh, oil |
| 21, 22, 23, 24 | grippy sand, moving rock (two grips), rock prop walls |
| 25 | Nebula Belt's track |
| 26, 27, 28 | grind rail, rail dash section, water rail |
| 29, 30 | sea, invisible barrier |
| 31-46 | rough off-road versions of a material, one or two per course |

In City Trial the sea covers about 80% of the out-of-bounds box seen from above, the island spans
x -712..536 and z -856..888, and about 30% of the land has another floor under its top surface:
tunnels, the garage, rooms under the forest and the buildings' floors.
