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

### Enemy-Specific Commands (11-28) -- Table at 0x804b26b0

12-byte entries in the enemy command table. These extend the generic command set with enemy-specific behaviors.

| Cmd | Handler | Purpose |
|-----|---------|---------|
| 11 | 0x80200e58 | State change from script |
| 12 | 0x80200eb8 | Set animation parameters |
| 13 | 0x80201138 | Spawn VFX/particle |
| 14 | 0x80201180 | Set flag/attribute |
| 15 | 0x802011bc | Enable/disable collision |
| 16 | 0x80201418 | Set movement target |
| 17 | 0x80201488 | Damage region setup |
| 18 | 0x80201498 | Sound effect variant |
| 19 | 0x80201504 | Set velocity/impulse |
| 20 | 0x8020152c | Ground attachment |
| 21 | 0x80201578 | Scale/transform |
| 22 | 0x802016ac | Spawn child/projectile |
| 23 | 0x8020172c | Set AI target |
| 24 | 0x8020174c | Timer/counter set |
| 25 | 0x80201798 | Callback registration |

Commands 26-28 exist in the table but are rarely used by standard enemy types.

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

**Waddle Dee (0x17)** -- state table at 0x804b3e78, **5 entries (states 0x0E-0x12)**:

| State | Entry | Anim | Purpose (inferred) |
|-------|-------|------|--------------------|
| 0x0E | 0 | -1 | Default/spawn -- init position, play idle, begin AI |
| 0x0F | 1 | 0x0E | Walk variant -- ground-snap movement (func3/func4 set) |
| 0x10 | 2 | 0x0F | Walk variant -- func1/func2/func3 set |
| 0x11 | 3 | 0x10 | Walk/turn -- all four funcs set |
| 0x12 | 4 | 0x11 | Walk/patrol -- func1/func3/func4 set |

Waddle Dee AI loop: spawn -> idle -> random walk direction -> walk -> back to idle.

**Sword Knight (0x05)** -- state table at 0x804b3118, **4 entries (states 0x0E-0x11)**:

| State | Entry | Anim | Purpose (inferred) |
|-------|-------|------|--------------------|
| 0x0E | 0 | -1 | Default/spawn -- initialize (func1 only) |
| 0x0F | 1 | 0x0E | Idle/combat -- all four funcs set |
| 0x10 | 2 | 0x0F | Attack/combat -- all four funcs set |
| 0x11 | 3 | 0x10 | Chase/pursue -- all four funcs set |

Sword Knight AI loop: idle -> detect player -> chase -> attack at close range -> cycle.

**Wheelie (0x08)** -- state table at 0x804b3350, **3 entries (states 0x0E-0x10)**:

| State | Entry | Anim | Purpose (inferred) |
|-------|-------|------|--------------------|
| 0x0E | 0 | -1 | Default/spawn -- initialize (func1 only) |
| 0x0F | 1 | 0x0E | Idle/roam -- func2/func3 set (uses `Enemy_AIPhysicsTick`, 0x802081ec) |
| 0x10 | 2 | 0x0F | Active/drive -- func2/func3 set, ground-snap movement |

**Gordo (0x0E)** -- state table at 0x804b3808, **3 entries (states 0x0E-0x10)**:

| State | Entry | Anim | Purpose (inferred) |
|-------|-------|------|--------------------|
| 0x0E | 0 | -1 | Default -- func1 only |
| 0x0F | 1 | 0x0E | Bounce -- func1/func4 set, oscillates Y position |
| 0x10 | 2 | 0x0F | Death/timer -- func1/func4 set |

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
    void *post_capture_cb; // +0x18: Called when inhaled/captured by rider
    void *damage_cb;       // +0x1C: Non-null for most T0 enemies. Likely "while captured" or "on damage" callback.
} PerTypeDescriptor;       // 0x20 bytes
```

Multiple actor types can share descriptors (e.g., IDs 0x00 and 0x01 both point to 0x804b2dd8). T1 and T2 of the same enemy always share the same descriptor pointer.

All callback addresses fall in the range 0x8020EA44-0x8021E0D4 (~60KB block).

### Per-Type State Complexity

Per-type state counts (count = entries between the table pointer and the next datum). Counts for enemies not listed here are not individually established.

| Enemy | State Table | Per-Type States (entry count) | Notes |
|-------|-------------|-------------------------------|-------|
| TAC (0x4C) | 0x804b4088 | 12 (states 0x0E-0x19) | Most complex enemy AI |
| Waddle Dee (0x17) | 0x804b3e78 | 5 (states 0x0E-0x12) | Simple patrol |
| Sword Knight (0x05) | 0x804b3118 | 4 (states 0x0E-0x11) | Chase + attack |
| Wheelie (0x08) | 0x804b3350 | 3 (states 0x0E-0x10) | Roam/drive |
| Gordo (0x0E) | 0x804b3808 | 3 (states 0x0E-0x10) | Bounce + timer |

The first entry of every per-type table (state 0x0E, entry 0) has `anim_idx = -1` and is the default/spawn entry. Simpler enemies have fewer entries; combat enemies have more.

### Init Callback Pattern

All per-type init callbacks follow a common pattern:

1. Call `EventActor_GroundSnap` (0x80204fac, ground snap/raycast) with scale parameter
2. Set render flags via `EventActor_DisableRendering` (0x802041b0)
3. Set 3 damage reaction callbacks at `ed+0xACC`/`ed+0xAD0`/other offsets
4. Optionally set AI callback at `ed+0xAC8`
5. Call `EventActor_FinalizeInit` (0x802042fc, finalize init -- animation setup, collision)
6. Optionally call `Enemy_SetTerrainLocked` (0x8020ae54) to set terrain-locked flag (Wheelie, Gordo do this; Waddle Dee does not)

### Special Variants (0x48-0x4E)

| ID | Type | Behavior |
|----|------|----------|
| 0x48-0x4A | Child parts (SP Broom Hatter, SP Sword Knight, SP Waddle Dee Truck) | No init, no default state. Mirror parent's transform via `EventActor_FollowParent` (0x80219eec). `parent_gobj` from descriptor. |
| 0x4B | Event Gordo | Independent actor, own init and behavior |
| 0x4C | TAC | Independent, own behavior (chases players, steals items). 12 per-type states (0x0E-0x19). |
| 0x4D | Dyna Blade | Independent, complex flight/swoop AI |
| 0x4E | Meteor | See [meteor-actor.md](meteor-actor.md) |

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

**Detection range:** Read from global param table at `*(r13+0x798)+0x80`.

**Retarget cooldown:** Random value in range `[table+0x94, table+0x98]`, preventing simultaneous retargeting of all enemies when a player moves.

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
| 2 | Unused | -- |
| 3 | Enemy-on-enemy | Collision between two enemies |
| 5 | Special | Used by scripted events |

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
| +0x08 | ptr | **Anim/model joint data** | Animation init |
| +0x0C | ptr | **Animation state table** -- 0x10-byte entries | `EnemyStateChange` indexes by `anim_idx * 0x10` |
| +0x10 | ptr | **Material/texture animation data** | Material update |
| +0x14 | ptr | Additional data | |

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
| 0x048 | void* | anim_data | Current animation data pointer (`*(actor_data + 0x0C) + anim_idx * 0x10`) |
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
| 0xAC8 | func_ptr | per_type_cb | Priority 7. Cleared to 0 on **every** state change (unconditional, regardless of flag 0x10). |
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

## Custom AI: Key Integration Points

To create custom enemy AI (e.g., for traps), you can:

1. **Spawn an existing actor type** via `EventActor_Create` and let vanilla AI handle it
2. **Override state callbacks** after creation by writing custom function pointers to `ed+0xAB8-0xAC4`
3. **Add a custom GOBJProc** via `GObj_AddProc(gobj, callback, priority)` at a chosen priority
4. **Manipulate physics directly**: write to `ed+0x2E0` (accel), `ed+0x2EC` (vel), `ed+0x2F8` (pos)
5. **Use targeting helpers**: `EnemyActor_DistToPlayer` (0x801fffa4) for distance, `EnemyActor_FindNearestPlayer` (0x801ffd78) for nearest-player targeting

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
| EnemyActor_FindNearestPlayer | 0x801ffd78 | 0x20c | Target selection with cooldown |
| EnemyActor_FindNearestPlayerFOV | 0x801ff8d8 | 0x3f4 | Target selection with 180-degree hemisphere check |
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
| Enemy parameter table | 0x805dd878 | Global detection range (+0x80), retarget cooldown (+0x94/+0x98), knockback launch (+0x50/+0x60) |
| Animation script table (enemy) | 0x804b26b0 | Enemy-specific script commands 11-28, 12-byte entries |
| Animation script table (HSD) | 0x80499628 | Generic animation script commands 0-10 (11 entries) |
| Knockback jump table | 0x804b2b50 | 8 entries for hit type mapping |
