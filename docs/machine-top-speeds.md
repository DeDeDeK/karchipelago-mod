# Machine Top Speeds

Every vehicle's cruise-speed caps are static data shipped in its `Vc*.dat` archive. They
are the numbers that decide whether a machine can satisfy speed-floor objectives such as
the Fantasy Meadows "1 lap without dropping below 20 mph!" checklist cell.

## Where the numbers live

Each `Vc*.dat` holds one public `vcData*` struct whose first field is a pointer to the
vehicle's attribute blob. `Machine_AdjustAttributes` (`0x801c7278`) memcpy's 124 words
(0x1f0 bytes) out of that blob into `MachineData+0x460`, so **attribute offset `k` lands
at `MachineData + 0x460 + k`**:

| MachineData | attr offset | Field |
|---|---|---|
| 0x4cc | 0x6c | `hp_max` |
| 0x4f0 | 0x90 | `top_speed_ground` |
| 0x5ac | 0x14c | `top_speed_air` |

After the copy, `Machine_AdjustAttributes` dispatches the per-class scaling callback
(`Machine_AdjustAttributesStar` `0x801e906c` / `Machine_AdjustAttributesBike`
`0x801f4dac`), which multiplies `top_speed_ground` by two `Machine_ScaleFromRatio`
factors — one keyed on the accel stat ratio, one on the top-speed stat ratio (the latter
using a per-VCKIND min/max pair). `Machine_ScaleFromRatio` returns exactly `1.0` at ratio
`0`, and `Machine_GetStatRatio` sums the patch/stat arrays at `MachineData+0x94C`,
`+0x970`, `+0x994`, `+0x9B8` plus `PlayerData.stat_aux`. **In Air Ride and Top Ride those
are all zero**, so the shipped attribute value *is* the machine's top speed there. Only
City Trial patches move it.

Finally `top_speed_current` (`+0x398`) is set from `top_speed_ground` while
`action_state_class` (`+0x754`) is 0 and from `top_speed_air` otherwise; that is the cap
the movement controllers clamp velocity against.

## Units and the mph scale

Caps are world units per frame, the same space as `MachineData.world_velocity` (`+0x354`,
the measured `pos - prev_pos` delta). The one place the game converts to a human unit is
the Fantasy Meadows speed watcher `AirRide_TrackMinLapSpeed` (`0x80231670`), which divides
`|world_velocity|` by `1.609344` (the mile/km constant, double at `0x805e2a58`) and
requires the result to stay at or above `0.8101851f` (`0x805e2a60`). Taking that pair at
its stated face value of 20 mph fixes the scale for the whole table:

```
20 mph  =  1.303867 units/frame
1 unit/frame  =  15.339 mph
```

## Table

Ground and air caps as shipped, sorted by ground speed. "20 mph" marks whether the
grounded cap clears the `1.303867` floor.

| VCKIND | Machine | `Vc*.dat` | ground | mph | air | mph | HP | 20 mph |
|---:|---|---|---:|---:|---:|---:|---:|:---:|
| 7 | Formula Star | VcStarFormula | 2.7945 | 42.86 | 2.2275 | 34.17 | 140 | yes |
| 8 | Dragoon | VcStarDragoon | 2.4300 | 37.27 | 3.6450 | 55.91 | 230 | yes |
| 22 | Rex Wheelie | VcWheelRex | 2.0452 | 31.37 | 2.4300 | 37.27 | 210 | yes |
| 11 | Swerve Star | VcStarRuins | 2.0331 | 31.19 | 1.9318 | 29.63 | 140 | yes |
| 20 | Wheel Kirby | VcWheelKirby | 1.9035 | 29.20 | 1.8225 | 27.96 | 200 | yes |
| 12 | Turbo Star | VcStarTurbo | 1.8630 | 28.58 | 1.6200 | 24.85 | 160 | yes |
| 17 | Wing Kirby | VcWingKirby | 1.8225 | 27.96 | 2.0250 | 31.06 | 200 | yes |
| 24 | Dedede Wheelie | VcWheelDedede | 1.7921 | 27.49 | 1.6200 | 24.85 | 200 | yes |
| 6 | Slick Star | VcStarSlick | 1.7617 | 27.02 | 1.9440 | 29.82 | 200 | yes |
| 21 | Wheelie Bike | VcWheelWheelie | 1.7496 | 26.84 | 1.8225 | 27.96 | 180 | yes |
| 19 | Wheel | VcWheelNormal | 1.7314 | 26.56 | 1.8630 | 28.58 | 200 | yes |
| 18 | Wing Meta Knight | VcWingMetaKnight | 1.7213 | 26.40 | 1.8225 | 27.96 | 170 | yes |
| 9 | Wagon Star | VcStarWagon | 1.7010 | 26.09 | 2.0250 | 31.06 | 250 | yes |
| 14 | Flight Warp Star | VcStarFlight | 1.6605 | 25.47 | 2.0250 | 31.06 | 200 | yes |
| 15 | Free Star | VcStarFree | 1.6605 | 25.47 | 1.9035 | 29.20 | 200 | yes |
| 16 | Steer Star | VcStarHandle | 1.6605 | 25.47 | 1.9035 | 29.20 | 200 | yes |
| 0 | Warp Star | VcStarNormal | 1.6443 | 25.22 | 1.8630 | 28.58 | 200 | yes |
| 5 | Bulk Star | VcStarHeavy | 1.6200 | 24.85 | 1.7010 | 26.09 | 280 | yes |
| 2 | Winged Star | VcStarWing | 1.5187 | 23.30 | 2.0452 | 31.37 | 150 | yes |
| 23 | Wheelie Scooter | VcWheelScooter | 1.3770 | 21.12 | 2.4705 | 37.89 | 140 | yes |
| 13 | Jet Star | VcStarJet | 1.3365 | 20.50 | 1.5795 | 24.23 | 200 | yes |
| 3 | Shadow Star | VcStarDevil | 1.2960 | 19.88 | 1.8225 | 27.96 | 140 | **no** |
| 4 | Hydra | VcStarHydra | 1.2150 | 18.64 | 2.2275 | 34.17 | 400 | **no** |
| 1 | Compact Star | VcStarLight | 1.1745 | 18.02 | 1.8468 | 28.33 | 100 | **no** |
| 10 | Rocket Star | VcStarRocket | 1.0125 | 15.53 | 1.2150 | 18.64 | 220 | **no** |
| 25 | VS Dedede Wheelie | VcWheelVsDedede | 0.9315 | 14.29 | 1.6200 | 24.85 | 270 | **no** |

The internal `Star_*` / `Wheel_*` names run in VCKIND order; the archive-to-VCKIND
mapping above comes from the file/symbol name pair table at `0x804b152c`, which the
vehicle loader walks in that same order.

Boost and charge release exceed the cap only transiently, and charging costs speed while
it is held, so neither raises a machine's sustainable floor. Gliding swaps in the air cap,
which is higher for every machine except Turbo Star, Dedede Wheelie and Rocket Star — but
it cannot be held for a full lap, and Rocket Star is under the 20 mph floor in both
states.

## Consequences for the 20 mph cell

The Fantasy Meadows cell needs `|world_velocity| >= 1.303867` on *every* frame of a lap,
so the cap is a **necessary** condition — but not a sufficient one, because how a machine
sheds speed matters as much as how fast it goes. Play-tested verdict:

| Machine | Cap | Verdict |
|---|---:|---|
| Rocket Star | 15.53 | fails, cap far under the floor |
| Compact Star | 18.02 | fails, cap under the floor |
| Shadow Star | 19.88 | fails, cap just under the floor |
| Swerve Star | 31.19 | **fails on handling** — it comes to a full stop to steer, and the course cannot be lapped without turning |
| Hydra | 18.64 | **passes** — resting cap is under the floor, but its charge carries it over |

Everything else on the table clears it. Jet Star (20.50) and Wheelie Scooter (21.12) have
the thinnest remaining margins, so a mistake costs the lap on those two even though they
are viable.
