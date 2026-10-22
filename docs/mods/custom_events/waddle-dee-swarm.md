# Waddle Dee Swarm Event

A custom City Trial event (`CUSTOM_EVKIND_WADDLE_DEE_SWARM`, kind 16) that spawns a rolling swarm
of Waddle Dees (`ENEMYKIND_WADDLE_DEE`, 0x17) around the human players. Each one runs down the
nearest rider - CPUs included - at that rider's top speed, hits on any contact and hops at its
target when close. A Dee that lands a hit fades out, and the swarm refills for as long as the
event lasts. It runs through the `custom_events` framework with a siren intro, sky preset 5 (Dark
Vignette), BGM file 0x34 (`event_supercharge`), an 1800-frame duration and roll weight 20, and
registers `start`, `active` and `end2`; `start` clears everything a cut-off run leaves behind, so
it needs no `abort`.

It is also the reference for driving a vanilla enemy outside its scripted path with custom chase
AI. The problems it solves - vanilla callbacks coming back on every state change, the spline
snap, the detection-range cap and retuning the enemy's hitbox - apply to any such use of the enemy
actor system.

## The Enemy State Machine

An actor's state indexes two tables. States 0x00-0x0D are common to every enemy type (0x09 death,
0x0A inhaled, 0x0B knockback, 0x0C launched, 0x0D sliding); from 0x0E (`ENEMYSTATE_PERTYPE`) up
they index the type's own table. Waddle Dee's (0x804b3e78) has 0x0E spawn, 0x0F patrol, 0x10
surprised hop, 0x11 sideways dash and 0x12 turn back.

`EventActor_ChangeState` (0x801fc398) installs the new state's callbacks, each run by one of the
actor's procs - `anim_cb` (priority 1, after the animation and its script advance), `phys_cb`
(4), `envcoll_cb` (5) and `pri6_cb` (6) - and clears `trigger_cb` (7). It also starts the new
animation's script at once, and every Waddle Dee per-type script arms its hitbox.
`EventActor_SharedUpdate` (0x801fd780) builds the model matrix at priority 6 from `pos`,
`forward`, `up` and `final_scale`.

Vanilla changes a Dee's state in three places the chase has to live with:

- **Spawn.** On the first frame the spawn state's `anim_cb`, `WaddleDee_SpawnAnim` (0x80219638),
  starts the fade-in, snaps the actor onto the nearest stage spline with
  `EventActor_PathFollowUpdate` (0x80209ce4) and enters 0x0F.
- **Fade-in.** About ten frames later, at priority 0, `EventActorGObj_ProcFadeIn` (0x801ff520)
  finds alpha at 1.0, rebuilds the model and the HurtData and re-enters the current state, which
  installs that state's vanilla callbacks again.
- **Knockback.** A hit on the Dee knocks it into the common states at priority 10, and they end
  in its death.

## Spawning

Every 20 frames `active` frees stale slots and spawns one Waddle Dee into the first free slot of
10. It picks a random human player and one of twelve fixed offsets about 40 units out from them,
and passes `EventActor_Create` (0x801fbb50) a zeroed `EventActorDesc` with the kind, that
position, forward (0, 0, 1), up (0, 1, 0), scale 1.0, `spawn_index` and `spawn_slot` -1 and
`leash_radius` -1.0 (no leash). `start` calls `Enemy_CheckAndLoad` so the enemy's archive is
resident. The new actor gets the chase proc at priority 2.

**Pruning.** Most of the ways vanilla destroys an actor - out of bounds, lifetime or forced kill,
the kill floor, death from a common state, a failed knockback landing - never pass through the
death or inhale states the chase proc watches, and the proc dies with its actor. A slot is
therefore kept only while its GObj is still on the `GAMEPLINK_ENEMY` list and is still a Waddle
Dee, which also catches the allocator handing a freed GObj to another actor.

**No null guards needed.** `EventActorGObj_GetAnimRate` (0x802049b8) crashes on a NULL
`parent_gobj` and `splArcLengthPoint` (0x80415958) on a NULL spline, and a standalone spawn carries
both. Neither is reached here: the first runs only in the child-part follow states, which Waddle Dee
never enters, and `EventActor_Create` attaches an actor with no leash to the nearest stage spline
(`EventActor_PathInit`, 0x80206e2c) before anything walks it. An actor spawned with no path at
all would need guards on both.

## The Chase Proc

The chase proc runs at priority 2, right after the state machine. The spawn and fade-in changes
come before it in the frame, so it follows each one before any callback runs: vanilla's per-type
callbacks never get a frame, and the hit passes at priority 9 always see the retuned hitbox.

Each frame it checks, in order: no slot (leave the actor alone); dying or inhaled (untrack it and
let vanilla finish); event over (untrack and destroy it); a state below 0x0F, the spawn or a
common one (leave it to vanilla). Its first pass past those puts the actor back at its spawn
position, undoing the spawn's spline snap. Then it runs the fade, the hit check or the hop,
retunes the hitbox and reinstalls the callbacks:

| Slot | Priority | Does |
|---|---|---|
| `anim_cb` | 1 | Cleared, so no vanilla transition runs |
| `phys_cb` | 4 | Velocity toward the nearest rider |
| `envcoll_cb` | 5 | `EventActor_GroundSnap` (0x80204fac) at the type's ground clearance |
| `pri6_cb` | 6 | Faces the target |

Clearing `anim_cb` stops vanilla's transitions, not the animations, which advance in the
priority-1 proc itself. The chase's own transitions call `EventActor_ChangeState` directly, which
does not snap.

**Targeting.** `EventActor_FindNearestPlayer` (0x801ffd78) acquires only riders within the type's
`detect_range` (50.0), far too short for a swarm. The movement callback picks the nearest rider
itself, with `Enemy_DistToPlayer` (0x801fffa4) on each slot - an empty slot returns `FLT_MAX` -
and writes it to `target_ply`, and the flattened direction from it to the actor into `turn_to`,
the field vanilla's targeting fills.

**Speed.** Velocity is `-turn_to` times the target's top speed, with Y zeroed because the ground
snap owns height. A mounted target's top speed is its machine's `top_speed_ground`, the
stat-scaled cruise cap; a target on foot uses `RiderData.walk_speed_max`. A rider holding top speed
in a straight line is never caught; one who turns, slows or stops is.

**Orientation.** `pri6_cb` runs after the ground snap rewrites `up` and before the model matrix is
built, which needs `forward` to be a unit vector perpendicular to `up`. It removes the component
of `-turn_to` along `up` and normalizes it into `forward`, keeping the old `forward` when the
result is degenerate, then calls `EventActor_UpdateGravity` (0x802054e4), which refreshes only the
gravity fields.

## The Hop

Within 15 units of its target, a walking chaser enters 0x10, vanilla's surprised hop: 27 frames,
crouching until frame 4, airborne until 25. Its vanilla `anim_cb` would move on to the sideways
dash when the animation ends, so the chase proc counts the frames itself and returns the actor to
0x0F after 27. While airborne the actor moves at 1.5 times the target's top speed, still homing;
through the crouch and the landing it stands still.

## The Hitbox

Waddle Dee's animation scripts arm one attack region in every per-type state: 3 damage, 25
knockback, radius 2.0, rehitting a victim every 30 frames. Every frame the chase proc sets its
`base_damage` to 8 and `base_knockback` to 60. Nothing arms the region later in the frame, so the
retuned values are what the hit passes read, walking or hopping.

A Dee is spent once it lands a hit. A region lists each victim's HurtData for its rehit interval,
and the chase proc checks the list with `Hit_IsVictimRecorded` (0x8018a408) against each rider's
HurtData, or their machine's while they ride - a mounted rider is hit only through the machine.
The list records a victim even when the hit did no damage, so touching an invulnerable rider
spends a Dee too. A spent Dee switches its hitbox off and starts a 20-frame fade that scales
`final_scale` toward 0 while the movement callback holds it still, then destroys it with
`EventActorGObj_Destroy`.

## The End

`end2` only clears the event's active flag. By then vanilla may already have destroyed some tracked
actors, so destroying through stale pointers would be unsafe; instead each chase proc sees the flag
on its next run and destroys its own live actor.
