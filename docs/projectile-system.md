# Projectile System

The projectile is Kirby Air Ride's actor type for transient, physics-driven objects: thrown bombs,
fired bullets, hovering auras, Gordo, Phan-Phan throws, firecrackers. All 17 `ProjectileKind` values
share one GObj/proc class (entity class 23, p_link 14) and one update pipeline, and specialise
through a per-kind vtable plus a per-kind state table.

## Object Layout

A projectile is a GObj with two pieces of user data:

- The **outer handle** returned by `Projectile_Create`, a small GObj tracking block whose only
  publicly-useful property is that `*(handle + 0x2c)` points at the inner data struct.
- The **inner `ProjectileData`**, a 0x220-byte (544) block allocated from the HSD object pool at
  `0x8055a8f8` and zeroed by `memset(proj, 0, 544)` at `0x8021f4a4`. All per-projectile state lives
  here: kind, current state, position, velocity, HurtData, particle-effect handles, per-state
  callback pointers.

The GObj is registered with entity class 23 (rider = 14, machine = 15, enemy = 21) and p_link 14
(`GAMEPLINK_PROJECTILE`), the global list scanned by rider, machine, item, and box hit checks.

## Per-Kind VTable

`ProjectileKind` indexes a pointer array at `0x804b4338`. Each entry points to an 8-word
(0x20-byte) vtable (`ProjKindVTable` in `projectile.h`):

| Offset | Field | Called from | Purpose |
|-------:|-------|-------------|---------|
| +0x00 | `state_table` | `Projectile_SetState` @ `0x8021f888`-`0x8021f8a0` | Array of 24-byte state entries. |
| +0x04 | reserved | - | Always 0 in all 17 kinds. |
| +0x08 | `init(proj)` | `Projectile_Create` @ `0x8021f660` | One-shot, early. Clears/inits per-kind scratch. |
| +0x0c | `refreshXfmA(proj)` | per-frame | Copies the model's JObj matrix into proj scratch. |
| +0x10 | `refreshXfmB(proj)` | per-frame | Byte-identical to `refreshXfmA` in every kind sampled. |
| +0x14 | `auxA(proj)` | state-exit / despawn | Cleanup. Bomb's `Bomb_AuxA_RemoveEffect` (`0x8022634c`) removes its lingering FADE particle effect. Can be NULL. |
| +0x18 | `postInit(proj)` | `Projectile_Create` @ `0x8021f7a0` | One-shot, late. Typical body: `Projectile_SetState(this, 0, ...)` plus the spawn-time `Effect_SpawnSync`. |
| +0x1c | `auxB(proj)` | state dispatcher (per-frame) | Kind-specific rendering/cleanup hook. Can be NULL. |

Two kind pairs alias at the pointer level: `PROJKIND_PLASMA_A` (5) and `PROJKIND_PLASMA_B` (6) both
use `0x804b47e0`; `PROJKIND_PLASMA_SPREAD_MID` (7) and `_SIDE` (8) both use `0x804b4848`. What
separates each pair is the **per-kind data** at `0x8055a9a8[kind]` (model/art/stats), not code.

## State Tables

Each kind's state table is an array of 24-byte entries:

```
struct ProjectileStateEntry {
    u32  state_id;     // +0x00: semantic id; 0xffffffff is a sentinel
    u32  flags;        // +0x04: animation-class + behaviour bits
    void (*fn0)(proj); // +0x08: per-frame slot, run at GObj prio 1
    void (*fn1)(proj); // +0x0c: per-frame slot, run at GObj prio 4
    void (*fn2)(proj); // +0x10: per-frame slot, run at GObj prio 5
    void (*fn3)(proj); // +0x14: per-frame slot, run at GObj prio 6
};
```

`Projectile_SetState(proj, index, fA, fB, flags)` (`0x8021f7dc`):

1. Selects `state_table[index]` from `proj+0x30` when `index < proj+0x28`, else from `proj+0x34`
   with offset `(index - proj+0x28) * 24`. `Projectile_Create` hardcodes `proj+0x28` to 0
   (@ `0x8021f508`) and nothing rewrites it, so the `proj+0x30` branch is dead in vanilla. Every
   dispatch uses `proj+0x34`, loaded by `Projectile_Create` (@ `0x8021f51c`) with the per-kind
   `state_table` (vtable +0x00).
2. Writes `proj+0x24 = index`, `proj+0x2c = entry.state_id`, copies `entry.fn0..fn3` into
   `proj+0x150..0x15c`, and writes `proj+0x38 = kind_data[0x0C] + state_id*16` (the per-state
   animation/blend spec, 16 bytes per entry - **not** the 24-byte state_table entry).
3. Runs the animation transition via `Projectile_AssignStateFlags`.

All four `fn0..fn3` slots are **per-frame** callbacks, not on-enter/on-exit hooks; transitions come
from `Projectile_SetState` calls made inside `fn0/fn1/fn2` themselves. For a one-shot on-enter, use
the per-kind vtable's `init` or `postInit` - there is no entry-level on-enter slot. The ten GObj
procs registered by `Projectile_Create` dispatch the slots each frame: prio 1 calls `proj+0x150`,
prio 4 calls `proj+0x154`, prio 5 calls `proj+0x158`, prio 6 calls `proj+0x15c`.

### Per-kind state tables

Entries are indexed 0..N-1. `state_id` is the value that lands in `proj+0x2c`; pass the **index** to
`Projectile_SetState`, since `state_id` can duplicate across entries.

| Kind | Table | Entries | State IDs |
|-----:|-------|:-------:|-----------|
| 0  SWORD_STAR_A       | `0x804b4588` | 1 | 0 |
| 1  SWORD_STAR_B       | `0x804b45c8` | 1 | 0 |
| 2  FIRE_BULLET        | `0x804b4648` | 3 | 0, 1, -1 (sentinel) |
| 3  FIRE_AURA          | `0x804b46b8` | 3 | 0, 1, 2 |
| 4  BOMB               | `0x804b4728` | 4 | 0, 1, 2, 3 |
| 5  PLASMA_A           | `0x804b47b0` | 2 | 0, 1 |
| 6  PLASMA_B           | `0x804b47b0` (shared with 5) | 2 | 0, 1 |
| 7  PLASMA_SPREAD_MID  | `0x804b4830` | 1 | 0 |
| 8  PLASMA_SPREAD_SIDE | `0x804b4830` (shared with 7) | 1 | 0 |
| 9  PLASMA_C           | `0x804b4870` | 2 | 0, 1 |
| 10 PLASMA_D           | `0x804b48d0` | 2 | 0, 1 |
| 11 SWORD_STAR_CHARGED | `0x804b4608` | 1 | 0 |
| 12 SPIKE_AURA         | `0x804b4928` | 3 | 0, 1, 2 |
| 13 ICE_AURA           | `0x804b4998` | 3 | 0, 1, 2 |
| 14 FIRECRACKER        | `0x804b4a08` | 2 | 0, 1 |
| 15 SENSORBOMB         | `0x804b4a88` | 5 | 0, 1, 2, 3, **2** |
| 16 GORDO              | `0x804b4b50` | 4 | 0, 1, **1**, -1 |

### State semantics

These are the indices to pass to `Projectile_SetState`; the matching enums are in `projectile.h`.

**`PROJKIND_BOMB`** (4 states)
- 0 HELD - fn3 snaps to the rider hand bone (`bl 0x80191ffc`), all other fn slots `blr`.
- 1 THROWN - fn1 integrates velocity into position; fn2 runs the `mpColl` hit check (`0x80221fd4`)
  and branches to detonate on env-coll.
- 2 EXPLODING - fn0 decrements a detonation timer (`proj+0x1c0`), transitions to state 3 at zero.
- 3 FADE - fn0 decrements `proj+0x1dc` (lifetime) and `proj+0x1d8` (alpha); triggers `GObj_Destroy`
  when lifetime expires.

**`PROJKIND_SENSORBOMB`** (5 entries; state_id 2 appears twice, so pass the index)
- 0 HELD
- 1 ARMED_FLYING - physics + proximity scan
- 2 ARMED_STATIONARY - landed, waiting; short-range sensor + timer
- 3 EXPLODING
- 4 FADE (state_id 2 again; distinct callbacks from index 2)

**`PROJKIND_GORDO`** (4 entries; state_id 1 appears twice; final entry is sentinel -1)
- 0 HELD
- 1 THROWN_ASCENDING - scales up to full size while flying.
- 2 THROWN_TIMED - scale locked, self-despawn timer; index 1 transitions here internally when its
  scale check completes.
- 3 DESPAWN - state_id sentinel -1. `Projectile_SetState` zeros `proj+0x38` here but still installs
  `fn0..fn3`, so the sentinel's fn0 (`0x8022b09c`) runs for one frame to spawn the despawn particle
  burst before teardown. This is the only vanilla use of the sentinel pattern inside a live state
  transition.

**`PROJKIND_FIRE_BULLET`** (3 entries; final is sentinel -1)
- 0 THROWN - flying, damaging.
- 1 HIT_PAUSE - all-`blr` stub held for animation time, most likely the hit-flash freeze frame.
- 2 DESPAWN - sentinel; fn0 is a rider-alive watchdog / cleanup.

**`PROJKIND_FIRE_AURA` / `_SPIKE_AURA` / `_ICE_AURA`** (3 entries each)
- 0 IDLE - fn3 re-snaps to the rider hand bone every frame; all other slots `blr`.
- 1 FIRING / EMITTING / DEPLOYED - per-kind "active" state. Only ICE has a real per-frame fn0
  (hurtbox tick); SPIKE differs from IDLE only in animation flags.
- 2 COOLING / SETTLED - structurally identical to IDLE with a different animation class. The label
  is an interpretation; "retract" or "idle2" would fit the code equally well.

**`PROJKIND_PLASMA_A/B`**, **`_PLASMA_C`**, **`_FIRECRACKER`** (2 entries each)
- 0 FLYING / DAMAGING - physics + collision.
- 1 ABSORBED - absorbed into rider/charger; fn0 is a rider-alive watchdog reading
  `proj+0x1b8`/`0x1bc`.

**`PROJKIND_PLASMA_D`** (2 entries, both active)
- 0 FLYING - timed, with a per-frame trail emitter in fn3.
- 1 TRAILING - timed tail fragment; could instead be a split phase of the same shot.

**`PROJKIND_SWORD_STAR_*` / `_PLASMA_SPREAD_*`** (1 entry each)
- 0 THROWN - fires once, dies on impact. No held or explode phase.

### State flags word

The `flags` word at entry+0x04 is routed through `Projectile_AssignStateFlags` at **`0x80222298`**
(size 0x74; called from `Projectile_SetState` at `0x8021f92c`). Its body, in order:

```
mr    r31, r3              ; r31 = proj
stw   r4, 8(r1)            ; spill the flags arg to stack
lbz   r3, 11(r1)           ; r3 = flags & 0xff  (the LOW byte = anim-class tag)
cmplwi r3, 0
beq   .mint                ; low == 0  -> mint a fresh id (this is NOT a skip)
lbz   r0, 0x17f(r31)       ; r0 = previous low byte (LSB of the proj+0x17c word)
cmplw r3, r0
beq   .store               ; low unchanged from last time -> keep the current id
.mint:
bl    0x80231b68           ; AllocSeqId16(): next nonzero u16 from a global counter
sth   r3, 0x194(r31)       ; proj+0x194 = freshly minted sequence id
.store:
lwz   r0, 8(r1)
stw   r0, 0x17c(r31)       ; proj+0x17c = flags (full 32 bits; its LSB feeds the +0x17f compare next time)
stw   r3=0, 0x184(r31)     ; proj+0x184 = 0
lhz   r0, 0x18a(r31)
rlwimi r0, r3=0, 1, 23, 30 ; clear middle 8 bits of proj+0x18a
sth   r0, 0x18a(r31)
lbz   r0, 0x18b(r31)
rlwimi r0, r3=0, 0, 31, 31 ; clear bit 31 of proj+0x18b
stb   r0, 0x18b(r31)
```

The flag word is stored verbatim at `proj+0x17c`, and its low byte is what `proj+0x17f` reads back on
the next transition. The low byte does **not** index a lookup table: `AllocSeqId16` (`0x80231b68`)
takes no arguments - it reads, increments, and writes back a global `u16` counter at `0x805DD8A0`
(`r13+0x7c0`), returns the pre-increment value, and resets to 1 on wrap-to-0. What the low byte gates
is *whether a fresh id is minted*: `proj+0x194` gets a new sequence id when the low byte is 0 **or**
differs from the previous transition's low byte; if the anim class is unchanged and nonzero, the old
id is kept. `proj+0x194` is therefore an animation-instance/generation counter that bumps on every
anim-class change, not an id resolved from the low byte. `proj+0x184` and parts of
`proj+0x18a`/`0x18b` are zeroed each transition.

The full set of flag values across the 17 kinds' state tables:

| Value | Used by (kind / state) |
|-------|------------------------|
| 0x0000 | held states, aura idle, FIRE_BULLET sentinel, SWORD_STAR_CHARGED |
| 0x0101 | FIRE_AURA idle |
| 0x0102 | FIRE_BULLET thrown + hit-pause |
| 0x0108 | BOMB (all states) |
| 0x0109 | PLASMA A/B/C/D flying, PLASMA_SPREAD |
| 0x010a | SPIKE_AURA idle |
| 0x010b | SPIKE_AURA deployed |
| 0x010d | ICE_AURA emitting |
| 0x010f | SWORD_STAR_A |
| 0x0111 | FIRECRACKER |
| 0x0112 | SENSORBOMB held..exploding |
| 0x0113 | GORDO thrown states |
| 0x0115 | SWORD_STAR_B |

The upper byte is 0 or 1, a boolean "animated state". The lower byte is effectively a per-kind
animation-class tag rather than a bit field: no state table uses more than two distinct flag values
(one "animated", plus 0x0000 for held/sentinel), and `Projectile_AssignStateFlags` compares the low
byte as a whole value against the previous one instead of testing bits. These stored words are the
inputs the per-kind `refreshXfm*` callbacks read each frame to drive the HSD animation object.

## Per-Frame Update Procs

`Projectile_Create`'s epilogue registers ten GObj procs on the projectile GObj via `GObj_AddProc`
(`0x804288a4`). Each priority is one step of a fixed pipeline; the priority ranks are the same ones
used by other actor types.

| Prio | Addr | Name | What it does |
|-----:|------|------|--------------|
| 0 | `0x8021f9b4` | `Projectile_Proc0_FrameStart` | `proj+0x110++` (frame counter), zero accel, `HurtData_ResetFrame`, tick the `proj+0x134` intang timer, call the `proj+0x160` user hook. |
| 1 | `0x8021fa18` | `Projectile_Proc1_RunStateFn0` | `HurtData_UpdatePerFrame`, call `state_fn0` at `proj+0x150`, then tick `proj+0x10c` lifetime and despawn at zero. |
| 4 | `0x8021faa4` | `Projectile_Proc4_Physics` | Call `state_fn1` at `proj+0x154`; integrate `vel += accel`, `pos += vel`, `pos_prev += vel`. |
| 5 | `0x8021fb44` | `Projectile_Proc5_RunStateFn2` | Clear the env-coll flag, call `state_fn2` at `proj+0x158`. |
| 6 | `0x8021fb88` | `Projectile_Proc6_RunStateFn3` | Call `state_fn3` at `proj+0x15c`, then `0x80220310` (mpColl pos sync), then a per-kind sub-cleanup. |
| 7 | `0x8021fbec` | `Projectile_Proc7_PostState` | Call the `proj+0x164` user hook. If `pos.y < floor_threshold`, `GObj_Destroy`; else update HurtData radius/position from `proj+0x74`/`0x78`. |
| 8 | `0x8021fc70` | `Projectile_Proc8_Stub` | Single `blr`. Reserved priority. |
| 9 | `0x8021fc74` | `Projectile_Proc9_HitColl` | Hit detection. Scans the rider / machine / projectile / item / stage-hazard lists. |
| 10 | `0x8021fcd4` | `Projectile_Proc10_HitReact` | Resolve the strongest logged hit, run the per-kind on-hit callback; state-transition if it returns non-zero. |
| 21 | `0x8021fed4` | `Projectile_Proc21_EndOfFrame` | Compute `vel_diff = pos - pos_prev`, save `pos -> pos_prev`, finalise the HurtColl attach. |

All ten attach at create time. Per-kind differences are expressed through the fn0..fn3 state entries,
not through extra procs.

## Hit Detection And Damage

### Outbound scans (prio 9)

`Projectile_Proc9_HitColl` at `0x8021fc74` iterates five p_link lists:

| Function | Address |
|----------|---------|
| `Projectile_CheckRiderCollision` | `0x802215a4` |
| `Projectile_CheckMachineCollision` | `0x80221660` |
| `Projectile_CheckProjectileCollision` | `0x8022171c` (skips self) |
| `Projectile_CheckItemCollision` | `0x80221814` |
| `Projectile_CheckStageHazardColl` | `0x80221878` |

Each follows the same skeleton: walk the target list, filter by pause/disabled gating, then call
`HitColl_CheckCollision(victim_hurt_data, proj_hurt_data)` at `0x8018d284` (r3 = victim, r4 =
attacker).

The rider-side outbound scan additionally calls `HitColl_CheckIfSamePlayer` (`0x8000b024`,
`r3 = owner_gobj`, `r4 = victim_gobj`) for owner exclusion. On a same-player match the scan skips
unless **`proj+0x1b5` bit 4** ("outbound self-hit allow") is set - a *different* flag from the
inbound side's.

### Inbound scans

Victims also scan the projectile global list:

| Function | Address |
|----------|---------|
| `Rider_CheckProjectileHit` | `0x801963c8` |
| `Machine_CheckProjectileCollision` | `0x801d7118` |
| `Box_CheckProjectileCollision` | `0x80252334` |

Each reads `Projectile_GetOwnerGObj(proj) = *(projGObj+0x2c+0x08)` (3-instruction accessor at
`0x8022312c`) for owner exclusion: if `proj->owner_gobj == victim_gobj`, skip the hit unless bit 0
of `proj+0x1b4` (the inbound "allow self-hit" flag, default off) is set.

A fourth reader, the enemy-side inbound check at `0x802020d4`, fetches a projectile's HurtData via
the accessor at `0x80223120` (`gobj->userdata` then `+0x108`).

### Damage values

Per-region params (damage, knockback, radius) live in the projectile's HurtData, populated at init
from `kind_data+0x10` (the `hurt_region_spec`: `+0x00` is the region-descriptor array base, `+0x04`
the region count). Regions are built by `HurtData_Create` (`0x8018c1c8`) plus `HurtData_InitRegion`
(`0x8018c598`) at a 0x18-byte stride.

Explosion-class projectiles cache a handful of scalars at `proj+0x1d0..0x1ec` on the EXPLODING ->
FADE transition, but those come from **per-projectile** blocks, not `kind_data`: `proj+0x1e4`/`0x1e0`
are copied from HurtData regions 0/1 (via `*(proj+0x108)+0x0c`, field `+0x18`), and the fade/alpha
ramp (`proj+0x1d8` alpha, `proj+0x1dc` lifetime, `proj+0x1e8`/`0x1ec` rate) is read from the
render-state block at `proj+0x104`. No bomb state function dereferences `kind_data` at all.

To override projectile damage:

- **Per-spawn**: hook the exit of `Projectile_InitHurtData` (`0x80221440`), walk the new HurtData's
  regions (stride 0xC8), and rewrite `base_damage` at `+0x04` / `base_knockback` at `+0x24`.
- **Per-hit**: hook `HitColl_SetDamageLog` (`0x8018cf94`) and discriminate by the attacker's HurtData
  kind.

### Self-hit allow flags

Owner exclusion is gated by *separate* bits on the inbound and outbound scan paths, living on
different bytes of `ProjectileData`:

| Scan side | Flag bit | Used by | Set by vanilla? |
|-----------|----------|---------|-----------------|
| Inbound (rider/machine/box walks the projectile list) | `proj+0x1b4` bit 0 | `Rider_CheckProjectileHit` / `Machine_CheckProjectileCollision` / `Box_CheckProjectileCollision` | Sensor bomb's `post_init` (`0x80228d8c`) sets it. Bomb and gordo do not. |
| Outbound (`Projectile_CheckRiderCollision` walks the rider list) | `proj+0x1b5` bit 4 | `0x802215a4` | Never set at create time on bomb / sensor bomb / gordo - vanilla throws target *other* players, so the default exclusion is what they want. |

**A projectile that must damage its own owner-player has to set both.** Custom-spawned trap
projectiles, where `owner_gobj` is the trapped player, are the canonical case:

```c
ProjectileData *proj = Projectile_GetData(handle);
proj->flag_a |= PROJ_ALLOW_SELF_HIT_INBOUND;   // proj+0x1b4 bit 0
proj->flag_b |= PROJ_ALLOW_SELF_HIT_OUTBOUND;  // proj+0x1b5 bit 4
```

Setting only one risks the hit being silently dropped depending on which scan path resolves it first.
Setting both is safe: the flags gate only same-player exclusion, so non-owner targets are unaffected.

## Lifecycle

### Creation

`Projectile_Create` (`0x8021f428`):

1. `GObj_Create(kind=23, pLink=14, prio=0)` -> outer gobj.
2. Register render at `0x80220000` via `GObj_AddGXLink`.
3. `HSD_ObjAlloc` the 0x220-byte `ProjectileData` from pool `0x8055a8f8`.
4. `GObj_AddUserData(gobj, 23, dtor=Projectile_UserDataDtor, ud=proj)` - the dtor at `0x8021ff54`
   frees everything at teardown.
5. `memset(proj, 0, 544)`.
6. Copy descriptor fields (kind, owner, position, forward/up, velocity, type_flag, charge) into proj.
   Set `proj+0x1b5` bit 2 and `proj+0x218` bit 0 (always-on "alive" markers).
7. Build the orientation matrix via `0x80220250` (PSMTXNormalize/Cross).
8. Allocate sub-resources: scratch mtx, sub-vtable table (`proj+0x6c`), render-state block
   (`proj+0x104`, via `Projectile_AllocRenderState` `0x802205b0`, holding the alpha/color/scale ramp
   fields), two particle-effect handles (`proj+0x114`/`0x118`, allocated by `0x802364e0`), text/vfx
   slot (`proj+0x10`), `mpColl` CollData (`proj+0x138`, if the kind wants one), anim object
   (`proj+0x140`/`0x148`), and HurtData via `Projectile_InitHurtData` (`0x80221440`). The model joint
   is loaded by `HSD_JObjLoadJoint` (`0x8040afe8`) from `kind_data+0x08`, or a global default when
   that pointer is NULL.
9. Call the per-kind **init** (vtable +0x08).
10. Register the ten GObj procs (priorities 0, 1, 4, 5, 6, 7, 8, 9, 10, 21).
11. Run the reset chain (`0x8021f2a0`, `0x80220310`, `0x80220654`, `0x80220230`, `0x80221c9c`,
    `0x80220578`, `0x80221534`, `0x80221300`, `0x80222240`) to zero accel/velocity and enter state 0.
12. Call the per-kind **postInit** (vtable +0x18). For throwable kinds this spawns the "spawn"
    particle effect via `Effect_SpawnSync` (`0x80236c40`).
13. Return the outer gobj. The inner `ProjectileData` is `*(gobj + 0x2c)`.

### Destruction

Two vanilla paths:

1. **Lifetime expiry** via `Projectile_Despawn` (`0x80220364`), triggered from prio 1 when
   `proj+0x10c` ticks to 0. Runs the per-kind `auxA` first, then `GObj_Destroy(gobj)`.
2. **Fell into the void** via prio 7 when `proj->pos.y` drops below a threshold loaded from
   `r2-16104`. Calls `GObj_Destroy` directly.

Both unwind through the GObj destructor, which invokes `Projectile_UserDataDtor` (`0x8021ff54`).
That dtor destroys the HurtData; stops and frees the two particle-effect handles
(`proj+0x114`/`0x118`, via the Effect-module helpers `0x8023641c` and `0x80236778`); runs the
per-kind `aux_a` (vtable +0x14), which for BOMB reaps the lingering FADE-state effect at
`proj+0x1c8`/`0x1cc`; destroys the text/vfx slot, anim obj (`proj+0x140`/`0x148`), render-state block
(`proj+0x104`), and mpColl CollData; then returns the `ProjectileData` block to its HSD pool.

### Auras and the rider backref

Aura kinds (`PROJKIND_FIRE_AURA`, `SPIKE_AURA`, `ICE_AURA`) spawn with zero velocity. Each copy
ability's `AbilityInit` handler stores the returned GObj handle at **`rider+0x3F0`**:

| Ability | Init (stores handle) | Lose (reads and destroys) |
|---------|----------------------|---------------------------|
| Fire | `Fire_AbilityInit` `0x801aed50` (calls `spawnFireAura`, then `stw r3, 0x3F0(rider)`) | `Fire_LoseAbility_Exit` `0x801af330` |
| Spike | `Spike_AbilityInit` `0x801b385c` (calls `spawnSpikeAura`, then `stw r3, 0x3F0(rider)`) | `Spike_LoseAbility_Exit` `0x801b3d18` |
| Ice | `Ice_AbilityInit` `0x801b4718` (calls `spawnIceAura`, then `stw r3, 0x3F0(rider)`) | `Ice_LoseAbility_Exit` `0x801b49d4` |

Each `*_LoseAbility_Exit` does the same three things:

1. Load `rider+0x3F0`; if NULL, skip the destroy.
2. Test a rider flag byte (Fire reads bit 4 of `rider+0x824`). If set, call `GObj_Destroy` directly
   (hard teardown, skipping `aux_a`); otherwise call `Projectile_DespawnGObj` (`0x802230a0`), which
   runs the per-kind `aux_a` cleanup first. The gating bit reads as
   "destroy-without-particle-cleanup", used when the rider is being wholesale reset (respawn, round
   end).
3. Zero `rider+0x3F0`.

`Projectile_DespawnGObj` is just `proj = *(gobj+0x2c); Projectile_Despawn(proj);`.

Only these three auras use `rider+0x3F0`. Throwable kinds (`BOMB`, `SENSORBOMB`, `GORDO`) keep their
projectile handle inside the ability's own state block instead.

## ProjectileData Offsets

Offsets not listed are either zeroed and unused or kind-specific scratch. The struct declaration is
`ProjectileData` in `externals/hoshi/include/projectile.h`.

| Offset | Name | Notes |
|-------:|------|-------|
| 0x00 | `gobj` | Back-pointer to the outer GObj. |
| 0x04 | `kind` | `ProjectileKind`. |
| 0x08 | `owner_gobj` | Owner rider/machine GObj. **Self-hit exclusion key.** |
| 0x0c | `owner_unk2` | Duplicate of owner for hit attribution. |
| 0x14 | `owner_byte` | Usually 0. |
| 0x20 | `kind_data` | `0x8055a9a8[kind]` entry; per-kind data pointer. |
| 0x24 | `state_index` | Current entry index (arg of `Projectile_SetState`). |
| 0x28 | `state_table_split` | Index cutoff between `proj+0x30` and `proj+0x34`. Hardcoded to 0 by `Projectile_Create`, never updated, so `proj+0x30` is dead in vanilla. |
| 0x2c | `state_id` | `state_table[state_index].state_id`. |
| 0x30 | `state_table_ext` | Extension state table; stays at its memset-0 default and is never read. Reserved for mod-provided extension sets. |
| 0x34 | `state_table` | Primary state table (loaded by `Projectile_Create` from vtable +0x00). Every vanilla `SetState` dispatch reads it. |
| 0x38 | `state_anim_spec` | Per-state 16-byte animation/blend spec (`kind_data[0x0C] + state_id*16`). Not the 24-byte state_table entry. |
| 0x70 | `velocity_scale` | Copy of `desc.velocity_scale`. |
| 0x74 | `cur_scale` | Live scale; read with `+0x78` by prio 7 when refreshing HurtData radius/position. |
| 0x78 | `type_flag` | Copy of `desc.type_flag`. Every vanilla spawner writes 1 (including `spawnBomb` @ `0x801a954c`). The field exists; semantics for values other than 1 are uncharted. |
| 0x88 | `spawn_velocity` | Vec3. Snapshot of `desc.velocity`, read-only after creation. |
| 0x94 | `velocity` | Vec3. Live physics velocity, integrated into `position` each frame by prio 4. |
| 0xac | `position` | Vec3. Live world position. |
| 0xb8 | `position_prev` | Vec3. Previous-frame position; used by swept collision. |
| 0xc4 | `position_init` | Vec3. Spawn position; collision anchor. |
| 0x104 | `render_state` | `HSD_ObjAlloc`'d block (`0x802205b0`) holding the alpha/color/scale ramp fields the FADE state reads (`+0x10` alpha, `+0x14` lifetime, `+0x2c`/`0x30` fade endpoints). Freed at teardown. **Not** `kind_data`. |
| 0x108 | `hurt_data` | HurtData from `Projectile_InitHurtData`; its `+0x0c` points at the region array (0xC8 stride). |
| 0x10c | `lifetime` | Frames remaining; prio 1 decrements and despawns at 0. |
| 0x110 | `frame_counter` | Monotonic counter incremented by prio 0. |
| 0x114 | `effect_handle_a` | Particle-effect handle (Effect module), allocated by `0x802364e0` and passed to `Effect_SpawnSync` as the attach parent. Freed in the dtor. |
| 0x118 | `effect_handle_b` | Second particle-effect handle, freed alongside `0x114`. |
| 0x14c | `charge` | Copy of `desc.charge`. |
| 0x150 | `state_fn0` | Copied by `Projectile_SetState` from entry+0x08. |
| 0x154 | `state_fn1` | ... from entry+0x0c. |
| 0x158 | `state_fn2` | ... from entry+0x10. |
| 0x15c | `state_fn3` | ... from entry+0x14. |
| 0x160 | `user_hook_0` | Per-state callback invoked at prio 0. Zeroed by `Projectile_SetState`; written by per-state setup. |
| 0x164 | `user_hook_1` | Per-state callback invoked at prio 7. |
| 0x168 | `user_hook_2` | Further per-state slot; zeroed on state change. |
| 0x16c | `user_hook_on_hit` | Per-state on-hit callback invoked by prio 10. Return non-zero to request a state transition. |
| 0x1b4 | `flag_a` | Bit 0 = allow-self-hit inbound (default 0). Other bits set during damage logging. |
| 0x1b5 | `flag_b` | Bit 0 = env-colliding this frame (set by the `mpColl` tick). Bit 2 = alive (always on). Bit 4 = allow-self-hit outbound. |
| 0x1b6 | `flag_c` | Effect/anim-state bits. Bit 7 = state-changed-this-frame (set by every transition, e.g. `Bomb_State2End_TransitionToFade`). Lower bits flag effect-handle liveness. |
| 0x218 | `flag_d` | Subproc-gating bits. Bit 0 always set by `Projectile_Create`. |

### Per-kind scratch (0x1c0..0x1f8)

Roughly 0x1c0 through 0x1f8 is per-kind scratch - treat it as opaque unless you are the kind's own
state code. The bomb overloads these offsets across states, and the handles it parks there are
**particle-effect** handles (Effect module: `Effect_SpawnSync` @ `0x80236c40`,
`abilityTimer_Plasma_removeEffect` @ `0x8023624c`), not audio. Mapped BOMB usage:

- **HELD (state 0):** `proj+0x1c0..0x1cc` stays at its memset-0 default. HELD's fn3 snaps
  `proj+0xac` (position) to the rider hand bone and writes orientation vectors at
  `proj+0xd0`/`0xdc`/`0xe8`; it never touches the 0x1c0 band.
- **THROWN (state 1):** still zero - state 1's fn slots integrate physics (`proj+0x94`/`0xac`) and
  scan mpColl collision.
- **EXPLODING (state 2):** `proj+0x1c0` is reused as the **detonation countdown**, seeded by
  `Bomb_DetonationTrigger` (`0x80225c8c`) and decremented by state-2 fn0
  `Bomb_State2_DetonationTimerTick` (`0x80225d48`). The detonation burst effect is spawned here and
  tracked in `proj+0x1f8`/`0x1fc`.
- **EXPLODING -> FADE** (`Bomb_State2End_TransitionToFade` @ `0x80225f90`): removes the EXPLODING
  burst at `proj+0x1f8`/`0x1fc` and zeroes it, then spawns **two** new effects via
  `Effect_SpawnSync` - a one-shot at `proj+0x1c0`/`0x1c4` (overwriting the now-dead countdown) and a
  positional one attached to `proj+0xac` at `proj+0x1c8`/`0x1cc` - and computes the alpha/lifetime
  fade ramp into `proj+0x1d0..0x1ec` from the render-state block (`proj+0x104`) and HurtData regions
  (`proj+0x108`).
- **Teardown** (BOMB `aux_a` @ `0x8022634c`): removes the lingering positional effect at
  `proj+0x1c8`/`0x1cc` and zeroes it.

The complete bomb effect-handle lifecycle: `proj+0x114`/`0x118` (the persistent anchors) are freed by
the dtor; the EXPLODING burst at `proj+0x1f8`/`0x1fc` is removed when FADE begins; the positional
FADE effect at `proj+0x1c8`/`0x1cc` is removed by `aux_a` at teardown; and the one-shot FADE effect
at `proj+0x1c0`/`0x1c4` has no explicit teardown - it self-terminates when its `Effect_SpawnSync`
animation completes. No slot leaks.

## Spawn Helpers

All live in the rider-side ability code. They build a `ProjectileDesc` from the rider/machine context
and call `Projectile_Create`.

| Addr | Name | Kind | Position source | Velocity source | Assert on |
|------|------|------|-----------------|-----------------|-----------|
| `0x801a8c80` | `spawnStarBullet` | 0 or 1 | rider hand bone | rider base + self_vel | `rd->ability_hat_model` |
| `0x801a8df8` | `spawnStarBullet_charged` | 11 | rider hand bone | rider base + self_vel | `rd->ability_hat_model` |
| `0x801a8f68` | `spawnFireBullet` | 2 | caller Vec3 | rider base + angle rotate | - |
| `0x801a9178` | `spawnFireAura` | 3 | aura slot (rider+0x318) | 0 | - |
| `0x801a9410` | `spawnBomb` | 4 | rider hand bone | rider base + self_vel | `rd->ability_hat_model` |
| `0x801a95a0` | `spawnPlasmaBullet` | arg | rider hand bone | rider base + self_vel | `rd->ability_hat_model` |
| `0x801a9870` | `spawnPlasmaSpread` | arg | rider hand bone | rider base + self_vel | `rd->ability_hat_model` |
| `0x801a9a54` | `spawnSpikeAura` | 12 | aura slot (rider+0x318) | 0 | - |
| `0x801a9b84` | `spawnIceAura` | 13 | aura slot (rider+0x318) | 0 | - |
| `0x801a9cb4` | `spawnCrackerBullet` | 14 | caller args | rider base + self_vel | `rd->ability_data` |
| `0x801a9e78` | `spawnSensorBomb` | 15 | rider hand bone | rider base + self_vel | `rd->ability_hat_model` |
| `0x801aa028` | `spawnGordo` | 16 | rider hand bone | rider base + self_vel | `rd->ability_hat_model` |

### Throw / transition wrappers

These are not spawners - they act on an already-created projectile, typically moving it from HELD
(state 0) to THROWN (state 1).

| Outer | Inner | Name | Meaning |
|-------|-------|------|---------|
| `0x801a9580` | `0x80225824` | `Rider_TryThrowBomb(gobj, unused, velVec3)` | Sets position/orientation from the rider hand, writes `proj+0x94..0x9c`, then `Projectile_SetState(proj, 1, ..., flags=1)`. Reads pos/forward/up from `*(proj+0x6c)+8`, a hand-bone matrix that only exists while a rider is actively holding the projectile - **not safe from custom spawn paths** that never routed the projectile through a rider's hand. |
| `0x801a9fe8` | `0x80228f08` | `Rider_TryThrowSensorBomb(gobj, velVec3)` | Guards on the sensor-ready flag at `proj+0x1bc`, then sets velocity and state index 1. The flag is written by sensor bomb's `post_init` (`0x80228d8c`) from `kind_data2[0x04]` at create time, so the guard passes for any vanilla-spawned sensor bomb - but custom paths that bypass `post_init` silently no-op through this wrapper. |
| `0x801aa008` | `0x8022a244` | `Rider_IsGordoThrowable(gobj) -> bool` | **Predicate, not a throw.** Returns true iff `state_id == 3` and bit 4 of `proj+0x1b6` is set. The actual gordo throw is `Gordo_EnterThrownState` (`0x8022a544`). |

### Gordo's throw transition

Unlike bomb/sensor bomb, gordo's HELD -> THROWN transition is not a thin `Projectile_SetState`
wrapper. `Gordo_EnterThrownState(projGObj, velVec3, posVec3)` at `0x8022a544` does a full per-kind
setup that gordo state 1's fn1/fn2 read back every frame:

- `proj+0x1d8 = 2` and `proj+0x1e0..0x1e8 = velocity` - written into the animation object every frame
  by gordo state 1 fn1 (`0x8022a710`) to drive the spinning-gordo model rotation. Zeroes leave the
  model unrotated.
- `proj+0x1dc = randomized angular velocity`, sign coin-flipped via `0x8041e668`. Without it the
  gordo does not spin.
- `proj+0x7c..0x84 = velocity-direction * kind_data[0x20]`, a velocity-derived acceleration
  transformed through `proj+0xac`'s rotation matrix. This is the real impulse; `desc.velocity` alone
  gives forward motion with no acceleration profile.
- `proj+0x10c = proj+0x100` (lifetime). Without it lifetime is zero and gordo state 1 fn2
  short-circuits at its `cmplwi r0,0; beq` test, bailing before its update body.

It also reads the owner's rider fields through `Rider_GetForward` (`0x80191ef8`) and `Rider_GetUp`
(`0x80191f18`) to build the throw-time orientation basis at `proj+0xd0..0xec`, so `owner_gobj` must
be a real rider GObj - `0` is fine for `Rider_TryThrowBomb` but not here.

After the state setup it tail-calls two general projectile helpers:

- `Gordo_EnforceMaxActive` (`0x8022b45c`) - walks the projectile p_link list, counts active gordos in
  non-HELD/non-DESPAWN state, and force-despawns the excess (state-3 transition plus zeroed
  accel/velocity/timers) once the count exceeds `kind_data2[0x1c]`, the per-stage max-gordos cap.
  Filters by `kind == PROJKIND_GORDO`.
- `Projectile_RebuildCollShape` (`0x80221c9c`) - calls `mpColl_Init` against the projectile's
  CollData (`proj+0x138`) using `proj->position`, the hand-bone-derived basis vectors at
  `proj+0xd0`/`0xdc`, and per-kind dimensions from `kind_data[0x14]`. All three vanilla throw
  transitions call it (bomb at `0x80225968`, sensor bomb at `0x80228fa4`, gordo at `0x8022a6c0`);
  it is the post-throw collision-shape refresh, not gordo-specific.

## Custom Spawn Recipe

To spawn a thrown projectile of any kind without a Bomb/Phan-Phan ability being active - bypassing
the rider-hand bone chain - use the flow in `SpawnProjectileForPlayer` in
`mods/custom_events/src/spawn_projectile.c`. The three `SpawnProjectile_*Trap()` entry points
(`_BombTrap`, `_GordoTrap`, `_SensorBombTrap`) are wired to no trap dispatcher yet, so the file is
currently a reference implementation rather than a live code path.

`SpawnProjectileForPlayer(ply_idx, kind, distance)` reads the player's machine via
`Ply_GetMachineGObj(ply_idx)` (`md = mg->userdata`) and takes `desc.position`/`forward`/`up`/
`velocity` from `md->pos` (0x3e8), `md->forward` (0x418), `md->up` (0x424), and `md->velocity`
(0x324) - not from a `RiderData`. The owner id comes from the rider GObj (`Ply_GetRiderGObj`) only
when one is present (`owner = *(int *)rg->userdata`, i.e. `rd->x0`) and defaults to 0 otherwise.

```c
ProjectileDesc desc = {0};
desc.kind = PROJKIND_BOMB;       // or SENSORBOMB / any kind whose state 1 is "flying"
desc.owner_unk1 = owner;         // rd->x0 if a rider GObj exists, else 0; required
desc.owner_unk2 = owner;         // for kinds whose state code reads owner_gobj (gordo)
desc.position   = md_pos + md_forward * distance;   // somewhere out front

desc.forward    = md->forward;
desc.up         = md->up;
desc.velocity_scale = 1.0f;
// Inherit carrier motion AND add a forward throw impulse. md->velocity alone
// leaves the projectile co-moving with the machine, which looks glued to Kirby
// and keeps it inside Kirby's geometry for the whole fall arc until env-coll
// fires. A constant-magnitude forward kick keeps the trajectory predictable
// across machine speeds.
desc.velocity.X = md->velocity.X + md->forward.X * THROW_SPEED;
desc.velocity.Y = md->velocity.Y + md->forward.Y * THROW_SPEED;
desc.velocity.Z = md->velocity.Z + md->forward.Z * THROW_SPEED;
desc.type_flag  = 1;
desc.charge     = 1.0f;

void *handle = Projectile_Create(&desc);
if (!handle) return;

// After postInit, PROJKIND_BOMB / SENSORBOMB sit in state 0 (HELD), pinned to a
// nonexistent rider hand and never detonating. Advance to state 1 manually,
// mirroring vanilla throwBomb.
ProjectileData *proj = Projectile_GetData(handle);   // *(handle + 0x2c)
if (!proj) return;

// The trapped player IS the owner, so vanilla owner-exclusion would drop the
// explosion on them unless both scan paths opt in.
proj->flag_a |= PROJ_ALLOW_SELF_HIT_INBOUND;   // proj+0x1b4 bit 0
proj->flag_b |= PROJ_ALLOW_SELF_HIT_OUTBOUND;  // proj+0x1b5 bit 4

// Per-frame physics reads proj+0x94..0x9c, which Projectile_Create leaves at
// zero (desc.velocity went to the spawn snapshot at proj+0x88). Seed it before
// the state transition, same order as vanilla.
proj->velocity = desc.velocity;

Projectile_SetState(proj, /*BOMB_STATE_THROWN / SENSOR_BOMB_STATE_ARMED_FLYING=*/1,
                    1.0f, 1.0f, /*flags=*/1);
```

`flags=1` matches vanilla throw: skip the rider-attached cleanup path that post-init ran for state 0.
Without the self-hit flags the trap can silently miss the player who owns it.

### Kinds requiring per-kind throw setup

The bare recipe works for any kind whose state-1 callbacks only read fields `Projectile_Create`
already populated - bomb, sensor bomb, plasma, sword star. For **gordo** it does not: gordo state 1
fn1/fn2 read per-kind scratch at `proj+0x1d8`/`0x1dc`/`0x1e0..0x1e8` (rotation cache), `proj+0x7c..0x84`
(acceleration impulse derived from `kind_data`), and `proj+0x10c` (lifetime) that a plain
`Projectile_SetState(proj, 1, ...)` leaves at zero. The symptoms are: no spin, no real impulse, zero
lifetime so state 1 fn2 short-circuits before its update body, and a model that can render degenerate
and look invisible.

Use `Gordo_EnterThrownState` (`0x8022a544`) for gordo instead. It does the full per-kind setup *and*
the post-throw helpers (`Gordo_EnforceMaxActive`, `Projectile_RebuildCollShape`), and needs
`proj->owner_gobj` to be a real rider GObj for the orientation basis.

For single-state kinds (Sword Star A/B/Charged, Plasma Spread) the projectile is already in its one
flying state after `Projectile_Create` - no extra call is needed.

If another kind's state-1 fn slots reference `proj+0x1c0`-band scratch that nothing else writes,
expect to need a similar dedicated "enter-thrown" routine. The bare recipe is the floor, not the
ceiling.

## Hooks

| Need | Hook at | Mechanism | Notes |
|------|---------|-----------|-------|
| On-spawn, any kind | `Projectile_Create` (`0x8021f428`) | `HOOKCREATE` | `r3 = desc` at entry; wrap to read `desc->kind`. |
| On-despawn, any path | `Projectile_UserDataDtor` (`0x8021ff54`) | `HOOKCREATE` | Catches both lifetime expiry and fell-into-void. |
| On-despawn, lifetime only | `Projectile_Despawn` (`0x80220364`) | `HOOKCREATE` | `r3 = proj`. |
| On-despawn, aura only | `Projectile_DespawnGObj` (`0x802230a0`) | `HOOKCREATE` | `r3 = projGObj`. Vanilla calls it only from the Fire/Spike/Ice `*_LoseAbility_Exit` handlers, so it intercepts aura teardown without touching lifetime expiry or void destruction. |
| On-hit, projectile side | `Projectile_Proc9_HitColl` (`0x8021fc74`) | `HOOKCREATE` | The whole collision-scan proc. |
| On-hit logging | `HitColl_SetDamageLog` (`0x8018cf94`) | `HOOKCREATE` | Shared with all damage sources - filter by attacker. |
| On-state-change | `Projectile_SetState` (`0x8021f7dc`) | `HOOKCREATE` | `r3 = proj`, `r4 = state_index`. |
| Override spawn damage | `Projectile_InitHurtData` (`0x80221440`) exit | `HOOKCREATE` | Patch region fields on `proj+0x108`. |

The outer GObj is always `r3` at `Projectile_Create`'s return; the inner `ProjectileData` is always
`*(gobj+0x2c)`. Use `Projectile_GetOwnerGObj` (`0x8022312c`) when you only have a proj and need the
owner.

## Known Limitations

**Contents of `kind_data+0x14..0x30`.** The structural fields of the per-kind data table are mapped
(`ProjKindData` in `projectile.h`): `+0x00 state_table`, `+0x04 NULL`, `+0x08 model descriptor`
(word0 feeds `HSD_JObjLoadJoint`; NULL falls back to a global default model), `+0x0c
state_anim_spec_array`, `+0x10 hurt_region_spec`. Whether anything lives at `+0x14..0x30` is unknown.
No bomb state function dereferences `kind_data` at all - its damage comes from the HurtData built at
create (from `+0x10`) and its fade params from the per-projectile `proj+0x104` block - so
`+0x14..0x30` may simply be unused by the kinds covered so far. Pinning it down needs a mid-stage
runtime dump, since the per-stage loader writes the `0x8055a9a8` table from data rather than code and
the values only exist while a stage is loaded.
