# Waddle Dee Swarm Event

Custom City Trial event (`event_waddle_dee_swarm.c`, `CUSTOM_EVKIND_WADDLE_DEE_SWARM`). Spawns a rolling swarm of Waddle Dees (enemy actor `ACTORID_WADDLE_DEE`, 0x17) that chase the nearest human player, fading out on contact and being replenished up to a cap for the event's duration. Currently **dormant** along with the rest of the custom-events framework.

This is the reference implementation for **standalone enemy actors with custom chase AI** — spawning a vanilla enemy outside its scripted path and steering it manually. The two non-obvious problems it solves (the spline snap and the detection-range cap) apply to any custom use of the enemy-actor system.

## Waddle Dee state machine

Actors start at state 0 (memset). The animation bytecode drives transitions through the common states (0x00–0x0D) and into the per-type states. The 0x0E→0x0F transition completes atomically within priority 1 (`ProcUpdate`), so a priority-10 proc never observes state 0x0E.

Per-type states for Waddle Dee (actor 0x17):

| State | Meaning |
|-------|---------|
| 0x0E | Idle. `func1` calls `SetVisibility`, re-inits the path (**snaps position to the nearest spline**), then immediately transitions to 0x0F. |
| 0x0F | Walk A — ground-path walk + orientation. |
| 0x10 | Walk B variant. |
| 0x11 | Turn / walk left. |
| 0x12 | Turn / walk right. |

Each per-type state dispatches four callbacks at distinct priorities: `func1` (animation/path), `func2` (movement, priority 4), `func3` (ground snap, priority 5), `func4` (orientation, priority 6), followed by `EventActor_SharedUpdate` (model matrix).

## The spline-snap problem

Every vanilla state transition (0x0F→0x10→0x11→0x12→…) re-runs `func1`, which **snaps the actor's position to the nearest spline point**. For a normally-spawned, path-following Waddle Dee that's invisible; for a standalone swarm member it teleports the actor back onto a stage path mid-chase.

The snap happens inside `EnemyStateChange` → the new `func1`, all within **priority 1, before** the priority-10 chase proc runs — so the proc cannot prevent it, only undo it. The fix is a per-frame restore:

- Every frame, the chase proc (`WaddleDeeChaseProc`, priority 10) records each actor's `pos` and `state` into `saved_pos[]` / `saved_state[]` at the end of the frame.
- On the next frame, if `ed->state != saved_state[slot]`, a vanilla transition (and therefore a snap) occurred — restore `ed->pos = saved_pos[slot]` (the last known good position), then re-snapshot.
- The very first frame past init (state reaches 0x0E, `chase_active` still 0) does the same restore against `saved_pos` seeded from the spawn descriptor's `position`, undoing the initial 0x0E snap.

This preserves the chase movement applied by `func2`/`func3` while erasing the snap. `func1` is left untouched, so the vanilla walk animations keep playing.

## Chase override

The proc reinstalls three callbacks every frame (they're reset by vanilla on each state change):

| Slot | Custom function | Priority | Role |
|------|-----------------|----------|------|
| `state_func2` | `WaddleDeeChaseMovement` | 4 | Velocity toward the nearest player |
| `state_func3` | `WaddleDeeChaseGroundSnap` | 5 | Snap Y to ground (`EventActor_GroundSnap`, mirrors vanilla walk state 0x80219A48) |
| `state_func4` | `WaddleDeeChaseOrientation` | 6 | Face the target |

Ordering matters: `func4` runs **after** `func3` (which modifies `up` and re-orthogonalizes `forward`) but **before** `EventActor_SharedUpdate` computes the model matrix.

### Bypassing the detection-range cap

`EnemyActor_FindNearestPlayer` only acquires targets within a maximum detection range — too short for a swarm meant to hunt across the map. `WaddleDeeChaseMovement` works around it: it computes the nearest player itself (no range limit) via `EnemyActor_DistToPlayer`, writes the result into `ed->target_player_idx`, then sets `ed->chase_flag = 0` and `ed->retarget_cooldown = 2` **before** calling `EnemyActor_FindNearestPlayer`. The non-zero cooldown makes the vanilla function keep the pre-set target (and compute `chase_direction`/orientation from it) instead of re-evaluating with its range check. Velocity is then `-chase_direction * WADDLE_DEE_CHASE_SPEED` (negated because `chase_direction` points enemy→away-from-player); `vel.Y` is zeroed so gravity doesn't accumulate (ground snap owns Y).

## Spawning

`WaddleDeeSwarm_Active` calls `WaddleDeeSpawnOne` every `WADDLE_DEE_SPAWN_INTERVAL` (20) frames, up to `WADDLE_DEE_MAX_COUNT` (10) live actors. Each spawn picks a random human player and places the actor at one of twelve fixed ~40-unit offsets around them via a zero-initialized `EventActorDesc` (`actor_id = ACTORID_WADDLE_DEE`, `spawn_index`/`spawn_slot = -1`, `bounds_flag = -1.0`). `Enemy_CheckAndLoad(ACTORID_WADDLE_DEE)` in `WaddleDeeSwarm_Start` ensures the archive is resident first. The new GObj gets `WaddleDeeChaseProc` attached at priority 10, with `saved_pos` seeded from the descriptor and `chase_active = 0`.

## Despawn and fade-out

On contact (distance to target < `WADDLE_DEE_HIT_RADIUS` = 1.0) the proc starts a fade: `fade_timer` counts down from `WADDLE_DEE_FADE_FRAMES` (20), shrinking `ed->final_scale` toward 0 and freezing velocity, then untracks and `EventActor_Destroy`s the actor at 0. Actors that vanilla inhales/kills (state 0x09/0x0A) are simply untracked.

## Lifecycle

| Function | Role |
|----------|------|
| `WaddleDeeSwarm_Start` | Load archive, reset `spawn_timer`, set `swarm_active = 1`. |
| `WaddleDeeSwarm_Active` | Periodic replenishment spawn. |
| `WaddleDeeSwarm_End2` | Set `swarm_active = 0` and return. |

**`End2` deliberately does not destroy the actors itself.** By event-end, some tracked GObj pointers may be stale (vanilla can have already destroyed an actor via an out-of-bounds kill, inhale, etc.). Clearing `swarm_active` instead lets each `WaddleDeeChaseProc` observe the flag on its next frame and self-destruct safely against a live `gobj`.

## State module-globals

All per-actor state is in fixed-size parallel arrays indexed by swarm slot (`WaddleDeeFindSlot` maps a GObj back to its slot): `swarm_gobjs[]`, `saved_pos[]`, `saved_state[]`, `chase_active[]`, `fade_timer[]`, `fade_scale0[]`, plus the module-level `spawn_timer` and `swarm_active`.
