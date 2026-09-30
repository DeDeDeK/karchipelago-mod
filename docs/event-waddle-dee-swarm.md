# Waddle Dee Swarm Event

A custom City Trial event (`CUSTOM_EVKIND_WADDLE_DEE_SWARM`, kind 16) that spawns a rolling swarm of Waddle Dees (enemy actor `ENEMYKIND_WADDLE_DEE`, 0x17) around human players. Each Waddle Dee chases the nearest rider (CPUs included), fades out on contact, and is replaced for as long as the event lasts. Implemented in `mods/custom_events/src/event_waddle_dee_swarm.c`.

Like every custom event, it is a row in `events[]` in `custom_events.c`; the mod's wrappers on the event state table call the kind's callbacks in place of the vanilla per-kind dispatch.

**Parameters:**
- 1800-frame duration (~30 s), with a siren intro
- sky preset 5 (Dark Vignette)
- BGM file 0x34 (`event_supercharge`)
- roll weight 20

It registers `start`, `active` and `end2`. It needs no `abort`: `start` clears every slot a cut-off swarm left behind.

It is the reference implementation for **standalone enemy actors with custom chase AI**: spawning a vanilla enemy outside its scripted path and steering it manually. The two non-obvious problems it solves, the spline snap and the detection-range cap, apply to any custom use of the enemy-actor system.

## Why This Event Needs No Null Guards

Two engine functions crash on NULL pointers that a standalone spawn can carry:
- `EventActorGObj_GetAnimRate` (0x802049b8) on a NULL `parent_gobj`;
- `splArcLengthPoint` (0x80415958) on a NULL spline.

This event reaches neither:
- **`parent_gobj`**: never set, but that function is only called from the child-part follow-parent states, which Waddle Dee never enters.
- **Spline**: vanilla's spawn state 0x0E is allowed to attach the actor to a real stage spline before anything walks it; the event only undoes the resulting position snap.

An actor spawned with no path at all would need guards on both.

## Waddle Dee State Machine

Actors start at state 0 (memset).

**Common states 0x00-0x0D** are shared by every actor type (`ENEMYSTATE_*` in `enemy.h`):

| State | Name |
|-------|------|
| 0x09 | death |
| 0x0A | inhaled |
| 0x0B | knockback |
| 0x0C | launched |
| 0x0D | sliding |

**States 0x0E and up** index the type's own table. Waddle Dee's (table 0x804b3e78):

| State | Name |
|-------|------|
| 0x0E | spawn |
| 0x0F | patrol |
| 0x10 | windup |
| 0x11 | lunge |
| 0x12 | recover |

The spawn state's `anim_cb` attaches the actor to its path and moves it on to 0x0F, all within priority 1 (`EventActorGObj_ProcAnim`, `ENEMYPRI_ANIM`). A priority-10 proc therefore never observes state 0x0E.

Each per-type state dispatches four callbacks at distinct priorities, followed by `EventActor_SharedUpdate` (0x801fd780), which builds the model matrix from `pos`, `forward`, `up` and `final_scale`:
- `anim_cb`: animation/path, priority 1
- `phys_cb`: movement, priority 4
- `envcoll_cb`: ground snap, priority 5
- `pri6_cb`: orientation, priority 6

## The Spline-Snap Problem

Entering a per-type state runs its `anim_cb`, and the path-following ones **snap the actor's position to its spline** (spawn and recover run `EventActor_PathFollowUpdate`). For a normally spawned Waddle Dee that is invisible. For a swarm member it teleports the actor back onto a stage path mid-chase.

The snap happens inside `EventActor_ChangeState` -> the new `anim_cb`, within **priority 1, before** the priority-10 chase proc runs. The proc cannot prevent it, only undo it, using a per-frame restore:

1. At the end of each chase frame, `WaddleDeeChaseProc` records the actor's `pos` and `state` in its `SwarmSlot` (`saved_pos` / `saved_state`).
2. On the next frame, `state != saved_state` means a transition (and possibly a snap) happened, so `pos` is restored from `saved_pos`.
3. The first chase frame (`chase_active` still 0) does the same restore against `saved_pos` seeded from the spawn descriptor's position, which undoes the spawn snap.

This keeps the chase movement applied by `phys_cb`/`envcoll_cb` while erasing the snap. `anim_cb` is left untouched, so the vanilla animations keep playing.

## Chase Override

The proc reinstalls three callbacks every frame, because vanilla resets them on each state change:

| Slot | Custom function | Priority | Role |
|------|-----------------|----------|------|
| `phys_cb` | `WaddleDeeChaseMovement` | 4 | Velocity toward the nearest rider |
| `envcoll_cb` | `WaddleDeeChaseGroundSnap` | 5 | Snap to the ground (`EventActor_GroundSnap` 0x80204fac, `param_ground_clearance` above it) |
| `pri6_cb` | `WaddleDeeChaseOrientation` | 6 | Face the target |

Ordering matters: `pri6_cb` runs **after** `envcoll_cb`, whose ground snap rewrites `up`, but **before** `EventActor_SharedUpdate` builds the model matrix from `forward` and `up`. The matrix needs `forward` to be a unit vector perpendicular to `up`, so `WaddleDeeChaseOrientation` builds it itself: it takes `-turn_to` flattened to XZ, removes its component along `up`, and normalizes it into `forward` (keeping the old `forward` when the result is degenerate). It then calls `EventActor_UpdateGravity` (0x802054e4), which only refreshes `gravity_strength` and `gravity_dir` and leaves `forward` and `up` alone.

### Bypassing the detection-range cap

`EventActor_FindNearestPlayer` (0x801ffd78) only acquires targets within `EnemyParamTable.detect_range` (50.0). That is far too short for a swarm meant to hunt across the map. `WaddleDeeChaseMovement` works around it:

1. It finds the nearest rider itself, with no range limit, by calling `Enemy_DistToPlayer` (0x801fffa4) on each of the 4 rider slots. An empty slot returns `FLT_MAX`.
2. It writes the result into `ed->target_ply`.
3. It sets `ed->turn_timer = 0` and `ed->retarget_cooldown = 2`. The vanilla function only recomputes `turn_to` while `turn_timer` is 0 (no turn in progress).
4. Only then does it call `EventActor_FindNearestPlayer`.

The non-zero cooldown makes the vanilla function keep the pre-set target instead of re-evaluating it with its range check. It still computes `turn_to` (normalized `pos - target pos`) from that target.

Velocity is then `-turn_to * WADDLE_DEE_CHASE_SPEED` (0.6); the sign is flipped because `turn_to` points from the player to the enemy. `vel.Y` is zeroed so gravity does not accumulate; the ground snap owns Y.

## Spawning, Fade-Out and Lifecycle

### Start and spawn passes

`WaddleDeeSwarm_Start`:
- calls `Enemy_CheckAndLoad(ENEMYKIND_WADDLE_DEE)` so the archive is resident;
- clears every slot;
- sets `swarm_active`.

`WaddleDeeSwarm_Active` runs a spawn pass every `WADDLE_DEE_SPAWN_INTERVAL` (20) frames: first `WaddleDeePruneSlots`, then `WaddleDeeSpawnOne` into the first free slot of `WADDLE_DEE_MAX_COUNT` (10).

### Why slots are pruned

Most of the ways vanilla destroys an actor never pass through the death or inhale states the chase proc watches for. The chase proc goes with the actor, so its slot would never be freed. Those paths are:
- out-of-bounds distance in `EventActorGObj_ProcPhys`;
- lifetime or force-kill in `EventActorGObj_ProcEndOfFrame`;
- the kill floor;
- death from the common states (`EventActor_CommonPhys`);
- a failed knockback landing.

A slot is kept only while its GObj is still on the `GAMEPLINK_ENEMY` list **and** has kind `ENEMYKIND_WADDLE_DEE`. The kind check covers the allocator handing a freed GObj straight to another actor.

### Spawning one Waddle Dee

`WaddleDeeSpawnOne` picks a random human player and places the actor at one of twelve fixed ~40-unit XZ offsets around them. It builds a zero-initialized `EventActorDesc` and passes it to `EventActor_Create` (0x801fbb50):

| Field | Value |
|-------|-------|
| `kind` | `ENEMYKIND_WADDLE_DEE` |
| `forward` | `{0,0,1}` |
| `up` | `{0,1,0}` |
| `scale` | 1.0 |
| `spawn_index`, `spawn_slot` | -1 |
| `leash_radius` | -1.0 (no leash) |

The new GObj gets `WaddleDeeChaseProc` at priority 10, and a fresh slot with `saved_pos` seeded from the descriptor.

### The chase proc

Each frame, the chase proc checks these in order:
1. **No slot**: the actor is left alone.
2. **Dying or inhaled** (state 0x09/0x0A): the actor is untracked and left for vanilla to finish.
3. **`swarm_active` clear**: the actor is untracked and destroyed.
4. **A common state below 0x0E** (spawn, knockback, launched, sliding): left to vanilla.
5. **Otherwise**: it undoes spline snaps, reinstalls the chase callbacks, and runs the fade or the contact check.

### Fade-out on contact

Contact means the target's `Enemy_DistToPlayer` falls below `WADDLE_DEE_HIT_RADIUS` = 1.0. That value is a centre-to-centre 3D distance to `PlayerData.position`, which the game refreshes from the rider position every frame.

On contact the proc starts a fade:
1. `fade_timer` counts down from `WADDLE_DEE_FADE_FRAMES` (20).
2. Each frame it scales `ed->final_scale` toward 0 and freezes XZ velocity.
3. When the timer runs out it untracks the actor and calls `EventActorGObj_Destroy`.

### Ending the swarm

**`WaddleDeeSwarm_End2` deliberately does not destroy the actors itself.** It only clears `swarm_active`. By the time the event ends, some tracked GObj pointers can be stale, because vanilla may already have destroyed those actors. Clearing the flag lets each `WaddleDeeChaseProc` see it on its next frame and destroy its own live actor.
