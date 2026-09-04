# Machine Attributes

Everything about how a vehicle drives is static data shipped in its `Vc*.dat`, in two
blocks that the engine copies into the live `MachineData` and then scales by the player's
patch stats. This is the whole of a machine's tuning: the physics code is shared by every
star, and what separates Slick Star from Swerve Star is these numbers.

## The two blocks

A `Vc*.dat`'s one public is a `vcData`, and two of its seven pointers are attributes:

| `vcData` field | Struct | Size | Copied to |
|---|---|---|---|
| `+0x00` `attr` | `vcAttributes` | `0x1f0` | `MachineData + 0x460` |
| `+0x14` `handling_attr` | `vcHandlingAttr` | `0xf8` | `md->attr->handling`, i.e. `*(md+0x650) + 0xa8` |

`MachineData + 0x650` is a separate `0x1a4`-byte allocation made per machine by
`Machine_AllocAttrStruct` (`0x801c71a8`), not a pointer into `MachineData`. Its first
`0xa8` bytes are a **per-class** block shared by every star, whose `+0x1c` is the class-wide
speed cap `accelerateStar` (`0x801ec074`) clamps velocity to. The handling block follows at
`+0xa8`; that half is the machine's own.

Both classes author a `0xf8` handling block, but the star and bike controllers read
different subsets of it, so a field named for one class means nothing under the other.

## The rebuild

`Machine_AdjustAttributes` (`0x801c7278`) is the one entry point, and it rebuilds a machine
completely:

1. memcpy 62 pairs of words - `0x1f0` bytes - from `md->vcData->attr` to `md+0x460`, so
   `vcAttributes` field `k` lands at `md + 0x460 + k`. `hp_max` is `+0x4cc`,
   `top_speed_ground` is `+0x4f0`, `top_speed_air` is `+0x5ac`;
2. dispatch through a per-`is_bike` table at `r13+0x770`: `+0x1c` is
   `Machine_CopyCommonAttributes` (`0x801e812c`), which refills `md->attr` - the class block
   from a table indexed by `MachineData.kind`, the handling half from
   `md->vcData->handling_attr` - and `+0x20` is `Machine_AdjustAttributesStar`
   (`0x801e906c`) or `Machine_AdjustAttributesBike` (`0x801f4dac`), which apply the stat
   scaling;
3. set `top_speed_current` (`+0x398`) from `top_speed_ground` while `action_state_class`
   (`+0x754`) is 0 and from `top_speed_air` otherwise, and carry the change in `hp_max` into
   current HP, clamping down if the new maximum is lower.

It runs at spawn and again on every patch pickup, through `Machine_GivePatch` /
`Machine_GiveAllUp` - so it is safe to call mid-round, and calling it re-applies the patch
stats from scratch rather than stacking them.

**Three fields it does not touch.** `Machine_Star_Init` (`0x801e7f3c`) seeds them once from
the attributes and nothing rewrites them afterwards, so an attribute swap that goes through
`Machine_AdjustAttributes` alone leaves them on the old machine's values:

| `MachineData` | Seeded from |
|---|---|
| `ground_grip` (`+0x6ec`) | `vcAttributes.ground_grip` |
| `air_grip` (`+0x6f0`) | `vcAttributes.air_grip` |
| `lift_max` (`+0x388`), and `lift_accum` (`+0x384`) with it | `handling.lift_ceiling` |

## The stat map

`Machine_ApplyStarStatScaling` (`0x801e81e4`) is the game's own statement of which attribute
belongs to which of the nine patch stats. Each site is
`field *= Machine_ScaleFromRatio(pair, Machine_GetStatRatio(md, stat))`, where the pair
comes from a per-class table at `md+0x658` and `Machine_ScaleFromRatio` (`0x801cab4c`)
returns exactly `1.0` at ratio 0 - so **the shipped value is the machine at zero patches**,
which is what it is in Air Ride and Top Ride, where the stat arrays stay zero.

| Stat | `vcAttributes` | `vcHandlingAttr` |
|---|---|---|
| Boost | `top_speed_ground`, `slope_speed_up`, `boost_gain_any`, `boost_gain_sliding`, `takeoff_speed`, `+0x1a4` | `accel_floor`, `accel_turn_keep`, `x044[0..4]`, `x06c[1]`, `pitch_max_up`, `air_accel` |
| Top Speed | `top_speed_ground`, `top_speed_air`, `+0x1a0` | `accel_floor`, `x044[0..4]`, `x06c[1]`, `air_accel`, `air_impulse` |
| Turn | `glide_up_speed`, `glide_down_speed`, `+0x18c`, `+0x190`, `+0x19c` | `turn_rate_rest`, `turn_rate_top`, `x06c[0..2]`, `x094`, `lean_approach`, `lean_step_max` |
| Charge | `charge_rate`, `charge_rate_turning`, `charge_full_duration`, `charge_cooldown_duration` | `x044[0..4]` |
| Glide | the four `descent_*`, `glide_up_speed`, `glide_down_speed`, `x164`/`x168`/`x16c`/`x170`, `turn_speed_on_slope`, `base_offense`, `base_defense` | `lift_ceiling`, `x028[5..6]`, `x044[0..4]`, `lean_step_max`, `lean_step_max_0`, `air_accel`, `air_accel_fwd`, `air_accel_back`, `x0d0[0]` |
| Weight | `top_speed_ground`, `slope_speed_up`, `slope_speed_down`, `ground_grip`, `base_hp`-adjacent damage terms, `air_grip`, the fall tiers | most of the above, plus `air_impulse` and `air_recover_len` |
| Offense | `hitbox_size`, `+0x088` | - |
| Defense | `base_hp`, `base_defense`, `base_offense` | - |
| HP | - | - |

Two rows are worth reading twice. `x044[0..4]` (handling `+0x044`..`+0x054`) is scaled by
five of the nine stats and has no direct reader anywhere in the machine or rider code, so
whatever consumes it lives elsewhere. And the Weight stat touches nearly everything, which
is why a heavy machine feels different in every axis rather than just slower.

## The main levers

Ranked by how far machines spread on them and how directly they change the feel.

**Grip.** `vcAttributes.ground_grip` (`+0x0e0`) is how hard velocity is dragged onto the
heading each frame - `Machine_Star_Init` copies it to `MachineData.ground_grip` and
`Machine_Star_ApplyGrip` (`0x801ebc90`) spends it. It is the single biggest difference in
how a machine feels. `air_grip` (`+0x150`) is the same thing off the ground.

**Yaw.** `Machine_RotateDuringCharge` (`0x801ec5cc`) is the grounded steer for every driving
state, not only the charge - `Star_RunPushForward_3`, `Star_Landing_3`, `MachinePhys_Charge`
and `CompactStar_PhysicsThink` all call it. It computes

```
turn = -stick * lerp(handling.turn_rate_rest, handling.turn_rate_top,
                     |velocity| / top_speed_current)
```

and then, if the machine is already slipping past `handling.slip_penalty_deg`, multiplies by
`handling.slip_penalty` (0.2 on every machine). So `turn_rate_rest` is the yaw standing
still and `turn_rate_top` the yaw at the cap. Swerve Star's `0 -> 0.1` is why it cannot
pivot in place and carves hard at speed; Formula Star's `0.002 -> 0.5` is the opposite
extreme.

**Acceleration.** `Machine_Star_UpdateThrust` (`0x801eb57c`) rebuilds `MachineData.thrust`
(`+0x6e8`) on every state entry from the active top speed and `handling.accel_floor`
grounded / `handling.air_accel` airborne, each capped at that top speed.
`Machine_Star_ApplyGroundThrust` (`0x801ecae4`) spends it into `MachineData.accel`, scaled
down toward `handling.accel_turn_keep` as slip grows and biased by `slope_speed_up` /
`slope_speed_down`. `Machine_Star_ApplyAirThrust` (`0x801ed4d8`) is the airborne twin, with
`handling.air_accel_fwd` applied while the stick agrees with the heading and
`air_accel_back` while it opposes.

| Machine | grip | air grip | yaw rest | yaw top | accel floor | turn keep | ground cap | air cap | full boost | boost decay | air impulse |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Warp Star | 0.233 | 0.4 | 0.029 | 0.0225 | 1.2 | 0.037 | 1.644 | 1.863 | 0.2 | 1 | 500 |
| Compact Star | 0.8 | 0.3 | 0.041 | 0.1 | 1.2 | 0.044 | 1.174 | 1.847 | 0.2 | 0.33 | 450 |
| Winged Star | 0.233 | 0.6 | 0.027 | 0.018 | 1.1 | 0.03 | 1.519 | 2.045 | 0.2 | 1 | 1000 |
| Shadow Star | 0.18 | 0.7 | 0.043 | 0.01 | 1.3 | 0.06 | 1.296 | 1.822 | 0.2441 | 1 | 400 |
| Hydra | 0.1 | 0.8 | 0.04 | 0.04 | 4 | 0.07 | 1.215 | 2.227 | 0.03 | 0.00012 | 400 |
| Bulk Star | 0.233 | 0.005 | 0.03 | 0.02 | 1.8 | 0.05 | 1.62 | 1.701 | 0.0092 | 0.0036 | 1 |
| Slick Star | 0.01 | 0.3 | 0.043 | 0.01 | 0 | 0.05 | 1.762 | 1.944 | 0.14 | 1 | 600 |
| Formula Star | 0.233 | 0.1 | 0.002 | 0.5 | 1.6 | 0.025 | 2.795 | 2.227 | 0.2218 | 1 | 20 |
| Dragoon | 0.2 | 1.5 | 0.017 | 0.015 | 1.2 | 0.037 | 2.43 | 3.645 | 0.37 | 1 | 1500 |
| Wagon Star | 0.5 | 0.6 | 0.038 | 0.02 | 2.3 | 0.042 | 1.701 | 2.025 | 0 | 0.06 | 22 |
| Rocket Star | 0.25 | 3 | 0.025 | 0.02 | 1.1 | 0.033 | 1.013 | 1.215 | 3.3 | 1 | 600 |
| Swerve Star | 0.233 | 0.2 | 0 | 0.1 | 1.2 | 0.037 | 2.033 | 1.932 | 0.28 | 0.08 | 230 |
| Turbo Star | 0.15 | 0.01 | 0.018 | 0.015 | 1.2 | 0.1 | 1.863 | 1.62 | 0.22 | 1 | 60 |
| Jet Star | 0.233 | 0.25 | 0.035 | 0.04 | 1.8 | 0.05 | 1.337 | 1.579 | 0.2694 | 1 | 400 |
| Flight Warp Star | 0.233 | 0.6 | 0.029 | 0.0225 | 1.2 | 0.037 | 1.661 | 2.025 | 0.25 | 1 | 1800 |
| Free Star | 0.233 | 0.4 | 0.029 | 0.0225 | 1.2 | 0.037 | 1.661 | 1.903 | 0.2 | 0.33 | 500 |
| Steer Star | 0.233 | 0.4 | 0.029 | 0.0225 | 1.2 | 0.037 | 1.661 | 1.903 | 0.2 | 0.33 | 500 |

Free Star, Steer Star and Flight Warp Star are Warp Star variants and share most of its
handling block; Flight Warp Star's whole difference is thirty-four fields.

## The boost curve

`vcAttributes.boost_gain[11]` (`+0x0a8`..`+0x0d0`) is what a charge release is worth.
`Machine_ApplyChargeBoost` (`0x801da3c0`) calls `LerpTable(0.1, charge_display_value,
boost_gain)` (`0x80062c4c`), which takes `i = (int)(charge / 0.1)` and lerps between
`boost_gain[i]` and `boost_gain[i+1]`, then multiplies by `boost_gain_any` - 1.0 on every
machine - and writes the boost velocity. Entry `n` is therefore the gain at `n/10` charge.

| Machine | 0.0 | 0.1 | 0.2 | 0.3 | 0.4 | 0.5 | 0.6 | 0.7 | 0.8 | 0.9 | 1.0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Slick Star | 0.01 | 0.01 | 0.01 | 0.02 | 0.03 | 0.04 | 0.05 | 0.06 | 0.07 | 0.08 | 0.14 |
| Warp Star | 0.01 | 0.015 | 0.021 | 0.036 | 0.0556 | 0.0818 | 0.1051 | 0.135 | 0.16 | 0.18 | 0.2 |
| Flight Warp Star | 0.0102 | 0.0187 | 0.0276 | 0.0432 | 0.0693 | 0.0952 | 0.1254 | 0.1566 | 0.1888 | 0.2196 | 0.25 |
| Turbo Star | 0.01 | 0.015 | 0.02 | 0.03 | 0.08 | 0.1 | 0.13 | 0.15 | 0.17 | 0.2 | 0.22 |
| Jet Star | 0.01 | 0.015 | 0.0252 | 0.0378 | 0.0577 | 0.0856 | 0.1171 | 0.1559 | 0.2045 | 0.2414 | 0.2694 |
| Swerve Star | 0.01 | 0.0216 | 0.0288 | 0.036 | 0.0459 | 0.0613 | 0.0829 | 0.1081 | 0.15 | 0.1937 | 0.28 |
| Dragoon | 0.01 | 0.015 | 0.026 | 0.043 | 0.063 | 0.084 | 0.115 | 0.153 | 0.202 | 0.268 | 0.37 |
| Formula Star | 0.003 | 0.0053 | 0.0096 | 0.018 | 0.0278 | 0.0428 | 0.0593 | 0.08 | 0.1089 | 0.1471 | 0.2218 |
| Hydra | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0.03 | 0.03 | 0.03 |
| Rocket Star | 0.01 | 0.01 | 0.01 | 0.01 | 0.01 | 0.01 | 0.01 | 0.01 | 0.01 | 0.01 | 3.3 |

Hydra's eight leading zeros are its charging requirement, expressed entirely in data. Rocket
Star's cliff at 1.0 is its launch. Wagon Star's row is all zero, so it has no charge boost at
all.

Three attributes go with the curve. `charge_rate` (`+0x09c`) and `charge_rate_turning`
(`+0x0a0`) are the per-frame fill rates that `Machine_IncrementCharge` interpolates between
by how far the machine is turned. `charge_deplete_rate` (`+0x0a4`) is how fast a spent boost
bleeds off - Hydra's `0.00012` is why its boost is effectively permanent once earned.
`charge_full_duration` (`+0x050`) and `charge_cooldown_duration` (`+0x054`) are the frames a
full meter holds before auto-discharging and the frames of the overcharge lockout after it:
360 and 60 on Slick Star, 1600 on Hydra.

## Air and glide

`handling.air_impulse` (`+0x0c8`) scales the impulse `zz_801ebe88_` returns from a steep
surface contact, and it is where the gliders live: 1800 on Flight Warp Star and 1500 on
Dragoon against 500 on Warp Star and 20 on Formula Star. `handling.air_recover_len`
(`+0x0cc`) is how many frames the post-airborne velocity blend runs over, and
`handling.air_accel` (`+0x0bc`) is the airborne acceleration budget.

On the attribute side, `glide_up_speed` / `glide_up_amount` / `glide_down_speed` /
`glide_down_amount` (`+0x174`..`+0x180`) are the glide itself, and the four `descent_*`
terms at `+0x190`..`+0x19c` are how fast a machine sinks - Flight Warp Star runs
`1.5 / 0.4 / 0.38 / 0.015` where Warp Star runs `1.8 / 0.45 / 0.45 / 0.02`.
`takeoff_speed` (`+0x13c`) is the pop off a lip, and Jet Star's `3.0375` is ten times the
field: it is why that machine leaves the ground off anything.

## Presentation

`handling +0x094`..`+0x0b0` is the model's lean, in degrees: `pitch_max_down` /
`pitch_max_up` clamp the pitch (36 / 36 on most stars, 5 / 1 on Formula Star, 35 / -20 on
Dragoon), `roll_max` sets the bank per unit of stick, and `lean_approach` /
`lean_step_max` / `lean_step_max_0` control how fast the model gets there. It is drawn only -
`zz_801ec118_` and its three siblings write a `JOBJ`'s rotation - but it is a large part of
what a machine reads as.

## Identity fields

Attributes that describe the machine as an object rather than as a vehicle, and that a
handling change has no business touching: `rider_sit_bone_idx` / `rider_extra_bone_idx`
(`+0x000`/`+0x004`, joint indices into that machine's own tree, so a wrong one puts the rider
on the wrong part or off the model), `model_scale` (`+0x008`), `start_cam_distance`
(`+0x010`), the shadow extents (`+0x018`/`+0x01c`), and the four hitbox terms at `+0x070`,
`+0x074`, `+0x11c` and `+0x120`.

## Reading a machine's numbers

The blocks are on the disc, not in `mem1.raw` - only the machines a scene loaded are in RAM.
`scripts/hsd/explore.py ls iso/files/VcStarSlick.dat` names the archive's `vcData` public,
and the two attribute blocks are the pointers at its `+0x00` and `+0x14`, read with
`scripts/hsd/archive.py`'s `Archive.deref`.
