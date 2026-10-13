# Machine Attributes

How a vehicle drives is static data shipped in its `Vc*.dat`: two attribute blocks that the
engine copies into the live `MachineData` and then scales by the player's patch stats. The physics
code is shared by every machine of a class, so what separates Slick Star from Swerve Star is these
numbers. A modder tuning a machine, authoring a new one, or reading why a stat patch feels the way
it does works here.

## The two blocks

A `Vc<Class><Stem>.dat` has one public, `vcData<Class><Stem>`, a `vcData` of seven pointers. Two of
them are attributes:

| `vcData` field | Struct | Size | Lands in |
|---|---|---|---|
| `+0x00` `attr` | `vcAttributes` | `0x1f0` | `MachineData + 0x460`, so field `k` is at `+0x460 + k` |
| `+0x14` `handling_attr` | `vcHandlingAttr` | `0xf8` star, `0x108` bike | `md->attr`, after the class block |

`vcAttributes` has the same layout in both classes. `hp_max` lands at `+0x4cc`, `top_speed_ground`
at `+0x4f0` and `top_speed_air` at `+0x5ac`.

`MachineData.attr` (`+0x650`) is a separate per-machine allocation from an object pool
(`Machine_AllocAttrStruct`, 0x801c71a8). It starts with a per-class block shared by every machine
of the class, copied from the class archive's `vcDataKindStar` (`VcStar.dat` or `VcWheel.dat`), and
the machine's own handling block follows it:

| Class | Copy | Class block | Handling block |
|---|---|---|---|
| Star | `Machine_Star_CopyAttributes` (0x801e812c) | `0xac` bytes; `+0x1c` is the speed cap `accelerateStar` (0x801ec074) clamps velocity to | `+0xac`, `vcHandlingAttr` (`MachineAttrWork`) |
| Bike | `Machine_Wheel_CopyAttributes` (0x801f3c94) | `0x1c` bytes | `+0x1c`, `0x108` bytes (`MachineAttrWorkWheel`) |

The `vcHandlingAttr` field names describe the star layout only; on a bike they name nothing.

## The rebuild

`Machine_AdjustAttributes` (0x801c7278) rebuilds a machine's attributes from scratch:

1. It copies all `0x1f0` bytes of `md->vcData->attr` to `md + 0x460`, then applies
   `hp_max_override` (`+0xa1c`) when it is nonzero.
2. It calls the class descriptor's `copy_attr` (`+0x1c`) and `adjust_attr` (`+0x20`) slots. The
   descriptors sit at `r13 - 0x6148`, indexed by `is_bike`. `copy_attr` refills `md->attr` (the
   table above). `adjust_attr` is `Machine_AdjustAttributesStar` (0x801e906c) or
   `Machine_AdjustAttributesBike` (0x801f4dac); both run the class's stat scaling, and the star
   one then rebuilds the thrust (`Machine_Star_UpdateThrust`, 0x801eb57c).
3. It sets `top_speed_current` (`+0x398`) from `top_speed_ground`, or from `top_speed_air` while
   `is_airborne` (`+0x754`) is set.
4. A rise in `hp_max` is added to current HP. Current HP is then clamped to the new maximum.

It runs at spawn and on every patch pickup. Each call starts again from the archive data, so
calling it mid-round re-applies the stats rather than stacking them.

Three fields sit outside the rebuild. `Machine_Star_Init` (0x801e7f3c) seeds them once from the
attributes, and `Machine_AdjustAttributes` does not rewrite them. A swap that only reruns the
rebuild leaves them on the old values:

| `MachineData` | Seeded from |
|---|---|
| `ground_grip` (`+0x6ec`) | `vcAttributes.ground_grip`, after stat scaling |
| `air_grip` (`+0x6f0`) | `vcAttributes.air_grip`, after stat scaling |
| `lift_max` (`+0x388`) and `lift_accum` (`+0x384`) | `handling.lift_ceiling` |

## Patch-stat scaling

`Machine_ApplyStarStatScaling` (0x801e81e4) and `Machine_ApplyBikeStatScaling` (0x801f3d44) are the
game's own record of which attribute each of the nine patch stats moves. Most sites are

```
field *= Machine_ScaleFromRatio(pair, Machine_GetStatRatio(md, stat))
```

The pair is a `(low, high)` entry in the class table `MachineData.stat_scale` (`+0x658`) points at,
which comes from the class archive.

- `Machine_GetStatRatio` (0x801caa8c) sums the stat across the master stats (`+0x94c`), the three
  stat-cap groups (`+0x970`, `+0x994`, `+0x9b8`) and `stat_aux` (`+0x9e8`). It divides the sum by
  `Patch_GetMaxValue` and clamps the result to `[-1, 1]`.
- `Machine_ScaleFromRatio` (0x801cab4c) returns exactly 1.0 at ratio 0. Above zero it returns
  `1 + ratio * (high - 1)`, and below zero `1 + |ratio| * (low - 1)`.

The shipped value is therefore the machine with no patches. In Air Ride and Top Ride the stat arrays
stay zero, so the shipped value is what the machine drives with. City Trial starts the eight non-HP
stats at -2, so a fresh City Trial machine sits on the low side of every pair.

Fields each stat scales (`vcAttributes` fields are named or given by offset; handling fields are
star-class):

| Stat | `vcAttributes` | `vcHandlingAttr` |
|---|---|---|
| Boost | `top_speed_ground`, `slope_speed_up`, `boost_gain_any`, `boost_gain_sliding`, `takeoff_speed`, `+0x1a4` | `accel_floor`, `accel_turn_keep`, `pitch_max_up`, `air_accel`, `x040[0..4]`, `x054[4]` |
| Top Speed | `top_speed_ground`, `top_speed_air` (per kind), `+0x1a0` | `accel_floor`, `air_accel`, `air_impulse`, `x040[0..4]`, `x054[4]` |
| Turn | `glide_up_speed`, `glide_down_speed`, `+0x18c`, `+0x190`, `+0x19c` | `turn_rate_rest`, `turn_rate_top`, `x054[3..4]`, `x068[0]`, `x068[2..4]` |
| Charge | `charge_full_duration`, `charge_cooldown_duration`, `charge_rate`, `charge_rate_turning` | `x040[0..4]` |
| Weight | `top_speed_ground`, `slope_speed_up`, `slope_speed_down`, `ground_grip`, `air_grip`, `base_defense`, `coll_radius`, `boost_gain_any`, `turn_speed_on_slope`, `glide_up_speed`, `glide_down_speed`, `+0x03c`, `+0x044`, `+0x078`, `+0x18c`..`+0x19c`, `+0x1a4`, `+0x1a8` | `accel_floor`, `turn_rate_rest`, `turn_rate_top`, `x014`, `x024[0..1]`, `x040[0..4]`, `x054[4]`, `x068[2]`, `air_accel`, `air_accel_fwd`, `air_accel_back`, `x0cc[0]`, `x0cc[3]`; adds to `air_impulse` and `air_recover_len` (per kind) |
| Glide | `coll_radius`, `base_defense`, `slope_speed_up`, `slope_speed_down`, `ground_grip`, `takeoff_speed`, `glide_up_speed`, `glide_down_speed`, `turn_speed_on_slope`, `+0x03c`, `+0x044`, `+0x078`, `+0x164`..`+0x170`, `+0x190`, `+0x1a4`, `+0x1a8`; adds to `air_grip` | `lift_ceiling`, `x024[0..1]`, `x040[0..4]`, `x068[3..4]`, `air_accel`, `air_accel_fwd`, `air_accel_back`, `x0cc[0]`; adds to `air_impulse` and `air_recover_len` (per kind) |
| Offense | `hitbox_size`, `weapon_charge_scale` | - |
| Defense | `coll_radius`, `base_defense`, `+0x03c`, `+0x044` | - |
| HP | `hp_max`, through `Machine_GetStatRatio2` (0x801cabd4) and a pair in `VcCommon.dat` | - |

`x040[0..4]` is scaled by five stats and has no reader in the star controller. Weight touches
nearly every axis, which is why a heavy machine feels different everywhere rather than just slower.
The additive sites go through `Machine_ScaleFromRatio2` (0x801cab94). It returns 0 at ratio 0 and
`|ratio|` times the pair end otherwise. Glide's additions are clamped at 0 afterwards.

**Per-kind blocks.** Most of the class table is one pair per site. A few blocks hold one row per
class slot and are indexed by `MachineData.kind` (`+0x24`) with no bound:

| Class | Block (from `stat_scale`) | Stat | Applies to |
|---|---|---|---|
| Star, 19 rows | `+0x068 + 8 * kind` | Top Speed | `top_speed_ground`, multiplied |
| | `+0x100 + 8 * kind` | Top Speed | `top_speed_air`, multiplied |
| | `+0x328 + 8 * kind` | Weight | `handling.air_impulse`, added |
| | `+0x3c0 + 8 * kind` | Weight | `handling.air_recover_len`, added |
| | `+0x538 + 8 * kind` | Glide | `handling.air_impulse`, added, then clamped at 0 |
| | `+0x5d0 + 8 * kind` | Glide | `handling.air_recover_len`, added, then clamped at 0 |
| Bike, 7 rows | `+0x060 + 8 * kind` | Top Speed | `top_speed_ground` |
| | `+0x098 + 8 * kind` | Top Speed | `top_speed_air` |
| | `+0x128` to `+0x2b0`, `0x38` apart, `+ 8 * kind` | Turn | `md->attr` `+0x74`, `+0x78`, `+0x84`, `+0x88`, `+0x94`, `+0x98`, `+0x9c`, `+0xa0` in order (the bike's handling block) |

Every bike row multiplies. Each block runs straight into the next, so a class slot past the last
row reads the next block's first rows. On a star, slot 19's Weight and Glide additions to
`air_impulse` would come from the `air_recover_len` rows, which are sized in hundreds of frames.

## How the main attributes act

The per-machine values below are star-class unless stated.

**Grip.** `vcAttributes.ground_grip` (`+0x0e0`) is how hard velocity is pulled onto the heading each
frame. `Machine_Star_Init` copies it to `MachineData.ground_grip`, and `Machine_Star_ApplyGrip`
(0x801ebc90) spends it. It is the single biggest difference in feel: 0.01 on Slick Star, 0.233 on
Warp Star, 0.8 on Compact Star. `air_grip` (`+0x150`) is the same off the ground.

**Yaw.** `Machine_RotateDuringCharge` (0x801ec5cc) is the grounded steer for the run, push-forward,
landing and charge states, not only the charge. While the machine is not charging it computes

```
turn = -stick * lerp(handling.turn_rate_rest, handling.turn_rate_top, speed term)
```

where the speed term grows with `|velocity|` against the cap. The turn is multiplied by
`handling.slip_penalty` (0.55 on most stars) once the machine is already slipping past
`handling.slip_penalty_deg`. `turn_rate_rest` is the yaw at a standstill and `turn_rate_top` the yaw
at the cap: Formula Star's 0.025 -> 0.002 and Swerve Star's 0.037 -> 0 all but stop turning at
speed, and Turbo Star's 0.1 is the hardest pivot in place. While the machine is charging, the same
function steers from a second set of terms, `handling.x068[1..5]`, instead.

**Acceleration.** `Machine_Star_UpdateThrust` rebuilds `MachineData.thrust` (`+0x6e8`) from the
active top speed and `handling.accel_floor` on the ground, or `handling.air_accel` in the air, each
capped at that top speed. `Machine_Star_ApplyGroundThrust` (0x801ecae4) spends it into
`MachineData.accel`. As slip grows it scales the thrust toward `handling.accel_turn_keep`, and it
biases the thrust by `slope_speed_up` / `slope_speed_down`. `Machine_Star_ApplyAirThrust`
(0x801ed4d8) is the airborne twin. It applies `handling.air_accel_fwd` while the stick agrees with
the heading and `air_accel_back` while it opposes it.

**Charge and boost.** `charge_rate` / `charge_rate_turning` (`+0x09c` / `+0x0a0`) fill the charge
meter, and `boost_gain[11]` (`+0x0a8`) is what a release is worth at each tenth of the meter.
`charge_deplete_rate` (`+0x0a4`) is how fast the boost bleeds off. `charge_full_duration` /
`charge_cooldown_duration` (`+0x050` / `+0x054`) time the auto-discharge and the overcharge lockout.

**Air and glide.** `Machine_Star_ApplyAirThrust` blends the airborne thrust from the heading toward
its reflection off the surface normal at `MachineData + 0x774`. The blend happens over
`handling.air_recover_len` (`+0x0c8`) frames, which is where the gliders live: 1800 on Flight Warp
Star and 1500 on Dragoon, against 500 on Warp Star and 20 on Formula Star. `handling.air_impulse`
(`+0x0c4`) scales the kick `Machine_Star_ApplyAirImpulse` (0x801ebe88) adds to `MachineData.accel`
in flight. It ships between 0.002 and 0.04 on most stars, 0.19 on Bulk Star and 0 on Formula and
Rocket Star. On the attribute side, `glide_up_speed` / `glide_up_amount` / `glide_down_speed` /
`glide_down_amount` (`+0x174`..`+0x180`) are the glide itself. The four descent terms at
`+0x190`..`+0x19c` set how fast a machine sinks: Flight Warp Star runs 1.5 / 0.4 / 0.38 / 0.015,
where Warp Star runs 1.8 / 0.5 / 0.45 / 0.02. `takeoff_speed` (`+0x13c`) is the pop off a lip. Jet
Star's 3.0375 is about ten times the usual, and it is why that machine leaves the ground off
anything.

**Presentation.** Handling `+0x090`..`+0x0ac` is the model's lean, in degrees.
`pitch_max_down` / `pitch_max_up` clamp the pitch (36 / 36 on most stars, 5 / 5 on Formula Star,
35 / 35 on Dragoon), `roll_max` sets the bank per unit of stick, and `lean_approach` /
`lean_step_max` / `lean_step_max_0` set how fast the model gets there. The lean only changes the
drawn model, but it is a large part of how a machine reads.

**Identity fields.** Some attributes describe the machine as an object rather than as a vehicle, and
a handling change should leave them alone:

- `rider_sit_bone_idx` / `rider_extra_bone_idx` (`+0x000` / `+0x004`) are joint indices into the
  machine's own tree. A wrong one puts the rider on the wrong part or off the model.
- `model_scale` (`+0x008`), `start_cam_distance` (`+0x010`) and the shadow extents (`+0x018`,
  `+0x01c`).
- The hitbox terms `hitbox_size`, `hitbox_dist_x`, `landing_hitbox_size` and
  `landing_hitbox_dist_x` (`+0x070`, `+0x074`, `+0x11c`, `+0x120`).

## Speed units

Speeds are world units per frame, the same space as `MachineData.world_velocity` (`+0x354`), the
measured `pos - prev_pos` of the last frame. The game converts to a human unit in one place: the
Fantasy Meadows "1 lap without dropping below 20 mph" checklist watcher, `AirRide_TrackMinLapSpeed`
(0x80231670). It divides `|world_velocity|` by 1.609344 (the double at 0x805e2a58) and fails the
lap below 0.8101851 (0x805e2a60). Taking that test as 20 mph gives the scale:

```
20 mph        = 1.303867 units/frame
1 unit/frame  = 15.339 mph
```

Shipped ground caps run from 0.9315 (VS Dedede Wheelie) to 2.7945 (Formula Star). Air caps run up
to 3.645 (Dragoon). Boost and charge release exceed the cap only briefly, and holding a charge costs
speed, so neither raises the speed a machine can hold.

## Reading a machine's numbers

The blocks are on the disc, not in a memory snapshot: only the machines a scene loaded are in RAM.
The vehicle loader maps a `MachineKind` to its archive through `stc_vcNameTable[is_bike]`
(`r13 - 0x6150`), a list of `{filename, symbol}` pairs. The star list at 0x804b1528 is class slots
0-18 in `MachineKind` order. The bike list at 0x804b1c08 is slots 0-6, whose `MachineKind` is
`19 + slot`. In an archive, the attribute blocks are the pointers at `+0x00` and `+0x14` of its one
public.
