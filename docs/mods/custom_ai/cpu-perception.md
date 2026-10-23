# Custom AI: Perception and Racing Line

What custom_ai adds to what a CPU rider knows: a shared per-tick view of every rider and of the
City Trial world, a per-CPU view of its own machine, a decoded hazard list, a racing line per Air
Ride course, and a glide predictor.

## The per-tick view

The first CPU update of each tick, from the `_Rider_UpdateCPU` call in `RiderGObj_CPUThink`
(0x8018fc80), refreshes a view of every player slot and of the world, which the presets'
threat, vulnerability, HP and chase checks read. It runs only where CPUs think, so a player's
rider pointer is never read after its round, and it freezes with the riders' p_link in the
match pause. A new tick is told by `HSD_Update.engine_frames`.

| Rider view | Source |
|---|---|
| on foot | `RiderData.machine_gobj` |
| spinning | `MachineGObj_IsQuickSpinning` |
| boosting | `MachineData.boost_hitbox_timer` or `dash_frames` |
| invincible | `RiderData.candy_duration`, or a rider or machine `HurtData.vuln` that is invincible, intangible or timed |
| stunned | `RiderData.status` from `RDSTATE_DAMAGE` through `RDSTATE_GETOFF_FALLDEATHWAIT` |
| HP ratio | `Ply_GetHP` / `Ply_GetMaxHP` |
| position, velocity | `Ply_GetPosition`, `MachineData.world_velocity` (on foot `RiderData.self_vel`) |
| race progress | `GameData.player_race_distance`, metres, on a course |

A rider is threatening when spinning, boosting or invincible, and vulnerable when not
invincible and stunned, on foot or under 35% HP.

The world view holds the City Trial round's progress (`grBoxGeneInfo.match_progress`), the
running event (`CityEvent_GetActiveKind`), and each meteor's predicted impact: a
falling meteor's velocity is cast 600 units ahead against the ground (`Raycast_Ground`), giving
the impact point, the frames to it, and a blast radius of 25 units per unit of
`EnemyData.final_scale`.

Each CPU's own view - its speed and charge - is refreshed at the start of its own update.

## Hazards

`Rider_CPUCollectHazards` fills the per-update `stc_cpu_hazards` list (8 `CpuHazard` entries:
the enemy, rider, weapon or machine behind it, world and relative position, relative velocity,
radius, frames to impact - under 10 marks it imminent - and a static flag). Every decide
handler's call to it is wrapped: after the list is built, the mod records each CPU's soonest
imminent threat - its kind and, for a rider or machine, the player behind it - which Cautious
dodges by.

Static spheres come from `Rider_CPUGetStaticHazardSpheres`, a stub in retail, whose call the mod
replaces (0x8026964c) with a per-tick list in stage units (`grGetStageScale`): with CPU Fixes on,
every active yakumono attack region that can hurt a rider or machine. The static pass lists a
sphere within 100 units on the CPU's path, so every CPU steers around it.

## Racing line

At each Air Ride 3D load, when some CPU will follow it (Racing Line is All CPUs, or a CPU rolled
Racer), the mod builds a line from the first CPU's range, set by `Rider_CPUInit`:

1. **Graph.** Each range's continuations are the enabled ranges `Spline_GetJunctionExits` lists
   at its end point that are driven start to end, as races drive every range. Directions come
   from the centre lines (`Spline_GetCenter`).
2. **Lap.** From the CPU's range, the straightest continuation is followed until the walk returns
   (a loop) or stops. Each lap range records its lap position.
3. **Cross-sections.** Every 8 units of the lap, wider when the lap needs more than 1536 less one
   per range (each range takes at least one), the left and right road edges
   (`Spline_GetForward`, `Spline_GetBackward`) at the same position.
4. **Relaxation.** 300 passes pull each point toward the midpoint of its neighbours, kept on its
   cross-section and 3 units off each edge (10-45% of the road). The line straightens until it
   touches the inside of each corner.
5. **Curvature.** Turn per unit at each point, raised to the sharpest of the next four so a
   corner reads sharp on its approach.

The buffers live on the scene heap and are dropped at each 3D load.

**Steering.** A hook in `Rider_CPUDecideCruisePlus` after the route's waypoints are placed
(0x80271e58, r30 the RiderData, r28 the `CpuRoute`) moves waypoints 1-3 onto the line: the
nearest line point to each waypoint's cross-section is projected across it, snapped into the
free gaps vanilla found (`CpuRoute.gap`), and blended in by the CPU's line weight. The route is
classified from the moved points afterwards, so `Rider_CPUGetRouteDir` and the corner checks
follow them. The weight is 1 for a Racer (unless the line is Off) and, under All CPUs,
0.5 -> 1 by skill for the rest; on a range off the lap, such as a shortcut branch, it is 0 and
the CPU drives vanilla's route until it rejoins.

**Braking.** At the arbitration exit a race CPU on the line that can brake and charge-hold
commits ChargeHold when a point within 24 frames ahead at its speed has a corner speed - its
machine's `turn_rate_top` (0.03 on a bike) times 1.3, over the curvature - more than 15% under
its speed. A machine whose `turn_rate_top` is under 0.01 (Swerve, Formula: they turn by
charging) is left to vanilla's braking.

**Shortcuts.** `Rider_CPURankJunctionExits`' `Rider_CPUGetPreferredFork` call (0x8026454c) is
wrapped. Vanilla's per-course picks stand; otherwise, for a CPU on the line and on a lap range,
each exit is walked along its straightest continuations until it rejoins the lap, and its extra
distance is its length minus the lap distance it covers. A branch that never rejoins, or rejoins
more than half a lap on (behind the junction), is never taken. An exit saving 20 or more units
over the lap's own continuation goes to the top of the ranking.

## Glide predictor

While airborne, each CPU's vertical acceleration is measured from its velocity and smoothed. The
predictor steps the current velocity forward 5 frames at a time under that acceleration (clamped
to -0.06..0.02) for up to 600 frames, casting each step against the ground. It sizes Target
Flight's reachable targets.
