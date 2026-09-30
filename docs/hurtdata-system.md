# HurtData And Hit Collision System

The hurt/damage system handles all entity-vs-entity collision damage in Kirby Air Ride -
machine-vs-machine, machine-vs-enemy, machine-vs-item, projectile-vs-anything. It owns the full
pipeline from collision detection through damage calculation, knockback resolution, and hit reaction.
Entity-vs-environment collision (ground, walls, raycasts) is a separate system that shares nothing
with this one.

The structs (`HurtData`, `HurtDesc`, `HurtParams`, `HitCollData`) are declared in
`externals/hoshi/include/hurt.h`; `TriggerData` is in `trigger.h`.

## Inbound Versus Outbound

Every entity runs collision checks each frame, but the checks are one-directional: a per-frame
`*_UpdateHitColl` / `*_ProcHitColl` pass treats **its own entity as the victim** and walks other
entities' lists to find attackers whose regions overlap it. `HitColl_CheckCollision` (`0x8018d284`)
takes `r3 = victim`, `r4 = attacker`, and iterates the *attacker's* attack regions against the
*victim's* defensive sub-regions.

Consequences that matter when hooking:

- `EventActor_ProcHitColl` (priority 9, `0x801fc8ec`) is **inbound only**. It is the enemy-as-victim
  pass; it never delivers the enemy's own attack.
- An enemy's **outbound** attack on a rider is delivered **machine-side**.
  `Machine_CheckEventCollision` (`0x801d71ec`) walks the p_link-12 event-actor list, fetches each
  enemy's attack HurtData at `ed+0x410` via `EventActorGObj_GetHurtData` (`0x80204878`), and calls
  `HitColl_CheckCollision` with the machine's HurtData (`MachineData+0x660`) as victim.
- There is **no `Rider_CheckEventCollision`**. `Rider_UpdateHitColl` (`0x8018f95c`) has no
  event-actor sub-check and never calls `EventActorGObj_GetHurtData`. **Riders take enemy contact
  damage through their machine, not directly.**

## Where HurtData Lives

Every damageable object owns one `HurtData`, embedded in the owning entity's data struct. It carries
two arrays: **regions** (stride 0xC8) are attack hitboxes with embedded damage/knockback parameters,
and **sub-regions** (stride 0x44) are defensive hurtboxes built from model joints.

| Entity | Offset | Attack regions | Defensive sub-regions |
|--------|--------|----------------|-----------------------|
| MachineData | +0x660 | 4 | per model joints |
| RiderData | +0x390 | 4 | per model joints |
| EnemyData | +0x410 | 2 (Dyna Blade = 8) | per model joints |
| GrYakuData (stage hazards) | +0xEC | 2 | per model joints |
| ItemData (city items) | +0x148 | varies | varies |
| WeaponData | +0x108 | 2 (hardcoded) | per model joints |

The two output fields the rest of the game reads are `kb_mag` (+0x24) - non-zero is what triggers a
hit reaction - and `dmg_taken` (+0x28), the frame's accumulated damage.

### Attack region entry (stride 0xC8)

Regions have no struct in the headers. A region begins with an `active` word at +0x00, followed by
the **13-dword HurtParams block copied to +0x04..0x34** by `Trigger_InitParameters` (`0x8018a118`),
which also stores its `flags` argument at +0x38. HurtParams field X therefore lands at region offset
`X + 0x04`.

| Offset | Field | Notes |
|--------|-------|-------|
| 0x00 | `active` | 0 = skip; checked first in `HitColl_CheckCollision` |
| 0x04 | `base_damage` | int; `HitColl_GetDamageDealt` reads it and converts to float |
| 0x08 | `dmg_distance_factor` | damage scales by relative velocity; 0 = fixed |
| 0x20 | (HurtParams x1c) | scale / magnitude factor |
| 0x24 | `base_knockback` | read by `HitColl_CalcKnockback` |
| 0x28 | `kb_distance_factor` | knockback scales by relative velocity; 0 = fixed |
| 0x30 | `hit_flags` | read as a word `w`. `(byte +0x30) >> 3` = hurt type (8 = skip); the victim's hit reaction and `HurtData.attacker_flags` take it. `(w >> 10) & 0x7f` = victim mask, tested against `1 << victim HurtData.kind`. `((u16 +0x32) >> 2) & 0xff` = rehit interval in frames |
| 0x31 | filter byte | bit 1 admits victims of vulnerability class 0, bit 2 class 1; class 2 always passes |
| 0x33 | disable byte | bit 0 skips the region (`lbz 0x33; clrlwi.,31`) |
| 0x38 | `flags` | `Trigger_InitParameters` 3rd arg; `EnemyKnockback_Default` reads it as the hurt-entry type |
| 0x40 | `pos_cur` | Vec3, fallback velocity source |
| 0x4C | `radius` | collision sphere radius |
| 0x50 | `pos_prev` | Vec3, fallback velocity source |
| 0x68 | victim list | 12 `{victim HurtData *, frames}` pairs; next slot to overwrite at +0x3c |

A region hits a given victim once per rehit interval. `HitColl_SetDamageLog` records the victim in
every attacker region of the same group through `HitColl_ClearLogEntry` (`0x80189e3c`), which despite
its name stores `{victim, rehit frames}`. It does so before its knockback and vulnerability tests, so
a hit on an invulnerable victim still spends the interval. `HitColl_CheckCollision` skips a victim
that `Hit_IsVictimRecorded` (`0x8018a408`) finds listed, `Hit_TickVictimTimers` (`0x80189fd4`)
counts the timers down, and `Hit_ResetVictimList` (`0x80189d34`) clears the list when a region is
re-armed.

The victim mask is by `HurtData.kind`: 0 rider, 1 ridden machine, 2 empty machine, 3 event actor
(enemies, Dyna Blade), 4 item, 5 projectile, 6 stage object. A machine's HurtData is created as kind
2; mounting switches it to 1 (`0x801c82f8`) and `setEmptyVehicleHitbox` (`0x801c8384`) switches it
back.

Everything from +0x04 to +0x34 is the HurtParams copy, so no independent geometry or joint index
lives in that band. Per-frame velocity normally comes from the **`HurtData.pos_tracker`** object
(HurtData+0x68): `HurtData_UpdatePerFrame` (`0x8018c4e8`) writes the new contact position to
`pos_tracker+0x00` and rolls the previous into `pos_tracker+0x0C`. The region's own +0x40/+0x50 pair
is only the no-tracker fallback.

Defensive sub-regions (stride 0x44) are built by `HurtData_InitRegion` (`0x8018c598`) /
`HurtDesc_SetupRegion` from model joint data - a joint index, an offset and a radius scale per
`HurtDesc`.

## GObj p_link Classes

The collision iterators walk GObj linked lists by p_link class. `stc_gobj_lookup` (`0x805de334`,
`r13+0x1254`) is the list-head array; **a class's head is at `stc_gobj_lookup + p_link*4`**, and the
walk follows the GObj `p_link` next pointer at GObj+0x08.

| p_link | Entity type | GetHurtData function |
|--------|-------------|----------------------|
| 8 | Stage hazards (GrYaku) | `GrYaku_GetHurtData` (`0x800f8248`) - GObj userdata then +0xEC |
| 9 | Machines | `MachineGObj_GetHurtData` (`0x801c8660`) - MachineData+0x660 |
| 10 | Riders | `RiderGObj_GetHurtData` (`0x80192788`) - RiderData+0x390 |
| 12 | Event actors / enemies | `EventActorGObj_GetHurtData` (`0x80204878`) - EnemyData+0x410 |
| 14 | Projectiles | accessor at `0x80223120` - WeaponData+0x108 |

## Damage Pipeline

### Machine side

`Machine_UpdateHitColl` (`0x801c67a0`) runs once per machine per frame:

1. `HitColl_Init(md->hurt_data)` (`0x8018cf64`) - clears `stc_hitcolldata.coll_num` and points
   `stc_hitcolldata.hurt_data` at this victim.
2. Eight collision sub-checks, each walking one list and calling `HitColl_SetDamageLog` on overlap:
   `Machine_CheckRiderCollision` (`0x801d6fd0`, p_link 10), `Machine_CheckMachineCollision`
   (`0x801d706c`, p_link 9), `Machine_CheckWeaponCollision` (`0x801d7118`, p_link 14),
   **`Machine_CheckEventCollision` (`0x801d71ec`, p_link 12 - where all enemy damage enters)**,
   `Machine_CheckItemCollision` (`0x801d7248`), `Machine_CheckStageHazardCollision` (`0x801d72a4`,
   p_link 8), `Machine_CheckMachineBumpCollision` (`0x801daac4`), `Machine_CheckPatchColl`
   (`0x801dba74`).
3. `HitColl_ActOnCollision(md->hurt_data)` (`0x8018d878`) - scans the log for the strongest
   knockback and writes it into `kb_mag`, plus contact position and attacker info.
4. `Machine_ActOnHitCollision(md)` (`0x801d7308`) - if `kb_mag != 0`, identify the attacker, record
   "hit by" tracking, and call `Machine_EnterHitReaction`.

### Enemy side

`EventActor_ProcHitColl` (`0x801fc8ec`) is the priority-9 GObj proc, inbound only. (Priority 8,
`EventActor_ProcHitCollInit` at `0x801fc8e8`, is a no-op `blr` stub.) Per frame, per enemy actor:

1. Gating: the pass runs only while `damage_accum_1` (EnemyData+0x994) is below the HP threshold
   param at +0x3B0, so an actor stops taking hits once its accumulated damage reaches it. Also a
   float gate at EnemyData+0x364, a flag/state check at EnemyData+0xB08, and a type test that makes
   the special actors 0x48-0x4A (special Broom Hatter, Sword Knight, Waddle Dee Truck) skip entirely.
2. `HitColl_Init(ed->hurtdata)` - EnemyData+0x410 is the **victim**.
3. Five sub-checks, each opening with `HitColl_SetUnk(0)` (`0x8018cf84`) and then walking one p_link
   list into `HitColl_CheckCollision(ed+0x410, attacker_hurtdata)`: riders `0x8020200c`, machines
   `0x80202070`, projectiles `0x802020d4`, other actors `0x80202130` (skips self), stage hazards
   `0x80202198`.
4. `HitColl_ActOnCollision(ed->hurtdata)`.
5. Post-collision handler `0x802021fc` - if damage was received, sets the damage-pending slot
   (EnemyData+0xA20/+0xA24) that feeds `EventActor_ProcDamage` at priority 10.

### HitColl_CheckCollision (0x8018d284)

Called with two HurtData objects: **r3 = victim** (its sub-regions are iterated, and its `kind` at
+0x00 is what the victim mask is tested against), **r4 = attacker** (`region_count` from
attacker+0x08 and `regions` from attacker+0x0C).

1. Iterate the attacker's regions (stride 0xC8). For each active region, check the victim mask
   against `1 << victim.kind`, the filter byte against the victim's vulnerability class, and the
   region's victim list.
2. Iterate the victim's sub-regions (stride 0x44) and run `Hit_CheckOverlap`, a sphere-vs-sphere
   test, for each pair.
3. On overlap, call `HitColl_SetDamageLog` with the attacker's **region entry** as the damage source.

### HitColl_SetDamageLog (0x8018cf94)

The damage calculator, called on every overlap.

**Damage** comes from `HitColl_GetDamageDealt` (`0x8018ace4`). It short-circuits when
`dmg_distance_factor` (region+0x08) is exactly 0 and returns `base_damage` unclamped; otherwise
`damage = base_damage + dmg_distance_factor * relative_velocity_magnitude`, clamped to a **minimum of
1.0** (SDA2 constant at `-22284(r2)`, *not* 0). Only the velocity-scaled path applies that floor. The
relative-velocity magnitude is `|pos_cur - pos_prev|`, taken from the attacker's `pos_tracker` when
present, else from region+0x40/+0x50.

**Knockback** comes from `HitColl_CalcKnockback` (`0x8018ab90`), with the same shape:
`kb_distance_factor == 0` gives plain `base_knockback` with no velocity term and no clamp.

Both results are multiplied by `1 - dmg_multiplier` of the victim (HurtData+0x80), so the field is a
damage-reduction fraction. For machines it is MachineData+0x4EC, the vcAttributes `base_defense`
(+0x8c) after the stat scaling: 0.2 on Warp Star, 0.5 on Hydra. The hit is then **skipped and not
logged** if any of: the scaled knockback is at or below the SDA2 threshold `FLOAT_805e1040`
(approximately 0), `HurtData_CheckVulnerability` (`0x8018cd9c`) returns non-zero (intangible or
invulnerable victim), or the entry already has a pending hit.

A logged hit is appended to `stc_hitcolldata.log` (max 20 entries; asserts on overflow) with the
attacker hurt struct, the trigger/region params, the per-region log object, the attacker position and
the computed knockback, and it accumulates `dmg_taken` and bumps `max_single_hit`.

The `on_damage_callback` at HurtData+0x8C, if set, is called **unconditionally at the end** with a
"was it logged?" flag - including when the hit was skipped. For machines that is
`Machine_OnDamageCallback`; for enemies `EventActor_OnDamageCallback` (`0x80201c78`).

### HitColl_ActOnCollision (0x8018d878)

After all sub-checks, iterates the log for the **maximum** knockback and stores the winning entry's
data into the victim's HurtData (`kb_mag`, `hitcoll_log_idx`, collision position, attacker flags).
With no entries, or max knockback at or below 0, `kb_mag` stays 0 and no hit reaction fires.

### Machine_ActOnHitCollision (0x801d7308)

If `kb_mag != 0`, a switch on the attacker's `HurtKind` decides the bookkeeping before
`Machine_EnterHitReaction` puts the machine into state 5 (bounce):

| Attacker kind | Source | Special cases |
|---------------|--------|---------------|
| 0 | Rider | records attacker player index |
| 1 | Ridden machine | records the rider's player index; attack word from `MachineGObj_GetAttackerLog` |
| 2, 4 | Empty machine, item | no attacker recorded |
| 3 | Event actor | Dyna Blade (0x4D) hitting with its region 1 or 2 sets the victim's "trampled by Dyna Blade" bit |
| 5 | Projectile | records the owner's player index (`Weapon_GetOwnerPly`, `0x802230c4`); the attack block is `proj+0x17c` (`Weapon_GetAttackerLog`, `0x80223178`), the kind's state flags, so it names the kind, not the projectile |
| 6 | Stage object | special-cases yakumono descs 0x3D, 0x41 |

The attacker's player is credited through `Machine_StoreAttacker` (`0x80231d90`), which keys on the
low byte of the attack word: a cause of 1-0x1A always counts, any other only while bit 0x8000 is set.

## Damage Application

`Machine_GiveDamage(md, float damage, int *hit)` (`0x801e1ee8`) - `damage` arrives in `f1`. It
adds `damage` to `MachineData.dmg_accumulator` (+0x6AC) clamped to a max constant, calls
`Gm_IsDamageEnabled` (`0x8000a188`) and subtracts from `MachineData.hp` (+0xA18) only when damage is
enabled, enters the death sequence at minimum HP and applies the low-HP warning color animation.

It then calls `Machine_DropPatchesOnDamage` (`0x801e09ac`), which is where a heavy hit knocks
patches out. In the city (`Gm_IsInCity`) with a rider aboard (`MachineData.rider_gobj`), that call
passes the machine's master stats (+0x94c) and the damage, truncated to an int, to
`RiderGObj_DropPatchesOnDamage` (`0x80192980`) -> `Rider_DropPatchesOnDamage` (`0x8019cdfc`). That
runs a mode-0 `Rider_DropPatches` when the damage exceeds `RiderCommonParam.patch_drop_damage_min`
(+0x1b8 of the rider tuning block `RdCommon.dat` loads, 8.0). The on-foot damage path (`0x801a06f0`) reaches the
same function with the rider's own stats.

`hit` is the machine's hit record, `&md->hurt_data->hitcoll_log_idx`, which is what both
`Machine_DmgApply` call sites pass. It is **not** a GObj. The drop call reads it for a deflect
direction: `Hit_CalcDeflectDir` (`0x80194ca4`) takes the record's `knockback_dir` (+0x20 from the
record, `HurtData+0x3c`) and mirrors it off the ground plane. `Rider_DropPatchesOnDamage` then
ignores both that direction and the hand position it is also handed, so the contents never
matter. The pointer is still dereferenced in the city with a rider aboard, so it **must not be
NULL**.

`Machine_GiveDamage` does **not** itself cause knockback or bounce.

`Machine_EnterHitReaction(md)` (`0x801e05bc`) does: it saves the previous state, clears formation
tracking, calls `HurtData_UpdateVulnState` on the machine's HurtData, and registers the hit reaction.

`Machine_DmgApply` (`0x801c6834`) is where a logged hit lands: when `kb_mag != 0` it passes
`dmg_taken` to `Machine_GiveDamage`, then `Machine_DispatchHitReaction` (`0x801e2620`). That calls
`0x801e2324` (mapped as `Machine_EnterDeath?`, but it sets up the reaction, not a death). It stores the
attacker's hurt type at +0x1ba4 and a duration from `kb_mag`, with a 0-2 strength tier at +0x1bb0.
The dispatcher then enters one of sub-states 4-11 through the table at `0x804b0e70`, indexed by
hurt type. None of the reaction actions carries a hitbox.

## Machine Hit Regions

| Region | Role | Live while |
|--------|------|------------|
| 0 | spins | an action script arms it: quick spin (sub-state 1, actions 0x41 / 0x42) and the plain forced spin (sub-state 2, actions 0x43 / 0x45), each for about 44 frames |
| 1 | boost | `VcCommon x0+0x130` = 2 frames from the first frame of each charge boost (`MachineData.xc34` bit 0x02) |
| 2 | Candy | `xc36` bit 0x40 is set (`Machine_StartCandy`) and a rider is aboard; no speed or state test |
| 3 | ram | a rider is aboard, `xc39` bits 4-5 are clear, and `\|world_velocity\| >= VcCommon x0+0xe4` |

**The switch.** Regions 1-3 are armed and updated only while `Gm_IsMachineHitboxEnabled`
(`0x8000a200`, GameData+0xaa5 bit 0x01) returns 1.
- `CityTrial_Init` sets it.
- `Stadium_ApplyDescConfig` copies it from byte 4 bit 0x20 of the stadium's 6-byte entry at
  `gmDataAll+8` in GmData.dat. The bit is clear for all 24 StadiumKinds.
- Every exit of `MinorExit_AirRideMachineSelect` clears it.

So ram, boost and Candy damage exist only in the city. Region 0 is not switched, so the spins hit
in every mode.

**Ram (region 3).** `Machine_UpdateMovingHitbox` (`0x801d7604`) runs each frame.
- It first sets the attack word's bit 0x8000.
- If Candy or the boost region is live, that region takes the frame and the ram region stays off.
- With no rider, or with `xc39` bits 4-5 set, it clears bit 0x8000 and turns the region off.
  `Machine_EnterCharge`, the grounded hit reaction and other states set those bits;
  `MachineStateChange` clears them.
- Otherwise, while the measured speed is at least 0.6075 per frame (about 9.3 mph), it arms the
  region: a sphere of radius vcAttributes +0x70, centered +0x74 ahead along `world_velocity`. Only the
  front of the machine hits.
- Side and rear contact go through `Machine_CheckMachineBumpCollision` (`0x801daac4`), which pushes
  the machines apart with knockback and a hit reaction but never calls `Machine_GiveDamage`.

**Regions 1-3 share one damage shape.** Each sets per frame:
- `base_damage = (int)(MachineData+0x46c * damage attr)`
- `dmg_distance_factor = 24.6857 * factor attr`, so damage grows with relative speed
- knockback of the same form

MachineData+0x46c is vcAttributes +0x0c after the Weight, Glide and Defense scaling. The Offense stat
scales the ram sphere (+0x70). The attribute offsets:

| Region | Size | Offset | Damage | Damage factor | Knockback | Knockback factor |
|--------|------|--------|--------|---------------|-----------|------------------|
| 1 boost | +0x11c | +0x120 along facing | +0x124 | +0x128 | +0x12c | +0x130 |
| 2 Candy | +0x108 | centered | +0x10c | +0x110 | +0x114 | +0x118 |
| 3 ram | +0x70 | +0x74 along velocity | +0x78 | +0x7c | +0x80 | +0x84 |

A Warp Star ram deals `20 + 12.3 * |dv|` damage and `20 + 24.7 * |dv|` knockback before the
victim's reduction. On most machines the boost region's damage factor is twice the ram's.

The HurtParams templates for regions 3, 2 and 1 sit at `vcDataCommon` +0x24 / +0x28 / +0x2c in
VcCommon.dat. All three carry the hit word 0x0017ec78:
- hurt type 0;
- victim mask 0x7b, every kind but an empty machine;
- a 30-frame rehit.

Candy (`Machine_GiveCandy`) also makes the machine invincible through `HurtData_SetCandyInvincible`.

**Credit.** While the updater holds bit 0x8000, a ram, boost or Candy hit credits the rider with
cause 0. That counts for rivals through `Machine_StoreAttacker` and for event actors through
`Ply_RecordEnemyDefeat`. Every credited hit on Dyna Blade (actor 0x4D) also sets the rider's
"damaged Dyna Blade" bit (`Ply_SetDamagedDynaBlade`). The spin sub-states' `attack_log` is 0x110, so every spin
hit, spin panels included, is credited as cause 0x10, Quick Spin.

**Kirby's one-frame hitbox.** Kirby's rider states 30 (`AS_RaceStartGo`) and 40
(`AS_StarBeginCharge`) play action 100.
- At frame 2 it arms a rider region-0 hitbox for one frame.
- Base damage 2; victim mask 0x78 (event actors, items, projectiles, stage objects).
- The switch does not gate it.

The state's `attack_log` is 0, so `Ply_RecordEnemyDefeat` returns without crediting anyone. An actor
it touches is still knocked out.

## Vulnerability

`HurtData.vuln.kind` has three states: 0 vulnerable, 1 invincible (no damage or knockback, but
collision is still detected), 2 intangible (no collision at all - the hurtbox is ghosted).

Two timers drive it, both counting down each frame:

- **Intangibility** (`vuln.intang_timer`, +0x94), set by `HurtData_GiveIntangibility` (`0x8018cb5c`).
  Takes priority over invincibility, sets
  `vuln.kind = 2`, and only ever *raises* the timer - a shorter request is ignored.
- **Invulnerability** (`vuln.invuln_timer`, +0x98), set by `HurtData_GiveInvincibility`
  (`0x8018cc38`). Sets `vuln.kind = 1`, and only takes effect while the intangibility timer is 0.

`HurtData_UpdateVulnState` (`0x8018cb28`) refreshes the state each frame: it clears flag bit 7 at
+0x9C, returns immediately if the intangibility timer is non-zero, and otherwise sets `vuln.kind` to
1 or 0 from the invulnerability timer.

## Enemy Damage Mechanics

### HurtData creation

`EventActor_HurtDataCreate` (`0x80201ee8`) builds the HurtData at EnemyData+0x410 with
`HurtData_Create(gobj, HURTKIND_EVENTACTOR, N, joint_count, 0)` - `N` is 8 attack regions for Dyna Blade
(actor 0x4D) and 2 for every other enemy. It sets `on_damage_callback` to `EventActor_OnDamageCallback` and
walks the actor's joint descriptor to build the defensive sub-regions via `HurtData_InitRegion`.

### Attack hitboxes toggle per animation frame

Enemy attack hitboxes are not statically on:

- **Refresh:** `EventActor_RefreshAttackParams` (`0x80201ba4`) reads the current animation frame's
  hurt descriptor and writes the attack params into the enemy's TriggerData at EnemyData+0x45C.
- **Enable:** activation is **data-driven by the animation frame** - there is no dedicated "activate"
  anim command. When the current frame carries hurt data, `EventActor_RefreshAttackParams` invokes
  `Trigger_SetState1` (`0x8018a0e8`), which sets `region.active = 1` and optionally calls
  `Trigger_InitParameters` to copy the 0x34-byte HurtParams block. `Trigger_SetState1` is the
  de-facto `Hit_SetActive`, mirroring `Hit_SetInactive`.
- **Disable:** anim-script cmd-13, `EnemyAnimCmd_DisableHit` (`0x80201138`), is an explicit per-frame
  OFF switch the script can fire. It calls `Hit_SetInactive` (`0x80189d1c`, `region.active = 0`)
  through the region-index wrapper at `0x8018c7f8`, which computes `regions[idx]` at stride 0xC8 off
  HurtData+0x0C. The region index is `bytecode_operand & 0x03FFFFFF`.
- `EnemyAnimCmd_SetHitDesc` (`0x80200eb8`) separately parses packed binary collision data out of
  animation streams into regions.

**This is why writing enemy attack region parameters from a mod does not work.** The chain
`0x801ff520` -> `EventActor_RefreshAttackParams` -> `Trigger_SetState1` -> `Trigger_InitParameters`
reads `*(*(enemyData+0x14) + 0x14)` (the current animation frame's hurt parameter data) and
overwrites the region's HurtParams every frame. Direct region writes are gone by the next frame; use
a direct damage call instead.

### Receiving damage

`EventActor_ProcDamage` (`0x801fc9f0`) dereferences the enemy GObj's userdata and early-returns for
the special actors 0x48-0x4A (EnemyData+0x0C). Otherwise, reading the HurtData at EnemyData+0x410:

1. Reads `kb_mag` and `dmg_taken`.
2. `kb_mag == 0` is the sentinel: it calls `giveEnemyDamage(dmg_taken)` only when `dmg_taken` is
   non-zero - an accumulator update, no launch.
3. `kb_mag != 0`: clamp `dmg_taken`, call `giveEnemyDamage`, then dispatch knockback through the
   enemy's **custom damage handler at EnemyData+0xAD0** if set, else `EnemyKnockback_Default`
   (`0x8020bcd8`) - a thin dispatcher that reads the hurt-entry type at `region+0x38` and calls
   `Enemy_ApplyKnockback`.
4. Always clears the damage-pending flag at EnemyData+0x9A0.

### Knockback tiers

Enemies have no HP. `Enemy_ApplyKnockback` (`0x8020b784`):

1. Scales the incoming damage (`trigger_params+0x08`) via `Enemy_ScaleDamage` (`0x8020b71c`), then
   classifies it into tier 0-3 via `Enemy_ClassifyDamageTier` (`0x8020b740`). Thresholds come from
   the **global enemy parameter table**, reached through the SDA pointer at `r13+0x798`
   (`0x805DD878`), at table offsets +0x08, +0x0C and +0x10 - damage below the first is tier 0, and so
   on up to tier 3 at or above the third. The tier is stored at EnemyData+0xA1C. The threshold values
   are runtime-loaded data, not constants in the executable.
2. Sets the launch/stun frame count (EnemyData+0xA18) from the tier's base stun `tbl[tier*4 + 0x60]`,
   plus an extra term scaled by `tbl[tier*4 + 0x30]` and a per-enemy launch multiplier
   `*(actor_data->+0x00 + 0xA0)` (where `actor_data` = EnemyData+0x14).
3. Grants intangibility for the launch duration via `HurtData_GiveIntangibility(EnemyData+0x410,
   frames)`.
4. Randomizes the three knockback sign components using `HSD_Randi(8)` (bits 0/1/2 pick +/- per axis).
5. Builds the knockback direction (from attacker position or trigger vectors, mode-dependent), then
   transitions the enemy to its knockback state via `EventActor_DisableRendering` - rendering is off
   during launch.

Death occurs when the launch/stun counter at +0xA18 reaches 0 during the knockback state.

`giveEnemyDamage` (`0x8020b680`) adds damage to two accumulators at EnemyData+0x994 and +0x998,
capped at 9999 each. Neither drives the launch or its death timer, but +0x994 gates hittability:
`EventActor_ProcHitColl` skips its pass once +0x994 reaches the HP threshold at +0x3B0.

## Applying Damage From Custom Code

### Direct, bypassing the collision pipeline

Fetch the machine (`Ply_GetMachineGObj`), call
`Machine_GiveDamage(md, amount, &md->hurt_data->hitcoll_log_idx)`, optionally write
`md->hurt_data->kb_mag` for the physics response, and call `Machine_EnterHitReaction(md)` to enter
the bounce state. Skipping the last two gives HP loss with no visible reaction. An amount over 8 in
the city also knocks patches out of the rider, as a vanilla heavy hit does.

Live consumers: the 1 HP trap in `mods/archipelago/src/ap_item_handler.c` calls
`Machine_GiveDamage` for `md->hp - 1.0f` per human player, so in the city it also drops a patch.
The hail weather effect in `mods/custom_weather/src/hail.c` deals 1 on a cooldown, under the drop
threshold.

### Through the collision pipeline

Zero a `HurtParams` with `Trigger_ClearParameterStruct` (`0x8018a0c0`), fill in `base_damage`,
`dmg_distance_factor` (0 for fixed damage), `base_knockback` and `kb_distance_factor`, then call
`Machine_ApplyHurt(hurt_data, slot_idx, &params)` (`0x8018d1a8`), which runs
`Trigger_InitParameters` followed by `HitColl_SetDamageLog`. This produces a real logged hit, so the
victim's normal `HitColl_ActOnCollision` / `Machine_ActOnHitCollision` resolution runs on it.

## Function Reference

| Function | Address | Description |
|----------|---------|-------------|
| HitColl_Init | 0x8018cf64 | Clears the collision log, sets the victim hurt_data |
| HitColl_SetUnk | 0x8018cf84 | Called with 0 at the head of each enemy inbound sub-check |
| HitColl_CheckCollision | 0x8018d284 | Attacker regions vs victim sub-regions (r3 = victim, r4 = attacker) |
| HitColl_GetDamageDealt | 0x8018ace4 | Damage from region params + relative velocity |
| HitColl_CalcKnockback | 0x8018ab90 | Knockback from region params + relative velocity |
| HitColl_SetDamageLog | 0x8018cf94 | Calculates damage/kb and logs the hit; fires on_damage_callback |
| HitColl_ActOnCollision | 0x8018d878 | Resolves the log to the strongest knockback |
| HitColl_CalcContactPoint | 0x8018a5b8 | Contact point between two collision shapes |
| HitColl_CalcKnockbackDir | 0x8018ab10 | Knockback direction from contact data |
| HitColl_ClearLogEntry | 0x80189e3c | Records a victim in a region's victim list with the rehit timer |
| Hit_IsVictimRecorded | 0x8018a408 | 1 while a victim is in a region's list |
| Hit_ResetVictimList | 0x80189d34 | Clears a region's victim list |
| Hit_TickVictimTimers | 0x80189fd4 | Counts a region's victim timers down, dropping expired pairs |
| HitColl_ResolveLogEntry | 0x8018db10 | Retrieves entry data for `Machine_ActOnHitCollision` |
| Hit_SetInactive | 0x80189d1c | `region.active = 0`; reached via the index wrapper 0x8018c7f8 |
| Trigger_SetState1 | 0x8018a0e8 | `region.active = 1`; de-facto `Hit_SetActive` |
| Trigger_ClearParameterStruct | 0x8018a0c0 | Zeros a HurtParams (0x34 bytes) |
| Trigger_InitParameters | 0x8018a118 | Copies HurtParams into region offsets 0x04-0x34, flags to +0x38 |
| HurtData_Create | 0x8018c1c8 | Allocates a HurtData from the object pools |
| HurtData_InitRegion | 0x8018c598 | Builds a defensive sub-region from joint data |
| HurtData_UpdatePerFrame | 0x8018c4e8 | Per-frame position/radius update and pos_tracker roll |
| HurtData_CheckVulnerability | 0x8018cd9c | Non-zero if the target is protected |
| HurtData_GiveIntangibility | 0x8018cb5c | Sets the intangibility timer |
| HurtData_GiveInvincibility | 0x8018cc38 | Sets the invulnerability timer |
| HurtData_UpdateVulnState | 0x8018cb28 | Refreshes `vuln.kind` from the timers |
| Machine_ApplyHurt | 0x8018d1a8 | Applies hurt from a HurtParams through the log |
| Machine_GiveDamage | 0x801e1ee8 | `(md, damage, hit)`; HP only, no knockback. `hit` is `&hurt_data->hitcoll_log_idx`, non-NULL in the city |
| Machine_DropPatchesOnDamage | 0x801e09ac | In the city with a rider aboard, a hit over 8 damage drops patches (mode 0) |
| Machine_EnterHitReaction | 0x801e05bc | Enters bounce/hit state 5 |
| Machine_UpdateHitColl | 0x801c67a0 | Per-frame pipeline orchestrator (machine side) |
| Machine_CheckEventCollision | 0x801d71ec | Enemy/event-actor check (p_link 12); delivers enemy outbound attacks |
| Machine_CheckStageHazardCollision | 0x801d72a4 | Stage-hazard check (p_link 8) |
| Machine_ActOnHitCollision | 0x801d7308 | Attacker identification + hit reaction dispatch |
| Machine_InitHurtData | 0x801d6e84 | Creates a machine's HurtData |
| Gm_IsMachineHitboxEnabled | 0x8000a200 | GameData+0xaa5 bit 0x01, the switch for machine regions 1-3 |
| Machine_UpdateMovingHitbox | 0x801d7604 | Per-frame driver of regions 1-3; arms the ram region |
| Machine_InitRamHitbox | 0x801d74cc | Arms region 3 from its template |
| Machine_InitCandyHitbox / Machine_UpdateCandyHitbox | 0x801d785c / 0x801d7980 | Region 2 |
| Machine_InitBoostHitbox / Machine_UpdateBoostHitbox | 0x801d7ac8 / 0x801d7bf4 | Region 1 |
| Machine_CheckMachineBumpCollision | 0x801daac4 | Side contact: push, knockback and hit reaction, no damage |
| Machine_DmgApply | 0x801c6834 | Applies a logged hit: damage, then the hit reaction |
| Machine_DispatchHitReaction | 0x801e2620 | Enters the hit-reaction sub-state for the attacker's hurt type |
| MachineGObj_GetAttackerLog | 0x801c8708 | &MachineData.dmg_log, whose first word is the attack word |
| Machine_StoreAttacker | 0x80231d90 | Credits the attacker's player on a machine hit |
| HurtData_SetCandyInvincible / HurtData_ClearCandyInvincible | 0x8018cbc8 / 0x8018cbe8 | Candy invincibility on and off |
| Rider_InitHurtData | 0x80196170 | Creates a rider's HurtData |
| Rider_UpdateHitColl | 0x8018f95c | Per-frame pipeline (rider side); has NO event-actor sub-check |
| RiderGObj_GetHurtData | 0x80192788 | RiderData+0x390 |
| MachineGObj_GetHurtData | 0x801c8660 | MachineData+0x660 |
| EventActorGObj_GetHurtData | 0x80204878 | EnemyData+0x410 |
| GrYaku_GetHurtData | 0x800f8248 | GrYakuData+0xEC |
| EventActor_HurtDataCreate | 0x80201ee8 | Creates enemy HurtData (2 attack regions; 8 for Dyna Blade 0x4D) |
| EventActor_RefreshAttackParams | 0x80201ba4 | Per attack frame: anim descriptor -> TriggerData (EnemyData+0x45C), enables the region |
| EventActor_ProcHitCollInit | 0x801fc8e8 | Priority-8 no-op `blr` stub |
| EventActor_ProcHitColl | 0x801fc8ec | Priority-9 enemy inbound hitcoll (enemy as victim only) |
| EventActor_ProcDamage | 0x801fc9f0 | Turns received damage into knockback |
| EventActor_OnDamageCallback | 0x80201c78 | Enemy on-damage callback (HurtData+0x8C) |
| EnemyKnockback_Default | 0x8020bcd8 | Default knockback dispatcher (region+0x38 type -> `Enemy_ApplyKnockback`) |
| EnemyAnimCmd_DisableHit | 0x80201138 | Anim cmd-13 handler: explicit region OFF |
| EnemyAnimCmd_SetHitDesc | 0x80200eb8 | Parses packed collision data from anim streams into regions |
| Enemy_ClassifyDamageTier | 0x8020b740 | Classifies damage into tier 0-3 |
| Enemy_ApplyKnockback | 0x8020b784 | Full enemy knockback sequence |
| Enemy_ScaleDamage | 0x8020b71c | Scales damage by a global factor from the enemy param table |
| giveEnemyDamage | 0x8020b680 | Adds to the damage accumulators; +0x994 gates hittability |
| Gm_IsDamageEnabled | 0x8000a188 | Gates the HP subtraction in `Machine_GiveDamage` |
| EnemyActor_RumblePlayer | 0x801ff80c | Controller rumble - NOT a damage function, despite sitting next to them |
