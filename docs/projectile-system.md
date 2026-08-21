# Projectile System

The projectile is Kirby Air Ride's actor type for transient, physics-driven objects: thrown bombs,
fired bullets, hovering auras, Gordo, Phan-Phan throws, firecrackers. All 17 `ProjectileKind` values
share one GObj/proc class and one update pipeline, and specialise through a per-kind vtable plus a
per-kind state table. The structs, enums and function prototypes named here live in
`externals/hoshi/include/projectile.h`.

## Object Layout

A projectile is a GObj registered with entity class 23 and p_link 14 (`GAMEPLINK_PROJECTILE`) - the
global list scanned by rider, machine, item, and box hit checks. (For comparison: rider is class 14,
machine 15, enemy 21.)

`Projectile_Create` returns that outer GObj. All per-projectile state lives in the inner
`ProjectileData` at `*(gobj + 0x2c)` (`gobj->userdata`), a 0x220-byte block allocated from the HSD
object pool at `0x8055a8f8` and zeroed at create: kind, current state, position, velocity, HurtData,
particle-effect handles, per-state callback pointers.

Two more per-kind tables hang off `kind`:

- The **vtable** at `0x804b4338[kind]` (`ProjKindVTable`) - `init` and `post_init` one-shots run by
  `Projectile_Create`, two per-frame transform refreshers, two aux hooks, a `despawn` slot, and the
  pointer to the state table. A NULL `despawn` falls back to `GObj_Destroy`; most bullet kinds
  install a slot that is itself a bare `GObj_Destroy`, so they vanish without a fade.
- The **kind data** at `0x8055a9a8[kind]` (`ProjKindData`) - model, animation specs, default
  lifetime, mpColl description, vulnerable-region list.

Two kind pairs alias at the vtable pointer level: `PROJKIND_PLASMA_A` (5) and `_PLASMA_B` (6) both
use `0x804b47e0`; `PROJKIND_PLASMA_SPREAD_MID` (7) and `_SIDE` (8) both use `0x804b4848`. What
separates each pair is the per-kind *data*, not code.

### Kind-data availability

`0x8055a9a8` is filled **all at once by rider creation**, not per stage and not per copy ability.
`Rider_Create` (`0x8018e21c`) hands `rdData->rdDataKirby+0x20` - a `(count, entries[])` list that
`Rider_LoadMotionFile` (`0x801a5a8c`) copies out of `RdKirbyAbility.dat` - to the kind-data registrar
at `0x802201e0`. That list covers all 17 kinds, so every kind is spawnable once any rider exists.
`Projectile_ClearKindDataTable` (`0x8022011c`) zeroes the table at system init and `Projectile_Create`
dereferences the slot unguarded (`0x8021f5c4`), so custom spawners outside the normal rider lifetime
must check `((void **)0x8055a9a8)[kind] != NULL`.

## State Tables

Each kind's state table is an array of 24-byte `ProjectileStateEntry` records: a `state_id`
(`0xffffffff` is a sentinel), a flags word, and four function pointers.

All four fn slots are **per-frame** callbacks, not on-enter/on-exit hooks; transitions come from
`Projectile_SetState` calls made inside `fn0/fn1/fn2` themselves. For a one-shot on-enter, use the
per-kind vtable's `init` or `post_init` - there is no entry-level on-enter slot. The GObj procs
registered by `Projectile_Create` dispatch the slots each frame at priorities 1, 4, 5 and 6.

`Projectile_SetState(proj, index, blendA, blendB, flags)` (`0x8021f7dc`):

1. Selects `state_table[index]` from `proj+0x30` when `index < proj+0x28`, else from `proj+0x34`
   with offset `(index - proj+0x28) * 24`. `Projectile_Create` hardcodes `proj+0x28` to 0
   (@ `0x8021f508`) and nothing rewrites it, so the `proj+0x30` branch is dead in vanilla and that
   field is available as a mod-provided extension table. Every dispatch reads `proj+0x34`, loaded
   from the vtable's `state_table` at create.
2. Writes `proj+0x24 = index`, `proj+0x2c = entry.state_id`, copies the four fn pointers into
   `proj+0x150..0x15c`, and writes `proj+0x38 = kind_data->state_anim_spec_array + state_id*16` (the
   per-state animation/blend spec, 16 bytes per entry - **not** the 24-byte state_table entry).
3. Runs the animation transition via `Projectile_AssignStateFlags`.

It does **not** touch physics velocity. Vanilla throw code writes velocity first, then transitions.

### Per-kind state tables

Entries are indexed 0..N-1. Pass the **index** to `Projectile_SetState`, not `state_id` - `state_id`
duplicates across entries in two kinds.

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

The per-kind state index enums (`BombState`, `SensorBombState`, `GordoState`, `FireBulletState`, the
three aura enums, `PlasmaState`, `PlasmaDState`, `FirecrackerState`) are in `projectile.h`. The
non-obvious cases:

- **Sensor bomb** has five entries but `state_id` 2 appears twice (index 2 ARMED_STATIONARY and
  index 4 FADE), with different callbacks.
- **Gordo** index 1 and 2 share `state_id` 1: index 1 scales the model up while flying, then
  transitions internally to index 2, which locks scale and starts the self-despawn timer.
- **Gordo index 3** is the sentinel (`state_id` -1). `Projectile_SetState` zeros `proj+0x38` there
  but still installs the fn slots, so the sentinel's fn0 (`0x8022b09c`) runs for one frame to spawn
  the despawn particle burst before teardown. This is the only vanilla use of the sentinel pattern
  inside a live state transition.
- **Held / idle states** do almost nothing except re-snap position to the rider hand bone from fn3
  (`bl 0x80191ffc`); every other slot is `blr`. That is why a bomb, sensor bomb or aura left in
  state 0 with no rider just sits at a garbage position and never detonates.
- **Aura index 2** ("cooling"/"settled") is structurally identical to idle with a different animation
  class. The label is an interpretation.
- The **"absorbed" state 1** of plasma A/B/C and firecracker is a rider-alive watchdog reading
  `proj+0x1b8`/`0x1bc`, not an active phase.

### State flags word

The `flags` word at entry+0x04 is routed through `Projectile_AssignStateFlags` (`0x80222298`), called
from `Projectile_SetState`. The upper byte is a boolean "animated state"; the lower byte is a
per-kind animation-class tag, compared as a whole value rather than tested as bits - no state table
uses more than two distinct flag values (one "animated", plus 0x0000 for held/sentinel states).

What the low byte gates is *whether a fresh animation-instance id is minted*. `proj+0x194` gets a new
id from `AllocSeqId16` (`0x80231b68`) when the low byte is 0 **or** differs from the previous
transition's low byte; if the anim class is unchanged and nonzero, the old id is kept. `AllocSeqId16`
takes no arguments - it reads, increments, and writes back a global `u16` counter at `0x805DD8A0`
(`r13+0x7c0`), returns the pre-increment value, and resets to 1 on wrap. So `proj+0x194` is a
generation counter that bumps on every anim-class change, not an id resolved from a lookup table.

The flag word itself is stored verbatim at `proj+0x17c`, whose low byte is what the next transition
compares against; `proj+0x184` and parts of `proj+0x18a`/`0x18b` are zeroed each transition. These
stored words are what the per-kind `refresh_xfm_*` callbacks read each frame to drive the HSD
animation object.

## Per-Frame Update Procs

`Projectile_Create`'s epilogue registers ten GObj procs on the projectile GObj via `GObj_AddProc`
(`0x804288a4`). Each priority is one step of a fixed pipeline, using the same priority ranks as other
actor types. All ten attach at create time - per-kind differences are expressed through the fn0..fn3
state entries, never through extra procs.

| Prio | Addr | Name | What it does |
|-----:|------|------|--------------|
| 0 | `0x8021f9b4` | `Projectile_Proc0_FrameStart` | Bump `proj+0x110` frame counter, zero accel (via `0x80220350`), `HurtData_ResetFrame`, tick the `proj+0x134` intang timer, call the `proj+0x160` user hook. The hook runs **after** the accel zeroing and before prio 4 integrates, which is what makes it the place to write a custom accel. |
| 1 | `0x8021fa18` | `Projectile_Proc1_RunStateFn0` | `HurtData_UpdatePerFrame`, call `state_fn0`, then tick `proj+0x10c` lifetime and despawn at zero. |
| 4 | `0x8021faa4` | `Projectile_Proc4_Physics` | Call `state_fn1`; integrate `vel += accel` then `pos += vel`. Does **not** touch `pos_prev` - prio 21 does that. |
| 5 | `0x8021fb44` | `Projectile_Proc5_RunStateFn2` | Clear the env-coll flag, call `state_fn2`. |
| 6 | `0x8021fb88` | `Projectile_Proc6_RunStateFn3` | Call `state_fn3`, then `0x80220310` (mpColl pos sync), then a per-kind sub-cleanup. |
| 7 | `0x8021fbec` | `Projectile_Proc7_PostState` | Call the `proj+0x164` user hook. If `pos.y` falls below a floor threshold, `GObj_Destroy`; else update HurtData radius/position from `cur_scale` and `type_flag`. |
| 8 | `0x8021fc70` | `Projectile_Proc8_Stub` | Single `blr`. Reserved priority. |
| 9 | `0x8021fc74` | `Projectile_Proc9_HitColl` | Outbound hit detection. |
| 10 | `0x8021fcd4` | `Projectile_Proc10_HitReact` | Resolve the strongest logged hit, run the per-kind on-hit callback (`proj+0x16c`); state-transition if it returns non-zero. |
| 21 | `0x8021fed4` | `Projectile_Proc21_EndOfFrame` | Compute `vel_diff = pos - pos_prev`, save `pos -> pos_prev`, finalise the HurtColl attach. |

## Hit Detection And Damage

### Outbound scans (prio 9)

`Projectile_Proc9_HitColl` iterates five p_link lists via `Projectile_CheckRiderCollision`
(`0x802215a4`), `Projectile_CheckMachineCollision` (`0x80221660`), `Projectile_CheckProjectileCollision`
(`0x8022171c`, skips self), `Projectile_CheckItemCollision` (`0x80221814`) and
`Projectile_CheckStageHazardColl` (`0x80221878`). Each follows the same skeleton: walk the target
list, filter by pause/disabled gating, then call `HitColl_CheckCollision` (`0x8018d284`) with
`r3 = victim` and `r4 = attacker`.

The rider-side outbound scan additionally calls `HitColl_CheckIfSamePlayer` (`0x8000b024`) for owner
exclusion, skipping a same-player match unless the outbound self-hit bit is set.

### Inbound scans

Victims also scan the projectile global list: `Rider_CheckProjectileHit` (`0x801963c8`),
`Machine_CheckProjectileCollision` (`0x801d7118`), `Box_CheckProjectileCollision` (`0x80252334`), and
the enemy-side check at `0x802020d4`.

The rider and machine scans read `Projectile_GetOwnerGObj` (`0x8022312c`) for owner exclusion: if
`proj->owner_gobj == victim_gobj`, skip unless the inbound self-hit bit is set. The owner pointer is
only ever **compared**, never dereferenced, on these paths. `Box_CheckProjectileCollision` does no
owner check at all - it goes straight from the HurtData accessor at `0x80223120` to
`HitColl_CheckCollision`, so boxes are hit by any projectile regardless of these flags.

### Self-hit allow flags

Owner exclusion is gated by *separate* bits on the inbound and outbound scan paths, on different
bytes of `ProjectileData`, because either scan can resolve a hit first:

| Scan side | Flag | Set by vanilla? |
|-----------|------|-----------------|
| Inbound (victim walks the projectile list) | `PROJ_ALLOW_SELF_HIT_INBOUND` = `proj->flag_a` (`+0x1b4`) bit 0 | Only sensor bomb's `post_init` (`0x80228d8c`). Bomb and gordo do not. |
| Outbound (`Projectile_CheckRiderCollision` walks the rider list) | `PROJ_ALLOW_SELF_HIT_OUTBOUND` = `proj->flag_b` (`+0x1b5`) bit 5 (`0x20`) | Never. Vanilla throws target *other* players, so the default exclusion is what they want. |

The outbound mask is **`0x20`, not `0x10`**: the test at `0x802215e8` is `rlwinm. r0,r0,27,31,31`,
rotating bit 5 down to the LSB, and `Projectile_InitRuntimeState` clears the same bit with
`rlwimi r0,r3,5,26,26`. A second exclusion at `0x80221620` uses bit 3 (`0x08`).

**A projectile that must damage its own owner-player has to set both bits.** Custom-spawned trap
projectiles, where `owner_gobj` is the trapped player, are the canonical case. Setting only one risks
the hit being silently dropped depending on which scan path resolves it first; setting both is safe,
since the flags gate only same-player exclusion.

### Damage values

`Projectile_InitHurtData` (`0x80221440`) calls `HurtData_Create(gobj, 5, 2, count, 0)`. The `2` is
hardcoded, so **every projectile gets exactly two attack regions** (at `hurt+0x0c`, 200-byte stride,
count at `hurt+0x08`) - that pair is what `HitColl_SetDamageLog` iterates. Their damage is driven by
the current state's animation spec (`proj+0x38`), so a projectile put into its real flying state with
valid `kind_data` deals vanilla damage with no extra setup.

`kind_data->vuln_region_spec` supplies only the *vulnerable*-region list (`hurt+0x14`, 0x44-byte
stride) and is NULL for every kind except `FIRE_BULLET` and `SENSORBOMB`.

`HurtData_Create` also stores the **projectile's own GObj** as the attacker identity at `hurt+0x04`,
not the owner - which is why an ownerless projectile still logs damage normally.

To override damage, either hook the exit of `Projectile_InitHurtData` and rewrite `base_damage`
(`+0x04`) / `base_knockback` (`+0x24`) across the new HurtData's regions (stride 0xC8), or hook
`HitColl_SetDamageLog` (`0x8018cf94`) per hit and discriminate by attacker HurtData kind.

Explosion-class projectiles cache a handful of scalars at `proj+0x1d0..0x1ec` on the EXPLODING ->
FADE transition, but those come from **per-projectile** blocks, not `kind_data`: `proj+0x1e4`/`0x1e0`
are copied from HurtData regions 0/1, and the fade/alpha ramp is read from the render-state block at
`proj+0x104`. No bomb state function dereferences `kind_data` at all.

## Lifecycle

### Creation

`Projectile_Create` (`0x8021f428`):

1. `GObj_Create(kind=23, pLink=14, prio=0)`, then register render at `0x80220000` via `GObj_AddGXLink`.
2. `HSD_ObjAlloc` the 0x220-byte `ProjectileData` from pool `0x8055a8f8`, attach it with
   `GObj_AddUserData(gobj, 23, dtor=Projectile_UserDataDtor, ud=proj)`, and zero it.
3. Copy descriptor fields (kind, owner, position, forward/up, velocity, type_flag, charge) into proj,
   set the always-on alive markers (`proj+0x1b5` bit 2, `proj+0x218` bit 0), and build the
   orientation matrix via `0x80220250`.
4. Allocate sub-resources: scratch mtx, sub-vtable table (`proj+0x6c`), render-state block
   (`proj+0x104`, via `Projectile_AllocRenderState` `0x802205b0`), two particle-effect handles
   (`proj+0x114`/`0x118`, via `0x802364e0`), text/vfx slot, `mpColl` CollData (`proj+0x138`, if the
   kind wants one), anim object, and HurtData via `Projectile_InitHurtData`. The model joint comes
   from `HSD_JObjLoadJoint` (`0x8040afe8`) on `kind_data->model_desc`, or a global default when NULL.
5. Call the per-kind `init`.
6. Register the ten GObj procs.
7. Run `Projectile_InitRuntimeState` (`0x8021f2a0`) and its chain to zero accel/velocity, seed
   `cur_scale` and lifetime, and enter state 0.
8. Call the per-kind `post_init` - for throwable kinds this spawns the "spawn" particle effect via
   `Effect_SpawnSync` (`0x80236c40`).

It touches no rider bones and no rider state, so it is safe to call from anywhere. What is *not* safe
is leaving the result in state 0 for a kind whose state 0 is "held in a rider's hand".

### Destruction

Two vanilla paths:

1. **Lifetime expiry** via `Projectile_Despawn` (`0x80220364`), triggered from prio 1 when
   `proj+0x10c` ticks to 0. Calls the kind's `despawn` slot, or `GObj_Destroy` when it is NULL.
2. **Fell into the void** via prio 7 when `proj->pos.y` drops below a threshold loaded from `r2-16104`.
   Calls `GObj_Destroy` directly.

Both unwind through `Projectile_UserDataDtor` (`0x8021ff54`), which destroys the HurtData; stops and
frees the two particle-effect handles (via the Effect helpers `0x8023641c` / `0x80236778`); runs the
per-kind `aux_a`; destroys the text/vfx slot, anim obj, render-state block and mpColl CollData; then
returns the `ProjectileData` block to its HSD pool.

### Auras and the rider backref

Aura kinds (`PROJKIND_FIRE_AURA`, `SPIKE_AURA`, `ICE_AURA`) spawn with zero velocity. Each copy
ability's init handler stores the returned GObj handle at **`rider+0x3F0`**: `Fire_AbilityInit`
(`0x801aed50`), `Spike_AbilityInit` (`0x801b385c`), `Ice_AbilityInit` (`0x801b4718`). The matching
`Fire_LoseAbility_Exit` (`0x801af330`), `Spike_LoseAbility_Exit` (`0x801b3d18`) and
`Ice_LoseAbility_Exit` (`0x801b49d4`) each do the same three things:

1. Load `rider+0x3F0`; if NULL, skip the destroy.
2. Test a rider flag byte (Fire reads bit 4 of `rider+0x824`). If set, call `GObj_Destroy` directly -
   a hard teardown that skips `aux_a`, used when the rider is being wholesale reset (respawn, round
   end). Otherwise call `Projectile_DespawnGObj` (`0x802230a0`), which runs `aux_a` first.
3. Zero `rider+0x3F0`.

Only these three auras use `rider+0x3F0`. Throwable kinds keep their projectile handle inside the
ability's own state block instead.

## Per-Kind Scratch

Roughly `proj+0x1c0` through `proj+0x1f8` is per-kind scratch - treat it as opaque unless you are the
kind's own state code. Kinds overload the same offsets across states, and the handles parked there
are **particle-effect** handles (Effect module: `Effect_SpawnSync` `0x80236c40`, removal via
`0x8023624c`), not audio.

The bomb is the worked example. `proj+0x1c0` is untouched through HELD and THROWN; the EXPLODING
state reuses it as the detonation countdown, seeded by `Bomb_DetonationTrigger` (`0x80225c8c`) and
ticked by `Bomb_State2_DetonationTimerTick` (`0x80225d48`), with the burst effect tracked at
`proj+0x1f8`/`0x1fc`. `Bomb_State2End_TransitionToFade` (`0x80225f90`) then removes that burst,
spawns two new effects - a one-shot over the now-dead countdown at `proj+0x1c0`/`0x1c4` and a
positional one at `proj+0x1c8`/`0x1cc` - and computes the fade ramp into `proj+0x1d0..0x1ec`. At
teardown `Bomb_AuxA_RemoveEffect` (`0x8022634c`) reaps the positional FADE effect. The one-shot at
`proj+0x1c0`/`0x1c4` has no explicit teardown; it self-terminates when its effect animation
completes. No slot leaks.

## Spawn Helpers

All live in the rider-side ability code; they build a `ProjectileDesc` from the rider/machine context
and call `Projectile_Create`. Prototypes are in `projectile.h`. The default shape is: position from
the rider hand bone, velocity from the machine's world velocity plus `rider->self_vel`, and an assert
on `rd->ability_hat_model`. The exceptions:

| Addr | Name | Kind | Deviates by |
|------|------|------|-------------|
| `0x801a8f68` | `spawnFireBullet` | 2 | position from a caller `Vec3`; velocity rotated by an angle; no hat assert |
| `0x801a9178` | `spawnFireAura` | 3 | position from the aura slot at `rider+0x318`; zero velocity; no hat assert |
| `0x801a9a54` | `spawnSpikeAura` | 12 | same as fire aura |
| `0x801a9b84` | `spawnIceAura` | 13 | same as fire aura |
| `0x801a9cb4` | `spawnCrackerBullet` | 14 | position/forward from caller args; asserts on `rd->ability_data` |

The remaining helpers (`spawnStarBullet` `0x801a8c80`, `spawnStarBullet_charged` `0x801a8df8`,
`spawnBomb` `0x801a9410`, `spawnPlasmaBullet` `0x801a95a0`, `spawnPlasmaSpread` `0x801a9870`,
`spawnSensorBomb` `0x801a9e78`, `spawnGordo` `0x801aa028`) follow the default shape exactly.

### Throw / transition wrappers

These act on an already-created projectile, typically moving it from HELD to THROWN. All three are
poor fits for custom spawn paths:

- `Rider_TryThrowBomb` (`0x801a9580` -> `0x80225824`) reads pos/forward/up from `*(proj+0x6c)+8`, a
  hand-bone matrix that only exists while a rider is actively holding the projectile.
- `Rider_TryThrowSensorBomb` (`0x801a9fe8` -> `0x80228f08`) guards on the sensor-ready flag at
  `proj+0x1bc`, written by sensor bomb's `post_init` from `kind_data2[0x04]`. Custom paths that
  bypass `post_init` silently no-op through this wrapper.
- `Rider_IsGordoThrowable` (`0x801aa008` -> `0x8022a244`) is a **predicate, not a throw**: true iff
  `state_id == 3` and bit 4 of `proj+0x1b6` is set.

### Gordo's throw transition

Gordo's HELD -> THROWN transition is not a thin `Projectile_SetState` wrapper.
`Gordo_EnterThrownState(projGObj, velVec3, posVec3)` (`0x8022a544`) does the full per-kind setup that
gordo state 1's fn1/fn2 read back every frame:

- `proj+0x1d8 = 2` and `proj+0x1e0..0x1e8 = velocity`, written into the animation object every frame
  by gordo state 1 fn1 (`0x8022a710`) to drive the spinning model. Zeroes leave it unrotated.
- `proj+0x1dc = randomized angular velocity`, sign coin-flipped via `0x8041e668`. Without it the
  gordo does not spin.
- `proj+0x7c..0x84 = velocity-direction * kind_data[0x20]`, the real acceleration impulse.
  `desc.velocity` alone gives forward motion with no acceleration profile.
- `proj+0x10c = proj+0x100` (lifetime). Without it lifetime is zero and gordo state 1 fn2
  short-circuits before its update body.

It reads the owner's rider fields through `Rider_GetForward` (`0x80191ef8`) and `Rider_GetUp`
(`0x80191f18`) to build the throw-time orientation basis, so `owner_gobj` must be a real rider GObj -
`0` is fine for `Rider_TryThrowBomb` but not here.

It then tail-calls two general helpers:

- `Gordo_EnforceMaxActive` (`0x8022b45c`) walks the projectile p_link list, counts active gordos in
  non-HELD/non-DESPAWN state, and force-despawns the excess once the count exceeds `kind_data2[0x1c]`,
  the per-stage max-gordos cap.
- `Projectile_RebuildCollShape` (`0x80221c9c`) calls `mpColl_Init` against `proj+0x138` using the
  projectile position, the basis vectors at `proj+0xd0`/`0xdc`, and per-kind dimensions from
  `kind_data->mpcoll_desc`. All three vanilla throw transitions call it; it is the generic
  post-throw collision-shape refresh, not gordo-specific.

## Custom Spawns

`SpawnProjectileForPlayer` in `mods/custom_events/src/spawn_projectile.c` is the reference
implementation for spawning a thrown projectile with no copy ability active. It builds the descriptor
from the player's **machine** (`Ply_GetMachineGObj`, then `md->pos` / `forward` / `up` / `velocity`)
rather than a `RiderData`, takes `owner` from the rider GObj's `rd->x0` when a rider exists and 0
otherwise, sets both self-hit bits, seeds `proj->velocity`, and finally transitions to the flying
state (`Gordo_EnterThrownState` for gordo, `Projectile_SetState(proj, 1, 1.0f, 1.0f, 1)` for bomb and
sensor bomb). The three `SpawnProjectile_*Trap()` entry points are wired to no trap dispatcher yet.

Three details in it are easy to get wrong:

- **Velocity has to be written twice.** `Projectile_Create` copies `desc.velocity` into the spawn
  *snapshot* at `proj+0x88`; per-frame physics reads `proj+0x94`, which stays zero. Seed
  `proj->velocity` before the state transition, matching vanilla throw ordering.
- **Inherit machine velocity *and* add a forward impulse.** Using `md->velocity` alone leaves the
  projectile co-moving with the machine - it looks glued to Kirby and stays inside his geometry for
  the whole fall arc until env-coll fires. A constant-magnitude forward kick keeps the trajectory
  predictable across machine speeds.
- **Both self-hit bits.** The trapped player is the owner, so vanilla owner exclusion drops the hit
  otherwise.

`flags=1` on the `Projectile_SetState` call matches vanilla throw: skip the rider-attached cleanup
path that `post_init` ran for state 0.

### Kinds requiring per-kind throw setup

The bare recipe works for any kind whose state-1 callbacks only read fields `Projectile_Create`
already populated - bomb, sensor bomb, plasma, sword star. **Gordo needs `Gordo_EnterThrownState`**;
a plain `Projectile_SetState(proj, 1, ...)` leaves its rotation cache, impulse and lifetime at zero,
producing no spin, no impulse, an fn2 that short-circuits, and a model that renders degenerate and
looks invisible.

For single-state kinds (Sword Star A/B/Charged, Plasma Spread) the projectile is already in its one
flying state after `Projectile_Create` - no extra call is needed.

If another kind's state-1 fn slots reference `proj+0x1c0`-band scratch that nothing else writes,
expect to need a similar dedicated enter-thrown routine.

### Ownerless spawns

A projectile spawned by the world rather than by a player - a stage hazard, a volcano - wants
`desc.owner_gobj = NULL, desc.owner_unk2 = 0`. That is safe for the **shared pipeline** and is in fact
the cleanest way to make a projectile threaten everybody: `HitColl_CheckIfSamePlayer` (`0x8000b024`)
NULL-checks `r3` before its first dereference and returns 0, so a NULL owner reads as "never the same
player" and no victim is excluded. The inbound scans only compare the pointer, and
`HitColl_SetDamageLog` / `Projectile_Proc10_HitReact` never touch it.

It is **not** safe per *kind*. `Rider_GetHandBonePos` (`0x80191ffc`), `Rider_GetUp` (`0x80191f18`)
and `Rider_GetForward` (`0x80191ef8`) all open with an unguarded `lwz r5,0x2c(r3)`, so any kind whose
`init` / `post_init` / state callbacks route the owner into one of them takes a DSI on a NULL owner.

| Kind | Ownerless? | Why |
|------|-----------|-----|
| 7/8 `PLASMA_SPREAD_MID`/`_SIDE` | **Yes** | Single state; `fn0`/`fn1`/`fn3` are all `blr`. Best default. |
| 5/6 `PLASMA_A`/`_B` | **Yes** | Same, but lifetime is only 6 / 9 frames - override it. |
| 11 `SWORD_STAR_CHARGED` | **Yes** | `init` is a bare `blr`; `post_init` overwrites velocity with `forward * 3.465`. |
| 14 `FIRECRACKER` | **Yes** | `post_init` copies `desc.velocity` verbatim. Detonates on any surface, and self-destructs on its own fuse. |
| 4 `BOMB`, 15 `SENSORBOMB` | **Yes, if transitioned immediately** | Create is clean, but state 0's `fn3` hand-snaps, so the `Projectile_SetState(proj, 1, ...)` must happen before any proc runs. |
| 2 `FIRE_BULLET` | **Yes, with a borrowed owner** | `init` (`0x80224cc8`) and `post_init` (`0x80224d4c`) read rider fields through the owner *during* `Projectile_Create`, so `desc.owner_gobj` must be a live rider GObj for that call. Nothing afterwards touches it, so `proj->owner_gobj = NULL` right after create restores full ownerless behaviour. Also seed the charge scratch (below). |
| 0/1 `SWORD_STAR_A`/`_B`, 9 `PLASMA_C`, 10 `PLASMA_D` | No | Same crash in `init`; and their `fn1` homing helper (`0x80223298`) rewrites `proj->velocity` every frame, so they cannot hold a ballistic arc even with a real owner. |
| 3 `FIRE_AURA`, 12 `SPIKE_AURA`, 13 `ICE_AURA` | No | Every state's `fn3` re-snaps position to the owner's hand bone each frame. **The auras cannot fly at all** - there is no thrown ice kind in the game. |
| 16 `GORDO` | No | `Gordo_EnterThrownState` reads the owner's rider fields for the throw basis. |

### Gravity

Nothing in the pipeline applies gravity. Accel at `proj+0x7c..0x84` is zeroed every frame by prio 0
and integrated into velocity by prio 4, so a one-shot accel write never survives. To arc a
projectile, write the accel from a per-frame hook:

```c
static void Gravity(void *p) { ((ProjectileData *)p)->accel.Y = -0.35f; }
...
proj->user_hook_0 = Gravity;   // set AFTER any Projectile_SetState - it clears 0x160..0x178
```

`user_hook_0` is invoked at the tail of prio 0, immediately after the zeroing and before prio 4
integrates. The flying-state `fn1` of `BOMB` / `FIRECRACKER` / `SENSORBOMB` / `FIRE_BULLET` samples a
stage air current (`0x800ceb18`) and **adds** it to accel, so it never clobbers a hook-written value;
the plasma and sword-star kinds have an all-`blr` `fn1` and are pure ballistic hosts.

### Lifetime

`proj->lifetime` is seeded automatically - `Projectile_InitRuntimeState` copies `proj+0x100` (from
`kind_data->params[3]`) into `proj+0x10c`. Prio 1 treats **0 as infinite**. Defaults by kind:

| Kind | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|------|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|
| frames | 240 | 480 | 120 | 0 | 0 | 6 | 9 | 120 | 120 | 200 | 360 | 90 | 0 | 0 | 240 | 0 | 540 |

Plasma A/B's 6-9 frames are far too short for a long flight, and bomb / sensor bomb never expire on
their own. Overwrite `proj->lifetime` after create (and after any `SetState`); it is a plain frame
counter with no other consumer.

Lifetime is **not** the only self-destruct. `FIRECRACKER` carries an independent fuse: its state-0
`fn0` (`0x8022888c`) counts down the two-word pair at `proj+0x1b8` / `proj+0x1bc` and bursts via
`0x80228b3c` when both reach zero, regardless of `lifetime`. A kind whose state `fn0` is a `blr`
(`FIRE_BULLET`, plasma, sword star) honours `lifetime` alone.

### Owner-derived scratch

A kind's `init` may cache values read off the owner and consume them much later, so a projectile can
spawn and fly correctly and only misbehave on impact. `FIRE_BULLET` is the case that matters:
`FireBullet_Init` (`0x80224cc8`) writes the owner's Fire-ability charge into kind scratch as
`proj+0x1b8 = charge / max` and `proj+0x1bc = charge`. On environment collision,
`FireBullet_ApplyChargeScale` (`0x80224ef8`) assigns `proj+0x1bc` to `cur_scale` and multiplies every
HurtData region's radius (`region+0x24`, `0xC8` stride) by `proj+0x1b8` and by `render_state` word0.

An owner who is not holding a charged Fire ability has `rd+0xa0c == 0`, so both land at zero and the
impact burst gets a zero-radius hitbox and a zero-scale model. The model's matrix scale collapses to
0, which makes the effect system print `Warning: effect request scale is zero. (kind=240006)` - the
scale is read out of the parent JObj's matrix at `+0x44` by `0x8023d0b8`, not from its `scale` field,
and is clamped to an epsilon. A custom spawner must seed both words itself (`1.0` = full charge)
after `Projectile_Create`.

## Hooks

| Need | Hook at | Notes |
|------|---------|-------|
| On-spawn, any kind | `Projectile_Create` (`0x8021f428`) | `r3 = desc` at entry; wrap to read `desc->kind`. |
| On-despawn, any path | `Projectile_UserDataDtor` (`0x8021ff54`) | Catches both lifetime expiry and fell-into-void. |
| On-despawn, lifetime only | `Projectile_Despawn` (`0x80220364`) | `r3 = proj`. |
| On-despawn, aura only | `Projectile_DespawnGObj` (`0x802230a0`) | `r3 = projGObj`. Vanilla calls it only from the Fire/Spike/Ice lose-ability handlers, so it intercepts aura teardown without touching lifetime expiry or void destruction. |
| On-hit, projectile side | `Projectile_Proc9_HitColl` (`0x8021fc74`) | The whole outbound collision-scan proc. |
| On-hit logging | `HitColl_SetDamageLog` (`0x8018cf94`) | Shared with all damage sources - filter by attacker. |
| On-state-change | `Projectile_SetState` (`0x8021f7dc`) | `r3 = proj`, `r4 = state_index`. |
| Override spawn damage | `Projectile_InitHurtData` (`0x80221440`) exit | Patch region fields on `proj->hurt_data`. |

All are `HOOKCREATE` sites. The outer GObj is always `r3` at `Projectile_Create`'s return; the inner
`ProjectileData` is always `*(gobj+0x2c)`. Use `Projectile_GetOwnerGObj` (`0x8022312c`) when you only
have a proj and need the owner.
