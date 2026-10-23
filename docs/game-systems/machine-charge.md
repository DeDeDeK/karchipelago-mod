# Machine Charge

Holding A on a grounded machine fills a 0.0-1.0 charge meter, and letting go spends it on a boost.
The meter lives on `MachineData` and exists in the 3D modes only. Top Ride has an unrelated charge
component with its own fields and a per-frame decay, and none of this applies to it. A modder reads
this to change how charge fills, what a release is worth, or to write the meter from outside the
engine.

## The meter

| `MachineData` | Offset | Role |
|---|---|---|
| `charge_value` | `+0x78c` | The meter, 0.0-1.0 |
| `charge_full_timer` | `+0x790` | Frames left before a full meter auto-discharges |
| `charge_cooldown_timer` | `+0x794` | Frames left of the overcharge lockout |
| `charge_display_value` | `+0x798` | The copy the HUD and the boost read, rewritten whenever the meter moves |
| `charge_full_duration` / `charge_cooldown_duration` | `+0x4b0` / `+0x4b4` | The two timer seeds, from `vcAttributes +0x050` / `+0x054` |
| `xc32` | `+0xc32` | Charge flags (below) |

The meter is not the Charge stat. `stats.values[PATCHKIND_CHARGE]` (`+0x95c`) is a patch stat that
scales the fill rates and the two durations. Charge patches change how fast the meter fills, not how
full it is.

## Entering the charge

`Rider_IASACheck_Charge` (0x801ab624) first lets a held Plasma ability take the button. Otherwise it
tests A (mask 0x100 in the rider's held buttons) and enters rider state 0x28 through
`AS_StarBeginCharge` (0x801ab688). That calls `Machine_CheckEnterCharge` (0x801ef150), which reads
the machine's own button word at `+0x66c` and hands off to `Machine_EnterCharge` (0x801ef278) to
move the machine to state 0xF.

The rider and the machine run separate state machines, and both have to move. `Machine_SetInput`
(0x801c75f4) copies the stick and buttons into the machine's input words (`+0x664` stick Y,
`+0x668` stick X, `+0x66c` buttons). `Machine_NullCharge` (0x801c8edc), the first call in
`Machine_FrameStartThink` (0x801c5fe0), zeroes them each frame.

## Filling

The charge state's physics, `MachinePhys_Charge` (0x801ef364), runs the ordinary movement and then
calls `Machine_IncrementCharge` (0x801cc480) only while `charge_is_grounded` (`+0xc30` bit 0x40) is
set. That gate is what makes charging grounded-only on a star; `Machine_Star_PushChargeUpdate`
(0x801ef338) applies the same test. Bikes have no gate: `Machine_Wheel_PushChargeUpdate`
(0x801fa1c8) increments unconditionally.

`Machine_IncrementCharge` interpolates the rate by how far the machine is turned:

```
r    = VEC_Reflection(md->up, md->velocity)
a    = angle between r and md->forward          (0 below 1e-5 of either length)
rate = base_charge_rate + (a / pi) * (turning_charge_rate - base_charge_rate)
```

`VEC_Reflection` (0x80064c18) is, despite its name, a plane projection `v - n * dot(n, v)`, so `a`
is the slip angle between the ground-plane velocity and the heading, the value
`Machine_GetSlipAngle` (0x801cc2fc) returns. A stationary machine charges at the base rate. The
two rates are `vcAttributes.charge_rate` and `charge_rate_turning` after stat scaling.

Three things skip the add: `+0xc35` bit 0x01, the sign bit of the byte at `+0xc36`, and a running
`charge_cooldown_timer`. Otherwise the rate is added to a meter below 1.0. If that crosses 1.0, the
meter clamps there, `charge_full_timer` is loaded from `charge_full_duration`,
`Machine_PlaySFX(md, 3)` plays the full-charge cue and `Machine_ApplyColAnim(md, 0x1D, 0)` starts
the glow. A meter already at 1.0 is left alone, so the cue and the timer fire only on the crossing.

The function also sets flags in `xc32`. Bits 0x40 and 0x20 are set and 0x10 cleared on every
frame the add runs. The machine audio tests that pair to keep the charge loop playing, so the loop
dies the frame an add is skipped. Bit 0x08 is set whenever the computed rate is nonzero and the
meter is not full, even on a skipped frame.

Two siblings take an explicit rate in `f1`, for vehicle phases that compute their own (star flight,
rail run, the wheelie push). Both apply the same three skips, clamp, play the cue and set the glow.
Star flight is a passive fill: every frame of the Fly state adds `vcAttributes.air_charge_rate`
(`+0x160`, 0.0025 on most stars, not stat-scaled) through `Machine_AddCharge`, A or not.

| Function | Arms the auto-discharge | Flags |
|---|---|---|
| `Machine_AddChargeEx` (0x801cc378) | Yes | As `Machine_IncrementCharge` |
| `Machine_AddCharge` (0x801ca334) | No | Sets 0x40, clears 0x20 and 0x10 |

## Holding and the overcharge

`Machine_ChargeUpdate` (0x801ca4c0) runs every frame from `Machine_FrameStartThink`:

1. It clears `xc32` bits 0x20 and 0x40. Only an add sets them again, so anything that stops the
   meter moving stops the charge sound on the same frame.
2. It counts `charge_cooldown_timer` down, and removes the 0x1F overlay when it reaches zero.
3. It counts `charge_full_timer` down. Reaching zero is the auto-discharge: the meter and its display
   copy are zeroed, the 0x1D glow comes off, and `charge_cooldown_timer` is loaded from
   `charge_cooldown_duration`. The machine's `overheat_loop_sfx` then plays at full volume and
   ColAnim 0x1F goes on. Despite its name that sound is a one-shot penalty cue, not a loop.

Slick Star holds a full meter for 360 frames and locks out for 60; Hydra holds for 1600.

## Release

`RiderState_StarChargeReleaseEnter` (0x801abc64) moves the rider to state 0x2A and copies input to
the machine. It applies no boost itself; the machine's next state spends the meter.

| Where | States | Boost function |
|---|---|---|
| Ground | Run, Landing | `Machine_ApplyGroundChargeBoost` (0x801d93a0) |
| Rail | RailRun, RailChange | `Machine_ApplyChargeBoost` (0x801da3c0) |

Stars and bikes share both functions. Each does nothing while `charge_display_value` is 0 or while
`xc32` bit 0x40 says charge was added this frame, and each runs every frame of the release:

- **Gain.** `LerpTable(0.1, charge_display_value, boost_gain)` (0x80062c4c) over the eleven floats
  of `vcAttributes.boost_gain` (`+0x0a8`, `MachineData + 0x508`). `LerpTable` takes
  `i = (int)(charge / 0.1)` and lerps between entries `i` and `i + 1`, so entry `n` is the gain at
  `n / 10` charge. The rail version scales it by `boost_gain_any` (`+0x0d8`, 1.0 on every machine).
  The ground version lerps that multiplier up to `boost_gain_sliding` (`+0x0dc`, 2.1 on most
  machines) as the slip angle goes from 5 to 180 degrees, so a release while sliding sideways is
  worth up to twice as much. The result is this frame's `boost_accel` (`+0x79c`); a star's ground
  release (`Machine_Star_UpdateGroundBoost`, 0x801ecd00) also adds it to `star_dash_accum`.
- **Redirect.** On its first frame the ground version turns the velocity onto the heading,
  `v = forward * |v| * k + v * (1 - k)` with `k = boost_redirect` (`+0x0d4`): 0.6 on most machines,
  1.0 on Rocket Star and Wagon Star, 0.07 on Formula Star. The rail version has no redirect.
- **Bleed.** `Machine_DepleteCharge` (0x801ca3f8) takes `charge_deplete_rate` (`+0x0a4`) off the
  meter and `min(rate, 0.1)` off `charge_display_value`. The gain tapers as the display falls, and
  the release ends when it reaches 0, clearing `xc32` bit 0x10. At the usual rate of 1.0 a full
  release lasts 10 frames; Wagon Star's 0.06 stretches it to about 17 and Bulk Star's 0.0036 to
  about 280, and Hydra's 0.00012 makes its boost near-permanent once earned.

`xc32` bit 0x10 marks a release in progress. `xc34` bit 0x02 is set on each frame a boost is
applied and cleared by `Machine_FrameStartThink`; `Machine_UpdateBoostHitbox` (0x801d7bf4) arms the
boost hitbox on its rising edge and runs it for `boost_hitbox_timer` (`+0xb38`) frames.

| Machine | 0.0 | 0.1 | 0.2 | 0.3 | 0.4 | 0.5 | 0.6 | 0.7 | 0.8 | 0.9 | 1.0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Warp Star | 0.01 | 0.015 | 0.021 | 0.036 | 0.0556 | 0.0818 | 0.1051 | 0.135 | 0.16 | 0.18 | 0.2 |
| Slick Star | 0.01 | 0.01 | 0.01 | 0.02 | 0.03 | 0.04 | 0.05 | 0.06 | 0.07 | 0.08 | 0.14 |
| Dragoon | 0.01 | 0.015 | 0.026 | 0.043 | 0.063 | 0.084 | 0.115 | 0.153 | 0.202 | 0.268 | 0.37 |
| Hydra | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0.03 | 0.03 | 0.03 |
| Rocket Star | 0.01 | 0.01 | 0.01 | 0.01 | 0.01 | 0.01 | 0.01 | 0.01 | 0.01 | 0.01 | 3.3 |
| Bulk Star | 0.0092 | 0.0092 | 0.0092 | 0.0092 | 0.0092 | 0.0092 | 0.0092 | 0.0092 | 0.0092 | 0.0092 | 0.0092 |

Charge requirements are expressed entirely in this data. Hydra's eight leading zeros are its charge
requirement, Rocket Star's cliff at 1.0 is its launch, and Wagon Star's curve is all zero, so it has
no charge boost.

## Clearing

`Machine_ClearChargeState` (0x801ca294) zeroes the meter, its display copy and the full timer,
clears the flags and removes the glow. Many paths call it: hit reactions, state transitions,
death, being knocked off. It clears an injected meter as readily as an earned one.

## Writing the meter from outside

A mod that writes `charge_value` directly bypasses the engine's crossing logic:

- **No cue and no glow.** Both fire only on an add that crosses 1.0, so a meter written full is
  silent and unlit.
- **No auto-discharge for a full write.** A meter written to 1.0 or more is never added to again,
  so `charge_full_timer` never loads and the meter stays full until it is spent or cleared. A
  meter written just under 1.0 gets the cue, the glow and the countdown on the first charging frame
  that crosses.
- **No state gate.** Nothing ties a write to the charge state, `charge_is_grounded` or the
  cooldown, and every `Machine_ClearChargeState` path still zeroes it.
- **The release reads the display copy.** Both boost functions read `charge_display_value`, which a
  write to `charge_value` alone does not change.

## Related functions

| Function | Address | Role |
|---|---|---|
| `AS_StarChargeHold` | 0x801ab940 | Rider state 0x28's think |
| `chargeMain` | 0x801abad0 | Shared per-frame charge routine for the rider's charge states; also tests for dismount |
| `Machine_UpdateMapColl` | 0x801cceb8 | The machine's per-frame map collision update, which keeps running through the charge |
| `Machine_RotateDuringCharge` | 0x801ec5cc | Turns the machine while it charges, from its own handling terms |
