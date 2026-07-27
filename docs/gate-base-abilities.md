# Base Ability Gating

Kirby's three fundamental moves — inhale, quick spin, and machine charge — are each gated behind an Archipelago unlock item. Until the item is received the move does nothing; once received it works normally. Gating is **human-only**: CPUs keep every move, since gating the universal machine charge for all machines would leave every CPU racer unable to boost.

## What Is Gated

The 3 `BaseAbilityKind` values (`archipelago_api.h`), one bit each in `base_ability_unlocked_mask`. This is an Archipelago-only enum, not a vanilla game enum. Each bit gates both the 3D-mode move (Air Ride / City Trial) and its Top Ride analog, so one unlock enables the move everywhere.

| Bit | `BaseAbilityKind` | Name | 3D gate | Top Ride gate | AP item |
|----:|-------------------|------|---------|---------------|--------:|
| 0 | `BASEABILITY_INHALE` | Inhale | `Rider_StartInhale` call site | none (Top Ride has no inhale) | 771 |
| 1 | `BASEABILITY_QUICKSPIN` | Quick Spin | Kirby / Dedede / Meta Knight spin enters | stick-flick spin attack | 772 |
| 2 | `BASEABILITY_CHARGE` | Charge | three charge accumulators | `TopRide_ChargeUpdate` store | 773 |

This is *gating only* — no new location checks. Copy abilities are a separate category (`gate-abilities.md`).

## Entry Points

**Files:** `mods/archipelago/src/gate_base_abilities.c` / `gate_base_abilities.h`

| Symbol | Kind | Where | Role |
|--------|------|-------|------|
| `GateBaseAbilities_OnBoot(void)` | mod | gate_base_abilities.c | Installs 12 `REPLACECALL`s and 1 conditional hook (called from `main.c`). |
| `GateBaseAbilities_StartInhale(RiderData*)` | mod | gate_base_abilities.c | Wrapper for `Rider_StartInhale`. |
| `GateBaseAbilities_QuickSpinEnter(float, RiderData*, int, int)` | mod | gate_base_abilities.c | Wrapper for Kirby's `Rider_QuickSpin_Enter` (both call sites). |
| `GateBaseAbilities_DededeSpinEnter(RiderData*, int)` | mod | gate_base_abilities.c | Wrapper for `Rider_Dedede_QuickSpin_Enter`. |
| `GateBaseAbilities_MetaKnightSpinEnter(RiderData*, int)` | mod | gate_base_abilities.c | Wrapper for `Rider_MetaKnight_QuickSpin_Enter`. |
| `GateBaseAbilities_IncrementCharge(MachineData*)` | mod | gate_base_abilities.c | Wrapper for `Machine_IncrementCharge` (4 sites). |
| `GateBaseAbilities_AddCharge(double, MachineData*)` | mod | gate_base_abilities.c | Wrapper for `Machine_AddCharge` (glide charge). |
| `GateBaseAbilities_AddChargeEx(double, MachineData*)` | mod | gate_base_abilities.c | Wrapper for `Machine_AddChargeEx` (rail / wheelie push). |
| `GateBaseAbilities_TopRideChargeStore(TopRideChargeComponent*, float)` | mod | gate_base_abilities.c | Conditional-hook body replacing the inline TR charge store. |
| `GateBaseAbilities_TopRideQuickSpinQuery(int *history)` | mod | gate_base_abilities.c | Wrapper for `TopRide_KirbyHistoryQuery`. |
| `GateBaseAbilities_UnlockAbility(BaseAbilityKind)` | mod | gate_base_abilities.c | Sets the unlock bit and posts a textbox. Called from `ap_item_handler.c`. |
| `Rider_StartInhale` | game | 0x801ad2c4 | Forces the inhale action-state. |
| `Rider_QuickSpin_Enter` | game | 0x801b7ee4 | Kirby's quick-spin enter (action-state 0x2c). |
| `Rider_Dedede_QuickSpin_Enter` | game | 0x801c05f8 | Dedede's quick-spin enter (action-state 0x2c). |
| `Rider_MetaKnight_QuickSpin_Enter` | game | 0x801c3f90 | Meta Knight's quick-spin enter (action-state 0x2d). |
| `Machine_IncrementCharge` | game | 0x801cc480 | Grounded / wheelie charge accumulation (rate computed internally). |
| `Machine_AddCharge` | game | 0x801ca334 | Explicit-rate accumulator; used by Star flight (glide charge). |
| `Machine_AddChargeEx` | game | 0x801cc378 | Like `Machine_AddCharge` plus the charge-state flags at +0xc32. |
| `TopRide_KirbyHistoryQuery` | game | 0x80312000 | Stick-history oscillation query; 0 = no flick. |

## Game System

Each gate reads the unlock mask every time it runs, so it is fully reversible: the patch stays installed permanently and simply runs the original engine behavior once the ability is unlocked (or when the acting entity is a CPU). Human test: `Ply_GetPKind(rd->ply) == PKIND_HMN` (3D), or `TopRide_GetPlayerKind(kirby->player_slot) == TR_PKIND_HMN` (Top Ride).

`CODEPATCH_REPLACEFUNC` can't be used to gate reversibly — it writes a bare branch with no trampoline, so the original is unreachable. Each choke therefore uses the mechanism that fits its call site: `REPLACECALL` where the move is entered through a `bl`, and `HOOKCONDITIONALCREATE` where the engine writes the value inline.

### Inhale (Air Ride / City Trial)

The native inhale pipeline is `Rider_TryStartInhale` (0x8019c5ac) → `Rider_CanStartInhale` gate (0x801a617c) → `Rider_StartInhale` (0x801ad2c4). It is distinct from the copy-ability grant/roulette path, so gating it cannot affect AP-granted abilities.

### Quick spin (Air Ride / City Trial)

Kirby, Dedede, and Meta Knight are separate rider characters, each with its own quick-spin enter; a single mask bit gates all three.

- **Kirby** funnels both entries — `Rider_IASACheck_QuickSpin` (0x801b7e80) and the neutral-state entry `Rider_TryQuickSpinNeutral` (0x801b7e0c) — into `Rider_QuickSpin_Enter` (0x801b7ee4).
- **Dedede / Meta Knight** never touch `Rider_QuickSpin_Enter`. Their per-character IASA checks funnel through `Rider_Dedede_QuickSpin_Enter` (0x801c05f8, action-state 0x2c) and `Rider_MetaKnight_QuickSpin_Enter` (0x801c3f90, action-state 0x2d), each `void W(RiderData *rd, int dir)`, each with a single call site.

All spin paths share the rotation detector `Rider_CheckQuickSpinInput` (0x80191980). The Tornado copy ability also shares that detector but enters via yet another function (gated on `copy_kind == COPYKIND_TORNADO`), so it is unaffected by any of these gates.

### Machine charge (Air Ride / City Trial)

Charge is accumulated by per-vehicle-phase stat-table callbacks (the "push" callbacks), and **three** distinct functions grow `charge_value` (MachineData+0x78c) while A is held. All take the `MachineData*` in r3; gating just the grounded one would leave flight, rail, and wheelie phases chargeable. Each reads only `md` (and, for the explicit-rate pair, `rate` in f1) — no register passthrough, so a thin C wrapper is safe.

### Top Ride

Top Ride is a separate object system (`TopRideKirby`), so it needs its own gates.

The A-hold charge accumulation in `TopRide_ChargeUpdate` (0x802df900) is an inline store `stfs f0,52(r3)` at `0x802e01b4` (`TopRideChargeComponent.charge_value`, component+0x34), not a call.

The voluntary quick spin is the L/R stick-flick spin attack, the analog of the 3D `Rider_QuickSpin`. It is **not** the `KirbySpin` state (vtable[61], `TopRide_KirbySpinMethod` 0x802d59cc): that is the *hazard* spin-out (a damage reaction, alongside Burn/Freeze/Crush/Explode), which is deliberately left ungated — locking a base ability should not make the player immune to spin hazards. The voluntary spin lives in `TopRide_KirbyPhysUpdate` (0x802d5ec0): each frame it pushes the stick input into the kirby's history ring (`kirby->history`, +0x64) via `TopRide_KirbyHistoryPush` (0x80311f88), then calls `TopRide_KirbyHistoryQuery` (0x80312000) to measure the ring's oscillation. The query returns 0 when the summed per-entry `|delta|` is ≤ 200 (no flick), else ±1 (the spin direction). A nonzero result makes the function set `charge.angular_velocity` and enter the spin-attack state (`AC_SPINATTACK_L/R_START`) via `TopRide_KirbyQuickSpinSetter` (0x802f18d8).

## Implementation

Every 3D gate and the Top Ride spin gate are `CODEPATCH_REPLACECALL`s; the Top Ride charge store is a `CODEPATCH_HOOKCONDITIONALCREATE`.

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

Details that constrain the wrappers:

- **Kirby's two spin sites share one wrapper.** Both set up the same argument registers, so `void W(float, RiderData*, int dir, int flag)` serves both: locked human → return (no spin); otherwise call `Rider_QuickSpin_Enter`.
- **Gating the inhale call site, not the function.** No-oping `Rider_StartInhale` itself would also kill the hypernova mod's direct-inhale calls; wrapping the single `bl` inside `Rider_TryStartInhale` leaves those intact.
- **`rate` stays a named wrapper parameter** on `GateBaseAbilities_AddCharge` / `_AddChargeEx` so the compiler preserves f1 across the human check before forwarding it.
- **The player is resolved through `md->rider_gobj`** (MachineData+0x4) → `RiderData.ply` for all three charge wrappers.
- **The Top Ride charge hook always returns 1**, so the hook's alt exit (`0x802e01b8`) is taken and the original store never re-runs; the wrapper performs the store itself for CPUs and unlocked humans. The human is resolved via `comp->kirby_ptr` (component+0x00) → `player_slot`. Only the accumulation is suppressed: `is_charging` / `charge_ready`, the decay branch, and the max clamp all still run.
- **The Top Ride spin query receives `&kirby->history` in r3**, so the wrapper recovers the `TopRideKirby` as that pointer minus 0x64. Returning 0 ("no flick") for a locked human makes `TopRide_KirbyPhysUpdate` skip the entire spin block; normal steering reads the stick through a separate path and is untouched.

### Charge and EnergyLink

The charge gate is shaped so **received energy still charges the meter while the player can't contribute energy**:

- EnergyLink generation (`EnergyLink_PerFrame` in `energylink.c`) mints energy from the positive frame-to-frame delta of `md->charge_value`. Blocking `Machine_IncrementCharge` means holding A never raises `charge_value`, so a locked human generates no charge energy.
- EnergyLink Auto-Charge writes `md->charge_value` **directly** (not through `Machine_IncrementCharge`) and re-snaps its baseline, so it still fills the meter from the shared pool. Entering and releasing the charge state is not gated, so a meter filled by Auto-Charge still fires a boost on release.
- Top Ride Auto-Charge (`EnergyLink_TopRidePerFrame`) is gated on `is_charging && charge_ready`, both of which the Top Ride charge hook leaves running, so it still injects received energy into a locked human's meter.

## Save Data

`u8 base_ability_unlocked_mask` in `APSave` (`main.h`, accessed via the global `ap_save`) — bit N = `BaseAbilityKind` N.

The mask is exposed through `ArchipelagoAPI` as `AP_UNLOCK_BASE_ABILITY`. When the slot option `base_ability_gating_enabled` is 0 — the default, and the value an apworld that ships no base-ability items leaves — `APOptions_ApplyUngatedCategories` in `main.c` pre-fills the mask with `(1 << BASEABILITY_NUM) - 1` at connect, so every move works as vanilla.

## AP Items

3 AP items, `AP_BASE_ABILITY_UNLOCK_BASE` (771, `archipelago_api.h`) + `BaseAbilityKind` index → IDs 771–773. `ap_item_handler.c` routes IDs in `[771, 771 + BASEABILITY_NUM)` to `GateBaseAbilities_UnlockAbility(id - AP_BASE_ABILITY_UNLOCK_BASE)`, which sets the bit, logs, and enqueues `"Unlock: <name>"` via `tb_api->EnqueueColoredNoun` with `tb_api->DefaultColor`. Names come from the local `BaseAbility_Names[]` table ("Inhale", "Quick Spin", "Charge").

## Design Decisions

**Human-only:** the lock targets the human player(s) whose AP save this is; CPUs are never gated. Required for charge (universal to all machines) and applied uniformly to inhale and quick spin for consistency.

**One mask, all modes:** the 3D and Top Ride gates for a given move share the same `BaseAbilityKind` bit, so a single unlock item enables the move everywhere.

**Gate the narrowest choke:** each patch sits on the smallest instruction range that still covers every path into the move (a single `bl`, or the one inline store), so unrelated state transitions, hazard reactions, and UI keep running while the move is locked.
