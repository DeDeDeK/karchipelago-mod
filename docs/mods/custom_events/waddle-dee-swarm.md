# Waddle Dee Swarm Event

A custom City Trial event (`CUSTOM_EVKIND_WADDLE_DEE_SWARM`, kind 16) that spawns a rolling swarm
of Waddle Dees (`ENEMYKIND_WADDLE_DEE`, 0x17) around the human players. Each one chases the nearest
rider - CPUs included - fades out on contact, and is replaced for as long as the event lasts. It
runs through the `custom_events` framework with a siren intro, sky preset 5 (Dark Vignette), BGM
file 0x34 (`event_supercharge`), an 1800-frame duration and roll weight 20, and registers `start`,
`active` and `end2`; `start` clears everything a cut-off run leaves behind, so it needs no `abort`.

It is also the reference for driving a vanilla enemy outside its scripted path with custom chase
AI. Two problems it solves - the spline snap and the detection-range cap - apply to any such use of
the enemy actor system.

## The Enemy State Machine

An actor's state indexes two tables. States 0x00-0x0D are common to every enemy type (0x09 death,
0x0A inhaled, 0x0B knockback, 0x0C launched, 0x0D sliding); from 0x0E (`ENEMYSTATE_PERTYPE`) up
they index the type's own table. Waddle Dee's (0x804b3e78) has 0x0E spawn, 0x0F patrol, 0x10 windup,
0x11 lunge and 0x12 recover.

Each per-type state supplies callbacks that run at distinct proc priorities: `anim_cb` (animation
and path, priority 1), `phys_cb` (movement, 4), `envcoll_cb` (ground snap, 5) and `pri6_cb`
(orientation, 6). `EventActor_SharedUpdate` (0x801fd780) then builds the model matrix from `pos`,
`forward`, `up` and `final_scale`. The spawn state's `anim_cb` attaches the actor to a path and
moves it to state 0x0F within priority 1, so a proc at a later priority never sees state 0x0E.

## Spawning

Every 20 frames `active` frees stale slots and spawns one Waddle Dee into the first free slot of
10. It picks a random human player and one of twelve fixed offsets about 40 units out from them,
and passes `EventActor_Create` (0x801fbb50) a zeroed `EventActorDesc` with the kind, that
position, forward (0, 0, 1), up (0, 1, 0), scale 1.0, `spawn_index` and `spawn_slot` -1 and
`leash_radius` -1.0 (no leash). `start` calls `Enemy_CheckAndLoad` so the enemy's archive is
resident. The new actor gets the chase proc at priority 10 (`ENEMYPRI_DMGAPPLY`).

**Pruning.** Most of the ways vanilla destroys an actor - out of bounds, lifetime or forced kill,
the kill floor, death from a common state, a failed knockback landing - never pass through the
death or inhale states the chase proc watches, and the proc dies with its actor. A slot is
therefore kept only while its GObj is still on the `GAMEPLINK_ENEMY` list and is still a Waddle
Dee, which also catches the allocator handing a freed GObj to another actor.

**No null guards needed.** `EventActorGObj_GetAnimRate` (0x802049b8) crashes on a NULL
`parent_gobj` and `splArcLengthPoint` (0x80415958) on a NULL spline, and a standalone spawn carries
both. Neither is reached here: the first runs only in the child-part follow states, which Waddle Dee
never enters, and the spawn state attaches the actor to a real stage spline before anything walks
it. An actor spawned with no path at all would need guards on both.

## The Chase

Each frame the chase proc checks, in order: no slot (leave the actor alone); dying or inhaled
(untrack it and let vanilla finish); event over (untrack and destroy it); a common state below
0x0E (leave it to vanilla). Otherwise it undoes any spline snap, reinstalls the chase callbacks -
vanilla resets them on every state change - and runs the fade or the contact check.

| Slot | Priority | Does |
|---|---|---|
| `phys_cb` | 4 | Velocity toward the nearest rider |
| `envcoll_cb` | 5 | `EventActor_GroundSnap` (0x80204fac) at the type's ground clearance |
| `pri6_cb` | 6 | Faces the target |

**The spline snap.** Entering a per-type state runs its `anim_cb`, and the path-following ones
snap the actor's position to its spline. For a normal Waddle Dee that is invisible; for a chaser it
teleports the actor back onto a stage path mid-chase. The snap happens inside
`EventActor_ChangeState` at priority 1, before the chase proc, which can only undo it: at the end
of each pass it records `pos` and `state`, and on the next pass a changed state means a transition
happened, so `pos` is restored. The first pass restores the spawn position the same way, undoing
the spawn snap. The movement from `phys_cb` and `envcoll_cb` is kept and `anim_cb` is left alone,
so the vanilla animations still play.

**The detection-range cap.** `EventActor_FindNearestPlayer` (0x801ffd78) acquires only targets
within the type's `detect_range` (50.0), far too short for a swarm. The movement callback finds the
nearest rider itself with `Enemy_DistToPlayer` (0x801fffa4) on each rider slot - an empty slot
returns `FLT_MAX` - writes it to `target_ply`, sets `turn_timer` to 0 and `retarget_cooldown` to
2, and only then calls `EventActor_FindNearestPlayer`. The non-zero cooldown makes it keep the
preset target instead of re-evaluating it under the range check, while still computing `turn_to`
(the normalized vector from the target to the enemy). Velocity is `-turn_to` times 0.6, with Y
zeroed because the ground snap owns height.

**Orientation.** `pri6_cb` runs after the ground snap rewrites `up` and before the model matrix is
built, which needs `forward` to be a unit vector perpendicular to `up`. It flattens `-turn_to` onto
the ground plane, removes its component along `up` and normalizes it into `forward`, keeping the old
`forward` when the result is degenerate, then calls `EventActor_UpdateGravity` (0x802054e4), which
refreshes only the gravity fields.

## Contact and the End

Contact is `Enemy_DistToPlayer` to the target falling below 1.0 - a center-to-center distance to
`PlayerData.position`, which follows the rider. Contact starts a 20-frame fade that scales
`final_scale` toward 0 with horizontal velocity frozen, then destroys the actor with
`EventActorGObj_Destroy`.

`end2` only clears the event's active flag. By then vanilla may already have destroyed some tracked
actors, so destroying through stale pointers would be unsafe; instead each chase proc sees the flag
on its next run and destroys its own live actor.
