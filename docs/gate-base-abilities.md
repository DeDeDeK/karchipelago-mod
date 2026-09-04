# Base Ability Gating

Kirby's three fundamental moves - inhale, quick spin, and machine charge - are each gated behind an Archipelago unlock item. Until the item is received the move does nothing; once received it works normally. AP items 771-773 (`AP_BASE_ABILITY_UNLOCK_BASE` + `BaseAbilityKind`) route through `ap_item_handler.c` to `GateBaseAbilities_UnlockAbility`, which sets the bit in `APSave.base_ability_unlocked_mask` and posts a textbox. The mask is exposed through `ArchipelagoAPI` as `AP_UNLOCK_BASE_ABILITY`; `base_ability_gating_enabled` defaults to 0, which is also what an apworld shipping no base-ability items leaves, so the connect-time pre-fill in `APOptions_ApplyUngatedCategories` (`main.c`) normally sets all three bits and every move behaves as vanilla.

`BaseAbilityKind` (`archipelago_api.h`) is an Archipelago-only enum, not a vanilla one: `BASEABILITY_INHALE`, `BASEABILITY_QUICKSPIN`, `BASEABILITY_CHARGE`. Each bit gates both the 3D-mode move (Air Ride / City Trial) and its Top Ride analog, so one unlock enables the move everywhere. This is gating only - no new location checks. Copy abilities are a separate category with its own mask.

**File:** `mods/archipelago/src/gate_base_abilities.c`.

## Human-only, and reversible

Every gate reads the mask each time it runs and every patch stays installed permanently, so a move simply resumes vanilla behavior the moment its item arrives. The acting entity also has to be human - `Ply_GetPKind(rd->ply) == PKIND_HMN` in 3D, `TopRide_GetPlayerKind(kirby->player_slot) == TR_PKIND_HMN` in Top Ride. Charge is universal to every machine, so gating it for CPUs would leave every CPU racer unable to boost; inhale and quick spin follow the same rule for consistency.

That reversibility rules out `CODEPATCH_REPLACEFUNC`, which writes a bare branch with no trampoline and leaves the original unreachable. Each choke instead uses whatever fits its call site: `REPLACECALL` where the move is entered through a `bl`, and `HOOKCONDITIONALCREATE` where the engine writes the value inline.

## Game System

### Inhale (Air Ride / City Trial)

`Rider_TryStartInhale` (0x8019c5ac) -> `Rider_CanStartInhale` (0x801a617c) -> `Rider_StartInhale` (0x801ad2c4). This pipeline is distinct from the copy-ability grant and roulette path, so gating it cannot affect an AP-granted copy ability.

### Quick spin (Air Ride / City Trial)

Kirby, Dedede and Meta Knight are separate rider characters with separate enters, all covered by the one mask bit.

- **Kirby** funnels both entries - the IASA check `Rider_IASACheck_QuickSpin` (0x801b7e80) and the neutral-state entry `Rider_TryQuickSpinNeutral` (0x801b7e0c) - into `Rider_QuickSpin_Enter` (0x801b7ee4).
- **Dedede / Meta Knight** never touch that function. Their per-character IASA checks call `Rider_Dedede_QuickSpin_Enter` (0x801c05f8, action-state 0x2c) and `Rider_MetaKnight_QuickSpin_Enter` (0x801c3f90, action-state 0x2d), each `void W(RiderData*, int dir)` with a single call site.

All three share the rotation detector `Rider_CheckQuickSpinInput` (0x80191980). The Tornado copy ability shares that detector too but enters through yet another function gated on `copy_kind == COPYKIND_TORNADO`, so it is untouched by any of these gates.

### Machine charge (Air Ride / City Trial)

Charge is accumulated by the per-vehicle-phase stat-table "push" callbacks, and **three** distinct functions grow `charge_value` while A is held: `Machine_IncrementCharge` (0x801cc480, grounded and wheelie, rate computed internally), `Machine_AddCharge` (0x801ca334, explicit rate, used by Star flight glide), and `Machine_AddChargeEx` (0x801cc378, like `AddCharge` plus the charge-state flags). All take `MachineData*` in r3 and read nothing else beyond the explicit-rate pair's f1, so thin C wrappers are safe - but gating only the grounded one would leave flight, rail and wheelie phases chargeable.

### Top Ride

Top Ride is a separate object system (`TopRideKirby`) and needs its own two gates.

The A-hold charge accumulation in `TopRide_ChargeUpdate` (0x802df900) is an **inline store**, `stfs f0,52(r3)` at 0x802e01b4 into `TopRideChargeComponent.charge_value` - there is no call to intercept.

The voluntary quick spin is the L/R stick-flick spin attack. It is **not** the `KirbySpin` state (vtable[61], `TopRide_KirbySpinMethod` 0x802d59cc): that is the *hazard* spin-out, a damage reaction alongside Burn/Freeze/Crush/Explode, and is deliberately left ungated - locking a base ability should not make the player immune to spin hazards. The voluntary spin lives in `TopRide_KirbyPhysUpdate` (0x802d5ec0): each frame it pushes stick input into the kirby's history ring via `TopRide_KirbyHistoryPush` (0x80311f88), then calls `TopRide_KirbyHistoryQuery` (0x80312000) to measure the ring's oscillation. The query returns 0 when the summed per-entry `|delta|` is at most 200 (no flick), else the spin direction; a nonzero result sets `charge.angular_velocity` and enters `AC_SPINATTACK_L/R_START` via `TopRide_KirbyQuickSpinSetter` (0x802f18d8).

## Implementation

Thirteen `CODEPATCH_REPLACECALL`s plus one `CODEPATCH_HOOKCONDITIONALCREATE`, all applied in `GateBaseAbilities_OnBoot`.

| Ability | Patch site(s) | Enclosing function | Wrapper |
|---------|---------------|--------------------|---------|
| Inhale | `0x8019c610` | `Rider_TryStartInhale` (0x8019c5ac) | `GateBaseAbilities_StartInhale` |
| Quick spin (Kirby) | `0x801b7ec0`, `0x801b7e58` | `Rider_IASACheck_QuickSpin` (0x801b7e80), `Rider_TryQuickSpinNeutral` (0x801b7e0c) | `GateBaseAbilities_QuickSpinEnter` |
| Quick spin (Dedede) | `0x801c05d4` | Dedede IASA check | `GateBaseAbilities_DededeSpinEnter` |
| Quick spin (Meta Knight) | `0x801c3f6c` | Meta Knight IASA check | `GateBaseAbilities_MetaKnightSpinEnter` |
| Charge (grounded) | `0x801ef424`, `0x801ef350` | `MachinePhys_Charge` (0x801ef364) and its minimal sibling | `GateBaseAbilities_IncrementCharge` |
| Charge (wheelie) | `0x801fa1d4`, `0x801fa29c` | Wheel/wheelie stat-table callbacks | `GateBaseAbilities_IncrementCharge` |
| Charge (glide) | `0x801efa6c` | `Star_Fly_3_HandleFlightPhysics` (0x801ef9a0) | `GateBaseAbilities_AddCharge` |
| Charge (rail / wheelie push) | `0x801eb968`, `0x801f5f30` | Star RailRunPush, Wheel Ready/RailRunPush | `GateBaseAbilities_AddChargeEx` |
| Top Ride charge | `0x802e01b4` (conditional hook, alt exit `0x802e01b8`) | `TopRide_ChargeUpdate` (0x802df900) | `GateBaseAbilities_TopRideChargeStore` |
| Top Ride quick spin | `0x802d5f90` | `TopRide_KirbyPhysUpdate` (0x802d5ec0) | `GateBaseAbilities_TopRideQuickSpinQuery` |

Constraints the wrappers are shaped around:

- **Kirby's two spin sites share one wrapper.** Both set up the same argument registers, so `void W(float, RiderData*, int dir, int flag)` serves both.
- **The inhale gate sits on the call site, not the function.** No-oping `Rider_StartInhale` itself would also kill the hypernova mod's direct calls into it; wrapping the single `bl` inside `Rider_TryStartInhale` leaves those intact.
- **`rate` stays a named wrapper parameter** on `GateBaseAbilities_AddCharge` / `_AddChargeEx` so the compiler preserves f1 across the human check before forwarding it.
- **The player is resolved through `md->rider_gobj` -> `RiderData.ply`** in all three charge wrappers.
- **The Top Ride charge hook always returns 1**, taking the alt exit so the original store never re-runs; the wrapper performs the store itself for CPUs and unlocked humans. Only the accumulation is suppressed - `is_charging` / `charge_ready`, the decay branch and the max clamp all still run.
- **The Top Ride spin query receives `&kirby->history` in r3**, so the wrapper recovers the `TopRideKirby` as that pointer minus 0x64. Returning 0 makes `TopRide_KirbyPhysUpdate` skip the whole spin block; steering reads the stick through a separate path and is untouched.

## Charge and EnergyLink

The charge gate is deliberately shaped so **received** energy still charges the meter while the player cannot contribute any:

- EnergyLink generation (`EnergyLink_PerFrame`, `energylink.c`) mints energy from the positive frame-to-frame delta of `md->charge_value`. Blocking `Machine_IncrementCharge` means holding A never raises it, so a locked human generates nothing.
- EnergyLink Auto-Charge writes `md->charge_value` **directly** rather than through the accumulators, and re-snaps its baseline, so it still fills the meter from the shared pool. Entering and releasing the charge state is not gated, so a meter filled that way still fires a boost on release.
- Top Ride Auto-Charge (`EnergyLink_TopRidePerFrame`) is gated on `is_charging && charge_ready`, both of which the Top Ride charge hook leaves running, so it still injects received energy into a locked human's meter.
