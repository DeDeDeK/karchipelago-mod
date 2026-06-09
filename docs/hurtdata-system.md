# HurtData & Hit Collision System

The hurt/damage system handles all physical collision damage in Kirby Air Ride — machine-vs-machine, machine-vs-enemy, machine-vs-item, etc. This document covers the full pipeline from collision detection through damage application and knockback.

**Related docs:** `collision-system.md` (HitColl / `Item_InitDesc` collision-shape setup — overlaps the HitColl structs here), `enemy-ai-system.md` (enemy state machine that the knockback state feeds into), `projectile-system.md` (uses `HurtData_UpdatePerFrame`), `deathlink.md` and `topride-kirby-states.md` (alternative ways to kill/apply state to a player that bypass this pipeline — DeathLink uses `Ply_AddDeath`/`Ply_SetHP`, not `Machine_GiveDamage`).

## Key Structs

### HurtData (0x9C+ bytes) — `hurt.h`

The core per-entity hurt state. Every damageable object (rider, machine, enemy, item) owns a HurtData. A HurtData contains two arrays:
- **Regions** (stride 0xC8): Attack hitboxes with embedded damage/knockback parameters
- **Sub-regions** (stride 0x44): Defensive hurtboxes (collision shapes)

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x00 | HurtKind | kind | Owner type (RIDER=0, MACHINE=2, HURTKIND_3=3, POWERUP=4, STAGE=6) |
| 0x04 | void* | desc | Pointer to HurtDesc data (joint/bone info) |
| 0x08 | int | region_count | Number of **attack regions** (stride 0xC8) |
| 0x0C | void* | regions | Attack region array (stride 0xC8, contains damage params) |
| 0x10 | int | sub_region_count | Number of **defensive sub-regions** (stride 0x44) |
| 0x14 | void* | sub_regions | Defensive sub-region array (stride 0x44, collision shapes) |
| 0x18 | void* | x18 | Unused/reserved |
| 0x1C | int | hitcoll_log_idx | Index of strongest hit in HitCollData log |
| 0x20 | int | attacker_kind | HurtKind of the attacker that dealt the strongest hit |
| 0x24 | float | kb_mag | **Knockback magnitude** — the main output. Non-zero triggers hit reaction |
| 0x28 | float | dmg_taken | Cumulative damage taken this frame |
| 0x2C | float | max_single_hit | Largest single-hit damage this frame |
| 0x30–0x38 | Vec3 | contact_point | Collision contact position |
| 0x3C–0x44 | Vec3 | knockback_dir | Knockback direction vector |
| 0x48–0x50 | Vec3 | attacker_pos | Attacker's position at time of collision |
| 0x54 | int | attacker_flags | Attacker's hit flags (bits from trigger param +0x30) |
| 0x68 | void* | pos_tracker | Position-tracking object (for velocity computation) |
| 0x6C | float | radius | Hurtbox radius (set per-frame by UpdatePerFrame) |
| 0x70–0x78 | Vec3 | center_pos | Hurtbox center position (set per-frame) |
| 0x80 | float | dmg_multiplier | Damage scaling factor (from MachineData+0x4EC for machines) |
| 0x84 | int | x84 | Gap/padding |
| 0x88 | int | vuln.kind | **Vulnerability state**: 0=vulnerable, 1=invincible, 2=intangible |
| 0x8C | void(\*)() | vuln.on_damage_callback | Called by HitColl_SetDamageLog when a valid hit is logged |
| 0x90 | int | vuln.x90 | Unknown |
| 0x94 | int | vuln.intang_timer | Intangibility timer (counts down; >0 prevents all collision) |
| 0x98 | int | vuln.invuln_timer | Invulnerability timer (counts down; >0 prevents damage) |
| 0x9C | byte | flags2 | Misc flags (bit 7 cleared each frame by UpdateVulnState) |

**Where HurtData lives on each entity:**

| Entity | Offset | Attack regions | Defensive sub-regions |
|--------|--------|----------------|----------------------|
| MachineData | +0x660 | 4 | per model joints |
| RiderData | +0x390 | 4 | per model joints |
| EnemyData | +0x410 | 2 (meteor=8) | per model joints |
| GrYakuData (stage hazards) | +0xEC | 2 | per model joints |
| ItemData (city items) | +0x148 | varies | varies |

### Region Entry (stride 0xC8 = 200 bytes)

Each region in the `regions` array is an **attack hitbox** that contains embedded damage parameters. When `HitColl_CheckCollision` finds a region overlapping a victim's sub-region, the region's damage/knockback values are used.

A region begins with an `active` word at +0x00, followed by the **13-dword HurtParams block at +0x04–0x34** (written by `Trigger_InitParameters`, which also stores its `flags` arg at +0x38). HurtParams field X lands at region offset `X + 0x04`. Verified field offsets (from `getDamageDealt`/`HitColl_CalcKnockback`/`HitColl_CheckCollision` reads):

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x00 | int | active | 0 = inactive/skip; nonzero = active (checked first in `HitColl_CheckCollision`) |
| 0x04 | int | base_damage | Base damage (int; `getDamageDealt` reads region+0x04, converts to float) |
| 0x08 | float | dmg_distance_factor | Damage scales by relative velocity (`getDamageDealt` reads region+0x08; 0 = fixed) |
| 0x0C–0x20 | | (HurtParams x08–x1c) | Additional HurtParams dwords (x1c at +0x20 = scale/magnitude factor) |
| 0x24 | float | base_knockback | Base knockback magnitude (`HitColl_CalcKnockback` reads region+0x24) |
| 0x28 | float | kb_distance_factor | Knockback scales by relative velocity (`HitColl_CalcKnockback` reads region+0x28; 0 = fixed) |
| 0x2C | int | (HurtParams x28) | Unknown |
| 0x30 | int | type_flags / hit_flags | = HurtParams `hit_flags` (+0x2C). Bits 3–5: hurt type (`(b>>3)&7`; value 8 = skip). Bits 10–16: collision-layer mask, matched against `1 << victim_player_index` |
| 0x31 | byte | filter_flags | Bit 0 / bit 1: type filters (tested in `HitColl_CheckCollision` against victim vuln class) |
| 0x33 | byte | disabled | Bit 0: if set, region is skipped (`lbz 0x33; clrlwi.,31`) |
| 0x34 | int | (HurtParams x30) | Last HurtParams dword |
| 0x38 | int | flags | Set by `Trigger_InitParameters` from its 3rd arg |
| 0x40–0x48 | Vec3 | pos_cur | Current region position (fallback used by `getDamageDealt`/`HitColl_CalcKnockback` when HurtData.pos_tracker is NULL) |
| 0x50–0x58 | Vec3 | pos_prev | Previous region position (velocity = pos_cur − pos_prev) |
| 0x4C | float | radius | Collision sphere radius |

> **Note:** earlier drafts placed `current_pos`/`prev_pos` at +0x1C/+0x28 and a `joint_idx` at +0x18; those overlap the HurtParams block and are incorrect. Per-frame velocity is normally computed from the **`HurtData.pos_tracker` object** (HurtData+0x68): `HurtData_UpdatePerFrame` writes the new contact position to pos_tracker+0x00 and rolls the previous into pos_tracker+0x0C. The region's own +0x40/+0x50 pair is only the no-tracker fallback. Exact word offsets for `radius` (0x4C) and the geometry past +0x40 are read from live (gameplay) instances and are **not verifiable from the vanilla main-menu mem1.raw snapshot**.

### Sub-Region Entry (stride 0x44 = 68 bytes)

Each sub-region is a **defensive hurtbox** — a collision shape used to detect incoming hits. These are initialized by `HurtData_InitRegion` / `HurtDesc_SetupRegion` from model joint data.

### HurtDesc (0x18 bytes) — `hurt.h`

Describes a single hurtbox shape attached to a skeleton joint.

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x00 | int | joint_idx | Skeleton joint index this hurtbox attaches to |
| 0x04 | int | x4 | Unknown (usually 0) |
| 0x08 | float | scale | Hurtbox sphere radius multiplier |
| 0x0C | Vec3 | offset | Local offset from joint position |

### HurtParams (0x34 bytes) — `hurt.h`

Configuration struct that defines attack parameters. Zeroed by `Trigger_ClearParameterStruct` (0x8018a0c0), then fields are set before passing to `Trigger_InitParameters` which copies them into a region entry.

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x00 | int | base_damage | Base damage value (int, converted to float by getDamageDealt) |
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

### HitCollData — `hurt.h`

Global collision log at `stc_hitcolldata` (0x80559bf4). Tracks all collisions found against the current victim per frame.

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x08 | u8 | active | Set to 1 by HitColl_Init |
| 0x0C | log[20] | — | Array of 20 collision entries (0x1C bytes each) |
| 0x23C | int | coll_num | Number of valid entries in the log |
| 0x240 | HurtData* | hurt_data | The current victim's HurtData pointer |

Each log entry (0x1C bytes):

| Offset | Type | Description |
|--------|------|-------------|
| 0x00 | void* | Victim's collision data |
| 0x04 | void* | Attacker's trigger data |
| 0x08 | void* | Attacker's hurt entry |
| 0x0C | Vec3 | Attacker position at collision |
| 0x18 | float | Knockback magnitude for this hit |

### TriggerData (0x60 bytes) — `trigger.h`

Per-entity collision trigger state. For enemies, located at EnemyData+0x45C. Initialized by `zz_80201ba4_` from actor_data descriptors. Contains attack parameters at offsets 0x04–0x34 (same layout as HurtParams after `Trigger_InitParameters`).

## GObj p_link Classes

The collision system iterates GObj linked lists by p_link class:

`stc_gobj_lookup` is the GObj list array at `r13+0x1254` (0x805DD0E0 + 0x1254). Each p_link class's list head lives at a fixed offset into it; the collision iterators walk the list via the GObj `p_link` next pointer at GObj+0x08.

| p_link | stc_gobj_lookup offset | Entity Type | GetHurtData Function |
|--------|----------------------|-------------|---------------------|
| 8 | +0x20 | Stage Hazards (GrYaku) | `GrYaku_GetHurtData` (0x800f8248) — reads GObj userdata (+0x2C) then +0xEC |
| 12 (0xC) | +0x30 | Event Actors / Enemies | `EventActorGObj_GetHurtData` (0x80204878) — reads GObj userdata (+0x2C) then +0x410 |

**Naming note (resolved):** the getter at 0x800f8248 is named **`GrYaku_GetHurtData`** in the current map (it reads GrYakuData+0xEC, not EnemyData — an earlier draft of this doc called it the misnamed `Enemy_GetHitColl`). Likewise, the per-frame stage-hazard check at 0x801d72a4 is named **`Machine_CheckStageHazardCollision`** (an earlier draft called it the misnamed `Machine_CheckEnemyCollision`). Enemy/event actors are checked by `Machine_CheckEventCollision` (0x801d71ec, p_link 12); it loads stc_gobj_lookup+0x30, calls `EventActorGObj_GetHurtData` per actor, then `HitColl_CheckCollision` against the machine's hurt_data (MachineData+0x660).

## Damage Pipeline — Per-Frame Flow

### Machine Side (Machine_UpdateHitColl, 0x801c67a0)

Each frame, for every machine:

```
1. HitColl_Init(md->hurt_data)
   └── Clears stc_hitcolldata.coll_num = 0
   └── Sets stc_hitcolldata.hurt_data = victim

2. Collision Checks (each calls HitColl_SetDamageLog on overlap):
   ├── Machine_CheckRiderCollision     (p_link riders)
   ├── Machine_CheckMachineCollision   (p_link machines)
   ├── Machine_CheckProjectileCollision
   ├── Machine_CheckEventCollision        (p_link 12) ← ENEMY DAMAGE ENTERS HERE
   ├── Machine_CheckItemCollision
   ├── Machine_CheckStageHazardCollision  (p_link 8)
   ├── Machine_CheckMachineBumpCollision
   └── Machine_CheckPatchColl

3. HitColl_ActOnCollision(md->hurt_data)
   └── Scans all log entries, finds max knockback
   └── Sets hurt_data->kb_mag, contact position, attacker info

4. Machine_ActOnHitCollision(md)
   └── If kb_mag != 0:
       ├── Identifies attacker type (rider/machine/enemy/event/item)
       ├── Stores attacker info for "hit by" tracking
       └── Machine_EnterHitReaction(md)  → state 5 (bounce)
```

### Enemy Side (EventActor_ProcHitColl, 0x801fc8ec)

Each frame, for each enemy actor — this is the **priority 9** GOBJProc (priority 8, `EventActor_ProcHitCollInit` at 0x801fc8e8, is a no-op `blr` stub; see the proc-priority list in `enemy.h`):

```
1. Gating checks before running collision:
   - damage_accum_1 (EnemyData+0x994) vs the HP threshold param at +0x3B0
   - a float gate at EnemyData+0x364
   - type NOT in [0x48, 0x4A] (meteor variants skip)
   (plus a flag/state check that can still let some types through)

2. HitColl_Init(ed->hurtdata)  // at EnemyData+0x410

3. Collision checks against machines/riders/etc.
   (zz_8020200c_ through zz_80202198_)

4. HitColl_ActOnCollision(ed->hurtdata)

5. Post-collision handler (zz_802021fc_)
   └── If damage received, queues for EventActor_ProcDamage (priority 10)
```

### How Collision Checks Work (HitColl_CheckCollision, 0x8018d284)

Called with two HurtData objects (victim and attacker). The function:

1. **Iterates attacker's regions** (stride 0xC8): For each active region, checks collision mask against victim's player index.

2. **Iterates victim's sub-regions** (stride 0x44): For each pair, calls `Hit_CheckOverlap` for sphere-vs-sphere overlap test.

3. **On overlap**: Calls `HitColl_SetDamageLog` with the attacker's **region entry** as the damage source. The region's fields at offsets 0x04-0x34 provide the damage/knockback values to `getDamageDealt` and `HitColl_CalcKnockback`.

### HitColl_SetDamageLog (0x8018cf94) — The Core Damage Calculator

Called whenever two collision shapes overlap. For each collision:

1. **Calculates damage** via `getDamageDealt`:
   ```
   if dmg_distance_factor == 0:   damage = base_damage          (fixed, no clamp)
   else:                          damage = base_damage + dmg_distance_factor × relative_velocity_magnitude
                                  then clamped to a minimum of 1.0
   ```
   `getDamageDealt` short-circuits when `dmg_distance_factor` (region+0x08) is exactly 0 and returns `base_damage` unclamped; only the velocity-scaled path applies the 1.0 floor (SDA2 constant at `-22284(r2)`, *not* 0). The relative-velocity magnitude is `|pos_cur − pos_prev|` taken from the attacker's `pos_tracker` (HurtData+0x68) when present, else from region+0x40/+0x50. The result is then scaled by the victim's damage multiplier (`hurtdata->dmg_multiplier`).

2. **Calculates knockback** via `HitColl_CalcKnockback`:
   ```
   if kb_distance_factor == 0:  knockback = base_knockback     (no velocity term, no clamp)
   else:                        knockback = base_knockback + kb_distance_factor × relative_velocity_magnitude
   ```
   Both damage and knockback are then scaled by the victim's damage multiplier `hurtdata->dmg_multiplier` (HurtData+0x80). The hit is **skipped** (not logged) if the scaled knockback ≤ a threshold (SDA2 `FLOAT_805e1040`, ≈0), OR `HurtData_CheckVulnerability` returns nonzero (intangible / invulnerable victim), OR the entry already has a pending hit.

3. **Logs the collision** into `stc_hitcolldata.log` (max 20 entries; asserts on overflow). Each entry stores: attacker hurt struct (+0x00), the trigger/region params (+0x04), the per-region log object (+0x08), the attacker position (+0x0C, Vec3), and the computed knockback (+0x18). It also accumulates `dmg_taken` (HurtData+0x28 += scaled damage) and bumps `max_single_hit` (HurtData+0x2C).

4. **Calls on_damage_callback** at HurtData+0x8C if set — called unconditionally at the end (a "logged?" flag is passed as an argument), even when the hit was skipped. For machines this is `Machine_OnDamageCallback`; for enemies `EventActor_OnDamageCallback`.

### HitColl_ActOnCollision (0x8018d878) — Knockback Resolution

After all collision checks, scans the log to find the **strongest** knockback:
- Iterates all entries, finds max `knockback` value
- Stores the winning entry's data into the victim's HurtData: `kb_mag`, `hitcoll_log_idx`, collision position, attacker flags
- If no entries or max knockback ≤ 0, `kb_mag` remains 0 → no hit reaction

### Machine_ActOnHitCollision — Hit Reaction Dispatch

If `kb_mag != 0`, identifies the attacker type via a switch:

| Attacker Kind | Source | Special Cases |
|---------------|--------|---------------|
| 0 | Rider | Gets attacker player index |
| 1 | Machine | Gets attacker player index |
| 3 | Event Actor | Special-cases meteor (type 0x4D) |
| 5 | Enemy | Gets enemy type for tracking |
| 6 | Item/Ground | Special-cases types 0x3D, 0x41 |

Then calls `Machine_EnterHitReaction` to enter state 5 (bounce/knockback).

## Damage Application (HP Reduction)

### Machine_GiveDamage (0x801e1ee8)

`void Machine_GiveDamage(MachineData *md, float damage, GOBJ *source_gobj)` — `damage` is passed in `f1`; the 3rd arg is the GOBJ that dealt the hit (used by `Machine_OnDamageVisual` for the hit-spark direction — **must not be NULL in City Trial**), not a flags integer. Applies actual HP loss to a machine:
1. Adds `damage` to `MachineData.dmg_accumulator` (+0x6AC), clamped to a max constant
2. Calls `Gm_IsDamageEnabled()` (0x8000a188); if enabled, subtracts `damage` from `MachineData.hp` (+0xA18), clamped to a minimum
3. If HP reaches the minimum: enters the death sequence
4. If HP drops below a threshold fraction of max: applies warning color animation
5. Triggers visual damage effects via `Machine_OnDamageVisual` (reads `source_gobj`)

Note: `Machine_GiveDamage` does **not** itself cause knockback/bounce — pair it with `kb_mag` + `Machine_EnterHitReaction` (see Method 1).

### Machine_EnterHitReaction (0x801e05bc)

Transitions a machine into hit reaction state (state 5):
1. Saves previous state
2. Clears formation tracking
3. Calls `HurtData_UpdateVulnState` on the machine's HurtData
4. Registers the hit reaction with the system

## Vulnerability System

Three states controlled by `HurtData.vuln.kind`:

| Value | State | Effect |
|-------|-------|--------|
| 0 | Vulnerable | Normal — takes damage and knockback |
| 1 | Invincible | No damage, no knockback, but collision still detected |
| 2 | Intangible | No collision at all — hurtbox is "ghosted" |

### Timers

- **Intangibility** (`vuln.intang_timer` at +0x94): Set by `HurtData_UpdateIntangibility` (0x8018cb5c) — declared in `hurt.h` as `HurtData_GiveIntangibility` (map name kept for compatibility). Takes priority over invincibility. Counts down each frame.
- **Invulnerability** (`vuln.invuln_timer` at +0x98): Set by `HurtData_GiveInvincibility` (0x8018cc38). Active only when intangibility timer is 0.

### HurtData_UpdateVulnState (0x8018cb28)

Called each frame to refresh the vulnerability state:
```c
clear flag bit 7 at +0x9C;
if (intang_timer != 0) return;  // stay in current state
vuln.kind = (invuln_timer != 0) ? 1 : 0;
```

### Setting Timers

- `HurtData_GiveIntangibility(hurtdata, frames)` (map name: `HurtData_UpdateIntangibility`, 0x8018cb5c) — Sets intangibility for at least `frames` (only updates if new value > current). Sets `vuln.kind = 2`.
- `HurtData_GiveInvincibility(hurtdata, frames)` — Sets invulnerability for at least `frames`. Sets `vuln.kind = 1` (or 2 if intangibility is also active).

## Enemy-Specific Damage Mechanics

### Enemy HurtData Initialization

`EventActor_HurtDataCreate` (0x80201ee8) creates HurtData for enemies at EnemyData+0x410:
- **Meteor** (type 0x4D): `HurtData_Create(gobj, HURTKIND_3, 8, joint_count, 0)` — 8 attack regions
- **All other enemies**: `HurtData_Create(gobj, HURTKIND_3, 2, joint_count, 0)` — 2 attack regions
- Sets `on_damage_callback = EventActor_OnDamageCallback` (0x80201c78)
- Iterates model joints to initialize **defensive sub-regions** via `HurtData_InitRegion`

### Enemy TriggerData

Each enemy has a TriggerData at EnemyData+0x45C, initialized by `zz_80201ba4_` from actor_data descriptors. This contains the enemy's attack parameters (base_damage, knockback, etc.) loaded from the game's data files.

The TriggerData at +0x45C and the HurtData's attack regions at +0x410 both participate in the collision system. The attack regions (stride 0xC8) contain embedded HurtParams at offsets 0x04-0x34 that `getDamageDealt` and `HitColl_CalcKnockback` read during `HitColl_CheckCollision`.

### Enemy Receiving Damage (EventActor_ProcDamage, 0x801fc9f0)

Takes the enemy GObj; dereferences userdata (+0x2C) to EnemyData, and early-returns for meteor types (EnemyData+0x0C in [0x48, 0x4A]). Otherwise, reading the HurtData at EnemyData+0x410:
1. Reads kb_mag (HurtData+0x24) and dmg_taken (HurtData+0x28).
2. If kb_mag == 0 (sentinel): only calls `giveEnemyDamage(dmg_taken)` when dmg_taken is nonzero — cosmetic accumulator update, no launch.
3. If kb_mag != 0: clamps dmg_taken, calls `giveEnemyDamage`, then dispatches knockback through the enemy's **custom damage handler at EnemyData+0xAD0** if set, else the default `EnemyKnockback_Default` (0x8020bcd8) — a thin dispatcher that reads the hurt-entry type at `region+0x38` and calls `Enemy_ApplyKnockback`.
4. Always clears the damage-pending flag at EnemyData+0x9A0.

### Enemies Taking Damage (Knockback Tiers)

Enemies don't have HP. When hit, `Enemy_ApplyKnockback` (0x8020b784):

1. Scales the incoming damage (`trigger_params+0x08`) via `Enemy_ScaleDamage` (0x8020b71c), then classifies it into a tier (0–3) via `Enemy_ClassifyDamageTier` (0x8020b740). The thresholds come from the **global enemy parameter table**, reached through the SDA pointer at `r13+0x798` (0x805DD878, "DAT_805dd878"); the three thresholds live at table offsets +0x08, +0x0C, +0x10:
   - Tier 0: damage < `tbl[+0x08]`
   - Tier 1: `tbl[+0x08]` ≤ damage < `tbl[+0x0C]`
   - Tier 2: `tbl[+0x0C]` ≤ damage < `tbl[+0x10]`
   - Tier 3: damage ≥ `tbl[+0x10]`

   The tier is stored at EnemyData+0xA1C. (Threshold *values* are runtime-loaded data, not present in the vanilla mem1.raw snapshot.)

2. Sets the launch/stun frame count (EnemyData+0xA18) from the tier's base stun `tbl[tier*4 + 0x60]`, plus an extra term scaled by `tbl[tier*4 + 0x30]` and a per-enemy launch multiplier `*(actor_data→+0x00 + 0xA0)` (where `actor_data` = EnemyData+0x14).

3. Grants intangibility for the launch duration via `HurtData_UpdateIntangibility(EnemyData+0x410, frames)`.

4. Randomizes the three knockback sign components using `HSD_Randi(8)` (bits 0/1/2 pick +/− per axis).

5. Builds the knockback direction (from attacker position or trigger vectors, mode-dependent), then transitions the enemy to its knockback state via `EventActor_DisableRendering` (rendering disabled during launch).

Death occurs when the launch/stun counter at +0xA18 reaches 0 during the knockback state.

### Damage Accumulators

`giveEnemyDamage` (0x8020b680) adds damage to two accumulators at EnemyData+0x994 and +0x998 (capped at 9999 each). These are **cosmetic only** — nothing reads them for death logic.

## Applying Damage from Custom Code

### Method 1: Direct Damage + Hit Reaction (Bypassing Collision Pipeline)

For custom events that need to damage a player's machine directly:

```c
// Get the machine GObj (needed: source_gobj must be non-NULL in City Trial —
// Machine_OnDamageVisual reads its +0x20 forward vector for the hit-spark direction).
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

This skips the collision pipeline entirely. `Machine_GiveDamage` by itself does *not* cause knockback/bounce — set `kb_mag` and call `Machine_EnterHitReaction` for that. The live consumer is the **1 HP trap** in `mods/archipelago/src/ap_item_handler.c` (it calls `Machine_GiveDamage(md, md->hp - 1.0f, mg)` for each human player, passing the machine GObj as the source). Useful for scripted damage events.

### Method 2: Through the Collision Pipeline (HurtParams)

For entities that should deal damage through normal collision:

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

### Method 3: Modifying Enemy Attack Regions (Limitations)

Enemy attack data lives in the HurtData regions (stride 0xC8) at EnemyData+0x410. Each region has a HurtParams embedded at offset +0x04:

```
Region entry (0xC8 bytes):
  +0x00: int active          (0 = inactive, 1 = active)
  +0x04: HurtParams (0x34 bytes)  ← damage/knockback config
  +0x38: int shape_param
  +0x3C: ... position data, collision geometry ...
```

**However, region parameters are refreshed every frame** from animation data. The per-frame chain is: `zz_801ff520_` → `zz_80201ba4_` → `Trigger_SetState1` → `Trigger_InitParameters`, which reads from `*(*(enemyData+0x14) + 0x14)` (the current animation frame's hurt parameter data) and overwrites the region's HurtParams. Additionally, `zz_80200eb8_` parses packed binary collision data from animation streams into regions.

This means **writing to region entries directly won't persist** — changes are overwritten next frame. To customize enemy damage, use Method 1 (direct damage in a GOBJProc) instead.


## Key Function Reference

| Function | Address | Description |
|----------|---------|-------------|
| HitColl_Init | 0x8018cf64 | Clears collision log, sets victim hurt_data |
| HitColl_CheckCollision | 0x8018d284 | Core: iterates attacker regions vs victim sub-regions |
| getDamageDealt | 0x8018ace4 | Computes damage from region params + velocity |
| HitColl_CalcKnockback | 0x8018ab90 | Computes knockback from region params + velocity |
| HitColl_SetDamageLog | 0x8018cf94 | Logs collision: calculates damage/kb, stores in global log |
| HitColl_ActOnCollision | 0x8018d878 | Resolves log → strongest knockback → HurtData |
| HitColl_CalcContactPoint | 0x8018a5b8 | Computes contact point between two collision shapes |
| HitColl_CalcKnockbackDir | 0x8018ab10 | Computes knockback direction from contact data |
| HitColl_ClearLogEntry | 0x80189e3c | Clears collision log entries matching attacker kind |
| HitColl_ResolveLogEntry | 0x8018db10 | Retrieves entry data for Machine_ActOnHitCollision |
| HurtData_CheckVulnerability | 0x8018cd9c | Returns non-zero if target is protected |
| Machine_ApplyHurt | 0x8018d1a8 | Applies hurt via collision system from HurtParams (calls Trigger_InitParameters then HitColl_SetDamageLog). Also aliased `Machine_ApplyHurtFinal` = HitColl_SetDamageLog in machine.h |
| Machine_GiveDamage | 0x801e1ee8 | `(md, float damage, GOBJ *source_gobj)` — subtracts HP, triggers death at min HP. source_gobj must be non-NULL in City Trial. No knockback by itself |
| Machine_EnterHitReaction | 0x801e05bc | Enters bounce/hit state 5 |
| Machine_UpdateHitColl | 0x801c67a0 | Per-frame pipeline orchestrator (machine side) |
| Machine_CheckEventCollision | 0x801d71ec | Checks enemy/event actor collision (p_link 12) |
| Machine_ActOnHitCollision | 0x801d7308 | Reacts to strongest hit: attacker ID + hit reaction |
| Machine_InitHurtData | 0x801d6e84 | Creates HurtData for a machine |
| Rider_InitHurtData | 0x80196170 | Creates HurtData for a rider |
| EventActor_HurtDataCreate | 0x80201ee8 | Creates HurtData for an enemy/event actor |
| EventActor_ProcHitColl | 0x801fc8ec | Enemy per-frame hitcoll processing (receiving damage) |
| EventActor_ProcDamage | 0x801fc9f0 | Processes received damage → knockback (via EnemyData+0xAD0 custom handler or EnemyKnockback_Default) |
| EnemyKnockback_Default | 0x8020bcd8 | Default enemy knockback dispatcher (region+0x38 type → Enemy_ApplyKnockback). |
| EventActor_OnDamageCallback | 0x80201c78 | Enemy on-damage callback (set in HurtData+0x8C) |
| Trigger_ClearParameterStruct | 0x8018a0c0 | Zeros a HurtParams (0x34 bytes) |
| Trigger_InitParameters | 0x8018a118 | Copies HurtParams into region/trigger offsets 0x04-0x34 |
| HurtData_Create | 0x8018c1c8 | Allocates HurtData from object pools |
| HurtData_InitRegion | 0x8018c598 | Initializes a defensive sub-region from joint data |
| HurtData_UpdatePerFrame | 0x8018c4e8 | Per-frame position/radius update |
| HurtData_GiveIntangibility (map: HurtData_UpdateIntangibility) | 0x8018cb5c | Sets intangibility timer |
| HurtData_GiveInvincibility | 0x8018cc38 | Sets invulnerability timer |
| HurtData_UpdateVulnState | 0x8018cb28 | Refreshes vuln.kind from timers |
| Enemy_ClassifyDamageTier | 0x8020b740 | Classifies damage into tier 0-3 |
| Enemy_ApplyKnockback | 0x8020b784 | Full enemy knockback sequence |
| Enemy_ScaleDamage | 0x8020b71c | Scales damage by global factor from enemy param table |
| giveEnemyDamage | 0x8020b680 | Adds to cosmetic damage accumulators |
| EnemyActor_RumblePlayer | 0x801ff80c | Triggers controller rumble (NOT a damage function) |
