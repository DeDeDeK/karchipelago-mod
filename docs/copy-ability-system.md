# Copy Ability System

The engine's copy-ability lifecycle for a rider: how an ability is granted, held and ticked each frame, and torn down. This is the mechanics layer beneath the AP gating, which decides *whether* an ability may be obtained. The `copy_kind` system is a `RiderData` field and exists only in the 3D modes (City Trial / Air Ride); Top Ride is a separate object system (`TopRideKirby`, no `copy_kind`) that creates no `RiderData` at all, so no copy ability can be obtained there - its analogs are the four ability-power *items* (Fire, Freeze Fan, Bomb, Walky), each a timed Kirby state.

There are 11 `CopyKind`s (`rider.h`): FIRE(0), WHEEL(1), SLEEP(2), SWORD(3), BOMB(4), PLASMA(5), NEEDLE(6), MIC(7), FREEZE(8), TORNADO(9), BIRD(10), plus `COPYKIND_NONE` = -1. The currently-held kind lives in `RiderData.copy_kind` (+0x454); a queued-but-not-yet-granted kind in `queued_ability_kind` (+0x458).

## RiderData Fields

| Offset | Name | Meaning |
|--------|------|---------|
| 0x454 | `copy_kind` | Held `CopyKind`, or -1 (`COPYKIND_NONE`). |
| 0x458 | `queued_ability_kind` | Ability queued while the rider can't yet receive one. |
| 0x45c | `powerup_kind` | Held power-up; also checked by `Rider_AbilityRemoveModel`. |
| 0x7e4 | - | Per-ability helper installed alongside +0x7f8 by most grant functions. |
| 0x7f8 | `cb_ability_remove2` | Teardown callback slot A. |
| 0x7fc | `cb_ability_remove` | Teardown callback slot B. |
| 0x8fc | `copy_wheel_jobj` | Ability's animated JObj (also the copy-chance wheel model while it spins); advanced each frame by the tick. |
| 0x904 | - | Heap allocation backing the ability model; freed with the JObj. |
| 0x91c | `copy_timer` | Countdown; the ability expires at 0. |
| 0x920 | - | "About to expire" threshold: when `copy_timer` drops below it the warning blink fires. |
| 0x92c | `cb_ability_tick` | Per-frame ability callback (the `abilityTimer_*` for this kind). Not every kind installs one. |

Both teardown slots are live. Most grant functions fill **+0x7f8** plus the +0x7e4 helper - Fire `0x801af618` / `0x801af5d4`, Sword `0x801aff1c` / `0x801afed8`, Bomb `0x801b13ac` / `0x801b138c`, Freeze `0x801b46f8` / `0x801b46b4` - while Wing (`ability_Bird`) instead fills **+0x7fc** (`0x801b5734`) with a +0x7e8 helper (`0x801b5730`). Later lifecycle paths install into +0x7fc too: the queued-give path stores `0x801af408` there for Fire (site `0x801aedc0`) and `0x801afd04` for Sword (site `0x801afb10`). `Rider_AbilityRemoveModel` calls +0x7f8 first and then +0x7fc, skipping whichever is null, so it is the only safe way to trigger teardown - never call a slot directly.

## Grant

`Rider_GiveAbility(RiderData*, CopyKind)` (0x801a81a4) is the master entry:

1. `Rider_CheckUnableAbility` (0x80191798) - non-zero means the rider can't receive one right now, so the kind is stashed in `queued_ability_kind` (only if both `queued_ability_kind` and `queued_powerup_kind` are -1) and granted later.
2. Look up the grant function in `stc_ability_init_table` at **0x804af4f0** (11 entries indexed by `CopyKind`); a null entry aborts with a 0 return.
3. `Rider_AbilityRemoveModel` (0x80191554) - strip the currently-held ability.
4. `Rider_AbilityClearQueued` (0x801915c4) - frees the pending queued objects (+0x8fc / +0x904) and resets `queued_ability_kind` / `queued_powerup_kind` to -1.
5. `Rider_RecordCopyAbility(ply, kind)` (0x8022ee00) - appends to the 6-entry ability history (`PlayerStats+0x360`, count and the three sequence flags packed into `+0x378`), tests the sequence tables at 0x804b4c20 / 0x804b4c38 / 0x804b4c50, and bumps the per-kind grant counter `PlayerStats.copy_obtain_count[kind]` (`+0x334`).
6. Call the per-kind grant function; return 1.

`Rider_MarkCopyAbilityObtained` (0x8022f150) is **not** part of this sequence - only the copy-wheel callers invoke it (`randomAbility_aPress` at 0x801ae874, `randomAbility_autoSelect` at 0x801ae910). It sets `PlayerStats.copy_chance_mask` (`+0x37a`), MSB-first bit `15 - CopyKind`, so that mask means "the Copy Chance Wheel granted this kind" while `copy_obtain_count` counts grants from every source.

`stc_ability_init_table` (0x804af4f0), per `CopyKind` index:

| CopyKind | Grant function | Address |
|---------|----------------|---------|
| FIRE (0) | `ability_Fire` | 0x801af474 |
| WHEEL (1) | `ability_Wheel` | 0x801af950 |
| SLEEP (2) | `ability_Sleep` | 0x801b0bf0 |
| SWORD (3) | `ability_Sword` | 0x801afd54 |
| BOMB (4) | `ability_Bomb` | 0x801b11d4 |
| PLASMA (5) | `ability_Plasma` | 0x801b2a4c |
| NEEDLE (6) | `ability_Spike` | 0x801b3688 |
| MIC (7) | `ability_Mic` | 0x801b3dac |
| FREEZE (8) | `ability_Ice` | 0x801b454c |
| TORNADO (9) | `ability_Tornado` | 0x801b4a3c |
| BIRD (10) | `ability_Bird` | 0x801b5480 |

Mic runs three action states: the hold pose `0x3e`, the singing blast `0x61` (entered from the hold on an attack press; spawns Effect `0x5a5a2` and SFX `0x2006b`) and the recovery `0x62` the blast falls into once its body anim ends. `ability_Mic` installs no hitbox of its own, so the blast has no attack-method index in the `+0x74` / `+0xe4` cause space the way Tornado (`0x0e`) and Quick Spin (`0x10`) do; the rider's single `TriggerData` (`RiderData+0x674`) takes its cause from the rider archetype once at `Rider_Create`. Identifying a Mic kill therefore means reading `copy_kind` plus the live action state.

Each grant function transitions the rider into the ability's hold action-state (`RiderStateChange` 0x8018e580 - Fire uses state 57), calls `ability_ChangeSpeedometerDesign` (0x801a809c), swaps in the ability model + hat (`randomAbility_changeModel` 0x801a6640 dispatches to `randomAbility_changeModel_<Kind>`, then `ability_<Kind>_giveHat`), arms the countdown (`copy_timer` seeded via `zz_801a7bdc_` 0x801a7bdc), and installs the ability's callbacks.

## Per-Frame Tick

While the rider is in the ability action-state, `abilityTimerBranchToAbilityCountdown` (0x801a5f68) runs each frame - it is the entry at +0x14 in the action-state callback struct at 0x804af4a0. It advances the ability model animation (`copy_wheel_jobj`, +0x8fc) if present, then calls `cb_ability_tick` (+0x92c) if present.

`cb_ability_tick` is the ability's `abilityTimer_*` (e.g. `abilityTimer_Fire` 0x801aee28, `abilityTimer_Sword_checkIf0` 0x801afb30, `abilityTimer_Bird_checkIfZero` 0x801b5660). It calls `abilityTimerDecreaser` (0x801a7c84) - which decrements `copy_timer` and, once it dips below the +0x920 threshold, fires the warning blink (`Rider_ApplyColAnim(rd, 0x2b, 0)`, 0x8019bfb4) and zeroes the threshold so it fires once - then checks the expiry condition. When `copy_timer` reaches 0 (or the ability's fuel/ammo runs out) it runs the drop.

## Teardown

Teardown is layered; the drop path calls the top and each layer calls the next:

1. **Per-ability revert** - e.g. `Fire_LoseAbility_Exit` (0x801af330), `abilityTimer_Sword_revertModel` (0x801afc70), `abilityTimer_Bomb_remove` (0x801b13ec). Frees ability-specific state (active projectiles, attached models) and calls the model revert.
2. **`revertKirbyModel`** (0x801a7d70) - frees the ability JObj (+0x8fc) and its heap alloc (+0x904) and nulls both, calls the teardown core, then zeroes the ability callback fields +0x92c, +0x930, +0x934, +0x938, +0x7fc, +0x7e8 and +0x7ec.
3. **`Rider_TeardownCopyAbility`** (0x801a810c) - the core: sets `copy_kind` = -1, spawns the "ability lost" poof effect (`Effect_SpawnSync` 0x80236c40, effect id `0x3a990`), plays the loss SFX, and applies the fade ColAnim (`0x2c`).

`Rider_AbilityRemoveModel` (0x80191554) is the universal front door: if `copy_kind` or `powerup_kind` is set it invokes the installed teardown slots (+0x7f8 then +0x7fc), so it handles every ability. It does **not** play the spit-out animation.

`AS_LoseCopyAbility` (0x801b0adc, aka `Rider_LoseAbilityState_Enter`) is only the spit-out animation: it `RiderStateChange`s to action-state `0x68`. It performs no teardown, so on its own the ability is not lost - every engine caller runs a revert first.

## The Two Ways An Ability Leaves

- **Expiry / use up** - the per-frame `cb_ability_tick` sees `copy_timer == 0` (or ammo/fuel gone), runs the per-ability revert, then `AS_LoseCopyAbility` for the spit animation.
- **Replacement** - a new inhale calls `Rider_GiveAbility`, which calls `Rider_AbilityRemoveModel` to strip the old ability (no spit animation) before granting the new one.

## Forcing A Drop From Mod Code

To discard the held ability from a mod (a manual "drop ability" control, a trap), reproduce the expiry structure but use the universal remover so it works for any kind. `mods/archipelago/src/drop_ability.c` does exactly this on a Z press:

```c
Rider_AbilityRemoveModel(rd);     // teardown: copy_kind = -1, poof VFX/SFX, model/hat removed
Rider_LoseAbilityState_Enter(rd); // AS_LoseCopyAbility spit-out animation
```

Call these only when `rd->copy_kind != COPYKIND_NONE`. `Rider_AbilityRemoveModel` clears `copy_kind`, so a per-frame trigger that re-checks it won't re-fire. Calling `AS_LoseCopyAbility` alone would play the animation but leave the ability equipped.
