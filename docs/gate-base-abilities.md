# Base Ability Gating

## Overview

Kirby's three fundamental moves — **inhale**, **quick spin**, and **machine
charge** — are each gated behind an Archipelago unlock item. Until the item is
received the move does nothing; once received it works normally. Gating is
**human-only**: CPUs keep every move (gating the universal machine charge for all
machines would leave every CPU racer unable to boost).

This covers the three 3D-mode moves (Air Ride / City Trial) plus their Top Ride
analogs (Top Ride has charge and spin, but no inhale). It is *gating only* — no
new location checks. For the copy-ability system (a separate gating category) see
`gate-abilities.md`.

**Files:** `gate_base_abilities.c` / `gate_base_abilities.h`.

## Save Data & AP Items

`u8 base_ability_unlocked_mask` in `APSave` (`main.h`) — bit N = `BaseAbilityKind`
N (`BASEABILITY_INHALE` 0, `BASEABILITY_QUICKSPIN` 1, `BASEABILITY_CHARGE` 2;
`archipelago_api.h`).

Three AP items, `AP_BASE_ABILITY_UNLOCK_BASE` (771) + `BaseAbilityKind`, IDs
771–773. Dispatched in `ap_item_handler.c` to `GateBaseAbilities_UnlockAbility`,
which sets the bit and shows an "Unlock: <name>" TextBox.

Category `AP_UNLOCK_BASE_ABILITY` backs the mask via `Unlock_GetMask` /
`Unlock_SetMask`. Slot option `base_ability_gating_enabled`: when 0 (the default,
and the value an apworld that ships no base-ability items leaves), the mask is
pre-filled all-1s at connect (`APOptions_ApplyUngatedCategories`) so every move
works as vanilla.

## Reversible, human-only gates

Each gate reads the unlock mask every time it runs, so it is fully reversible —
the patch stays installed permanently and simply runs the original engine
behavior once the ability is unlocked (or when the acting entity is a CPU).
Human test: `Ply_GetPKind(rd->ply) == PKIND_HMN` (3D), or
`TopRide_GetPlayerKind(kirby->player_slot) == TR_PKIND_HMN` (Top Ride).

`CODEPATCH_REPLACEFUNC` can't be used to gate reversibly (it writes a bare branch
with no trampoline, so the original is unreachable). Each choke therefore uses the
mechanism that fits its call site.

### Inhale (Air Ride / City Trial)

`REPLACECALL` at `0x8019c610` — the `bl Rider_StartInhale` inside
`Rider_TryStartInhale` (0x8019c5ac). When inhale is locked for a human the wrapper
returns without starting the suck; otherwise it calls `Rider_StartInhale`
(0x801ad2c4) unchanged. The native inhale pipeline
(`Rider_TryStartInhale` → `Rider_CanStartInhale` gate → `Rider_StartInhale`) is
distinct from the copy-ability grant/roulette path, so this cannot affect
AP-granted abilities. Gating this call site (rather than no-oping
`Rider_StartInhale` itself) leaves the hypernova mod's direct-inhale calls intact.

### Quick spin (Air Ride / City Trial)

Kirby, Dedede, and Meta Knight are separate rider characters, each with its own
quick-spin enter — a single mask bit gates all three.

**Kirby:** `REPLACECALL` at both `0x801b7ec0` (in `Rider_IASACheck_QuickSpin`,
0x801b7e80) and `0x801b7e58` (in the neutral-state entry
`Rider_TryQuickSpinNeutral`, 0x801b7e0c) — the two `bl Rider_QuickSpin_Enter`
sites. Both set up the same arg registers, so one wrapper
`void W(float, RiderData*, int dir, int flag)` serves both: locked human → return
(no spin); otherwise call `Rider_QuickSpin_Enter` (0x801b7ee4).

**Dedede / Meta Knight:** these characters never touch `Rider_QuickSpin_Enter`.
Their per-character IASA checks funnel through their own enters —
`Rider_Dedede_QuickSpin_Enter` (0x801c05f8, action-state 0x2c) and
`Rider_MetaKnight_QuickSpin_Enter` (0x801c3f90, action-state 0x2d), each
`void W(RiderData *rd, int dir)`. Each has a single call site (`0x801c05d4`,
`0x801c3f6c`) `REPLACECALL`'d to a wrapper that skips the enter for a locked
human. Gating the single enter call covers every state that can spin, since all
of a character's IASA checks share it.

All spin paths (Kirby and the alt characters) share the rotation detector
`Rider_CheckQuickSpinInput` (0x80191980). The Tornado copy ability also shares
that detector but enters via yet another function (gated on `copy_kind ==
COPYKIND_TORNADO`), so it is unaffected by any of these gates.

### Machine charge (Air Ride / City Trial)

Charge is accumulated by per-vehicle-phase stat-table callbacks (the "push"
callbacks), and there are **three** distinct functions that grow `charge_value`
(MachineData+0x78c) while holding A. All take the `MachineData*` in r3 and are
`REPLACECALL`'d at every call site; gating just the grounded one leaves flight,
rail, and wheelie phases chargeable. Each read only `md` (and, for the
explicit-rate pair, `rate` in f1) - no register passthrough, so a thin C wrapper
is safe.

- **`Machine_IncrementCharge`** (0x801cc480) - computes the turn-angle rate
  internally. Four sites, wrapper `void W(MachineData *md)`: `0x801ef424` in
  `MachinePhys_Charge` and `0x801ef350` in its minimal sibling (generic grounded),
  `0x801fa1d4` and `0x801fa29c` in the Wheel/wheelie callbacks.
- **`Machine_AddCharge`** (0x801ca334) - takes an explicit `rate` (f1). Used by
  Star flight physics, i.e. the **glide** charge; site `0x801efa6c` in
  `Star_Fly_3_HandleFlightPhysics`. Wrapper `void W(double rate, MachineData *md)`.
- **`Machine_AddChargeEx`** (0x801cc378) - like `Machine_AddCharge` but also
  updates the charge-state flags at +0xc32. Used by rail-run and wheelie ready
  push; sites `0x801eb968` (Star RailRunPush) and `0x801f5f30` (Wheel
  Ready/RailRunPush). Wrapper `void W(double rate, MachineData *md)`.

`rate` stays a named wrapper parameter so the compiler preserves f1 across the
human check before forwarding it. When charge is locked for a human each wrapper
skips the accumulation; otherwise it runs normally. The player is resolved through
`md->rider_gobj` (MachineData+0x4) → `RiderData.ply`.

## Charge & EnergyLink

The charge gate is designed so **received energy still charges the meter while the
player can't contribute energy**:

- EnergyLink generation (`energylink.c` `EnergyLink_PerFrame`) mints energy from
  the positive frame-to-frame delta of `md->charge_value`. Blocking
  `Machine_IncrementCharge` means holding A never raises `charge_value`, so a
  locked human generates no charge energy.
- EnergyLink Auto-Charge writes `md->charge_value` **directly** (not through
  `Machine_IncrementCharge`) and re-snaps its baseline, so it still fills the meter
  from the shared pool. Entering and releasing the charge state is not gated, so a
  meter filled by Auto-Charge still fires a boost on release.

## Top Ride

Top Ride is a separate object system (`TopRideKirby`), so it needs its own gates.

### Top Ride charge — conditional hook

The A-hold accumulation in `TopRide_ChargeUpdate` (0x802df900) is an inline store
`stfs f0,52(r3)` at `0x802e01b4` (`TopRideChargeComponent.charge_value`,
component+0x34), not a call — so it's gated with `CODEPATCH_HOOKCONDITIONALCREATE`.
The hook passes the charge component (r3) and the post-add value (f1) to
`GateBaseAbilities_TopRideChargeStore`, which performs the store for CPUs /
unlocked humans and skips it for a locked human, always returning 1 so the hook's
alt exit (`0x802e01b8`) is taken and the original store never re-runs. The human is
resolved via `comp->kirby_ptr` (component+0x00) → `player_slot`.

Only the accumulation is suppressed: `is_charging` / `charge_ready`, the decay
branch, and the max clamp all still run. Because EnergyLink's Top Ride Auto-Charge
(`EnergyLink_TopRidePerFrame`) is gated on `is_charging && charge_ready`, it still
injects received energy into a locked human's meter.

### Top Ride spin — history-query gate

Top Ride's voluntary quick spin is the L/R stick-flick spin attack, the analog of
the 3D `Rider_QuickSpin`. It is **not** the `KirbySpin` state (vtable[61],
`TopRide_KirbySpinMethod` 0x802d59cc): that is the *hazard* spin-out (a damage
reaction, alongside Burn/Freeze/Crush/Explode), which is deliberately left ungated
— locking a base ability should not make the player immune to spin hazards.

The voluntary spin lives in `TopRide_KirbyPhysUpdate` (0x802d5ec0). Each frame it
pushes the stick input into the kirby's history ring (`kirby->history`, +0x64) via
`TopRide_KirbyHistoryPush`, then calls `TopRide_KirbyHistoryQuery` (0x80312000) to
measure the ring's oscillation: it returns 0 when the summed per-entry `|delta|`
is ≤ 200 (no flick), else ±1 (the spin direction). A nonzero result makes the
function set `charge.angular_velocity` and enter the spin-attack state
(`AC_SPINATTACK_L/R_START`) via `TopRide_KirbyQuickSpinSetter` (0x802f18d8).

The query call at `0x802d5f90` is the single gate for the whole move. `REPLACECALL`
there routes to `GateBaseAbilities_TopRideQuickSpinQuery`, which returns 0 ("no
flick") for a locked human so `TopRide_KirbyPhysUpdate` skips the entire spin
block; for CPUs and unlocked humans it calls the real query unchanged. Normal
steering reads the stick through a separate path and is untouched. The query
receives `&kirby->history` in r3, so the wrapper recovers the kirby as that
pointer minus 0x64.

## Design Notes

**Human-only:** the lock targets the human player(s) whose AP save this is; CPUs
are never gated. Required for charge (universal to all machines) and applied
uniformly to inhale and quick spin for consistency.

**One mask, all modes:** the 3D and Top Ride gates for a given move share the same
`BaseAbilityKind` bit, so a single unlock item enables the move everywhere.
