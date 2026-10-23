# Custom AI: CPU Fixes and Stadium CPUs

Corrections custom_ai makes to the vanilla CPU rider brain. CPU Fixes (on by default) covers
every CPU; Stadium CPUs (on by default) adds fixes for the stadium events. Each fix follows its
setting: instruction patches are written from a table, and the vanilla words restored, when the
setting changes, and the C hooks check the setting on each call.

## Flight and steering

- **Flight pitch.** `Rider_CPUEmitFlySteer` (0x8026f0e4) compares its pitch error against a dead
  zone of +/-57.2958 (`stc_cpu_fly_pitch_deadzone`), which a radian error never leaves, and
  builds the error from the target pitch in radians minus `forward.y`, a sine. A conditional hook
  on the pitch output (0x8026fb38: f29 target, f30 `forward.y`, r28 the RiderData, the steer
  envelope's cap at sp+8) replaces it: the error is the target minus `asin(forward.y)`, the stick
  is proportional with full deflection at 0.35 rad, and stick down pitches up. The per-machine
  pitch tables of Air Glider and High Jump reach the stick through it, and with Stadium CPUs on
  it also runs for the flight stadiums' profiles while CPU Fixes is off.
- **Avoidance.** `Rider_CPUResolveAvoidVector` (0x8026d1fc) takes the first listed hazard, even
  one about to hit, and bends around a hazard only while it is 10 or more frames away. The fixes
  sort the hazard list (soonest imminent first, then nearest), take the first eligible entry
  (0x8026d314 becomes `b 0x8026d334`) and avoid imminent hazards too (0x8026d34c becomes a nop).
  Its bend is `atan2(|dir|, radius)` on a normalized `dir`, so larger hazards get smaller bends;
  a hook on the `atan2` operand (0x8026d3b0, r29 the CpuHazard) makes the bend the hazard's
  angular size from the rider, `atan(radius / distance)`, capped at a tangent of 2.
- **Route bypass.** `Rider_CPUGetRouteDir` ignores the planned route whenever
  `navigate_frames / 1200` is odd, so every other 20 seconds a city CPU drives straight at its
  target. Navigate's hook pins `navigate_frames` to 0.

## Perception

- **Hazard order.** `Rider_CPUCollectHazards` runs its passes in a chain that stops once its
  8-entry list is full - riders, machines, static spheres, enemies, then weapons behind a
  `CPUBEHAVE_EXTRA_HAZARDS` gate - so riders and their machines alone can fill it. The fixes
  run weapons (still only with the gate bit) and enemies first, then static spheres, riders and
  machines, by replacing four of the pass calls (the static pass stays third) and dropping the
  gate's branch (0x80269bc8).
- **Stopped CPUs.** The collector builds its scan basis from the CPU's velocity and skips every
  pass when that has no direction, so a CPU charging in place saw no attack coming. A hook once
  the velocity is on the stack (0x80269998, r29 the RiderData, sp+0x38 the velocity) gives a
  CPU slower than 0.01 its facing at that speed.
- **Rams.** The Attack and Patrol profiles ram their rival, which the rider pass lists as a
  hazard to steer around. Their `Rider_CPUCollectHazards` calls are wrapped to drop the rival's
  rider and machine entries.
- **Yakumono hazards.** Vanilla never lists a yakumono's attack regions. Its static-sphere source
  (`Rider_CPUGetStaticHazardSpheres`, a stub that returns nothing in retail) is replaced at its
  call (0x8026964c), and with the fixes on it returns every active yakumono attack region that
  can hit a rider or machine and does damage, rebuilt once per tick.

## Targets and items

- **Rival selection.** `Rider_CPURivalSelect` (0x80264210) starts its pick at player 1 and keeps
  it when nobody outscores it, even with a score of 0, so a CPU with nobody in reach targets
  player 1 wherever (or whether) it is. It also divides HP by max HP as integers, so any rider
  below full HP counts as weak. The function is replaced by a copy that, with the fixes on,
  picks only a positive score and divides as floats. In the Vs. King Dedede fight it scores
  only Dedede, as vanilla does, so Dedede himself has no rival.
- **Item direction.** `Rider_CPUScanItems` gives its +5 to items outside the 45-degree cone
  ahead, so CPUs turn around for side and rear items (0x80263de8 becomes `blt`).
- **Machine distance.** `Rider_CPUScanCityObjects` takes 10 off machines 100-500 units away rather
  than past 500, so an on-foot CPU prefers a far machine (0x80263a44 becomes `blt`).
- **Unreachable items.** City CPUs skip items the reachability check rejects.
- **Healing.** The cascade's ApproachWaypoint row sends a city CPU to a Recovery Zone below 90%
  HP, ahead of items, every frame; the fixes write 0.6 to `stc_cpu_heal_hp_ratio`.

## Charging and abilities

- **Charge holding.** `Rider_CPUManeuverChargeHold` holds the gauge to the machine's release
  level only in races (`ai_state` 2); every other profile lets go on the first aligned frame, so
  Rocket Star, which only moves on a released charge, crawls in Drag Race, the city and the
  stadiums.
  A conditional hook on the profile test (0x8026ede0, r30 the RiderData) takes the
  hold-to-release path (0x8026edec) for Drag Race, the profiles above Patrol that share the race
  handler, and any machine whose release level is 0.95 or more.
- **Stalls.** The perceive stage raises `CPUSTATUS_VEL_STUCK` for a single frame when a CPU has
  been too slow or moving against its facing for 60 frames, then restarts its count, and
  `Rider_CPUEmitChargeStutter`'s press, hold-20, hold-40 pump only sees the bit when the command
  player happens to be idle that frame. `Rider_UpdateCPU`'s `Rider_CPUDecideState` call
  (0x8026bf08) is wrapped to hold the bit until the CPU moves faster than 0.3 or the pump has run
  its three steps (the RecoverForward and SteerToNav calls are wrapped to tell), resetting the
  pump once unstuck.
- **Counters.** The cascade skips its inhale and dodge for a CPU holding an ability that counters
  hazards. Its list names Sword, Needle, Tire and Ice, but Needle and Tire have no counter while
  Plasma and Tornado do; the compares at 0x802751d8 and 0x802751e0 become Plasma and Tornado.
- **Fire.** `Rider_CPUEmitAbilityAction`'s race target search compares against FLT_MAX backwards
  and never picks anyone (0x80274104 swaps the operands).
- **Melee riders.** Sword, Dedede and Meta Knight swing on their own at anyone within a few
  units; in the city they chase the nearest rider within 120.
- **Bomb and the copy wheel.** `Rider_ProcessCPUManeuver`'s `Rider_CPUEmitAbilityAction` call
  (0x8026c07c) is replaced: Bomb is thrown only at a rider within 120 units in a narrow cone
  ahead, held half a frame per unit (up to 60), instead of blind, and the copy wheel is stopped
  only while it is not on Sleep.

## Stadium CPUs

Stadium events keep their vanilla profiles; the Stadium CPUs setting adds fixes to them.

- **Kirby Melee 2.** GrColosseum5's CPU roads form two networks with no link between them - the
  pit and lower deck, and the upper deck and outer rim - and the central disc has none.
  `Rider_CPUScanRouteGoal` picks an enemy without regard to that, and junctions are ranked toward
  `nav_target_pos`, so a CPU on the rim whose enemy is in the pit laps the rim forever. Two hooks
  in `Rider_CPUDecideRouteFollowCity`: at 0x80272b00 (r30 the RiderData), just before it builds
  the route, `nav_target_pos` takes the jump-pad override that vanilla writes only into
  `steer_target_pos`; after the route-valid bit is stored (0x80272bb0), a CPU above y=15 whose
  goal is more than 15 below has `CPUSTATUS_ROUTE_VALID` cleared, so `Rider_CPUGetRouteDir`
  steers straight at it and drives off the ledge.
- **Kirby Melee 1 and 2.** The brain only Wiggles at hazards; a CPU now quick-spins at an enemy
  within 12 units (0.05 -> 0.3 a frame by level).
- **Destruction Derby and the Dedede fight.** `Rider_CPUDecideAttack` pushes its spin burst into
  the command buffer during decide, where the next script overwrites it. The arbitration exit
  plays it as a Wiggle at the rival within 25 units ahead, on the intended roll
  (`0.8 * scale + 0.1`). In the Dedede fight (`city_kind` 0x12) the CPUs' item detour is
  cleared, so they stay on Dedede.
- **Target Flight.** `Rider_CPUEmitFlySteer`'s Target Flight branch projects both directions onto
  the plane across the flight axis, where their angle is about 0 or pi, so the CPU can neither
  dive nor climb at a target. A conditional hook at its head (0x8026f8ec) pitches toward the
  target direction's elevation instead and returns to the shared pitch output.
  The pitch reaches the stick through the proportional pitch hook, which Target Flight's Charge
  profile uses with Stadium CPUs on even when CPU Fixes is off.
  `Rider_CPUDecideCharge`'s two `Rider_CPUSelectChargeAnchor` calls are replaced: in flight, the
  CPU takes the best-scoring target below it within its predicted glide's reach (1.2x the
  distance to where the glide comes down) and a 37-degree cone ahead, and falls back to vanilla's
  random pick among the best few when none qualifies.
- **Air Glider and High Jump.** `Rider_CPUDecideReposition` never charges on the ramp and its
  flight pitch never reaches the stick. On the ground, a CPU that can charge-hold holds the press
  from 70 frames before the lip (High Jump X = -1850 heading +X, Air Glider Z = 5150 heading -Z)
  and lets go 12 frames before it, so the boost is spent on the ramp. The press is written to the
  pad after the brain's update each frame, since ChargeHold outside a race lets go as soon as the
  CPU is aligned. In the air the pitch hook flies
  expert targets: Air Glider climbs at 0.2 rad while faster than 85% of `top_speed_air` and
  dives at -0.35 once under 60% to win speed back; High Jump climbs at 0.6 while rising and
  flattens to 0.15 once falling.
