# Machine Charge System

## Overview

The charge system is the core mechanic of Kirby Air Ride machines (stars/bikes/etc.). The player holds A to charge while grounded, then releases to boost. This document covers the vanilla charge mechanics and how EnergyLink interacts with them.

## Key Data Fields (MachineData)

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x324 | Vec3 | `velocity` | Velocity vector; first operand of the charge-rate angle calculation |
| 0x424 | Vec3 | `up` | Up vector; second operand of the charge-rate angle calculation (reflected against velocity) |
| 0x4B0 | int | `charge_full_duration` | Frame count loaded into `charge_full_timer` when charge hits 1.0 |
| 0x4B4 | int | `charge_cooldown_duration` | Frame count loaded into `charge_cooldown_timer` after auto-discharge |
| 0x4FC | float | `base_charge_rate` | Base rate of charge accumulation per frame (scaled by charge stat patches) |
| 0x500 | float | `turning_charge_rate` | Charge rate when turning (higher than base; interpolated by angle) |
| 0x664 | Vec2 | `input.stick` | Analog stick input, written by `Rider_UpdateIsCharge` during charge |
| 0x66C | int | `input.buttons` | Button input; nonzero when player is pressing A. Checked by `Machine_CheckEnterCharge` |
| 0x78C | float | `charge_value` | **Current charge level.** Ranges 0.0–1.0. This is the value that determines boost strength on release |
| 0x790 | int | `charge_full_timer` | Counts down from `charge_full_duration` when charge reaches 1.0. Auto-discharges at 0 |
| 0x794 | int | `charge_cooldown_timer` | Post-discharge cooldown; blocks further charging while nonzero |
| 0x798 | float | `charge_display_value` | Mirror of `charge_value`, written each frame for HUD display |
| 0xC30 bit 0x80 | bitfield | `charge_is_playing_skid_sfx` | Skid sound effect flag during charge |
| 0xC30 bit 0x40 | bitfield | `charge_is_grounded` | **Must be set for charge to increment.** Read by `MachinePhys_Charge` to gate the `Machine_IncrementCharge` call. Set when machine touches ground; bikes always have this set |
| 0xC32 | byte | (charge effect flags) | Effect-state flag byte written by `Machine_IncrementCharge` / `Machine_ChargeUpdate` / `Machine_ClearChargeState` (bits 0x40 / 0x20 / 0x10 / 0x08). Distinct from the `charge_is_grounded` flag at 0xC30. Not yet broken out in `machine.h` |

### Charge Stat vs Charge Value

- `stats.charge` (at MachineData+0x95C, `PATCHKIND_CHARGE` = index 4 in the `stats.values[9]` array based at 0x94C) is the **charge stat** — a patch-modifiable attribute that affects charge rate. Collecting charge patches increases this stat.
- `charge_value` (at 0x78C) is the **current charge meter level** — the 0.0–1.0 value that fills up while holding A and is consumed on release for a boost.

These are distinct: the stat affects how fast the meter fills, the value is the meter itself.

## Charge Lifecycle

### 1. Entering Charge State

```
Player holds A
  → Rider_IASACheck_Charge (0x801AB624)
    → checks button mask 0x100 (A button)
    → AS_StarBeginCharge (0x801AB688)
      → RiderStateChange to state 0x28
      → Machine_CheckEnterCharge (0x801EF150)
        → checks md->0x66C != 0 (charge input flag)
        → Machine_EnterCharge (0x801EF278)
          → MachineStateChange to state 0xF
```

### 2. Per-Frame Charge Accumulation

While in the charge state, the machine's physics function is `MachinePhys_Charge` (0x801EF364):

```
MachinePhys_Charge (each frame)
  → ... physics updates ...
  → if (charge_is_grounded):  // 0xC30 bit 0x40
      Machine_IncrementCharge (0x801CC480)
```

**`Machine_IncrementCharge` formula** (cleaned up from decompilation):

```c
// 1. Compute steering angle factor (0.0 = straight, 1.0 = max turn) from the
//    velocity vector (md+0x324) and the up vector (md+0x424), via
//    PSVECMagnitude (0x803d2158) → VEC_Reflection (0x80064c18) →
//    Vec_GetAngleBetween (0x80062ecc), normalized by a r2-relative constant.
angle_factor = compute_angle_factor(&md->velocity /*0x324*/, &md->up /*0x424*/);

// 2. Interpolate between base and turning charge rates
charge_rate = base_charge_rate + angle_factor * (turning_charge_rate - base_charge_rate);

// 3. Guard conditions: skip increment (jump to step 7) if any of:
//    - md->0xC35 bit 0x01 is set (unknown block flag)
//    - md->0xC36 is negative (sign-bit / 0x80 test on the byte)
//    - md->0x794 != 0 (post-discharge cooldown active)

// 4. If charge_value < 1.0:
charge_value += charge_rate;
if (charge_value > 1.0):
    charge_value = 1.0;
    // Charge is full:
    md->0x790 = md->0x4B0;  // start auto-discharge timer
    Machine_PlaySFX(md, 3);  // "charge full" sound
    Machine_ApplyColAnim(md, 0x1D, 0);  // glow effect

// 5. Mirror charge to 0x798 for display
md->0x798 = charge_value;
// 6. Set/clear effect flags in byte md+0xC32 (set 0x40, set 0x20, clear 0x10).
//    NOTE: this is *not* charge_is_grounded (0xC30 bit 0x40) — that flag is
//    *read* by MachinePhys_Charge to gate whether this function runs at all.

// 7. (always runs, even when steps 3-6 were guarded out) If the computed
//    charge_rate != 0.0 AND charge_value != 1.0 (actively charging, not full),
//    set md+0xC32 bit 0x08 ("charging in progress" marker).
```

### 3. Charge Hold

While held at full charge, `Machine_ChargeUpdate` (0x801CA4C0) decrements the timer at 0x790. When it reaches 0, the charge auto-discharges:

```c
// Auto-discharge when timer expires:
if (md->0x790 != 0) {
    md->0x790--;
    if (md->0x790 == 0) {
        // Clear charge state
        charge_value = 0.0;
        md->0x798 = 0.0;
        md->0x790 = 0;
        // Start cooldown timer
        md->0x794 = md->0x4B4;
        Machine_PlaySFX(md, release_sfx);
        Machine_ApplyColAnim(md, 0x1F, 0);  // discharge visual
    }
}
```

### 4. Charge Release (Boost)

When the player releases A:
```
AS_StarChargeRelease (0x801ABC64)
  → RiderStateChange to state 0x2A
  → Rider_CopyInputToMachine
  → boost applied proportional to charge_value
```

The boost magnitude is proportional to `charge_value` at the moment of release.

### 5. Charge Cleared

`Machine_ClearChargeState` (0x801CA294) resets all charge fields:
```c
charge_value = 0.0;
md->0x798 = 0.0;
md->0x790 = 0;
// clear bitfield flags
```

Called on: hit reactions, state transitions, death, getting knocked off, etc. (11+ call sites).

## Call Hierarchy Summary

```
Machine_ChargeThink? (0x801C5FE0) — per-frame top-level
  ├── Machine_NullCharge — zeros input fields 0x664/0x668/0x66C
  ├── Machine_ChargeUpdate — manages timers (0x790, 0x794)
  ├── [physics func pointer] → MachinePhys_Charge
  │     ├── ... movement/collision ...
  │     └── if grounded: Machine_IncrementCharge — adds to charge_value
  └── ... other per-frame updates ...
```

## EnergyLink Interaction

Charge is one of three energy sources tracked by EnergyLink (the others are objects destroyed and patches collected). Charge gain is the positive frame-to-frame delta of `md->charge_value`, scaled by `CHARGE_ENERGY_SCALE` (5.0) — a full 0→1 charge generates 5 energy.

Auto-Charge (opt-in toggle under Settings → Energy Link → Auto-Charge) does the inverse: each frame it adds a **capped** amount to `charge_value` by spending `gain * SCALE` energy from the local balance, where `gain = min(1.0 - charge_value, rate_cap)`. The cap is a fixed per-frame rate selected by the **Auto-Charge Rate** setting (Slow `0.00555` / Medium `0.01111` / Fast `0.02222`), so the meter rises steadily and stacks with the player's own charging instead of snapping to full. After injection, `prev_charge_value` is re-snapped so the injected charge is invisible to the next send delta — this is the only feedback-loop guard in the file. See `docs/energylink.md` for the rate table and energy-accounting details.

Notes specific to charge:
- **No SFX/visual on Auto-Charge inject** — bumping `charge_value` doesn't trigger the "charge full" sound or glow. The boost is invisible until the player next holds A.
- **No auto-discharge trigger** — vanilla sets `charge_full_timer` via `Machine_IncrementCharge`. External writes bypass this; the timer is set the next frame the player charges and `Machine_IncrementCharge` sees `charge_value >= 1.0`.
- **State-agnostic** — Auto-Charge fills regardless of machine state. Hit reactions / `Machine_ClearChargeState` paths will clear the injected value normally.
- **Meta Knight excluded** — Auto-Charge skips the Wing machine (`VCKIND_WINGMETAKNIGHT`), whose `charge_value` is a raw speed term rather than a chargeable boost meter; pinning it would give a constant max-speed buff. Dedede has a normal meter and is included.

For the rest of the EnergyLink design (accumulator semantics, baseline gating, patch-receive feedback handling, Top Ride tracking), see `docs/energylink.md`.

## Reference: Key Addresses

| Address | Size | Symbol |
|---------|------|--------|
| 0x801AB624 | 0x64 | `Rider_IASACheck_Charge` |
| 0x801AB688 | 0xAC | `AS_StarBeginCharge` |
| 0x801AB940 | 0x90 | `AS_StarChargeHold` |
| 0x801ABAD0 | 0x58 | `chargeMain` |
| 0x801ABC64 | 0x4C | `AS_StarChargeRelease` |
| 0x801C5FE0 | 0x1AC | `Machine_ChargeThink?` |
| 0x801C75F4 | 0x14 | `Rider_UpdateIsCharge` |
| 0x801C8EDC | 0x18 | `Machine_NullCharge` |
| 0x801CA294 | 0x68 | `Machine_ClearChargeState` |
| 0x801CA4C0 | 0x124 | `Machine_ChargeUpdate` |
| 0x801CC480 | 0x178 | `Machine_IncrementCharge` |
| 0x801CCEB8 | 0x394 | `Rider_UpdateCharge` |
| 0x801EC5CC | 0x1C0 | `Machine_RotateDuringCharge` |
| 0x801EF150 | 0x78 | `Machine_CheckEnterCharge` |
| 0x801EF278 | 0x7C | `Machine_EnterCharge` |
| 0x801EF364 | 0xF0 | `MachinePhys_Charge` |
