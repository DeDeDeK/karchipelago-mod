# Archipelago Star Handling Profiles

The Archipelago Star drives differently depending on how many of its six pods it still
carries. Firing one - the star's full-charge release launches a pod as a projectile - drops
it to the next profile down a fixed ladder, and growing the ring back puts it on the first
again. The pods are on the machine, so how many are left is a readout of how it handles.

The whole feature is `mods/ap_star/src/ap_star_handling.c`, behind
`ap_star_settings.handling_enabled`. The pod count comes from the ring state
`ap_star_shot.c` already keeps per machine.

The flag has no option on the Archipelago Star settings page, so it holds its default of 0
and no build reaches the ladder: every star drives on profile 0, the machine as shipped.
Binding an `OptionDesc` to the flag is all that offering it takes.

## The ladder

| Pods | Profile | What it is |
|---:|---|---|
| 6 | Slick | the machine as shipped, which is Slick Star's numbers |
| 5 | Glide | Flight Warp Star whole, the game's glider |
| 4 | Speed | Formula Star's cruise on Hydra's weight, with no charge requirement |
| 3 | Warp | Warp Star whole, the game's baseline machine |
| 2 | Boost | nothing until the meter is nearly full, then Rocket Star's launch |
| 1 | Jet | Jet Star, which leaves the ground off anything |

An empty ring is the 60-frame regrow window, and it holds whatever profile the machine was
already on rather than passing through a seventh. The refill takes the count back to six, so
a machine that empties its ring drives as Jet until the pods return and then goes back to
Slick.

The ladder is fixed, so a player can read a rival's handling off their machine at a glance
and can spend shots to reach a profile they want.

## How a profile is applied

A profile is a copy of the star's own two attribute blocks, either with a handful of named
fields overwritten or with a stock machine's blocks laid over them whole.
`Machine_AdjustAttributes` (`0x801c7278`) rebuilds a machine entirely out of `md->vcData` -
memcpy of `vcAttributes` into `MachineData+0x460`, refill of the live `md->attr` block from
`vcData->handling_attr`, then the patch-stat scaling - so a profile is applied by pointing
the machine at a `vcData` of the mod's own and asking for that rebuild.

Six `{vcAttributes, vcHandlingAttr, vcData}` triples are built at once, the first time a
machine needs one, by copying the star's live blocks and running that profile's writer over
them. They are seeded from `md->vcData` rather than from the archive, so they pick up
whatever the scene loaded, and they are dropped on every 3D load. A machine that never
leaves profile 0 is never pointed at one at all.

Two things fall out of routing through `Machine_AdjustAttributes` rather than writing
`MachineData` directly:

- **Patches keep working.** The rebuild re-applies the nine patch stats from scratch over
  the new base, and a later patch pickup - which calls `Machine_AdjustAttributes` itself -
  reads the profile's `vcData`, so it can neither wipe the profile nor stack with it.
- **HP is carried.** The rebuild adds the change in `hp_max` to current HP and clamps down
  if the new maximum is lower. No profile changes `base_hp`, `base_offense` or
  `base_defense`, so City Trial combat balance is untouched either way.

Three fields the rebuild does not reach are carried over by hand afterwards, because
`Machine_Star_Init` (`0x801e7f3c`) seeds them once and nothing rewrites them:
`MachineData.ground_grip` (`+0x6ec`) from `vcAttributes.ground_grip`, `air_grip` (`+0x6f0`)
from `vcAttributes.air_grip`, and `lift_max` (`+0x388`) from `handling.lift_ceiling`, with
`lift_accum` clamped down to it. Leaving those alone would keep the previous profile's grip.

## Becoming a stock machine

Glide and Warp are not recipes but copies: Warp Star's and Flight Warp Star's shipped
`0x1f0` + `0xf8` blocks are embedded in `ap_star_handling.c` word for word out of
`VcStarNormal.dat` and `VcStarFlight.dat`, and `CopyStock` lays them over the seed whole.
They are embedded rather than read from the donor's `vcData` at runtime because only the
machines a scene loaded are in RAM, and the star has to be able to become one in Air Ride
too, where nothing else is loaded.

A copy is the only way to carry the terms a machine's feel lives in that have no name yet -
the `x028` / `x044` / `x06c` handling rows that the patch stats scale, the fall tiers, and
the unnamed attribute blocks at `+0x0e8` and `+0x1a0`. A named-field recipe reaches the
numbers a reader can point at and leaves the rest of the donor behind, which shows up first
in the air, where most of what a glider does is in those rows.

What the copy does *not* take is the star's identity. `CopyStock` puts back, from the seed:
the two rider bone indices, `model_scale`, `start_cam_distance` and the `x014` term that
moves with it, the three shadow extents, the four hitbox terms, and `base_hp` /
`base_offense` / `base_defense`. So the machine keeps its model, its rider, its collision
size and its combat balance, and changes only how it drives.

## What each profile moves

Every profile is seeded from the star's own blocks, which are Slick Star's - `VcStarAp.dat`
was built from `VcStarSlick.dat` - so the numbers below are what a profile moves against
those.

**Glide** is Flight Warp Star: a 2.025 air cap over a 1.6605 ground one, `ground_grip` 0.233
and `air_grip` 0.6 against Slick's 0.01 and 0.3, a yaw of `0.029 -> 0.0225`,
`handling.accel_floor` 1.2 where Slick has none, `air_accel` 1.0, `air_impulse` 1800 against
Slick's 600, and `air_recover_len` 1.5 against 4.5. `takeoff_speed` 0.405 pops it off every
lip, the four `descent_*` terms are the glider's `1.5 / 0.4 / 0.38 / 0.015`, and the boost
curve tops out at 0.25 over a 640-frame full-charge hold. It is the profile that can steer
once it is airborne, and it steers as that machine does.

**Speed** runs a 2.70 ground cap, just short of Formula Star's, on Hydra's weight:
`ground_grip` 0.10, `air_grip` 0.80, `handling.accel_floor` 3.0 so it pulls to the cap hard,
`accel_turn_keep` 0.07, and a yaw of `0.026 -> 0.016` - it turns, but it turns wide, which is
the price of the speed. `charge_deplete_rate` drops to 0.02, so a boost carries a long way.
The charge requirement is the part that is deliberately *not* Hydra: its `boost_gain` curve is
a flat 0.05 to 0.25 ramp that pays out at every charge level, where Hydra's first eight entries
are zero.

**Warp** is Warp Star, the machine every player already knows: `ground_grip` 0.233 against
Slick's 0.01, `air_grip` 0.4, a yaw of `0.029 -> 0.0225`, `handling.accel_floor` 1.2, a
1.6443 ground cap and a 1.863 air one, `air_impulse` 500, and a boost curve topping out at
0.2 over a 180-frame full-charge hold - half Slick Star's. It is the even profile in a
ladder of specialists, and the one a player drops to when none of the others is what the
round wants.

**Boost** is Rocket Star's shape without its cliff being quite so absolute: the curve is
0.005 at rest, 0.14 at four fifths and 0.32 at nine tenths, then 1.20 at full, so only a
completed charge is worth much. The ground cap comes down to 1.60 so the boost is where the
speed lives, `charge_rate` and `charge_rate_turning` go up to 0.035 / 0.05 so the meter fills
fast, `charge_deplete_rate` is 0.35 so the launch carries, and `charge_full_duration` is 900
frames - fifteen seconds of holding a full meter, against Slick Star's six - so it can be
spent on purpose rather than dumped by the auto-discharge.

**Jet** is Jet Star's numbers: a 1.3365 ground cap, a 1.5795 air cap, and `takeoff_speed`
3.0375, ten times the field, so it launches off anything. `handling.accel_floor` 1.8,
`ground_grip` 0.233, `air_grip` 0.25, a yaw of `0.035 -> 0.040` and the machine's own steep
fall tiers at `+0x15c`, `+0x168` and `+0x16c`. It is the slowest profile on the ground and
spends most of its time off it.

## Turning it off

With the flag off the pod count is read and discarded and every machine is held on profile
0, which is the star's own attributes - so clearing it mid-round takes a machine back to the
machine as shipped on its next tick rather than freezing it on whatever it was driving, and
setting it puts it straight onto the profile its pods call for. A machine that never fired
is never pointed at a profile `vcData` at all.

The **Star Shot** toggle gates the firing itself. With shots off the ring never loses a pod,
so the star stays on the first profile whatever the flag says.
