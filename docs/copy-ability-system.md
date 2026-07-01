# Copy Ability System

The engine's copy-ability lifecycle for a rider: how an ability is granted, held
and ticked each frame, and torn down. This is the mechanics layer beneath the AP
gating (which decides *whether* an ability may be obtained — see
`gate-abilities.md`). The `copy_kind` system described here is a `RiderData`
field and exists only in the 3D modes (City Trial / Air Ride). Top Ride is a
separate object system (`TopRideKirby`, no `copy_kind`); its copy-ability analogs
are the four ability-power *items* (Fire, Freeze Fan, Bomb, Walky), each a timed
Kirby state — see `topride-item-system.md`.

There are 11 `CopyKind`s (`rider.h`): FIRE(0), WHEEL(1), SLEEP(2), SWORD(3),
BOMB(4), PLASMA(5), NEEDLE(6), MIC(7), FREEZE(8), TORNADO(9), BIRD(10), plus
`COPYKIND_NONE` = -1. The currently-held kind lives in `RiderData.copy_kind`
(+0x454); a queued-but-not-yet-granted kind in `queued_ability_kind` (+0x458).

## RiderData fields

| Offset | Name | Meaning |
|--------|------|---------|
| 0x454 | `copy_kind` | Held `CopyKind`, or -1 (`COPYKIND_NONE`). |
| 0x458 | `queued_ability_kind` | Ability queued while the rider can't yet receive one. |
| 0x8fc | `copy_wheel_jobj` | Ability's animated JObj (also the copy-chance wheel model while it spins); advanced each frame by the tick. |
| 0x91c | `copy_timer` | Countdown; the ability expires at 0. |
| 0x920 | — | "About to expire" threshold: when `copy_timer` drops below it the warning blink fires. |
| 0x92c | `cb_ability_tick` | Per-frame ability callback (the `abilityTimer_*` for this kind). Not every kind installs one (Bomb doesn't). |
| 0x7f8 | `cb_ability_remove2` | Teardown callback — the slot used by Bomb (`0x801b13ac`). |
| 0x7fc | `cb_ability_remove` | Teardown callback — the slot used by Fire (`0x801af408`), Sword (`0x801afd04`), Wheel, Bird, etc. |

An ability installs exactly one of the `cb_ability_remove` / `cb_ability_remove2`
pair; both route to the same teardown core. Don't call them directly — use
`Rider_AbilityRemoveModel`, which invokes whichever is set.

## Grant

`Rider_GiveAbility(RiderData*, CopyKind)` (0x801a81a4) is the master entry:

1. `Rider_CheckUnableAbility` (0x80192650) — if the rider can't receive one right
   now, the kind is stashed in `queued_ability_kind` and granted later.
2. Look up the grant function in the give table at **0x804af4f0** (11 entries
   indexed by `CopyKind`); a null entry aborts.
3. `Rider_AbilityRemoveModel` — strip the currently-held ability (below).
4. `Rider_AbilityClearQueued` (0x801915c4), `Rider_RecordCopyAbility` (0x8022ee00),
   `Rider_MarkCopyAbilityObtained` (0x8022f150) — bookkeeping / checklist tracking.
5. Call the per-kind grant function.

Give table (0x804af4f0) → per-kind grant: Fire `0x801af474`, Wheel `0x801af950`,
Sleep `0x801b0bf0`, Sword `0x801afd54`, Bomb `ability_Bomb 0x801b11d4`, Plasma
`0x801b2a4c`, Needle `0x801b3688`, Mic `0x801b3dac`, Freeze `0x801b454c`, Tornado
`0x801b4a3c`, Wing `ability_Bird 0x801b5480`.

Each grant function transitions the rider into the ability's hold action-state
(`RiderStateChange`), swaps in the ability model + hat (`randomAbility_changeModel`
0x801a6640 dispatches to `randomAbility_changeModel_<Kind>`, then `ability_<Kind>_giveHat`),
adjusts the speedometer design, arms the countdown (`copy_timer` seeded via
`zz_801a7bdc_`), and installs the ability's callbacks (`cb_ability_tick` and one of
`cb_ability_remove`/`cb_ability_remove2`).

## Per-frame tick

While the rider is in the ability action-state, `abilityTimerBranchToAbilityCountdown`
(0x801a5f68) runs each frame (it is one entry in the action-state callback struct
at 0x804af4a0): it advances the ability model animation (`copy_wheel_jobj`) and, if
present, calls `cb_ability_tick`.

`cb_ability_tick` is the ability's `abilityTimer_*` (e.g. `abilityTimer_Fire`
0x801aee28, `abilityTimer_Sword_checkIf0` 0x801afb30, `abilityTimer_Bird_checkIfZero`).
It calls `abilityTimerDecreaser` (0x801a7c84) — which decrements `copy_timer` and,
once it dips below the +0x920 threshold, fires the warning blink
(`Rider_ApplyColAnim(rd, 0x2b, 0)`) — then checks the expiry condition. When
`copy_timer` reaches 0 (or the ability's fuel/ammo runs out) it runs the drop.

## Teardown

Teardown is layered; the drop path calls the top and each layer calls the next:

1. **Per-ability revert** — e.g. `Fire_LoseAbility_Exit` (0x801af330),
   `abilityTimer_Sword_revertModel` (0x801afc70), `abilityTimer_Bomb_remove`
   (0x801b13ec). Frees ability-specific state (active projectiles, attached models
   via `give_Model_`) and calls the model revert.
2. **`revertKirbyModel_`** (0x801a7d70) — removes the ability JObj/model + its heap
   alloc, calls the teardown core, and clears the ability callback fields
   (`cb_ability_tick`, `cb_ability_remove`, …) back to 0.
3. **`Rider_TeardownCopyAbility`** (0x801a810c) — the core: sets `copy_kind` = -1,
   spawns the "ability lost" poof effect (`Effect_SpawnSync`, effect id `0x3a990`),
   plays the loss SFX, and applies the fade ColAnim (`0x2c`). (Poof effect details:
   see `effects-system.md`.)

`Rider_AbilityRemoveModel` (0x80191554) is the universal front door: if
`copy_kind` or `powerup_kind` is set it invokes `cb_ability_remove2` (+0x7f8) and
`cb_ability_remove` (+0x7fc) — whichever the current kind installed — so it handles
every ability. It does **not** play the spit-out animation.

`AS_LoseCopyAbility` (0x801b0adc, aka `Rider_LoseAbilityState_Enter`) is only the
spit-out animation: it `RiderStateChange`s to action-state `0x68`. It performs no
teardown, so on its own the ability is not lost — every engine caller runs a revert
first.

## The two ways an ability leaves

- **Expiry / use up** — the per-frame `cb_ability_tick` sees `copy_timer == 0` (or
  ammo/fuel gone), runs the per-ability revert, then `AS_LoseCopyAbility` for the
  spit animation.
- **Replacement** — a new inhale calls `Rider_GiveAbility`, which calls
  `Rider_AbilityRemoveModel` to strip the old ability (no spit animation) before
  granting the new one.

## Forcing a drop from mod code

To discard the held ability from a mod (e.g. a manual "drop ability" control or a
trap), reproduce the expiry structure but use the universal remover so it works for
any kind:

```c
Rider_AbilityRemoveModel(rd);     // teardown: copy_kind = -1, poof VFX/SFX, model/hat removed
Rider_LoseAbilityState_Enter(rd); // AS_LoseCopyAbility spit-out animation
```

Call these only when `rd->copy_kind != COPYKIND_NONE`. `Rider_AbilityRemoveModel`
clears `copy_kind`, so a per-frame trigger that re-checks it won't re-fire. Calling
`AS_LoseCopyAbility` alone would play the animation but leave the ability equipped.
