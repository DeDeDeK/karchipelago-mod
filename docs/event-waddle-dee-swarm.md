# Waddle Dee Swarm Event

A custom City Trial event (`CUSTOM_EVKIND_WADDLE_DEE_SWARM`, kind 16) that spawns a rolling swarm of Waddle Dees (enemy actor `ACTORID_WADDLE_DEE`, 0x17) which chase the nearest human player, fade out on contact, and are replenished up to a cap for the event's duration. Implemented in `mods/custom_events/src/event_waddle_dee_swarm.c`.

Like every custom event it is a row in `custom_params[]` and `custom_functions[]` in `custom_events.c`; the mod's wrappers on the event state table call the kind's callbacks in place of the vanilla per-kind dispatch. Its parameters: 1800-frame duration (~30 s), siren intro, sky preset 5 (Dark Vignette), BGM file 0x34 (`event_supercharge`), roll weight 20.

It is the reference implementation for **standalone enemy actors with custom chase AI** - spawning a vanilla enemy outside its scripted path and steering it manually. The two non-obvious problems it solves, the spline snap and the detection-range cap, apply to any custom use of the enemy-actor system.

**Why this event needs no null guards.** Two engine functions crash on the NULL pointers a standalone spawn can carry: `EventActor_GetParentAnimRate` (0x802049b8) on a NULL `parent_gobj`, and `splArcLengthPoint` (0x80415958) on a NULL spline. This event reaches neither. It never sets `parent_gobj`, but that function is only called from the child-actor follow-parent states, which Waddle Dee never enters; and it lets vanilla's state 0x0E `func1` run `EnemyPath_Init`, so the actor holds a real stage spline before anything walks it - the event only undoes the resulting position snap. An actor spawned with no path at all would need guards on both.

## Waddle Dee State Machine

Actors start at state 0 (memset). The animation bytecode drives transitions through the common states (0x00-0x0D) and into the per-type states. The 0x0E -> 0x0F transition completes atomically within priority 1 (`ProcUpdate`), so a priority-10 proc never observes state 0x0E.

Per-type states for actor 0x17: 0x0E is idle - its `func1` calls `SetVisibility`, re-inits the path (**snapping position to the nearest spline**) and immediately transitions to 0x0F; 0x0F and 0x10 are the two ground-path walk variants; 0x11 and 0x12 are turn/walk left and right. Each per-type state dispatches four callbacks at distinct priorities - `func1` (animation/path), `func2` (movement, priority 4), `func3` (ground snap, priority 5), `func4` (orientation, priority 6) - followed by `EventActor_SharedUpdate` (0x801fd780, model matrix). States 0x09 and 0x0A are the common inhaled/dying states.

## The Spline-Snap Problem

Every vanilla state transition (0x0F -> 0x10 -> 0x11 -> 0x12 -> ...) re-runs `func1`, which **snaps the actor's position to the nearest spline point**. For a normally spawned, path-following Waddle Dee that is invisible; for a standalone swarm member it teleports the actor back onto a stage path mid-chase.

The snap happens inside `EnemyStateChange` -> the new `func1`, all within **priority 1, before** the priority-10 chase proc runs - so the proc cannot prevent it, only undo it. The fix is a per-frame restore:

- At the end of each frame, `WaddleDeeChaseProc` (priority 10) records each actor's `pos` and `state` into `saved_pos[]` / `saved_state[]`.
- Next frame, `ed->state != saved_state[slot]` means a vanilla transition (and therefore a snap) happened, so `ed->pos` is restored from `saved_pos[slot]` - the last known good position - and re-snapshotted.
- The first frame past init (state has reached 0x0E, `chase_active` still 0) does the same restore against `saved_pos` seeded from the spawn descriptor's `position`, undoing the initial 0x0E snap.

This preserves the chase movement applied by `func2`/`func3` while erasing the snap. `func1` is left untouched, so the vanilla walk animations keep playing.

## Chase Override

The proc reinstalls three callbacks every frame, because vanilla resets them on each state change:

| Slot | Custom function | Priority | Role |
|------|-----------------|----------|------|
| `state_func2` | `WaddleDeeChaseMovement` | 4 | Velocity toward the nearest player |
| `state_func3` | `WaddleDeeChaseGroundSnap` | 5 | Snap Y to ground (`EventActor_GroundSnap` 0x80204fac scaled by `param_move_speed`, mirroring the vanilla walk state's func3 at 0x80219a48) |
| `state_func4` | `WaddleDeeChaseOrientation` | 6 | Face the target |

Ordering matters: `func4` runs **after** `func3` (which rewrites `up` and re-orthogonalizes `forward`) but **before** `EventActor_SharedUpdate` builds the model matrix.

### Bypassing the detection-range cap

`EnemyActor_FindNearestPlayer` (0x801ffd78) only acquires targets within a global detection range (50.0) - far too short for a swarm meant to hunt across the map. `WaddleDeeChaseMovement` works around it: it computes the nearest player itself with no range limit (`EnemyActor_DistToPlayer` 0x801fffa4 over the 4 rider slots), writes the result into `ed->target_player_idx`, then sets `ed->chase_flag = 0` and `ed->retarget_cooldown = 2` **before** calling `EnemyActor_FindNearestPlayer`. The non-zero cooldown makes the vanilla function keep the pre-set target - and still compute `chase_direction` and orientation from it - instead of re-evaluating with its range check.

Velocity is then `-chase_direction * WADDLE_DEE_CHASE_SPEED` (0.6), negated because `chase_direction` points enemy -> away from player. `vel.Y` is zeroed so gravity does not accumulate; the ground snap owns Y.

## Spawning, Fade-Out and Lifecycle

`WaddleDeeSwarm_Start` calls `Enemy_CheckAndLoad(ACTORID_WADDLE_DEE)` so the archive is resident, resets `spawn_timer` and sets `swarm_active = 1`. `WaddleDeeSwarm_Active` calls `WaddleDeeSpawnOne` every `WADDLE_DEE_SPAWN_INTERVAL` (20) frames, which fills the first free slot of `WADDLE_DEE_MAX_COUNT` (10) - so deaths are replenished for the whole event. Each spawn picks a random human player and places the actor at one of twelve fixed ~40-unit XZ offsets around them, through a zero-initialized `EventActorDesc` (`actor_id = ACTORID_WADDLE_DEE`, `forward = {0,0,1}`, `up = {0,1,0}`, `scale = 1.0`, `spawn_index`/`spawn_slot = -1`, `bounds_flag = -1.0`) passed to `EventActor_Create` (0x801fbb50). The new GObj gets `WaddleDeeChaseProc` at priority 10, `saved_pos` seeded from the descriptor and `chase_active = 0`.

On contact (distance to target below `WADDLE_DEE_HIT_RADIUS` = 1.0) the proc starts a fade: `fade_timer` counts down from `WADDLE_DEE_FADE_FRAMES` (20), scaling `ed->final_scale` toward 0 and freezing XZ velocity, then untracks and `EventActor_Destroy`s the actor at 0. Actors that vanilla inhales or kills (state 0x09/0x0A) are just untracked, leaving vanilla to dispose of them.

**`WaddleDeeSwarm_End2` deliberately does not destroy the actors itself** - it only clears `swarm_active`. By event end some tracked GObj pointers can be stale, since vanilla may already have destroyed an actor through an out-of-bounds kill or inhale; clearing the flag lets each `WaddleDeeChaseProc` see it on its next frame and self-destruct against a live `gobj`.

All per-actor state lives in fixed-size parallel arrays indexed by swarm slot, with `WaddleDeeFindSlot` mapping a GObj back to its slot: `swarm_gobjs[]`, `saved_pos[]`, `saved_state[]`, `chase_active[]`, `fade_timer[]`, `fade_scale0[]`, plus module-level `spawn_timer` and `swarm_active`.
