# Machine Charge System

Holding A while grounded fills a 0.0-1.0 meter; releasing it spends the meter on a boost. This is the 3D-mode engine, which lives on `MachineData`. Top Ride has an unrelated charge component of its own, with different fields and a per-frame decay branch, and none of this applies to it.

`MachineData.charge_value` (+0x78c) is the meter, and `charge_display_value` (+0x798) is the copy the HUD reads, rewritten on every frame the meter moves. Neither is the charge *stat*: `stats.values[PATCHKIND_CHARGE]` (+0x95c) is a patch-modifiable attribute that scales `base_charge_rate`, so charge patches change how fast the meter fills, not how full it is.

## Entering the state

`Rider_IASACheck_Charge` (0x801ab624) tests the A button - mask 0x100 in the rider's button word - and enters rider state 0x28 through `AS_StarBeginCharge` (0x801ab688). That calls `Machine_CheckEnterCharge` (0x801ef150), which checks the machine's own input word at +0x66c and hands off to `Machine_EnterCharge` (0x801ef278) to move the machine to state 0xF.

Rider state and machine state are separate machines and both have to move. `Rider_UpdateIsCharge` (0x801c75f4) is what copies the stick and the button into the machine's input words (+0x664 stick, +0x66c buttons), which is why the machine-side check reads its own copy rather than the controller.

## Accumulating

The charge state's physics function is `MachinePhys_Charge` (0x801ef364). It runs the ordinary movement and collision, then calls `Machine_IncrementCharge` (0x801cc480) only while `charge_is_grounded` (+0xc30 bit 0x40) is set. That gate is what makes charging a grounded-only move; bikes hold the flag set always.

`Machine_IncrementCharge` interpolates the rate by how far the machine is turned:

```
r    = VEC_Reflection(md->up /*0x424*/, md->velocity /*0x324*/)
a    = Vec_GetAngleBetween(r, md->forward /*0x418*/)      // radians
rate = base_charge_rate + (a / pi) * (turning_charge_rate - base_charge_rate)
```

`PSVECMagnitude` (0x803d2158) guards both vectors at 1e-5; below that the angle is taken as 0 and the rate is the base one, so a stationary machine charges at `base_charge_rate`.

Three conditions skip the increment: +0xc35 bit 0x01, the sign bit of the byte at +0xc36, and a nonzero `charge_cooldown_timer` (+0x794). While any holds, the meter, the display mirror and two of the effect flags are all left alone.

Otherwise the rate is added and, if that crosses 1.0, the meter clamps there, `charge_full_timer` (+0x790) is loaded from `charge_full_duration` (+0x4b0), `Machine_PlaySFX(md, 3)` plays the charge-full cue and `Machine_ApplyColAnim(md, 0x1D, 0)` puts the glow on. Filling the meter plays nothing else and starts nothing else - it arms the auto-discharge timer.

Three flags in the byte at +0xc32 come out of this function. Bits 0x40 and 0x20 are set and 0x10 cleared on every frame the meter actually moves, and the audio path tests that pair to keep the charge loop alive, so the loop dies the instant an increment is skipped. Bit 0x08 is set whenever the computed rate is nonzero and the meter is not full, and that one runs even on the frames the guards skipped everything else.

`Machine_AddCharge` (0x801ca334) and `Machine_AddChargeEx` (0x801cc378) are the explicit-rate siblings, taking the rate in `f1` for vehicle phases that compute it themselves - star glide, rail run, the wheelie push. Both add, clamp and fire the same full-charge effects; only `AddChargeEx` also updates the +0xc32 flags.

## Holding, and the overcharge penalty

`Machine_ChargeUpdate` (0x801ca4c0) runs each frame from `Machine_ChargeThink` (0x801c5fe0) and does three things in order.

It clears +0xc32 bits 0x20 and 0x40 first, which is the other half of the arrangement above: those flags are re-set only by an increment, so anything that stops the meter moving stops the charge sound on the same frame.

It then decrements `charge_cooldown_timer` if it is running, and clears the 0x1F overlay when it reaches zero.

Finally it decrements `charge_full_timer`, and reaching zero there is the auto-discharge: `charge_value` and `charge_display_value` are zeroed, the 0x1D glow is taken off, `charge_cooldown_timer` is loaded from `charge_cooldown_duration` (+0x4b4), the machine's `overheat_loop_sfx` plays at full volume, and ColAnim 0x1F goes on. Despite the field's name that sound is a one-shot penalty cue for overcharging, not a loop.

## Release and clearing

`AS_StarChargeRelease` (0x801abc64) moves the rider to state 0x2A, copies input to the machine and applies a boost proportional to `charge_value` at the moment of release.

What "proportional" means is a per-machine table. `Machine_ApplyChargeBoost` (0x801da3c0) calls `LerpTable(0.1, charge_display_value, boost_gain)` (0x80062c4c) over the eleven floats at `vcData->attr+0x0a8`, which the attribute memcpy puts at `MachineData+0x508`: entry *n* is the gain at *n*/10 charge, and the sample lerps between neighbours. The result is scaled by `boost_gain_any` (attr `+0x0d8`, 1.0 on every machine) and becomes the boost velocity. Hydra's first eight entries are 0 and its last three are 0.03, which is the whole of its charge requirement; Rocket Star holds 0.01 for ten entries and jumps to 3.3 at full; Wagon Star's are all zero, so it has no charge boost. `charge_deplete_rate` (attr `+0x0a4`) then decides how fast the boost bleeds off - 1.0 on most machines, 0.00012 on Hydra.

`charge_full_duration` (+0x4b0) and `charge_cooldown_duration` (+0x4b4) are themselves attributes, at `+0x050` and `+0x054` of the archive block, and both are scaled by the charge stat. Slick Star ships 360 and 60; Hydra ships 1600.

`Machine_ClearChargeState` (0x801ca294) zeroes the meter, the display mirror, the full timer and the flags. Eleven-odd sites call it - hit reactions, state transitions, death, being knocked off the machine - and they clear an externally injected meter as readily as an earned one.

Other functions on the path: `AS_StarChargeHold` (0x801ab940) is rider state 0x28's think; `chargeMain` (0x801abad0) is the shared per-frame charge routine those states call, which also tests for dismount; `Rider_UpdateCharge` (0x801cceb8) is the rider-side ground update, re-running the point collision and reading ground type and traction alongside the same velocity/up/forward angle math; `Machine_RotateDuringCharge` (0x801ec5cc) turns the machine in place while it charges; and `Machine_NullCharge` (0x801c8edc), the first call in `Machine_ChargeThink`, zeroes the machine's input words at +0x664, +0x668 and +0x66c.

## External writes to `charge_value`

EnergyLink's Auto-Charge writes `md->charge_value` from outside the engine's charge path and reads its positive frame-to-frame delta back as an energy source. How the engine reacts to a write it did not make:

- **No sound and no glow.** The charge-full cue and the 0x1D overlay only fire on `Machine_IncrementCharge`'s own crossing of 1.0, so a meter filled from outside is invisible until the player next holds A.
- **No auto-discharge.** `charge_full_timer` is loaded only by `Machine_IncrementCharge`, so an externally filled meter never starts its own countdown. It gets one on the first frame the player charges with the meter already at or above 1.0.
- **State-agnostic.** Nothing gates an external write on the charge state, `charge_is_grounded` or `charge_cooldown_timer`. The engine's own clears still apply: any `Machine_ClearChargeState` path zeroes the injected value.
- **`VCKIND_WINGMETAKNIGHT` is not a charge meter.** Meta Knight's Wing machine reads `charge_value` as a raw speed term, so writing it every frame is a permanent top-speed buff rather than a stored boost. King Dedede's meter is ordinary.
