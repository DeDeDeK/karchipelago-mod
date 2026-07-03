# Air Quick Spin

## Overview

In vanilla Kirby Air Ride the L/R-flick quick spin (the stick-snap spin attack) is
only available while the machine is grounded. This mod adds a menu toggle that
also enables it while airborne, in **Air Ride and City Trial**. It is a
convenience enhancement, not an Archipelago gate: no items, no save state.

**Files:** `air_quick_spin.c` / `air_quick_spin.h`. Menu wiring in
`settings_menu.c` / `settings_menu.h`.

## Why quick spin is grounded-only

Each rider action-state has a per-frame logic callback that runs its interrupt
(IASA) checks. The grounded machine-riding state uses `groundLogic` (0x801ab554,
which delegates to `Rider_IASA_MachineGround` 0x801ab300); the airborne state
uses `airControl` (0x801ac128). Both run the same sequence — copy-input,
`Copy_PlasmaThink`, `Rider_IASACheck_Charge` (0x801ab624),
`Rider_IASACheck_Spin` (0x80191a58), and `Rider_IASACheck_TornadoSpin`
(0x801b4d04, the `copy_kind == COPYKIND_TORNADO` spin) — with one difference:

- **Grounded:** after `Rider_IASACheck_TornadoSpin` returns 0 (no Tornado spin),
  it calls `Rider_IASACheck_QuickSpin` (0x801b7e80), the normal L/R-flick spin.
- **Airborne:** `airControl` stops at `Rider_IASACheck_TornadoSpin`; the quick
  spin check is never called. The compiler even left a dead `cmpwi r3,0` at
  0x801ac170 where the grounded build's `if (tornado == 0)` branch used to be.

This is why the Tornado copy ability spins mid-air in vanilla but the plain quick
spin does not: Tornado goes through `Rider_IASACheck_TornadoSpin`, which
`airControl` still calls. Both share the same stick detector
(`Rider_CheckQuickSpinInput` 0x80191980), so the input already registers in the
air — only the enter path is missing.

## The hook

`CODEPATCH_HOOKCREATE` at 0x801ac170 (the dead `cmpwi`) reinstates the missing
call. At that point `r31` still holds the `RiderData` (airControl keeps it there
for the whole function) and `r3` holds the `Rider_IASACheck_TornadoSpin` result.
The hook prologue moves them into arg registers and calls
`AirQuickSpin_TryAerialSpin(rd, tornado_fired)`, which — when the toggle is on and
Tornado did not already fire — calls `Rider_IASACheck_QuickSpin(rd)`. Skipping the
call when Tornado fired mirrors the grounded `if (tornado == 0)` guard, so a
Tornado player still gets the Tornado spin and not a competing quick spin.

The hook only reinstates a call; it never touches `Rider_IASACheck_TornadoSpin`,
so vanilla Tornado air-spin is unchanged.

## Scope

`airControl` is the airborne callback of the 3D machine-riding state, used only in
Air Ride and City Trial. Top Ride is a separate object system (`TopRideKirby`)
and is unaffected, so the toggle is inherently CT/AR-only. The reinstated call
funnels through `Rider_QuickSpin_Enter` at 0x801b7ec0, which the base-ability
gating replaces with its quick-spin lock wrapper — so if quick spin is
AP-locked, it stays locked in the air as well.

The hook applies to every rider in the airborne state, mirroring the grounded
quick spin (which is likewise available to all riders, human and CPU).

## Menu

`ap_menu_settings.air_quick_spin_enabled` (`APMenuSettings`), an On/Off toggle in
the Archipelago Settings menu. Default **Off** (vanilla behavior); the player
opts in. Changes are logged via `OnToggleAirQuickSpin`.
