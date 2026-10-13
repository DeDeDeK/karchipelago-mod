# CPU Rider AI

How the computer-controlled riders of City Trial and Air Ride decide what to do and drive. A CPU
rider runs the same `RiderData` -> `MachineData` pipeline as a human; the only difference is
where its controller input comes from. Each frame its brain picks a maneuver, writes a short
command script for it, and plays the script into a **virtual pad** that the rider reads in place
of a real controller. Everything downstream - physics, charge, collision - is shared with
humans, which makes the pad, the maneuver choice and the navigation target the places a mod can
retune CPU behavior. Top Ride's CPU is separate C++ code on `TopRideKirby` and shares nothing
with this brain.

## The virtual pad

`RiderData.cpu` (+0x778) points at the rider's `CpuData` (0x19c bytes), allocated by
`Rider_CPUInit` (0x80262d6c) for CPU riders and NULL for humans. Its first fields are the pad:
`buttons` (+0x00), `stick_x` (+0x04) and `stick_y` (+0x06). `RiderGObj_InputThink` (0x8018ee28)
copies them into the rider's input fields (`held` +0x3d8, `stickX` +0x3ec, `stickY` +0x3ed)
through `Rider_GetCPUButtons` / `Rider_GetCPUStickX` / `Rider_GetCPUStickY`, which are plain
field reads (the sticks truncated to s8). Writing the pad fields is exactly equivalent to
pressing the pad. The brain only ever toggles one button, 0x100 (press/charge).

Allocated `CpuData` blocks are listed in a five-entry table at 0x8055de08; they are freed with
the scene, never per rider.

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

1. **Perceive** refreshes the rider's view of itself: its machine's kind, position and velocity,
   a short forward ray, and two anti-stuck detectors - position (no progress for 241 frames sets
   `status_flags` 0x04) and velocity (too slow or against the facing for 60 frames sets 0x02),
   both active only while behavior bit 0x40 is set. It also clears three per-frame scratch lists
   the later stages fill: route waypoints (0x8055e964), hazards (`stc_cpu_hazards`, 0x8055e698)
   and forward targets (`stc_cpu_forward`, 0x8055e8b4).
2. **Decide** dispatches on `ai_state` to one of nine handlers. Each rewrites `behavior_flags`,
   seeds `desire_flags`, picks the navigation target and the target entities, and tail-calls
   `Rider_CPUArbitrateManeuver` (0x80274ec0), the only writer of `maneuver`.
3. **Emit** runs only when the command player is idle: it resets the write pointer, runs the
   maneuver's handler (which appends steering and button opcodes), then
   `Rider_CPUEmitAbilityAction` (0x80273d1c, copy-ability presses) and
   `Rider_CPUTerminateCmdStream` (0x80276228), which caps the stream and arms the player. A
   maneuver's script therefore plays out before the next maneuver is emitted.
4. **Play** is a bytecode interpreter and the only writer of the pad fields.

## Profile and maneuver

The brain has two layers, and only the lower one changes during a match.

`ai_state` (`CpuData+0x08`, 1-10) is the **profile**, chosen once by `Rider_CPUInit` and never
written again. `Rider_CPUInitPlayer` (0x80275c04) passes 0, which picks it with
`Rider_CPUSelectProfile` (0x802766fc): City Trial's city gets 3 (Navigate), Target Flight 7,
Kirby Melee 1 and 2 get 5 and 6, a rider in slot 4 gets 10, and anything else is looked up by
`Gm_GetCityKind()` in the table at 0x804b7f78, defaulting to 2. The Destruction Derby (city kind
0xe) and the King Dedede fight (0x12) map to 8 (Attack); every Air Ride race is 2.
`Rider_CPUInitPlayerFixed` (0x80275c40) forces profile 1 at level 8; `Game_Think` (0x80011024)
uses it to put a human's rider on autopilot when the city kind is `AIRRIDE_RULE_TIMEATTACK` (4),
marking the slot CPU only after the init returns.

| State | Handler | Profile | Behavior flags |
|---:|---|---|---|
| 1 | `Rider_CPUDecideCruise` (0x80271790) | Follow the racing line, combat muted | 0xc1 |
| 2, 4, >10 | `Rider_CPUDecideCruisePlus` (0x80271b24) | Racing line with item and attack desires | 0xd7 |
| 3 | `Rider_CPUDecideNavigate` (0x80271eb4) | City path-finding, items and machines; holds the stuck-recovery sweep | 0xff |
| 5 | `Rider_CPUDecideRouteFollow` (0x802726fc) | Steer at the best inhalable enemy | 0x17 |
| 6 | `Rider_CPUDecideRouteFollowCity` (0x80272888) | As 5, plus city objects and stuck recovery | 0xf7 |
| 7 | `Rider_CPUDecideCharge` (0x80272dd0) | Drive to a charge anchor and charge | 0xd5 |
| 8 | `Rider_CPUDecideAttack` (0x802735dc) | Chase the nearest rival in a cone, spin-attack | 0x1f |
| 9 | `Rider_CPUDecideReposition` (0x80273228) | Stage-specific position nudges | 0xd5 |
| 10 | `Rider_CPUDecidePatrol` (0x80273b48) | Timed toggle between nav target and rival | 0x17 |

State 0 asserts. So a CPU's personality is fixed by stage: a Destruction Derby CPU is born in
Attack and never becomes aggressive mid-match. The dynamism - chase, grab, dodge, recover -
lives in `maneuver` (`CpuData+0x10`), re-chosen every time the previous script ends.
`base_maneuver` (+0x14) is the maneuver a profile parks on (1 or 2) and falls back to.

Difficulty is `difficulty_level` (`CpuData+0x22`, 0-8, the CSS CPU level), which
`Rider_CPUDifficultyScale` (0x80276f00) turns into `scale = level / 8`. Only Charge and Attack
roll in their handlers: Charge commits ChargeStill near its anchor when `rand < scale`; Attack
picks a waypoint every 300 frames on `rand < 0.75*scale + 0.05` and fires its spin burst on
`rand < 0.8*scale + 0.1` (left or right on a plain coin flip). The other profiles are
deterministic; the moment-to-moment rolls live in the cascade.

## Behavior and desire flags

Two flag fields gate the cascade, with opposite polarity.

`behavior_flags` (+0x2c) are **enable** bits, rewritten by every decide pass from the profile
(values in the table above):

| Bit | Enables |
|---|---|
| 0x01 | Opportunistic maneuvers: ChargeHold, RamCharge, PursueLOS |
| 0x02 | An extra pass in `Rider_CPUCollectHazards` (0x80269928) |
| 0x08 | The target-steer maneuvers 0xb and 0xc |
| 0x20 | Predictive target leading in `Rider_CPUBuildRoute` |
| 0x40 | Rival pursuit and intercept setup, and the anti-stuck detectors |
| 0x80 | The forward-collision lookahead (`Rider_CPUForwardLookahead`, 0x80269f10) |

`desire_flags` (+0x1c) are **inhibit** bits, cleared each pass and re-seeded from the rider's
current action state by `Rider_CPUSeedDesire` (0x802762dc); Cruise also ORs in 0xf00:

| Bit | Suppresses |
|---|---|
| 0x100 | RamCharge's press |
| 0x200 | Obstacle-avoidance steering |
| 0x400 | The hazard dodge, the attack scan and the wiggle |
| 0x1000000 | Charge-up and rival intercept |

The seed is a flat lookup by `RiderData.status`: `{id, flags}` pairs at 0x804b7b18 for the 29
common states (every rider kind) and at 0x804b7c00 for states 29-130 (Kirby only; other riders
in those states get no seed). The damage, knockback, spin and stun states 0x35-0x6e carry
0x1000000, the free states (0x1a-0x1c, 0x21-0x27, 0x82) carry nothing, and the rest mute some
mix of ram, avoid and dodge.

## The arbitration cascade

`Rider_CPUArbitrateManeuver` walks a fixed priority list and commits the first maneuver whose
guard passes; most arms return early while their maneuver is already running.

| Pri | Commits | Guard |
|---:|---|---|
| 0 | 0xf NavSteer+Tap | current is 0xe and stuck or a hazard is listed |
| 1 | 0 Coast | machine hard reset (`MachineData+0xc33` bit 0x08) |
| - | keep | current is 0xe, 0xf or 0x10 |
| 2 | 0xe NavSteer | no machine |
| 3 | 8 AvoidObstacle | machine hazard state, no flag gate |
| 4 | 0x12 TapOnce | attack/target scan finds a target (rolled) |
| 5 | 0x13 Wiggle | imminent hazard and a roll; inhibited by 0x400 |
| 6 | 9 DodgeWeapon | the machine is being hit |
| 7 | 0xd ChargeCentered | Navigate's city-object pick beats the current machine, roll `< 0.05*scale` |
| 8 | 0x14 Brake | profiles 2/4/7/9, level > 3, brakeable machine, roll `< scale` |
| 9 | 7 ChargeHold | enable 0x01, chargeable machine near the item target |
| 10 | 6 ApproachWaypoint | in the city with a path point set |
| 11, 12 | 0xb, 0xc SteerTarget | enable 0x08 and a target within 1600 units |
| 13 | 3 RamCharge | enable 0x01, a ram candidate ahead, roll `< 0.2*scale + 0.01` |
| 14 | 4 PursueLOS | as 13 for a pursuit candidate |
| 15 | 7 ChargeHold | enable 0x01 and a charge-level gate |
| 16 | `base_maneuver` | always |

Machine-state guards outrank the flag-gated ones, so a machine in a hit or hazard state
overrides the AI's intent. RamCharge copies the nearest `side == 1` entry of `stc_cpu_forward`
to `CpuData+0xd0` / `+0xd4` before committing. ChargeStill (0x15) is only ever set by the Charge
profile.

## Maneuvers

`Rider_ProcessCPUManeuver` dispatches through the 22-entry table at 0x804b79b8; an out-of-range
value, and the unused index 0x11, run Coast.

| Man | Handler | Pad pattern |
|---:|---|---|
| 0 | 0x8026c0a0 | Coast: steer toward the heading, no throttle or button |
| 1 | 0x8026dd4c | RecoverForward: steer to the route point, charge-tap when stuck |
| 2 | 0x8026de68 | SteerToNav: steer to `nav_target_pos` |
| 3 | 0x8026e448 | RamCharge: intercept a rival, short press on contact |
| 4 | 0x8026e97c | PursueLOS: steer at a rival in the cone, never press |
| 5, 6 | 0x8026ee7c | ApproachWaypoint: press and hold on arrival |
| 7 | 0x8026ed4c | ChargeHold: hold charge until the gauge tops off |
| 8 | 0x8026fbe0 | AvoidObstacle: snap to a clear route waypoint |
| 9 | 0x8026fe7c | DodgeWeapon: rolled evasive burst against an incoming projectile |
| 10 | 0x8026f00c | ChargeRelease: charge toward the target, then back to base |
| 0xb, 0xc | 0x80270154, 0x802704c4 | SteerTarget: steer at the target, wiggle or advance |
| 0xd | 0x80270758 | ChargeCentered: press, center the stick, full throttle |
| 0xe-0x10 | 0x80270bac, 0x80270e64, 0x802711d8 | NavSteer, plus a tap or a stall-recovery charge |
| 0x12 | 0x802715a8 | TapOnce |
| 0x13 | 0x802715e0 | Wiggle: random stick shake |
| 0x14, 0x15 | 0x802707c4, 0x80270850 | Brake, ChargeStill: press with a centered stick |

Cruise and navigate maneuvers steer along waypoints from `Rider_CPUBuildRoute` (0x8026a734),
which walks the course graph up to three hops ahead and fails on a missing link or too sharp a
corner so the handler can fall back to recovery. Steering goes through `Rider_CPUEmitSteer`
(0x8026d6a0), which bends the heading away from the nearest imminent hazard and hands it to
`Rider_CPUEmitSteerStick` (0x8026c4ec). That picks a relative nudge or a ramp by yaw error,
bounded by a per-level `(step, cap)` envelope at 0x804b7f54 that widens with level (14/115 at 0
up to 22/127 at 8), and sets the throttle by machine kind. `Rider_CPUEmitChargeStutter`
(0x8026da40) pumps the charge button to break free while stuck.

## Command opcodes

`Rider_CPUProcessCmd` counts `cmd_timer` (+0x110) down and only reads the next opcode at
`cmd_read_ptr` (+0x114) when it reaches 0; maneuvers append at `cmd_write_ptr` (+0x118) into the
128-byte `cmd_buffer` (+0x11c) with `Rider_CPUPushCmd1/2/3` (0x80275fcc, 0x80276050,
0x80276118). Any other byte asserts.

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

## Targets and navigation

The default steering target is a look-ahead point on the rider's course spline
(`RiderData.track_spline_id`), at `CpuData+0xb8` (`nav_target_pos`); `steer_target_pos` (+0xc4)
is that after the city-object override, and is what maneuvers steer toward. `target_primary`
(+0x38) and `target_secondary` (+0x44) are indices into the stage's spline-node array, not
entity handles - `Rider_CPUUpdateNavTarget` (0x8026b6d0) snaps them to the racing line, and an
out-of-range id asserts. Rival pursuit bypasses them by writing the rival's live position into
`nav_target_pos`.

The decide handlers fill the target fields from scans over live objects:

- **Rival** - `Rider_CPURivalSelect` (0x80264210) into `rival_ply` (+0x70, 5 = none), re-picked
  every 60-119 frames, scoring distance, whether the rival is human or already engaged, and its
  HP when damage is on.
- **Item** - `Rider_CPUScanItems` (0x80263c4c) ranks the top five items around a point by
  `Rider_CPUGetItemScore` (0x802768a8) plus distance bands. The score table at 0x804b862c rates
  stat patches, All Up, HP and blue boxes 50, food 5 (20 at under a quarter HP, 10 under half),
  abilities, weapons and green or red boxes 1, the Max boosts, Candy and legendary parts 0, and
  down and fake patches, Speed Min, Charge None and the Sleep ability -1. Navigate keeps its
  target until a radius-2 rescan around it comes back empty, then scans 700 units around the
  rider and falls back to the whole map.
- **City object** - `Rider_CPUScanCityObjects` (0x802638a4), field machines worth switching to.
- **Route goal** - `Rider_CPUScanRouteGoal` (0x80263fd0), the best inhalable enemy.
- **Charge anchor** - `Rider_CPUSelectChargeAnchor` (0x80263610), for the Charge profile.

Per-machine behavior comes from tables indexed by machine kind: `CpuMachineCaps` (stars
0x804b8854, bikes 0x804b89d0) with a swap score and capability flags (may switch machines,
brake, charge-hold, ram), and `CpuMachineSteer` (0x804b8f30) with alignment cosines, the turn
tolerance and the stuck angle.

## Notes for modders

- **Difficulty.** `difficulty_level` is the finest lever: every roll scales with it, as do the
  steering envelope and the ability press timing.
- **Flags.** Rewrite `behavior_flags` or `desire_flags` after the decide stage to enable or mute
  a capability; both are rebuilt every frame, so a one-time write does not stick.
- **Maneuver.** The exit of `Rider_CPUArbitrateManeuver` (0x80275bc4, with `r31` the
  `RiderData*`) sees the committed maneuver and can veto it back to `base_maneuver` or commit
  another; the arm guards above say what each needs to run.
- **Target.** Write `nav_target_pos` each frame after the decide stage rather than the
  spline-node ids. In Navigate, a hook at 0x80272258 (`r28` the `RiderData*`) runs after the
  handler picks `nav_target_pos` and before it becomes the steer target and route goal.
- **The pad.** Hooking `_Rider_UpdateCPU` gives a CPU-only entry with the `RiderData*` in `r3`.
  Calling `Rider_UpdateCPU` and then adjusting `cpu->buttons` / `stick_x` / `stick_y` is the
  lightest touch; skipping it replaces the brain entirely.
