# HurtData And Hit Collision System

The hurt/damage system handles all entity-vs-entity collision damage in Kirby Air Ride -
machine-vs-machine, machine-vs-enemy, machine-vs-item, projectile-vs-anything. It owns the full
pipeline from collision detection through damage calculation, knockback resolution, and hit reaction.
(Entity-vs-environment collision - ground, walls, raycasts - is a separate system.)

## Inbound Versus Outbound

Every entity runs collision checks each frame, but the checks are one-directional: a per-frame
`*_UpdateHitColl` / `*_ProcHitColl` pass treats **its own entity as the victim** and walks other
entities' lists to find attackers whose regions overlap it. `HitColl_CheckCollision` (`0x8018d284`)
takes `r3 = victim`, `r4 = attacker`, and iterates the *attacker's* attack regions against the
*victim's* defensive sub-regions.

Consequences that matter when hooking:

- `EventActor_ProcHitColl` (priority 9, `0x801fc8ec`) is **INBOUND only**. It is the enemy-as-victim
  pass; it never delivers the enemy's own attack.
- An enemy's **OUTBOUND** attack on a rider is delivered **machine-side**.
  `Machine_CheckEventCollision` (`0x801d71ec`) walks the p_link-12 event-actor list, fetches each
  enemy's attack HurtData at `ed+0x410` via `EventActorGObj_GetHurtData` (`0x80204878`), and calls
  `HitColl_CheckCollision` with the machine's HurtData (`MachineData+0x660`) as victim.
- There is **no `Rider_CheckEventCollision`**. `Rider_UpdateHitColl` (`0x8018f95c`) has no
  event-actor sub-check and never calls `EventActorGObj_GetHurtData`. **Riders take enemy contact
  damage through their machine, not directly.**

## Key Structs

### HurtData (0x9C+ bytes) - `hurt.h`

The core per-entity hurt state. Every damageable object (rider, machine, enemy, item) owns one. A
HurtData carries two arrays: **regions** (stride 0xC8) are attack hitboxes with embedded
damage/knockback parameters, and **sub-regions** (stride 0x44) are defensive hurtboxes.

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x00 | HurtKind | kind | Owner type (RIDER=0, MACHINE=2, HURTKIND_3=3, POWERUP=4, STAGE=6) |
| 0x04 | void* | desc | HurtDesc data (joint/bone info) |
| 0x08 | int | region_count | Number of **attack regions** (stride 0xC8) |
| 0x0C | void* | regions | Attack region array (stride 0xC8; contains damage params) |
| 0x10 | int | sub_region_count | Number of **defensive sub-regions** (stride 0x44) |
| 0x14 | void* | sub_regions | Defensive sub-region array (stride 0x44; collision shapes) |
| 0x18 | void* | x18 | Unused/reserved |
| 0x1C | int | hitcoll_log_idx | Index of the strongest hit in the HitCollData log |
| 0x20 | int | attacker_kind | HurtKind of the attacker that dealt the strongest hit |
| 0x24 | float | kb_mag | **Knockback magnitude** - the main output. Non-zero triggers hit reaction |
| 0x28 | float | dmg_taken | Cumulative damage taken this frame |
| 0x2C | float | max_single_hit | Largest single-hit damage this frame |
| 0x30-0x38 | Vec3 | contact_point | Collision contact position |
| 0x3C-0x44 | Vec3 | knockback_dir | Knockback direction vector |
| 0x48-0x50 | Vec3 | attacker_pos | Attacker's position at time of collision |
| 0x54 | int | attacker_flags | Attacker's hit flags (bits from trigger param +0x30) |
| 0x68 | void* | pos_tracker | Position-tracking object (for velocity computation) |
| 0x6C | float | radius | Hurtbox radius (set per-frame by `HurtData_UpdatePerFrame`) |
| 0x70-0x78 | Vec3 | center_pos | Hurtbox center position (set per-frame) |
| 0x80 | float | dmg_multiplier | Damage scaling factor (from MachineData+0x4EC for machines) |
| 0x84 | int | x84 | Gap/padding |
| 0x88 | int | vuln.kind | **Vulnerability state**: 0 = vulnerable, 1 = invincible, 2 = intangible |
| 0x8C | void(\*)() | vuln.on_damage_callback | Called by `HitColl_SetDamageLog` when a hit is logged |
| 0x90 | int | vuln.x90 | Unknown |
| 0x94 | int | vuln.intang_timer | Intangibility timer (counts down; >0 prevents all collision) |
| 0x98 | int | vuln.invuln_timer | Invulnerability timer (counts down; >0 prevents damage) |
| 0x9C | byte | flags2 | Bit 7 cleared each frame by `HurtData_UpdateVulnState`; bits 4-5 set by `HurtData_UpdatePerFrame` |

Where the HurtData lives on each entity:

| Entity | Offset | Attack regions | Defensive sub-regions |
|--------|--------|----------------|-----------------------|
| MachineData | +0x660 | 4 | per model joints |
| RiderData | +0x390 | 4 | per model joints |
| EnemyData | +0x410 | 2 (meteor = 8) | per model joints |
| GrYakuData (stage hazards) | +0xEC | 2 | per model joints |
| ItemData (city items) | +0x148 | varies | varies |
| ProjectileData | +0x108 | per kind | per model joints |

### Region entry (stride 0xC8 = 200 bytes)

Each region in the `regions` array is an **attack hitbox** with embedded damage parameters. When
`HitColl_CheckCollision` finds a region overlapping a victim's sub-region, that region's
damage/knockback values are used.

A region begins with an `active` word at +0x00, followed by the **13-dword HurtParams block at
+0x04-0x34** (written by `Trigger_InitParameters`, which also stores its `flags` arg at +0x38).
HurtParams field X lands at region offset `X + 0x04`.

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x00 | int | active | 0 = inactive/skip; nonzero = active (checked first in `HitColl_CheckCollision`) |
| 0x04 | int | base_damage | Base damage (int; `HitColl_GetDamageDealt` reads region+0x04 and converts to float) |
| 0x08 | float | dmg_distance_factor | Damage scales by relative velocity (`HitColl_GetDamageDealt` reads region+0x08; 0 = fixed) |
| 0x0C-0x20 | | (HurtParams x08-x1c) | Additional HurtParams dwords (x1c at +0x20 = scale/magnitude factor) |
| 0x24 | float | base_knockback | Base knockback magnitude (`HitColl_CalcKnockback` reads region+0x24) |
| 0x28 | float | kb_distance_factor | Knockback scales by relative velocity (`HitColl_CalcKnockback` reads region+0x28; 0 = fixed) |
| 0x2C | int | (HurtParams x28) | Unknown |
| 0x30 | int | type_flags / hit_flags | = HurtParams `hit_flags` (+0x2C). Bits 3-5: hurt type (`(b>>3)&7`; value 8 = skip). Bits 10-16: collision-layer mask, matched against `1 << victim_player_index` |
| 0x31 | byte | filter_flags | Bit 0 / bit 1: type filters (tested in `HitColl_CheckCollision` against victim vuln class) |
| 0x33 | byte | disabled | Bit 0: region is skipped (`lbz 0x33; clrlwi.,31`) |
| 0x34 | int | (HurtParams x30) | Last HurtParams dword |
| 0x38 | int | flags | Set by `Trigger_InitParameters` from its 3rd arg; read by `EnemyKnockback_Default` as the hurt-entry type |
| 0x40-0x48 | Vec3 | pos_cur | Current region position (fallback for `HitColl_GetDamageDealt`/`HitColl_CalcKnockback` when `HurtData.pos_tracker` is NULL) |
| 0x4C | float | radius | Collision sphere radius |
| 0x50-0x58 | Vec3 | pos_prev | Previous region position (velocity = pos_cur - pos_prev) |

There is **no** `current_pos`/`prev_pos` at +0x1C/+0x28 and no `joint_idx` at +0x18 - those offsets
overlap the HurtParams block. Per-frame velocity is normally computed from the **`HurtData.pos_tracker`
object** (HurtData+0x68): `HurtData_UpdatePerFrame` writes the new contact position to
pos_tracker+0x00 and rolls the previous into pos_tracker+0x0C. The region's own +0x40/+0x50 pair is
only the no-tracker fallback.

### Sub-region entry (stride 0x44 = 68 bytes)

Each sub-region is a **defensive hurtbox** - a collision shape used to detect incoming hits.
Initialized by `HurtData_InitRegion` / `HurtDesc_SetupRegion` from model joint data.

### HurtDesc (0x18 bytes) - `hurt.h`

Describes a single hurtbox shape attached to a skeleton joint.

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x00 | int | joint_idx | Skeleton joint index this hurtbox attaches to |
| 0x04 | int | x4 | Unknown (usually 0) |
| 0x08 | float | scale | Hurtbox sphere radius multiplier |
| 0x0C | Vec3 | offset | Local offset from joint position |

### HurtParams (0x34 bytes) - `hurt.h`

Attack-parameter configuration. Zeroed by `Trigger_ClearParameterStruct` (`0x8018a0c0`), filled, then
copied into a region entry by `Trigger_InitParameters`.

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x00 | int | base_damage | Base damage value (int, converted to float by `HitColl_GetDamageDealt`) |
| 0x04 | float | dmg_distance_factor | Damage scaling by relative velocity (0 = fixed damage) |
| 0x08 | float | x08 | Unknown |
| 0x0C | float | x0c | Unknown |
| 0x10 | float | x10 | Unknown |
| 0x14 | int | x14 | Unknown |
| 0x18 | int | x18 | Unknown |
| 0x1C | float | x1c | Scale / magnitude factor |
| 0x20 | float | base_knockback | Base knockback magnitude |
| 0x24 | float | kb_distance_factor | Knockback scaling by relative velocity (0 = fixed knockback) |
| 0x28 | int | x28 | Unknown |
| 0x2C | int | hit_flags | Bitfield: bit 3 = use zero direction vectors instead of positions |
| 0x30 | int | x30 | Unknown |

### HitCollData - `hurt.h`

Global collision log at `stc_hitcolldata` (`0x80559bf4`). Tracks all collisions found against the
current victim this frame.

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x08 | u8 | active | Set to 1 by `HitColl_Init` |
| 0x0C | log[20] | - | Array of 20 collision entries (0x1C bytes each) |
| 0x23C | int | coll_num | Number of valid entries in the log |
| 0x240 | HurtData* | hurt_data | The current victim's HurtData |

Each log entry (0x1C bytes):

| Offset | Type | Description |
|--------|------|-------------|
| 0x00 | void* | Victim's collision data |
| 0x04 | void* | Attacker's trigger data |
| 0x08 | void* | Attacker's hurt entry |
| 0x0C | Vec3 | Attacker position at collision |
| 0x18 | float | Knockback magnitude for this hit |

### TriggerData (0x60 bytes) - `trigger.h`

Per-entity collision trigger state; for enemies it lives at EnemyData+0x45C. Initialized by
`EventActor_RefreshAttackParams` (`0x80201ba4`) from actor_data descriptors. Attack parameters sit at
offsets 0x04-0x34, the same layout a region gets after `Trigger_InitParameters`.

## GObj p_link Classes

The collision iterators walk GObj linked lists by p_link class. `stc_gobj_lookup` is the list-head
array at `r13+0x1254` (`0x805DD0E0 + 0x1254`); **a class's head is at `stc_gobj_lookup + p_link*4`**,
and the walk follows the GObj `p_link` next pointer at GObj+0x08.

| p_link | Offset | Entity type | GetHurtData function |
|--------|--------|-------------|----------------------|
| 8 | +0x20 | Stage hazards (GrYaku) | `GrYaku_GetHurtData` (`0x800f8248`) - GObj userdata (+0x2C) then +0xEC |
| 9 | +0x24 | Machines | `MachineGObj_GetHurtData` (`0x801c8660`) - MachineData+0x660 |
| 10 | +0x28 | Riders | `RiderGObj_GetHurtData` (`0x80192788`) - RiderData+0x390 |
| 12 (0xC) | +0x30 | Event actors / enemies | `EventActorGObj_GetHurtData` (`0x80204878`) - EnemyData+0x410 |
| 14 (0xE) | +0x38 | Projectiles | accessor at `0x80223120` - ProjectileData+0x108 |

The getter at `0x800f8248` is `GrYaku_GetHurtData` (it reads GrYakuData+0xEC, not EnemyData), and the
per-frame stage-hazard check at `0x801d72a4` is `Machine_CheckStageHazardCollision`.

## Damage Pipeline

### Machine side (`Machine_UpdateHitColl`, 0x801c67a0)

Each frame, for every machine:

```
1. HitColl_Init(md->hurt_data)
   - Clears stc_hitcolldata.coll_num = 0
   - Sets stc_hitcolldata.hurt_data = victim

2. Collision checks (each calls HitColl_SetDamageLog on overlap):
   - Machine_CheckRiderCollision      (p_link 10)
   - Machine_CheckMachineCollision    (p_link 9)
   - Machine_CheckProjectileCollision (p_link 14)
   - Machine_CheckEventCollision      (p_link 12)  <- ENEMY DAMAGE ENTERS HERE
   - Machine_CheckItemCollision
   - Machine_CheckStageHazardCollision (p_link 8)
   - Machine_CheckMachineBumpCollision
   - Machine_CheckPatchColl

3. HitColl_ActOnCollision(md->hurt_data)
   - Scans all log entries, finds max knockback
   - Sets hurt_data->kb_mag, contact position, attacker info

4. Machine_ActOnHitCollision(md)
   - If kb_mag != 0:
     - Identifies attacker type (rider/machine/enemy/event/item)
     - Stores attacker info for "hit by" tracking
     - Machine_EnterHitReaction(md) -> state 5 (bounce)
```

### Enemy side (`EventActor_ProcHitColl`, 0x801fc8ec)

The priority-9 GObj proc, inbound only. (Priority 8, `EventActor_ProcHitCollInit` at `0x801fc8e8`, is
a no-op `blr` stub.) Each frame, for each enemy actor:

```
1. Gating checks before running collision:
   - damage_accum_1 (EnemyData+0x994) vs the HP threshold param at +0x3B0
   - a float gate at EnemyData+0x364
   - type NOT in [0x48, 0x4A] (meteor variants skip)
   (plus a flag/state check at EnemyData+0xB08 that can still let some types through)

2. HitColl_Init(ed->hurtdata)   // EnemyData+0x410 is the VICTIM

3. Five sub-checks, each walking one p_link list and calling
   HitColl_CheckCollision(ed+0x410, attacker_hurtdata):
   - 0x8020200c  riders          (p_link 10, RiderGObj_GetHurtData)
   - 0x80202070  machines        (p_link 9,  MachineGObj_GetHurtData)
   - 0x802020d4  projectiles     (p_link 14, accessor 0x80223120)
   - 0x80202130  other actors    (p_link 12, EventActorGObj_GetHurtData; skips self)
   - 0x80202198  stage hazards   (p_link 8,  GrYaku_GetHurtData)

4. HitColl_ActOnCollision(ed->hurtdata)

5. Post-collision handler (0x802021fc)
   - If damage received, sets the damage-pending slot (EnemyData+0xA20/+0xA24)
     feeding EventActor_ProcDamage (priority 10)
```

Each sub-check opens with `HitColl_SetUnk(0)` (`0x8018cf84`) before walking its list.

### How collision checks work (`HitColl_CheckCollision`, 0x8018d284)

Called with two HurtData objects. Argument convention: **r3 = victim** (its sub-regions are iterated,
and the player-index collision-mask source is read from victim+0x00), **r4 = attacker** (the loop
reads `region_count = *(attacker+0x08)` and `regions = *(attacker+0x0C)`, iterating the *attacker's*
regions).

1. **Iterate the attacker's regions** (stride 0xC8). For each active region, check the collision mask
   against the victim's player index.
2. **Iterate the victim's sub-regions** (stride 0x44). For each pair, call `Hit_CheckOverlap` for a
   sphere-vs-sphere overlap test.
3. **On overlap**, call `HitColl_SetDamageLog` with the attacker's **region entry** as the damage
   source. The region's fields at 0x04-0x34 feed `HitColl_GetDamageDealt` and `HitColl_CalcKnockback`.

### HitColl_SetDamageLog (0x8018cf94) - the damage calculator

Called whenever two collision shapes overlap.

1. **Damage** via `HitColl_GetDamageDealt`:
   ```
   if dmg_distance_factor == 0:   damage = base_damage          (fixed, no clamp)
   else:                          damage = base_damage + dmg_distance_factor * relative_velocity_magnitude
                                  then clamped to a minimum of 1.0
   ```
   `HitColl_GetDamageDealt` short-circuits when `dmg_distance_factor` (region+0x08) is exactly 0 and
   returns `base_damage` unclamped; only the velocity-scaled path applies the 1.0 floor (SDA2
   constant at `-22284(r2)`, *not* 0). The relative-velocity magnitude is `|pos_cur - pos_prev|`,
   taken from the attacker's `pos_tracker` (HurtData+0x68) when present, else from region+0x40/+0x50.
   The result is then scaled by the victim's damage multiplier (`hurtdata->dmg_multiplier`).

2. **Knockback** via `HitColl_CalcKnockback`:
   ```
   if kb_distance_factor == 0:  knockback = base_knockback     (no velocity term, no clamp)
   else:                        knockback = base_knockback + kb_distance_factor * relative_velocity_magnitude
   ```
   Both damage and knockback are scaled by the victim's `dmg_multiplier` (HurtData+0x80). The hit is
   **skipped** (not logged) if the scaled knockback is at or below a threshold (SDA2
   `FLOAT_805e1040`, approximately 0), OR `HurtData_CheckVulnerability` returns nonzero (intangible /
   invulnerable victim), OR the entry already has a pending hit.

3. **Logs the collision** into `stc_hitcolldata.log` (max 20 entries; asserts on overflow). Each
   entry stores the attacker hurt struct (+0x00), the trigger/region params (+0x04), the per-region
   log object (+0x08), the attacker position (+0x0C, Vec3), and the computed knockback (+0x18). It
   also accumulates `dmg_taken` (HurtData+0x28) and bumps `max_single_hit` (HurtData+0x2C).

4. **Calls `on_damage_callback`** at HurtData+0x8C if set - unconditionally at the end, with a
   "logged?" flag as an argument, even when the hit was skipped. For machines this is
   `Machine_OnDamageCallback`; for enemies `EventActor_OnDamageCallback`.

### HitColl_ActOnCollision (0x8018d878) - knockback resolution

After all collision checks, scans the log for the **strongest** knockback: iterates all entries,
finds the max `knockback`, and stores the winning entry's data into the victim's HurtData (`kb_mag`,
`hitcoll_log_idx`, collision position, attacker flags). With no entries, or max knockback at or below
0, `kb_mag` stays 0 and no hit reaction fires.

### Machine_ActOnHitCollision - hit reaction dispatch

If `kb_mag != 0`, identifies the attacker type via a switch:

| Attacker kind | Source | Special cases |
|---------------|--------|---------------|
| 0 | Rider | Gets attacker player index |
| 1 | Machine | Gets attacker player index |
| 3 | Event actor | Special-cases meteor (type 0x4D) |
| 5 | Enemy | Gets enemy type for tracking |
| 6 | Item/Ground | Special-cases types 0x3D, 0x41 |

Then calls `Machine_EnterHitReaction` to enter state 5 (bounce/knockback).

## Damage Application

### Machine_GiveDamage (0x801e1ee8)

`void Machine_GiveDamage(MachineData *md, float damage, GOBJ *source_gobj)` - `damage` arrives in
`f1`; the 3rd arg is the GOBJ that dealt the hit (used by `Machine_OnDamageVisual` for the hit-spark
direction, and **must not be NULL in City Trial**), not a flags integer. It:

1. Adds `damage` to `MachineData.dmg_accumulator` (+0x6AC), clamped to a max constant.
2. Calls `Gm_IsDamageEnabled()` (`0x8000a188`); if enabled, subtracts `damage` from `MachineData.hp`
   (+0xA18), clamped to a minimum.
3. Enters the death sequence if HP reaches the minimum.
4. Applies the warning color animation if HP drops below a threshold fraction of max.
5. Triggers visual damage effects via `Machine_OnDamageVisual` (reads `source_gobj`).

`Machine_GiveDamage` does **not** itself cause knockback/bounce - pair it with `kb_mag` plus
`Machine_EnterHitReaction`.

### Machine_EnterHitReaction (0x801e05bc)

Transitions a machine into hit reaction (state 5): saves the previous state, clears formation
tracking, calls `HurtData_UpdateVulnState` on the machine's HurtData, and registers the hit reaction
with the system.

## Vulnerability System

Three states controlled by `HurtData.vuln.kind`:

| Value | State | Effect |
|-------|-------|--------|
| 0 | Vulnerable | Normal - takes damage and knockback |
| 1 | Invincible | No damage, no knockback, but collision is still detected |
| 2 | Intangible | No collision at all - hurtbox is "ghosted" |

Timers:

- **Intangibility** (`vuln.intang_timer` at +0x94): set by `HurtData_GiveIntangibility`
  (`0x8018cb5c`, named `HurtData_UpdateIntangibility` in the symbol map). Takes priority over
  invincibility. Sets `vuln.kind = 2`. Counts down each frame. Only raises the timer (updates if the
  new value exceeds the current).
- **Invulnerability** (`vuln.invuln_timer` at +0x98): set by `HurtData_GiveInvincibility`
  (`0x8018cc38`). Sets `vuln.kind = 1` (or 2 if intangibility is also active). Active only when the
  intangibility timer is 0.

`HurtData_UpdateVulnState` (`0x8018cb28`) refreshes the state each frame:

```c
clear flag bit 7 at +0x9C;
if (intang_timer != 0) return;  // stay in current state
vuln.kind = (invuln_timer != 0) ? 1 : 0;
```

## Enemy Damage Mechanics

### Enemy HurtData initialization

`EventActor_HurtDataCreate` (`0x80201ee8`) creates the HurtData at EnemyData+0x410:

- **Meteor** (type 0x4D): `HurtData_Create(gobj, HURTKIND_3, 8, joint_count, 0)` - 8 attack regions.
- **All other enemies**: `HurtData_Create(gobj, HURTKIND_3, 2, joint_count, 0)` - 2 attack regions.
- Sets `on_damage_callback` (HurtData+0x8C) = `EventActor_OnDamageCallback` (`0x80201c78`).
- Iterates the actor's joint descriptor to build the defensive sub-regions via `HurtData_InitRegion`.

### Attack hitbox enable / disable (per frame)

Enemy attack hitboxes are not statically on - they toggle per animation frame:

- **Per-frame param refresh:** `EventActor_RefreshAttackParams` (`0x80201ba4`) reads the current
  animation frame's hurt descriptor and writes the attack params into the enemy's TriggerData at
  EnemyData+0x45C.
- **Enable:** activation is **data-driven by the animation frame** - there is no dedicated "activate"
  anim command. When the current anim frame carries hurt data, `EventActor_RefreshAttackParams`
  invokes `Trigger_SetState1` (`0x8018a0e8`), which sets `region.active = 1` and optionally calls
  `Trigger_InitParameters` to copy the 0x34-byte HurtParams block. `Trigger_SetState1` is the
  de-facto `Hit_SetActive` (mirror of `Hit_SetInactive`).
- **Disable:** anim-script **cmd-13**, `EnemyAnimCmd_DisableHit` (`0x80201138`), is an explicit
  per-frame OFF switch the script can fire. It calls `Hit_SetInactive` (`0x80189d1c`, sets
  `region.active = 0`) through the index wrapper at `0x8018c7f8`, which computes `regions[idx]`
  (stride 0xC8 off HurtData+0x0C). The region index is `bytecode_operand & 0x03FFFFFF`.

Both the TriggerData at +0x45C and the attack regions at +0x410 participate in collision. The regions
(stride 0xC8) carry embedded HurtParams at 0x04-0x34 that `HitColl_GetDamageDealt` and
`HitColl_CalcKnockback` read during `HitColl_CheckCollision`, driven from the machine side by
`Machine_CheckEventCollision`.

### Enemy receiving damage (`EventActor_ProcDamage`, 0x801fc9f0)

Takes the enemy GObj, dereferences userdata (+0x2C) to EnemyData, and early-returns for meteor types
(EnemyData+0x0C in [0x48, 0x4A]). Otherwise, reading the HurtData at EnemyData+0x410:

1. Reads `kb_mag` (HurtData+0x24) and `dmg_taken` (HurtData+0x28).
2. If `kb_mag == 0` (sentinel): calls `giveEnemyDamage(dmg_taken)` only when `dmg_taken` is nonzero -
   a cosmetic accumulator update, no launch.
3. If `kb_mag != 0`: clamps `dmg_taken`, calls `giveEnemyDamage`, then dispatches knockback through
   the enemy's **custom damage handler at EnemyData+0xAD0** if set, else the default
   `EnemyKnockback_Default` (`0x8020bcd8`) - a thin dispatcher that reads the hurt-entry type at
   `region+0x38` and calls `Enemy_ApplyKnockback`.
4. Always clears the damage-pending flag at EnemyData+0x9A0.

### Knockback tiers

Enemies have no HP. When hit, `Enemy_ApplyKnockback` (`0x8020b784`):

1. Scales the incoming damage (`trigger_params+0x08`) via `Enemy_ScaleDamage` (`0x8020b71c`), then
   classifies it into a tier (0-3) via `Enemy_ClassifyDamageTier` (`0x8020b740`). Thresholds come
   from the **global enemy parameter table**, reached through the SDA pointer at `r13+0x798`
   (`0x805DD878`); the three thresholds live at table offsets +0x08, +0x0C, +0x10:
   - Tier 0: damage < `tbl[+0x08]`
   - Tier 1: `tbl[+0x08]` <= damage < `tbl[+0x0C]`
   - Tier 2: `tbl[+0x0C]` <= damage < `tbl[+0x10]`
   - Tier 3: damage >= `tbl[+0x10]`

   The tier is stored at EnemyData+0xA1C.

2. Sets the launch/stun frame count (EnemyData+0xA18) from the tier's base stun `tbl[tier*4 + 0x60]`,
   plus an extra term scaled by `tbl[tier*4 + 0x30]` and a per-enemy launch multiplier
   `*(actor_data->+0x00 + 0xA0)` (where `actor_data` = EnemyData+0x14).

3. Grants intangibility for the launch duration via
   `HurtData_GiveIntangibility(EnemyData+0x410, frames)`.

4. Randomizes the three knockback sign components using `HSD_Randi(8)` (bits 0/1/2 pick +/- per axis).

5. Builds the knockback direction (from attacker position or trigger vectors, mode-dependent), then
   transitions the enemy to its knockback state via `EventActor_DisableRendering` (rendering is
   disabled during launch).

Death occurs when the launch/stun counter at +0xA18 reaches 0 during the knockback state.

### Damage accumulators

`giveEnemyDamage` (`0x8020b680`) adds damage to two accumulators at EnemyData+0x994 and +0x998
(capped at 9999 each). These are **cosmetic only** - nothing reads them for death logic.

## Applying Damage From Custom Code

### Direct damage plus hit reaction (bypassing the collision pipeline)

```c
// source_gobj must be non-NULL in City Trial - Machine_OnDamageVisual reads its
// +0x20 forward vector for the hit-spark direction.
GOBJ *mg = Ply_GetMachineGObj(player_idx);
if (!mg) return;
MachineData *md = mg->userdata;

// Apply HP damage. 3rd arg is the GOBJ* damage source (NOT a flags int).
Machine_GiveDamage(md, damage_amount, mg);

// Set knockback magnitude on the machine's hurt data (optional, for physics)
md->hurt_data->kb_mag = knockback_magnitude;

// Enter hit reaction state (bounce)
Machine_EnterHitReaction(md);
```

This skips the collision pipeline entirely. The live consumer is the 1 HP trap in
`ap_item_handler.c` (`mods/archipelago`), which calls `Machine_GiveDamage(md, md->hp - 1.0f, mg)` for
each human player, passing the machine GObj as the source.

### Through the collision pipeline (HurtParams)

```c
HurtParams params;
Trigger_ClearParameterStruct(&params);  // memset 0x34 bytes

params.base_damage = 10;              // HP damage dealt (int, converted to float)
params.dmg_distance_factor = 0.0f;    // 0 = fixed damage regardless of speed
params.base_knockback = 5.0f;         // knockback strength
params.kb_distance_factor = 0.0f;     // 0 = fixed knockback

// Apply to a machine via the collision system
Machine_ApplyHurt(machine_hurt_data, hurt_slot_idx, &params);
```

### Why writing enemy attack regions does not work

Enemy attack region parameters are **refreshed every frame** from animation data. The per-frame chain
is `0x801ff520` -> `EventActor_RefreshAttackParams` (`0x80201ba4`) -> `Trigger_SetState1` ->
`Trigger_InitParameters`, which reads `*(*(enemyData+0x14) + 0x14)` (the current animation frame's
hurt parameter data) and overwrites the region's HurtParams. The animation script can also disable a
region mid-attack via cmd-13. Separately, `EnemyAnimCmd_SetHitDesc` (`0x80200eb8`) parses packed
binary collision data from animation streams into regions.

So direct writes to region entries do not persist - they are overwritten next frame. Use the direct
damage path above instead.

## Key Functions

| Function | Address | Description |
|----------|---------|-------------|
| HitColl_Init | 0x8018cf64 | Clears the collision log, sets victim hurt_data |
| HitColl_SetUnk | 0x8018cf84 | Called with 0 at the head of each enemy inbound sub-check |
| HitColl_CheckCollision | 0x8018d284 | Core: iterates attacker regions vs victim sub-regions (r3 = victim, r4 = attacker) |
| HitColl_GetDamageDealt | 0x8018ace4 | Computes damage from region params + velocity |
| HitColl_CalcKnockback | 0x8018ab90 | Computes knockback from region params + velocity |
| HitColl_SetDamageLog | 0x8018cf94 | Logs a collision: calculates damage/kb, stores in the global log |
| HitColl_ActOnCollision | 0x8018d878 | Resolves the log to the strongest knockback into HurtData |
| HitColl_CalcContactPoint | 0x8018a5b8 | Computes the contact point between two collision shapes |
| HitColl_CalcKnockbackDir | 0x8018ab10 | Computes knockback direction from contact data |
| HitColl_ClearLogEntry | 0x80189e3c | Clears log entries matching an attacker kind |
| HitColl_ResolveLogEntry | 0x8018db10 | Retrieves entry data for `Machine_ActOnHitCollision` |
| Hit_SetInactive | 0x80189d1c | Sets `region.active = 0`; reached via the index wrapper 0x8018c7f8 |
| Trigger_SetState1 | 0x8018a0e8 | Sets `region.active = 1`; de-facto `Hit_SetActive`, calls `Trigger_InitParameters` |
| HurtData_CheckVulnerability | 0x8018cd9c | Returns non-zero if the target is protected |
| Machine_ApplyHurt | 0x8018d1a8 | Applies hurt from HurtParams (calls `Trigger_InitParameters` then `HitColl_SetDamageLog`). Also aliased `Machine_ApplyHurtFinal` = `HitColl_SetDamageLog` in `machine.h` |
| Machine_GiveDamage | 0x801e1ee8 | `(md, float damage, GOBJ *source_gobj)` - subtracts HP, triggers death at min HP. source_gobj must be non-NULL in City Trial. No knockback by itself |
| Machine_EnterHitReaction | 0x801e05bc | Enters bounce/hit state 5 |
| Machine_UpdateHitColl | 0x801c67a0 | Per-frame pipeline orchestrator (machine side) |
| Machine_CheckEventCollision | 0x801d71ec | Enemy/event-actor check (p_link 12); delivers enemy OUTBOUND attacks |
| Machine_CheckStageHazardCollision | 0x801d72a4 | Stage-hazard check (p_link 8) |
| Machine_ActOnHitCollision | 0x801d7308 | Reacts to the strongest hit: attacker ID + hit reaction |
| Machine_InitHurtData | 0x801d6e84 | Creates HurtData for a machine |
| Rider_InitHurtData | 0x80196170 | Creates HurtData for a rider |
| Rider_UpdateHitColl | 0x8018f95c | Per-frame pipeline (rider side); has NO event-actor sub-check |
| RiderGObj_GetHurtData | 0x80192788 | Rider HurtData (RiderData+0x390) |
| MachineGObj_GetHurtData | 0x801c8660 | Machine HurtData (MachineData+0x660) |
| EventActorGObj_GetHurtData | 0x80204878 | Enemy/event-actor HurtData (EnemyData+0x410) |
| GrYaku_GetHurtData | 0x800f8248 | Stage-hazard HurtData (GrYakuData+0xEC) |
| EventActor_HurtDataCreate | 0x80201ee8 | Creates enemy HurtData (2 attack regions; 8 for meteor 0x4D) |
| EventActor_RefreshAttackParams | 0x80201ba4 | Per attack frame: refreshes hurt params from the anim frame descriptor into TriggerData (EnemyData+0x45C); enables the region via `Trigger_SetState1` |
| EventActor_ProcHitCollInit | 0x801fc8e8 | Priority-8 no-op `blr` stub |
| EventActor_ProcHitColl | 0x801fc8ec | Priority-9 enemy INBOUND hitcoll processing (enemy as victim only) |
| EventActor_ProcDamage | 0x801fc9f0 | Processes received damage into knockback (via the EnemyData+0xAD0 custom handler or `EnemyKnockback_Default`) |
| EnemyKnockback_Default | 0x8020bcd8 | Default enemy knockback dispatcher (region+0x38 type -> `Enemy_ApplyKnockback`) |
| EventActor_OnDamageCallback | 0x80201c78 | Enemy on-damage callback (set in HurtData+0x8C) |
| EnemyAnimCmd_DisableHit | 0x80201138 | Anim cmd-13 handler: explicit region OFF |
| EnemyAnimCmd_SetHitDesc | 0x80200eb8 | Parses packed collision data from anim streams into regions |
| Trigger_ClearParameterStruct | 0x8018a0c0 | Zeros a HurtParams (0x34 bytes) |
| Trigger_InitParameters | 0x8018a118 | Copies HurtParams into region/trigger offsets 0x04-0x34 |
| HurtData_Create | 0x8018c1c8 | Allocates HurtData from object pools |
| HurtData_InitRegion | 0x8018c598 | Initializes a defensive sub-region from joint data |
| HurtData_UpdatePerFrame | 0x8018c4e8 | Per-frame position/radius update |
| HurtData_GiveIntangibility (map: HurtData_UpdateIntangibility) | 0x8018cb5c | Sets the intangibility timer |
| HurtData_GiveInvincibility | 0x8018cc38 | Sets the invulnerability timer |
| HurtData_UpdateVulnState | 0x8018cb28 | Refreshes `vuln.kind` from the timers |
| Enemy_ClassifyDamageTier | 0x8020b740 | Classifies damage into tier 0-3 |
| Enemy_ApplyKnockback | 0x8020b784 | Full enemy knockback sequence |
| Enemy_ScaleDamage | 0x8020b71c | Scales damage by a global factor from the enemy param table |
| giveEnemyDamage | 0x8020b680 | Adds to the cosmetic damage accumulators |
| Gm_IsDamageEnabled | 0x8000a188 | Gates the HP subtraction in `Machine_GiveDamage` |
| EnemyActor_RumblePlayer | 0x801ff80c | Triggers controller rumble (NOT a damage function) |

## Known Limitations

- **TriggerData to attack-region plumbing.** Whether EnemyData+0x45C backs region[0]'s params
  directly, or a per-frame copy step writes them into the +0x410 regions that
  `Machine_CheckEventCollision` reads, is not traced. Resolving it needs a live attacking-enemy
  instance.
- **Region geometry past +0x40.** The exact word offsets for `radius` (0x4C) and the geometry beyond
  +0x40 are read from live gameplay instances only.
- **Enemy tier threshold values.** The three thresholds at `tbl[+0x08]`/`[+0x0C]`/`[+0x10]` off
  `r13+0x798` are runtime-loaded data, so only their offsets are known, not their values.
