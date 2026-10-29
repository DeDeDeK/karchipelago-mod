# HurtData and Hit Collision

How entities damage each other: machine against machine, machine against enemy, projectiles
against anything. Every damageable object owns a `HurtData` holding its attack hitboxes and its
hurtboxes, and a per-frame pass logs every overlap, scales damage and knockback, keeps the
strongest hit and hands it to the owner. A modder needs this to deal damage, give or take away a
hitbox, make something invulnerable, or read who hit whom. Collision with the stage (floors,
walls, raycasts) is a separate system that shares nothing with this one.

## HurtData

Each `HurtData` carries two arrays. **Attack regions** (`HitRegion`, stride 0xC8) are hitboxes
with their own damage and knockback parameters. **Sub-regions** (stride 0x44) are the defensive
hurtboxes, built by `HurtData_InitRegion` (0x8018c598) from model joints - a joint index, an
offset and a radius scale each.

A machine's hurtboxes come from its `vcData` (`+0x08`, a `{descriptors, count}` pair read by
`Machine_InitHurtData`, 0x801d6e84), with each radius scaled by `MachineData.model_scale`. Every
vanilla machine has one: a sphere of radius 2.27 on the Warp Star (4.2 on the Hydra, 0.95-1.95 on
the bikes), centered on the machine body. It is much smaller than the machine and its rider look,
and a mounted rider has no hurtbox of its own, so an attack has to reach that sphere.

| Owner | HurtData | Attack regions | p_link | GetHurtData |
|---|---|---|---|---|
| `MachineData` | +0x660 | 4 | 9 | `MachineGObj_GetHurtData` (0x801c8660) |
| `RiderData` | +0x390 | 4 | 10 | `RiderGObj_GetHurtData` (0x80192788) |
| `EnemyData` | +0x410 | 2 (Dyna Blade 8) | 12 | `EventActorGObj_GetHurtData` (0x80204878) |
| `YakumonoData` | +0xEC | 2 | 8 | `YakumonoGObj_GetHurtData` (0x800f8248) |
| `ItemData` | +0x148 | varies | 13 | - |
| `WeaponData` | +0x108 | 2 | 14 | accessor at 0x80223120 |

`HurtData.kind` is a `HurtKind`: 0 rider, 1 ridden machine, 2 empty machine, 3 event actor
(enemies, Dyna Blade), 4 item, 5 projectile, 6 stage object. A machine is created as 2; mounting
switches it to 1 (0x801c82f8) and `setEmptyVehicleHitbox` (0x801c8384) switches it back. The
fields the rest of the game reads are `kb_mag` (+0x24), non-zero when the owner was hit this
frame, and `dmg_taken` (+0x28), the frame's accumulated damage.

### Attack regions

A `HitRegion` starts with a `state` word (0 = off), then a 0x34-byte `HurtParams` copy at +0x04
that `Trigger_InitParameters` (0x8018a118) fills, then the joint it follows (+0x38) and its
world position, radius and previous position. `MachineData`, `ItemData` and `RiderData` each
also embed a `TriggerData`, which wraps one `HitRegion` for their touch hitbox.

| HurtParams field | Meaning |
|---|---|
| `base_damage` (int) | Damage at zero relative speed |
| `dmg_distance_factor` | Damage added per unit of relative speed; 0 = fixed damage |
| `offset`, `radius` | Placement relative to the joint |
| `base_knockback`, `kb_distance_factor` | Knockback, same shape as damage |
| `hit_flags` (word) | Top 5 bits: hurt type (8 = skip), which the victim's reaction reads. `(w >> 10) & 0x7f`: victim mask, tested against `1 << victim kind`. `((w & 0xffff) >> 2) & 0xff`: rehit interval in frames |
| `x30` top byte | 0xe0: region group; 0x08: ignore this region's own velocity |

Two more fields of the same word gate the region: bit 0x1 (byte `+0x33`) disables it, and bits
0x20000 and 0x40000 (byte `+0x31`) admit victims of vulnerability class 0 and 1 (class 2 always
passes). Each region keeps 12 `{victim, frames}` pairs at +0x68, so it hits a given victim once
per rehit interval; the hit is recorded in every region of the same group, and recorded even
when the victim turns out to be invulnerable.

Per-frame velocity for the damage formula comes from the owner's `HurtData.pos_tracker` (+0x68),
which `HurtData_UpdatePerFrame` (0x8018c4e8) rolls each frame; the region's own position pair is
the fallback when there is no tracker.

## One frame of hit collision

The check is one-directional: each owner's per-frame pass treats **its own HurtData as the
victim** and walks other owners' GObj lists (`stc_gobj_lookup + p_link*4`) for attackers whose
regions overlap it. Nothing delivers an attack outward. The machine pass,
`Machine_UpdateHitColl` (0x801c67a0), is the model:

1. `HitColl_Init` (0x8018cf64) clears the global log and makes the machine the victim.
2. The sub-checks each walk one list and call `HitColl_CheckCollision` against every attacker:
   riders `Machine_CheckRiderCollision` (0x801d6fd0), machines (0x801d706c), projectiles
   (0x801d7118), event actors `Machine_CheckEventCollision` (0x801d71ec), items (0x801d7248),
   stage hazards `Machine_CheckStageHazardCollision` (0x801d72a4), then the side-bump, item
   touch and patch passes.
3. `HitColl_ActOnCollision` (0x8018d878) keeps the logged hit with the largest knockback and
   writes it into the victim's `kb_mag`, `hit_pos` and attacker fields.
4. `Machine_ActOnHitCollision` (0x801d7308) credits the attacker when `kb_mag != 0`.

The priority-10 proc `Machine_DmgApply` (0x801c6834) then applies it.

Because the check is victim-side, an enemy's attack on a rider is delivered by the machine:
`Machine_CheckEventCollision` reads each enemy's attack regions against the machine's HurtData.
A mounted rider takes enemy contact damage only through its machine. `Rider_UpdateHitColl`
(0x8018f95c) runs only while the rider is off its machine; its event-actor check
(`Rider_CheckEventCollision`, 0x8019649c) hits a rider on foot directly, adding to the rider's
on-foot damage (`RiderData+0x49c`) and `PlayerStats.onfoot_dmg_total`.

The enemy's own pass, `EventActorGObj_ProcHitColl` (0x801fc8ec, priority 9), is likewise inbound
only: riders, machines, projectiles, other actors and stage hazards against the enemy.

### HitColl_CheckCollision (0x8018d284)

`r3` is the victim, `r4` the attacker. For each active attacker region it tests the victim mask,
the vulnerability-class filter and the region's victim list, then runs `Hit_CheckOverlap` (a
swept-sphere or capsule test) against each of the victim's sub-regions, and calls
`HitColl_SetDamageLog` (0x8018cf94) on overlap.

### HitColl_SetDamageLog (0x8018cf94)

`HitColl_GetDamageDealt` (0x8018ace4) returns `base_damage` alone when `dmg_distance_factor` is
0, otherwise `base_damage + dmg_distance_factor * |relative velocity|` clamped to at least 1.
`HitColl_CalcKnockback` (0x8018ab90) has the same shape without the clamp. The relative velocity
is the attacker's frame displacement minus the victim's.

The damage is multiplied by the attacker's `dmg_scale` (+0x7C), which also updates the
attacker's `max_dmg_dealt` / `max_dmg_victim`. Knockback and logged damage are then multiplied
by `1 - dmg_reduction` of the victim (+0x80). For a machine that is `MachineData+0x4EC`, the
Defense-scaled `base_defense` attribute (0.2 on Warp Star, 0.5 on Hydra).

The hit is **skipped** when the scaled knockback is at or below 0, when
`HurtData_CheckVulnerability` (0x8018cd9c) reports the victim protected, or when the entry is
already pending. Otherwise it is appended to `stc_hitcolldata.log` (20 entries) and adds to
`dmg_taken`. The victim's `on_damage_callback` (+0x8C) runs at the end either way, with a
"logged" flag.

## Damage on a machine

`Machine_ActOnHitCollision` switches on the attacker's `HurtKind` to record who hit the machine:

| Attacker | Recorded |
|---|---|
| Rider (0), ridden machine (1) | The player; a machine's attack word comes from `MachineGObj_GetAttackerLog` |
| Empty machine (2), item (4) | Nothing |
| Event actor (3) | Dyna Blade (0x4D) hitting with region 1 or 2 sets the victim's trampled-by-Dyna-Blade bit |
| Projectile (5) | The owner (`WeaponGObj_GetOwnerPly`, 0x802230c4); the attack word is the projectile kind's |
| Stage object (6) | Special cases for yakumono kinds 0x3D and `YAKUKIND_RAILFIRE` |

`Machine_StoreAttacker` (0x80231d90) credits the player, keyed on the attack word's low byte:
causes 1-0x1A always count, any other only while bit 0x8000 is set.

`Machine_DmgApply` passes `dmg_taken` to `Machine_GiveDamage` and then
`Machine_DispatchHitReaction` (0x801e2620), which stores the attacker's hurt type and a duration
and strength tier from `kb_mag`, and enters one of hit sub-states 4-11 by hurt type.

`Machine_GiveDamage(md, damage, hit)` (0x801e1ee8) is HP only. It adds to
`MachineData.dmg_accumulator` (+0x6AC), subtracts from `hp` (+0xA18) when `Gm_IsDamageEnabled`
(0x8000a188), starts the death sequence at 0 HP and plays the low-HP flash. It then calls
`Machine_DropPatchesOnDamage` (0x801e09ac): in the city with a rider aboard, damage above
`RiderCommonParam.patch_drop_damage_min` (8.0, from `RdCommon.dat`) knocks patches out. `hit` is
the machine's hit record, `&hurt_data->hitcoll_log_idx`; the drop path dereferences it (for a
deflect direction it then ignores), so it must not be NULL. No bounce or knockback comes from
this call.

## Machine attack regions

| Region | Role | Live while |
|---|---|---|
| 0 | spins | an action script arms it: quick spin and the forced spin, about 44 frames each |
| 1 | boost | 2 frames from the first frame of each charge boost |
| 2 | Candy | Candy is active and a rider is aboard |
| 3 | ram | a rider is aboard, no charge or hit state blocks it, and speed >= 0.6075 per frame |

Regions 1-3 are armed only while `Gm_IsMachineHitboxEnabled` (0x8000a200, `GameData+0xaa5` bit
0) returns 1. `CityTrial_Init` sets it; `Stadium_ApplyDescConfig` copies it from the stadium's
entry in `GmData.dat`, where it is clear for every stadium. So ram, boost and Candy damage exist
only in the city. Region 0 has no switch, so spins hit everywhere.

`Machine_UpdateMovingHitbox` (0x801d7604) drives regions 1-3 each frame. Candy or a live boost
takes the frame; otherwise the ram region is armed as a sphere (vcAttributes +0x70) centered
ahead of the machine along `world_velocity` (+0x74), so only the front hits. Side and rear
contact go through `Machine_CheckMachineBumpCollision` (0x801daac4), which pushes the machines
apart with knockback but deals no damage.

All three set `base_damage = MachineData+0x46c * damage attr` and a velocity factor of `24.6857
* factor attr` each frame, with knockback in the same form; `MachineData+0x46c` is vcAttributes
+0x0c after stat scaling. A Warp Star ram deals `20 + 12.3 * |dv|` damage before the victim's
reduction. Their templates in `VcCommon.dat` share hit word 0x0017ec78: hurt type 0, victim mask
0x7b (everything but an empty machine), 30-frame rehit.

| Region | Size | Offset | Damage | Damage factor | Knockback | Knockback factor |
|---|---|---|---|---|---|---|
| 1 boost | +0x11c | +0x120 along facing | +0x124 | +0x128 | +0x12c | +0x130 |
| 2 Candy | +0x108 | centered | +0x10c | +0x110 | +0x114 | +0x118 |
| 3 ram | +0x70 | +0x74 along velocity | +0x78 | +0x7c | +0x80 | +0x84 |

While the updater holds attack-word bit 0x8000, a ram, boost or Candy hit credits the rider with
cause 0, for rivals through `Machine_StoreAttacker` and for enemies through
`Ply_RecordEnemyDefeat`. Spin hits carry `attack_log` 0x110 and are credited as cause 0x10,
Quick Spin.

Kirby's push-start states (`RDSTATE_READYPUSHSTART`, `RDSTATE_PUSHSTART`) arm a rider region-0
hitbox for one frame - base damage 2, victim mask 0x78 (event actors, items, projectiles, stage
objects) - with attack log 0, so it knocks enemies out without crediting anyone.

## Vulnerability

`HurtData.vuln.kind` is 0 vulnerable, 1 invincible (collision detected, no damage or knockback)
or 2 intangible (no collision at all). Two timers drive it, both counting down:
`HurtData_GiveIntangibility` (0x8018cb5c) sets `intang_timer` (+0x94) and kind 2, only ever
raising the timer; `HurtData_GiveInvincibility` (0x8018cc38) sets `invuln_timer` (+0x98) and
kind 1, which takes effect only while the intangibility timer is 0. `HurtData_UpdateVulnState`
(0x8018cb28) recomputes the kind from the timers each frame. Candy invincibility is a separate
flag, set by `HurtData_SetCandyInvincible` (0x8018cbc8) and cleared by
`HurtData_ClearCandyInvincible` (0x8018cbe8).

## Enemy attack regions

An enemy's attack regions come from its animation script. Command 12
(`EnemyAnimCmd_SetHitDesc`, 0x80200eb8) decodes a hit descriptor into a `HurtParams` and arms
the region through `EventActor_EmitHitDesc` (0x80201e18) and `HurtData_SetAttackRegion`
(0x8018c8c4); command 13 (`EnemyAnimCmd_DisableHit`, 0x80201138) turns one off through
`Hit_SetInactive` (0x80189d1c). The script runs once per state, so a region keeps its
parameters until the next state change. `EventActor_ChangeState` then calls
`HurtData_ClearStateRegions` (0x8018c824), which deactivates the regions the script armed, and
the new state's script arms them again with a fresh victim list.

A mod can retune or switch off a live enemy region by writing `HurtData.regions` before the
machines' hit pass at priority 9 - from the actor's `trigger_cb` (priority 7), say - and must
repeat the write after every state change.

`EnemyData.hit_region` (+0x45C) is not an attack region. It is the touch region the inhale scans
test, placed once at create by `EventActor_RefreshAttackParams` (0x80201ba4) with an offset and
radius and no damage.

## Dealing damage from mod code

- **HP only.** `Machine_GiveDamage(md, amount, &md->hurt_data->hitcoll_log_idx)` takes HP with
  no visible reaction. Above 8 in the city it also knocks patches out, as a vanilla heavy hit
  does.
- **A logged hit.** `Machine_ApplyHurt(hurt_data, slot, &params)` (0x8018d1a8) runs
  `Trigger_InitParameters` and `HitColl_SetDamageLog` from a `HurtParams` (zeroed with
  `Trigger_ClearParameterStruct`, 0x8018a0c0, then `base_damage`, `base_knockback` and the two
  factors filled in) against a static dummy attacker. That only logs the hit. Called from a proc
  of its own it does nothing: `HitColl_Init` clears the log at the start of the victim's next
  pass, before `HitColl_ActOnCollision` reads it. The hit has to be logged inside the victim's
  pass, between `HitColl_Init` and `HitColl_ActOnCollision`, for the normal resolution -
  attacker bookkeeping, `Machine_DmgApply`, the bounce - to run on it.

## Key functions

| Function | Address | Role |
|---|---|---|
| `HurtData_Create` | 0x8018c1c8 | Allocates a HurtData from the pools |
| `HurtData_UpdatePerFrame` | 0x8018c4e8 | Places regions and rolls the position tracker |
| `HitColl_CheckCollision` | 0x8018d284 | Attacker regions against victim sub-regions |
| `HitColl_SetDamageLog` | 0x8018cf94 | Damage and knockback, logs the hit, fires the callback |
| `HitColl_ActOnCollision` | 0x8018d878 | Keeps the strongest logged hit |
| `Machine_UpdateHitColl` | 0x801c67a0 | Machine-side per-frame pass |
| `Machine_DmgApply` | 0x801c6834 | Applies a logged hit: damage, then the hit reaction |
| `Machine_GiveDamage` | 0x801e1ee8 | HP and patch drops, no reaction |
| `Machine_ApplyHurt` | 0x8018d1a8 | Logs a hit from a `HurtParams` |
| `HurtData_SetAttackRegion` | 0x8018c8c4 | Arms one attack region from a `HurtParams` |
| `HurtData_ClearStateRegions` | 0x8018c824 | Deactivates the state's attack regions on a state change |
| `Machine_UpdateMovingHitbox` | 0x801d7604 | Drives the boost, Candy and ram regions |
