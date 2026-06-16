# Projectile System

The **projectile** is Kirby Air Ride's actor type for transient, physics-driven
objects: thrown bombs, fired bullets, hovering auras, Gordo, Phan-Phan throws,
firecrackers. All live on a single GObj/proc class (entity class 23, p_link 14)
with a shared update pipeline and a per-kind vtable that specialises behaviour
for each of the 17 `ProjectileKind` values.

Sections are tagged with confidence:

- **Solid** — high confidence.
- **Plausible** — inferred from strong contextual evidence.
- **Speculative** — best-guess; call out explicitly before relying on it.

## 1. High-level shape

A projectile is a GObj with two pieces of user data:

- The **outer handle** returned by `Projectile_Create`. Internally this is a
  small tracking block allocated by the GObj system; its only publicly-useful
  property is that `*(handle + 0x2c)` points to the inner data struct.
- The **inner `ProjectileData`** — a 0x220-byte (544-byte) block allocated from
  the HSD object pool at `0x8055a8f8`. Zero-initialised by `Projectile_Create`
  via `memset(proj, 0, 544)` at `0x8021f4a4`. This is where all per-projectile
  state lives: kind, current state, position, velocity, HurtData,
  particle-effect handles, and per-state callback pointers.

Each projectile is registered as a GObj with:

- Entity class 23 (compare: rider=14, machine=15, enemy=21).
- p_link 14 (`GAMEPLINK_PROJECTILE`) — the global list scanned by rider,
  machine, item, and box hit checks.

## 2. Per-kind vtable (at `0x804b4338`)

The 17 `ProjectileKind` values index a pointer array at `0x804b4338`. Each entry
points to an 8-word (0x20-byte) **per-kind vtable**:

| Offset | Field             | Called from                     | Purpose                                                     |
|-------:|-------------------|---------------------------------|-------------------------------------------------------------|
| +0x00  | `state_table`     | `Projectile_SetState` @ 0x8021f888–8a0 | Array of 24-byte state entries. See §3.            |
| +0x04  | reserved          | —                               | Always 0 in all 17 kinds.                                   |
| +0x08  | `init(proj)`      | `Projectile_Create` @ 0x8021f660      | One-shot, early. Clears/inits per-kind scratch fields.      |
| +0x0c  | `refreshXfmA(proj)` | per-frame (see §4)            | Copies the model's JObj matrix into `proj` scratch.         |
| +0x10  | `refreshXfmB(proj)` | per-frame (see §4)            | Byte-identical to `refreshXfmA` in every kind sampled.      |
| +0x14  | `auxA(proj)`      | state-exit / despawn            | State-exit cleanup. E.g. bomb's `Bomb_AuxA_RemoveEffect` (`0x8022634c`) removes its lingering FADE particle effect. Can be NULL. |
| +0x18  | `postInit(proj)`  | `Projectile_Create` @ 0x8021f7a0      | One-shot, late. Typical body: `Projectile_SetState(this, 0, …)` + spawn the spawn-time particle effect (`Effect_SpawnSync`). |
| +0x1c  | `auxB(proj)`      | state dispatcher (per-frame)    | Kind-specific rendering/cleanup hook. Can be NULL.          |

Two kind pairs share a vtable — the aliases are at the pointer level:

- `PROJKIND_PLASMA_A` (5) and `PROJKIND_PLASMA_B` (6) both use `0x804b47e0`.
- `PROJKIND_PLASMA_SPREAD_MID` (7) and `_SIDE` (8) both use `0x804b4848`.

The difference between each pair comes from the **per-kind data** at
`0x8055a9a8[kind]` (model/art/stats), not from the code.

## 3. State tables and state entries

Each kind's state table is an array of **24-byte entries**:

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

`Projectile_SetState(proj, index, fA, fB, flags)`:

1. Selects `state_table[index]` from `proj+0x30` when `index < proj+0x28`,
   else from `proj+0x34` with offset `(index - proj+0x28) * 24`. **In vanilla
   `proj+0x28` is hardcoded to 0 by `Projectile_Create` (@ 0x8021f508) and
   never rewritten, so `index < 0` is never true and the `proj+0x30` branch
   is dead.** Every vanilla dispatch uses `proj+0x34`, which `Projectile_Create`
   (@ 0x8021f51c) loads with the per-kind `state_table` (vtable[0]).
2. Writes `proj+0x24 = index`, `proj+0x2c = entry.state_id`, copies
   `entry.fn0..fn3` into `proj+0x150..0x15c`, and writes
   `proj+0x38 = kind_data[0x0C] + state_id*16` (per-state animation/blend
   spec, 16 bytes per entry — NOT the 24-byte state_table entry).
3. Runs the animation transition (per-kind state-enter setup, via
   `Projectile_AssignStateFlags`).

All four `fn0..fn3` slots are **per-frame** callbacks, not on-enter/on-exit
hooks. State transitions are driven by `Projectile_SetState` calls from inside
`fn0/fn1/fn2` themselves. If you need an on-enter hook, use the per-kind
vtable's `init` (one-shot at create) or `postInit` (one-shot at create) —
there's no entry-level on-enter slot.

`Projectile_SetState` (0x8021f7dc) stores `fn0..fn3` at
`proj+0x150..0x15c`; the ten GObj procs registered by `Projectile_Create`
(at priorities 0, 1, 4, 5, 6, 7, 8, 9, 10, 21) dispatch into those slots each
frame — specifically prio 1 calls `proj+0x150`, prio 4 calls `proj+0x154`,
prio 5 calls `proj+0x158`, prio 6 calls `proj+0x15c`.

### 3.1 Per-kind state tables

Summary of all 17 kinds. Entries are indexed 0..N-1; `state_id` is the value
that ends up in `proj+0x2c`; **use the index** as the argument to
`Projectile_SetState` — `state_id` can duplicate across entries.

| Kind | Table | Entries | State IDs                 |
|-----:|-------|:-------:|---------------------------|
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

### 3.2 State semantics (Solid for thrown/held; Plausible for auras)

These are the values to pass to `Projectile_SetState`.

**`PROJKIND_BOMB`** (4 states)
- 0 HELD — fn3 snaps to rider hand bone (`bl 0x80191ffc`), all other fn slots blr.
- 1 THROWN — fn1 integrates velocity into position, fn2 runs `mpColl` hit check
  (`0x80221fd4`) and branches to detonate on env-coll.
- 2 EXPLODING — fn0 decrements a detonation timer (`proj+0x1c0`), transitions
  to state 3 when it hits zero.
- 3 FADE — fn0 decrements `proj+0x1dc` (lifetime) and `proj+0x1d8` (alpha);
  triggers `GObj_Destroy` when lifetime expires.

**`PROJKIND_SENSORBOMB`** (5 entries; state_id 2 appears twice — pass the **index**)
- 0 HELD
- 1 ARMED_FLYING — physics + proximity scan
- 2 ARMED_STATIONARY — landed, waiting; short-range sensor + timer
- 3 EXPLODING
- 4 FADE (state_id 2 again — distinct callbacks from index 2)

**`PROJKIND_GORDO`** (4 entries; state_id 1 appears twice; final is sentinel -1)
- 0 HELD
- 1 THROWN_ASCENDING — scales up to full size while flying.
- 2 THROWN_TIMED — scale locked, self-despawn timer; index-1 transitions here
  internally when its scale check completes.
- 3 DESPAWN — state_id sentinel -1. `Projectile_SetState` zeros `proj+0x38`
  here but still installs `fn0..fn3`, so the sentinel's fn0 (`0x8022b09c`)
  runs for one frame to spawn its despawn particle burst before the proc is
  torn down. This is the only vanilla use of the sentinel pattern inside a
  live state transition.

**`PROJKIND_FIRE_BULLET`** (3 entries; final is sentinel -1)
- 0 THROWN — flying, damaging.
- 1 HIT_PAUSE — all-blr stub held for animation time. Probably the hit-flash
  freeze frame.
- 2 DESPAWN — sentinel; fn0 = rider-alive watchdog / cleanup.

**`PROJKIND_FIRE_AURA` / `_SPIKE_AURA` / `_ICE_AURA`** (3 entries each)
- 0 IDLE — fn3 re-snaps to rider hand bone every frame. All other fn slots blr.
- 1 FIRING / EMITTING / DEPLOYED — per-kind "active" state. Only ICE has a
  real per-frame fn0 (hurtbox tick); SPIKE differs from IDLE only in
  animation flags.
- 2 COOLING / SETTLED — structurally identical to IDLE with a different
  animation class. **Plausible** interpretation; could equally be "retract"
  or "idle2".

**`PROJKIND_PLASMA_A/B`**, **`_PLASMA_C`**, **`_FIRECRACKER`** (2 entries each)
- 0 FLYING / DAMAGING — physics + collision.
- 1 ABSORBED — absorbed into rider/charger; fn0 is a rider-alive watchdog that
  reads `proj+0x1b8/0x1bc`. Seen across plasma C, firecracker, plasma A/B;
  Solid interpretation.

**`PROJKIND_PLASMA_D`** (2 entries, both active)
- 0 FLYING — timed, with a per-frame trail emitter in fn3.
- 1 TRAILING — timed tail fragment. **Plausible**; could be split-phase.

**`PROJKIND_SWORD_STAR_*` / `_PLASMA_SPREAD_*`** (1 entry each)
- 0 THROWN — fires once, dies on impact. No held or explode phases.

### 3.3 flags field meanings (Solid upper byte; Plausible low byte)

The `flags` word at entry+0x04 is routed through `Projectile_AssignStateFlags`
at **`0x80222298`** (size 0x74; called from `Projectile_SetState` at
`0x8021f92c`). Its body, in order:

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

So the flag word is stored verbatim at `proj+0x17c` (and its low byte is what
`proj+0x17f` reads back next transition). The **low byte** does *not* index a
lookup table: `0x80231b68` (`AllocSeqId16`) takes **no arguments** — it reads,
increments, and writes back a global `u16` counter at `0x805DD8A0`
(`r13+0x7c0`), returning the pre-increment value and resetting to 1 on
wrap-to-0. What the low byte actually gates is *whether a fresh id is minted*:
`proj+0x194` gets a new sequence id whenever the low byte is 0 **or** differs
from the previous transition's low byte; if the anim-class is unchanged and
nonzero, the old id is kept. `proj+0x194` is therefore an
**animation-instance/generation counter** that bumps on every anim-class
change — not an id "resolved from" the low byte. `proj+0x184` and parts of
`proj+0x18a/0x18b` are zeroed each transition. The full set of flag
values across the 17 kinds' state tables:

| value   | used by (kind / state)                                           |
|---------|------------------------------------------------------------------|
| 0x0000  | held states, aura idle, FIRE_BULLET sentinel, SWORD_STAR_CHARGED |
| 0x0101  | FIRE_AURA idle                                                   |
| 0x0102  | FIRE_BULLET thrown + hit-pause                                   |
| 0x0108  | BOMB (all states)                                                |
| 0x0109  | PLASMA A/B/C/D flying, PLASMA_SPREAD                             |
| 0x010a  | SPIKE_AURA idle                                                  |
| 0x010b  | SPIKE_AURA deployed                                              |
| 0x010d  | ICE_AURA emitting                                                |
| 0x010f  | SWORD_STAR_A                                                     |
| 0x0111  | FIRECRACKER                                                      |
| 0x0112  | SENSORBOMB held..exploding                                       |
| 0x0113  | GORDO thrown states                                              |
| 0x0115  | SWORD_STAR_B                                                     |

Upper byte is either 0 or 1 — a boolean "animated state". Lower byte groups
states by kind (one unique value per kind for active states). None of the
state tables use more than two distinct flag values (an "animated" one and
0x0000 for held/sentinel), so the lower byte is effectively a per-kind
animation-class tag, not a general-purpose bit field: in
`Projectile_AssignStateFlags` the low byte is compared as a whole value
against the previous one, not tested bit-by-bit.

`proj+0x17c` keeps the full 32-bit flag word; `proj+0x194` holds a fresh
animation-instance id minted (from the global counter, not from the byte's
value) whenever the anim class changes; `proj+0x184 / 0x18a / 0x18b` are zeroed
every transition. These are the inputs the per-kind `refreshXfm*` callbacks
read each frame to drive the HSD animation object.

## 4. Per-frame update (the 10 procs)

`Projectile_Create`'s epilogue registers ten GObj procs on the projectile GObj
via `GObj_AddProc` (`0x804288a4`). Each priority corresponds to one step of a
fixed per-frame pipeline (priorities are the same ranks used elsewhere — see
`docs/enemy-spawn-system.md`):

| Prio | Addr          | Name                              | What it does                                                                 |
|-----:|---------------|-----------------------------------|------------------------------------------------------------------------------|
| 0  | `0x8021f9b4` | `Projectile_Proc0_FrameStart`       | `proj+0x110++` (frame counter), zero accel, `HurtData_ResetFrame`, tick `proj+0x134` intang timer, call `proj+0x160` user hook. |
| 1  | `0x8021fa18` | `Projectile_Proc1_RunStateFn0`      | `HurtData_UpdatePerFrame`, call `proj->state_fn0` at `proj+0x150`, then tick `proj+0x10c` lifetime → despawn on zero. |
| 4  | `0x8021faa4` | `Projectile_Proc4_Physics`          | Call `state_fn1` at `proj+0x154`; integrate `vel += accel`, `pos += vel`, `pos_prev += vel`. |
| 5  | `0x8021fb44` | `Projectile_Proc5_RunStateFn2`      | Clear env-coll flag, call `state_fn2` at `proj+0x158`.                       |
| 6  | `0x8021fb88` | `Projectile_Proc6_RunStateFn3`      | Call `state_fn3` at `proj+0x15c`, then `0x80220310` (mpColl pos sync), then a per-kind sub-cleanup. |
| 7  | `0x8021fbec` | `Projectile_Proc7_PostState`        | Call `proj+0x164` user hook. If `pos.y < floor_threshold`: `GObj_Destroy`. Else update HurtData radius/position from `proj+0x74/0x78`. |
| 8  | `0x8021fc70` | `Projectile_Proc8_Stub`             | Single `blr`. Reserved priority.                                              |
| 9  | `0x8021fc74` | `Projectile_Proc9_HitColl`          | The hit-detection proc. Scans rider / machine / projectile / item / stage-hazard lists. |
| 10 | `0x8021fcd4` | `Projectile_Proc10_HitReact`        | Resolve strongest logged hit, run per-kind on-hit callback; state-transition if it returns non-zero. |
| 21 | `0x8021fed4` | `Projectile_Proc21_EndOfFrame`      | Compute `vel_diff = pos - pos_prev`, save `pos → pos_prev`, finalise HurtColl attach.  |

All ten are attached at create time. Most per-kind differences are expressed
through the fn0..fn3 state entries, not through new procs.

## 5. Hit detection and damage

### 5.1 Outbound scans (prio 9)

`Projectile_Proc9_HitColl` at `0x8021fc74` iterates five p_link lists:

- `Projectile_CheckRiderCollision`     `0x802215a4`
- `Projectile_CheckMachineCollision`   `0x80221660`
- `Projectile_CheckProjectileCollision` `0x8022171c` (skips self)
- `Projectile_CheckItemCollision`      `0x80221814`
- `Projectile_CheckStageHazardColl`    `0x80221878`

Each follows the same skeleton: walk the target list, filter by pause/disabled
gating, call `HitColl_CheckCollision(proj_hurt_data, victim_hurt_data)` at
`0x8018d284`. The HurtData/HitColl pipeline is documented in
`docs/hurtdata-system.md`.

The rider-side outbound scan additionally calls `HitColl_CheckIfSamePlayer`
(`0x8000b024`, `r3 = owner_gobj, r4 = victim_gobj`) to implement
owner-exclusion. If same-player, the scan skips unless **`proj+0x1b5` bit 4**
is set ("outbound self-hit allow"). Note this is a *different* flag from the
inbound side's allow bit — see §5.4.

### 5.2 Inbound scans

Victims also scan *us*. The projectile global list is read from:

- `Rider_CheckProjectileHit`          `0x801963c8`
- `Machine_CheckProjectileCollision`  `0x801d7118`
- `Box_CheckProjectileCollision`      `0x80252334`

Each of these reads `Projectile_GetOwnerGObj(proj) = *(projGObj+0x2c+0x08)`
(3-insn accessor at `0x8022312c`) to implement owner-exclusion:

> If `proj->owner_gobj == victim_gobj`, skip the hit — unless bit 0 of
> `proj+0x1b4` is set (the inbound "allow self-hit" flag). Default is off.

### 5.3 Damage values

Per-region params (damage, knockback, radius) are stored in the projectile's
HurtData, populated at init time from `kind_data+0x10` (the `hurt_region_spec`,
whose `+0x00` is the region-descriptor array base and `+0x04` the region count;
regions are built by `HurtData_Create` @ `0x8018c1c8` + `HurtData_InitRegion`
@ `0x8018c598`, 0x18-byte stride). Explosion-class projectiles cache a handful
of scalars at `proj+0x1d0..0x1ec` when entering the EXPLODING→FADE transition,
but these come from **per-projectile** blocks, not from `kind_data`:
`proj+0x1e4/0x1e0` are copied from HurtData regions 0/1 (via
`*(proj+0x108)+0x0c`, field `+0x18`), and the fade/alpha ramp
(`proj+0x1d8` alpha, `proj+0x1dc` lifetime, `proj+0x1e8/0x1ec` rate) is read
from the render-state block at `proj+0x104`. No bomb state function
dereferences `kind_data` at all.

If you want to override projectile damage:

- **Per-spawn override**: hook the exit of `Projectile_InitHurtData`
  (`0x80221440`), then walk the newly-created HurtData's regions
  (stride 0xC8) and rewrite `base_damage` at `+0x04` / `base_knockback`
  at `+0x24`.
- **Per-hit override**: hook `HitColl_SetDamageLog` (`0x8018cf94`) and
  discriminate by the attacker's HurtData kind.

### 5.4 Self-hit allow flags (two of them)

Owner-exclusion is gated by *separate* flag bits on the inbound and outbound
scan paths, and the two flags live on different bytes of `ProjectileData`:

| Scan side | Flag bit | Used by | Set by vanilla? |
|-----------|----------|---------|-----------------|
| Inbound (rider/machine/box looks at projectile list) | `proj+0x1b4` bit 0 | Rider_CheckProjectileHit / Machine_CheckProjectileCollision / Box_CheckProjectileCollision | Sensor bomb `post_init` (`0x80228d8c`) sets this. Bomb / gordo do not. |
| Outbound (`Projectile_CheckRiderCollision` walking the rider list) | `proj+0x1b5` bit 4 | `0x802215a4` | Never set at create time on bomb / sensor bomb / gordo — vanilla throws target *other* players, so the default exclusion is what they want. |

**A projectile that needs to damage its own owner-player must set both**.
Custom-spawned trap projectiles where `owner_gobj` is the trapped player are
the canonical case for this — set them right after `Projectile_Create`:

```c
ProjectileData *proj = Projectile_GetData(handle);
proj->flag_a |= PROJ_ALLOW_SELF_HIT_INBOUND;   // proj+0x1b4 bit 0
proj->flag_b |= PROJ_ALLOW_SELF_HIT_OUTBOUND;  // proj+0x1b5 bit 4
```

Setting only one risks the hit being silently dropped depending on which
scan path resolves it first. Setting both is safe because the flag *only*
gates same-player exclusion; non-owner targets are unaffected.

## 6. Lifecycle

### 6.1 Creation

`Projectile_Create` (0x8021f428):

1. `GObj_Create(kind=23, pLink=14, prio=0)` → outer gobj.
2. Register render at `0x80220000` via `GObj_AddGXLink`.
3. `HSD_ObjAlloc` the 0x220-byte `ProjectileData` from pool `0x8055a8f8`.
4. `GObj_AddUserData(gobj, 23, dtor=0x8021ff54, ud=proj)` — the destructor at
   `0x8021ff54` is what frees everything at teardown.
5. `memset(proj, 0, 544)`.
6. Copy descriptor fields (kind, owner, position, forward/up, velocity,
   type_flag, charge) into proj. Set flag bits at `proj+0x1b5` bit 2 and
   `proj+0x218` bit 0 (always-on "alive" markers).
7. Build orientation matrix via `0x80220250` (PSMTXNormalize/Cross).
8. Allocate sub-resources: scratch mtx, sub-vtable table (`proj+0x6c`),
   render-state block (`proj+0x104`, `HSD_ObjAlloc` from its own pool — holds
   the alpha/color/scale ramp fields), two particle-effect handles
   (`proj+0x114/0x118`), text/vfx slot (`proj+0x10`), `mpColl` CollData
   (`proj+0x138`, if kind wants one), anim object (`proj+0x140/148`), HurtData
   via `Projectile_InitHurtData` (`0x80221440`). The model joint itself is
   loaded by `HSD_JObjLoadJoint` (`0x8040afe8`) from `kind_data+0x08` (or a
   global default when that pointer is NULL).
9. Call per-kind **init** (vtable +0x08).
10. Register the ten GObj procs (priorities 0, 1, 4, 5, 6, 7, 8, 9, 10, 21).
11. Run the reset chain (`0x8021f2a0`, `0x80220310`, `0x80220654`,
    `0x80220230`, `0x80221c9c`, `0x80220578`, `0x80221534`, `0x80221300`,
    `0x80222240`) to zero accel/velocity and enter state 0.
12. Call per-kind **postInit** (vtable +0x18). For throwable kinds this spawns
    the "spawn" particle effect via `Effect_SpawnSync` (`0x80236c40`).
13. Return the outer gobj.

The inner `ProjectileData` pointer is reached via `*(gobj + 0x2c)`.

### 6.2 Destruction

Two vanilla paths:

1. **Lifetime expiry** via `Projectile_Despawn` (`0x80220364`), triggered from
   prio-1 when `proj+0x10c` ticks to 0. Runs per-kind `auxA` first, then
   `GObj_Destroy(gobj)`.
2. **Fell into void** via prio-7 when `proj->pos.y` drops below a threshold
   loaded from `r2-16104`. Calls `GObj_Destroy` directly.

Both paths unwind through the GObj destructor, which invokes the userdata
dtor at `0x8021ff54`. That dtor:

- Destroys HurtData.
- Stops + frees the two particle-effect handles (`proj+0x114/0x118`, via the
  Effect-module helpers `0x8023641c` + `0x80236778`).
- Runs the per-kind `aux_a` (vtable +0x14) — for BOMB this reaps the lingering
  FADE-state effect at `proj+0x1c8/0x1cc` (see §7).
- Destroys text/vfx, anim obj (`proj+0x140/0x148`), render-state block
  (`proj+0x104`), mpColl CollData.
- Returns the `ProjectileData` block to its HSD pool.

### 6.3 Auras and rider backref

Aura kinds (`PROJKIND_FIRE_AURA`, `SPIKE_AURA`, `ICE_AURA`) are spawned with
zero velocity. Each copy ability's `AbilityInit` handler stores the returned
GObj handle at **`rider+0x3F0`** (1008 bytes in). The sites:

| Ability | Init (stores handle)          | Lose (reads & destroys)            |
|---------|-------------------------------|------------------------------------|
| Fire    | `Fire_AbilityInit`  `0x801aed50` (calls `spawnFireAura`  then `stw r3, 0x3F0(rider)`) | `Fire_LoseAbility_Exit`  `0x801af330` |
| Spike   | `Spike_AbilityInit` `0x801b385c` (calls `spawnSpikeAura` then `stw r3, 0x3F0(rider)`) | `Spike_LoseAbility_Exit` `0x801b3d18` |
| Ice     | `Ice_AbilityInit`   `0x801b4718` (calls `spawnIceAura`   then `stw r3, 0x3F0(rider)`) | `Ice_LoseAbility_Exit`   `0x801b49d4` |

Each `*_LoseAbility_Exit` does the same three things:

1. Load `rider+0x3F0`; if NULL, skip destroy.
2. Test a rider flag byte (Fire reads bit 4 of `rider+0x824`). If the bit is
   set, call `GObj_Destroy` directly (hard teardown, skips `aux_a`); otherwise
   call `Projectile_DespawnGObj` (`0x802230a0`) which runs the per-kind
   `aux_a` cleanup before `GObj_Destroy`. The gating bit is likely
   "destroy-without-particle-cleanup", used when the rider is being wholesale
   reset (respawn, round end).
3. Zero `rider+0x3F0`.

`Projectile_DespawnGObj` itself is just `proj = *(gobj+0x2c); Projectile_Despawn(proj); return;`.

Only these three auras use the `rider+0x3F0` slot. Throwable kinds
(`BOMB`, `SENSORBOMB`, `GORDO`) do not — their `AbilityInit` flow keeps the
projectile handle inside the ability's own state block, not in a rider-level
slot.

## 7. `ProjectileData` known-offset reference

**Solid** unless tagged otherwise. Offsets not listed are either zeroed and
unused or kind-specific scratch. See `externals/hoshi/include/projectile.h`
for the struct declaration.

| Offset | Name                 | Notes                                                |
|-------:|----------------------|------------------------------------------------------|
| 0x00   | `gobj`               | Back-pointer to the outer GObj.                      |
| 0x04   | `kind`               | `ProjectileKind`.                                    |
| 0x08   | `owner_gobj`         | Owner rider/machine GObj. **Self-hit exclusion key.** |
| 0x0c   | `owner_unk2`         | Duplicate of owner for hit attribution.              |
| 0x14   | `owner_byte`         | Usually 0.                                           |
| 0x20   | `kind_data`          | `0x8055a9a8[kind]` entry; per-kind data pointer.     |
| 0x24   | `state_index`        | Current entry index (arg of `Projectile_SetState`).  |
| 0x28   | `state_table_split` | Index cutoff between `proj+0x30` and `proj+0x34`. Hardcoded to 0 by `Projectile_Create`, never updated — so `proj+0x30` is dead code in vanilla. |
| 0x2c   | `state_id`           | `state_table[state_index].state_id`.                 |
| 0x30   | `state_table_ext`    | Extension state table; stays at memset-0 default in vanilla, never read. Reserved for future/mod-provided extension sets. |
| 0x34   | `state_table`        | Primary state table (loaded by `Projectile_Create` from vtable[0]). Every vanilla `SetState` dispatch reads from here. |
| 0x38   | `state_anim_spec`    | Per-state 16-byte animation/blend spec (`kind_data[0x0C] + state_id*16`). Not the 24-byte state_table entry. |
| 0x70   | `velocity_scale`     | Copy of `desc.velocity_scale`.                       |
| 0x78   | `type_flag`          | Copy of `desc.type_flag`. Every vanilla spawner writes 1 (including `spawnBomb` @ 0x801a954c — BOMB is thrown but writes 1). Field exists; semantics for non-1 values are uncharted. |
| 0x88   | `spawn_velocity`     | Vec3. Snapshot of `desc.velocity`. Read-only after creation. |
| 0x94   | `velocity`           | Vec3. Live physics velocity. Integrated into `position` each frame by prio 4. |
| 0xac   | `position`           | Vec3. Live world position.                           |
| 0xb8   | `position_prev`      | Vec3. Previous-frame position; used by swept collision. |
| 0xc4   | `position_init`      | Vec3. Spawn position; used as collision anchor.      |
| 0x10c  | `lifetime`           | Frames remaining; prio-1 decrements, triggers despawn at 0. (**Plausible.**) |
| 0x110  | `frame_counter`      | Monotonic counter incremented by prio-0.             |
| 0x104  | `render_state`       | `HSD_ObjAlloc`'d block (`0x802205b0`) holding the alpha/color/scale ramp fields read by the FADE state (`+0x10` alpha, `+0x14` lifetime, `+0x2c/0x30` fade endpoints). Freed at teardown. **Not** `kind_data`. |
| 0x108  | `hurt_data`          | HurtData created by `Projectile_InitHurtData`; `+0x0c` points at its region array (0xC8 stride). |
| 0x114  | `effect_handle_a`    | Particle-effect handle (Effect module). Allocated by `0x802364e0`; passed to `Effect_SpawnSync` as the attach parent. Freed in the dtor. |
| 0x118  | `effect_handle_b`    | Second particle-effect handle. Freed alongside `0x114`. |
| 0x14c  | `charge`             | Copy of `desc.charge`.                               |
| 0x150  | `state_fn0`          | Copied by `Projectile_SetState` from entry+0x08.     |
| 0x154  | `state_fn1`          | … from entry+0x0c.                                   |
| 0x158  | `state_fn2`          | … from entry+0x10.                                   |
| 0x15c  | `state_fn3`          | … from entry+0x14.                                   |
| 0x160  | `user_hook_0`        | Per-state callback slot, invoked at prio 0. Zeroed by `Projectile_SetState`; written by per-state setup. |
| 0x164  | `user_hook_1`        | Per-state callback slot, invoked at prio 7.          |
| 0x168  | `user_hook_2`        | Further per-state slots; zeroed on state change.     |
| 0x16c  | `user_hook_on_hit`   | Per-state on-hit callback, invoked by prio 10. Return non-zero to request state transition. |
| 0x1b4  | `flag_a`             | Bit 0 = allow-self-hit (default 0). Other bits set during damage logging. |
| 0x1b5  | `flag_b`             | Bit 0 = env-colliding this frame (set by `mpColl` tick). Bit 2 = alive (always-on). |
| 0x1b6  | `flag_c`             | Effect/anim-state bits. Bit 7 = state-changed-this-frame (set by every state transition, e.g. `Bomb_State2End_TransitionToFade`). Lower bits flag effect-handle liveness. |
| 0x218  | `flag_d`             | Subproc-gating bits. Bit 0 always set by `Projectile_Create`. |

The span from roughly 0x1c0 through 0x1f8 is **per-kind scratch**. Treat as
opaque unless you're the kind's own state code. The bomb overloads these
offsets across states, and the handles it stores there are **particle-effect
handles** (Effect module, `Effect_SpawnSync` @ `0x80236c40` /
`abilityTimer_Plasma_removeEffect` @ `0x8023624c`), not audio. Mapped BOMB
usage:

- **HELD (state 0):** The `proj+0x1c0..0x1cc` region stays at its memset-0
  default. HELD's fn3 snaps `proj+0xac` (position) to the rider hand bone
  and writes orientation vectors at `proj+0xd0`/`0xdc`/`0xe8` — it never
  touches the 0x1c0 band.
- **THROWN (state 1):** Still zero — the state 1 fn slots integrate physics
  (`proj+0x94`/`0xac`) and scan mpColl collision; no 0x1c0-band writes.
- **EXPLODING (state 2):** `proj+0x1c0` is reused as the **detonation
  countdown** here (seeded by `Bomb_DetonationTrigger` @ `0x80225c8c`,
  decremented by state-2 fn0 `Bomb_State2_DetonationTimerTick` @ `0x80225d48`).
  The detonation burst effect is spawned at this point and tracked in
  `proj+0x1f8/0x1fc`.
- **EXPLODING→FADE (`Bomb_State2End_TransitionToFade` @ `0x80225f90`):**
  removes the EXPLODING burst at `proj+0x1f8/0x1fc` (then zeroes it), then
  spawns **two** new effects via `Effect_SpawnSync` — a one-shot at
  `proj+0x1c0/0x1c4` (overwriting the now-dead countdown) and a positional one
  (attached to `proj+0xac`) at `proj+0x1c8/0x1cc` — and computes the alpha/
  lifetime fade ramp into `proj+0x1d0..0x1ec` from the render-state block
  (`proj+0x104`) and HurtData regions (`proj+0x108`).
- **Teardown (BOMB `aux_a` @ `0x8022634c`):** removes the lingering positional
  effect at `proj+0x1c8/0x1cc` and zeroes it.

So the complete bomb effect-handle lifecycle is:
`proj+0x114/0x118` (the persistent anchors) are freed by the dtor; the
EXPLODING burst at `proj+0x1f8/0x1fc` is removed when FADE begins; the
positional FADE effect at `proj+0x1c8/0x1cc` is removed by `aux_a` at teardown;
and the one-shot FADE effect at `proj+0x1c0/0x1c4` has no explicit teardown —
it self-terminates when its `Effect_SpawnSync` animation completes. No slot
leaks.

## 8. Spawn helpers

All live in the rider-side ability code. They build a `ProjectileDesc` from
the rider/machine context and call `Projectile_Create`.

| Addr          | Name               | Kind   | Position source | Velocity source | Assert on                      |
|---------------|--------------------|--------|-----------------|------------------|--------------------------------|
| `0x801a8c80`  | `spawnStarBullet`  | 0 or 1 | rider hand bone | rider base + self_vel | `rd->ability_hat_model` |
| `0x801a8df8`  | `spawnStarBullet_charged` | 11 | rider hand bone | rider base + self_vel | `rd->ability_hat_model` |
| `0x801a8f68`  | `spawnFireBullet`  | 2      | caller Vec3     | rider base + angle rotate | — |
| `0x801a9178`  | `spawnFireAura`    | 3      | aura slot (rider+0x318) | 0 | — |
| `0x801a9410`  | `spawnBomb`        | 4      | rider hand bone | rider base + self_vel | `rd->ability_hat_model` |
| `0x801a95a0`  | `spawnPlasmaBullet` | arg   | rider hand bone | rider base + self_vel | `rd->ability_hat_model` |
| `0x801a9870`  | `spawnPlasmaSpread` | arg   | rider hand bone | rider base + self_vel | `rd->ability_hat_model` |
| `0x801a9a54`  | `spawnSpikeAura`   | 12     | aura slot (rider+0x318) | 0 | — |
| `0x801a9b84`  | `spawnIceAura`     | 13     | aura slot (rider+0x318) | 0 | — |
| `0x801a9cb4`  | `spawnCrackerBullet` | 14   | caller args | rider base + self_vel | `rd->ability_data` |
| `0x801a9e78`  | `spawnSensorBomb`  | 15     | rider hand bone | rider base + self_vel | `rd->ability_hat_model` |
| `0x801aa028`  | `spawnGordo`       | 16     | rider hand bone | rider base + self_vel | `rd->ability_hat_model` |

### 8.1 Throw / transition wrappers

These are NOT spawners — they act on an already-created projectile, typically
transitioning it from HELD (state 0) to THROWN (state 1).

| Outer        | Inner        | Name                            | Meaning |
|--------------|--------------|---------------------------------|---------|
| `0x801a9580` | `0x80225824` | `Rider_TryThrowBomb(gobj, unused, velVec3)` | Sets position/orientation from rider hand, writes `proj+0x94..0x9c`, `Projectile_SetState(proj, 1, …, flags=1)`. Reads pos/forward/up from `*(proj+0x6c)+8` (a hand-bone matrix that only exists when a rider is actively holding the projectile) — **not safe to call from custom spawn paths** that haven't routed the projectile through a rider's hand. |
| `0x801a9fe8` | `0x80228f08` | `Rider_TryThrowSensorBomb(gobj, velVec3)` | Guards on sensor-ready flag at `proj+0x1bc`; sets velocity and state index 1. The flag is populated by sensor bomb's `post_init` (`0x80228d8c`) from `kind_data2[0x04]` at create time, so the guard is satisfied for any vanilla-spawned sensor bomb — but custom spawn paths that bypass `post_init` (or freshly-created bombs in the moment between create and the per-kind setup) will silently no-op through this throw wrapper. |
| `0x801aa008` | `0x8022a244` | `Rider_IsGordoThrowable(gobj) → bool`   | **Predicate, not a throw.** Returns true iff `state_id == 3` and flag bit 4 of `proj+0x1b6` is set. Don't let the name fool you — the actual gordo throw logic is `Gordo_EnterThrownState` at `0x8022a544` (see §8.2). |

### 8.2 The actual gordo throw transition

Unlike bomb/sensor bomb, gordo's HELD→THROWN transition is **not** a thin
`Projectile_SetState` wrapper. `Gordo_EnterThrownState(projGObj, velVec3, posVec3)`
at `0x8022a544` does a full per-kind setup that gordo state 1 fn1 / fn2 read
back every frame:

- `proj+0x1d8 = 2` and `proj+0x1e0..0x1e8 = velocity` — written into the
  animation object every frame by gordo state 1 fn1 (`0x8022a710`). Drive the
  spinning-gordo model rotation. Zero values leave the model unrotated.
- `proj+0x1dc = randomized angular velocity` — sign coin-flipped via
  `0x8041e668`. Without this the gordo doesn't spin.
- `proj+0x7c..0x84 = velocity-direction * kind_data[0x20]` (a velocity-derived
  acceleration, transformed through `proj+0xac`'s rotation matrix). This is
  the *real* impulse on the gordo — `desc.velocity` alone gives it forward
  motion but no acceleration profile.
- `proj+0x10c = proj+0x100` (lifetime). Without this, lifetime is zero and
  gordo state 1 fn2 short-circuits at its `cmplwi r0,0; beq` test (it bails
  out immediately rather than running its update body).

It also reads `proj->owner_gobj`'s rider-data fields via `Rider_GetForward`
(`0x80191ef8`) and `Rider_GetUp` (`0x80191f18`) to build the throw-time
orientation basis at `proj+0xd0..0xec`, so `owner_gobj` MUST be a real rider
GObj — `0` here is fine for `Rider_TryThrowBomb` but not for this call.

After the state setup it tail-calls two general projectile helpers:

- `Gordo_EnforceMaxActive` (`0x8022b45c`) — walks the projectile p_link list,
  counts active gordos in non-HELD/non-DESPAWN state, and force-despawns the
  excess (state-3 transition + zero accel/velocity/timers) once the count
  exceeds `kind_data2[0x1c]` (the per-stage max-gordos cap). Filters by
  `kind == PROJKIND_GORDO`; safe to leave alone.
- `Projectile_RebuildCollShape` (`0x80221c9c`) — calls `mpColl_Init` against
  the projectile's CollData (`proj+0x138`) using `proj->position` plus the
  hand-bone-derived basis vectors at `proj+0xd0`/`+0xdc` and per-kind dimensions
  from `kind_data[0x14]`. Called from all three vanilla throw transitions
  (bomb at `0x80225968`, sensor bomb at `0x80228fa4`, gordo at `0x8022a6c0`)
  — it's the post-throw collision-shape refresh, not gordo-specific.

## 9. Custom spawn recipe (no copy ability required)

To spawn a thrown projectile of any kind without a Bomb/Phan-Phan ability
being active — i.e. bypassing the rider-hand bone chain — the flow used by
`mods/custom_events/src/spawn_projectile.c` is below.

> **Status:** as of this writing the three `SpawnProjectile_*Trap()` entry
> points in `spawn_projectile.c` (`_BombTrap`, `_GordoTrap`,
> `_SensorBombTrap`) are **dormant** — they compile and are correct, but
> nothing in the tree calls them yet (no trap dispatcher is wired to them).
> Treat this section as the reference implementation, not a live code path.

The real `SpawnProjectileForPlayer(ply_idx, kind, distance)` reads the
player's machine via `Ply_GetMachineGObj(ply_idx)` (`md = mg->userdata`) and
takes `desc.position`/`forward`/`up`/`velocity` from `md->pos` (0x3e8),
`md->forward` (0x418), `md->up` (0x424), and `md->velocity` (0x324) — *not*
from a `RiderData`. The owner id is pulled from the rider GObj
(`Ply_GetRiderGObj`) only when present (`owner = *(int *)rg->userdata`, i.e.
`rd->x0`) and defaults to 0 otherwise. The skeleton:

```c
ProjectileDesc desc = {0};
desc.kind = PROJKIND_BOMB;       // or SENSORBOMB / any kind whose state 1 is "flying"
desc.owner_unk1 = owner;         // rd->x0 if a rider GObj exists, else 0; required
desc.owner_unk2 = owner;         // for kinds whose state code reads owner_gobj (gordo)
desc.position   = md_pos + md_forward * distance;   // somewhere out front

desc.forward    = md->forward;
desc.up         = md->up;
desc.velocity_scale = 1.0f;
// Inherit carrier motion AND add a forward throw impulse — `md->velocity`
// alone leaves the projectile co-moving with the machine, which both looks
// like the bomb is glued to Kirby and means it spends the entire fall arc
// inside Kirby's geometry until env-coll fires. A constant-magnitude forward
// kick keeps the trajectory predictable across machine speeds.
desc.velocity.X = md->velocity.X + md->forward.X * THROW_SPEED;
desc.velocity.Y = md->velocity.Y + md->forward.Y * THROW_SPEED;
desc.velocity.Z = md->velocity.Z + md->forward.Z * THROW_SPEED;
desc.type_flag  = 1;
desc.charge     = 1.0f;

void *handle = Projectile_Create(&desc);
if (!handle) return;

// After postInit, PROJKIND_BOMB / SENSORBOMB are in state 0 (HELD), pinned
// to a nonexistent rider hand and never detonating. Manually advance to
// state 1, mirroring vanilla throwBomb:
ProjectileData *proj = Projectile_GetData(handle);   // *(handle + 0x2c)
if (!proj) return;

// The trapped player IS the owner, so vanilla owner-exclusion would drop
// the explosion on them unless we opt in on BOTH scan paths (see §5.4).
proj->flag_a |= PROJ_ALLOW_SELF_HIT_INBOUND;   // proj+0x1b4 bit 0
proj->flag_b |= PROJ_ALLOW_SELF_HIT_OUTBOUND;  // proj+0x1b5 bit 4

// The per-frame physics reads proj+0x94..0x9c (which vanilla Projectile_Create
// leaves at zero — the desc.velocity was copied to the spawn-snapshot slot
// at proj+0x88). Seed it before the state transition, same order as vanilla:
proj->velocity = desc.velocity;

Projectile_SetState(proj, /*BOMB_STATE_THROWN / SENSOR_BOMB_STATE_ARMED_FLYING=*/1,
                    1.0f, 1.0f, /*flags=*/1);
```

`flags=1` matches vanilla throw: skip the rider-attached cleanup path that
post-init ran for state 0. The flag-setting and `Projectile_GetData` are
exactly what the live `SpawnProjectileForPlayer` does; without the self-hit
flags the trap can silently miss the player who is its own owner.

### 9.1 Kinds requiring per-kind throw setup

This bare recipe works for any kind whose state-1 callbacks only read fields
already populated by `Projectile_Create` — bomb, sensor bomb, plasma, sword
star, etc. For **gordo** it does *not*: gordo state 1 fn1/fn2 read per-kind
scratch fields at `proj+0x1d8`/`+0x1dc`/`+0x1e0..0x1e8` (rotation cache),
`proj+0x7c..0x84` (acceleration impulse derived from kind_data), and
`proj+0x10c` (lifetime) that vanilla `Projectile_SetState(proj, 1, …)`
leaves at zero. The visible symptoms of going through the bare recipe:

- gordo doesn't spin (rotation values stay at zero)
- no real impulse, only the inherited `desc.velocity`
- lifetime is zero, so state 1 fn2 short-circuits and the gordo's update
  body never runs
- the model can render in a degenerate state and look invisible

Use `Gordo_EnterThrownState` (`0x8022a544`) for gordo instead of the bare
`SetState(1)`. It does the full per-kind setup *and* the post-throw helpers
(`Gordo_EnforceMaxActive`, `Projectile_RebuildCollShape`) — see §8.2. It
needs `proj->owner_gobj` to be a real rider GObj because it reads
`Rider_GetForward` and `Rider_GetUp` from it to build orientation.

For single-state kinds (Sword Star A/B/Charged, Plasma Spread) the
projectile already starts in its one flying state after `Projectile_Create`
— no extra call is needed.

If you find another kind whose state-1 fn slots reference `proj+0x1c0`-band
scratch that nothing else writes, expect to need a similar dedicated
"enter-thrown" routine; the bare recipe is the floor, not the ceiling.

## 10. Hook points for mod code

| Need                              | Hook at           | Mechanism                | Notes |
|-----------------------------------|-------------------|--------------------------|-------|
| On-spawn, any kind                | `Projectile_Create` (`0x8021f428`) | `HOOKCREATE` | `r3 = desc` at entry; wrap to read `desc->kind`. |
| On-despawn, any path              | `0x8021ff54` (userdata dtor) | `HOOKCREATE` | Catches both lifetime-expiry and fell-into-void.  |
| On-despawn, lifetime-only         | `0x80220364` (`Projectile_Despawn`) | `HOOKCREATE` | `r3 = proj`. |
| On-despawn, aura-only             | `0x802230a0` (`Projectile_DespawnGObj`) | `HOOKCREATE` | `r3 = projGObj`. Thin wrapper vanilla only calls from the Fire/Spike/Ice `*_LoseAbility_Exit` handlers — useful if you want to intercept the aura teardown path without touching lifetime-expiry or fell-into-void destruction. |
| On-hit, projectile-side           | `0x8021fc74` (prio-9) | `HOOKCREATE` | Entire collision-scan proc. |
| On-hit logging                    | `0x8018cf94` (`HitColl_SetDamageLog`) | `HOOKCREATE` | Shared with all damage sources — filter by attacker. |
| On-state-change                   | `0x8021f7dc` (`Projectile_SetState`) | `HOOKCREATE` | `r3=proj, r4=state_index`. |
| Override spawn damage             | `0x80221440` (`Projectile_InitHurtData`) exit | `HOOKCREATE` | Patch region fields on `proj+0x108`. |

The outer GObj is always `r3` in `Projectile_Create` at return; the inner
`ProjectileData` is always `*(gobj+0x2c)`. Use `Projectile_GetOwnerGObj`
(`0x8022312c`) if you only have a proj and need the owner.

## 11. Open questions

- **Concrete contents of `kind_data+0x14..0x30`.** The structural fields of the
  per-kind data table are mapped (see `ProjKindData` in `projectile.h`):
  `+0x00 state_table`, `+0x04 NULL`, `+0x08 model descriptor` (word0 →
  `HSD_JObjLoadJoint`, NULL falls back to a global default model), `+0x0c
  state_anim_spec_array`, `+0x10 hurt_region_spec`. What remains unknown is
  whether anything lives at `+0x14..0x30` and what it is. No bomb state
  function dereferences `kind_data` at all — its damage comes from the HurtData
  built at create (from `+0x10`), and its fade params from the per-projectile
  `proj+0x104` block. So `+0x14..0x30` may simply be unused by the kinds
  covered so far. Pinning it down needs a mid-stage runtime dump: the per-stage
  loader writes the `0x8055a9a8` table from data, not code, so the values only
  exist while a stage is loaded. **Low priority** — nothing in the known code
  paths reads it.
