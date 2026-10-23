# Copy Abilities

The engine's copy-ability lifecycle for a rider: how an ability is granted, held and ticked each
frame, and torn down. A modder needs it to grant, strip, gate or detect an ability. A mod that gates
abilities hooks the two commit points: `RiderGObj_CheckAndGiveAbility` for pickups and
`randomAbility_giveAbility` for the wheel.

Copy abilities exist only in the 3D modes (City Trial and Air Ride), where the rider is a
`RiderData` with a `copy_kind`. Top Ride creates no `RiderData`, so no copy ability can be obtained
there. Its analogs are the four ability-power items (Fire, Freeze Fan, Bomb, Walky), each a timed
Kirby state.

## Kinds and fields

There are 11 `CopyKind`s: FIRE (0), TIRE (1, shown as Wheel), SLEEP (2), SWORD (3), BOMB (4),
PLASMA (5), NEEDLE (6), MIKE (7, shown as Mic), ICE (8, shown as Freeze), TORNADO (9) and BIRD
(10, shown as Wing). `COPYKIND_NONE` is -1.

| `RiderData` | Offset | Meaning |
|---|---|---|
| `copy_kind` | `0x454` | Held `CopyKind`, or -1 |
| `queued_ability_kind` | `0x458` | Ability queued while the rider cannot take one yet |
| `powerup_kind` | `0x45c` | Held City Trial power-up; it shares the teardown path |
| `cb_status_remove` | `0x7f8` | Teardown installed by the current status; every `RiderStateChange` clears it |
| `cb_ability_remove` | `0x7fc` | Teardown that persists across statuses |
| `copy_wheel_jobj` | `0x8fc` | The ability's animated model (also the Copy Chance Wheel while it spins), with its heap block at `0x904` |
| `copy_timer` | `0x91c` | Countdown; the ability expires at 0 |
| `x920` | `0x920` | Warning threshold: the expiry blink fires when `copy_timer` drops below it |
| `cb_ability_tick` | `0x92c` | Per-frame ability callback; not every kind installs one |
| `ability_gobj` | `0x3f0` | The weapon GObj of an aura ability (Fire, Needle, Freeze) |

Both teardown slots are live. Most grant functions enter a GET status and fill `cb_status_remove`
for its duration (Fire, Sword, Bomb, Freeze, and the inhale statuses). The persistent
`cb_ability_remove` is filled at grant by kinds such as Wing, Wheel and Mic, and by Fire and Sword
once their GET status ends. `Rider_AbilityRemoveModel` (0x80191554) calls `cb_status_remove` and then `cb_ability_remove`,
skipping whichever is NULL, when `copy_kind` or `powerup_kind` is set. That makes it the safe way to
trigger teardown; never call a slot directly.

## Where abilities come from

| Source | Path |
|---|---|
| Copy panel item (floating panel, box or event drop) | `Machine_OnTouchItem` (0x801db34c) -> `RiderGObj_CheckAndGiveAbility` (0x80192650, Kirby only) -> `Rider_GiveAbility` |
| Swallowed enemy | `randomAbility_queuedGive` (0x801aec60), once the swallow animation ends |
| Ground copy panel (collision zone kind 15) | `Machine_ProcessEnvColl` (0x801e5108) -> `RiderGObj_GiveRandomAbility` (0x80191fb8) -> `Rider_StartRandomCopyWheel` (0x801ae4ec) |

`RiderGObj_CheckAndGiveAbility` is the only non-wheel caller of `Rider_GiveAbility`, so it is the
gate point for pickups. A swallowed enemy normally starts the Copy Chance Wheel on that enemy's
ability. It is granted directly through `randomAbility_giveAbility` (0x801a61d4) instead when a wheel
is already up (`RiderData + 0x825` bit 0x01 or 0x02) or `RiderData + 0x924` is 1.

**The wheel.** `Rider_StartCopyWheel(rd, start)` (0x801ae550) spins from `start % 11` over one of two
`CopyWheelTable` lists. `stc_copy_wheel_normal` (0x804af730) is the 11 kinds in order.
`stc_copy_wheel_melee` (0x804af738) is used under the Kirby Melee rule and in one other case keyed on
a `GameData` flag. It is a 29-entry list holding nine kinds three times each, and Bomb and Mic once.
`randomAbility_mainLoop` (0x801a5fb8) runs the wheel through `cb_copy_input`.
`randomAbility_aPress` (0x801ae7f4) stops it on A and `randomAbility_autoSelect` (0x801ae890) on
timeout, and both commit through `randomAbility_giveAbility`.

**Ground panels.** `Machine_ProcessEnvColl` starts the wheel when the machine enters a kind-15 zone
other than the one it last recorded (`MachineData + 0x7e8`). `Rider_StartRandomCopyWheel` strips
the held ability and spins from `HSD_Randi(11)`. The same call site then bumps
`PlayerStats.copy_chance_on_tree` (`+0x7f0`, a Celestial Valley checklist counter) whether or not a
rider got an ability. City Trial bakes one kind-15 zone into `GrCity1.dat`. Air Ride courses bake
none: their panels are yakumono placed at load that add their own zones. Nebula Belt places four
and Celestial Valley one, on top of the tree.

**Stadium pools.** Only four grounds ship an item node (`GrData.item`, `+0x2c`): `GrCity1`,
`GrColosseum1`, `GrColosseum3` and `GrDedede1`. `fn_grGetItemData` (0x800da518) picks the node by the
stage's ItemposId (`Stage.dat` row `+0x24`). The open city uses the full table. The stadiums use
`pool_a` / `pool_b`, filed into the box pools by `CityItemSpawn_InitStadiumPoolsA` (0x800ed8b0) /
`B` (0x800eda0c) with one of three chance columns rolled per round. Every other stadium and every
Air Ride course has no item node, so it has no copy panel items:

| Stadium | Ground (ItemposId) | Copy panels with a nonzero chance |
|---|---|---|
| Destruction Derby 1 | `GrColosseum1` (0) | All 11 |
| Destruction Derby 2 | `GrColosseum3` (0) | All 11 |
| Destruction Derby 3 | `GrDedede1` (0) | All but Wheel; Sleep, Wing, Tornado and Mic in columns 1-2 only |
| Destruction Derby 4 | `GrCity1` (1) | All 11; Wing in columns 1-2 only |
| Destruction Derby 5 | `GrCity1` (2) | Freeze, Plasma, Sword and Needle only, same in all three columns |
| Vs. King Dedede | `GrColosseum1` (1) | All 11 |

## Grant

`Rider_GiveAbility(RiderData *, CopyKind)` (0x801a81a4) is the master entry:

1. If `Rider_CheckUnableAbility` (0x80191798) says the rider cannot take an ability now, the kind
   is stashed in `queued_ability_kind` (only when nothing is queued already) and 0 is returned.
   `Rider_ResolveQueuedAbility` (0x801a8454) grants it later.
2. `COPYKIND_NONE` spits a star instead (`RiderState_InhaleStarSpitEnter`) and returns 0.
3. The grant function comes from `stc_ability_init_table` (0x804af4f0, 11 entries by `CopyKind`). A
   NULL entry returns 0.
4. `Rider_AbilityRemoveModel` strips the held ability, and `Rider_AbilityClearQueued` (0x801915c4)
   frees any queued model and resets both queue fields to -1.
5. `Ply_RecordCopyAbility(ply, kind)` (0x8022ee00) records the grant.
6. The grant function runs, and 1 is returned.

| CopyKind | Grant function | Address |
|---|---|---|
| FIRE | `ability_Fire` | 0x801af474 |
| TIRE | `ability_Tire` | 0x801af950 |
| SLEEP | `ability_Sleep` | 0x801b0bf0 |
| SWORD | `ability_Sword` | 0x801afd54 |
| BOMB | `ability_Bomb` | 0x801b11d4 |
| PLASMA | `ability_Plasma` | 0x801b2a4c |
| NEEDLE | `ability_Needle` | 0x801b3688 |
| MIKE | `ability_Mike` | 0x801b3dac |
| ICE | `ability_Ice` | 0x801b454c |
| TORNADO | `ability_Tornado` | 0x801b4a3c |
| BIRD | `ability_Bird` | 0x801b5480 |

A grant function moves the rider into the ability's hold state with `RiderStateChange`
(0x8018e580). It also restyles the speedometer (`ability_ChangeSpeedometerDesign`, 0x801a809c),
swaps in the ability model and hat, arms `copy_timer`, and installs the ability's callbacks.

**Recording.** Every grant path records through `Ply_RecordCopyAbility`. Its callers are
`Rider_GiveAbility`, `Rider_ResolveQueuedAbility`, a second queued-grant path at 0x801a8630, and
`randomAbility_giveAbility` (0x801a61d4) for the Copy Chance Wheel. The function:

- appends the kind to the six-entry `PlayerStats.copy_history` (`+0x360`, oldest first, the oldest
  dropped once full). The entry count is the high 5 bits of `copy_history_num` (`+0x378`), and the
  three ability-sequence flags are its low 3 bits;
- checks the sequence tables at 0x804b4c20, 0x804b4c38 and 0x804b4c50;
- bumps `PlayerStats.copy_obtain_count[kind]` (`+0x334`).

`Ply_MarkCopyAbilityObtained` (0x8022f150) is separate. Only the wheel's `randomAbility_aPress`
(0x801ae7f4) and `randomAbility_autoSelect` (0x801ae890) call it. It sets bit `15 - kind` of
`PlayerStats.copy_chance_mask` (`+0x37a`), so that mask means "the Copy Chance Wheel granted this
kind", while `copy_obtain_count` counts grants from every source.

## Per-frame tick

While the rider is in an ability state, `abilityTimerBranchToAbilityCountdown` (0x801a5f68) runs
each frame. It is entry `+0x14` of the action-state callback block at 0x804af4a0. It advances
`copy_wheel_jobj`'s animation if there is one, then calls `cb_ability_tick` if set.

`cb_ability_tick` is the kind's `abilityTimer_*` (for example `abilityTimer_Fire` 0x801aee28,
`abilityTimer_Sword_checkIf0` 0x801afb30, `abilityTimer_Bird_checkIfZero` 0x801b5660). It calls
`abilityTimerDecreaser` (0x801a7c84), which on Kirby counts `copy_timer` down. Once the timer drops
below the `x920` threshold, it fires the warning blink (`Rider_ApplyColAnim(rd, 0x2b, 0)`) and
zeroes the threshold so the blink fires once. The tick then checks its expiry condition and runs
the drop when `copy_timer` reaches 0 or the ability's fuel or ammo runs out.

## Teardown

Teardown is layered, each layer calling the next:

1. **The kind's revert** - `Fire_LoseAbility_Exit` (0x801af330), `abilityTimer_Sword_revertModel`
   (0x801afc70), `abilityTimer_Bomb_remove` (0x801b13ec) and their siblings. It frees the ability's
   own state (live projectiles, attached models).
2. **`Rider_RevertCopyAbility`** (0x801a7d70) frees `copy_wheel_jobj` and its heap block, calls the
   core, then zeroes `cb_ability_tick`, `cb_ability_remove` and the other ability callback fields.
3. **`Rider_TeardownCopyAbility`** (0x801a810c), the core, sets `copy_kind = -1` and plays the
   ability-lost effects: the poof (`Effect_SpawnSync` 0x80236c40, effect 0x3a990), the loss sound
   and the fade ColAnim 0x2c.

`RiderState_LoseAbilityEnter` (0x801b0adc) is only the spit-out animation: it enters action state
0x68 and tears nothing down. On its own the ability stays equipped.

An ability leaves in one of two ways:

- **Expiry or use-up.** The tick sees the end condition, runs the kind's revert, then
  `RiderState_LoseAbilityEnter` for the spit.
- **Replacement.** A new grant calls `Rider_AbilityRemoveModel` to strip the old ability, with no
  spit, before running the new grant function.

## Forcing a drop

To discard the held ability from mod code, reproduce the expiry with the universal remover so it
works for any kind:

```c
if (rd->copy_kind != COPYKIND_NONE)
{
    Rider_AbilityRemoveModel(rd);     // teardown: copy_kind = -1, poof, model and hat removed
    RiderState_LoseAbilityEnter(rd);  // spit-out animation
}
```

`Rider_AbilityRemoveModel` clears `copy_kind`, so a per-frame trigger that re-checks it fires once.
It also strips a held power-up, because power-ups share the teardown slots.

## Mic

Mic's hold pose is action state 0x3e. An attack press enters the singing blast, 0x61, which falls
into the recovery 0x62 once its animation ends. Neither the grant nor the blast installs a hitbox
of its own, so the blast carries no attack cause of its own the way Tornado (0x0e) and Quick Spin
(0x10) do. Identifying a Mic kill means reading `copy_kind` together with the live action state.
