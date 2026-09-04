# Enemy AI System

How the enemy/event actors (Waddle Dee, Sword Knight, Scarfy, TAC, Dyna Blade, ...) decide
what to do and move: the two-level state machine, the animation-script bytecode, per-type AI,
targeting and knockback. `EnemyData`, the `ActorID` enum and the spawn-descriptor structs are
declared in `externals/hoshi/include/enemy.h`; only the offsets a reader needs to follow the
explanations below are repeated here.

## Architecture

Three things drive an enemy every frame:

1. **A two-level state machine.** `EnemyStateChange` resolves a state ID to a state-table
   entry and installs four function pointers into `EnemyData`.
2. **The GObj proc chain.** Ten prioritized callbacks registered by `EventActor_Create` run
   animation, physics, collision and damage, and dispatch those four state functions.
3. **Per-type data.** One descriptor per actor type supplies the per-type state table, the
   init/capture callbacks and the `.dat`-resident parameter blocks.

The four state-table function pointers **are** the per-type AI brain. There is no separate
"AI callback" in use: `ed+0xAC8` (`per_type_cb`), dispatched at priority 7, is a dead slot -
across the whole enemy code region (0x801fb000-0x8021f000) the only accesses are the
zero-store in `EnemyStateChange` (0x801fc634) and the read in `EventActor_ProcPerType`
(0x801fc85c). That makes it the cleanest custom-AI injection point.

## GObj Proc Execution Order

`EventActor_Create` registers these procs on every enemy GOBJ. They run in priority order
every frame:

| Priority | Address | Name | Purpose |
|----------|---------|------|---------|
| 0 | 0x801fc670 | `EventActor_ProcResetDamage` | Zero per-frame damage accumulators |
| 1 | 0x801fc698 | `EventActor_ProcUpdate` | HSD animation advance, animation-script machine, `state_func1` dispatch |
| 4 | 0x801fc6fc | `EnemyPhysicsProc` | `state_func2` dispatch, `vel += accel`, `pos += vel`, OOB floor kill (skipped for actor_id >= 0x4C) |
| 5 | 0x801fc7c4 | `EventActor_ProcStateActive` | `state_func3` dispatch - main per-state AI logic |
| 6 | 0x801fc7f8 | `EventActor_ProcSharedModel` | Shared model update (shadow, 3D transform), `state_func4` dispatch |
| 7 | 0x801fc848 | `EventActor_ProcPerType` | `per_type_cb` dispatch (if non-null), HurtData update, position snap |
| 8 | 0x801fc8e8 | `EventActor_ProcHitCollInit` | Single `blr` - no-op stub |
| 9 | 0x801fc8ec | `EventActor_ProcHitColl` | HitColl processing + collision checks |
| 10 | 0x801fc9f0 | `EventActor_ProcDamage` | Reads HurtData output, calls `giveEnemyDamage` (0x8020b680), dispatches hit-reaction callback `ed+0xAD0` |
| 21 | 0x801fcabc | `EventActor_ProcFinal` | `pos` to `pos_prev`, ground state flags, lifetime/despawn checks |

Priority 1 is the animation half of the frame: `HurtData_UpdatePerFrame` advances the HSD
animation, VFX update runs, `EventActor_StateMachine` executes the animation-script bytecode,
a post-update finalizes the animation frame, and only then is `state_func1` called.

## Animation Script Bytecode

Every enemy animation carries a bytecode script that drives animation-triggered behavior:
play a sound at frame N, emit a hitbox, spawn VFX, transition state on animation end.

The script state lives at `ed+0x4C`: a countdown timer (initialized to -1.0 so the first
command fires immediately), the current animation-frame accumulator (`ed+0x2A8 + ed+0x2AC`,
rewritten each frame), the bytecode pointer (NULL = stopped) and a loop-stack depth followed
by the loop stack itself.

Each frame the timer decrements; at `timer <= 0.0` the next command byte is read. The command
ID is `(byte >> 2) & 0x3F` - the **upper** six bits of the byte, with the low two bits kept as
an inline sub-selector that some handlers consume.

`EventActor_StateMachine` (0x802017d0) dispatches by first calling the generic HSD handler
`zz_80068d74_` with the command ID. That function handles commands 0-10 via a 4-byte-entry
jump table at 0x80499628 (`cmd < 11` -> `table[cmd]`, returns 1) and returns 0 otherwise.
On a 0 return the state machine falls back to the enemy command table at 0x804b26b0, indexed
by `(cmd - 11) * 12`.

### Builtin commands 0-10 (jump table 0x80499628)

Generic HSD animation/timing commands shared across the engine.

| Cmd | Name | Purpose |
|-----|------|---------|
| 0 | Stop | Sets `script_ptr = NULL`, halting the script |
| 1 | Wait(frames) | Sets timer to a frame count for countdown |
| 2 | WaitUntil(frame) | Jumps timer to a specific animation frame |
| 3 | LoopBegin(count, addr) | Pushes loop counter and return address onto loop stack |
| 4 | LoopEnd | Decrements loop counter; jumps back or pops stack when done |
| 5 | Call(addr) | Subroutine call - pushes return address, jumps to addr |
| 6 | Return | Pops return address from stack, resumes caller |
| 7 | Goto(addr) | Unconditional jump to bytecode address |
| 8 | SetTimerInfinite | Pauses script indefinitely (timer set to huge value) |
| 9 | PlaySFX(id) | Plays a sound effect by ID |
| 10 | StopSFX | Stops the current sound effect |

### Enemy commands 11-25 (table 0x804b26b0)

Entries are 12 bytes, `{word0 = handler, word1, word2 = operand-word-count}`; dispatch uses
`word0`. Handlers receive **r3 = `EnemyData*`** and **r4 = `ed+0x4C`** (the script state), and
read operands through the script pointer, advancing it 4 bytes per operand word.

The callable range is exactly **cmd 11-25 (15 handlers)**. The trailing slots are
non-callable: index 15 (cmd 26) `word0 = NULL`, index 16 (cmd 27) `word0 = 0x01000000`,
index 17 (cmd 28) `word0 = 0xFFFFFFFF`. The 0x80201928/0x80201948 values in that region sit in
the `word1` column and are never dispatched.

| Cmd | Handler | Purpose |
|-----|---------|---------|
| 11 | 0x80200e58 | Set attribute flag bits in `ed+0xB00` and a float in `ed+0xB04` |
| 12 | 0x80200eb8 | Emit a HitColl attack-box descriptor: decodes ~6 operand words (int16 offsets/sizes -> float-scaled + packed flag bits) into a collision-box descriptor and pushes it into the enemy's HurtData (`ed+0x410`), indexing a fn-pointer table at `ed+0x2B4` |
| 13 | 0x80201138 | Disable a HitColl box by index (operand `& 0x03FFFFFF`) via `Hit_SetInactive` |
| 14 | 0x80201180 | Flush the pending HitColl boxes: `Hit_SetInactive` on each box of the `ed+0x410` array, clear the enable flag |
| 15 | 0x802011bc | Spawn a projectile / sub-actor (optional random variant; decodes transform operands, calls `EventActor_SpawnProjectile` 0x8020c738) |
| 16 | 0x80201418 | Broadcast controller rumble (per human player) |
| 17 | 0x80201488 | No-op: advance script ptr 1 word |
| 18 | 0x80201498 | Spawn/refresh a persistent particle effect (`Effect_SpawnSync`; handle stored to `ed+0xA70`/`ed+0xA74`, prior one destroyed) |
| 19 | 0x80201504 | No-op: advance script ptr 3 words |
| 20 | 0x8020152c | Play a sound/voice (2-bit selector + word arg; AudioEmitter from `ed+0xA58[]`, handle cached to `ed+0xA60`) |
| 21 | 0x80201578 | Play a random 1-of-N sound (`HSD_Randi` pick, then the cmd-20 audio path) |
| 22 | 0x802016ac | Store a 24-bit script constant into one of `ed+0xAF0/0xAF4/0xAF8/0xAFC` (low-2-bit selector) |
| 23 | 0x8020172c | Set event/state flag bit (bit 1 of byte `ed+0xB0A`) |
| 24 | 0x8020174c | Apply a color animation (`ColAnim_Apply` on the ColAnim component at `ed+0x70`) |
| 25 | 0x80201798 | Reset the color animation |

## State Machine

### EnemyStateChange (0x801fc398)

```c
void EnemyStateChange(EnemyData *ed, int state_id, int flags, float anim_rate, float anim_end_frame);
```

Register assignment is the natural one: `ed` r3, `state_id` r4, `flags` r5, `anim_rate` f1,
`anim_end_frame` f2. State IDs below 0x0E come from the common table (`ed+0x40`, 0x804b2950);
0x0E and above index the type's own table (`ed+0x44`) at `state_id - 0x0E`.

Each table entry is 0x14 bytes and is the whole of a state's definition:

```c
typedef struct StateTableEntry {
    int   anim_idx;   // +0x00: animation index (-1 = none)
    void *func1;      // +0x04: -> ed+0xAB8, priority 1 (ProcUpdate)
    void *func2;      // +0x08: -> ed+0xABC, priority 4 (pre-physics)
    void *func3;      // +0x0C: -> ed+0xAC0, priority 5 (state active / main AI)
    void *func4;      // +0x10: -> ed+0xAC4, priority 6 (shared + model)
} StateTableEntry;
```

**Flags (r5):**

| Bit | Mask | Effect |
|-----|------|--------|
| 0 | 0x01 | Skip animation setup. Function pointers are still installed. |
| 1 | 0x02 | Skip animation reset if `anim_idx` unchanged |
| 2 | 0x04 | Skip cleanup function (`EventActor_StateCleanup`, 0x801fe110) |
| 3 | 0x08 | Save/restore position (ed+0x538-0x540) across the transition |
| 4 | 0x10 | Skip clearing `ed+0xAD4` only - `ed+0xAC8` is cleared regardless |
| 5 | 0x20 | Skip HurtData animation reset |
| 6 | 0x40 | Skip SFX handle cleanup (`ed+0xA3C`/`ed+0xA40`) |

**Order of work:** write `state_id` to `ed+0x34`; check the special-actor grounded flag (bit 3
of `ed+0xB0B`, used by Gordo and event actors); clean up the SFX handles via
`EventActor_CleanupVfxA3C`/`EventActor_CleanupVfxA40` (0x8020c6e0/0x8020c70c, both routing to
the sound stop at 0x80236358) unless flag 0x40; run `EventActor_StateCleanup` unless flag 0x04;
update the HSD animation scale and reset HurtData unless flag 0x20; look up the table entry
and install `anim_idx` (`ed+0x3C`) and the animseq pointer (`ed+0x48`); unless flag 0x01, do
the animation setup (conditional anim reset, playback rate from `anim_rate`, end frame from
`anim_end_frame`, HSD anim object, material anim reset, script state re-init with the timer at
-1.0 so the first command fires immediately).

Two facts matter for anyone hooking this:

- **func1-func4 are always copied** into `ed+0xAB8`-`ed+0xAC4`, whatever the flags say. Flag
  0x01 only skips the animation half.
- **`ed+0xAC8` (per_type_cb) is cleared unconditionally** on every transition. Only the
  `ed+0xAD4` store is guarded by flag 0x10.

### Common states 0x00-0x0D (table 0x804b2950)

Fourteen entries shared by every actor type.

States **0x00-0x08** all install the same trio (func1 0x8020bd68, func2 0x8020be1c, func3
0x8020c558, func4 NULL) and differ only in `anim_idx` 0-8. These are the normal behavioral
states: the animation index picks idle/walk/attack while the per-type state functions do the
actual thinking.

| State | Anim | func1 | func2 | func3 | func4 | Purpose |
|-------|------|-------|-------|-------|-------|---------|
| 0x00-0x08 | 0-8 | 0x8020bd68 | 0x8020be1c | 0x8020c558 | NULL | Normal animation states |
| 0x09 | 9 | 0x80203e60 | - | - | - | Death/despawn |
| 0x0A | 10 | - | 0x80203a50 | 0x80203b28 | 0x80203b64 | Inhaled/absorbed by Kirby |
| 0x0B | 11 | 0x8020ddb4 | 0x8020de90 | 0x8020dfe8 | - | Knockback/hit reaction |
| 0x0C | 12 | - | 0x8020e338 | 0x8020e61c | - | Launched/airborne |
| 0x0D | 13 | - | 0x8020e7e0 | 0x8020e954 | - | Grounded/sliding |

**Shared animation states (0x00-0x08).**

- `EnemyState_AnimEnter` (0x8020bd68, func1) manages animation playback rate: frozen during
  stun, otherwise a decaying `anim_speed_scale` (ed+0xA2C) with a minimum clamp so the
  animation never fully stops.
- `EnemyState_AnimTick` (0x8020be1c, func2) branches on HP. Alive: attraction/inhale physics
  in three modes (attracted toward a player, fully captured and following the rider, or
  skipped) plus player-ride attachment. Dead, first frame: stop all SFX, zero velocity, play
  the death VFX/SFX, call `custom_death_callback` (ed+0xAEC) if set or the default death
  handler. Dead, later frames: count `death_frame_counter` (ed+0x9C8) and destroy the actor
  past the threshold.
- `EnemyState_AnimExit` (0x8020c558, func3) runs stun: while `stun_frames` (ed+0xA18) > 0 it
  ticks the stun animation and spawns a spark VFX (SFX 0x27C6) every 5th frame; at zero stun
  with no death timer it returns to idle.

**Death (0x09).** `EnemyState_DeathEnter` (0x80203e60) is enter-only: hide the model, play the
death SFX/VFX (`ed+0x9C0`/`ed+0x9C4`), run special-actor cleanup, and set
`death_frame_counter` to 600. Since 601 already exceeds the 120-frame destruction threshold,
the actor dies on the very next frame - death is effectively instantaneous.

**Inhaled (0x0A).** func2 (0x80203a50) updates the attachment slot relative to the parent
rider; func3 (0x80203b28) counts `inhale_timer` (ed+0xB20) and destroys the actor past 120
frames; func4 (`EnemyState_InhaledFunc4`, 0x80203b64) tracks the rider's mouth bone with
`JOBJ_GetWorldPosition` and shrinks the enemy toward it.

**Knockback (0x0B).** func1 (0x8020ddb4) builds the launch velocity from the direction fields
(ed+0x334) scaled by `kb_speed_mult` (ed+0x878) and `param_speed_2` (ed+0x39C). func2
(0x8020de90) follows the hit source's position/orientation if `ed+0x950` is set. func3
(0x8020dfe8) counts the recovery timer (ed+0x888) and raycasts for a landing spot: on success
it moves to state 0x0C, on failure it calls `EventActor_Destroy`.

**Launched (0x0C) and sliding (0x0D).** 0x0C func2 (0x8020e338) is spline-based projectile
motion driven by `launch_time_accum`/`launch_time_step` (ed+0x8A8/0x8AC); on reaching the
endpoint it switches to 0x0D with grounded physics enabled and a decaying bounce velocity
(ed+0x8F0). 0x0D func2 (0x8020e7e0) applies ground friction (`friction = constant /
slide_timer`, ed+0x8EC) and interpolates orientation toward the ground axes; func3
(0x8020e954) does the ground collision response and snap.

## Per-Type AI

States 0x0E and up are type-specific, read from the state table the per-type descriptor
points at. State IDs are relative: 0x0E is entry 0, 0x0F is entry 1, and so on. T1 and T2
variants of an enemy always share their parent's descriptor, so they have identical state
tables. Entry 0 always has `anim_idx = -1` and is the spawn entry.

### The decision loop

On spawn an enemy enters state 0x0E, whose func1 runs a one-time init-and-launch (set up
velocity, ground-snap, attach to a patrol spline) and immediately `EnemyStateChange`s into the
type's active/combat state, normally 0x0F. From there func1-func4 carry the per-frame logic.
**The slot roles are not fixed across types** - each enemy distributes perceive / decide /
move across whichever slots it likes (Sword Knight decides in func1, Scarfy targets in func4).
The recurring composition is:

- **One slot perceives** - `EnemyActor_FindNearestPlayer`, `EnemyActor_FindNearestPlayerFOV`,
  or one of the cone scanners.
- **One slot moves** - `EnemyActor_CombatAI` / `EnemyActor_CombatMovement` (grounded chasers),
  `Enemy_AIPhysicsTick` (spline patrol), or a flyer mover.
- **State transitions** (idle -> chase -> attack -> recover) are `EnemyStateChange` calls made
  from inside those funcs when a range, timer or crossing condition trips.

No enemy ever installs `ed+0xAC8`, in its init callback or anywhere else.

### Worked examples

**Waddle Dee (0x17)** - state table 0x804b3e78, init_cb 0x80219448, 5 entries. Not a pure
patroller: it detects and lunges at riders.

| State | Anim | func1 | func2 | func3 | func4 | Purpose |
|-------|------|-------|-------|-------|-------|---------|
| 0x0E | -1 | 0x80219638 | - | - | - | Spawn - ZeroVelocity, `EnemyPath_FollowUpdate`, -> 0x0F |
| 0x0F | 0x0E | - | - | 0x802196e0 | 0x80219704 | Patrol a spline while scanning; func4 calls `EnemyActor_FindNearestPlayerAhead` (0x801fe5d4) - on a hit -> 0x10 |
| 0x10 | 0x0F | 0x802197ac | 0x802197e8 | 0x802197ec | - | Windup / turn toward target -> 0x11 |
| 0x11 | 0x10 | 0x80219908 | 0x80219988 | 0x80219a48 | 0x80219a84 | Lunge / hop attack (accumulates a swing into ed+0xB54, adjusts height) -> 0x12 |
| 0x12 | 0x11 | 0x80219b4c | - | 0x80219c40 | 0x80219c64 | Recover / settle -> `EnemyPath_FollowUpdate` -> 0x0F |

The loop is patrol-while-scanning -> detect -> windup -> lunge-hop -> recover -> patrol. There
is no idle state.

**Sword Knight (0x05)** - descriptor 0x804b3168, state table 0x804b3118, 4 entries. A spline
patroller that slashes when a rider crosses its front.

| State | Role | func1 (pri 1) | func2 (pri 4) | func3 (pri 5) | func4 (pri 6) |
|-------|------|---------------|---------------|---------------|---------------|
| 0x0E | spawn | 0x802113ec | - | - | - |
| 0x0F | seek / slash-watch | `SwordKnight_State0FDecide` (0x80211520) | -> CombatMovement (0x80211694) | -> CombatAI (0x802116c8) | shared (0x802116e8) |
| 0x10 | attack (slash) | 0x802117c0 | 0x802117e8 | -> CombatAI (0x80211c14) | 0x802116e8 |
| 0x11 | recover | 0x80211ca8 | 0x80211de8 | -> CombatAI (0x80211e08) | 0x802116e8 |

- `SwordKnight_Init` (0x802111d8): `EventActor_SpawnChild` for the SP Sword Knight rider
  (actor 0x49), installs the landing/grounded callback trio into `ed+0xADC/0xAE0/0xAE4`, then
  `EventActor_FinalizeInit`.
- Spawn func1 (0x802113ec) calls `SwordKnight_BeginCombat` (0x80211444):
  `EventActor_SetupVelocity` + `EventActor_GroundSnap`, `EnemyStateChange` -> 0x0F,
  `EnemyPath_FollowUpdate` to attach to a patrol spline, target lock reset (`ed+0xB4C = -1`).
- `SwordKnight_State0FDecide` (0x80211520) runs each frame after a detection delay
  (`*(actor_data+4)`[7] frames), scanning the 4 players with `EnemyActor_PlayerAheadDist`
  (0x801fea60). It locks the nearest player **crossing from behind to in front** within attack
  range (`*(actor_data+4)`[0]) into `ed+0xB4C`, and after a windup (`*(actor_data+4)`[1]
  frames) fires `SwordKnight_TriggerAttack` (0x80211734) -> state 0x10.

**Scarfy (0x04)** - descriptor 0x804b2ff8, state table 0x804b2f80. The iconic
Kirby-chaser puts its perception in func4 (0x8021027c, `Scarfy_TargetFOV`), which calls
`EnemyActor_FindNearestPlayerFOV` with the global detection range
(`stc_enemy_param_table + 0x90`), acquiring the nearest rider inside a forward hemisphere and
homing on a body bone.

**Wheelie (0x08)** - state table 0x804b3350, init_cb 0x802132ec, 3 entries; init calls
`Enemy_SetTerrainLocked` (0x8020ae54). Spawn (0x0E) sets up velocity and path, roam (0x0F)
runs `Enemy_AIPhysicsTick` and flips to drive (0x10) when it reports stationary, drive runs
`EnemyActor_GroundFollowMovement` (0x80208bd4) and returns to roam. No player targeting.

**Gordo (0x0E)** - state table 0x804b3808, init_cb 0x80215a00, 3 entries. Its init is just
`EventActor_FinalizeInit`; the spawn func sets `grounded_active` (ed+0x908) = 1 directly rather
than calling `Enemy_SetTerrainLocked`. Bounce (0x0F) is a frame-gated timer (ed+0xB48) that
flips to hide (0x10) at a random threshold, while func4 runs
`EnemyActor_FindNearestPlayerFOV` to face the nearest rider; hide counts ed+0xB4A down with
the model hidden and velocity zeroed, then returns. **Nothing writes pos.y** - the visible
vertical bounce is the looping animation, and the oscillation is a state/animation/visibility
cycle, not code-driven motion.

**Broom Hatter (0x00)** - state table 0x804b2d88, init_cb 0x8020ea44, 4 entries. Composite
(init `EventActor_SpawnChild`s the SP Broom Hatter rider, actor 0x48) and terrain-locked. It
is a spline/ground wanderer-chaser using the same `EnemyActor_CombatMovement` /
`EnemyActor_CombatAI` mover pair as Sword Knight, with a roam/recover state (0x10) on a
300-frame timer and a chase state (0x11) that despawns on timeout or loss of spline. No player
targeting.

### Who targets players

Player targeting is widespread, not exceptional. Call-site counts are from a `bl`-xref scan of
the enemy code region 0x801fb000-0x8021f000.

| Entry point | Address | Call sites | Notes |
|---|---|---|---|
| `EnemyActor_FindNearestPlayer` | 0x801ffd78 | 13 | Nearest rider within the global acquisition radius (50.0) |
| `EnemyActor_FindNearestPlayerFOV` | 0x801ff8d8 | 13 | Adds forward-hemisphere + bone aim. Scarfy, Bronto Burt, Bomber, Gordo, Walky |
| `EnemyActor_PlayerAheadDist` | 0x801fea60 | 4 | Per-player crossing test (Sword Knight) |
| `EnemyActor_FindNearestPlayerAhead` | 0x801fe5d4 | 1 | Waddle Dee - nearest rider in a forward cone |
| `EnemyActor_FindPlayerInRangeFwd` | 0x801fe764 | - | TAC grab probe |
| `EnemyActor_FindDiveTarget` | 0x801fe8dc | 2 | Bronto Burt dive gate |

The last three are a family of standalone cone scanners: each loops the 4 players via
`Ply_GetRiderGObj`/`Ply_GetPosition`, distance-gates, then dot-tests against the enemy's
forward axis, returning a player index rather than writing `ed+0xB24`.

Cappy and Noddy do not target at all: Cappy is a func1-only animation-driven jump-out machine
(func2/3/4 all NULL) and Noddy's combat state is entirely NULL, a sleeper.

### Flyers

Airborne enemies do **not** integrate `vel += accel` plus ground-snap the way grounded chasers
do. They compute position directly from an anchor, through two shared movers:

| Mover | Address | Used by | What it does |
|-------|---------|---------|--------------|
| `EnemyActor_FlyMovement` | 0x8020354c | Scarfy, Bomber, Bronto Burt recover | Anchored hover: `pos = pos_initial (ed+0x310) + sin(phase) * amplitude` (mode 1), or a homing steer offset re-aimed via `HSD_Randf` when blocked (mode 2), plus animation root motion. No gravity, no ground raycast. Mode is `*(actor_data)+0x148`. |
| `EnemyActor_FlyForward` | 0x8020335c | Bronto Burt cruise + dive | Builds a basis from forward/up/right (ed+0x334/0x340/0x34C) scaled by speed (ed+0x344) and adds it straight into pos - advance along facing at speed. |

Flyer func3 slots still call the ground-snap/path helpers (0x80205884 / 0x80205a60) to keep
the shadow and ground-height reference current while airborne; those are not movers. The only
flyer that touches `Enemy_AIPhysicsTick` is Bronto Burt's cruise, a hybrid that also calls
`CombatMovement`.

**Bronto Burt dive-bomb (0x02, descriptor 0x804b2ecc, state table 0x804b2e68)** is the worked
flyer. Dive-watch (0x10) func1 (0x8020f6a8) reads altitude and scans for a rider with
`EnemyActor_FindDiveTarget`; on a hit it zeroes velocity and moves to dive (0x11), whose func2
(0x8020f800) drives the ballistic plunge with `EnemyActor_FlyForward` while func1 polls
`EventActor_JObjCheck` (0x80200d10, the anim/JObj-state gate) for completion. Recover (0x12)
func2 (0x8020f8d4) drifts back toward the anchor with `EnemyActor_FlyMovement`. func4 keeps the
Burt oriented at the nearest rider every frame; the dive-watch range scan is the actual gate.

Only **Bronto Burt, Bomber and Scarfy** are true flyers. Cappy, Walky and Noddy are grounded:
Cappy is an animation-driven ground ambusher with no mover, Walky a grounded spline chaser via
`CombatMovement`/`CombatAI`, Noddy a grounded spline sleeper.

### Shared movement helpers

| Helper | Address | Role |
|--------|---------|------|
| `EnemyActor_CombatAI` | 0x802069e8 | Two-phase grounded movement keyed on `ed+0x908`: phase 0 approach collision probe, phase 1 engaged ground physics (`Enemy_GroundPhysicsVelocity` + ground-snap); flips phase and re-attaches to ground on contact. |
| `EnemyActor_CombatMovement` | 0x8020b490 | Sibling of CombatAI on the same `ed+0x908` phase bit: phase 0 = spline `Enemy_AIPhysicsTick`; once moving, phase 1 = accel along the ground normal (`accel = ground_normal * param_gravity`) + `EnemyActor_GroundFollowMovement`. |
| `EnemyActor_ClassifyRange` | 0x80206cc0 | Proximity classifier. Reads detect range (+0x10) and chase range (+0x14) from the actor_data param-root (`*(ed+0x14)`), buckets target distance into out/detect/attack, stores it in `ed+0xB09` bits 3-4 and returns it. For actors >= 0x4C it additionally clears the bucket via `zz_801ffce4_`. |
| `EnemyActor_PlayerAheadDist` | 0x801fea60 | Per-player test: gets player `i`'s rider position, writes the signed forward distance `(player - enemy) . forward` to the out param, returns 1 if the player is behind, -1 if the player has no rider. |
| Ground helpers | 0x80205884, 0x80205a60, 0x802064b0, 0x80206a7c, 0x80206b98, 0x80206d90 | mpColl ground-snap and `ed+0x908` phase-transition helpers used by the two combat movers. |

### Per-type state counts

Entry counts are read off the gap between a table pointer and the next datum (each 0x14-byte
array is terminated by the descriptor's own back-pointer). Enemies not listed have not had
their counts established individually.

| Enemy | State table | Entries | Notes |
|-------|-------------|---------|-------|
| TAC (0x4C) | 0x804b4088 | 12 (0x0E-0x19) | Most complex enemy AI - chase/grab/flee + loot scatter |
| Dyna Blade (0x4D) | 0x804b41e8 | 8 (0x0E-0x15) | Descend -> cruise -> anim-driven swoop -> exit |
| Scarfy (0x04) | 0x804b2f80 | 6 (0x0E-0x13) | FOV-homing flyer-chaser |
| Waddle Dee (0x17) | 0x804b3e78 | 5 (0x0E-0x12) | Patrol + detect -> lunge attack |
| Bronto Burt (0x02) | 0x804b2e68 | 5 (0x0E-0x12) | Flyer; cruise / dive-watch / dive / recover |
| Cappy (0x06) | 0x804b31f8 | 5 (0x0E-0x12) | Grounded ambusher - func1-only, anim-driven jump-out |
| Sword Knight (0x05) | 0x804b3118 | 4 (0x0E-0x11) | Chase + attack |
| Broom Hatter (0x00) | 0x804b2d88 | 4 (0x0E-0x11) | Composite (child 0x48); spline+ground chaser |
| Wheelie (0x08) | 0x804b3350 | 3 (0x0E-0x10) | Roam/drive |
| Gordo (0x0E) | 0x804b3808 | 3 (0x0E-0x10) | Bounce (state/anim cycle) + FOV facing |
| Bomber (0x0F) | 0x804b3750 | 2 (0x0E-0x0F) | Flyer; single cruise state |
| Noddy (0x0A) | 0x804b3530 | 2 (0x0E-0x0F) | Grounded sleeper - combat state entirely NULL |
| Walky (0x15) | 0x804b3bb8 | 2 (0x0E-0x0F) | Grounded spline chaser |

## Per-Type Descriptor Table (0x804b1d98)

An array of 79 (0x4F) pointers indexed by ActorID, resolving to 47 unique 0x20-byte
descriptors. All descriptor callbacks live in 0x8020EA44-0x8021E0D4.

```c
typedef struct PerTypeDescriptor {
    void *state_table;     // +0x00: per-type state table (states >= 0x0E), stored to ed+0x44
    void *pad_04;          // +0x04: NULL
    void *init_cb;         // +0x08: init callback, run during EventActor_Create
    void *copy_data_cb1;   // +0x0C: copies actor_data params into EnemyData
    void *copy_data_cb2;   // +0x10: second data copy pass
    void *destroy_cb;      // +0x14: cleanup on GObj destruction
    void *post_init_cb;    // +0x18: final-init, dispatched at the END of EventActor_Create
    void *post_capture_cb; // +0x1C: dispatched from EventActor_OnCapture when inhaled
} PerTypeDescriptor;
```

**The +0x18 and +0x1C slots are easy to mix up.** Both are read through the same
`descriptor[ed->kind]` lookup but at different sites. **+0x18 fires once at the tail of
`EventActor_Create` (0x801fbef4)**, after all ten procs and the GXLink are registered: it does
the final ground/spline re-snap and `EnemyStateChange`s into the type's default state 0x0E. It
is not capture-related. **+0x1C fires inside `EventActor_OnCapture` (0x802038c4)** when the
enemy is inhaled by a rider, attaching the enemy onto the rider's mouth-bone slot and
detaching its own child.

There is **no damage callback in the descriptor**: the damage path
(`EventActor_ProcDamage`, priority 10) uses `ed+0xAD0` and falls back to
`EnemyKnockback_Default`, never indexing the descriptor.

### Init callback pattern

Per-type init callbacks follow a common shape:

1. Composite enemies only: `EventActor_SpawnChild` (0x801fcda0) to spawn the rider/attached
   actor.
2. `EventActor_GroundSnap` (0x80204fac) with a scale parameter.
3. Install hit-reaction callbacks at `ed+0xACC`/`ed+0xAD0` and/or the knockback-landing trio
   `ed+0xADC`/`ed+0xAE0`/`ed+0xAE4` (Broom Hatter, Sword Knight).
4. `EventActor_FinalizeInit` (0x802042fc) for animation setup and collision.
5. Optionally `Enemy_SetTerrainLocked` (0x8020ae54), which sets bit 2 (0x04) of `ed+0xB0B`.
   Broom Hatter and Wheelie do; Gordo instead sets `grounded_active` (ed+0x908) directly.

## Special and Event Actors (0x48-0x4E)

| ID | Type | Behavior |
|----|------|----------|
| 0x48-0x4A | Child parts (SP Broom Hatter, SP Sword Knight, SP Waddle Dee Truck) | No init, no default state. Mirror the parent's transform via `EventActor_FollowParent` (0x80219eec); `parent_gobj` comes from the descriptor. |
| 0x4B | Event Gordo | Independent actor with its own init and behavior |
| 0x4C | TAC | Independent; chase/grab/flee, scatters items |
| 0x4D | Dyna Blade | Independent; scripted flyover that rains items |
| 0x4E | Meteor | Independent scripted hazard: falls from the sky and damages what it lands on. Its fall parameters come from the City Trial meteor-event globals rather than its own descriptor. |

TAC and Dyna Blade are the two complex standalone AI actors. Both have `actor_id >= 0x4C`, so
`EnemyPhysicsProc` skips their OOB floor kill and `EventActor_SetVisibility` leaves them
render-disabled until their own idle func re-enables rendering. Neither uses the spawn-slot
pool - they come in through the event system. **Both interact with items only by spawning
fresh City Trial pickups into the world** (`CityItem_GetEventItem` 0x80254114 +
`CityItem_Throw` 0x80253ce4); neither removes anything from a rider's inventory.

### TAC (0x4C)

Descriptor 0x804b4178 -> state table 0x804b4088 (12 states), init_cb 0x8021a534.

Spawn (0x0E) randomly enters chase (0x0F) or a timed wander (0x12). In chase, func4 (0x8021aa6c)
runs the forward-cone probe
`EnemyActor_FindPlayerInRangeFwd` (0x801fe764); with a rider in range and in front it commits
to a dash and grab (0x14 -> 0x15), steering toward the target each frame with
`RotateVecAroundAxis` while wall-avoiding at speed `param[4]`. A successful grab roll goes to
recover (0x16), which can re-dash or fall through to flee (0x17): TAC climbs past `param[5]`,
disables its hitbox and `EventActor_Destroy`s once off-screen. Being hit drops it into
hit-reaction (0x19). States 0x10/0x11/0x13 are chase sub-moves and 0x18 is an animation-driven
despawn.

**The "steal" is not a steal.** `Tac_ScatterItems` (0x8021c8ec) loops
`CityItem_GetEventItem`/`CityItem_Throw`, fanning directions around TAC's forward. It is called
mid-dash (gated on `ed+0xB62 % 60 == 30` and `HSD_Randi(5) == 0`) and again on hit (scattering
`param[11]` items). There is no write to any player item-collect array anywhere in TAC's code;
the on-screen impression of theft is TAC lunging while scattering new pickups. Its HitColl body
is the actual contact/damage mechanism.

### Dyna Blade (0x4D)

Descriptor 0x804b4288 -> state table 0x804b41e8 (8 states), init_cb 0x8021c9dc.

It spawns high and descends (0x0E -> 0x0F) to a saved cruise altitude. Pass-over (0x10) is the
only player-aware code in the actor: it loops the 4 riders with `EnemyActor_DistToPlayer` and,
within `param[5]`, fires `EnemyActor_RumblePlayer(p, 4, 30)` - a proximity rumble, not
targeting. It ping-pongs between two flap poses (0x11 and 0x12), then commits to the swoop
(0x13), whose flight path **is the baked model animation**: `DynaBlade_StateDive_Proc`
(0x8021d500) samples the JOBJ translate node's world position each frame and
finite-differences it into velocity and acceleration. The swoop's first frame fires
`DynaBlade_ThrowItems` (0x8021db44). Once it has climbed back out of the arena (0x14) it stops
its audio emitter and destroys itself. A strong hit sends it to recoil (0x15) with a forced
item throw. There are zero `FindNearestPlayer`/FOV calls in Dyna Blade: it flies a fixed baked
path, and whoever is underneath gets rumbled, hit by its contact body, or showered with items.

Both actors' concrete tuning values - `param[N] = *(actor_data+4)[N]`: detect range, dash
speed, item-drop counts, swoop count - come from `Enemy.dat`, not from code constants. The
gating structure above is the code; the thresholds are archive data.

## Enemy Offensive Hitboxes

The enemy's attack hitboxes live in its HurtData at `ed+0x410`, built by
`EventActor_HurtDataCreate` (0x80201ee8): **2 attack regions for normal enemies, 8 for Dyna
Blade**. It sets the HurtData `on_damage_callback` (`+0x8C`) to
`EventActor_OnDamageCallback` (0x80201c78) and builds the defensive sub-regions from the
actor's joint descriptor.

Per attack frame the params are refreshed from the current animation frame's hurt descriptor
into the TriggerData at `ed+0x45C` by `EventActor_RefreshAttackParams` (0x80201ba4). Enabling
is data-driven via `Trigger_SetState1` when the animation frame carries hurt data; disabling is
animation-script command 13.

**Inbound and outbound are different code paths.** `EventActor_ProcHitColl` (priority 9,
0x801fc8ec) is inbound only - the enemy as victim, tested against the rider/machine/enemy/hazard
hurtdata lists. The enemy's outbound attack on a rider is delivered on the **machine** side:
`Machine_CheckEventCollision` (0x801d71ec) reads the enemy's `ed+0x410` attack regions as the
attacker against the machine's HurtData (`MachineData+0x660`). Riders are damaged through their
machine; there is no rider-versus-enemy collision check.

## Movement

### Spline path following

`EnemyPath_FollowUpdate` (0x80209ce4) is the primary mover for ground enemies, following the
splines embedded in stage geometry. It reads the parametric position from `ed+0x5FC`
(0.0-1.0), calls `splArcLengthPoint` for world positions at the current and a nearby point,
derives forward from the difference, stores the movement direction into `ed+0x664`, and calls
`zz_8020a9dc_` to rebuild the up/right/forward axes. Its `param_10` selects the direction mode:
0 = forward on init, 1 = continue forward, 2/3 = lateral.

`EnemyPath_Init` (0x80206e2c) attaches an enemy to a path: `Spline_FindNearest` (0x800cf07c)
picks the closest spline, the path ID lands in `ed+0x5DC` and the parametric position in
`ed+0x5FC`, and `Spline_GetForward`/`Spline_GetBackward` (0x800cf3ac/0x800cf44c) supply the
neighbour pointers.

`EnemyPath_Advance` (0x8020a040) advances the parametric position each frame by the movement
speed, hands off to `zz_802070e8_` at a segment end to move onto the next connected spline,
and interpolates between the position and up splines for smooth terrain following.

### Ground physics

Two variants. `Enemy_GroundPhysicsVelocity` (0x80209104) projects velocity forward, raycasts
for ground (validated through `PointCollision_EnsureIDValid_`) and adjusts height to the
surface. `Enemy_GroundPhysicsSurface` (0x802096b4) advances position along the surface normal
instead, bouncing off walls via `VEC_Reflection` with the reflected velocity scaled by
`1.5 * movement_speed * spline_scale`, then snaps with friction. Both skip the first two frames
after a spawn (warmup counter `ed+0x880`) while position and normal stabilize, compute movement
direction from the position delta, raycast ahead, snap to ground, and finish through
`Enemy_GroundAttach` (0x8020a664).

Height above ground is smoothed, not snapped: target at `ed+0x864`, current at `ed+0x868`,
lerped 0.2 per frame (about 5 frames to settle). The slope factor (`ed+0x95C`, which scales the
ground projection ray distance) uses the same 0.2 lerp.

`EventActor_GroundSnap` (0x80204fac) is the one-shot version used by walking enemies and init
callbacks: cast a ray downward from above the enemy using `ed+0x340` as the surface normal,
find the hit with `EnvColl_Raycast`, move to the hit point plus a height offset along the
normal, and update the stored normal.

### AI physics tick

`Enemy_AIPhysicsTick` (0x802081ec) is the central ground-follow routine used by many types in
their normal AI states. It returns 0 when it computed movement and 1 when the enemy is
stationary - several state funcs use that return as their transition trigger.

- If `ed+0x964` (movement speed) is 0.0 it returns immediately.
- With speed: check path following via `Enemy_CheckPathFollow` (0x8020b01c), ground collision
  via `Spline_FindNearest`, refresh the spline references (`ed+0x5DC/0x5FC/0x5D4/0x5D8`),
  compute the target from `EnemyPath_Advance`, build a local frame from the ground normal,
  apply banked turning with `RotateVecAroundAxis_Vec3_`, and update facing through
  `EventActor_UpdateOrientation` (0x802054e4).
- With zero speed it derives an idle wander speed from `ed+0x974` and the animation rate and
  runs a line-of-sight/distance check against the target.
- Turning and banking are gated on bit 2 of `ed+0xB0B` (the terrain-locked flag) and read turn
  rate parameters from `ed+0x3D0/0x3D4/0x3D8`.

## Player Targeting

`EnemyActor_DistToPlayer` (0x801fffa4) takes a player index and the enemy position, resolves
the rider GObj via `Ply_GetRiderGObj`, and returns the Euclidean distance, or a large sentinel
if that player has no rider.

`EnemyActor_FindNearestPlayer` (0x801ffd78) is the main targeting function. It stores the
target index in `ed+0xB24` (s16, -1 = none) with a retarget cooldown in `ed+0xB26`:

- No target: iterate all 4 players and take the nearest within the acquisition radius.
- Target with cooldown at 0: re-evaluate all players, take the nearest, reset the cooldown.
- Cooldown above 0: decrement and keep the current target.

The acquisition radius is the **global** `stc_enemy_param_table + 0x80` = 50.0 - a single
scalar, not per-tier. The cooldown is `20 + HSD_Randi(40 - 20)` from `table+0x94`/`+0x98`,
so 20-39 frames, which keeps a crowd of enemies from all retargeting on the same frame.

Once a target is acquired and `ed+0xB28 == 0.0`, the function computes the normalized direction
enemy -> player into `ed+0xB38`, points `ed+0x334` (forward) at the player and rebuilds the
orientation axes with cross products.

`EnemyActor_FindNearestPlayerFOV` (0x801ff8d8) is the FOV-aware variant. It measures the angle
between `ed+0x334` and the direction to each player with `Vec_GetAngleBetween_Vec3_` and
applies a **180-degree hemisphere test** - it rejects only players behind the enemy, not a
narrow cone. On a valid target it calls `zz_801fd878_` to aim at a bone joint via
`JOBJ_GetWorldPosition`; with no target and a model present it falls back to interpolating the
stored bone data at `ed+0x918` with factor 0.1.

`EnemyActor_RumblePlayer` (0x801ff80c) resolves the player's rider GObj and triggers controller
rumble as `(controller_idx, 2, intensity, duration)`. It does no damage - damage always flows
through the HitColl pipeline.

> **Per-enemy range copies are dead.** `Enemy_CopyParamBlock` copies `param_detect_range`
> (ed+0x378) and `param_chase_range` (ed+0x37C) out of the archive, but nothing in the enemy
> code reads either (0 references). The live acquisition radius is the global table `+0x80`;
> the live proximity bucket is `EnemyActor_ClassifyRange`, which reads detect/chase range from
> the actor_data param-root (`*(ed+0x14)+0x10`/`+0x14`). To change detection range, write the
> global table or the archive root.

## Knockback

`EnemyKnockback_Default` (0x8020bcd8) is the entry point from the priority-10 damage proc. It
reads the hurtdata hit-type field (`hurtdata+0x38`) and bails without knockback if it is above
7. Otherwise the 8-entry jump table at 0x804b2b50 maps hit type to knockback kind (identity:
type N -> kind N; the jump-table arms just load the constant into r5) and it calls
`Enemy_ApplyKnockback`.

`Enemy_ApplyKnockback` (0x8020b784) does the work. It reads the per-tier knockback parameters
from the global param table (`+0x50` launch speed, stored to `ed+0x9D8`, and `+0x60` a
secondary parameter), randomizes the direction with `HSD_Randi(8)` - a 3-bit value giving the
sign of each of X/Y/Z, so 8 possible initial directions - then dispatches on the source kind in
`ed+0x99C`:

| Kind | Source | Behavior |
|------|--------|----------|
| 0 | Normal (from HitColl) | Standard damage-driven knockback |
| 1 | From attacker position | Direction computed attacker -> enemy |
| 2, 4 | Generic default | Hardcoded direction constant, no source lookup |
| 3 | Enemy-on-enemy | Uses the attacker position at `ed+0xA24` |
| 5 | Special | Shares the kind-0/1 path; used by scripted events |

## Actor Data (.dat archives)

`Enemy_GetActorData(actor_id)` (0x801fd498) reads `{data_index, flags}` from the table at
0x804b22b4 (stride 8), checks the loaded flag at `0x8055a210[data_index]`, takes the archive
root from `0x8055a228[data_index]` and indexes it by flags (0-4 select the tier/variant
sub-entry). The result is stored in `ed+0x14`.

The sub-entry is a small pointer block:

| Offset | Purpose |
|--------|---------|
| +0x00 | **Parameter block root** - behavioral floats, bulk-copied by `Enemy_CopyParamBlock` |
| +0x04 | **Per-type secondary params** - detect/attack tuning; 16 bytes copied to `ed+0x40C` |
| +0x08 | `-1` sentinel word (param header), not joint or animation data |
| +0x0C | **Animation state table** - `EnemyAnimSeqEntry[]`, indexed by `anim_idx` |
| +0x10 | Material/texture animation data |
| +0x14 | Additional data |

`ed+0x48` (`anim_data`) resolves to `*(actor_data + 0x0C) + anim_idx * 0x10`; the entry layout
is `EnemyAnimSeqEntry` in `externals/hoshi/include/enemy.h`. `EventActor_AnimDataInit`
(0x80200c04) applies it: `HSD_JObjRemoveAnimAll`, then `HSD_JObjAddAnimAll(rootJObj, AnimJoint,
MatAnimJoint, 0)`, then `HSD_JObjReqAnimAllByFlags`. The model's root JObj comes from
`ed+0x00` (HSD container) `+0x28`, **not** from `actor_data + 0x08`.

### Parameter block

`Enemy_CopyParamBlock` (0x802006b4) bulk-copies 0xA4 bytes from `*actor_data - 4` into
`ed+0x364` through `ed+0x408`. The fields that are read somewhere in the engine:

| actor_data offset | -> EnemyData | Purpose |
|-------------------|--------------|---------|
| +0x00 | 0x368 | `param_base_scale` (also copied to `ed+0x2D0` as the tier base scale) |
| +0x10 | 0x378 | detect range - the **copy is dead**; the live read is from the archive root |
| +0x14 | 0x37C | chase range - same, dead copy |
| +0x18 | 0x380 | movement parameter |
| +0x20 | 0x388 | path speed |
| +0x34 | 0x39C | `param_speed_2`, the knockback velocity scale |
| +0x3C | 0x3A4 | `param_gravity` |
| +0x44 | 0x3AC | frame count / duration |
| +0x48 | 0x3B0 | HP threshold, compared against the damage accumulator in the priority-10 damage proc |
| +0x58 | 0x3C0 | move speed; read by ~60 sites across the enemy code, typically copied into `ed+0x95C` before a `EventActor_SetupVelocity`/`EventActor_GroundSnap` pair |
| +0x68..0x70 | 0x3D0-0x3D8 | turn-rate params read by `Enemy_AIPhysicsTick` |

Four values past the copied block are read directly out of the archive rather than from
`EnemyData`: `+0x94` turn-rate type (0 = none, 1 = orbit, 2 = fixed), `+0x98`/`+0x9C` turn rate
params, `+0xA0` knockback launch multiplier.

### Tier-0 values per enemy

From each enemy's `Em*Data.dat` param-block root. `detect`/`chase` are the `+0x10`/`+0x14`
fields `EnemyActor_ClassifyRange` reads live from the archive root.

| Enemy | base_scale | detect | chase | move_speed | gravity | frames | hp_threshold |
|-------|-----------|--------|-------|------------|---------|--------|--------------|
| Waddle Dee | 2.4 | 10 | 4 | 0.1 | 0.02 | 60 | 1 |
| Sword Knight | 3.6 | 10 | 4 | 2 | 0.025 | 60 | 1 |
| Scarfy | 4.5 | 10 | 4 | 3 | 0.025 | 60 | 1 |
| Bronto Burt | 3.2 | 10 | 4 | 1.8 | 0.02 | 60 | 1 |
| Gordo | 3.6 | 10 | 4 | 3 | 0.025 | 60 | 1e8 |
| Broom Hatter | 4 | 10 | 4 | 4 | 0.02 | 60 | 1 |
| Wheelie | 4.5 | 10 | 4 | 0.1 | 0.02 | 60 | 1 |
| TAC | 4.5 | 10 | 4 | 0.1 | 0.1 | 60 | 40 |
| Dyna Blade | 3 | 2 | 1 | 0.1 | 0.25 | 60 | 150 |

Nearly every regular enemy shares detect 10 / chase 4, distinct from the global 50.0
acquisition radius. `hp_threshold` is the damage-to-kill gate: Gordo's ~1e8 makes it
effectively invincible, TAC needs 40 and Dyna Blade 150, everything else dies at 1. Dyna Blade
is the outlier across the board (detect 2, chase 1, gravity 0.25). Higher tiers exist for most
enemies; their values are not all captured.

The per-type secondary block at `*(actor_data + 4)` is 16 bytes copied to `ed+0x40C`. For
Waddle Dee T0: attack/damage range 100.0, attack rate 0.05, damage multiplier 1.5, attack type
4.

## EnemyData Offsets That Matter Here

`EnemyData` is 0xBC0 bytes and fully declared in `externals/hoshi/include/enemy.h` - use the
header for the whole layout. The handful of fields the explanations above hinge on, and the
ones mod code writes:

| Offset | Name | Why it matters |
|--------|------|----------------|
| 0x014 | `actor_data` | Archive param root, from `Enemy_GetActorData(kind)`; the live detect/chase ranges are read through it |
| 0x020 / 0x024 | `spawn_slot` / `spawn_index` | -1 for standalone actors that do not come from the spawn pool |
| 0x034 | `state` | Current state ID, written by `EnemyStateChange` |
| 0x040 / 0x044 | `common_state_table` / `per_type_state_table` | The two halves of the state lookup |
| 0x2E0 / 0x2EC / 0x2F8 | `accel` / `vel` / `pos` | Integrated unconditionally by the priority-4 proc |
| 0x334 / 0x340 / 0x34C | `forward` / `up` / `right` | Orientation basis; targeting and the flyer movers write these |
| 0x378 / 0x37C | detect / chase range copies | Dead - nothing reads them |
| 0x654 / 0xA8C | `spline_path_ready` / `path_active_flag` | Set (1 and -1.0) before `EnemyPath_Init` to attach a spawned actor to a spline |
| 0x908 | `grounded_active` | Phase bit shared by `EnemyActor_CombatAI` and `EnemyActor_CombatMovement` |
| 0x964 / 0x974 | `movement_speed` / `idle_wander_speed` | The live speeds; `Enemy_AIPhysicsTick` early-exits when `0x964` is 0.0 |
| 0x99C | `knockback_source_kind` | Selects the direction path in `Enemy_ApplyKnockback` |
| 0xAB8-0xAC4 | `state_func1`-`state_func4` | The per-type brain, reinstalled on every state change |
| 0xAC8 | `per_type_cb` | Priority-7 dispatch, never installed by vanilla, zeroed on every state change |
| 0xB24 / 0xB26 / 0xB38 | `target_player_idx` / `retarget_cooldown` / `chase_direction` | Targeting output |
| 0xB48-0xB4E | generic s16 timers | Nominally meteor fields, reused freely (Gordo's bounce/hide cycle, Sword Knight's target lock at 0xB4C) |

## Spawn Manager

Two per-frame manager procs feed the actors this doc describes: `Enemy_Think` (0x800f3904,
Air Ride) and `Enemy_CityTrialThink` (0x800f33c0, City Trial). Both cache per-player state into
the spawn slots and scan the EventActor list for occupancy; the City Trial one additionally
makes the spawn decisions. Neither touches per-actor AI: the slot pool, spawn cadence and
weighted enemy selection sit on that side of the fence, and finished actors arrive through
`EventActor_Create`.

## Influencing Enemy Behavior

Two levers, in increasing order of intrusiveness: retune the global parameter table, or inject
per-frame logic through the dead `per_type_cb` slot. `mods/custom_ai` uses only the first.

### The global parameter table

`stc_enemy_param_table` (the hoshi macro for the pointer stored at 0x805dd878) is loaded from
`Enemy.dat`'s `emDataAll` by `Enemy_LoadCommonParams` (0x801fd580) and is NULL until a stage
with enemies loads. It is RAM-resident, so writing it retunes **all** enemies at once.

- **Distance ladder** (archive file offset 0x30): `+0x80` = 50.0 acquisition radius (read by
  `EnemyActor_FindNearestPlayer`), `+0x84` = 30.0 close, `+0x88` = 30.0, `+0x8C` = 300.0 mid,
  `+0x90` = 500.0 max/leash. The leash is the dominant range constant, read by around 15 AI
  state funcs. Raise the acquisition and leash rungs to make enemies notice riders from
  farther; drop them to make them passive.
- `+0x94`/`+0x98` retarget cooldown bounds (20/40 -> 20-39 frames). Lower is twitchy switching,
  higher locks onto one target.
- `+0x04` damage scale (0.4), `+0x08/+0x0C/+0x10` damage-tier thresholds (10/21/32),
  `+0x30/+0x40/+0x50/+0x60` per-tier knockback magnitude/scale/launch/stun - how hard enemies
  are to knock out of the arena.
- Int array at `+0x14..+0x20` = {10, 30, 50, 70}; consumer unidentified.

Per-archive detect/chase range is the other data-side lever: `EnemyActor_ClassifyRange` reads
them from the actor_data param-root (`*(ed+0x14)+0x10`/`+0x14`), shared by every enemy of that
data_index and tier, so patching the archive root scales the proximity bucket for all instances
of a type.

For speed, write the live fields `ed+0x964`/`ed+0x974`. The state funcs rewrite them each
frame, so a one-time post-spawn write is overwritten - re-assert it every frame.

### mods/custom_ai

`EnemyAI_ApplyParams` (`enemy_hook.c`) retunes the global table from an epilogue hook on
`Enemy_LoadCommonParams` at 0x801fd664 (`lwz r0,20(r1)`), by which point the table pointer is
already stored to 0x805dd878, so the hook needs no register setup. It snapshots the vanilla
values the first time it sees the table and thereafter always writes `base * mult`, which makes
re-application idempotent whether the table buffer is reloaded fresh or returned cached and
lets the "Default" preset restore the stock values exactly. `Scene_GetCurrentMajor()` picks the
Air Ride versus City Trial selection, since only Air Ride courses and the City Trial Kirby
Melee stadiums spawn pool enemies - the free-roam city has none.

Each preset is three multipliers:

| Dial | Table fields | Effect |
|------|--------------|--------|
| `range_mult` | +0x80 acquisition, +0x8C mid, +0x90 leash | How far enemies notice and pursue riders |
| `retarget_mult` | +0x94/+0x98 cooldown bounds (result clamped >= 1) | <1 twitchy switching, >1 locks onto one target |
| `knockback_mult` | +0x30/+0x40/+0x50 per-tier magnitude/scale/launch | <1 tanky, shrugs off hits |

| Preset | range | retarget | knockback |
|--------|-------|----------|-----------|
| Default | 1.0 | 1.0 | 1.0 |
| Aggressive | 1.75 | 0.6 | 1.0 |
| Relentless | 2.5 | 2.0 | 0.65 |
| Docile | 0.4 | 1.3 | 1.0 |
| Erratic | 1.15 | 0.3 | 1.0 |
| Tanky | 1.0 | 1.0 | 0.4 |
| Random | one of the five non-Default presets, rolled per load | | |

Changing the menu mid-session takes effect on the next enemy-system load - the next Air Ride
course or City Trial entry.

### Injecting per-frame logic

The cleanest hook is the `ed+0xAC8` per_type_cb slot. Vanilla never installs it, yet
`EventActor_ProcPerType` (priority 7) dispatches it every frame with `EnemyData*` in r3, so
writing a function pointer there injects custom per-frame AI without fighting any vanilla
callback. Because `EnemyStateChange` zeroes it on every transition, re-assert it - set it once
per frame from your own proc, or after each `EnemyStateChange`. From the callback you can:

- **Steer targeting** by overwriting `ed+0xB24` and `ed+0xB38` after the vanilla targeting has
  run: home on something other than a rider, or negate the chase direction to flee.
- **Pin movement** by re-asserting scaled `ed+0x964`/`ed+0x974`.
- **Force states** with `EnemyStateChange` into the type's attack or idle state.

Other options: overwrite the state callbacks `ed+0xAB8`-`ed+0xAC4` directly after spawn (again
re-asserting after each state change); add a `GObj_AddProc(gobj, cb, priority)` of your own; or
for fresh actors, drive `ed+0x2E0`/`ed+0x2EC`/`ed+0x2F8` (accel/vel/pos) directly.

`mods/custom_events/src/spawn_enemy.c` is a worked but currently uncalled example of standalone
spawning: `SpawnEnemy_Random` (a random actor near a machine, with optional `EnemyPath_Init`
spline attach) and `SpawnEnemy_MeteorTrap` (a meteor over every human player). Its
Its
`SpawnEnemy_OnBoot`, called from the mod's `OnBoot`, installs null-safety patches for
`EventActor_GetParentAnimRate` and `splArcLengthPoint`, both of which crash on the null
parent/spline pointers a standalone spawn has.

### Constraints

- State callbacks take `EnemyData*` as their sole parameter in r3.
- The physics proc always runs (`vel += accel`, `pos += vel`), so a non-physics actor must keep
  velocity and acceleration zeroed.
- `ed+0xAC8` is cleared on every state change, unconditionally - flag 0x10 protects only
  `ed+0xAD4`.
- Common states 0x00-0x0D are shared by every type and should not be overridden.
- Standalone spawns need `spawn_slot` and `spawn_index` set to -1.
- State-table function pointers are always installed by `EnemyStateChange` regardless of flags;
  flag 0x01 only skips animation setup.

## Key Functions

| Function | Address | Purpose |
|----------|---------|---------|
| `EnemyStateChange` | 0x801fc398 | State transition: ed, state_id, flags, anim_rate, anim_end_frame |
| `EventActor_ProcUpdate` | 0x801fc698 | Priority 1: animation + `state_func1` dispatch |
| `EnemyPhysicsProc` | 0x801fc6fc | Priority 4: `state_func2` + vel/pos integration |
| `EventActor_StateMachine` | 0x802017d0 | Animation-script bytecode processor |
| `EventActor_StateCleanup` | 0x801fe110 | Per-state cleanup run by `EnemyStateChange` |
| `EnemyState_AnimEnter` | 0x8020bd68 | States 0x00-0x08 func1: anim rate, stun freeze |
| `EnemyState_AnimTick` | 0x8020be1c | States 0x00-0x08 func2: alive/dead branch, attraction physics |
| `EnemyState_AnimExit` | 0x8020c558 | States 0x00-0x08 func3: stun countdown, spark VFX |
| `EnemyState_DeathEnter` | 0x80203e60 | State 0x09: death sequence, next-frame destroy |
| `EnemyState_InhaledFunc4` | 0x80203b64 | State 0x0A func4: mouth-bone tracking, shrink toward rider |
| `EnemyState_KnockbackEnter/Tick/Exit` | 0x8020ddb4 / 0x8020de90 / 0x8020dfe8 | State 0x0B |
| `EnemyState_LaunchedTick/Exit` | 0x8020e338 / 0x8020e61c | State 0x0C |
| `EnemyState_SlidingTick/Exit` | 0x8020e7e0 / 0x8020e954 | State 0x0D |
| `EnemyKnockback_Default` | 0x8020bcd8 | Hit type -> knockback kind via the table at 0x804b2b50 |
| `Enemy_ApplyKnockback` | 0x8020b784 | Applies the knockback: per-tier params, random direction, kind dispatch |
| `EnemyActor_DistToPlayer` | 0x801fffa4 | Enemy-to-player distance |
| `EnemyActor_RumblePlayer` | 0x801ff80c | Controller rumble, no damage |
| `EnemyActor_FindNearestPlayer` | 0x801ffd78 | Nearest rider within the 50.0 acquisition radius, 20-39 frame cooldown |
| `EnemyActor_FindNearestPlayerFOV` | 0x801ff8d8 | Same plus forward-hemisphere test and bone aim |
| `EnemyActor_PlayerAheadDist` | 0x801fea60 | Per-player signed forward distance + behind flag |
| `EnemyActor_FindNearestPlayerAhead` | 0x801fe5d4 | Cone scanner (Waddle Dee) |
| `EnemyActor_FindPlayerInRangeFwd` | 0x801fe764 | Cone scanner (TAC grab probe) |
| `EnemyActor_FindDiveTarget` | 0x801fe8dc | Cone scanner (Bronto Burt dive gate) |
| `EnemyActor_ClassifyRange` | 0x80206cc0 | Proximity bucket from the archive detect/chase ranges |
| `EnemyActor_CombatAI` | 0x802069e8 | Two-phase grounded movement |
| `EnemyActor_CombatMovement` | 0x8020b490 | Two-phase spline/ground movement |
| `EnemyActor_GroundFollowMovement` | 0x80208bd4 | Ground-following chase physics |
| `EnemyActor_FlyMovement` | 0x8020354c | Flyer mover: anchored sin-hover or homing steer |
| `EnemyActor_FlyForward` | 0x8020335c | Flyer mover: advance along facing at speed |
| `Enemy_AIPhysicsTick` | 0x802081ec | Ground-following movement + pathfinding; returns 1 when stationary |
| `EnemyPath_Init` | 0x80206e2c | Attach to the nearest spline |
| `EnemyPath_FollowUpdate` | 0x80209ce4 | Spline path-following movement |
| `EnemyPath_Advance` | 0x8020a040 | Advance the parametric position |
| `Enemy_GroundPhysicsVelocity` | 0x80209104 | Velocity-based ground projection |
| `Enemy_GroundPhysicsSurface` | 0x802096b4 | Surface advancement, wall bounce |
| `Enemy_GroundAttach` | 0x8020a664 | Final ground attachment |
| `EventActor_GroundSnap` | 0x80204fac | Raycast + snap to ground |
| `EventActor_UpdateOrientation` | 0x802054e4 | Recalculate the orientation basis |
| `EventActor_SetupVelocity` | 0x80205310 | Configure movement speed/direction |
| `EventActor_HurtDataCreate` | 0x80201ee8 | Build the attack/hurt regions |
| `EventActor_RefreshAttackParams` | 0x80201ba4 | Refresh attack params from the animation frame |
| `EventActor_OnCapture` | 0x802038c4 | Inhale entry: capture flags, descriptor +0x1C, state 0x0A |
| `Enemy_SetTerrainLocked` | 0x8020ae54 | Sets bit 2 of `ed+0xB0B`; unlock sibling at 0x8020ae68 |
| `Enemy_GetActorData` | 0x801fd498 | Resolve actor_data by ID |
| `Enemy_LoadCommonParams` | 0x801fd580 | Load `Enemy.dat` `emDataAll`, store the table pointer to 0x805dd878 |
| `SwordKnight_Init` | 0x802111d8 | SpawnChild (rider 0x49) + landing callbacks + FinalizeInit |
| `SwordKnight_BeginCombat` | 0x80211444 | SetupVelocity + GroundSnap + state 0x0F + path attach |
| `SwordKnight_State0FDecide` | 0x80211520 | Player-crossing slash decision |
| `SwordKnight_TriggerAttack` | 0x80211734 | State change into the slash |
| `Scarfy_TargetFOV` | 0x8021027c | FOV targeting with the global detection range |
| `Tac_Init` / `Tac_ScatterItems` | 0x8021a534 / 0x8021c8ec | TAC init; loot scatter |
| `DynaBlade_Init` / `DynaBlade_StateDive_Proc` / `DynaBlade_ThrowItems` | 0x8021c9dc / 0x8021d500 / 0x8021db44 | Dyna Blade init, anim-driven swoop, item rain |
| `Enemy_Think` / `Enemy_CityTrialThink` | 0x800f3904 / 0x800f33c0 | Per-frame spawn managers |

## Data Addresses

| Data | Address | Description |
|------|---------|-------------|
| Common state table | 0x804b2950 | 14 entries (states 0x00-0x0D), 0x14 bytes each |
| Per-type descriptor table | 0x804b1d98 | 79 pointers by ActorID, 47 unique descriptors |
| Actor data table | 0x804b22b4 | `{data_index, flags}` per ActorID, stride 8 |
| Archive loaded flags | 0x8055a210 | One byte per data_index (22 entries) |
| Archive root pointers | 0x8055a228 | One pointer per data_index (22 entries) |
| Enemy parameter table pointer | 0x805dd878 | Holds a pointer to the `emDataAll` block; NULL until enemies load |
| Animation script table (enemy) | 0x804b26b0 | Commands 11-25, 12-byte entries |
| Animation script table (HSD) | 0x80499628 | Generic commands 0-10 |
| Knockback jump table | 0x804b2b50 | 8 entries mapping hit type to knockback kind |
