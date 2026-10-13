# Weapons

The weapon is Kirby Air Ride's actor type for short-lived, physics-driven objects: thrown bombs,
fired bullets, hovering auras, Gordo, firecrackers. "Weapon" is the game's own term (`wnparts.c`,
`WnCommon.dat`); this doc also calls them projectiles. All 17 `WeaponKind`s share one GObj class and
one update pipeline. They differ through a per-kind vtable, a per-kind state table and per-kind
data. A modder needs this to spawn a projectile from mod code, recognise one in a hit, change its
damage, or add a kind of their own.

## Object layout

A projectile is a GObj of entity class 23 on p_link 14 (`GAMEPLINK_WEAPON`), the list that rider,
machine, item and box hit checks scan. `Weapon_Create` returns that GObj. The state lives in a
`WeaponData` at `gobj->userdata`, a 0x220-byte block from the object pool at 0x8055a8f8, zeroed at
create. It holds the kind, state, position, velocity, HurtData, effect groups and per-state
callbacks.

Two per-kind tables hang off the kind:

- **The vtable** at `0x804b4338[kind]` (`WeaponKindVTable`): the state table and eight function
  slots - `system_init` (run by `Weapon_SystemInit`, 0x8021f1fc, on every 3D load), `init` and
  `post_init` (one-shots in `Weapon_Create`), two render-state loaders, `aux_a` (run by the
  destructor), `on_hit` (`+0x1c`, `int (wp, hit)`, run at priority 10; nonzero destroys the
  projectile) and `despawn`. Every function slot is NULL-checked where it is called. A NULL
  `despawn` falls back to `GObj_Destroy`, and most bullet kinds install one that is itself a bare
  `GObj_Destroy`, so they vanish without a fade. Kinds 5/6 share a vtable, as do 7/8; the per-kind
  data is what separates them.
- **The kind data** at `0x8055a9a8[kind]` (`WeaponKindData`): `params` (root model scale, cull
  radius, default lifetime), a render-state template copied to `wp + 0x104`, the model, the per-state
  animation specs, the environment-collider descriptor, and a vulnerable-region list that only
  `FIRE_BULLET` and `SENSORBOMB` have.

**Where the kind data comes from.** `Rider_Create` (0x8018e21c) hands a 17-entry list out of
`RdKirbyAbility.dat` (copied by `Rider_LoadMotionFile`, 0x801a5a8c) to
`Weapon_RegisterKindDataList` (0x802201e0). The registrar fills only empty slots, so every kind is
spawnable once any rider exists. `Weapon_ClearKindDataTable` (0x8022011c) zeroes the table on every
3D load. `Weapon_Create` reads the slot unguarded, so a spawner outside the normal rider lifetime
must check that the kind's slot is non-NULL.

**Adding a kind.** Both tables are exactly 17 entries. The word after the vtable table (0x804b437c)
is the `"WnCommon.dat"` string `Weapon_LoadCommonArchive` (0x80220194) loads, so an 18th kind needs
the vtable table relocated. Ten `lis`/`addi` pairs form its address: three in `Weapon_Create`, and
one each in `Weapon_SystemInit`, `Weapon_Proc10_HitReact`, `Weapon_UserDataDtor`, `Weapon_Despawn`
and `Weapon_LoadKindParams`, with two more in `Weapon_ReloadKindParams` (0x80220654). The
`Weapon_SystemInit` loop is bounded at 17, so it can stay on the vanilla table for a kind with no
`system_init`. The word after the kind-data table (0x8055a9ec) is padding: the clear loop stops at 17
and the registrar writes only listed kinds, so an 18th kind's data pointer can live there for the
run. Nothing else in the engine is indexed by kind; the readers of `WeaponGObj_GetKind` (0x80223184)
only compare against fixed kinds.

## Environment collision

A kind with a collider descriptor (`WeaponKindData.mpcoll_desc`, `+0x14`) gets its own mpColl
`CollData` at `wp->coll_data` (`+0x138`). `Weapon_CreateEnvColl` (0x80221cf0) creates it and
`Weapon_RebuildCollShape` (0x80221c9c) sizes it from the descriptor's radius, unscaled by the
projectile's `scale`. A kind with no descriptor (the three auras) has no environment collision.

`Weapon_UpdateEnvColl` (0x80221fd4) is one frame of that collider: `mpColl_Update`, the map sweep and
the pushback, then `flag_b` bit 0 (`WP_FLAGB_ENV_CONTACT`) if anything was touched. Every colliding
kind's `envcoll_callback` starts with it, so a replacement callback must call it too.

The pushback writes the resolved centre back into `wp->pos` and leaves velocity alone. A projectile
driving into a wall is pushed out along it every frame and slides until its own callback ends it.
What was touched is read off `coll_data->coll_info` (`under_rec_num`, `wall_rec_num`,
`top_rec_num`), with `Weapon_HasFloorContact` (0x80222144) and `Weapon_GetFloorContactNormal`
(0x802221b8) for the floor. Plasma Spread's callback (`PlasmaSpread_State0_EnvCollide`, 0x802269fc)
bursts on any non-floor contact. On a floor contact it bursts only when the angle between velocity
and the floor normal reaches its render-state word 1 (135 degrees for kind 7, 150 for kind 8).

The same sweep is City Trial's prop-break dispatch, gated on `CollData.flags` (`+0x34c`) bit 2,
which `mpColl_Create` clears and only machines set. The break force is `CollData.radius` (`+0x344`)
x impact speed squared, and a projectile leaves that radius at zero. A projectile breaks props only
with both `mpColl_SetSceneObjBreak(cd, 1)` (0x80247e58) and a nonzero radius.

## State tables

A state table is an array of 24-byte `WeaponStateEntry` records: a `state_id` (-1 is a sentinel), an
attack word, and four callbacks - `anim_callback`, `phys_callback`, `envcoll_callback` and
`post_envcoll_callback`. All four are per-frame, run at priorities 1, 4, 5 and 6; there is no
on-enter slot. Transitions come from `Weapon_StateChange` calls inside the callbacks, and a one-shot
on-enter belongs in the vtable's `init` or `post_init`.

`Weapon_StateChange(wp, state, anim_frame, anim_rate, flags)` (0x8021f7dc):

1. Picks the entry from `state_table` (`+0x34`), or from `common_state_table` (`+0x30`) when the
   state is below `common_state_num` (`+0x28`). `Weapon_Create` sets `common_state_num` to 0 (at
   0x8021f508) and nothing changes it, so the common table is unused.
2. Writes `wp->state` (`+0x24`) and the `state_id`, copies the four callbacks to
   `wp + 0x150..0x15c`, and clears `wp + 0x160..0x178` (which includes `framestart_callback`). It
   points `wp + 0x38` at the state's 16-byte animation spec, indexed by `state_id`.
3. Binds that spec onto the projectile's model with `Weapon_BindStateAnim` (0x80220b20) and runs
   its script once (`Weapon_RunScript`, 0x802211cc). It then resets the attack block with
   `Weapon_AssignStateFlags` (0x80222298) and records the attack with `Ply_RecordAttackUsed`.

It does not touch velocity; vanilla throw code writes velocity first, then transitions. A state
animation spec (`WeaponStateAnimSpec`) is `{AnimJoint*, MatAnimJoint*, script*, flags}`. NULL anims
are safe. The script carries the hitbox command (op 0xc, `WeaponScript_CmdHitbox`, 0x80220ca0), and
`Weapon_AnimThink` (0x802208fc) advances both at priority 1. The animation binds joint by joint in
preorder to whatever model the projectile loaded, so a swapped model still receives the kind's
tracks, including a PTCL track on the root.

Pass the entry index to `Weapon_StateChange`, not the `state_id`; two kinds repeat an id.

| Kind | Table | Entries | State ids |
|---:|---|:-:|---|
| 0 SPITSMALL | 0x804b4588 | 1 | 0 |
| 1 SPITLARGE | 0x804b45c8 | 1 | 0 |
| 2 FIRE_BULLET | 0x804b4648 | 3 | 0, 1, -1 |
| 3 FIRE_AURA | 0x804b46b8 | 3 | 0, 1, 2 |
| 4 BOMB | 0x804b4728 | 4 | 0, 1, 2, 3 |
| 5/6 PLASMA_A/B | 0x804b47b0 | 2 | 0, 1 |
| 7/8 PLASMA_SPREAD_MID/SIDE | 0x804b4830 | 1 | 0 |
| 9 PLASMA_C | 0x804b4870 | 2 | 0, 1 |
| 10 PLASMA_D | 0x804b48d0 | 2 | 0, 1 |
| 11 SPITCHARGED | 0x804b4608 | 1 | 0 |
| 12 NEEDLE_AURA | 0x804b4928 | 3 | 0, 1, 2 |
| 13 ICE_AURA | 0x804b4998 | 3 | 0, 1, 2 |
| 14 FIRECRACKER | 0x804b4a08 | 2 | 0, 1 |
| 15 SENSORBOMB | 0x804b4a88 | 5 | 0, 1, 2, 3, 2 |
| 16 GORDO | 0x804b4b50 | 4 | 0, 1, 1, -1 |

The per-kind state index enums (`BombState`, `GordoState` and the rest) are in hoshi's weapon
header. The cases to know:

- **Sensor bomb** index 2 (armed) and index 4 (fade) share `state_id` 2, with different callbacks.
- **Gordo** index 1 scales the model up in flight and moves itself to index 2, which locks the scale
  and starts the despawn timer. Index 3 is the sentinel: `Weapon_StateChange` zeroes `wp + 0x38`
  there but still installs its callbacks, so its `anim_callback` runs once to spawn the despawn
  burst.
- **Held states** do nothing but snap the projectile to the rider's hand bone from
  `post_envcoll_callback`. A bomb, sensor bomb or aura left in state 0 with no rider sits at a
  garbage position and never detonates.
- **State 1 of plasma A/B/C and firecracker** is a rider-alive watchdog reading `wp + 0x1b8` /
  `0x1bc`, not an active phase.

**The attack word.** An entry's second word is the state's attack word. `Weapon_AssignStateFlags`
resets the attack block at `wp + 0x17c` with it: the word, the hit counter (`+0x184`) and the victim
mask (`+0x18a`). When a hit lands, `Machine_StoreAttacker` (0x80231d90) copies the word to the
victim's `DmgLog.credited_attack` (`+0x4`), next to `attacker_ply` (`+0x1c`). The word then names what
hit the victim until the next credited hit.

- The low byte is the attack cause, an index into the per-player stat arrays. Causes 1-0x1a are
  bounds-checked by `Machine_StoreAttacker` and `Ply_RecordEnemyDefeat`; 0 or 0x1b and up credit only
  with bit 0x8000 set. `Ply_RecordAttackUsed` (0x80231bdc) indexes `PlayerStats + 0x16c + cause * 4`
  with no bound, so a cause from 0x29 up overwrites the per-actor defeat counters. `Ply_AddDeath`
  compares the cause against 0x11 / 0x12 / 0x13 (Firework, Sensor Bomb, Gold Spike KOs). Plasma
  Spread's word is 0x109, cause 9.
- Bit 0x100 (`WP_ATTACK_ACTIVE`) is set on every active vanilla state. Bit 0x1000 makes
  `Ply_RecordAttackUsed` set `PlayerStats + 0x330`.

`wp + 0x194` is the attack instance id. `AllocSeqId16` (0x80231b68, a `u16` counter at 0x805dd8a0
that wraps to 1) mints a new one when the cause is 0 or changes. `Machine_StoreAttacker` copies it to
`DmgLog + 0x1a`, which is how one attack is credited to a victim once.

## Per-frame procs

`Weapon_Create` registers ten procs, all at create time:

| Prio | Proc | Does |
|---:|---|---|
| 0 | `Weapon_Proc0_FrameStart` (0x8021f9b4) | Bumps the frame counter, zeroes `accel`, resets the HurtData frame, ticks the intangibility timer, then calls `framestart_callback` (`+0x160`) |
| 1 | `Weapon_Proc1_RunStateFn0` (0x8021fa18) | Ticks the HurtData timers, runs `Weapon_AnimThink` and `anim_callback`, then counts `lifetime` (`+0x10c`) down and despawns at zero |
| 4 | `Weapon_Proc4_Physics` (0x8021faa4) | `phys_callback`, then `vel += accel` and `pos += vel` |
| 5 | `Weapon_Proc5_RunStateFn2` (0x8021fb44) | Clears the contact flag, calls `envcoll_callback` |
| 6 | `Weapon_Proc6_RunStateFn3` (0x8021fb88) | `post_envcoll_callback`, then `Weapon_SyncRootMtx` (0x80220310) rebuilds the root matrix at `scale * params.model_scale` |
| 7 | `Weapon_Proc7_PostState` (0x8021fbec) | `trigger_callback` (`+0x164`); destroys the projectile below a void height, else sizes and places the HurtData from `scale` |
| 8 | `Weapon_Proc8_Stub` (0x8021fc70) | Empty |
| 9 | `Weapon_Proc9_HitColl` (0x8021fc74) | Outbound hit scans |
| 10 | `Weapon_Proc10_HitReact` (0x8021fcd4) | Resolves the strongest logged hit and runs its handler (`on_damage_callback` `+0x16c`, the `+0x170` hook or the vtable's `on_hit`); destroys on nonzero |
| 21 | `Weapon_Proc21_EndOfFrame` (0x8021fed4) | `pos_delta = pos - pos_prev` (`+0xa0`), then `pos_prev = pos` |

## Hit detection

**Outbound.** `Weapon_Proc9_HitColl` walks five lists through `Weapon_CheckRiderCollision`
(0x802215a4), `Weapon_CheckMachineCollision` (0x80221660), `Weapon_CheckWeaponCollision`
(0x8022171c, which skips itself), `Weapon_CheckItemCollision` (0x80221814) and
`Weapon_CheckStageHazardColl` (0x80221878). Each filters by pause and disabled state and calls
`HitColl_CheckCollision` (0x8018d284) with `r3 = victim`, `r4 = attacker`. The rider scan excludes
the projectile's own player through `HitColl_CheckIfSamePlayer` (0x8000b024).

**Inbound.** Victims also scan the projectile list: `Rider_CheckWeaponHit` (0x801963c8),
`Machine_CheckWeaponCollision` (0x801d7118), `Box_CheckWeaponCollision` (0x80252334) and the
enemy-side check at 0x802020d4. The rider and machine scans skip a projectile whose owner
(`WeaponGObj_GetOwnerGObj`, 0x8022312c) is the victim. They only compare the owner pointer, never
dereference it. `Box_CheckWeaponCollision` does no owner check, so boxes take hits from any
projectile.

**Self-hit flags.** Owner exclusion is gated by separate bits on the two paths, and either path can
resolve a hit first:

| Path | Flag | Set by vanilla |
|---|---|---|
| Inbound | `WP_ALLOW_SELF_HIT_INBOUND`, `flag_a` (`+0x1b4`) bit 0 | Only sensor bomb's `post_init` |
| Outbound | `WP_ALLOW_SELF_HIT_OUTBOUND`, `flag_b` (`+0x1b5`) bit 5 (0x20) | Never |

The outbound test at 0x802215e8 is `rlwinm. r0,r0,27,31,31`, bit 5, not bit 4. A projectile that
must hit its own owner, such as a trap spawned with the trapped player as owner, sets both bits.

**Damage.** `Weapon_InitHurtData` (0x80221440) calls `HurtData_Create(gobj, 5, 2, count, 0)`, so
every projectile has exactly two attack regions (`hurt + 0x0c`, stride 0xc8). The current state's
animation script drives their size and damage, so a projectile in its real flying state with valid
kind data deals vanilla damage with no extra setup. `HurtData_Create` stores the projectile's own
GObj as the attacker at `hurt + 0x04`, not the owner, so an ownerless projectile still logs damage.
To change damage, either patch `params.base_damage` (`+0x04`) and `params.base_knockback` (`+0x24`)
across both regions after `Weapon_InitHurtData`, or hook `HitColl_SetDamageLog` (0x8018cf94) and
filter by attacker.

## Lifecycle

**Creation.** `Weapon_Create` (0x8021f428):

1. Creates the GObj (class 23, p_link 14) with its render GX link, allocates and zeroes the
   `WeaponData`, and attaches it with `Weapon_UserDataDtor` (0x8021ff54) as destructor.
2. Copies the descriptor (kind, owner, position, forward/up, velocity, type flag, charge) and
   builds the orientation matrix.
3. Allocates the render-state block (`Weapon_AllocRenderState`, 0x802205b0), the two effect groups
   (`+0x114`, `+0x118`), the collider if the kind has one, the animation object and the HurtData.
   The model comes from `JObj_LoadJoint` (0x8040afe8) on the kind's model, or a global default.
4. Runs `init`, registers the ten procs, runs `Weapon_InitRuntimeState` (0x8021f2a0), which zeroes
   accel and velocity, seeds `scale` and `lifetime` and enters state 0, then runs `post_init`.

It touches no rider state, so it can be called from anywhere. What is unsafe is leaving a kind whose
state 0 is "held in a hand" in that state.

**Destruction.** Lifetime expiry runs `Weapon_Despawn` (0x80220364) from priority 1, which calls the
kind's `despawn` or `GObj_Destroy`. Falling below the void height destroys the projectile directly
from priority 7. Both end in `Weapon_UserDataDtor`. It destroys the HurtData, stops and frees both
effect groups, runs `aux_a`, frees the render state, animation object and collider, and returns the
`WeaponData` to its pool.

**Auras.** The aura kinds (3, 12, 13) spawn with zero velocity, and the ability stores the GObj at
`RiderData.ability_gobj` (`+0x3f0`): `Fire_AbilityInit` (0x801aed50), `Needle_AbilityInit`
(0x801b385c), `Ice_AbilityInit` (0x801b4718). The matching lose-ability exits destroy it:
`Fire_LoseAbility_Exit` (0x801af330), `Needle_LoseAbility_Exit` (0x801b3d18) and
`Ice_LoseAbility_Exit` (0x801b49d4). On a wholesale rider reset they use `GObj_Destroy`, which skips
`aux_a`. Otherwise they use `WeaponGObj_Despawn` (0x802230a0), which runs it. Throwable kinds keep
their handle in the ability's own state instead.

**Per-kind scratch.** From `wp + 0x1b8` to about `+0x1fc` is per-kind scratch. Kinds reuse the same
offsets across states, and handles parked there are particle-effect handles. Treat the range as
opaque unless you are the kind's own state code.

## Spawning

The rider-side helpers build a `WeaponDesc` and call `Weapon_Create`. By default they take the
position from the rider's hand bone and the velocity from the machine's plus the rider's own, and
they assert that the copy-ability hat is present. The ones that differ:

| Helper | Kind | Differs by |
|---|---:|---|
| `Rider_SpawnFireBullet` (0x801a8f68) | 2 | Position and basis from the caller; velocity rotated by an angle; no hat assert |
| `Rider_SpawnFireAura` (0x801a9178) | 3 | Hand-bone position, zero velocity, no hat assert |
| `Rider_SpawnNeedleAura` (0x801a9a54) | 12 | As the fire aura |
| `Rider_SpawnIceAura` (0x801a9b84) | 13 | As the fire aura |
| `Rider_SpawnCrackerBullet` (0x801a9cb4) | 14 | Position and basis from the caller |

`Rider_SpawnStarBullet` (0x801a8c80), `Rider_SpawnStarBulletCharged` (0x801a8df8), `Rider_SpawnBomb`
(0x801a9410), `Rider_SpawnPlasmaBullet` (0x801a95a0), `Rider_SpawnPlasmaSpread` (0x801a9870),
`Rider_SpawnSensorBomb` (0x801a9e78) and `Rider_SpawnGordo` (0x801aa028) follow the default.

**A custom spawn** can put a thrown projectile in front of a player with no ability active. Build
the descriptor from the player's machine (`md->pos`, `forward`, `up`, `velocity`), take the owner
from the rider GObj, then:

- **Seed `wp->vel` yourself.** `Weapon_Create` copies `desc.vel` into a spawn snapshot at
  `wp + 0x88`, while physics reads `wp + 0x94`. Write it before the state change, as vanilla does.
- **Add a forward kick to the machine's velocity.** The machine's velocity alone leaves the
  projectile co-moving with Kirby, inside his model.
- **Enter the flying state.** Use `Weapon_StateChange(wp, 1, 1.0, 1.0, 1)` for bomb and sensor bomb.
  `flags = 1` skips the rider-attached cleanup, as a vanilla throw does. The spit stars and Plasma
  Spread have one state and fly from creation.
- **Gordo needs its own transition.** `WeaponGObj_EnterGordoThrownState(gobj, vel, pos)`
  (0x8022a544) seeds the spin, the acceleration impulse (`wp + 0x7c`) and the lifetime that its
  flying callbacks read. A plain state change leaves a Gordo that does not spin, has no impulse and
  renders degenerate. It reads the owner's rider basis through `RiderGObj_GetForward` (0x80191ef8)
  and `RiderGObj_GetUp` (0x80191f18), so the owner must be a real rider. It also enforces the
  per-stage Gordo cap (`Gordo_EnforceMaxActive`, 0x8022b45c).

The throw wrappers do not fit a custom path. `WeaponGObj_TryThrowBomb` (0x801a9580) reads a
hand-bone matrix that exists only while a rider holds the bomb. `WeaponGObj_TryThrowSensorBomb`
(0x801a9fe8) silently does nothing without the ready flag `post_init` sets.
`WeaponGObj_IsGordoThrowable` (0x801aa008) is a predicate, not a throw.

**Ownerless spawns.** A projectile from the world, a stage hazard for instance, can pass NULL owners.
`HitColl_CheckIfSamePlayer` NULL-checks first and answers "not the same player", so a NULL owner
threatens everyone, and the inbound scans and damage logging never dereference it. Kinds whose code
reads the owner through `RiderGObj_GetHandBonePos` (0x80191ffc), `RiderGObj_GetUp` or
`RiderGObj_GetForward` crash on a NULL owner, because those dereference it unguarded:

| Kind | Ownerless | Why |
|---|---|---|
| 7/8 Plasma Spread | Yes | Single state with empty callbacks; the best default |
| 5/6 Plasma A/B | Yes | Lifetime is only 6 / 9 frames; override it |
| 11 Spit Charged | Yes | `post_init` sets velocity to `forward * 3.465` |
| 14 Firecracker | Yes | Keeps `desc.vel`; bursts on any surface or on its own fuse |
| 4 Bomb, 15 Sensor Bomb | Yes, if moved to state 1 before any proc runs | State 0 snaps to the hand bone |
| 2 Fire Bullet | Only with a live rider as owner during create | `FireBullet_Init` (0x80224cc8) reads the owner; clearing `owner_gobj` afterwards is safe |
| 0/1 Spit, 9/10 Plasma C/D | No | `init` reads the owner, and their homing rewrites velocity every frame |
| 3/12/13 Auras | No | Every state snaps to the owner's hand; no aura can fly |
| 16 Gordo | No | The thrown transition reads the owner |

**Gravity.** Nothing applies gravity. Priority 0 zeroes `accel` and priority 4 integrates it, so a
one-shot write does not survive. Write the acceleration every frame from `framestart_callback`,
which runs right after the zeroing. Set it after any `Weapon_StateChange`, which clears it. The
flying `phys_callback`s of bomb, firecracker, sensor bomb and fire bullet add a stage air current
(0x800ceb18) to accel rather than overwriting it. Plasma and the spit stars are purely ballistic.

**Lifetime.** `Weapon_InitRuntimeState` seeds `lifetime` from `params.lifetime`, and 0 means
infinite:

| Kind | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|
| Frames | 240 | 480 | 120 | 0 | 0 | 6 | 9 | 120 | 120 | 200 | 360 | 90 | 0 | 0 | 240 | 0 | 540 |

Overwrite `wp->lifetime` after create and after any state change; nothing else reads it.
Firecracker also burns an independent fuse at `wp + 0x1b8` / `0x1bc` and bursts when it runs out.

**Owner-derived scratch.** `FireBullet_Init` caches the owner's Fire charge as `wp + 0x1b8`
(charge / max) and `wp + 0x1bc` (charge). On impact, `FireBullet_ApplyChargeScale` (0x80224ef8)
uses them as the burst's `scale` and as a knockback multiplier. An owner with no charged Fire ability
gives a zero-size, zero-knockback burst, and the effect system warns that the effect scale is zero.
A custom spawner seeds both words itself (1.0 is full charge).

## Hook points

| Need | Hook | Notes |
|---|---|---|
| Any spawn | `Weapon_Create` (0x8021f428) | `r3 = desc` at entry; the GObj in `r3` at return |
| Any despawn | `Weapon_UserDataDtor` (0x8021ff54) | Catches lifetime expiry and the void |
| Lifetime despawn | `Weapon_Despawn` (0x80220364) | `r3 = wp` |
| Aura teardown | `WeaponGObj_Despawn` (0x802230a0) | Vanilla calls it only from the three aura lose-ability exits |
| Projectile-side hits | `Weapon_Proc9_HitColl` (0x8021fc74) | The whole outbound scan |
| Hit logging | `HitColl_SetDamageLog` (0x8018cf94) | Shared by every damage source; filter by attacker |
| State changes | `Weapon_StateChange` (0x8021f7dc) | `r3 = wp`, `r4 = state` |
| Spawn damage | Exit of `Weapon_InitHurtData` (0x80221440) | Patch the two regions on `wp->hurt_data` |
