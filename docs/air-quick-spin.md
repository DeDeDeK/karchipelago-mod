# Air Quick Spin

In vanilla Kirby Air Ride the L/R-flick quick spin (the stick-snap spin attack) is only
available while the machine is grounded. A menu toggle reinstates the missing interrupt check
in the airborne rider state so the spin also fires mid-air, in Air Ride and City Trial, for all
three rider characters. It is a convenience enhancement, not an Archipelago gate: no items, no
save state.

Implementation: `mods/archipelago/src/air_quick_spin.c`, with the menu option in
`mods/archipelago/src/settings_menu.c`.

## Game System

Each rider action-state has a per-frame logic callback that runs its interrupt (IASA) checks.
Every rider character has its own state-descriptor table, so the airborne machine-riding state
resolves to a different callback per character - the three sole callers of the airborne input
helper `0x8019fcf0`:

| Character | Grounded logic | Airborne logic |
|---|---|---|
| Kirby | `groundLogic` (`0x801ab554`) | `airControl` (`0x801ac128`) |
| Dedede | `0x801bec34` | `Rider_Dedede_AirControl` (`0x801bf534`) |
| Meta Knight | `0x801c2150` | `Rider_MetaKnight_AirControl` (`0x801c2b08`) |

The flick itself is detected by two shared functions. `Rider_UpdateQuickSpinTimers`
(`0x80191a58`) ticks a pair of byte accumulators at `RiderData+0xa40` / `+0xa41` - frames since
the stick was last held past the threshold to the right / to the left, held at 0 while the
stick is there and saturating at 0xfe. `Rider_CheckQuickSpinInput` (`0x80191980`) then reads
them back: one accumulator at 0 while the other is still under the config threshold at
`*0x805DD814 + 0x1b0` means the stick just snapped across, and it reports the direction. A
state that never calls the tick leaves the accumulators frozen, so the detector cannot fire
there at all.

Each character funnels a successful detection into its own enter, via its own per-frame
interrupt check:

- Kirby: `Rider_IASACheck_QuickSpin` (`0x801b7e80`) -> `Rider_QuickSpin_Enter` (`0x801b7ee4`)
- Dedede: `Rider_Dedede_IASACheck_QuickSpin` (`0x801c05a8`) -> `Rider_Dedede_QuickSpin_Enter`
  (`0x801c05f8`)
- Meta Knight: `Rider_MetaKnight_IASACheck_QuickSpin` (`0x801c3f40`) ->
  `Rider_MetaKnight_QuickSpin_Enter` (`0x801c3f90`)

All three grounded paths run an earlier interrupt check, then the accumulator tick, then the
quick spin check, and skip the last two when the earlier check already fired. Kirby's
(`Rider_IASA_MachineGround`, `0x801ab300`) guards on the Tornado spin; Dedede's (`0x801bea24`)
and Meta Knight's guard on their charge check. The airborne logic stops short in a different
place for each character:

- **Kirby:** `airControl` runs `Copy_PlasmaThink` (`0x801b2c4c`), `Rider_IASACheck_Charge`
  (`0x801ab624`), the tick, and `Rider_IASACheck_TornadoSpin` (`0x801b4d04`, the
  `copy_kind == COPYKIND_TORNADO` spin) - then stops. The compiler left a dead `cmpwi r3,0` at
  `0x801ac170` where the grounded build's `if (tornado == 0)` branch would be.
- **Dedede:** runs his charge check (`0x801bed04`) and the tick (`bl` at `0x801bf560`), then
  returns. No dead instruction is left over.
- **Meta Knight:** runs only his charge check (`0x801c2268`), leaving a dead `cmpwi r3,0` at
  `0x801c2b28`. He is the one character whose airborne state omits the accumulator tick, so his
  spin accumulators freeze for the whole time he is in the air.

This is why the Tornado copy ability spins mid-air in vanilla but the plain quick spin does
not: Tornado goes through `Rider_IASACheck_TornadoSpin`, which `airControl` still calls.

## Hooks

Kirby and Meta Knight each get a `CODEPATCH_HOOKCREATE` on their dead `cmpwi` (`0x801ac170`,
`0x801c2b28`). At both sites `r31` still holds the `RiderData` and `r3` holds the result of the
check the grounded build would have branched on - Tornado for Kirby, charge for Meta Knight -
so the hook prologue moves them into arg registers and the C body reproduces the grounded
`if (... == 0)` guard before calling that character's quick spin check. Meta Knight's body also
calls `Rider_UpdateQuickSpinTimers` first, since his airborne state never ticks it and the
detector would otherwise read frozen accumulators.

Dedede has no dead slot, so instead his airborne `bl Rider_UpdateQuickSpinTimers` at
`0x801bf560` is redirected with `CODEPATCH_REPLACECALL` to a wrapper that ticks and then runs
his quick spin check. That `bl` is already the last call of the charge-free branch, which is
exactly where his grounded states run the check, so the guard and the ordering come for free.

No hook touches `Rider_IASACheck_TornadoSpin`, so vanilla Tornado air-spin is unchanged.

## Scope

The airborne machine-riding state belongs to the 3D rider object, used only in Air Ride and
City Trial. Top Ride is a separate object system (`TopRideKirby`) and is unaffected, so the
toggle is inherently CT/AR-only.

Base-ability gating replaces the `bl` to each quick-spin enter (call sites `0x801b7ec0` and
`0x801b7e58` for Kirby, `0x801c05d4` for Dedede, `0x801c3f6c` for Meta Knight) with its
quick-spin lock wrapper, so an AP-locked quick spin stays locked in the air as well.

The hooks apply to every rider in the airborne state, mirroring the grounded quick spin (which
is likewise available to all riders, human and CPU).

## Menu

`ap_menu_settings.air_quick_spin_enabled` (`APMenuSettings`), an On/Off toggle in the
Archipelago Settings menu. Default **Off** (vanilla behavior); the player opts in. Changes are
logged via `OnToggleAirQuickSpin`.
