# CPU Rider AI

How the computer-controlled riders of City Trial, the stadiums and Air Ride decide what to do and
drive. A CPU rider runs the same `RiderData` -> `MachineData` pipeline as a human; the only
difference is where its controller input comes from. Each frame its brain picks a maneuver,
writes a short command script for it, and plays the script into a **virtual pad** that the rider
reads in place of a real controller. Everything downstream - physics, charge, collision - is
shared with humans, which makes the pad, the maneuver choice and the navigation target the places
a mod can retune CPU behavior. Top Ride's CPU is separate C++ code on `TopRideKirby` and shares
nothing with this brain.

## The virtual pad

`RiderData.cpu` (+0x778) points at the rider's `CpuData` (0x19c bytes), allocated by
`Rider_CPUInit` (0x80262d6c) for CPU riders and NULL for humans. Its first fields are the pad:
`buttons` (+0x00), `stick_x` (+0x04) and `stick_y` (+0x06). `RiderGObj_InputThink` (0x8018ee28)
copies them into the rider's input fields (`held` +0x3d8, `stickX` +0x3ec, `stickY` +0x3ed)
through `Rider_GetCPUButtons` / `Rider_GetCPUStickX` / `Rider_GetCPUStickY`, plain field reads
(the sticks truncated to s8). Writing the pad fields is exactly pressing the pad. The brain only
ever toggles one button, 0x100 (press/charge).

## Per-frame pipeline

```
RiderGObj_CPUThink (0x8018fc58)          proc; only when Ply_GetPKind(ply) == PKIND_CPU
  _Rider_UpdateCPU (0x80275c70)          one-instruction wrapper
    Rider_UpdateCPU (0x8026beec)
      Rider_ProcessCPUDistance (0x8026bbe0)   1. perceive
      Rider_CPUDecideState     (0x802716e8)   2. decide
      Rider_ProcessCPUManeuver (0x8026bf30)   3. emit a command stream
      Rider_CPUProcessCmd      (0x80275cbc)   4. play it into the pad
```

1. **Perceive** refreshes the rider's view of itself (machine kind, position, velocity) and two
   anti-stuck detectors: position (no progress for 241 frames sets `status_flags` 0x04) and
   velocity (too slow or against the facing for 60 frames sets 0x02 for that one frame and
   restarts the count, so `Rider_CPUEmitChargeStutter`'s pump only sees it when the command
   player happens to be idle). It clears the per-frame
   scratch the later stages fill - the route (`stc_cpu_route`, 0x8055e964), hazards
   (`stc_cpu_hazards`) and forward zones (`stc_cpu_forward`) - and `steer_flags`, and copies the
   machine's `CpuMachineSteer` cosines and corner angle to 0x8055e68c-0x8055e694 for the route
   code.
2. **Decide** dispatches on `ai_state` to a profile handler, which rewrites `behavior_flags`,
   seeds `desire_flags`, picks the navigation target and tail-calls
   `Rider_CPUArbitrateManeuver` (0x80274ec0), the only writer of `maneuver`.
3. **Emit** runs only when the command player is idle: it runs the maneuver's handler (steering
   and button opcodes), then `Rider_CPUEmitAbilityAction` (0x80273d1c, copy-ability presses) and
   `Rider_CPUTerminateCmdStream` (0x80276228). A maneuver's script plays out before the next
   maneuver is emitted.
4. **Play** is a bytecode interpreter and the only writer of the pad fields.

## Profile and maneuver

`ai_state` (`CpuData+0x08`) is the **profile**, chosen once by `Rider_CPUInit` and never written
again. `Rider_CPUSelectProfile` (0x802766fc) picks it by stage first - the City Trial city 3,
Kirby Melee 1 and 2 give 5 and 6, Target Flight 7 - then gives a rider in player slot 4 (King
Dedede, in his fight) 10, and otherwise looks `Gm_GetCityKind()` up in the `{city kind,
profile}` table at 0x804b7f78, defaulting to 2:

| Round | City kind | Profile |
|---|---|---|
| Air Ride race, Single Race | 0-3, 15 | 2 |
| Drag Race | 7 | 4 |
| Air Glider, High Jump | 8, 11 | 9 |
| Destruction Derby, Vs. King Dedede | 14, 18 | 8 |

`Rider_CPUInitPlayerFixed` (0x80275c40) forces profile 1 at level 8; `Game_Think` uses it to put a
human's rider on autopilot in Time Attack.

| State | Handler | Profile | Behavior flags |
|---:|---|---|---|
| 1 | `Rider_CPUDecideCruise` (0x80271790) | Follow the racing line, combat muted | 0xc1 |
| 2, 4, >10 | `Rider_CPUDecideCruisePlus` (0x80271b24) | Racing line, plus the weapon hazard pass and without Cruise's 0xf00 inhibits | 0xd7 |
| 3 | `Rider_CPUDecideNavigate` (0x80271eb4) | City path-finding, items, machines, city objects | 0xff |
| 5 | `Rider_CPUDecideRouteFollow` (0x802726fc) | Steer at the best inhalable enemy | 0x17 |
| 6 | `Rider_CPUDecideRouteFollowCity` (0x80272888) | As 5, plus city objects and stuck recovery | 0xf7 |
| 7 | `Rider_CPUDecideCharge` (0x80272dd0) | Drive to a charge anchor (a Target Flight target) and charge | 0xd5 |
| 8 | `Rider_CPUDecideAttack` (0x802735dc) | Chase the nearest rival in a cone, spin-attack | 0x1f |
| 9 | `Rider_CPUDecideReposition` (0x80273228) | Stage-specific position nudges | 0xd5 |
| 10 | `Rider_CPUDecidePatrol` (0x80273b48) | Timed toggle between nav target and rival | 0x17 |

A CPU's personality is fixed by the round; the dynamism lives in `maneuver` (`CpuData+0x10`),
re-chosen every time the previous script ends. `base_maneuver` (+0x14) is what the cascade falls
back to and a maneuver returns to when its condition lapses: 1 (RecoverForward) from
`Rider_CPUInit`, 2 (SteerToNav) under RouteFollow, Attack and Patrol.

Only Attack and Patrol pick a rival (`Rider_CPURivalSelect`, 0x80264210) and only Navigate and
Attack scan for items; Cruise and CruisePlus do neither. The rival score favours near riders,
riders without a Tire, Sword, Needle or Ice ability (most of all with none), and humans. With
damage on, a rider scores 1 more - 5 while `Gm_GetDededeHPMargin` (0x8000ca18) is under -0.15,
except under Patrol - when its integer HP divided by its integer max HP is under 0.5, an integer
division, so any rider below full HP. The pick starts at player 1 and keeps it when
nobody outscores it, so a CPU with no one in reach still targets player 1. In the Vs. King
Dedede fight (city kind 18) only Dedede scores, 1, so the challengers' rival is Dedede and
Dedede's own is none.

Difficulty is `difficulty_level` (`CpuData+0x22`, 0-8, the CSS CPU level), which
`Rider_CPUDifficultyScale` (0x80276f00) turns into `scale = level / 8`. Charge commits ChargeStill
near its anchor on `rand < scale`; Attack picks a waypoint every 300 frames on `rand < 0.75*scale
+ 0.05` and rolls its spin burst on `rand < 0.8*scale + 0.1`, but pushes the burst into the
command buffer during decide, where the next emitted script overwrites it, so it never plays.
The other rolls live in the cascade. `Rider_CPUInvDifficultyScale` (0x80276f7c) is `1 - scale`
from its own read of the level and sets only `Rider_CPUEmitSteerStick`'s alignment dead zone;
`Rider_CPUWalkRoute`'s junction pick, the cascade's LaunchCharge gate and
`Rider_CPUGetAbilityPressHold` read `difficulty_level` directly.

## Behavior and desire flags

`behavior_flags` (+0x2c) are **enable** bits, rewritten by every decide pass (values above):

| Bit | Enables |
|---|---|
| 0x01 | ChargeHold (rows 9 and 15) and the forward-zone maneuvers ZonePress and ZonePass |
| 0x02 | The weapon pass of `Rider_CPUCollectHazards` (0x80269928) |
| 0x08 | The target-steer maneuvers 0xb and 0xc |
| 0x20 | Ranges may be driven end to start: `walk_dir` follows the planned route, and junction exits driven end to start stay in the ranking |
| 0x80 | The forward zone scan `Rider_CPUForwardLookahead` (0x80269f10) |

The deciders also write 0x04, 0x10 and 0x40, which nothing reads; the anti-stuck detectors run
regardless.

`status_flags` (+0x2d) carry state between passes: 0x01 the route was built this frame, 0x02 and
0x04 the stuck detectors, 0x08 Patrol's phase (0 toward its course points, 1 after the rival,
flipped every 900 frames), 0x20 lets enemy hazards bend the steering (Cruise, CruisePlus,
Navigate), and 0x40 arms machine switching. `steer_flags` (+0x2e) bit 0x80 is set by
`Rider_CPUEmitSteerStick` when the yaw error is under 5 degrees and cleared each perceive pass;
ChargeHold, ReverseRecover, SteerTargetAdvance and the ability presses wait on it.

`desire_flags` (+0x1c) are **inhibit** bits, cleared each pass and re-seeded from
`RiderData.status` by `Rider_CPUSeedDesire` (0x802762dc) through `{status, flags}` tables at
0x804b7b18 (common states) and 0x804b7c00 (Kirby states 29-130); Cruise also ORs in 0xf00. 0x100
mutes ZonePress's press, 0x200 the inhale scan behind TapOnce, 0x400 the hazard Wiggle and
Attack's spin burst, and 0x1000000 machine switching. 0x1000000 is seeded in Kirby statuses
0x35-0x6e, the ability, power-up, attack and landing states.

## The arbitration cascade

`Rider_CPUArbitrateManeuver` walks a fixed priority list and commits the first maneuver whose guard
passes; a "keep" row returns early while that maneuver is already running.

| Pri | Commits | Guard |
|---:|---|---|
| 0 | 0xf NavSteerTap | current is 0xe and position-stuck or a hazard is listed |
| 1 | 0 Coast | the machine is on a jump path (`is_superjump_path`) |
| - | keep | 0xe, 0xf, 0x10 |
| 2 | 0xe NavSteer | no machine - the rider is on foot |
| 3 | 8 Fly | the machine flies (`is_flying`, or `MachineData+0xc32` bit 0x01); kept while 8 |
| 4 | 0x12 TapOnce | an inhalable target wins a level-scaled roll; skipped for an enemy hazard `Rider_CPUIsHazardCounterable` (0x8027655c) passes while holding Sword, Needle, Tire or Ice |
| 5 | 0x13 Wiggle | an imminent hazard and `rand < 0.15*scale` (Attack `0.3*scale + 0.5`); inhibited by 0x400 |
| 6 | 9 RailRide | the machine is on a rail (`is_on_rail`) |
| 7 | 0xd Dismount | machine switching, below; kept while 0xd |
| 8 | 0x14 LaunchCharge | before GO, profiles 2/4/7/9, `CPUMACHCAP_BRAKE`, level > 3, `rand < scale`; kept while 0x14 |
| 9 | 7 ChargeHold | enable 0x01, a charge-hold machine under its speed and gauge gates; kept while 7 |
| 10 | 6 ApproachWaypoint | in the city, `path_point` set and HP under 0.9 of max (`stc_cpu_heal_hp_ratio`, 0x805e3190) |
| 11 | 0xb SteerTargetWiggle | in the city, enable 0x08, `interaction_target` or a box item target within 40 units |
| 12 | 0xc SteerTargetAdvance | in the city, enable 0x08, a non-box item target within 40 units or on the rider's range |
| 13 | 3 ZonePress | enable 0x01, `rand < 0.2*scale + 0.01`, the nearest side-1 forward zone |
| 14 | 4 ZonePass | as 13 for side 0 |
| 15 | 7 ChargeHold | enable 0x01, and `Rider_CPUCheckSharpCorner` (0x8026ccec) on a `CPUMACHCAP_BRAKE` and `CPUMACHCAP_CHARGE_HOLD` machine, or on a bike `bike_slip` (`MachineData+0xc54`) above 0.5 |
| 16 | `base_maneuver` | unless the current maneuver is 0xa |

Machine-state guards outrank intent: a flying, railed or jumping machine overrides whatever the
profile wants. The sharp-corner check needs a speed of 0.6076 or more and counts frames where the
route is classified a sharp turn or bends past the machine's `CpuMachineSteer.stuck_angle` from
the body's heading; it returns 1 on the ninth straight one, so a CPU charges (brakes) into tight
corners.

**ChargeHold** presses every frame while steering the route and only considers letting go once
`steer_flags` 0x80 says it is aligned. Then Bulk and Hydra keep holding to 0.9 and 0.999
(`MachineGObj_CPUGetChargeReleaseOverride`), profile 2 keeps holding while the gauge is at or under
`CpuMachineCaps.charge_release`, and every other profile releases on the first aligned frame. A
rider holding the Bird ability releases as soon as it is aligned.

## Maneuvers

`Rider_ProcessCPUManeuver` dispatches through the 22-entry table at 0x804b79b8; an out-of-range
value and the unused 0x11 run Coast.

| Man | Handler | Pad pattern |
|---:|---|---|
| 0 | `Rider_CPUManeuverCoast` (0x8026c0a0) | Steer toward the heading, no button |
| 1 | `Rider_CPUManeuverRecoverForward` (0x8026dd4c) | Steer to the route point, charge-tap when stuck |
| 2 | `Rider_CPUManeuverSteerToNav` (0x8026de68) | Steer to `nav_target_pos` |
| 3 | `Rider_CPUManeuverZonePress` (0x8026e448) | Steer into the zone, 4-frame press once the machine is in a dash or flagged zone; 0xa when rolling backward |
| 4 | `Rider_CPUManeuverZonePass` (0x8026e97c) | Steer through the zone, never press |
| 5, 6 | `Rider_CPUManeuverApproachWaypoint` (0x8026ee7c) | Drive to the waypoint, press and hold on arrival |
| 7 | `Rider_CPUManeuverChargeHold` (0x8026ed4c) | Hold charge along the route until aligned, then release (above) |
| 8 | `Rider_CPUManeuverFly` (0x8026fbe0) | Fly the route through `Rider_CPUEmitFlySteer` (0x8026f0e4) |
| 9 | `Rider_CPUManeuverRailRide` (0x8026fe7c) | Ride the rail; a `CPUMACHCAP_RAM_CHARGE` machine charges approaching a rail junction or end |
| 0xa | `Rider_CPUManeuverReverseRecover` (0x8026f00c) | Press and steer at the nav target |
| 0xb, 0xc | `Rider_CPUManeuverSteerTargetWiggle`, `Rider_CPUManeuverSteerTargetAdvance` | Steer at the target, wiggle or advance |
| 0xd | `Rider_CPUManeuverDismount` (0x80270758) | Press with stick_y -127: get off (`checkIfGettingOffBike`) |
| 0xe-0x10 | `Rider_CPUManeuverNavSteer`, `Rider_CPUManeuverNavSteerTap`, `Rider_CPUManeuverChargedNavSteer` | Steer the route, plus a tap or a stall charge |
| 0x12 | `Rider_CPUManeuverTapOnce` (0x802715a8) | One press |
| 0x13 | `Rider_CPUManeuverWiggle` (0x802715e0) | Random stick shake |
| 0x14 | `Rider_CPUManeuverLaunchCharge` (0x802707c4) | Press, centred stick, until GO |
| 0x15 | `Rider_CPUManeuverChargeStill` (0x80270850) | Press, centred stick |

The forward zones come from `Rider_CPUForwardLookahead`: collision zones within 100 units and a
20-degree cone ahead, from a KD-tree walk near the rider plus the stage's moving zones. Each
`CpuForwardTarget` is `{zone, pos, side}`; side 1 marks dash panels (zone kinds 0-1) and flagged
yakumono zones (kinds 12-13), side 0 dash gates (2-3) and the copy panel (15). The arbiter copies
the chosen one to `zone_target_id` / `zone_target_pos` (+0xd0 / +0xd4).

Steering goes through `Rider_CPUEmitSteer` (0x8026d6a0), which bends the heading around a listed
hazard (`Rider_CPUResolveAvoidVector`, 0x8026d1fc, below) and hands it to
`Rider_CPUEmitSteerStick` (0x8026c4ec): a relative nudge or
a ramp by yaw error, bounded by `stc_cpu_steer_envelope` (0x804b7f54, `{s16 step, s16 cap}` per
level, 14/115 at 0 up to 22/127 at 8). **Flight never pitches:** `Rider_CPUEmitFlySteer` compares
a radian pitch error against +/-57.2958 (`stc_cpu_fly_pitch_deadzone`, 0x805e3160), so stick_y
stays 0 unless the heading was bent around a hazard. Its Target Flight branch (0x8026f8ec)
projects the current and desired directions onto the plane across the flight axis, where their
angle is about 0 or pi, so it could not climb or dive at a target even past the dead zone.

## Hazards

Every decider calls `Rider_CPUCollectHazards` (0x80269928), which fills `stc_cpu_hazards`
(0x8055e698, a `CpuHazardList` of up to eight `CpuHazard`s) in five passes, stopping when one
fills the list:

| Pass | Function | Hazards |
|---|---|---|
| Riders | `Rider_CPUCollectRiderHazards` (0x80268234) | Other riders' bodies and hurt regions |
| Machines | `Rider_CPUCollectMachineHazards` (0x80268808) | A radius-4 body for each machine in a filled spawn slot, and every machine's hit regions; skips the rider's own and `city_object` |
| Static | `Rider_CPUCollectStaticHazards` (0x802695ec) | Spheres from `Rider_CPUGetStaticHazardSpheres` (0x80277104) scaled by the stage scale; retail stubs that to none |
| Enemies | `Rider_CPUCollectEnemyHazards` (0x802691ec) | Hit regions grown by `CpuEnemyInfo.hazard_pad`; skips the special actors 0x48-0x4a and enemies in common states 1-10 |
| Weapons | `Rider_CPUCollectWeaponHazards` (0x80268e24) | Hit regions, skipping the rider's own weapons; only with behavior 0x02 |

A hazard is listed when it is within 100 units and either already overlaps the rider (its radius
plus the rider's largest hurt region plus 1) or is approaching on a line that passes that close. Each entry records the source (`enemy`, `rider`,
`weapon` or `machine` at +0x00-+0x0c, none for a static sphere), `pos`, `rel_pos` and `rel_vel`
(the hazard's last displacement minus the rider's velocity), `radius`, and `time_to_impact`:
`|rel_pos| / |rel_vel|` frames when that is under 10, which also sets `imminent` (+0x3c bit 0x80),
else 10000. Bit 0x40 marks a static sphere. The whole collection is skipped while the rider has
no horizontal velocity.

`Rider_CPUResolveAvoidVector` takes the first eligible entry in list order, then bends around
it only while its `time_to_impact` is above 10 frames - never around an imminent one - by
`atan2(|dir|, radius)` on a normalized `dir`, so a larger hazard gets a smaller bend. Enemies
count only under `status_flags` 0x20 and never the one behind `route_goal`. An imminent hazard also
feeds the Wiggle row. `CpuEnemyInfo` (0x804b8000, one per `EnemyKind`) supplies the copy kind and
inhale priority the arbiter's inhale row and `Rider_CPUScanRouteGoal` weigh, and the counterable
flag (0x40) row 4 and the ability presses check.

## Navigation

The default steering target is a look-ahead point, `nav_target_pos` (`CpuData+0xb8`). The race
deciders (Cruise, CruisePlus, Charge, Reposition) sample the course spline
`Gr_GetCourseSpline(RiderData.track_spline_id)` at `track_arc_pos` and 40 units further on, and
put the nav target 200 units from the rider along that chord (`stc_cpu_cruise_nav_point`,
0x8055de80). `steer_target_pos` (+0xc4) is what the route plans toward:
`Rider_CPUResolveTargetPos` (0x80263120) takes the point 100 units along a range's centre line,
on `target_secondary` when there is one, and the nav target wins whenever it is the nearer of the
two.

Both sit on the stage's **CPU ranges**: road segments, each a pair of linear splines for the left
and right edges plus a centre line (`Spline_GetForward` / `Spline_GetBackward` /
`Spline_GetCenter`), running between two junction points. `Rider_CPUUpdateNavTarget` (0x8026b6d0)
puts the rider's own range and its position along it in `target_primary` / `target_lead` (+0x38 /
+0x3c), and `walk_dir` (+0x40) is the direction of travel along it, +1 unless behavior 0x20 takes
it from the plan.

**The route.** Cruise, CruisePlus, Navigate and RouteFollowCity rebuild `stc_cpu_route` (a
`CpuRoute`) every frame:

1. `Rider_CPUBuildRoute` (0x8026a734) lays four road cross-sections, the first at the rider's
   position on `target_primary` and each one hop on through `Rider_CPUWalkRoute` (0x80264924). A
   hop is 15 units at speed 0.405 or less, rising linearly to 35 at 1.62. When the planned next
   range (`target_secondary`, +0x44) lines up with the current one, the last three go on it
   instead. It fails only with no range, at the end of a dead-end range or on a degenerate
   segment - never on a sharp corner. Each waypoint `point` starts at the road centre.
2. `Rider_CPUClassifyRoute` (0x80266584) labels the shape from the angles between segments against
   the `CpuMachineSteer` cosines: straight, a turn, a sharp turn or an S-bend, left or right.
3. `Rider_CPUBuildRouteGaps` (0x80267a4c) records, per waypoint, the spans across the road free of
   collision zone triangles in the route's height band, as fractions from the right edge (0) to
   the left (1).
4. `Rider_CPUPlaceRouteLateral` (0x80267238) moves waypoints 1-3 toward the inside of the turn,
   each on the line from the rider to the next one and snapped into the nearest free gap. It goes
   at most `0.5 * scale + 0.4` of the half-width off centre: 40% at level 0, 90% at level 8. It is
   skipped when the route follows `target_secondary`.

Maneuver 1, the race profiles' base, and the other route maneuvers steer with
`Rider_CPUGetRouteDir` (0x8026c100): at the first usable waypoint, switching to
`steer_target_pos` once a waypoint lies past it, and at `nav_target_pos` with no route.

At a junction the route follows `target_secondary`. With no plan it ranks the exits with
`Rider_CPURankJunctionExits` (0x80264510) - by how well each lines up with the target - and picks
`HSD_Randi(n - n*level/8)` of them: random at level 0, always the best at level 8. On
three courses `Rider_CPUGetPreferredFork` (0x80277194) forces an exit to the top: range 5 on Sky
Sands, a list of eight on Celestial Valley, and range 0x11 on Machine Passage for machines without
`CPUMACHCAP_SKIP_PASSAGE_BRANCH`.

**City path-finding.** Navigate re-plans every frame with `Rider_CPUPlanRoute` (0x80265d28,
from `target_primary` toward `steer_target_pos`):

1. `Rider_CPUFindGoalRange` (0x80264eb4) maps the goal to the nearest range whose left, right or
   centre line passes within 5 units of the goal's height, retrying with the goal shifted 20 units
   along X and Z. The City has 802 ranges and none higher than y = 123, so a goal above that - a
   rooftop or anything airborne - has no range and no plan.
2. `Rider_CPUBuildRangePath` (0x80265b44) runs `grRangeSpline_FindPath` (0x800cfb34), which reads
   an all-pairs next-hop table over the junction points, from each end of the start range; the
   shorter path's first five ranges go to `route_entries` (+0xe8) and the second to
   `target_secondary`.

Two overrides apply. When the target is more than 15 units above the rider, the steer target
becomes the nearest super jump or jump zone within 250 units at the rider's height
(`stc_cpu_jump_zones`, built once per scene by `Rider_CPUBuildJumpZoneTable`, 0x80262c00). And
`Rider_CPUGetRouteDir` (0x8026c100), the direction the route maneuvers steer, ignores the route in
the city whenever `navigate_frames / 1200` is odd (`CpuData+0x36`, incremented every Navigate
pass): every other 20 seconds a city CPU drives straight at `nav_target_pos`.

The jump-zone override writes only `steer_target_pos`, while the junction ranking reads
`nav_target_pos`. On Kirby Melee 2 (GrColosseum5) that matters: its ranges form two networks with
no link between them - the pit and lower deck, and the upper deck and outer rim - and
`Rider_CPUScanRouteGoal` picks an enemy without regard to that, so a CPU on the rim chasing one in
the pit laps the rim.

## City decisions

Navigate's target is, in order: a field machine it is switching to (`city_object`), the
`path_point` while maneuver 6 runs, `interaction_target`, then `item_target`. It fills them from
the city each frame:

- **Recovery.** `CityRecoveryZone_FindNearest` (0x800f7e30) - the nearest Recovery Zone in state
  0 or 1 - or failing that `CityLighthouse_FindBeamSpot` (0x800f7e50), where a lit Lighthouse beam
  lands. Within 400 units it becomes `path_point` (+0xac), which cascade row 10 drives to under 0.9
  HP.
- **Event Pillar.** `CityEventPillar_GetPos` (0x800f7e10) within 300 units becomes
  `interaction_target` (+0xa8), which steers ahead of items.
- **Items.** `Rider_CPUScanItems` (0x80263c4c) ranks the top five items by
  `Rider_CPUGetItemScore` (0x802768a8) plus distance bands, skipping any that score below 1. The score table at 0x804b862c rates stat
  patches, All Up, HP and blue boxes 50, food 5 (20 under a quarter HP, 10 under half), abilities,
  weapons and green or red boxes 1, the Max boosts, Candy and legendary parts 0, and down and fake
  patches, Speed Min, Charge None and the Sleep ability -1. It keeps a target until a radius-2
  rescan comes back empty, then scans 700 units around the rider and then the whole map.

**Machine switching.** Cascade row 7 needs `status_flags` 0x40, which Navigate, Charge and
Reposition set, a machine, and no 0x1000000 inhibit; only the city lets a rider off, so it matters
there. If the machine is stuck in a wall (`MachineGObj_IsStuck`) the CPU dismounts on a 1-in-100
roll per pass. Otherwise, when the machine may swap (`CPUMACHCAP_SWAP`),
`Rider_CPUScanCityObjects` (0x802638a4) looks among the city's spawned field machines within 100
units for one with a higher `CpuMachineCaps.swap_score`, and on `rand < 0.05*scale` stores it in
`city_object` and commits Dismount. On foot the cascade runs NavSteer toward it, and
`Rider_CheckMachineBoarding` (0x8019ef24) boards it on contact. The scan skips stuck machines,
machines above y = 150, and machines on a few special ranges the rider is not on, reading each
spawn slot's range from `stc_cpu_machine_slot_range` (0x8055de24, one slot refreshed per frame).

Per-machine behavior comes from tables indexed by machine kind: `CpuMachineCaps` (stars
0x804b8854, bikes 0x804b89d0) with the swap score and capability flags, and `CpuMachineSteer`
(0x804b8f30) with alignment cosines, the turn tolerance and the corner angle.

## Command opcodes

`Rider_CPUProcessCmd` counts `cmd_timer` (+0x110) down and reads the next opcode at `cmd_read_ptr`
(+0x114) when it reaches 0; maneuvers append at `cmd_write_ptr` (+0x118) into the 128-byte
`cmd_buffer` (+0x11c) with `Rider_CPUPushCmd1/2/3` (0x80275fcc, 0x80276050, 0x80276118). Any
other byte asserts.

| Opcode | Operands | Effect |
|---:|---|---|
| 1 / 2 | - | press / release 0x100 |
| 100 | - | release everything |
| 127 | - | stop |
| 128 / 129 | s8 | set `stick_x` / `stick_y` |
| 150 / 151 | u8 n | press / release, then hold n frames |
| 180 | u8 n | wait n frames |
| 190 | s8 delta, s8 cap | `stick_x += delta`, clamped to +/-cap |
| 192 | s8 step, s8 target | move `stick_x` toward target by at most step |

## Notes for modders

- **Difficulty.** `difficulty_level` is the finest lever: every roll scales with it, as do the
  junction choice, the steering envelope and the ability press timing.
- **Flags.** Rewrite `behavior_flags` or `desire_flags` after the decide stage; both are rebuilt
  every frame, so a one-time write does not stick.
- **Maneuver.** The exit of `Rider_CPUArbitrateManeuver` (0x80275bc4, with `r31` the
  `RiderData*`) sees the committed maneuver and can veto it back to `base_maneuver` or commit
  another.
- **Target.** Write `nav_target_pos` each frame after the decide stage. In Navigate, a hook at
  0x80272258 (`r28` the `RiderData*`) runs after the handler picks `nav_target_pos` and before it
  becomes the steer target and route goal. A goal no range reaches gets no plan, so keep city
  goals on the ground.
- **Flight and routing quirks.** Writing a small radian threshold into
  `stc_cpu_fly_pitch_deadzone` lets CPUs pitch; zeroing `navigate_frames` each frame keeps a city
  CPU on its route.
- **The pad.** Hooking `_Rider_UpdateCPU` gives a CPU-only entry with the `RiderData*` in `r3`.
  Calling `Rider_UpdateCPU` and then adjusting `cpu->buttons` / `stick_x` / `stick_y` is the
  lightest touch; skipping it replaces the brain entirely.
