# Cannon

A custom City Trial event (`CUSTOM_EVKIND_CANNON`, kind 24, "A cannon has appeared!") that puts
Machine Passage's cannon somewhere in the city. A machine that drives into it is loaded, held in
the barrel for a second and fired along the cannon's flight, the same way Machine Passage does it:
the cannon camera, the launch quake, rumble and flash, and the flight itself are all the game's
own. It runs through the `custom_events` framework with a siren intro, sky preset
`CITYSKY_DAY_2`, BGM file 0x34 (`event_supercharge`), an 1800-frame duration and roll weight 20,
and registers `start` and `end2`.

## The vanilla cannon

Machine Passage builds its cannon from four pieces of stage data:

- **A catch zone.** `GrYakuCatchZone_Create` (0x800fa2a0) claims a kind-18 collision zone.
  While a machine is not adhered, `Machine_ProcessEnvColl` writes the zone's target yakumono into
  `MachineData.adhere_yaku` (+0xaf8). That lookup is `mpColl_GetCatchZoneIndex` (0x802471c0) then
  `Gr_GetCatchZoneTarget` (0x800d2790), and for a cannon it also calls `GrYakuCannon_Enqueue`
  (0x800ff220). On the next model-matrix think (priority 6), `Machine_TryEnterAdhereYaku`
  (0x801e5014) enters the class's cannon state, star 0x1a or bike 0x20.
- **The cannon yakumono.** This is kind 48 (`YAKUKIND_CANNON`), created by `GrYakuCannon_Create`
  (0x800fed20) from a parameter block `{YakuCannonDesc *desc, 0, 0, 0, 0, audio_desc}`.
  `YakuCannonDesc` is `{id, joint, wait, cooldown, path_idx, sfx}`. The yakumono keeps a ring of 10
  `{gobj, timer}` entries in its `YakumonoData` tail. Each queued timer starts at `wait`. The
  first one to expire is launched with `MachineGObj_LaunchFromCannon` (0x801c8458), which plays
  `sfx` and starts a path jump, and then `cooldown` frames pass before the next launch. The cannon
  launches a queued machine from wherever that machine is.
- **The barrel joint.** On entry, `Machine_EnterCannonCommon` (0x801e6ed4) snaps the machine to
  `GrYakuCannon_GetPose` (0x800ff3e4): the world position, Z axis (forward) and Y axis (up) of the
  stage joint `desc->joint`.
- **The flight.** `GrData.subanim` (+0x24) slot `cannon` (+0x0c) holds the flight AnimJoints;
  `desc->path_idx` picks one. Each has one node with ROTX, ROTY and PATH tracks. The PATH track
  drives a joint along a spline. `Machine_PathJumpPhys` (0x801e441c) orients the machine from the
  absolute rotation tracks. It moves the machine by the frame-to-frame change of the path
  translate times `grGetStageScale()`, so the movement is relative to where the flight starts. The
  cannon camera, `PlyCam_CannonThink` (0x800caee0), aims at the path position itself, so the
  spline has to be in absolute stage units. When the anim ends the machine flies on.

Machine Passage's `GrCZK_Cannon` zone (kind 22) only feeds the camera. Nothing in the flow checks the
ground kind.

## Rebuilding it from the disc

The cannon has no joint or public of its own. Its base, turret and barrel are seven DObjs on
the terrain joint of `GrMachine2Model.dat`, which at 1.5 MB is too big for City Trial's heap. The
mod ships no geometry. It ships `CannonManifest.dat` (about 1 KB), a recipe that says which byte
ranges of the retail files to read and how to relocate them:

| Offset | Field | Meaning |
|---|---|---|
| 0x00 | `magic`, `version` | `'CNMF'`, 1 |
| 0x08 | `mesh` | `{donor, size, ranges, range_num, relocs, reloc_num}` over `GrMachine2Model.dat` |
| 0x20 | `dobjs`, `dobj_num` | payload offsets of the seven DObjDescs, in draw order |
| 0x28 | `pivot` | the barrel joint's parent, unrotated, in Machine Passage's stage units |
| 0x34 | `anchor_pos`, `anchor_rot` | the barrel joint, local to the pivot |
| 0x4C | `floor` | the mesh's lowest point below the pivot |
| 0x50 | `scale` | Machine Passage's stage scale, 0.92 |
| 0x54 | `path` | the same recipe over `GrMachine2.dat`, holding the flight AnimJoint |
| 0x6C | `path_root` | the AnimJointDesc's payload offset |

Ranges are `{donor_off, dest_off, length}`, all multiples of 32, as `File_Read` requires.
Relocations are `{dest_off, dest_val}`: the runtime writes `base + dest_val` at `dest_off`.

The DObjs index the terrain's shared vertex arrays (about 190 KB), but only a narrow slice of
each, so only those slices are read, about 25 KB. Each array pointer is relocated to "slice start
minus the bytes left out", which can wrap below the payload. The display lists' unchanged indices
then land on the same elements. The whole mesh payload is 76 KB; the flight is 416 bytes.

At every City Trial load with events on, `On3DLoadEnd` reads both payloads into the scene heap,
relinks the DObjs into one chain, and keeps a copy of the flight's spline. A missing file leaves
the event placing nothing and logs once.

## Placing it

`start` picks one entry of a short list. Each entry is `{x, z, heading, range}` in world units,
with heading in degrees from +Z. A ground raycast finds the height, and the pivot goes `floor`
times the 0.92 scale above it.

The mesh hangs off a mod joint rotated by `psi`, the gap between the entry's heading and Machine
Passage's flight heading, and scaled by 0.92. Machines are the same size on every stage, so this
draws the cannon at the size it has in Machine Passage. The barrel pose is the pivot transform
times the barrel joint's local transform.

The flight is re-posed in place. Each control point is turned by `psi` about the barrel joint,
and its offset from the joint is scaled by `f = 0.92 / grGetStageScale() * range`. The result is
placed after the barrel mouth in City Trial's stage units (world / 0.7). Under a rigid motion and
a uniform scale, the spline's normalized arc-length table stays valid: `total_length` grows by
`f` and every `seg_poly` coefficient (|dP/du|^2) grows by `f^2`. The ROTY track is absolute and
fixed-point, and cannot hold an arbitrary heading. It is rewritten as three float LIN keys: the
barrel's yaw at frame 0, then the flight heading from frame 30 to the end. ROTX and PATH stay as
Machine Passage authored them.

`start` then points `GrData.subanim->cannon`, NULL in City Trial, at a one-entry `GrSubAnim`
holding the flight. The cannon reads it on every launch.

The parameter block needs a `YakumonoTable.data_array` slot only while the yakumono is created,
and City Trial has none free: `fn_grSetupCityEventData` (0x8010f7c4) fills slots 31 and 32 with
Rail Fire's and the Secret Chamber's blocks. `GrYaku_Create` reads its slot once and keeps the
pointer, so `start` lends itself slot 31 for `GrYaku_Create(YAKUKIND_CANNON, 31)` and puts Rail
Fire's pointer back straight after, before `GrYakuCannon_TailInit`. The block has no audio, a
wait of 60 frames, a cooldown of 4 and sfx 0x120017 (`SFX_map_city_sjump_l`). Machine Passage's
own shot is in its stage bank, which City Trial does not load. The mesh GObj goes on
`GAMEGX_MAP` at priority 2, after the map GObj (priority 1). `Map_GX` (0x800d81e4) draws City
Trial's backdrop with depth compare and depth update off, then the sky tint quad, so anything
drawn earlier on the link gets painted over wherever the sky is behind it.

**The pose hook.** `GrYakuCannon_GetPose` is replaced. For the mod's cannon it returns the barrel
mouth, its forward and up; for any other cannon it runs the vanilla body
(`Gr_GetNodeWorldPos`, `Gr_GetNodeWorldAxisZ`, `Gr_GetNodeWorldAxisY` of `desc->joint`). This is
the only code patch.

## Catching machines

City Trial has no catch zone, so the mesh GObj's proc does the zone's job at priority 0. That
runs before `Machine_ProcessEnvColl` (5) and the model-matrix think (6), and below priority 8,
where `Machine_SetAirborne` would skip a matrix copy. The proc checks every ridden machine that is
not adhered. If the machine is inside a cylinder around the pivot (radius 22, from 5 below the
base to 35 above), the proc writes `adhere_yaku` and calls `Machine_TryEnterAdhereYaku`. Only a
machine that entered is passed to `GrYakuCannon_Enqueue`, since the cannon launches every queued
machine wherever it is; a refused machine gets `adhere_yaku` cleared. A caught player is ignored
for 120 frames, so the cannon doesn't catch it again on the way out. CPUs are caught only if they
happen to drive in; nothing steers them toward the cannon.

## Ending

`end2` stops the catching. The cannon stays until its queue is empty and no player is in a cannon
(`Ply_IsInCannon`), then the proc destroys the yakumono and itself and clears the flight slot.
If the event starts again while the old cannon is still up, catching resumes at the same spot.

At every 3D exit the flight slot goes back to NULL whatever state the event is in, so it never
outlives the scene heap its target lives in.
