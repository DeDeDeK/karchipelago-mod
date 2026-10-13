# Falling and Respawning

How a machine that leaves the course dies and comes back: the checkpoint every machine keeps,
the two triggers that declare a fall death, and the per-player timer that respawns it. A mod
that kills a rider on purpose, moves where riders come back, or reacts to a fall needs to reuse
this path rather than invent one.

## Checkpoints

A machine's checkpoint is not a world position. `MachineData.respawn_pos` (+0x8a8) holds three
spline parameters: a course-spline segment index (an integer stored raw in a float field), the
normalized progress along that segment, and a height offset.

`Machine_InitCheckpoint` (0x801ce364) seeds it to `{0xFFFFFFFF, 0, 0}` at machine creation and
runs one lookup. `Machine_UpdateCheckpoint` (0x801ce268) then runs every frame while the machine
is not fall-dead, on stages whose `StageNode` flags byte is set. It copies `respawn_pos` into
`prev_respawn_pos` (+0x8b4), reads the machine's mpColl position (`Machine_GetMpCollPosition`,
0x801ce954) and resolves it to the nearest course-spline point (`Machine_CheckpointLookup`,
0x800cef84, into `GrCourseSpline_ResolveCheckpoint`, 0x800e0e04). On success it stores the
result and clears `use_backup_checkpoint` (`+0xc37` bit 0x40). On the first failure it saves the
previous checkpoint into `backup_respawn_pos` (+0x8c0) and sets the flag, so a machine that
leaves the spline's reach keeps its last good point.

The lookup is by proximity, so near crossings, overlaps and the start line the nearest point can
be off the player's real progress, and on courses with sparse splines a respawn can land
noticeably behind where the rider fell.

## Fall death

`Machine_CheckFallDeath` (0x801e6464), called each frame from the machine's env-collision proc,
declares a fall in two ways:

- **Local dead zone** - the machine is inside a collision zone of kind 25 (`GrCZK_LocalDead`):
  `mpColl_GetDeadZoneIndex` (0x80247fac) returns its index, and `Gr_IsGroundHandleInvalid`
  (0x800d1f3c, 0 for an in-range index) accepts it. The zone index becomes the fall's ground
  handle.
- **Out of bounds** - `calcDistanceFromOOB` (0x800d4f20) is negative, the machine having left
  the stage's bounding box. The ground handle is then -1, and the checkpoint is `respawn_pos`,
  or `backup_respawn_pos` while the backup flag is set.

Either way it calls `Machine_SetFallDead(md, ground_handle, checkpoint)` (0x801e6520). For an
unridden machine that just destroys the GObj. Otherwise it stores the handle
(`fall_ground_handle`, +0x1b48), the three checkpoint floats (`respawn_spline_params`, +0x1b4c)
and `fall_respawn_timer` (+0x1b58, 300 from `VcCommon.dat`), runs the machine class's
`enter_fall_dead`, sets `is_fall_dead` (`+0xc35`), trims the velocity, and sets `xc3a` bit 0 and
`xc39` bit 0x40. A mod can call it directly with a checkpoint of its own choosing - the
out-of-bounds form, handle -1 with the machine's current checkpoint, respawns where a natural
fall at that spot would.

## Respawn

Two clocks run from there.

`Respawner_Update` (0x8000ff78) keeps a per-player timer. When it finds a player fall-dead
(`Ply_CheckIfFallDead`, 0x8022cc30) it starts the timer at 150 frames: at 90 it opens a camera
fade; at 30 it either marks the player permanently dead (on foot outside the Destruction Derby,
or with permadeath enabled) or sets the respawn request with `Ply_SetRespawnRequest`
(0x8022cc80), in the Derby also setting the player's HP field to 1.0; at 0 it cleans up the
fade.

Meanwhile the machine's fall-dead state runs `Machine_UpdateFallDead` (0x801e6670) each frame.
Its `Machine_CheckRespawnRequest` (0x801e66cc) waits for the request; when it arrives,
`Machine_RespawnFromFallDead` (0x801e6718) places the machine at the local dead zone's respawn
point (`grGetLocalDeadZoneParam`) when `fall_ground_handle` is a valid zone, else at the
position the spline parameters resolve to, clears the request, and resets motion and charge. A
normal fall therefore respawns about 120 frames in. Without a request, `fall_respawn_timer`
counts down and at 0 only stops the machine (`Machine_ResetMotion`, 0x801c8e50).

On the rider side, `Rider_DeadHitGround_Anim` (0x801a53d4) enters `RiderState_RespawnEnter`
(0x801a1d70), and `Rider_RespawnAnim` (0x801a1dec) puts the rider back on its machine through
`Rider_RespawnReposition`.

## Rebuilding a machine

Swapping a rider's machine outright - the Wing ability's star removal, the legendary assembly -
goes through `Rider_RespawnFullRecreate` (0x80193900) instead. It serializes the old machine
into a respawn packet (`Machine_BuildRespawnPacket`, 0x801cbe5c: velocity, a type at +0x10, the
mpColl position and, for a fall-dead machine, its stored fall block), destroys it, creates the
new one with `Machine_Create`, and restores the packet with `Machine_ApplyRespawnPacket`
(0x801cc0c4). That hands the packet to the machine class's callback, which for
`Machine_RespawnDispatch` (0x801eb738) switches on the type: 1-3 each have their own entry, 4
(fall dead) goes to `Machine_FallDeadRespawnEntry` (0x801e4ec4), and anything else restores the
default position.
