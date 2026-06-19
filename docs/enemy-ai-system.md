# Enemy AI System

Documentation of the enemy AI and behavioral systems in Kirby Air Ride (GKYE01). Covers the state machine, movement, targeting, per-type callbacks, animation scripts, and data structures needed to create custom enemy AI.

See [enemy-spawn-system.md](enemy-spawn-system.md) for the actor creation pipeline, descriptor struct, archive loading, and spawn-slot system.
See [meteor-actor.md](meteor-actor.md) for meteor-specific behavior.

## Architecture Overview

Enemy behavior is driven by three systems running in parallel each frame:

1. **State Machine** -- selects which behavior functions run via a two-level state table
2. **GObj Proc Chain** -- 10 prioritized callbacks (priorities 0, 1, 4, 5, 6, 7, 8, 9, 10, 21) executing physics, AI, collision, rendering
3. **Per-Type Callbacks** -- polymorphic function tables giving each enemy type unique behavior

```
EnemyStateChange(ed, state_id, flags, anim_rate, anim_end_frame)
    |
    +-- Looks up state table entry (common or per-type)
    +-- Optionally sets animation from entry (unless flag 0x01)
    +-- ALWAYS installs 4 function pointers into EnemyData:
            ed+0xAB8 (func1) <- called by priority 1 proc
            ed+0xABC (func2) <- called by priority 4 proc
            ed+0xAC0 (func3) <- called by priority 5 proc
            ed+0xAC4 (func4) <- called by priority 6 proc
```

These four state-table function pointers **are** the per-type AI brain. There is no separate "AI callback": `ed+0xAC8` (per_type_cb), dispatched at priority 7, is a **dead slot** — no enemy ever installs it (a region-wide scan of all enemy code 0x801fb000–0x8021f000 finds only the zero-store in `EnemyStateChange` at 0x801fc634 and the read in `EventActor_ProcPerType` at 0x801fc85c). See [Per-Type AI Decision Architecture](#per-type-ai-decision-architecture) for how the state funcs compose targeting + movement, and [Influencing Enemy Behavior](#influencing-enemy-behavior) for why the dead per_type_cb is the cleanest injection point.

## GObj Proc Execution Order

`EventActor_Create` registers these procs on every enemy GOBJ. They execute in priority order every frame:

| Priority | Address | Name | Purpose |
|----------|---------|------|---------|
| 0 | 0x801fc670 | `EventActor_ProcResetDamage` | Zero per-frame damage accumulators |
| 1 | 0x801fc698 | `EventActor_ProcUpdate` | HSD animation advance, animation script machine, state func1 dispatch |
| 4 | 0x801fc6fc | `EnemyPhysicsProc` | state func2 dispatch, `vel += accel`, `pos += vel`, OOB floor kill (skipped for actor_id >= 0x4C) |
| 5 | 0x801fc7c4 | `EventActor_ProcStateActive` | state func3 dispatch -- main per-state AI logic |
| 6 | 0x801fc7f8 | `EventActor_ProcSharedModel` | Shared model update (shadow, 3D transform), state func4 dispatch |
| 7 | 0x801fc848 | `EventActor_ProcPerType` | `ed+0xAC8` (per_type_cb) dispatch (if non-null), HurtData update, position snap |
| 8 | 0x801fc8e8 | `EventActor_ProcHitCollInit` | Single `blr` -- no-op stub, does nothing |
| 9 | 0x801fc8ec | `EventActor_ProcHitColl` | HitColl processing + collision checks |
| 10 | 0x801fc9f0 | `EventActor_ProcDamage` | Reads HurtData output, calls `giveEnemyDamage` (0x8020b680), dispatches hit-reaction callback `ed+0xAD0` |
| 21 | 0x801fcabc | `EventActor_ProcFinal` | `pos` to `pos_prev`, ground state flags, lifetime/despawn checks |

### Priority 1 (ProcUpdate) Flow

```
EventActor_ProcUpdate(gobj):
    ed = gobj->userdata
    HurtData_UpdatePerFrame(ed->hurtdata)     // advance HSD animation
    VFX_Update(ed->vfx_a3c)                   // update particle effects
    EventActor_StateMachine(ed)               // process animation script bytecode
    PostUpdate(ed)                            // finalize animation frame
    if (ed->state_func1)
        ed->state_func1(ed)                   // per-state callback
```

## Animation Script Bytecode System (0x802017d0)

Enemies have an animation script bytecode system that drives animation-triggered behaviors (play sound at frame N, spawn VFX, transition state on anim end, etc.).

### Script State (ed+0x4C)

```c
typedef struct AnimScriptState {  // ed+0x4C
    float timer;          // +0x00 (ed+0x4C): Countdown timer. Initialized to -1.0 for immediate first-command execution.
    float frame;          // +0x04 (ed+0x50): Current animation frame accumulator (= ed+0x2A8 + ed+0x2AC each frame), NOT a rate
    void *script_ptr;     // +0x08 (ed+0x54): Current bytecode pointer. NULL = stopped.
    int   loop_depth;     // +0x0C (ed+0x58): Current nesting depth in loop stack
    // loop_stack[] follows
} AnimScriptState;
```

### Execution Loop

Each frame, the timer decrements. When timer <= 0.0, the next command byte is read. The command ID is extracted as `(byte >> 2) & 0x3F` — i.e. the upper 6 bits of the byte, not the low 6. The low 2 bits carry a separate sub-field.

`EventActor_StateMachine` dispatches each command by first calling the generic HSD handler `zz_80068d74_` with the command ID. That function handles commands 0-10 via a 4-byte-entry jump table at 0x80499628 (`cmd < 11` → `table[cmd]`, returns 1) and returns 0 for `cmd >= 11`. When it returns 0, the state machine falls back to the enemy command table at 0x804b26b0, indexed by `(cmd - 11) * 12`.

### Builtin Commands (0-10) -- Jump Table at 0x80499628

These are generic HSD animation/timing commands shared across the engine.

| Cmd | Name | Purpose |
|-----|------|---------|
| 0 | Stop | Sets `script_ptr = NULL`, halting the script |
| 1 | Wait(frames) | Sets timer to a frame count for countdown |
| 2 | WaitUntil(frame) | Jumps timer to a specific animation frame |
| 3 | LoopBegin(count, addr) | Pushes loop counter and return address onto loop stack |
| 4 | LoopEnd | Decrements loop counter; jumps back or pops stack when done |
| 5 | Call(addr) | Subroutine call -- pushes return address, jumps to addr |
| 6 | Return | Pops return address from stack, resumes caller |
| 7 | Goto(addr) | Unconditional jump to bytecode address |
| 8 | SetTimerInfinite | Pauses script indefinitely (timer set to huge value) |
| 9 | PlaySFX(id) | Plays a sound effect by ID |
| 10 | StopSFX | Stops the current sound effect |

### Enemy-Specific Commands (11-25) -- Table at 0x804b26b0

**Calling convention.** The dispatcher `EventActor_StateMachine` (0x802017d0) loads the opcode byte from the script pointer (script-state struct at `ed+0x4c`, current script ptr at `+0x08`). The command is `(byte >> 2) & 0x3F`; the low 2 bits are an inline sub-selector consumed by some handlers. Commands 0-10 dispatch through the generic HSD table at 0x80499628. Commands `>= 11` index the enemy table at 0x804b26b0 as **12-byte entries `{word0 = handler, word1, word2 = operand-word-count}`**; dispatch is `word0`. Handlers receive **r3 = `EnemyData*`** and **r4 = `ed+0x4c`** (the script-state struct); they read operands via the script ptr at `+0x08`, advancing it `+4` per operand word.

The callable range is exactly **cmd 11-25 (15 handlers)**. The trailing slots are non-callable: index 15 (cmd 26) `word0 = NULL`, index 16 (cmd 27) `word0 = 0x01000000`, index 17 (cmd 28) `word0 = 0xFFFFFFFF`. The 0x80201928/0x80201948 values that appear in that region are leftover thunk pointers sitting in the `word1` column and are never dispatched.

| Cmd | Handler | Purpose | Proposed name |
|-----|---------|---------|---------------|
| 11 | 0x80200e58 | Set attribute flag bits in `ed+0xB00` and a float in `ed+0xB04` | `EnemyAnimCmd_SetAttrFlag` |
| 12 | 0x80200eb8 | Define/emit a HitColl attack-box descriptor: decodes ~6 operand words (int16 offsets/sizes → float-scaled + packed flag bits) into a collision-box descriptor and emits it into the enemy's HurtData (`ed+0x410`), indexing a fn-pointer table at `ed+0x2b4` | `EnemyAnimCmd_SetHitDesc` |
| 13 | 0x80201138 | Disable a HitColl box by index (operand `& 0x03FFFFFF`) via `Hit_SetInactive` on an `ed+0x410` region | `EnemyAnimCmd_DisableHit` |
| 14 | 0x80201180 | Flush/deactivate the pending HitColl boxes (iterates the `ed+0x410` box array, `Hit_SetInactive` on each, clears the enable flag) | `EnemyAnimCmd_FlushHitColls` |
| 15 | 0x802011bc | Spawn a projectile / sub-actor (optional random variant; decodes transform operands, calls sub-actor spawn 0x8020c738) | `EnemyAnimCmd_SpawnSubActor` |
| 16 | 0x80201418 | Broadcast controller rumble (per human player) | `EnemyAnimCmd_Rumble` |
| 17 | 0x80201488 | No-op: advance script ptr 1 word | `EnemyAnimCmd_Nop` |
| 18 | 0x80201498 | Spawn/refresh a persistent particle effect (`Effect_SpawnSync`; handle stored to `ed+0xA70`/`ed+0xA74`, prior one destroyed) | `EnemyAnimCmd_SpawnEffect` |
| 19 | 0x80201504 | No-op: advance script ptr 3 words | `EnemyAnimCmd_Nop3` |
| 20 | 0x8020152c | Play a sound/voice (2-bit selector + word arg; AudioEmitter from `ed+0xA58[]`, handle cached to `ed+0xA60`, sentinel `ed+0xA68`) | `EnemyAnimCmd_PlaySound` |
| 21 | 0x80201578 | Play a random 1-of-N sound (`HSD_Randi` pick, then same audio path as cmd 20) | `EnemyAnimCmd_PlayRandSound` |
| 22 | 0x802016ac | Store a 24-bit script constant into one of `ed+0xAF0/0xAF4/0xAF8/0xAFC` (low-2-bit selector) | `EnemyAnimCmd_SetConst` |
| 23 | 0x8020172c | Set event/state flag bit (bit 1 of byte `ed+0xB0A`) | `EnemyAnimCmd_SetEventBit` |
| 24 | 0x8020174c | Apply a color animation (`ColAnim_Apply` on the ColAnim component at `ed+0x70`) | `EnemyAnimCmd_ApplyColAnim` |
| 25 | 0x80201798 | Reset the color animation (`ColAnim_Reset` on `ed+0x70`) | `EnemyAnimCmd_ResetColAnim` |

## State Machine

### EnemyStateChange (0x801fc398)

```c
void EnemyStateChange(EnemyData *ed, int state_id, int flags, float anim_rate, float anim_end_frame);
```

Parameters:
- `ed` (r3): Enemy data pointer
- `state_id` (r4): Target state ID
- `flags` (r5): Bitmask controlling transition behavior
- `anim_rate` (f1): Animation playback rate
- `anim_end_frame` (f2): Animation end frame override

**State table lookup:**
```c
if (state_id < 0x0E)
    entry = common_state_table[state_id];         // ed+0x40, table at 0x804b2950
else
    entry = per_type_state_table[state_id - 0x0E]; // ed+0x44, from per-type descriptor
```

Each entry is 0x14 bytes:

```c
typedef struct StateTableEntry {
    int   anim_idx;   // +0x00: Animation index (-1 = none)
    void *func1;      // +0x04: -> ed+0xAB8, called from priority 1 (ProcUpdate)
    void *func2;      // +0x08: -> ed+0xABC, called from priority 4 (pre-physics)
    void *func3;      // +0x0C: -> ed+0xAC0, called from priority 5 (state active / main AI)
    void *func4;      // +0x10: -> ed+0xAC4, called from priority 6 (shared + model)
} StateTableEntry;    // 0x14 bytes
```

**Flags bitmask (r5):**

| Bit | Mask | Effect |
|-----|------|--------|
| 0 | 0x01 | Skip **animation setup** (steps 10a-10h). Function pointers are ALWAYS installed regardless of this flag. |
| 1 | 0x02 | Skip animation reset if anim_idx unchanged |
| 2 | 0x04 | Skip cleanup function (`EventActor_StateCleanup`, 0x801fe110) |
| 3 | 0x08 | Save/restore position (ed+0x538-0x540) across transition |
| 4 | 0x10 | Skip clearing **ed+0xAD4 only** (ed+0xAC8 is cleared unconditionally regardless of this flag) |
| 5 | 0x20 | Skip HurtData animation reset |
| 6 | 0x40 | Skip **SFX handle cleanup** (ed+0xA3C/0xA40) -- otherwise calls `EventActor_CleanupVfxA3C`/`EventActor_CleanupVfxA40` (0x8020c6e0/0x8020c70c), which invoke sound stop at 0x80236358 |

**Internal flow (13 steps):**

1. Write `state_id` to `ed+0x34`
2. Check special actor grounded flag (bit 3 of `ed+0xB0B`) for Gordo, event actors
3. Clean up SFX handles at ed+0xA3C/0xA40 (unless flag 0x40) -- calls `EventActor_CleanupVfxA3C` (0x8020c6e0) and `EventActor_CleanupVfxA40` (0x8020c70c), which invoke sound stop at `0x80236358`
4. Save position if flag 0x08
5. Run cleanup function `EventActor_StateCleanup` (0x801fe110) (unless flag 0x04)
6. Update HSD animation scale
7. Reset HurtData (unless flag 0x20)
8. Look up state table entry from common or per-type table
9. Install anim_idx to `ed+0x3C`, set up animation data pointer at `ed+0x48`
10. Animation setup (skipped if flag 0x01):
    - 10a. Conditional animation reset based on anim_idx change
    - 10b. Set animation playback rate from `anim_rate` parameter (f1)
    - 10c. Set animation end frame from `anim_end_frame` parameter (f2)
    - 10d. Configure HSD animation object
    - 10e. Reset material animation
    - 10f. Initialize animation script state
    - 10g. Set script timer to -1.0 (causes immediate first-command execution)
    - 10h. Begin script execution
11. Restore position if flag 0x08
12. Copy func1-4 from state table entry to `ed+0xAB8-0xAC4` -- **always happens, regardless of flag 0x01**
13. Clear `ed+0xAC8` (per-type cb) to 0 **unconditionally**; clear `ed+0xAD4` to 0 **unless flag 0x10** (the `ed+0xAC8` store is unconditional, only the `ed+0xAD4` store is guarded by the flag-0x10 branch)

### Common State Table (0x804b2950)

14 entries for states 0x00-0x0D. Shared by all actor types.

| State | Anim | func1 | func2 | func3 | func4 | Purpose |
|-------|------|-------|-------|-------|-------|---------|
| 0x00 | 0 | 0x8020bd68 | 0x8020be1c | 0x8020c558 | NULL | Animation state 0 |
| 0x01 | 1 | 0x8020bd68 | 0x8020be1c | 0x8020c558 | NULL | Animation state 1 |
| 0x02 | 2 | 0x8020bd68 | 0x8020be1c | 0x8020c558 | NULL | Animation state 2 |
| 0x03 | 3 | 0x8020bd68 | 0x8020be1c | 0x8020c558 | NULL | Animation state 3 |
| 0x04 | 4 | 0x8020bd68 | 0x8020be1c | 0x8020c558 | NULL | Animation state 4 |
| 0x05 | 5 | 0x8020bd68 | 0x8020be1c | 0x8020c558 | NULL | Animation state 5 |
| 0x06 | 6 | 0x8020bd68 | 0x8020be1c | 0x8020c558 | NULL | Animation state 6 |
| 0x07 | 7 | 0x8020bd68 | 0x8020be1c | 0x8020c558 | NULL | Animation state 7 |
| 0x08 | 8 | 0x8020bd68 | 0x8020be1c | 0x8020c558 | NULL | Animation state 8 |
| 0x09 | 9 | 0x80203e60 | NULL | NULL | NULL | Death/despawn |
| 0x0A | 10 | NULL | 0x80203a50 | 0x80203b28 | 0x80203b64 | Inhaled/absorbed by Kirby |
| 0x0B | 11 | 0x8020ddb4 | 0x8020de90 | 0x8020dfe8 | NULL | Knockback/hit reaction |
| 0x0C | 12 | NULL | 0x8020e338 | 0x8020e61c | NULL | Launched/airborne |
| 0x0D | 13 | NULL | 0x8020e7e0 | 0x8020e954 | NULL | Grounded/sliding |

### Common State Callbacks -- Detailed Analysis

**States 0x00-0x08 (EnemyState_AnimEnter / AnimTick / AnimExit):**

All nine animation states share the same three functions. These are the **normal behavioral states** -- the anim_idx selects different animations (idle, walk variants, attack, etc.) while per-type callbacks handle the actual AI logic.

- **func1 (AnimEnter, 0x8020bd68):** Manages animation playback rate. During stun, freezes the animation. Otherwise, uses a decaying `anim_speed_scale` (ed+0xA2C) with a minimum clamp to prevent animations from stopping entirely.

- **func2 (AnimTick, 0x8020be1c):** The most complex shared callback. Branches based on HP:
  - **HP > 0 (alive):** Processes attraction/inhale physics with three modes:
    - Normal: enemy is attracted toward a player (pulled by inhale suction)
    - Captured: enemy is fully captured, follows the rider
    - Skip: no attraction processing
    Also handles player-ride attachment mechanics.
  - **HP <= 0 (dead):** On the first frame of death: stops all SFX, zeros velocity, plays death effects (VFX + sound), calls `custom_death_callback` (ed+0xAEC) if set or a default death handler. On subsequent frames: tracks `death_frame_counter` (ed+0x9C8) and destroys the actor after the threshold is exceeded.

- **func3 (AnimExit, 0x8020c558):** Stun management. If `stun_frames` (ed+0xA18) > 0: runs stun animation update. If stun <= 0 and `death_timer` == 0: returns to idle state. Decrements stun_frames each frame. Every 5th frame: spawns stun spark VFX (SFX ID 0x27C6).

**State 0x09 (Death, EnemyState_DeathEnter, 0x80203e60):**

Only func1 is set (enter-only state). On entry:
1. Hides the model (sets render flags)
2. Plays death SFX (from `death_sfx_id`, ed+0x9C0) and VFX (from `death_vfx_id`, ed+0x9C4)
3. Handles special actor cleanup
4. Sets `death_frame_counter` (ed+0x9C8) to 600
5. Increments counter each frame -- since 601 > 120 (the destruction threshold), the actor is destroyed on the **next frame** after entering the death state

Death is effectively instantaneous (1 frame).

**State 0x0A (Inhaled):**

Handles being sucked in by Kirby's inhale.
- **func2 (0x80203a50):** Updates attachment slot positions relative to the parent rider
- **func3 (0x80203b28):** Increments `inhale_timer` (ed+0xB20), destroys the actor when timer exceeds 120 frames (2 seconds)
- **func4 (0x80203b64):** Tracks the inhaling rider's mouth bone via `JOBJ_GetWorldPosition`. Computes distance and scale interpolation, progressively shrinking the enemy toward the rider's mouth.

**State 0x0B (Knockback, EnemyState_KnockbackEnter/Tick/Exit):**

Hit reaction state entered when an enemy takes a significant hit.
- **func1 (0x8020ddb4):** Computes knockback velocity from direction fields (ed+0x334) scaled by `kb_speed_mult` (ed+0x878) and `param_speed_2` (ed+0x39C). Sets launch trajectory.
- **func2 (0x8020de90):** If a hit-source GObj exists (ed+0x950), follows its position and orientation with interpolation. Otherwise updates orientation independently.
- **func3 (0x8020dfe8):** Recovery logic. Decrements `recovery_timer` (ed+0x888). Tries to find a landing spot via ground collision raycast. On success: transitions to state 0x0C (launched). On failure: calls `EventActor_Destroy`.

**State 0x0C (Launched/Airborne, EnemyState_LaunchedTick/Exit):**

- **func2 (0x8020e338):** Spline-based projectile motion. Tracks progress via `launch_time_accum` (ed+0x8A8) and `launch_time_step` (ed+0x8AC). When the endpoint is reached: transitions to state 0x0D with grounded physics enabled. Applies a decaying bounce velocity (`bounce_vel` at ed+0x8F0).
- **func3 (0x8020e61c):** Ground shadow update and common physics tick.

**State 0x0D (Grounded/Sliding, EnemyState_SlidingTick/Exit):**

- **func2 (0x8020e7e0):** Friction and deceleration on the ground. Computes `friction = constant / slide_timer` (ed+0x8EC). Interpolates orientation toward ground-aligned axes. Velocity derived from spline direction plus bounce_vel.
- **func3 (0x8020e954):** Ground collision response. Updates surface normal and snaps to ground.

### Per-Type State Tables

States 0x0E+ are type-specific. Each enemy type has its own state table pointed to by the per-type descriptor. State IDs are relative: state 0x0E = entry 0, state 0x0F = entry 1, etc.

T1 and T2 variants of the same enemy always share the same descriptor pointer (so they have identical per-type state tables).

> **Per-type state-table entry counts and the "-1" entry.** Each per-type table is an array of 0x14-byte entries, the same format as the common table. The **first entry (relative index 0) is reached by state ID 0x0E**, and its `anim_idx` is -1 (the default/spawn entry). The *behavioral* descriptions of individual states below are partial/inferred.

**Waddle Dee (0x17)** -- state table at 0x804b3e78, init_cb 0x80219448, **5 entries (states 0x0E-0x12)**. Fully traced — **and it is NOT a pure patroller: it detects and lunges at riders.**

| State | Entry | Anim | func1 | func2 | func3 | func4 | Purpose (verified) |
|-------|-------|------|-------|-------|-------|-------|--------------------|
| 0x0E | 0 | -1 | 0x80219638 | – | – | – | Spawn — ZeroVelocity, `EnemyPath_FollowUpdate`, →0x0F |
| 0x0F | 1 | 0x0E | – | – | 0x802196e0 (ground move) | 0x80219704 **player scan** | Patrol a spline **while scanning**; func4 calls `zz_801fe5d4_` (0x801fe5d4, nearest-rider-in-front) — on a hit →0x10 |
| 0x10 | 2 | 0x0F | 0x802197ac | 0x802197e8 | 0x802197ec | – | Windup / turn toward target → 0x11 |
| 0x11 | 3 | 0x10 | 0x80219908 | 0x80219988 | 0x80219a48 | 0x80219a84 | Lunge / hop attack (accumulates a swing into ed+0xB54, adjusts height) → 0x12 |
| 0x12 | 4 | 0x11 | 0x80219b4c | – | 0x80219c40 | 0x80219c64 | Recover / settle → `EnemyPath_FollowUpdate` → 0x0F |

Waddle Dee AI loop: **patrol a spline (scanning for a rider in front) → detect → windup/turn → lunge-hop → recover → patrol.** (The old "spawn → idle → random walk → idle" description was wrong; there is no idle state and there *is* a player-reactive attack chain.)

**Sword Knight (0x05)** -- state table at 0x804b3118, **4 entries (states 0x0E-0x11)**. Fully traced -- see [Per-Type AI Decision Architecture](#per-type-ai-decision-architecture) § Worked example: Sword Knight:

| State | Entry | Anim | Purpose (verified) |
|-------|-------|------|--------------------|
| 0x0E | 0 | -1 | Default/spawn -- init then `EnemyStateChange`→0x0F (func1 only) |
| 0x0F | 1 | 0x0E | Seek / slash-watch -- func1 decides, func2/3 move (CombatMovement/CombatAI) |
| 0x10 | 2 | 0x0F | Attack (slash) -- entered by `SwordKnight_TriggerAttack` |
| 0x11 | 3 | 0x10 | Recover |

Sword Knight AI loop: spawn → patrol a spline → slash when a rider crosses its front → recover → patrol.

**Wheelie (0x08)** -- state table at 0x804b3350, init_cb 0x802132ec, **3 entries (states 0x0E-0x10)** (verified). Init calls `Enemy_SetTerrainLocked` (0x8020ae54) — **not** "sets the ground flags directly".

| State | Entry | Anim | Purpose (verified) |
|-------|-------|------|--------------------|
| 0x0E | 0 | -1 | Spawn -- SetupVelocity + `EnemyPath_FollowUpdate`, →0x0F (func1 only) |
| 0x0F | 1 | 0x0E | Roam -- func2 `Enemy_AIPhysicsTick` (0x802081ec); when it reports stationary →0x10. func3 `EnemyActor_CombatAI` |
| 0x10 | 2 | 0x0F | Drive -- func2 `EnemyActor_GroundFollowMovement` (0x80208bd4); when done re-setup →0x0F. func3 `EnemyActor_CombatAI` |

No player targeting — Wheelie is a roam/drive ground wanderer.

**Gordo (0x0E)** -- state table at 0x804b3808, init_cb 0x80215a00, **3 entries (states 0x0E-0x10)** (verified). Init is just `EventActor_FinalizeInit`; the spawn func sets `grounded_active` (ed+0x908) = 1 **directly** (it does **not** call `Enemy_SetTerrainLocked`).

| State | Entry | Anim | Purpose (verified) |
|-------|-------|------|--------------------|
| 0x0E | 0 | -1 | Spawn -- ZeroVelocity, DisableRendering, ed+0x908=1, →0x0F (func1 only) |
| 0x0F | 1 | 0x0E | Bounce — func1 is a frame-gated timer (ed+0xB48) that flips to 0x10 at a random threshold; func4 `EnemyActor_FindNearestPlayerFOV` (homes facing toward nearest rider). **No code writes pos.y** — the visible vertical bounce is the looping animation (anim_idx 0x0E), not a `pos.y +=`. |
| 0x10 | 2 | 0x0F | Hide/timer — func1 counts ed+0xB4A down (model hidden, velocity zeroed) → back to 0x0F; func4 same FOV homing. |

Gordo "oscillates" only as a **state/animation/visibility cycle** (0x0F↔0x10 driven by the timers), not a code-driven Y oscillation.

**Broom Hatter (0x00)** -- state table at 0x804b2d88, init_cb 0x8020ea44, **4 entries (states 0x0E-0x11)** (verified). Composite (init `EventActor_SpawnChild`s the SP Broom Hatter rider, actor 0x48) and terrain-locked via `Enemy_SetTerrainLocked` (0x8020ae54).

| State | Entry | Anim | Purpose (verified) |
|-------|-------|------|--------------------|
| 0x0E | 0 | -1 | Spawn — `EnemyPath_FollowUpdate`, GroundSnap, →0x0F (func1 only) |
| 0x0F | 1 | 0x0E | Spline+grounded chase — func2 `EnemyActor_CombatMovement`, func3 `EnemyActor_CombatAI` |
| 0x10 | 2 | 0x0F | Roam/recover — func2 `Enemy_AIPhysicsTick`, func3 ground helper; timer (vs 300) |
| 0x11 | 3 | 0x10 | Chase — func2 `CombatMovement`, func3 `CombatAI`; despawns on timeout/no-spline |

No player targeting — Broom Hatter is a spline/ground wanderer-chaser (same `CombatMovement`/`CombatAI` mover pair as Sword Knight).

## Per-Type AI Decision Architecture

The per-type brain is the set of **per-type state-table functions** (func1–func4 → `ed+0xAB8`–`ed+0xAC4`), dispatched at GObj proc priorities 1/4/5/6 every frame. There is **no** separate AI callback — `ed+0xAC8` (per_type_cb, priority 7) is never installed by any enemy (see Architecture Overview).

### The decision loop

On spawn an enemy enters its **default state 0x0E**, whose func1 runs a one-time init-and-launch (set up velocity, ground-snap, attach to a patrol spline) and immediately `EnemyStateChange`s into the type's **active/combat state** (typically 0x0F). From there the func1–func4 slots carry the per-frame logic. **The slot roles are not fixed across types** — each enemy distributes perceive / decide / move across whichever slots it likes. The recurring composition is:

- **One slot perceives/targets** — `EnemyActor_FindNearestPlayer`, `EnemyActor_FindNearestPlayerFOV`, or the per-player `EnemyActor_PlayerAheadDist`.
- **One slot moves** — `EnemyActor_CombatAI` / `EnemyActor_CombatMovement` (grounded chasers), `Enemy_AIPhysicsTick` (spline patrol), or a bespoke hover/drift routine (flyers).
- **State transitions** (idle→chase→attack→recover) are `EnemyStateChange` calls issued from inside those funcs when a range/timer/crossing condition trips.

### Who targets players

Player targeting is **widespread**, not exceptional. A region-wide `bl`-xref scan of the enemy code (0x801fb000–0x8021f000) gives the authoritative caller counts:

| Targeting entry point | Address | Call sites | Notes |
|---|---|---|---|
| `EnemyActor_FindNearestPlayer` | 0x801ffd78 | 13 | nearest rider within global detect range (50.0) |
| `EnemyActor_FindNearestPlayerFOV` | 0x801ff8d8 | 13 | + forward-hemisphere + bone-aim. Callers include Scarfy, Bronto Burt, Bomber, Gordo, Walky |
| `EnemyActor_PlayerAheadDist` | 0x801fea60 | 4 | per-player crossing test (Sword Knight) |
| `zz_801fe5d4_` (cone scan) | 0x801fe5d4 | 1 | **Waddle Dee** — nearest rider in a forward cone within range |
| `zz_801fe764_` (cone scan) | 0x801fe764 | — | **TAC** grab probe |
| `zz_801fe8dc_` (cone scan) | 0x801fe8dc | 2 | **Bronto Burt** dive gate |

The three `zz_801fe5d4_`/`_764_`/`_8dc_` helpers are a small family of standalone nearest-rider-in-cone scanners (each loops the 4 players via `Ply_GetRiderGObj`/`Ply_GetPosition`, distance-gates, then dot-tests against the enemy's forward axis), siblings of `FindNearestPlayerFOV` that return a player index rather than writing `ed+0xB24`.

**Corrections to the old roster:** Waddle Dee **does** target (via `zz_801fe5d4_`, contradicting "pure-patrol does not target"). Cappy and Noddy **do not** target — Cappy is a func1-only animation-driven jump-out machine (all func2/3/4 NULL) and Noddy's combat state is entirely NULL (a "sleeper"); neither appears in any targeting caller list. Sword Knight uses the lighter `EnemyActor_PlayerAheadDist` crossing test instead of a nearest-player scan.

### Worked example: Sword Knight (descriptor 0x804b3168, state table 0x804b3118)

A spline patroller that slashes when a rider passes across its front.

| State | Role | func1 (pri 1) | func2 (pri 4) | func3 (pri 5) | func4 (pri 6) |
|-------|------|---------------|---------------|---------------|---------------|
| 0x0E | spawn | 0x802113ec | – | – | – |
| 0x0F | seek / slash-watch | `SwordKnight_State0FDecide` (0x80211520) | →CombatMovement (0x80211694) | →CombatAI (0x802116c8) | shared (0x802116e8) |
| 0x10 | attack (slash) | 0x802117c0 | 0x802117e8 | →CombatAI (0x80211c14) | 0x802116e8 |
| 0x11 | recover | 0x80211ca8 | 0x80211de8 | →CombatAI (0x80211e08) | 0x802116e8 |

- **Init** (`SwordKnight_Init`, 0x802111d8): `EventActor_SpawnChild` (spawns the SP Sword Knight rider, actor 0x49), installs the landing/grounded-callback trio into `ed+0xADC/0xAE0/0xAE4`, `EventActor_FinalizeInit`.
- **Spawn** (state 0x0E func1 0x802113ec → `SwordKnight_BeginCombat` 0x80211444): `EventActor_SetupVelocity` + `EventActor_GroundSnap`, `EnemyStateChange`→0x0F, `EnemyPath_FollowUpdate` (attach to a patrol spline), reset the target lock (`ed+0xB4C = -1`) and frame counter.
- **Decide** (state 0x0F func1, 0x80211520): each frame, after a detection delay (`*(actor_data+4)`[7] frames), scans the 4 players with `EnemyActor_PlayerAheadDist` (0x801fea60) — which returns whether a player is in front of the knight's facing axis and the signed forward distance. It locks the nearest player **crossing from behind to in-front** within attack range (`*(actor_data+4)`[0]) into `ed+0xB4C`, then after a windup (`*(actor_data+4)`[1] frames) fires `SwordKnight_TriggerAttack` (0x80211734) → `EnemyStateChange`→0x10 (the slash).
- **Move** (func2 →`EnemyActor_CombatMovement`; func3 →`EnemyActor_CombatAI`): shared grounded chase/patrol movement.

### Worked example: Scarfy (descriptor 0x804b2ff8, state table 0x804b2f80)

The iconic Kirby-chaser. Scarfy puts its perception in **func4** (0x8021027c → `Scarfy_TargetFOV`), which calls `EnemyActor_FindNearestPlayerFOV` with the global detection range (`*(stc_enemy_param_table) + 0x90`) — acquiring the nearest rider inside a forward hemisphere and homing on a body bone. This shows the slot roles are type-specific: Sword Knight decides in func1, Scarfy targets in func4.

### The flyer movement archetype

Airborne enemies do **not** integrate `vel += accel` + ground-snap like grounded chasers. They compute position **directly** from an anchor, via two shared flyer movers (distinct from the grounded `EnemyActor_CombatAI`/`CombatMovement`):

| Mover | Address | Used by | What it does |
|-------|---------|---------|--------------|
| `EnemyActor_FlyMovement` (hover/wander/steer) | 0x8020354c | Scarfy, Bomber, Bronto-Burt-recover | Anchored hover. `pos = pos_initial (ed+0x310) + sin(phase)×amplitude` (mode 1, `sin` 0x800638f8), **or** a homing steer offset re-aimed via `HSD_Randf` when blocked (mode 2), plus the animation root-motion. **No gravity, no ground raycast.** Mode is `*(actor_data)+0x148`. |
| `EnemyActor_FlyForward` (straight/ballistic) | 0x8020335c | Bronto Burt cruise + dive | Builds a basis from forward/up/right (ed+0x334/0x340/0x34C) scaled by speed (ed+0x344) and **adds it straight into pos** — "advance along facing at speed". |

That `0x8020354c` is **shared** is verified: Bomber's cruise func2 (0x802156fc) and Scarfy's cruise func2 (0x80210244) both `bl 0x8020354c` identically. Flyer func3 slots still call the ground-snap/path helpers (0x80205884 / 0x80205a60) to keep the shadow + ground-height reference current while airborne; those are **not** movers. Flyers never hit `Enemy_AIPhysicsTick` except Bronto Burt's cruise (a hybrid that also calls `CombatMovement`).

**Worked example: Bronto Burt dive-bomb (0x02, descriptor 0x804b2ecc, state table 0x804b2e68).** A 4-state cycle (cruise/dive-watch are two spawn variants of the same descriptor):

1. **Dive-watch (0x10)** func1 (0x8020f6a8) reads altitude and scans for a rider in range/front via `zz_801fe8dc_` (0x801fe8dc, a cone scanner); on a hit it zeroes velocity and `EnemyStateChange`→**0x11**.
2. **Dive (0x11)** func2 (0x8020f800) → `EnemyActor_FlyForward` drives the ballistic plunge along the facing; func1 polls `EventActor_JObjCheck` (0x80200d10, the anim/JObj-state gate) and on completion transitions to recover.
3. **Recover (0x12)** func2 (0x8020f8d4) → `EnemyActor_FlyMovement` drifts back toward the anchor and climbs.

Per-frame, func4 (FOV targeting, 0x801ff8d8) keeps the Burt oriented at the nearest rider; the dive-watch range scan is the actual dive gate.

> **Grounded vs flyer — roster correction.** Of the "FOV-targeting flyers" the older roster implied, only **Bronto Burt, Bomber, Scarfy** are true flyers. **Cappy, Walky, Noddy are grounded** (Cappy = anim-driven ground ambusher with no mover; Walky = grounded spline chaser via `CombatMovement`/`CombatAI`; Noddy = grounded spline "sleeper").

### Shared movement/decision helpers

| Helper | Address | Role |
|--------|---------|------|
| `EnemyActor_CombatAI` | 0x802069e8 | Two-phase grounded movement keyed on `ed+0x908`: phase-0 approach collision probe vs phase-1 engaged ground physics (`Enemy_GroundPhysicsVelocity` + ground-snap); flips the phase and re-attaches to ground on contact. |
| `EnemyActor_CombatMovement` | 0x8020b490 | Sibling of CombatAI, also keyed on `ed+0x908`: phase 0 = spline `Enemy_AIPhysicsTick`; once moving, phase 1 = accel-along-ground-normal (`accel = ground_normal × param_gravity`) + `EnemyActor_GroundFollowMovement`. CombatAI and CombatMovement **share `ed+0x908`** as the engaged/grounded phase bit. |
| `EnemyActor_ClassifyRange` | 0x80206cc0 | Proximity classifier. Reads detect range (+0x10) and chase range (+0x14) **from the actor_data param-root** (`*(ed+0x14)`), buckets the target distance into out/detect/attack, stores the bucket in `ed+0xB09` bits 3–4, returns it (0/1/2). For actors >= 0x4C it additionally clears the bucket via `zz_801ffce4_`. |
| `EnemyActor_PlayerAheadDist` | 0x801fea60 | Per-player test: gets player `i`'s rider position, returns the dot of `(player − enemy)·forward` (signed forward distance, written to the out param) and whether the player is behind (return 1). -1 if the player has no rider. |
| CombatAI/Movement ground helpers | 0x80205884, 0x80205a60, 0x802064b0, 0x80206a7c, 0x80206b98, 0x80206d90 | mpColl ground-snap + `ed+0x908` phase-transition helpers invoked by the two combat movers. |

## Per-Type Descriptor Table (0x804b1d98)

Array of 79 (0x4F) pointers indexed by ActorID. Each points to a per-type descriptor. There are 47 unique descriptors across the 79 actor IDs.

```c
typedef struct PerTypeDescriptor {
    void *state_table;     // +0x00: Per-type state table (states >= 0x0E). Stored at ed+0x44
    void *pad_04;          // +0x04: NULL
    void *init_cb;         // +0x08: Init callback (called during EventActor_Create step 4)
    void *copy_data_cb1;   // +0x0C: Copies actor_data params to EnemyData
    void *copy_data_cb2;   // +0x10: Second data copy pass
    void *destroy_cb;      // +0x14: Cleanup on GObj destruction (typically EventActor_UnregisterSpawnSlot)
    void *post_init_cb;    // +0x18: Final-init callback dispatched at the END of EventActor_Create (0x801fbef4), after all 10 procs + GXLink are registered. Does the final ground/spline re-snap and `EnemyStateChange`s into the type's default state 0x0E. NOT capture-related.
    void *post_capture_cb; // +0x1C: Post-capture callback, dispatched from EventActor_OnCapture (0x80203968) when the enemy is inhaled by a rider — attaches the enemy onto the rider's mouth-bone slot and detaches its own child. (This is the *actual* capture callback; the +0x18 slot is not.)
} PerTypeDescriptor;       // 0x20 bytes
```

> **The +0x18/+0x1C slots are easy to mix up.** Both are read via the same `descriptor[ed->kind]` table (0x804b1d98), but at different sites: **+0x18 fires once at the tail of `EventActor_Create`** (post-init finalize), **+0x1C fires inside `EventActor_OnCapture`** (post-inhale). There is **no "damage callback" in the descriptor** — the damage path (`EventActor_ProcDamage`, priority 10) uses `ed+0xAD0` (hit_reaction_cb2) and falls back to `EnemyKnockback_Default`, never indexing the descriptor.

Multiple actor types can share descriptors (e.g., IDs 0x00 and 0x01 both point to 0x804b2dd8). T1 and T2 of the same enemy always share the same descriptor pointer.

All callback addresses fall in the range 0x8020EA44-0x8021E0D4 (~60KB block).

### Per-Type State Complexity

Per-type state counts (count = entries between the table pointer and the next datum). Counts for enemies not listed here are not individually established.

| Enemy | State Table | Per-Type States (entry count) | Notes |
|-------|-------------|-------------------------------|-------|
| TAC (0x4C) | 0x804b4088 | 12 (states 0x0E-0x19) | Most complex enemy AI — chase/grab/flee + loot scatter |
| Dyna Blade (0x4D) | 0x804b41e8 | 8 (states 0x0E-0x15) | Descend → cruise → anim-driven swoop → exit |
| Scarfy (0x05*) | 0x804b2f80 | 6 (states 0x0E-0x13) | FOV-homing flyer-chaser |
| Waddle Dee (0x17) | 0x804b3e78 | 5 (states 0x0E-0x12) | Patrol + detect → lunge attack |
| Bronto Burt (0x02) | 0x804b2e68 | 5 (states 0x0E-0x12) | Flyer; cruise / dive-watch / dive / recover |
| Cappy (0x06) | 0x804b31f8 | 5 (states 0x0E-0x12) | Grounded ambusher — func1-only, anim-driven jump-out, no targeting |
| Sword Knight (0x05) | 0x804b3118 | 4 (states 0x0E-0x11) | Chase + attack |
| Broom Hatter (0x00) | 0x804b2d88 | 4 (states 0x0E-0x11) | Composite (child 0x48); spline+ground chaser |
| Wheelie (0x08) | 0x804b3350 | 3 (states 0x0E-0x10) | Roam/drive |
| Gordo (0x0E) | 0x804b3808 | 3 (states 0x0E-0x10) | Bounce (state/anim cycle) + FOV facing |
| Bomber (0x0F) | 0x804b3750 | 2 (states 0x0E-0x0F) | Flyer; single cruise state |
| Noddy (0x0A) | 0x804b3530 | 2 (states 0x0E-0x0F) | Grounded "sleeper" — combat state entirely NULL |
| Walky (0x15) | 0x804b3bb8 | 2 (states 0x0E-0x0F) | Grounded spline chaser (CombatMovement/CombatAI + FOV) |

(\*Scarfy's per-type entry sits at descriptor 0x804b2ff8 / state table 0x804b2f80; the actor-ID/descriptor mapping is in the Per-Type Descriptor Table section.)

The first entry of every per-type table (state 0x0E, entry 0) has `anim_idx = -1` and is the default/spawn entry. Simpler enemies have fewer entries; combat enemies have more. Counts above are verified from the state-table bytes (each 0x14-byte entry array is terminated by the descriptor's own back-pointer).

### Init Callback Pattern

All per-type init callbacks follow a common pattern:

1. (Composite enemies only) Call `EventActor_SpawnChild` (0x801fcda0) to spawn the rider/attached actor
2. Call `EventActor_GroundSnap` (0x80204fac, ground snap/raycast) with scale parameter
3. Set hit-reaction callbacks at `ed+0xACC`/`ed+0xAD0` and/or the knockback-landing trio `ed+0xADC`/`ed+0xAE0`/`ed+0xAE4` (Broom Hatter, Sword Knight)
4. Call `EventActor_FinalizeInit` (0x802042fc, finalize init -- animation setup, collision)
5. Optionally call `Enemy_SetTerrainLocked` (0x8020ae54) for the terrain-locked flag — sets bit 2 (mask 0x04) of `ed+0xB0B`. **Broom Hatter and Wheelie** call this helper in their init. **Gordo** does not — its spawn func sets `grounded_active` (ed+0x908) = 1 directly.

> Init callbacks do **not** set `ed+0xAC8` (per_type_cb) — no enemy does. The per-frame brain is wired through the per-type state table, not this slot (see [Per-Type AI Decision Architecture](#per-type-ai-decision-architecture)).

### Special Variants (0x48-0x4E)

| ID | Type | Behavior |
|----|------|----------|
| 0x48-0x4A | Child parts (SP Broom Hatter, SP Sword Knight, SP Waddle Dee Truck) | No init, no default state. Mirror parent's transform via `EventActor_FollowParent` (0x80219eec). `parent_gobj` from descriptor. |
| 0x4B | Event Gordo | Independent actor, own init and behavior |
| 0x4C | TAC | Independent. Descriptor 0x804b4178, state table 0x804b4088, 12 states (0x0E-0x19), init 0x8021a534. Chases riders (func4 cone-probe `zz_801fe764_`), dashes/steers to grab, then flees and self-destructs off-screen. **"Steal" = it scatters fresh City Trial pickups into the world** (`Tac_ScatterItems` 0x8021c8ec → `CityItem_GetEventItem`/`CityItem_Throw`) on a grab roll and when struck — it does **not** remove items from a player's inventory. See [TAC AI](#tac-actor-0x4c). |
| 0x4D | Dyna Blade | Independent. Descriptor 0x804b4288, state table 0x804b41e8, 8 states (0x0E-0x15), init 0x8021c9dc. Spawns high → descends → pass-over (proximity rumble via `DistToPlayer`+`RumblePlayer`, **no targeting**) → cruise/flap → **anim-driven swoop** (flight path is the baked model animation, finite-differenced into velocity) → rains items (`DynaBlade_ThrowItems`) → climbs out → `EventActor_Destroy`. See [Dyna Blade AI](#dyna-blade-actor-0x4d). |
| 0x4E | Meteor | See [meteor-actor.md](meteor-actor.md) |

## Special Event Actor AI

TAC and Dyna Blade are the two complex standalone event actors (Meteor is in [meteor-actor.md](meteor-actor.md)). Both have `actor_id >= 0x4C`, so `EnemyPhysicsProc` skips their OOB floor-kill and `EventActor_SetVisibility` leaves them render-disabled (their idle func re-enables rendering). Neither uses the spawn-slot pool — they spawn through the event system. **Both interact with items by *spawning* City Trial pickups into the world** (`CityItem_GetEventItem` 0x80254114 + `CityItem_Throw` 0x80253ce4), never by removing items from a rider's inventory.

### TAC (actor 0x4C)

Descriptor 0x804b4178 → state table 0x804b4088 (12 states 0x0E-0x19), init_cb 0x8021a534.

**Behavior:** spawn (0x0E) randomly enters **chase (0x0F)** or a timed **wander (0x12)**. In chase, func4 (`Tac_Chase_ProbeAndGrab` 0x8021aa6c) runs the forward-cone player probe `zz_801fe764_` (0x801fe764); when a rider is in range/front it commits a **dash → grab (0x14 → 0x15)**, steering toward the target each frame (`RotateVecAroundAxis`) while wall-avoiding, at speed `param[4]`. A successful grab roll → **recover (0x16)** (can re-dash), eventually **flee (0x17)**: it climbs (`pos.y > param[5]`), disables its hitbox, and `EventActor_Destroy`s once off-screen. Getting hit drops it into **hit-reaction (0x19)**.

**The "steal":** `Tac_ScatterItems` (0x8021c8ec) loops `CityItem_GetEventItem`/`CityItem_Throw`, fanning directions around TAC's forward — called mid-dash (gated by `ed+0xB62%60==30` and `HSD_Randi(5)==0`) and on hit (scatters `param[11]` items). There is **no write to any player item-collect array** anywhere in TAC's code; the on-screen "steals your stuff" reads as TAC lunging and scattering fresh pickups. Its HitColl body is the actual contact/damage mechanism.

| State | Role | State | Role |
|-------|------|-------|------|
| 0x0E | spawn (random → 0x0F or 0x12) | 0x14 | dash-commit (1-frame) → 0x15 |
| 0x0F | chase decide + grab-probe (func4) | 0x15 | grab dash: steer-to-player + wall-avoid |
| 0x10/0x11 | chase sub-move → 0x0F | 0x16 | post-grab recover → re-dash / flee |
| 0x12 | wander/idle → re-chase | 0x17 | flee/carry: climb out → Destroy |
| 0x13 | chase variant | 0x18 | despawn (anim-done → Destroy) |
|  |  | 0x19 | hit-reaction (scatter loot) |

### Dyna Blade (actor 0x4D)

Descriptor 0x804b4288 → state table 0x804b41e8 (8 states 0x0E-0x15), init_cb 0x8021c9dc.

**Behavior:** spawns high and **descends (0x0E → 0x0F)** to a saved cruise altitude. **Pass-over (0x10)** is the only player-aware code — it loops the 4 riders with `EnemyActor_DistToPlayer` and, within `param[5]`, fires `EnemyActor_RumblePlayer(p, 4, 30)` (a **proximity rumble, not targeting**). It ping-pongs between two flap poses (**cruise 0x11 ↔ 0x12**), then commits to the **dive/swoop (0x13)**, whose flight path **is the baked model animation**: `DynaBlade_StateDive_Proc` (0x8021d500) samples the JOBJ translate node's world position each frame and finite-differences it into velocity/accel. The dive's first frame fires `DynaBlade_ThrowItems` (0x8021db44). When it has climbed back out of the arena (**exit 0x14**) it stops its audio emitter and `EventActor_Destroy`s. A strong hit → **recoil (0x15)** + force item-throw.

**No player targeting:** zero `FindNearestPlayer`/FOV calls — Dyna Blade flies a fixed baked path; whoever is underneath gets rumbled / hit by its contact body / showered with items.

> **Param caveat.** Both actors' concrete tuning values (`param[N] = *(actor_data+4)[N]`: detect range, dash speed, item-drop counts, swoop count) live in `Enemy.dat` and are not in the `mem1.raw` menu snapshot — the *gating structure* above is verified from disassembly, but the numeric thresholds need a live dump to pin down.

## Enemy Offensive Hitboxes

The enemy's attack hitboxes live in its HurtData at `ed+0x410`, built by `EventActor_HurtDataCreate` (0x80201ee8): **2 attack regions for normal enemies, 8 for Meteor (0x4D)**. It sets the `on_damage_callback` (`HurtData+0x8C`) `= 0x80201c78` and builds the defensive sub-regions from the actor's joint descriptor.

Per attack frame the params are refreshed from the current animation frame's hurt descriptor into the TriggerData at `ed+0x45C` by `EventActor_RefreshAttackParams` (0x80201ba4). Enable is `Trigger_SetState1` (data-driven, when the anim frame carries hurt data); disable is anim-script **cmd 13** (`EnemyAnimCmd_DisableHit`).

**Inbound vs outbound.** `EventActor_ProcHitColl` (priority 9, 0x801fc8ec) is **INBOUND only** — the enemy as victim, tested against the rider/machine/enemy/hazard hurtdata lists. The enemy's **OUTBOUND** attack on a rider is delivered on the **MACHINE side**: `Machine_CheckEventCollision` (0x801d71ec) reads the enemy's `ed+0x410` attack regions as the attacker against the machine's HurtData (`MachineData+0x660`). Riders are damaged **through their machine** — there is no `Rider_CheckEventCollision` against enemies. See [hurtdata-system.md](hurtdata-system.md) for the full pipeline.

## Movement System

### Path Following (`EnemyPath_FollowUpdate`, 0x80209ce4)

The primary movement function for ground enemies. Follows pre-defined spline paths embedded in stage geometry.

1. Reads current parametric position from `ed+0x5FC` (float, 0.0-1.0)
2. Calls `splArcLengthPoint()` to get world-space positions at current and nearby spline points
3. Computes forward direction from difference between two sample points
4. Stores movement direction in `ed+0x664-0x66C`
5. `param_10` controls direction mode: 0=forward on init, 1=continue forward, 2/3=lateral movement
6. Calls `zz_8020a9dc_` for proper orientation (up/right/forward axes)

**Path initialization** (`EnemyPath_Init` / 0x80206e2c):
- `zz_800cf07c_` finds nearest spline to enemy position
- Stores path ID in `ed+0x5DC`, parametric position in `ed+0x5FC`
- Gets forward/backward spline pointers via `zz_800cf3ac_`/`zz_800cf44c_`

**Path advancement** (`EnemyPath_Advance` / 0x8020a040):
- Advances parametric position each frame using movement speed
- At segment end, calls `zz_802070e8_` to transition to next connected spline
- Interpolates between position/up splines for smooth terrain following

### Ground Physics

Two variants for ground-following movement:

**`Enemy_GroundPhysicsVelocity` (0x80209104)** -- Velocity-based projection:
- Projects velocity forward, raycasts to find ground
- Uses `PointCollision_EnsureIDValid_` for collision validation
- Adjusts height to ground surface

**`Enemy_GroundPhysicsSurface` (0x802096b4)** -- Direct surface advancement:
- Position advancement along surface normal
- Wall bouncing via `VEC_Reflection` -- scales reflected velocity by 1.5 x movement_speed x spline_scale
- Ground-snapping with friction

Both functions:
1. Skip first 2 frames after spawning (warmup via `ed+0x880`) while position/normal stabilize
2. Compute movement direction from position delta
3. Raycast to find ground surface ahead
4. Snap to ground and update surface normal
5. Call `Enemy_GroundAttach` (0x8020a664) for final ground attachment

### Height Interpolation

Height above ground is smoothed rather than snapped instantly:
- **Target height** stored at `ed+0x864`
- **Current height** stored at `ed+0x868`
- Lerp factor of 0.2 per frame (~5 frames to settle to target)
- Slope factor interpolation uses the same 0.2 lerp, influencing the ground projection ray distance (ed+0x95C)

### Surface Snapping (`EventActor_GroundSnap`, 0x80204fac)

Used by walking enemies to snap to ground:
1. Casts ray downward from above enemy position (using `ed+0x340` as surface normal)
2. `EnvColl_Raycast` finds ground hit point
3. Moves enemy to hit position + height offset along normal
4. Updates surface normal from collision result

### AI Physics Tick (`Enemy_AIPhysicsTick`, 0x802081ec)

Central function used by many enemy types in normal AI states. Handles ground-following movement:

1. **Early exit**: If `ed+0x964` (movement speed) is 0.0, returns immediately
2. **Ground-based path (velocity > 0)**:
   - Checks path following via `Enemy_CheckPathFollow` (0x8020b01c)
   - Ground collision via `zz_800cf07c_`
   - Updates spline references at `ed+0x5DC/0x5FC/0x5D4/0x5D8`
   - Computes target position from `EnemyPath_Advance` (0x8020a040)
   - Builds local coordinate frame from ground normal
   - Applies banked turning via `RotateVecAroundAxis_Vec3_`
   - Updates facing direction via `EventActor_UpdateOrientation` (0x802054e4)
3. **Airborne/idle path (velocity == 0)**:
   - Computes idle wander speed from `ed+0x974` and animation rate
   - Line-of-sight/distance check against target
4. **Turning/banking** (guarded by flag bit `ed+0xB0B >> 2 & 1`):
   - Reads turn rate params from `ed+0x3D0/0x3D4/0x3D8`
   - Clamps turn delta, applies rotation
   - Interpolates orientation toward new facing

Returns 0 on successful movement computation, 1 if stationary.

## Player Targeting System

### Distance Check (`EnemyActor_DistToPlayer`, 0x801fffa4)

```c
float EnemyActor_DistToPlayer(int player_idx, Vec3 *enemy_pos);
```

- Gets player's rider GObj via `Ply_GetRiderGObj`
- If rider exists, gets position via `Ply_GetPosition`
- Returns Euclidean distance, or large sentinel if player has no rider

### Find Nearest Player (`EnemyActor_FindNearestPlayer`, 0x801ffd78)

The main player targeting function. Stores target at `ed+0xB24` (s16, player index; -1 = none) with a retarget cooldown at `ed+0xB26` (s16).

**Detection range:** Read from the global enemy param table at `*(stc_enemy_param_table) + 0x80` = **50.0** (a single scalar, not tier-indexed). This is the max acquisition radius; players beyond it are never targeted. It is the first rung of a distance ladder in the table (from `Enemy.dat` `emDataAll`, file offset 0x30): `+0x80`=50.0 (acquisition radius, here), `+0x84`=30.0 (close range), `+0x88`=30.0, `+0x8C`=300.0 (mid range), `+0x90`=500.0 (max/leash — the dominant range constant, read by ~15 AI state funcs).

**Retarget cooldown:** Random value in `[table+0x94, table+0x98]` = `20 + HSD_Randi(40-20)` = **20–39 frames**, preventing simultaneous retargeting of all enemies when a player moves.

> **The `ed+0x378`/`ed+0x37c` range copies are dead.** `Enemy_CopyParamBlock` copies `param_detect_range` (ed+0x378) and `param_chase_range` (ed+0x37c) out of the archive, but **nothing in the enemy code reads them** (0 references). The live detection range is the global table `+0x80` (here); the live proximity bucket is `EnemyActor_ClassifyRange`, which reads detect/chase range from the **actor_data param-root** (`*(ed+0x14)+0x10`/`+0x14`), not the copies. To change detection range, write the global table or the archive root — not these fields.

**Targeting logic:**
1. If `ed+0xB24 == -1` (no target): iterate all 4 players, find nearest within max detection range, set as target
2. If target exists but cooldown `ed+0xB26 == 0`: re-evaluate all players, pick nearest, reset cooldown to random value
3. If cooldown > 0: decrement, keep current target

**Chase behavior** (when target acquired and `ed+0xB28 == 0.0`):
- Gets target player position
- Computes direction vector (enemy -> player)
- Normalizes and stores in `ed+0xB38-0xB40` (chase direction)
- Updates `ed+0x334` (facing) to face the player
- Cross products for proper orientation axes

### FOV-Aware Targeting (`EnemyActor_FindNearestPlayerFOV`, 0x801ff8d8)

More sophisticated targeting with field-of-view check:
1. Iterates all 4 players, computing distance
2. Checks angle between enemy's forward vector (`ed+0x334`) and direction to player using `Vec_GetAngleBetween_Vec3_`
3. **180-degree hemisphere check** -- rejects only players directly behind the enemy (not a narrow cone)
4. On valid target, calls `zz_801fd878_` for melee attack targeting using bone joint positions via `JOBJ_GetWorldPosition`
5. Fallback: if no target found and model exists, uses bone interpolation with factor 0.1 from stored bone joint data (`ed+0x918`, 3 slots x 0x1C bytes)

### Controller Rumble (`EnemyActor_RumblePlayer`, 0x801ff80c)

```c
void EnemyActor_RumblePlayer(int player_idx, int intensity, int duration);
```

This function gets the player's rider GObj and triggers controller rumble with `(controller_idx, 2, intensity, duration)`. It does **not** apply direct damage. Actual damage to players flows through the HitColl collision pipeline (see [hurtdata-system.md](hurtdata-system.md)).

## Knockback System

When an enemy takes damage exceeding the knockback threshold, the knockback system determines the reaction.

### Knockback Direction

A random 3-bit value from `HSD_Randi(8)` (in `Enemy_ApplyKnockback`, 0x8020b784) determines the sign of each velocity component (X, Y, Z), giving 8 possible initial knockback directions.

### Knockback Source Kinds (ed+0x99C)

| Kind | Source | Behavior |
|------|--------|----------|
| 0 | Normal (from HitColl) | Standard damage-driven knockback |
| 1 | From attacker position | Direction computed from attacker to enemy |
| 2 | Generic default | Reachable (mapped from hit-type 2 by `EnemyKnockback_Default`) but no distinct behavior — falls into the generic default block (hardcoded direction constant, no source lookup), identical to kind 4 |
| 3 | Enemy-on-enemy | Collision between two enemies (uses attacker pos at `ed+0xA24`) |
| 4 | Generic default | Same generic default block as kind 2 (hardcoded direction constant, no source lookup) |
| 5 | Special | Shares the kind-0/1 HitColl/attacker-position direction path; used by scripted events |

`EnemyKnockback_Default` (0x8020bcd8) maps the hurtdata hit-type (`+0x38`) to a kind via the identity jump table at 0x804b2b50 (type N → kind N for 0..7, type > 7 → skip). Inside `Enemy_ApplyKnockback` (0x8020b784) the dispatch on `ed+0x99C` is: kinds 0/1 → HitColl/attacker-position direction path; kind 5 → shares that same path; kind 3 → enemy-on-enemy (attacker pos at `ed+0xA24`); kinds 2 **and** 4 → the generic default block (hardcoded direction constant, no source lookup).

### Default Knockback Handler (`EnemyKnockback_Default`, 0x8020bcd8)

This handler reads the hurtdata hit-type field (`hurtdata+0x38`); if `> 7` it bails (no knockback). Otherwise it uses an 8-entry jump table at 0x804b2b50 to map the hit type to a kind 0-7, then calls `Enemy_ApplyKnockback` (0x8020b784) with that kind. (The jump-table arms simply load the constant kind value into r5.)

`Enemy_ApplyKnockback` (0x8020b784) — **not** the default handler — is where the per-tier knockback parameters are read from the global enemy param table (`*(r13+0x798)`):
- `+0x50`: launch speed (stored to ed+0x9D8 = `kb_launch_speed`)
- `+0x60`: secondary knockback parameter

It also performs the `HSD_Randi(8)` direction randomization and branches on the `kb_source_kind` (ed+0x99C) values 0/1/2/3/5.

## Actor Data (from .dat Archives)

### Data Lookup

`Enemy_GetActorData(actor_id)` at 0x801fd498:
1. Reads `{data_index, flags}` from table at 0x804b22b4 (stride 8, indexed by actor_id)
2. Checks loaded flag at `0x8055a210[data_index]`
3. Reads archive root pointer from `0x8055a228[data_index]`
4. Indexes by flags: `archive_root[flags]` (flags 0-4 select sub-entry variant)

Returns a pointer stored in `EnemyData+0x14`.

### Actor Sub-Entry Structure

The returned pointer points to a per-tier data block:

| Offset | Type | Purpose | Accessed By |
|--------|------|---------|-------------|
| +0x00 | ptr | **Parameter block root** -- behavioral floats | `*actor_data` in InitFromDesc, bulk copy in `zz_802006b4_` |
| +0x04 | ptr | **Per-type secondary params** -- detection/attack | Per-type callback [3] copies 16 bytes to `ed+0x40C` |
| +0x08 | int | **`-1` sentinel word** (param header). NOT joint/anim data. | Param-block header |
| +0x0C | ptr | **Animation state table** -- 0x10-byte animseq entries | `EnemyStateChange` indexes by `anim_idx * 0x10` |
| +0x10 | ptr | **Material/texture animation data** | Material update |
| +0x14 | ptr | Additional data | |

**Animseq entry layout (0x10 bytes).** Each entry in the animation state table at `actor_data + 0x0C` is:

| Offset | Type | Purpose |
|--------|------|---------|
| +0x00 | `AnimJoint*` (HSD) | Skeletal animation joint |
| +0x04 | `MatAnimJoint*` (HSD) | Material animation joint |
| +0x08 | float/int | End-frame / flags |
| +0x0C | byte | Flags byte (bit `0x80` gates per-frame anim work) |

**Resolution.** `ed+0x48` (`anim_data`) `= *(actor_data + 0x0C) + anim_idx * 0x10`, where `anim_idx` is the state-table entry's `word0`. The animation is applied by `EventActor_AnimDataInit` (0x80200c04): `HSD_JObjRemoveAnimAll`, then `HSD_JObjAddAnimAll(rootJObj, AnimJoint, MatAnimJoint, 0)`, then `HSD_JObjReqAnimAllByFlags`. The model's root JObj is reached via `ed+0x00` (HSD container) → `+0x28`, **NOT** via `actor_data + 0x08`.

### Parameter Block Root

`*actor_data` points to behavioral parameters. `zz_802006b4_` bulk-copies 0xA4 bytes from `*actor_data - 4` into `EnemyData+0x364` through `EnemyData+0x408`.

Mapped fields (Waddle Dee T0 as reference):

| *actor_data Offset | -> EnemyData | Value | Type | Purpose |
|---------------------|-------------|-------|------|---------|
| -0x04 | 0x364 | 0 | int | Pre-header |
| +0x00 | 0x368 | 2.4 | float | **param_base_scale** (also -> `ed+0x2D0` as `tier_base_scale`) |
| +0x04 | 0x36C | 2.0 | float | Scale param 2 |
| +0x08 | 0x370 | -1 | int | Sentinel/flag |
| +0x0C | 0x374 | 1.0 | float | |
| +0x10 | 0x378 | 10.0 | float | **param_detect_range** |
| +0x14 | 0x37C | 4.0 | float | **param_chase_range** |
| +0x18 | 0x380 | 2.5 | float | **param_move_param** |
| +0x20 | 0x388 | 2.0 | float | **param_path_speed** |
| +0x24-0x28 | 0x38C-0x390 | 0.0 | float | **Spline walk speed base** |
| +0x2C | 0x394 | 0.0 | float | **Spline walk speed secondary** |
| +0x30 | 0x398 | 0.0 | float | **Random timing variation** (x `HSD_Randf()`) |
| +0x34 | 0x39C | 0.0 | float | **param_speed_2** |
| +0x38 | 0x3A0 | 0.0 | float | |
| +0x3C | 0x3A4 | 0.02 | float | **param_gravity** |
| +0x40 | 0x3A8 | 1.5 | float | param_3a8 |
| +0x44 | 0x3AC | 60 | int | **param_frame_count** (frame count/duration) |
| +0x48 | 0x3B0 | 1 | float | **param_hp_threshold** (compared against damage_accum_1 in the priority-10 damage proc; see `enemy.h`) |
| +0x4C | 0x3B4 | 2.5 | float | |
| +0x50 | 0x3B8 | 1 | int | |
| +0x54 | 0x3BC | 2 | int | |
| +0x58 | 0x3C0 | 0.1 | float | **param_move_speed** (passed to `EventActor_GroundSnap`, 0x80204fac) |
| +0x5C-0x90 | 0x3C4-0x3F8 | -- | -- | Additional parameters |
| +0x94 | (direct) | 0 | int | **param_turn_rate** type (0=none, 1=orbit, 2=fixed) |
| +0x98 | (direct) | 2.0 | float | **Turn rate param 1** |
| +0x9C | (direct) | 0.05 | float | **Turn rate param 2** |
| +0xA0 | (direct) | -- | float | **Knockback launch multiplier** |

### Per-Enemy Tier-0 Param Values

Tier-0 values extracted from each enemy's `Em*Data.dat` archive (the 0xA4-byte param-block root). `detect`/`chase` here are the `+0x10`/`+0x14` fields the live `EnemyActor_ClassifyRange` reads from the actor_data root.

| Enemy | base_scale (+0x00) | detect (+0x10) | chase (+0x14) | move_speed (+0x58) | gravity (+0x3C) | frames (+0x44) | hp_threshold (+0x48) |
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

Nearly all regular enemies share **`detect = 10` (+0x10) / `chase = 4` (+0x14)** — these are the live proximity-bucket ranges read by `EnemyActor_ClassifyRange` from the actor_data root, distinct from the global `+0x80`=50 acquisition radius and from the dead `ed+0x378`/`ed+0x37c` per-enemy copies. `hp_threshold` (+0x48) is the damage-to-kill gate: Gordo ~1e8 (effectively invincible), TAC 40, Dyna Blade 150, all others 1. **Dyna Blade is the outlier** (detect 2 / chase 1 / gravity 0.25). Higher tiers (T1/T2) exist for most enemies but their values are not all captured.

### Per-Type Secondary Params

`*(actor_data + 4)` points to per-type data. Callback [3] copies 16 bytes to `ed+0x40C`.

Waddle Dee T0 values:

| Offset | Value | Type | Purpose |
|--------|-------|------|---------|
| +0x00 | 100.0 | float | Attack/damage range |
| +0x04 | 0.05 | float | Attack rate/cooldown |
| +0x08 | 1.5 | float | Attack damage multiplier |
| +0x0C | 4 | int | Attack type/param |

## EnemyData Field Map (0xBC0 bytes)

### Core Identity (0x000-0x01C)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x000 | GOBJ* | gobj | This actor's own GOBJ |
| 0x004 | GOBJ* | child_gobj | Child/rider GOBJ (0 if none). Recursively destroyed. |
| 0x008 | GOBJ* | parent_gobj | Parent/target GOBJ. Used by child actors to follow parent. |
| 0x00C | int | kind | Actor type ID (0x00-0x4E) |
| 0x014 | void* | actor_data | Tier-specific data pointer from `Enemy_GetActorData(kind)` |

### Spawn/Lifecycle (0x020-0x030)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x020 | int | spawn_slot | From desc. -1 for standalone actors. |
| 0x024 | int | spawn_index | From desc. -1 for standalone. |
| 0x028 | int | lifetime_base | From desc (only if spawn_index != -1) |
| 0x02C | int | lifetime_counter | Decremented each frame in proc 21 for OOB enemies |
| 0x030 | int | tier_flags | Variant selector (0=T0, 1=T1/T2, 2/3/4=special) |

### State Machine (0x034-0x054)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x034 | int | state | Current state ID. Written by `EnemyStateChange`. |
| 0x038 | int | per_type_threshold | Constant 0x0E (set in `EventActor_InitFromDesc`). The state-ID cutoff separating common from per-type states. `EnemyStateChange` compares against a hardcoded 14, so this stored copy is informational. |
| 0x03C | int | anim_idx | Animation index from state table entry. -1 = no animation. Written by `EnemyStateChange` (`stw r0,60(r28)`). |
| 0x040 | void* | common_state_table | Pointer to common states 0x00-0x0D (0x804b2950). Read by `EnemyStateChange` for `state_id < 0x0E` (`lwz r3,64(r28)`). |
| 0x044 | void* | per_type_state_table | Per-type state table (states >= 0x0E). Initialized to `descriptor[0x00]` (the per-type descriptor's state-table pointer). Read by `EnemyStateChange` for `state_id >= 0x0E` (`lwz r3,68(r28)`). |
| 0x048 | void* | anim_data | Current animseq entry pointer (`*(actor_data + 0x0C) + anim_idx * 0x10`). 0x10-byte entry: `+0x00 AnimJoint*`, `+0x04 MatAnimJoint*`, `+0x08 end-frame/flags`, `+0x0C flags byte` (bit `0x80` gates per-frame anim work). Applied by `EventActor_AnimDataInit` (0x80200c04). |
| 0x04C | float | anim_timer | Animation script timer. Initialized to -1.0 for immediate first-command execution. |
| 0x050 | float | anim_frame | Current animation frame accumulator (`ed+0x2A8 + ed+0x2AC`, written each frame by `EventActor_StateMachine`) |
| 0x054 | void* | anim_command_ptr | Animation script bytecode pointer (NULL = stopped) |

### Scale (0x2C8-0x2DC)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x2C8 | float | mode_scale | 1.0 (Air Ride), 1.1 (Top Ride), 1.2 (City Trial) |
| 0x2CC | float | spawn_scale | From descriptor (typically 1.0) |
| 0x2D0 | float | tier_base_scale | From `*actor_data + 0x00` |
| 0x2D4 | float | global_enemy_scale | Global multiplier |
| 0x2D8 | float | final_scale | `mode * spawn * tier_base * global` |
| 0x2DC | float | collision_scale_mult | Used in collision sizing |

### Physics (0x2E0-0x330)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x2E0 | Vec3 | accel | Acceleration. `vel += accel` each frame. |
| 0x2EC | Vec3 | vel | Velocity. `pos += vel` each frame. |
| 0x2F8 | Vec3 | pos | Current world position |
| 0x304 | Vec3 | pos_prev | Previous frame position (copied from pos in proc 21) |
| 0x310 | Vec3 | pos_initial | Spawn position |
| 0x31C | Vec3 | pos_attached | For child actors |

### Orientation (0x334-0x358)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x334 | Vec3 | forward | Forward direction (unit vector). Model facing. |
| 0x340 | Vec3 | up | Up direction (unit vector). Surface normal for grounded. |
| 0x34C | Vec3 | right | Right direction (cross product of up x forward) |

### Behavioral Parameters (0x364-0x408)

Bulk-copied from `*actor_data - 4` (0xA4 bytes). See Parameter Block Root table above for field mapping. Named fields:

| Offset | Source | Name | Purpose |
|--------|--------|------|---------|
| 0x364 | `*actor_data - 0x04` | (pre-header) | Pre-header value |
| 0x368 | `*actor_data + 0x00` | param_base_scale | Base scale |
| 0x378 | `*actor_data + 0x10` | param_detect_range | Detection range |
| 0x37C | `*actor_data + 0x14` | param_chase_range | Chase range |
| 0x380 | `*actor_data + 0x18` | param_move_param | Movement parameter |
| 0x388 | `*actor_data + 0x20` | param_path_speed | Path speed |
| 0x39C | `*actor_data + 0x34` | param_speed_2 | Speed parameter (knockback velocity scale) |
| 0x3A4 | `*actor_data + 0x3C` | param_gravity | Gravity/fall acceleration |
| 0x3B0 | `*actor_data + 0x48` | param_hp_threshold | HP threshold (compared against damage_accum_1 in priority-10 damage proc) |
| 0x3C0 | `*actor_data + 0x58` | param_move_speed | Movement speed |
| 0x3D0 | (within bulk copy) | turn_rate_params | Turn-rate params read by `Enemy_AIPhysicsTick` (ed+0x3D0/0x3D4/0x3D8). Note: the bulk copy covers ed+0x364..0x408, so these come from `*actor_data + 0x68..0x70`, NOT the `+0x94` direct reads listed in the parameter-block table below. |

### HurtData / Collision (0x410-0x594)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x40C | void* | per_type_params | Allocated buffer, receives 16 bytes from `*(actor_data + 4)` |
| 0x410 | void* | hurtdata | HurtData pointer (hitbox/hurtbox collision) |
| 0x594 | void* | map_collision | Map collision object for ground detection |

### Ground State (0x5C4-0x5D0)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x5C4 | float | ground_height | Height from ground |
| 0x5C8 | Vec3 | ground_normal | Ground surface normal |

### Animation Transform (0x538-0x558)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x538 | Vec3 | anim_pos | Current animation-derived position |
| 0x544 | Vec3 | anim_pos_prev | Previous frame's anim_pos |
| 0x550 | Vec3 | anim_delta | Per-frame animation position delta |

### Spline/Path (0x5D4-0x67C)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x5D4 | void* | spline_primary | Primary spline curve pointer |
| 0x5D8 | void* | spline_secondary | Secondary spline curve pointer |
| 0x5DC | int | spline_segment | Index into stage spline array |
| 0x5F8 | int | spline_direction | 1=forward, else backward |
| 0x5FC | float | spline_arc_param | Parametric position (0.0-1.0) |
| 0x654 | int | spline_path_ready | Set to 1 before `EnemyPath_Init` |
| 0x658 | Vec3 | saved_up_normal | Saved up-direction for spline snap recovery |
| 0x664 | Vec3 | move_direction | Computed movement direction from path |

### Height / Ground Interpolation (0x864-0x880)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x864 | float | height_interp_target | Target height above ground |
| 0x868 | float | height_interp_current | Current interpolated height (lerp factor 0.2/frame) |
| 0x878 | float | kb_speed_mult | Knockback speed multiplier |
| 0x880 | int | ground_warmup | 2-frame warmup counter; ground physics skip while > 0 |

### Knockback / Launch (0x888-0x950)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x888 | int | recovery_timer | Knockback recovery countdown |
| 0x88C | int | launch_spline_id | Spline ID for launched projectile path |
| 0x8A8 | float | launch_time_accum | Accumulated time along launch spline |
| 0x8AC | float | launch_time_step | Per-frame time step for launch path |
| 0x8E8 | float | grounded_timer | Time since landing (state 0x0D) |
| 0x8EC | float | slide_timer | Sliding friction timer (friction = constant / slide_timer) |
| 0x8F0 | Vec3 | bounce_vel | Decaying bounce velocity after landing |
| 0x908 | int | grounded_active | Nonzero when grounded physics active |
| 0x950 | GOBJ* | hit_source_gobj | GObj of the entity that hit this enemy |
| 0x95C | float | slope_factor | Ground slope interpolation (lerp 0.2/frame) |

### JObj / Model (0x2B4-0x2C4)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x2B4 | void* | jobj_tree | JObj tree for model hierarchy |
| 0x2BC | void* | alloc_2bc | Second HSD_ObjAlloc structure |
| 0x2C4 | void* | alloc_2c4 | Third HSD_ObjAlloc structure |

### Animation JObj Overrides (0x918-0x950)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x918 | [0x1C] x 2 | anim_jobj_overrides | Two JObj override entries. Cleared on state change. |

### Shadow (0x954-0x958)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x954 | void* | shadow | Shadow object pointer |
| 0x958 | void* | shadow_2 | Secondary shadow |

### AI State (0x960-0x97C)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x964 | float | movement_speed | If 0.0, AI physics tick returns immediately |
| 0x974 | float | idle_wander_speed | Used for airborne/idle computation |

### Damage (0x994-0xA28)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x994 | float | damage_accum_1 | Total damage (capped 9999). Cosmetic. |
| 0x998 | float | damage_accum_2 | Secondary damage accumulator |
| 0x99C | int | knockback_source_kind | 0=normal, 1=attacker pos, 3=enemy-on-enemy, 5=special |
| 0x9C0 | int | death_sfx_id | SFX to play on death |
| 0x9C4 | int | death_vfx_id | VFX to spawn on death |
| 0x9C8 | int | death_frame_counter | Set to 600 on death entry; 601 > 120 threshold = next-frame destroy |
| 0xA10 | int | attraction_mode | Attraction/inhale mode |
| 0xA14 | void* | attraction_target | Target for attraction physics |
| 0xA18 | int | stun_frames | Frames in stun. Decremented each frame. |
| 0xA1C | int | knockback_tier | Response tier (0-3) |
| 0xA2C | float | anim_speed_scale | Decaying animation speed scale |
| 0xA78 | int | damage_frame_counter | Frames since last damage taken |

### Knockback Direction (0x9CC-0x9E8)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0x9CC | Vec3 | knockback_dir | Knockback direction vector |
| 0x9D8 | float | knockback_speed | Velocity magnitude |
| 0x9DC | Vec3 | knockback_pos_saved | Position at time of knockback |

### SFX / VFX Handles (0xA3C-0xA78)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0xA3C | int | sfx_handle_1 | SFX handle (cleaned up by flag 0x40 skip in EnemyStateChange) |
| 0xA40 | int | sfx_handle_2 | Second SFX handle (also cleaned up by flag 0x40 logic) |
| 0xA58 | int | sfx_handle_3 | SFX handle |
| 0xA5C | int | sfx_handle_4 | Second SFX handle |
| 0xA60 | int | sfx_state_1 | SFX state (init -1) |
| 0xA64 | int | sfx_state_2 | SFX state (init -1) |
| 0xA70 | int | hit_vfx_1 | Impact VFX handle |
| 0xA74 | int | hit_vfx_2 | Second impact VFX |

### Path Control (0xA8C)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0xA8C | float | path_active_flag | -1.0 = path-following enabled |

### State Callbacks (0xAB8-0xAF0)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0xAB8 | func_ptr | state_func1 | From state table entry[1]. Priority 1 (ProcUpdate). |
| 0xABC | func_ptr | state_func2 | From state table entry[2]. Priority 4 (pre-physics). |
| 0xAC0 | func_ptr | state_func3 | From state table entry[3]. Priority 5 (state active). |
| 0xAC4 | func_ptr | state_func4 | From state table entry[4]. Priority 6 (model). |
| 0xAC8 | func_ptr | per_type_cb | Priority 7 dispatch (`EventActor_ProcPerType`). **Never installed by any enemy** — only zeroed by `EnemyStateChange` (unconditional, regardless of flag 0x10). A dead slot in vanilla, hence the cleanest custom-AI injection point. |
| 0xACC | func_ptr | hit_reaction_cb1 | Set by per-type init. |
| 0xAD0 | func_ptr | hit_reaction_cb2 | From priority 10 damage proc (`EventActor_ProcDamage`). |
| 0xAD4 | int | (counter) | Cleared on state change **unless** flag 0x10 set. |
| 0xAE0 | func_ptr | grounded_callback | Called when enemy lands on ground |
| 0xAEC | func_ptr | custom_death_callback | Custom death handler (overrides default death in AnimTick) |

### Render / Status Flags (0xB08-0xB14)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0xB08 | byte | render_flags | Bit 0-1: ground_state (0=air, 1=transitioning, 2=grounded). Bit 4: render disabled (via EventActor_DisableRendering). Bit 7: invisible (via EventActor_Hide). Both must be clear for visibility. |
| 0xB09 | byte | status_flags_1 | Bit 5: set in InitFromDesc. Bit 6-7: ground contact flags. |
| 0xB0A | byte | status_flags_2 | Bit 0: set in InitFromDesc. Bit 2: no-spline flag (causes destroy in proc 21). Bits 5-6: knockback sub-state mode. |
| 0xB0B | byte | status_flags_3 | Bit 3: special actor grounded flag (Gordo, event actors -- checked in EnemyStateChange step 2). Bit 4: shadow visibility. Bit 5: shadow active. |
| 0xB10 | float | inhale_distance | Distance to inhaling rider's mouth |
| 0xB14 | float | shadow_base_scale | Shadow size computation |
| 0xB1C | int | suction_active | Nonzero when being inhaled |
| 0xB20 | int | inhale_timer | Frames since inhale started; destroyed at > 120 |

### Player Targeting (0xB24-0xB40)

| Offset | Type | Name | Purpose |
|--------|------|------|---------|
| 0xB24 | s16 | target_player_idx | Targeted player (-1 = none) |
| 0xB26 | s16 | retarget_cooldown | Frames until re-evaluation |
| 0xB28 | float | chase_flag | 0.0 = chase active |
| 0xB38 | Vec3 | chase_direction | Normalized direction toward target |

### Meteor-Specific (0xB48-0xB78)

See [meteor-actor.md](meteor-actor.md) for full details.

| Offset | Type | Name |
|--------|------|------|
| 0xB48 | s16 | timer |
| 0xB4A | s16 | frame_counter |
| 0xB4C | s16 | in_bounds_flag |
| 0xB4E | s16 | camera_flag |
| 0xB60 | int | landing_vfx_1 |
| 0xB64 | int | landing_vfx_2 |
| 0xB74 | void* | collision_sphere |
| 0xB78 | void* | collision_sphere_2 |

## Spawn Manager

### Enemy_Think (0x800f3904) -- Air Ride

Per-frame manager for enemy spawn slots:
1. Iterates player slots 0-3
2. Caches per-player data: position, velocity, orientation, boosting state, predicted future position
3. Scans GObj entities of type 4 for nearby vehicles
4. Increments global frame counter

### Enemy_CityTrialThink (0x800f33c0) -- City Trial

Extended version:
1. Tracks City Trial timer via `City_GetMinSecMs`
2. Computes spawn rate scaling based on elapsed time
3. Three spawn modes based on sub-state:
   - **Mode 2 (normal)**: Iterates spawn positions, counts down respawn timers, spawns via `Enemy_SpawnerDecide` when below cap. Round-robin position cycling.
   - **Mode 1**: Sequential spawning through all positions
   - **Mode 3**: Random-start sequential spawning

## Influencing Enemy Behavior

For applying behavior presets (e.g. `mods/custom_ai`), there are two strategies, mirroring `cpu-ai-system.md` § Influencing CPU Behavior.

### 1. Tweak — adjust vanilla parameters

- **Global enemy param table** (`*(stc_enemy_param_table)`, loaded from `Enemy.dat`'s `emDataAll` by `Enemy_LoadCommonParams` 0x801fd580; NULL until a stage with enemies loads). It is RAM-resident, so writing it retunes **all** enemies at once:
  - **Distance ladder** (from `Enemy.dat` `emDataAll`, file offset 0x30): `+0x80`=50.0 **acquisition radius** (`EnemyActor_FindNearestPlayer`), `+0x84`=30.0 (close range), `+0x88`=30.0, `+0x8C`=300.0 (mid range), `+0x90`=500.0 (**max/leash — the dominant range constant, read by ~15 AI state funcs**). Raise the acquisition/leash rungs for an *Aggressive* feel; drop them for *passive/Coward*.
  - `+0x94`/`+0x98` **retarget cooldown** (20/40 → 20–39 frames) — how often an enemy re-picks its nearest target. Lower = twitchy / *Erratic* switching.
  - `+0x04` damage scale (0.4), `+0x08/+0x0C/+0x10` tier thresholds (10/21/32), `+0x30/+0x40/+0x50/+0x60` per-tier knockback magnitude/scale/launch/stun — tune how hard enemies are to knock out (see [enemy-spawn-system.md](enemy-spawn-system.md) § Damage & Knockback System).
  - Int array at `+0x14..+0x20` = {10,30,50,70} — consumer not yet identified (open).
- **Per-enemy speed** — the live movement speeds are `ed+0x964` (`movement_speed`; `Enemy_AIPhysicsTick` early-exits if 0) and `ed+0x974` (`idle_wander_speed`). The state funcs rewrite these each frame, so a one-time post-spawn write is overwritten — re-assert it every frame (e.g. from an injected per_type_cb, below).
- **Per-archive detect/chase range** — `EnemyActor_ClassifyRange` reads detect/chase range from the **actor_data param-root** (`*(ed+0x14)+0x10`/`+0x14`), shared by every enemy of that data_index/tier. Patching the archive root scales the proximity bucket for all instances of that type.

> ⚠️ **Dead knobs.** The bulk param copy populates `ed+0x378` (`param_detect_range`) and `ed+0x37c` (`param_chase_range`), but **nothing reads them** (0 references). Scaling these per-enemy copies — as the current `mods/custom_ai` `EnemyAIPresetDef` (`detect_range_mult`/`chase_range_mult`) and `param_move_speed` (ed+0x3c0, also unread) do — is a **no-op**. Use the global table (+0x80) or the archive root (+0x10/+0x14) for range, and `ed+0x964`/`ed+0x974` for speed.

### 2. Replace — inject per-frame logic via the dead per_type_cb slot

The cleanest hook is the **`ed+0xAC8` per_type_cb slot**. Vanilla never installs it, yet `EventActor_ProcPerType` (priority 7) dispatches it every frame with `EnemyData*` in r3 — so writing a function pointer there injects custom per-frame AI **without fighting any vanilla callback**. Because `EnemyStateChange` zeroes `ed+0xAC8` on every transition, re-assert it (set it once per frame from your own proc, or after each `EnemyStateChange`). From the callback you can:

- **Steer targeting** — overwrite `ed+0xB24` (target_player_idx) / `ed+0xB38` (chase_direction) after the vanilla targeting runs: home on an item box instead of a rider (*Hoarder*), or negate the chase direction to flee (*Coward*).
- **Pin movement** — re-assert scaled `ed+0x964`/`ed+0x974` for faster/slower chase.
- **Force states** — `EnemyStateChange` into the type's attack or idle state to make it relentless or passive.

Alternative hooks: override the state callbacks `ed+0xAB8`–`ed+0xAC4` directly after spawn (re-assert after `EnemyStateChange`); add your own `GObj_AddProc(gobj, cb, priority)` at any priority; or for spawning fresh actors and driving physics directly, write `ed+0x2E0` (accel) / `ed+0x2EC` (vel) / `ed+0x2F8` (pos).

> **Reference implementation (currently dormant).** `mods/custom_events/src/spawn_enemy.c` is a worked example of standalone enemy spawning: `SpawnEnemy_Random` (random actor near a machine, optional `EnemyPath_Init` spline attach via `ed->spline_path_ready`/`ed->path_active_flag`) and `SpawnEnemy_MeteorTrap` (meteor over every human player). It also installs null-safety patches (`SpawnEnemy_OnBoot`) for `EventActor_GetParentScale` and `splArcLengthPoint`, which crash on the null parent/spline pointers that standalone spawns have. **None of these entry points are currently called** anywhere (`SpawnEnemy_OnBoot` is not wired into the custom_events boot path) — treat the file as a reference, not as live behavior.

### Key Constraints

- State callbacks are called with `EnemyData*` as the sole parameter (in r3)
- The physics proc always runs (`vel += accel`, `pos += vel`) -- zero velocity/accel for non-physics actors
- `ed+0xAC8` (per-type cb) is cleared to NULL on every state change -- **unconditionally** (flag 0x10 only protects `ed+0xAD4`, not `ed+0xAC8`) -- so it must be re-set after any `EnemyStateChange` if needed
- Common states 0x00-0x0D are shared and should not be overridden
- For standalone spawns, `ed+0x020` (spawn_slot) and `ed+0x024` (spawn_index) should be -1
- Function pointers from the state table are ALWAYS installed during `EnemyStateChange`, regardless of flags. Flag 0x01 only skips animation setup.

## Key Functions Reference

| Function | Address | Size | Purpose |
|----------|---------|------|---------|
| EnemyStateChange | 0x801fc398 | 0x2d8 | State transition. 5 params: ed, state_id, flags, anim_rate, anim_end_frame. |
| EventActor_ProcResetDamage | 0x801fc670 | -- | Priority 0: zero per-frame damage accumulators |
| EventActor_ProcUpdate | 0x801fc698 | 0x64 | Priority 1: animation + func1 dispatch |
| EnemyPhysicsProc | 0x801fc6fc | 0xc8 | Priority 4: vel/pos integration |
| EventActor_StateMachine | 0x802017d0 | 0x11c | Animation script bytecode processor |
| EnemyState_AnimEnter | 0x8020bd68 | -- | States 0x00-0x08 func1: anim rate management, stun freeze |
| EnemyState_AnimTick | 0x8020be1c | -- | States 0x00-0x08 func2: alive/dead branch, attraction physics |
| EnemyState_AnimExit | 0x8020c558 | -- | States 0x00-0x08 func3: stun countdown, spark VFX |
| EnemyState_DeathEnter | 0x80203e60 | -- | State 0x09: death sequence, next-frame destroy |
| EnemyState_InhaledTick | 0x80203a50 | -- | State 0x0A func2: attachment position update |
| EnemyState_InhaledExit | 0x80203b28 | -- | State 0x0A func3: inhale timer, destroy at 120 frames |
| EnemyState_InhaledFunc4 | 0x80203b64 | -- | State 0x0A func4: mouth bone tracking, shrink toward rider (named `EnemyState_InhaledFunc4` in map) |
| EnemyState_KnockbackEnter | 0x8020ddb4 | -- | State 0x0B func1: knockback velocity computation |
| EnemyState_KnockbackTick | 0x8020de90 | -- | State 0x0B func2: follow hit source |
| EnemyState_KnockbackExit | 0x8020dfe8 | -- | State 0x0B func3: recovery timer, ground check |
| EnemyState_LaunchedTick | 0x8020e338 | -- | State 0x0C func2: spline projectile motion |
| EnemyState_LaunchedExit | 0x8020e61c | -- | State 0x0C func3: shadow update |
| EnemyState_SlidingTick | 0x8020e7e0 | -- | State 0x0D func2: friction deceleration |
| EnemyState_SlidingExit | 0x8020e954 | -- | State 0x0D func3: ground collision response |
| EnemyKnockback_Default | 0x8020bcd8 | 0x90 | Default knockback handler, 8-entry jump table at 0x804b2b50. |
| EnemyActor_DistToPlayer | 0x801fffa4 | 0x60 | Distance from enemy to player |
| EnemyActor_RumblePlayer | 0x801ff80c | 0x58 | Trigger controller rumble (not direct damage) |
| EnemyActor_FindNearestPlayer | 0x801ffd78 | 0x20c | Target selection: nearest rider within detection range (50.0), retarget cooldown 20-39 |
| EnemyActor_FindNearestPlayerFOV | 0x801ff8d8 | 0x3f4 | Target selection with forward-hemisphere angle check + bone-based melee aim |
| EnemyActor_PlayerAheadDist | 0x801fea60 | 0xd0 | Per-player forward-axis dot test: returns signed forward distance + behind flag |
| EnemyActor_FindNearestPlayerAhead | 0x801fe5d4 | 0x190 | Cone scanner (Waddle Dee): nearest rider in a forward cone within range; returns player index or -1 (was `zz_801fe5d4_`) |
| EnemyActor_FindPlayerInRangeFwd | 0x801fe764 | 0x178 | Cone scanner (TAC grab probe): nearest in-front rider within range (was `zz_801fe764_`) |
| EnemyActor_FindDiveTarget | 0x801fe8dc | 0x184 | Cone scanner (Bronto Burt dive gate): in-range/in-front rider (was `zz_801fe8dc_`) |
| EnemyActor_FlyMovement | 0x8020354c | 0x378 | Shared flyer mover: anchored sin-hover (mode 1) or homing steer (mode 2) + anim root-motion; no gravity/ground (was `zz_8020354c_`) |
| EnemyActor_FlyForward | 0x8020335c | 0xfc | Shared flyer mover: advance along facing at speed (ballistic); adds basis×speed into pos (was `zz_8020335c_`) |
| Enemy_SetTerrainLocked | 0x8020ae54 | -- | Sets terrain-locked flag = bit 2 (0x04) of ed+0xB0B (Broom Hatter, Wheelie). Sibling unlock at 0x8020ae68 |
| EventActor_OnCapture | 0x802038c4 | -- | Inhale/capture entry: sets captured flags, dispatches descriptor +0x1C (post_capture_cb), → state 0x0A |
| Tac_Init / Tac_ScatterItems | 0x8021a534 / 0x8021c8ec | -- | TAC init_cb; loot-scatter (CityItem_GetEventItem + CityItem_Throw loop) |
| DynaBlade_Init / DynaBlade_StateDive_Proc / DynaBlade_ThrowItems | 0x8021c9dc / 0x8021d500 / 0x8021db44 | -- | Dyna Blade init; anim-driven swoop; item-rain |
| EnemyActor_ClassifyRange | 0x80206cc0 | 0xd0 | Proximity classifier: actor_data detect/chase range → range bucket in ed+0xB09 bits 3-4 |
| EnemyActor_CombatAI | 0x802069e8 | 0x94 | Two-phase grounded movement (ed+0x908): approach collision probe vs engaged ground physics |
| EnemyActor_CombatMovement | 0x8020b490 | 0x90 | Two-phase movement (ed+0x908): spline AIPhysicsTick vs accel-along-normal ground follow |
| EnemyActor_GroundFollowMovement | 0x80208bd4 | 0x530 | Ground-following chase physics: orientation, speed, terrain raycast, ground-snap |
| Enemy_LoadCommonParams | 0x801fd580 | -- | Loads Enemy.dat `emDataAll`, stores param-table pointer to `*0x805dd878` (was `fn_emLoadCommon`) |
| SwordKnight_Init | 0x802111d8 | -- | Sword Knight init_cb: SpawnChild (rider 0x49) + landing callback trio + FinalizeInit |
| SwordKnight_BeginCombat | 0x80211444 | -- | Sword Knight spawn helper: SetupVelocity + GroundSnap + state→0x0F + path attach |
| SwordKnight_State0FDecide | 0x80211520 | -- | Sword Knight state 0x0F func1: player-crossing slash decision |
| SwordKnight_TriggerAttack | 0x80211734 | 0x8c | Sword Knight attack trigger: EnemyStateChange→0x10 (slash) |
| Scarfy_TargetFOV | 0x8021027c | -- | Scarfy state 0x0E func4: FindNearestPlayerFOV with global detection range |
| Enemy_AIPhysicsTick | 0x802081ec | 0x9e8 | Ground-following movement + pathfinding |
| EnemyPath_Init | 0x80206e2c | 0xd4 | Find nearest spline path |
| EnemyPath_FollowUpdate | 0x80209ce4 | 0x35c | Spline path-following movement |
| EnemyPath_Advance | 0x8020a040 | 0x55c | Advance parametric position along spline |
| EventActor_GroundSnap | 0x80204fac | 0x1b4 | Raycast + snap to ground surface |
| Enemy_GroundPhysicsVelocity | 0x80209104 | 0x5b0 | Velocity-based ground projection |
| Enemy_GroundPhysicsSurface | 0x802096b4 | 0x630 | Surface-advancement ground physics, wall bounce |
| Enemy_GroundAttach | 0x8020a664 | 0x378 | Final ground attachment after path |
| EventActor_UpdateOrientation | 0x802054e4 | 0x38 | Recalculate facing/orientation |
| EventActor_SetupVelocity | 0x80205310 | 0x1d4 | Configure movement speed/direction |
| EventActor_CleanupVfxA3C | 0x8020c6e0 | 0x2c | Stop SFX/VFX at ed+0xA3C handle |
| EventActor_CleanupVfxA40 | 0x8020c70c | 0x2c | Stop SFX/VFX at ed+0xA40 handle |
| `zz_80236358_` (SoundStop) | 0x80236358 | 0xc4 | Low-level sound stop (called by the cleanup helpers). |
| Enemy_GetActorData | 0x801fd498 | 0xe8 | Look up actor_data by ID |

## Data Addresses

| Data | Address | Description |
|------|---------|-------------|
| Common state table | 0x804b2950 | 14 entries (states 0x00-0x0D), 0x14 bytes each |
| Per-type descriptor table | 0x804b1d98 | 79 pointers indexed by ActorID (47 unique descriptors) |
| Actor data table | 0x804b22b4 | {data_index, flags} per ActorID, stride 8 |
| Archive loaded flags | 0x8055a210 | byte per data_index (22 entries) |
| Archive root pointers | 0x8055a228 | pointer per data_index (22 entries) |
| Enemy parameter table pointer | 0x805dd878 | Holds a **pointer** to the param table (from `Enemy.dat` `emDataAll`, set by `Enemy_LoadCommonParams`; NULL until enemies load). Distance ladder: `+0x80`=50.0 (acquisition radius, `FindNearestPlayer`), `+0x84`=30.0 (close), `+0x88`=30.0, `+0x8C`=300.0 (mid), `+0x90`=500.0 (max/leash — dominant range constant, ~15 AI state funcs). Other fields: retarget cooldown (+0x94/+0x98=20/40), damage thresholds (+0x08/+0x0C/+0x10), per-tier knockback (+0x30/+0x40/+0x50/+0x60). Int array at `+0x14..+0x20` = {10,30,50,70} (consumer not yet identified). See [enemy-spawn-system.md](enemy-spawn-system.md) § Damage & Knockback System. |
| Animation script table (enemy) | 0x804b26b0 | Enemy-specific script commands 11-28, 12-byte entries |
| Animation script table (HSD) | 0x80499628 | Generic animation script commands 0-10 (11 entries) |
| Knockback jump table | 0x804b2b50 | 8 entries for hit type mapping |
