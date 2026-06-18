# CPU Rider AI System

How the AI-controlled riders (the CPU opponents in **City Trial and Air Ride**)
make decisions and drive their machines. This is the **rider** AI — the players
you race against — and is entirely separate from the enemy/event-actor AI (see
[enemy-ai-system.md](enemy-ai-system.md)), which drives inhalable Air Ride enemies.

**Scope:** this doc covers the `RiderData`-based modes (City Trial, Air Ride),
which are plain C. **Top Ride is a separate C++ mode** operating on `TopRideKirby`,
with its own CPU handling (`a2d_cpu_kirby.cpp`), now mapped in its own section below.
The two share no code. A separate City-Trial-only mechanic — **passive CPU stat
growth**, where CPUs are handed a difficulty-scaled pool of machine stats that drips
on over the round, independent of the steering brain and of any patch pickups — is
covered in [its own section](#cpu-passive-stat-growth-city-trial).

A CPU rider runs the same `RiderData` → `MachineData` pipeline as a human. The
only difference is the source of its controller input: instead of a physical
GameCube pad, a CPU rider synthesizes a **virtual pad** each frame and feeds it
into the normal rider input fields. Everything downstream (physics, charge,
collision) is identical to a human player. This makes the virtual pad the single
cleanest place to influence or replace CPU behavior.

## Per-Frame Pipeline

Two GObj procs on each rider drive the CPU. They run every frame:

```
Rider_CPUThink (0x8018fc58)            // proc: decide + fill the virtual pad
  └─ if plGetPlayerKind(RiderData.player_slot) == PKIND_CPU (1):
       _Rider_UpdateCPU (0x80275c70)   // thin wrapper
         └─ Rider_UpdateCPU (0x8026beec)            // orchestrator, 4 stages:
              ├─ Rider_ProcessCPUDistance (0x8026bbe0)   // 1. perceive (self-state, anti-stuck)
              ├─ Rider_CPUDecideState     (0x802716e8)   // 2. decide  (strategic state, CpuData+0x08)
              ├─ Rider_ProcessCPUManeuver (0x8026bf30)   // 3. process (tactical maneuver, CpuData+0x10 -> commands)
              └─ Rider_CPUProcessCmd      (0x80275cbc)   // 4. emit    (command stream -> virtual pad)

Rider_InputThink (0x8018ee28)          // proc: read effective input for this frame
  └─ if plGetPlayerKind == PKIND_CPU:
       RiderData.held   (0x3d8) = Rider_GetCPUButtons(RiderData)  // 0x80275cb0
       RiderData.stickX (0x3ec) = Rider_GetCPUStickX(RiderData)   // 0x80275c90
       RiderData.stickY (0x3ed) = Rider_GetCPUStickY(RiderData)   // 0x80275ca0
     (human path reads a real pad; replay path reads 3DReplay_GetInputs)

Rider_CopyInputToMachine (0x80190c54)  // rider input -> machine
```

`plGetPlayerKind` (0x8022c858) returns the `PKIND` of a controller slot
(`PKIND_HMN`=0, `PKIND_CPU`=1, `PKIND_NONE`). It is distinct from
`Ply_CheckIfCPU` (0x8000948c), a separate query.

### Stage roles

1. **Perceive** — `Rider_ProcessCPUDistance` (0x8026bbe0) refreshes the rider's
   *self*-state for the decision logic: it caches the machine ids from
   `RiderData.machine_gobj` (`CpuData+0x0c/+0x0d`), records position/velocity, casts a
   short forward predict-ray (`pos + vel·15` against the course), and caches three
   per-machine turn/speed-envelope scalars into the globals `0x8055e68c/690/694` (read
   by route-building and steering). It runs two **anti-stuck detectors** — a position
   one (no movement away from `recorded_pos` for 241 frames → `status_flags` bit `0x04`)
   and a velocity one (too slow or moving against facing for 60 frames → bit `0x02`) —
   **both gated by behavior bit `0x40` (rival-pursuit)**: when pursuit is off,
   `recorded_pos` is refreshed every frame and the stuck timers never accumulate. It
   also ticks the frame counter (`CpuData+0x28`) and a suppression timer (`CpuData+0x20`)
   that forces the secondary target off while active. Finally it zeroes the three global
   per-frame scratch buffers — `0x8055e964` (route waypoints), `0x8055e698` (hazard/
   threat list), `0x8055e8b4` (forward-collision list); these are **three distinct typed
   structures** (laid out under [Data Addresses](#data-addresses)), single-rider and
   transient, **not** a persistent multi-rider world model.
2. **Decide** — `Rider_CPUDecideState` (0x802716e8) dispatches on the **strategic
   state** `CpuData+0x08` (1..10; **state 0 asserts** — `cpu.c:0x21f1`) to one of 9
   handlers (`0x80271xxx`–`0x80273xxx`; states 2/4/`>10` share `0x80271b24`). The
   `0x804b7a28` table is the compiler's switch jump table, not an array of handler
   pointers. Each handler rewrites the **behavior flags** (`CpuData+0x2c`),
   computes the navigation target (see below), selects the target entities
   (`target_primary`/`target_secondary`), and then tail-calls the **maneuver-
   arbitration cascade** `Rider_CPUArbitrateManeuver` (0x80274ec0) — a flag-gated
   priority list that commits the **tactical maneuver** (`CpuData+0x10`). The
   handler itself does *not* write `+0x10`; the cascade is the sole maneuver chooser.
   It clears the scratch words `CpuData+0x18`/`+0x1c` at entry.
3. **Process** — `Rider_ProcessCPUManeuver` (0x8026bf30) dispatches on the
   **tactical maneuver** `CpuData+0x10` (0..0x15, ~22 handlers) to emit a fresh
   command stream — **but only when the command VM is idle** (`cmd_read_ptr == 0 &&
   cmd_timer == 0`). So one maneuver's command stream plays to completion before
   the next maneuver is chosen. It resets the write pointer
   (`cmd_write_ptr = &cmd_buffer`), runs the maneuver handler — which appends **both**
   the steering opcodes (via `Rider_CPUEmitSteer` 0x8026d6a0 → `Rider_CPUEmitSteerStick`
   0x8026c4ec) and the action opcodes — then runs `Rider_CPUEmitAbilityAction`
   (0x80273d1c, the copy-ability press controller, **not** a steering stage), and
   finally `Rider_CPUTerminateCmdStream` (0x80276228), which caps the stream with opcode
   0x7f and arms the VM. The heading→stick conversion lives **inside** each maneuver
   handler, not in a separate post-stage.
4. **Emit** — `Rider_CPUProcessCmd` (0x80275cbc) is a small **bytecode
   interpreter** and the **sole writer** of the pad output fields
   (`buttons`/`stick_x`/`stick_y`). The decide/process handlers never touch those
   fields directly; they append command opcodes into `cmd_buffer`, and this stage
   plays them back. It decrements the command timer (`CpuData+0x110`); only when it
   reaches 0 does it read the next opcode from `cmd_read_ptr` (`CpuData+0x114`),
   decode it, and continue until a timer-setting or terminating opcode. The command
   buffer is the 128-byte inline array `CpuData+0x11c .. +0x19c`.

### Two layers: a fixed profile over a dynamic maneuver

The brain has two layers on `CpuData`, but only the lower one transitions:

- **`ai_state` (`CpuData+0x08`, 1..10) — the AI *profile*, fixed for the match.**
  It is *not* a per-frame FSM: nothing transitions it during play. It is chosen
  once at `Rider_CPUInit` by `Rider_CPUSelectProfile` from stage/city/ply (see
  [Profile selection](#strategic-states-ai_state-cpudata0x08) below) and then stays
  put. Its decide handler runs every frame and does the real work — re-targeting,
  picking the maneuver, anti-stuck recovery — but the *profile* never changes.
  State 0 is invalid (asserts).
- **`maneuver` (`CpuData+0x10`, 0..0x15) — the dynamic tactical layer.** The
  concrete maneuver that *emits* a command stream, re-chosen by the arbitration
  cascade (`Rider_CPUArbitrateManeuver`, and by maneuvers handing off to each other)
  but only when the previous command stream finishes, giving each maneuver a minimum
  dwell time.

So the dynamism — chase this rival, grab that item, dodge, recover — lives entirely
in the maneuver layer and the per-frame targeting, **not** in strategic-state
switching. A "City Trial combat stadium" CPU is simply born in the Attack profile
and stays there; it never *becomes* aggressive mid-match.

The command stream is the seam between "what to do" (strategic/tactical state) and
"what buttons to press" (the pad). Three pointers walk `cmd_buffer`:
`cmd_read_ptr` (+0x114, VM playback), `cmd_write_ptr` (+0x118, where maneuver
handlers append), and `cmd_timer` (+0x110, the per-opcode frame countdown).

### Navigation model

The vanilla AI is **waypoint / path-graph navigation**, not free pursuit:

- The rider's course path is `RiderData.track_spline_id` (+0x474); its progress
  along it is `track_arc_pos` (+0x47c). A strategic handler resolves the spline
  (`zz_800cefb4_`) and samples a **look-ahead point** ahead of the rider with
  `splArcLengthPoint`, storing it at `CpuData+0xa4` (ptr) / `+0xb8` (Vec3). This is
  the default thing the rider steers toward.
- It can override the track point with a **target entity**:
  `target_primary` (+0x38) and `target_secondary` (+0x44) are nav-node ids,
  resolved to a world position by `Rider_CPUResolveTargetPos` (0x80263120). `target_lead`
  (+0x3c) is the node's **arc-length [0,1] position** (not a predict time); the resolver
  *leads* a moving target by projecting `±100/segment_length` of arc-length ahead (if the
  rider/route dot > +0.7) or behind (< −0.7) along the racing line. The handler steers at
  whichever is *closer* — the track look-ahead or the target entity. `CpuData+0xe4/+0xe8`
  is a small packed cache of upcoming route nodes.

Both target ids are **indices into the current stage's spline-node array** — owned
by the global course object `stc_grobj_ptr` (`r13[0x5ec]` = 0x805dd6cc; node `id`
resolves to record `[grobj+0x120] + id*0x1c`), accessed through the `0x800ce/cf`
course-path module (`grlib`). They are **not** rider or item handles, and an
out-of-range id asserts (`cpu.c:0x3f2`). Valid ids come *only* from the engine's own
nearest-node spatial query (`Rider_CPUUpdateNavTarget` 0x8026b6d0), which snaps to
the racing line — so the id space can only ever point the AI at a course node, never
an arbitrary world point or a moving entity.

Rival pursuit is a **separate channel** that bypasses the id space: the Attack/Patrol
states read `rival_player_idx` (+0x70) and write the rival's *live* position straight
into `nav_target_pos` (+0xb8) via `Ply_GetPosition`. So to steer the AI at an
arbitrary point or entity from outside, write `nav_target_pos` (+0xb8) directly each
frame (the decide stage overwrites it, so re-assert it after the brain runs) — or
just compute steering and write the pad (below). Driving the `+0x38/+0x44` ids is not
practical.

### Target selection & scoring

Each frame the decide handlers run several **scans** that populate the target fields the
maneuvers and the cascade later consume. All are nearest/best-scored picks over live
object lists — none are ids an external caller can synthesize.

- **Rival** — `Rider_CPURivalSelect` (0x80264210), shared by states 1, 2/4, 8, and 10
  (Patrol only when its sub-state == 1) — **not** Patrol-exclusive. Re-picks only when
  `rival_player_idx` (+0x70) == 5 (none) or `rival_reselect_timer` (+0x72) expired; else
  decrements the timer. Scores all 5 slots (skipping self/null/ineligible): distance band
  (close 3 / mid 2 / far 1) **+2** if the rival isn't already engaged, **+2** if the rival
  is human (no `CpuData`), and a low-HP bonus (+1, or +5 when this rider is aggressive)
  when damage is enabled. Winner → `+0x70`, cooldown `HSD_Randi(60)+60` (60–119f); none →
  `+0x70 = 5`. Consumed by writing the rival's *live* `Ply_GetPosition` into
  `nav_target_pos` (+0xb8).
- **Item** — `Rider_CPUScanItems` (0x80263c4c), states 3 & 8. Keeps a **top-5 ranked**
  array (score = base + behind-bonus + distance bands); the caller takes the rotating
  pick → `item_target` (+0x74), pos → `item_target_pos` (+0x78). State 8 re-validates each
  frame, acquires a new one only on `frame%300==0` past the Attack roll.
- **City object** — `Rider_CPUScanCityObjects` (0x802638a4), state 3. Scans actor slots
  0..0x16 with height/distance/kind filters, top-5 ranked → `city_object` (+0x84) /
  `city_object_pos` (+0x88).
- **Route goal** — `Rider_CPUScanRouteGoal` (0x80263fd0), states 5 & 6. **Single best**
  (not ranked) → `route_goal` (+0x94) / `route_goal_pos` (+0x98). States 5/6 fill
  `nav_target_pos` by priority `route_goal > city_object > item_target`.
- **Charge anchor** — `Rider_CPUSelectChargeAnchor` (0x80263610), state 7. Resolves a
  difficulty-scaled anchor node id → `charge_anchor_id` (+0xb4), pos → `charge_anchor`
  (+0xb0).
- **Interaction / path point** — state 3 city look-ahead fills `interaction_target`
  (+0xa8) / `path_point` (+0xac) from the city path-graph; `Rider_CPUBlendRoutePoints`
  (0x80267238) blends those world positions into the steering route, and `+0xa8` overrides
  `item_target` as "what am I acting on" when set.

`Rider_CPUUpdateNavTarget` (0x8026b6d0) is the nearest-node query that assigns
`target_primary` (+0x38) + its arc (+0x3c); `target_secondary` (+0x44) is a transient
promotion candidate, cleared after use.

### Command opcodes

The interpreter decodes this opcode set (operands are subsequent bytes in the
stream). `0x100` is the single button bit the AI toggles (the press/charge input).

| Opcode | Operands | Effect on virtual pad |
|--------|----------|------------------------|
| 1 (0x01)   | – | `buttons \|= 0x100` (press) |
| 2 (0x02)   | – | `buttons &= ~0x100` (release) |
| 100 (0x64) | – | `buttons = 0` (release all) |
| 127 (0x7f) | – | stop stream (`cmd_timer = 0`, `cmd_read_ptr = 0`) |
| 128 (0x80) | s8 | `stick_x = operand` (absolute) |
| 129 (0x81) | s8 | `stick_y = operand` (absolute) |
| 150 (0x96) | u8 | `buttons \|= 0x100`; `cmd_timer = operand` (press + hold N) |
| 151 (0x97) | u8 | `buttons &= ~0x100`; `cmd_timer = operand` (release + hold N) |
| 180 (0xb4) | u8 | `cmd_timer = operand` (wait N frames) |
| 190 (0xbe) | s8 delta, s8 cap | `stick_x = clamp(stick_x + delta, ±cap)` (relative nudge) |
| 192 (0xc0) | s8 target, s8 step | ramp `stick_x` toward target by ≤step, clamp [-128, 127] |

The stream is built by three appender helpers (assert source `cpcmdscript.c`):
`Rider_CPUPushCmd1` (0x80275fcc, opcode only), `Rider_CPUPushCmd2` (0x80276050,
opcode + 1 operand), `Rider_CPUPushCmd3` (0x80276118, opcode + 2 operands). As s8,
operand `0x7f` = full +, `0x81` = full −, `0x00` = center.

`cmd_timer` is a frame countdown: the interpreter advances only when it hits 0, so
the timer-setting opcodes (150/151/180) mean "hold the current pad state for N
frames." Steering (`stick_x`) has both relative (190) and ramp-to-target (192)
primitives for smoothing; accel/brake (`stick_y`) only has absolute set (129).

The interpreter is a comparison cascade, not a jump table, and its **default arm
asserts** (`cpcmdscript.c`) — so the 11 opcodes above are the **complete** set; any
other byte traps rather than no-ops. (Note there is no opcode 130 or 152: `stick_y`
absolute is 129, and release-and-hold is 151.)

## The Virtual Pad / CpuData (RiderData+0x778)

`RiderData.cpu` (offset 0x778) points to the CPU rider's AI state, allocated only
for CPU riders (null for humans). Its first fields are the synthesized controller
output read back by the three getters:

See `struct CpuData` in `rider.h` for the full confirmed map. The fields that
matter for influencing behavior:

| Offset | Field | Role |
|--------|-------|------|
| +0x00 | buttons | pad out → `held` (0x3d8) via `Rider_GetCPUButtons` |
| +0x04 | stick_x | pad out → `stickX` (0x3ec) via `Rider_GetCPUStickX` |
| +0x06 | stick_y | pad out → `stickY` (0x3ed) via `Rider_GetCPUStickY` |
| +0x08 | ai_state | **strategic** state (1..10; 0 asserts) |
| +0x10 | maneuver | **tactical** state (0..0x15) → command stream |
| +0x1c | desire_flags | per-action-state **INHIBITOR** bits (seeded by `Rider_CPUSeedDesire`) |
| +0x2c | behavior_flags | per-strategic-state **ENABLE** bits (read by the arbitration cascade) |
| +0x2d | status_flags | bit 0x02 velocity-stuck, bit 0x04 position-stuck |
| +0x38 | target_primary | primary nav target id — a **stage-spline-node index** (-1 = none) |
| +0x3c | target_lead | lead/predict time for target resolution |
| +0x44 | target_secondary | secondary nav target id (-1 = none) |
| +0x70 | rival_player_idx | rival pursuit channel (5 = none) → resolves to +0xb8 via Ply_GetPosition |
| +0xa4 | nav_target_ptr | → steering target (track look-ahead point) |
| +0xb8 | nav_target_pos | resolved nav target — the **raw** target (Vec3) |
| +0xc4 | steer_target_pos | **final** steering target after city-object override (Vec3) — what maneuvers steer toward |
| +0x110 | cmd_timer | command VM frame countdown |
| +0x114 | cmd_read_ptr | command VM playback position (0 = idle) |
| +0x118 | cmd_write_ptr | where maneuver handlers append opcodes |
| +0x11c | cmd_buffer[0x80] | command opcode stream |

The getters are trivial struct reads (e.g. `Rider_GetCPUStickX` is
`lwz r3,0x778(r3); lha r0,4(r3); extsb r3,r0`), so the pad layout above is exact.

## Strategic states (`ai_state`, CpuData+0x08)

The 9 decide handlers. Each one: rewrites the per-state `behavior_flags` (+0x2c,
ENABLE bits), computes the nav target, selects target entities, then tail-calls the
arbitration cascade (`Rider_CPUArbitrateManeuver`) to commit the tactical maneuver.
The cascade and the handlers gate decisions on two flag bytes (see
[Behavior & desire flags](#behavior--desire-flags)) and difficulty-scale their
probability rolls via `Rider_CPUDifficultyScale` (0x80276f00) — which maps the skill
level `CpuData+0x22` (0..8) to a `[0,1]` factor (`level/8`). In practice only states
**7 (Charge)** and **8 (Attack)** roll `HSD_Randf` in the handler body; the cruise/
navigate states are deterministic (their maneuver comes from geometry). The dodge / ram /
brake / wiggle probabilities live one layer down, in the cascade itself (see
[The arbitration cascade](#the-arbitration-cascade)). `base_maneuver` (+0x14) is the
maneuver a state parks on (states write 1 or 2); handlers fall back to it.

| State | Addr | Name | Behavior |
|-------|------|------|----------|
| 1 | 0x80271790 | **Cruise** | Follow the racing line; leanest flag set. Steers at whichever is closer: the track look-ahead or `target_primary`. |
| 2,4,>10 | 0x80271b24 | **Cruise+ (default)** | Catch-all cruise; same targeting as 1 but enables the item/attack desire bits. |
| 3 | 0x80271eb4 | **Navigate** | Heavyweight city path-finding; all behaviors enabled. Holds the **position-stuck recovery sweep** (the get-unstuck workhorse): on stuck + unreachable target it sweeps the reversed forward vector for an escape node and clears targets. |
| 5 | 0x802726fc | **RouteFollow** | Follow a precomputed route (3-slot cache) toward a goal; no spline look-ahead. |
| 6 | 0x80272888 | **RouteFollow+City** | Like 5, plus nearest-city-object override and the same anti-stuck escape as state 3. |
| 7 | 0x80272dd0 | **Charge** *(med)* | Drive to a fixed world anchor; on a difficulty-scaled `HSD_Randf` roll when close, commit maneuver 0x15. |
| 8 | 0x802735dc | **Attack** *(high)* | Acquire the nearest in-cone rival (loops players 0..4, distance minus collision radii), then on rolls fire a scripted spin-attack input burst (L/R variant by coin-flip). |
| 9 | 0x80273228 | **Reposition** *(med)* | Branches on `stage_kind` (+0xf) to nudge its own position by stage-specific offsets (sidestep / back off / climb), then resume. Deterministic. |
| 10 | 0x80273b48 | **Patrol** *(low-med)* | Timed ~900-frame toggle of a 2-bit sub-state (`+0x2d` bits 0x18); follows nav target or a rival from the shared `Rider_CPURivalSelect` (see [Target selection](#target-selection--scoring)). Deterministic itself. |

**The probability rolls.** With `scale = level/8` (from `Rider_CPUDifficultyScale`),
the only difficulty-scaled `HSD_Randf` gates in the handlers are:

| Roll | Gate | Threshold |
|------|------|-----------|
| State 7 — commit ChargeStill (0x15) | near anchor (`nav.y < -40` and dist < 60) | `rand < scale` |
| State 8 — acquire attack waypoint | `frame % 300 == 0`, no current waypoint | `rand < 0.75·scale + 0.05` |
| State 8 — fire the spin-attack burst | in-cone rival in `[-0.01, 1.5]` units | `rand < 0.8·scale + 0.1` |
| State 8 — L-vs-R variant of the burst | — | `rand ≤ 0.5` (coin flip, **not** scaled) |

So a level-8 Attack CPU commits the spin-attack on ~90% of eligible frames, a level-0
on ~10%; everything else (cruise/navigate steering, route-following, repositioning) is
fully deterministic on geometry.

### How the profile is chosen (the answer to "who sets `ai_state`")

No handler ever writes `ai_state`. It is set **once**, in `Rider_CPUInit`
(0x80262d6c — the function that `HSD_MemAlloc`s the 0x19c `CpuData` and registers it
in a max-5 global table), to either an explicit value from the caller or, when the
caller passes 0, the result of **`Rider_CPUSelectProfile` (0x802766fc)**. That
selector is a pure function of context:

```
stage_kind 9    -> 3  (Navigate)        stage_kind 0xf  -> 7  (Charge)
stage_kind 0x11 -> 5  (RouteFollow)     stage_kind 0x12 -> 6  (RouteFollow+City)
else if rider.ply == 4 -> 10 (Patrol)
else look up Gm_GetCityKind() in the table at 0x804b7f78, default 2 (Cruise+):
```

| city_kind | → state | city_kind | → state |
|-----------|---------|-----------|---------|
| 0,1,2,3 | 2 Cruise+ | 9 | 7 Charge |
| 5 | 3 Navigate | 0xe, 0x12 | **8 Attack** |
| 7 | 4 Cruise+ | 0x8, 0xb | 9 Reposition |
| 0xf, 0x10 | 2 Cruise+ | 0xd, 0x13 | 5 RouteFollow |

So the strategic state is a **per-stage / per-stadium AI personality**: combat
stadiums (city 0xe / 0x12) get Attack, collection ones get RouteFollow, etc. The
two init entry points are `Rider_CPUInitPlayer` (0x80275c04, passes 0 → computed
profile, from match setup) and `Rider_CPUInitPlayerFixed` (0x80275c40, forces
state 1 / difficulty 8, from `Game_Think`). **There is no dynamic strategic FSM —
the state graph is a one-shot lookup, not a set of transitions.**

## Behavior & desire flags

Two flag bytes drive the arbitration cascade, with **opposite polarity**. Together
they are the cleanest lever for retuning vanilla behavior without replacing the pad.

**`behavior_flags` (+0x2c) — ENABLE bits, rewritten wholesale by each strategic
state.** A set bit turns a capability on, so the per-state value *is* the personality.
Read by `Rider_CPUArbitrateManeuver` and the hazard/lookahead collectors:

| Bit | Enables | States that set it |
|-----|---------|--------------------|
| 0x01 | opportunistic-action maneuvers (commit ChargeHold / RamCharge / PursueLOS) | all |
| 0x02 | the extra (5th) hazard-detector pass in `Rider_CPUCollectHazards` | 2/4, 3, 5, 8, 10 |
| 0x08 | the target-steer maneuvers 0xb / 0xc | 3, 8 only |
| 0x20 | predictive target leading in `Rider_CPUBuildRoute` | 3, 6 only |
| 0x40 | rival pursuit / intercept setup | 1, 2/4, 3, 6, 7, 9 |
| 0x80 | forward-collision lookahead (`Rider_CPUForwardLookahead`) | 1, 2/4, 3, 6, 7, 9 |

Bits 0x04 and 0x10 are written but have no consumer (vestigial). The full per-state
write values: 1→`0xc1`, 2/4/>10→`0xd7`, 3→`0xff`, 5→`0x17`, 6→`0xf7`, 7→`0xd5`,
8→`0x1f`, 9→`0xd5`, 10→`0x17`.

**`desire_flags` (+0x1c) — INHIBITOR bits, seeded from the rider's *action state*
(animation / spin-out / knockback) by `Rider_CPUSeedDesire` (0x802762dc).** A set bit
*suppresses* a reaction, muting the AI while it is in a state where acting would be
wrong. State 1 (Cruise) additionally ORs `0xf00` to maximally mute combat:

| Bit | Suppresses | Tested in |
|-----|------------|-----------|
| 0x100 | the ram-charge attack press | maneuver 3 (RamCharge) |
| 0x200 | obstacle-avoidance steering selection | the arbitration cascade |
| 0x400 | reactive dodge / attack-scan / wiggle | state 8, maneuver 0xb, cascade |
| 0x1000000 | charge-up and rival-intercept setup | maneuver 0xd, cascade |

Bits 0x001/0x010/0x020/0x800 are seeded but have no consumer (vestigial; 0x800 only
ever appears via state 1's `0xf00` OR, never in the tables).

#### Desire-flag seed tables (the full enumeration)

The seeder is a flat array lookup, no search. Index by `RiderData.state_idx` (+0x1c),
the rider's current action/motion-state id (0x00..0x82). Each entry is
`{u32 id; u32 flags}` (the `id` just re-states the row index); the seeder ORs `flags`
into `desire_flags`. Selection:

- `state_idx < RiderData+0x20` (which holds **29**) → **Table 1** at `0x804b7b18`
  (29 entries, ids 0x00..0x1c) — applies to **all rider kinds**.
- otherwise → **Table 2** at `0x804b7c00` (102 entries, ids 0x1d..0x82), indexed by
  `state_idx - 29`, but **only when `RiderData.kind` (+0x04) == 0** (Kirby). A
  non-Kirby rider in a state ≥ 29 gets *no* seed (flags unchanged).

The raw `flags` words carry vestigial bits (`0x001/0x010/0x020`) that decorate some
rows without changing behavior, so by *active* inhibitor the values collapse to four
classes: `0x000` = nothing suppressed (full reactivity); `0x600`/`0x631` = no-avoid +
no-dodge (ram-charge still allowed); `0x700`/`0x730`/`0x731` = +no-ram (the three
reactive moves all muted); `0x1000600`/`0x1000700`/`0x1000731` = also no-charge/intercept.

**Table 1** (`0x804b7b18`, ids 0x00..0x1c, all kinds):

| State ids | Raw flags | Active inhibitors |
|-----------|-----------|-------------------|
| 0x00–0x02 | 0x0631 | no-avoid, no-dodge |
| 0x03–0x05 | 0x0730 | no-ram, no-avoid, no-dodge |
| 0x06–0x19 | 0x0731 | no-ram, no-avoid, no-dodge |
| 0x1a–0x1c | 0x0000 | none (fully reactive) |

**Table 2** (`0x804b7c00`, ids 0x1d..0x82, Kirby only):

| State ids | Raw flags | Active inhibitors |
|-----------|-----------|-------------------|
| 0x1d | 0x0631 | no-avoid, no-dodge |
| 0x1e–0x20 | 0x0700 | no-ram, no-avoid, no-dodge |
| 0x21–0x27 | 0x0000 | none (fully reactive) |
| 0x28–0x2b | 0x0700 | no-ram, no-avoid, no-dodge |
| 0x2c–0x2e | 0x0600 | no-avoid, no-dodge |
| 0x2f–0x34 | 0x0700 | no-ram, no-avoid, no-dodge |
| 0x35–0x42 | 0x1000700 | all four |
| 0x43–0x4f | 0x1000600 | no-avoid, no-dodge, no-charge (ram allowed) |
| 0x50–0x59 | 0x1000700 | all four |
| 0x5a–0x60 | 0x1000600 | no-avoid, no-dodge, no-charge |
| 0x61–0x65 | 0x1000700 | all four |
| 0x66–0x67 | 0x1000600 | no-avoid, no-dodge, no-charge |
| 0x68–0x6a | 0x1000700 | all four |
| 0x6b–0x6e | 0x1000731 | all four |
| 0x6f–0x81 | 0x0700 | no-ram, no-avoid, no-dodge |
| 0x82 | 0x0000 | none (fully reactive) |

The shape is legible: the `0x1000000` (no-charge/intercept) bit lights up only across
the contiguous block **0x35..0x6e** — the damage / knockback / spin / stun action
states, where the AI must not start a charge or a chase. The all-zero rows
(0x1a–0x1c, 0x21–0x27, 0x82) are the free/idle states where it may do anything. So the
tables only ever *add* inhibitors for the current animation; nothing here grants a
capability. (Consequently, a preset that wants an always-aggressive CPU can simply
clear `desire_flags` after the decide stage — it is re-seeded from scratch each frame.)

**The seam:** the strategic *profile* picks `behavior_flags` (what the CPU is allowed
to want), the *action state* picks `desire_flags` (what it must not do right now), and
`Rider_CPUArbitrateManeuver` (0x80274ec0) walks a fixed priority list of candidate
maneuvers, committing the first whose enable bit is set and whose inhibitor is clear —
falling through to `base_maneuver`. (`RiderData.copy_kind` +0x454 == 9 bypasses the
`0x400` no-dodge inhibitor for the threat sub-scan — an action-state gate, **not** a
"stadium" flag, as an earlier reading had it.) The full ordered priority list is in
[The arbitration cascade](#the-arbitration-cascade) below.

## The arbitration cascade

`Rider_CPUArbitrateManeuver` (0x80274ec0, 0xd24 bytes — the largest function in the
system) is the **sole writer of `maneuver` (+0x10)**; the decide handlers tail-call it
every frame. It is a fixed, top-to-bottom **priority cascade**: it walks candidate
maneuvers in order and commits the first whose guard passes, then returns. Most arms
also early-out (`if maneuver == X return`) so an in-progress maneuver runs to completion
rather than re-committing. All distance checks use `RiderData.pos` (+0x300); the
machine-state predicates read through `RiderData.machine_gobj` (+0x3f4) → MachineData
flag bytes (+0xc32/+0xc33/+0xc35).

Priority order (first match wins; guards [verified-disasm]):

| Pri | Commits | Guard |
|-----|---------|-------|
| 0 | **0xf** NavSteer+Tap | current == 0xe AND (pos-stuck `0x04` OR hazard count > 0) |
| 1 | **0** Coast | MachineData+0xc33 bit 0x08 (hard machine reset) |
| — | *(let-finish)* | current ∈ {0xe,0xf,0x10} → return |
| 2 | **0xe** NavSteer | `machine_gobj == NULL` |
| 3 | **8** AvoidObstacle | MachineData+0xc33 bit 0x80 OR +0xc32 bit 0x01 (machine hazard state; **no flag gate**) |
| — | *(skip)* | `machine_gobj == NULL` → jump to fallback |
| 4 | **0x12** TapOnce | attack/target-scan picks a scored target (RNG, ai_state-bucketed) |
| 5 | **0x13** Wiggle | threat sub-scan hit + roll (gated by desire `0x400`, bypassed by copy_kind==9) |
| 6 | **9** DodgeProjectile | MachineData+0xc33 bit 0x02 (being-hit) |
| 7 | **0xd** ChargeCentered | ENABLE `0x40` + desire `0x1000000` clear + rival found + roll < 0.05·scale (or a 1/100 short-circuit) |
| 8 | **0x14** Brake | ai_state ∈ {2,4,7,9} + difficulty > 3 + machine brakeable + roll < scale |
| 9 | **7** ChargeHold | ENABLE `0x01` + machine chargeable + per-machine dist/cone gate (`Vec2_Dist(pos, item_target_pos) < dist` AND charge < cone) |
| — | *(city gate)* | if NOT `CityTrial_IsInCity()` → skip 10–12 |
| 10 | **6** RouteFollow+City | in-city + `path_point` (+0xac) set + HUD-progress ratio < 0.9 |
| 11 | **0xb** SteerTarget/Wiggle | ENABLE `0x08` + target (`interaction_target`/`item_target`) within 1600 units |
| 12 | **0xc** SteerTarget/Advance | ENABLE `0x08` + `item_target` set + (within 1600 OR nearest spline node == `target_primary`) |
| 13 | **3** RamCharge | ENABLE `0x01` + intercept candidates > 0 + roll < 0.2·scale+0.01 → nearest "side 1" rival, pos copied to +0xd0 |
| 14 | **4** PursueLOS | as 13 but "side 0" rival |
| 15 | **7** ChargeHold | ENABLE `0x01` + machine chargeable + charge-level gate (geometry, no RNG) |
| 16 | **= base_maneuver** (+0x14) | unconditional fallback |

**The cascade rolls its own RNG.** The "only states 7 & 8 roll `HSD_Randf`" rule holds
for the *decide handlers*, but the **arbiter itself** rolls — all difficulty-scaled
(`scale = level/8`) — for maneuvers **3 / 4** (0.2·scale+0.01), **0xd** (0.05·scale),
**0x12 / 0x13** (0.15–0.3·scale, plus an `HSD_Randi` target-score lottery), and **0x14**
(scale). So a CPU's moment-to-moment aggression — when it dodges, rams, brakes, wiggles
— is decided **here**, not in the strategic handlers.

Sites 1/2/3/6 are pure **machine-state** gates (no behavior/desire flag) and take
priority over the flag-gated maneuvers: a machine in a hit/hazard animation overrides
the AI's intent. `maneuver 0x15 (ChargeStill)` is **never** committed by the cascade —
only state 7's decide handler sets it.

## Tactical maneuvers (`maneuver`, CpuData+0x10)

The ~20 process handlers that emit command streams. *Which* maneuver runs is chosen
upstream by the arbitration cascade (`Rider_CPUArbitrateManeuver`); these handlers
just play it out. The waypoint geometry the cruise/navigate maneuvers steer along is
built by `Rider_CPUBuildRoute` (0x8026a734) — a spline look-ahead sampler that, given
the rider's speed, walks the course path-graph up to 3 hops ahead, lays ≤4 lane-
midpoint waypoints (with per-corner offsets and banking normals) into the scratch
buffer at `0x8055e964`, and returns 0 when the route is invalid (missing link, a
corner too sharp for the lane, or a degenerate segment) so the handler can fall back
to anti-stuck recovery. Steering opcodes mostly come from the shared executor
`Rider_CPUEmitSteer` (0x8026d6a0 → 0x8026c4ec: emits 190 nudge / 192 ramp for
`stick_x`, 129 for `stick_y`); `"+0x10 = base_maneuver"` or `"= 10"` is how a maneuver
hands control onward.

| Man | Addr | Name | Pad pattern |
|-----|------|------|-------------|
| 0 | 0x8026c0a0 | **Coast** | Ramp stick toward heading, `stick_y = 0`, no button. |
| 1 | 0x8026dd4c | **RecoverForward** | Steer to arc point; charge-tap stutter (helper 0x8026da40) when velocity-stuck; bail to maneuver 10 if forward speed lost. |
| 2 | 0x8026de68 | **SteerToNav** | Steer to `nav_target_pos`; same stutter / bail. |
| 3 | 0x8026e448 | **RamCharge** | Intercept a rival via angle-cone gates; brief timed press (hold 4) on contact/charge; else steer. |
| 4 | 0x8026e97c | **PursueLOS** | Steer to rival while it stays in the cone; never presses. |
| 5,6 | 0x8026ee7c | **ApproachWaypoint** | Far → delegate to maneuver 1; close + aligned → press+hold 10 on arrival. |
| 7 | 0x8026ed4c | **ChargeHold** | Steer, hold the charge button, release when charge tops off. |
| 8 | 0x8026fbe0 | **AvoidObstacle** | 3-point scan over the **route-waypoint buffer** (0x8055e964), snapping to a clear waypoint then steering to `nav_target_pos`; terrain-shaped accel (helper 0x8026f0e4); no button. Gated on velocity-stuck. |
| 9 | 0x8026fe7c | **DodgeProjectile** | Reads the **machine's own incoming-projectile state** (not the hazard list); on an intercept-course projectile within ~4 units' closing time, RNG-gated evasive burst (set stick + press/hold 60 / release 20). |
| 10 | 0x8026f00c | **ChargeRelease** | Press/hold charge toward nav target until full, then return to `base_maneuver`. |
| 0xb | 0x80270154 | **SteerTarget/Wiggle** | Steer at target; on a difficulty-scaled roll, a stick-wiggle; bail to 10 if misaligned. |
| 0xc | 0x802704c4 | **SteerTarget/Advance** | Aligned → steer target; else press + steer toward nav. |
| 0xd | 0x80270758 | **ChargeCentered** | Press + center stick_x + full stick_y (charge-up); releases on a flag. |
| 0xe | 0x80270bac | **NavSteer** | Proportional `stick_x`/`stick_y` computed from nav heading; no button. |
| 0xf | 0x80270e64 | **NavSteer+Tap** | NavSteer plus a charge tap on even frames; aborts to maneuver 0x10. |
| 0x10 | 0x802711d8 | **ChargedNavSteer** | NavSteer plus a stall-recovery charge press (low speed > 10 ticks). |
| 0x12 | 0x802715a8 | **TapOnce** | Single press, then return. |
| 0x13 | 0x802715e0 | **Wiggle** | RNG-direction stick shake (128 ±127 / wait), then return. |
| 0x14 | 0x802707c4 | **Brake** | Press + centered stick, gated by distance to target. |
| 0x15 | 0x80270850 | **ChargeStill** | Press + centered stick, unconditional. |

Clusters: charge/hold (7, 0xd, 0x10, 0x14, 0x15), nav-steer (0xe/0xf/0x10),
wiggle-to-unstuck (0xb, 0x13), pursue/attack (3, 4), waypoint (2, 5/6). Only 0x9,
0xb, 0x13 use `HSD_Randf`; the rest are deterministic on geometry/flags/timers.

Dispatch is the 22-entry jump table at **0x804b79b8** (`maneuver` in 0..0x15; any
out-of-range value falls to **Coast**). The table is fully enumerated above with two
quirks: indices **5 and 6 share** the ApproachWaypoint handler, and index **0x11 is an
unused slot** — its table entry points straight at Coast (`0x8026c0a0`), the same as
the out-of-range default, and no decide/cascade path ever sets it. So 0x11 is the one
"missing" maneuver and it is missing on purpose (a no-op), not an unmapped handler.

### Steering output (heading → stick)

Every maneuver that steers calls `Rider_CPUEmitSteer` (0x8026d6a0): it projects the
desired heading onto the machine's horizontal plane, bends it away from the nearest
imminent hazard via `Rider_CPUResolveAvoidVector` (0x8026d1fc — rotates the heading
around an axis built from the hazard record's basis + the cached self-velocity basis at
`0x8055e89c`), then hands the result to `Rider_CPUEmitSteerStick` (0x8026c4ec). The stick
emitter computes a signed yaw error and picks the opcode by error band, using a
**per-difficulty `(step, cap)` envelope** (`cpu_steer_envelope_table` 0x804b7f54, indexed
by `difficulty_level` +0x22):

- error < ~5° deadzone → **opcode 192** (ramp toward center / setpoint),
- error in the ramp band → **opcode 192** with a proportional setpoint `lerp(step..cap)`,
- error ≥ ramp bound → **opcode 190** (hard relative nudge ±step, capped ±cap).

`stick_y` (accel/brake) is always **opcode 129** (absolute): base accel by machine kind,
brake when over-speed, with per-stage terrain overrides (gr_kind 0x12/0x14 brake at a map
edge). Higher difficulty widens the envelope → faster, harder turns.

Then the post-maneuver `Rider_CPUEmitAbilityAction` (0x80273d1c) dispatches on
`RiderData.kind` + `copy_kind` (+0x454) to time a **copy-ability press** (e.g. Plasma /
Freeze fire when a hazard-list threat is imminent and a roll passes; per-ability hold
length from `cpu_ability_press_hold_table` 0x804b7f30), and `Rider_CPUTerminateCmdStream`
(0x80276228) caps the per-maneuver stream with opcode 0x7f. While velocity-stuck, the
**charge-pump** `Rider_CPUEmitChargeStutter` (0x8026da40) cycles press → hold-20 → hold-40
to break free. Whether the rider can make a given turn at all is tracked by
`Rider_CPUTrackStuckProgress` (0x8026ccec) against the machine's max-turn tolerance
(`cpu_machine_turn_table` 0x804b8f30).

## This brain is self-contained C — Top Ride is separate

The pipeline above is plain C and self-contained. Its decide handlers call only
math / utility helpers — never any C++ class; the only non-`Rider_*` calls are:

- `PSVECMagnitude` (0x803d2158), `Vec2_Dist` (0x803d22cc) — perception distances.
- `splArcLengthPoint` (0x80415958) — spline arc-length, i.e. track position.
- `HSD_Randf` (0x8041e610) — the RNG that drives the personality probability rolls.
- `_savegpr_*` (e.g. 0x803adb48) — compiler register-save prologue helpers.

### Top Ride's CPU AI (source: `a2d_cpu_kirby.cpp`)

Top Ride is genuinely a **C++ mode** (real RTTI classes `Kirby`, `ChickMgr`,
`ChickMgr::Chick`, plus the Kirby state machine — see `topride-system.md` and
`topride-kirby-states.md`), operating on `TopRideKirby`, not `RiderData`. Its CPU
AI shares **no code** with the CT/AR brain above.

**Input is polymorphic — that is the human/CPU seam.** Every kirby holds an
`input_reader` at `TopRideKirby+0x48`. The reader's *class* decides where input
comes from:

| Slot | input_reader vtable | `vt[0x14]` poll | `vt[0x0c]` get-command |
|------|---------------------|-----------------|------------------------|
| Human | 0x804d25e0 | `TopRide_PadInputPoll` (0x802a06bc) | `TopRide_PadInputRead` (0x802d4c4c) — reads the GC pad via AutoRepeat + `stc_engine_pads` |
| **CPU** | **0x804d8710** | `TopRide_CpuInputPoll` (0x80291dec) | **`TopRide_CpuInputThink` (0x802eee90) — the steering brain** |

`TopRide_KirbyPhysUpdate` (0x802d5ec0) polls the reader each frame via `vt[0x14]`
**when its mode arg (`r4`) is non-zero** (`r4 == 2` is the controlled/charging update;
`r4 == 1` reads input only; `r4 == 0` skips it). The poll calls `vt[0x0c]` to get a
steer float + a press/charge byte, then packs them into the kirby's history ring
(`TopRide_PackStickInput` 0x80311db0 / `TopRide_UnpackStickInput` 0x80311ea0).
`+0x48` is the normal-play input seam for *both* humans and CPUs; only the reader's
vtable differs.
(The `game_config+0x38 == 3` demo branch in `TopRide_SpawnKirby` swaps in a *different*
reader sourced from the table at `DAT_8048a250`; normal play uses the per-slot config
byte at `game_config + slot*9 + 0x60` to pick pad-vs-CPU.)

**The brain — `TopRide_CpuInputThink` (0x802eee90).** Signature
`char think(TopRideCpuInputReader *reader, float *steer_out)`; CPU-gated by
construction. Each frame it:

1. Refreshes its **difficulty** `reader->difficulty` (+0x1c, 0..4) from the per-slot
   level scratch `DAT_804d8040[slot]` (`slot` via `reader->kirby->vt[0x30]` =
   `TopRide_KirbyGetSlot`), and a reaction budget from `DAT_804d7f90[+0x18]`.
2. Runs a **perception scan** (`TopRide_CpuPerceive` 0x802eb094) that fills a stack
   *blackboard* (sector look-ahead via a binary-searched, LOS-tested racing-line ring;
   nearest rival / item / obstacle; reaction-budget-indexed aggression weights), then four **situation
   detectors** and — if none commit — one of two **route-followers** (see the taxonomy
   below). `local_a4` is the CPU's *own current Kirby state ID* (read via the kirby's
   `vt[0x28]`), stored to `reader->prev_state` (+0x68); the brain's `switch` keys on it
   to know whether the CPU is even *able* to act (stunned states just twitch).
3. Computes `*steer_out` (lateral stick) from the chosen maneuver, damped against a
   9-slot history ring of recent steer deltas (+0x34..+0x54) to kill oscillation, with
   `HSD_Randf` jitter (`reader->steer_noise` +0x20) reseeded on timers (+0x6c).
4. Returns the **press/charge byte** (the second output); when `reader->debug_draw`
   (+0x30) is set it also writes a heading arrow into `EnemyMgr+0x3da0` (a debug viz).

**Difficulty comes from the CSS skill control — which is the `+3` ("handicap") byte,
not the `+2` ("cpu_level") byte.** The Top Ride lobby shows one per-slot skill control,
labelled **"Handicap" for human slots and "CPU Level" for CPU slots** — it is a single
field. `TopRide_PreGameThink` commits it from panel field `+0x2b` via
`TopRide_SetHandicap` into `game_config[slot*9 + 0x5b]` (= `GameData[slot*9 + 0xD23]`).
`reader->difficulty` (+0x1c) is seeded from exactly that byte at construction
(`TopRide_CpuInputReaderInit` 0x802eed00, asserts `<= 4`). The CSS value 1..5 maps to
internal **0..4**.

The other byte — `game_config+0x5a` / `GameData[slot*9+0xD22]`, `TopRide_SetCpuLevel`,
committed from panel field `+0x27` — stays **0** and is **not read by the steering
brain** (apparently vestigial; no CSS control writes it in normal play). Its
`TopRide_SetCpuLevel` name is misleading: it is *not* what the CSS "CPU Level" control
sets.

Verified live two ways: (a) default race — all CPUs `+3 = 2`, every `reader->difficulty`
and `DAT_804d8040[1..3]` read back 2; (b) CSS CPU Levels set to 3/4/5 — the three CPU
slots' `+3` bytes read 3/4/2 and their `reader->difficulty` read **3/4/2 to match**,
while `+2` stayed 0 throughout. The level indexes the per-level tuning tables — steer
gain `DAT_804d80d0`, reaction frames `DAT_804d8058` (60→0 as level 0→4), commit
thresholds `DAT_804d80bc` — so higher CPU Level = tighter, faster, more-committed
steering.

#### Top Ride detectors & route-followers

`TopRide_CpuPerceive` writes a *blackboard* (a stack scratch buffer the brain passes
to every helper) holding: self position/facing/velocity, a look-ahead distance, the
current/next track **sector** (found by binary-searching the `Route` sector ring and
LOS-raycasting each candidate via `EnemyMgr`), the vector to it, the heading error,
and four aggression weights (`blackboard[0x2a..0x2d]`, indexed by `reaction_budget`
(+0x14) — **not** difficulty). The four detectors then run **in order**;
the **first to commit short-circuits the rest** and suppresses the route-followers.
Each detector writes `reader->heading` (+0x08) and may raise the press/charge flag;
the commit routine `TopRide_CpuCommitSwerve` (0x802eec54) reseeds the steering jitter
and sets a 5-frame hold.

| Order | Detector | Surveys | Reaction |
|-------|----------|---------|----------|
| 1 | `TopRide_CpuDetectHazard` (0x802ed434) | projectiles (grenade / missile / mine / ember / smoke) | time-to-impact test → injects an evasive swerve (only if not invincible + able to act) |
| 2 | `TopRide_CpuDetectItem` (0x802ecc54) | `ItemMgr` | classify via `TopRide_ClassifyItem` (0x802ecb7c): a switch on item kind (`Item+0x60`) → +good / 0 neutral / −bad (kind **3** is the only BAD; 1/6/16/20 are strong-good). Steer toward good within (1,20) range / swerve away from bad (only at difficulty 4 or high budget), gated by a level-scaled aggression roll |
| 3 | `TopRide_CpuDetectRival` (0x802eda78) | rival kirbys | dispatch on own + rival Kirby state → block (intercept) or ram (chase); starts a ~30-frame block window |
| 4 | `TopRide_CpuDetectObstacle` (0x802ee210) | `CpuObstacleMgr` (walls / edges / air-currents) | by obstacle kind: avoid walls, or steer *into* a beneficial air-current and **press charge** |

If none commit, a route-follower steers the racing line. Which one is selected by
`reader->reaction_budget` (+0x14, *not* difficulty — a **float** from the table at
0x804d7f90, min 1.0): **`TopRide_CpuRouteFollowLagged` (0x802ec890)** is the normal-play
path (aims at the chosen sector, boosts on clear straights); **`TopRide_CpuRouteFollowFull`
(0x802ebb98)** is the "perfect line" path but is **dead in normal play** — selected only
when the budget is 0, which the shipped table never produces. Its apex line is read from a
**per-sector apex grid rebuilt every frame inside the `EnemyMgr` singleton at `+0x30dc`**
(a 26×10 grid of 12-byte cells `{valid, hint, heading-angle, arc-target}`), **not** a
static asset — so there is no donor table to patch; influencing it means hooking the
EnemyMgr update.

The brain's final `switch (local_a4)` keys on the CPU's **own Kirby state**: stunned
states (PRESS / CRUSH / EXPLODE / SPIN / NUMB / BURN / FREEZE) → a tiny randomized
flail with charge forced **off**; GRIND → hold the rail with charge **on**; buff/
debuff states → clear the hold and steer normally; **default (Normal)** → convert
`reader->heading` into `*steer_out` via the facing cross-product, clamp to ±0.98, and
oscillation-damp against the 9-slot steer-delta history.

**Difficulty** (`reader->difficulty` +0x1c, 0..4) indexes the behavioral tables (exact
values, levels 0→4): steer gain `DAT_804d80d0` `[80,65,50,0,−70]` (higher turns harder,
then over-rotates; note **L3 = 0**), commit-hold frames `DAT_804d8058` `[60,45,30,10,0]`
(faster re-decide), commit threshold `DAT_804d80bc` `[0,20,50,70,100]`, plus per-detector
aggression gates: item `DAT_804d80a8` `[0,20,40,70,100]`, rival `DAT_804d8094`
`[0,8,15,40,100]`, ram `DAT_804d806c` `[0,10,20,40,100]`. So higher CPU Level = sharper
turns, faster reactions, and more frequent item-grabs / rams / boosts.

**Resolved:** the `cpu_level` (`+2`) byte is inert (stays 0, no CSS control drives it,
the brain never reads it); the live skill knob is the `+3`/handicap byte. The item
classifier (`TopRide_ClassifyItem`) and the apex grid (rebuilt each frame in
`EnemyMgr+0x30dc`, not a static table) are now mapped. The only residual unknowns are
the exact `stfsx` site that writes the apex grid (computed addressing, in the EnemyMgr
update subtree) and the meaning of two per-cell hint bytes — neither blocks influencing
the AI.

## CPU Passive Stat Growth (City Trial)

Separate from the steering brain above: in City Trial, every CPU rider is handed a
finite pool of machine stats that drips onto its machine over the round, independent
of whether it ever picks up a patch. It is a flat, level-scaled head start — **not** a
leader-gap rubber-band (that is a different system; see [below](#not-to-be-confused-with)).

**Consumer — `CityTrial_GrowCpuStats` (0x80015a00).** Called every frame from
`Game_Think` (call site 0x80011f48). It fires on an interval and, for each CPU slot
with budget left, cashes a random slice of that budget into random stats. The tables
live in `gmGameParams` (`gmDataAll->game_params`, the first pointer in `gmDataAll`):

```c
gmGameParams *gp = gmDataAll->game_params;
if (Gm_GetCityTrialFrame() % gp->ct_cpu_stat_interval == 0)   // tick: every 180 CT frames (~3 s)
  for (slot = 0; slot < 5; slot++)
    if (plGetPlayerKind(slot) == 1                            // CPU only (PKIND_CPU)
        && GameData.city.cpu_stat_budget[slot] > 0.0f              // budget remaining
        && Ply_GetCpuLevel(slot) != -1) {
      Ply_GetStatAux(slot, buf);                              // 9 stat_aux entries (PlayerData+0x68)
      drain = (budget >= 1.0f) ? gp->ct_cpu_stat_rate[cpu_level] * HSD_Randf()
                               : budget;                      // amount to spend this tick
      for (d = drain; d >= 1.0f; d -= chunk) {                // sprinkle in sub-1.0 chunks
        chunk = HSD_Randf();                                  // 0..1
        buf[HSD_Randi(9)] += chunk;                           // random stat += chunk
      }
      if (d >= 0.0f) buf[HSD_Randi(9)] += d;                  // remainder onto one more stat
      GameData.city.cpu_stat_budget[slot] -= drain;                // spend the budget
      Ply_SetStatAux(slot, buf);                              // commit (pushes to machine if riding)
    }
```

The growth accumulates in the slot's `stat_aux` block (`PlayerData+0x68`), not the
named stats union — `Ply_SetStatAux` is what then clamps it into the machine's
added-patch array and recombines attributes (see [below](#how-it-reaches-the-machine)).
It is spread *randomly* across all nine stats (weight, boost, top speed, turn, charge,
glide, offense, defense, HP) — `HSD_Randi(9)` picks the stat, `HSD_Randf()` the increment
— so a CPU's stat profile drifts unevenly, the way a human's does from random patch
pickups, rather than rising in lockstep.

**Producer — seeded once at scene load by `SceneLoad_3D` (0x8001442c).** The pool is
not accrued during play; it is set at City Trial load as a flat function of `cpu_level`
and from then on only spent down:

```c
gmGameParams *gp = gmDataAll->game_params;
if (Scene_GetCurrentMajor() == 6 && Gm_GetCityData()[5] == 0)   // City Trial major, base mode
  for (slot = 0; slot < 5; slot++)
    GameData.city.cpu_stat_budget[slot] = (plGetPlayerKind(slot) == 1)
        ? gp->ct_cpu_stat_seed[cpu_level]   // CPU: difficulty-scaled seed (reads ply_desc[slot].cpu_level_eff)
        : 0.0f;                             // human: no growth
```

A higher-level CPU starts with a bigger pool **and** drains it faster (the rate table
scales too), so it ramps its machine up quickly over the first minute or so, then
plateaus once the pool empties. Humans get zero — this is purely a CPU handicap.

### Difficulty tables (`gmGameParams`)

These three tables are members of `gmGameParams` (the struct `gmDataAll->game_params`
points at), indexed by `cpu_level` 0..8. Both stat tables are **floats**.

| `cpu_level` | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|-------------|---|---|---|---|---|---|---|---|---|
| seed pool (`ct_cpu_stat_seed`, `+0x154`) | 3 | 5 | 10 | 15 | 20 | 25 | 30 | 35 | 42 |
| per-tick drain rate (`ct_cpu_stat_rate`, `+0x178`) | 1.0 | 1.0 | 1.0 | 1.0 | 1.2 | 1.5 | 2.0 | 2.5 | 3.0 |

Tick interval `ct_cpu_stat_interval` (`+0x19c`) = **180** frames (≈3 s at 60 fps). These
are the shipped defaults.

### How it reaches the machine

`Ply_SetStatAux` (0x8022d128) writes the PlayerData `stat_aux` block (+0x68) and, while
the rider is on a machine, pushes the same values into the machine: `cityTrial_setMasterStats`
(0x801c8258) → `Machine_SetStatBlockClamped` (0x80194f64, clamps each stat to
`Patch_GetMinValue`/`Patch_GetMaxValue`) into the machine's added-patch block
(`MachineData+0x9e8`) → `Machine_AdjustAttributes` recombines it into the **master stat
block `MachineData+0x94c`**. That master block is what `cityTrial_getMasterStats`
(0x801c81c0) copies back onto the rider every frame while riding. This is the **same
pipeline patch pickups use**, so the growth is indistinguishable downstream from
collected patches — effectively "free patch points over time."

### Not to be confused with

- **VS King Dedede catch-up** (`vsDedede_CPU_Cheating` 0x80040b80, `zz_8004078c_`
  0x8004078c) — boss-fight-only, operates on slot 4 (Dedede) against the field's average
  HP. A different mechanism with its own GameData scratch (`+0x604`..`+0x630`).
- **Finish-rank rubber-band** (`zz_800155d4_`) — a true gap-based handicap that
  interpolates stats by `Gm_GetPlayerFinishRank` and cpu_level and writes them directly
  via `Ply_SetAllStats`. It does **not** touch the `+0x46c` budget; it is independent of
  the growth pool above.

## Influencing CPU Behavior

Two strategies, in increasing order of leverage:

### 1. Tweak — adjust vanilla AI

- **`cpu_level`** is the coarse vanilla difficulty knob, set per player. Its range
  is **0..8 (9 steps) in City Trial / Air Ride** (`ply_cpu_level`, GameData+0x22d)
  and **0..4 (5 steps) in Top Ride** (`TopRide_SetCpuLevel`, 0x8000be74). Raising it
  shifts the desire rolls toward more skilled, aggressive play without changing
  *what* the AI prioritizes. The brain reads its own difficulty from `CpuData+0x22`
  (0..8) through `Rider_CPUDifficultyScale` (0x80276f00) — every personality roll is
  `HSD_Randf() < const * scale + const` — and CT/AR `cpu_level` maps to it **1:1**,
  so writing `+0x22` directly is the finest-grained difficulty lever. The CSS
  difficulty selector is a fixed-segment bar (9 / 5 notches), so widening the
  *range* needs new menu art, but tuning behavior at the existing levels does not —
  see `css-system.md` § CPU Level / Handicap Bar Widget.
- **Per-state probability rolls** — only states **7 (Charge)** and **8 (Attack)**
  roll `HSD_Randf` in the handler body; the thresholds are mapped (see
  [Strategic states](#strategic-states-ai_state-cpudata0x08), the "probability rolls"
  table). Editing those constants nudges how often a Charge/Attack CPU commits its
  signature move. Everything else (cruise/navigate steering, route-following) is
  deterministic, so there are no other roll-knobs to find. (Top Ride is a *separate*
  C++ mode and has its own per-level tables — see above.)
- **Re-profile via `ai_state`** — because `ai_state` (+0x08) is a *fixed profile*,
  not a transitioning state, simply writing a new value (e.g. 8 = Attack, 5 =
  RouteFollow) cleanly swaps the CPU's whole personality for the rest of the match —
  the highest-value tweak and exactly how our custom presets could map. Easiest hook
  is right after `Rider_CPUInit`, or override `Rider_CPUSelectProfile`'s return.
- **Flag overrides (`behavior_flags` / `desire_flags`)** — a finer dial than the
  profile: post-write `behavior_flags` (+0x2c) to toggle individual capabilities
  (e.g. set 0x40 to force rival-pursuit, clear 0x80 to disable wall-anticipation), or
  set a `desire_flags` (+0x1c) inhibitor to mute a specific reaction. The arbitration
  cascade re-reads both every frame, so a one-time write is overwritten — re-assert
  after the decide stage. See [Behavior & desire flags](#behavior--desire-flags).
- **Maneuver / target overrides** — forcing `maneuver` (+0x10) pins the tactical layer
  (e.g. 0x15 ChargeStill, 8 AvoidObstacle). For *targets*, do **not** write the
  `+0x38/+0x44` ids — they're stage-spline-node indices an external caller can't
  meaningfully synthesize. To steer the AI at an arbitrary point or entity, write
  `nav_target_pos` (+0xb8) directly each frame (re-assert after the decide stage), or
  just replace the pad (below).
- **Disable / scale passive stat growth** — the City-Trial stat-growth handicap (see
  [CPU Passive Stat Growth](#cpu-passive-stat-growth-city-trial)) is what lets CT CPUs
  build up a machine without earning patches; it is orthogonal to the steering brain.
  No-op `CityTrial_GrowCpuStats` (0x80015a00) to switch it off entirely, scale the
  per-`cpu_level` seed/rate tables (`gmGameParams.ct_cpu_stat_seed` / `ct_cpu_stat_rate`) to flatten or steepen
  it, or zero the per-slot seed at scene load to drop the head start while leaving the
  drain machinery intact. Relevant for a mode that wants CPUs living off real pickups
  only (e.g. a stat randomizer).

### 2. Replace — inject a custom virtual pad

The highest-leverage, lowest-risk path. Because the entire rider/machine pipeline
consumes the virtual pad, custom logic only has to produce stick + button values:

- **Override the pad fields** at `RiderData.cpu` (`+0x00` buttons, `+0x04`
  stick_x, `+0x06` stick_y) after the vanilla brain has run, or
- **Override the rider input fields** directly (`held` 0x3d8, `stickX` 0x3ec,
  `stickY` 0x3ed) after `Rider_InputThink` — bypassing the AI entirely.

Either way a preset computes inputs from game state — e.g. *Aggressive* steers
toward the nearest rival and holds boost; *Hoarder* steers toward the nearest
item; *Cautious* brakes and steers away from threats.

**The single cleanest hook is `_Rider_UpdateCPU` (0x80275c70).** It is a trivial
wrapper whose only body is `bl Rider_UpdateCPU` — and its one caller,
`Rider_CPUThink`, already gates it behind `plGetPlayerKind == PKIND_CPU` and
passes `RiderData*` in `r3`:

```
Rider_CPUThink:
    r31 = gobj->user (RiderData)
    if plGetPlayerKind(r31->player_slot[0x08]) == 1:   // CPU only
        _Rider_UpdateCPU(r31)                          // <-- replace this
```

Replacing `_Rider_UpdateCPU` (trampoline the function, or the `bl` at 0x8018fc80)
with a custom brain gives a CPU-gated entry, `RiderData*` in hand, and full
control of the pad — with **no command-language knowledge required**: just write
`cpu->stick_x` / `stick_y` / `buttons` directly. Options from there:

- **Full replace:** ignore vanilla entirely and write the three pad fields from
  preset logic.
- **Perceive-then-replace:** call `Rider_ProcessCPUDistance` (0x8026bbe0) first to
  reuse the engine's self-state / anti-stuck bookkeeping, then write the pad
  yourself. (It refreshes self-state only; it does not survey other riders.)
- **Overlay:** call vanilla `Rider_UpdateCPU` first, then post-adjust the pad
  (e.g. force boost held, bias steering) — lightest-touch personality tweak.

Still guard on `RiderData.cpu != NULL` defensively (humans have no `CpuData`),
though the `Rider_CPUThink` gate already guarantees it at this hook.

### 3. Top Ride (separate — the `a2d_cpu_kirby.cpp` brain)

The CT/AR pad-injection above does not transfer (no `RiderData`/`CpuData`), but Top
Ride has an exactly analogous seam now that it's mapped:

- **Tweak — difficulty.** Write the per-slot skill byte (the CSS "CPU Level" control)
  `game_config[slot*9 + 0x5b]` / `GameData[slot*9 + 0xD23]` — or `reader->difficulty`
  (+0x1c), or `DAT_804d8040[slot]` — to 0..4 to slide the whole steering profile (gain /
  reaction frames / commit thresholds). Do **not** use the `+2` "cpu_level" byte; it's
  inert. Global retuning of all CPUs is also possible by patching the per-level tables
  (`DAT_804d80d0` / `DAT_804d8058` / `DAT_804d80bc`).
- **Replace — the brain.** The cleanest hook is **`TopRide_CpuInputThink`
  (0x802eee90)** itself: CPU-only by construction, `TopRideCpuInputReader*` in `r3`,
  the steer-out pointer in `r4`, and the kirby reachable via `reader->kirby` (+0x04).
  Overlay (call vanilla, then bias `*steer_out` / the returned charge byte) or full
  replace (compute steering from `reader->kirby->charge.position` / rivals and write
  `*steer_out` + return the charge byte) both work — the same three options as CT/AR,
  just on the kirby's 2-axis steer instead of a virtual pad.

This makes the `cpu_ai_preset_tr` menu selection actionable: a Top Ride hook can map
each preset to a handicap level (tweak) and/or a `TopRide_CpuInputThink` overlay.

## Key Functions

| Function | Address | Size | Purpose |
|----------|---------|------|---------|
| Rider_CPUThink | 0x8018fc58 | 0x40 | Rider proc: if CPU, run the AI update |
| Rider_CPUInit | 0x80262d6c | 0x270 | Alloc + init `CpuData`; sets `ai_state` from caller or `Rider_CPUSelectProfile` |
| Rider_CPUInitPlayer | 0x80275c04 | 0x3c | Init wrapper (player idx → rider); profile computed from context |
| Rider_CPUInitPlayerFixed | 0x80275c40 | 0x30 | Init wrapper forcing state 1 / difficulty 8 (from `Game_Think`) |
| Rider_CPUSelectProfile | 0x802766fc | 0xd0 | Map stage/city/ply → AI profile (`ai_state`) |
| _Rider_UpdateCPU | 0x80275c70 | 0x20 | Wrapper → Rider_UpdateCPU |
| Rider_UpdateCPU | 0x8026beec | 0x44 | Orchestrates the 4 CPU stages |
| Rider_ProcessCPUDistance | 0x8026bbe0 | 0x30c | Stage 1: perceive (self-state, anti-stuck) |
| Rider_CPUDecideState | 0x802716e8 | 0xa8 | Stage 2: strategic dispatch on `ai_state` (+0x08), 9 handlers |
| Rider_ProcessCPUManeuver | 0x8026bf30 | 0x170 | Stage 3: tactical dispatch on `maneuver` (+0x10), ~22 handlers; emits commands when VM idle |
| Rider_CPUResolveTargetPos | 0x80263120 | 0x4f0 | Resolve a nav target id (+ lead time) to a world position |
| Rider_CPUArbitrateManeuver | 0x80274ec0 | 0xd24 | **The maneuver chooser**: flag-gated priority cascade, commits `maneuver` (+0x10). Tail-called by the decide handlers |
| Rider_CPUBuildRoute | 0x8026a734 | 0xc74 | Spline look-ahead sampler: lays ≤4 waypoints into scratch 0x8055e964; returns 0 = route invalid (does **not** pick the maneuver) |
| Rider_CPUWalkRoute | 0x80264924 | 0x590 | Advance along the course spline graph by arc-length; returns `{node_id, along, side}` where `side` is a discrete link-direction tag (−1/0/+1), **not** a lateral offset |
| Rider_CPUUpdateNavTarget | 0x8026b6d0 | 0x510 | Nearest-node query → assigns `target_primary` (+0x38) + arc (+0x3c) |
| Rider_CPURivalSelect | 0x80264210 | 0x340 | Per-frame rival selector (states 1/2/4/8/10): scores 5 slots → `rival_player_idx` (+0x70) |
| Rider_CPUScanItems | 0x80263c4c | – | Item scan (states 3/8): top-5 ranked → `item_target` (+0x74) |
| Rider_CPUScanCityObjects | 0x802638a4 | – | City-object scan (state 3): top-5 ranked → `city_object` (+0x84) |
| Rider_CPUScanRouteGoal | 0x80263fd0 | – | Route-goal scan (states 5/6): single best → `route_goal` (+0x94) |
| Rider_CPUSelectChargeAnchor | 0x80263610 | – | Charge-anchor selector (state 7) → `charge_anchor` (+0xb0/+0xb4) |
| Rider_CPUBlendRoutePoints | 0x80267238 | – | Blends item/interaction/path-point positions into the steering route |
| Rider_CPUCollectHazards | 0x80269928 | 0x38c | Obstacle/threat collector into scratch 0x8055e698 (behavior bit 0x02 adds a 5th pass) |
| Rider_CPUForwardLookahead | 0x80269f10 | 0x588 | Forward-collision anticipation (behavior bit 0x80 gates it) |
| Rider_CPUEmitSteer | 0x8026d6a0 | 0x3a0 | Steer executor: desired direction → stick_x nudge/ramp + stick_y opcodes |
| Rider_CPUEmitSteerStick | 0x8026c4ec | – | Stick-opcode emitter: yaw error → 190 nudge / 192 ramp (stick_x), 129 (stick_y); per-difficulty envelope 0x804b7f54 |
| Rider_CPUResolveAvoidVector | 0x8026d1fc | – | Rotates the desired heading off the nearest imminent hazard (reads 0x8055e698) |
| Rider_CPUEmitAbilityAction | 0x80273d1c | – | Post-maneuver copy-ability press emitter (dispatch on kind + `copy_kind` +0x454); hazard-timed |
| Rider_CPUTerminateCmdStream | 0x80276228 | – | Caps the per-maneuver command stream (opcode 0x7f), arms the VM |
| Rider_CPUEmitChargeStutter | 0x8026da40 | – | Velocity-stuck charge-pump (press → hold-20 → hold-40) |
| Rider_CPUTrackStuckProgress | 0x8026ccec | – | Ticks the stuck counter (+0x5c) vs the machine's max-turn tolerance |
| Rider_CPUCollectRiderHazards | 0x80268234 | – | Hazard pass 1: rider bodies + hurt-volumes → 0x8055e698 |
| Rider_CPUForwardLookaheadSetup | 0x8026a498 | – | Forward-lookahead wrapper (builds basis, calls `Rider_CPUForwardLookahead`) |
| Rider_CPUGetSteerEnvelope | 0x80276650 | – | Per-difficulty `(step,cap)` steer envelope from 0x804b7f54 |
| Rider_CPUGetMachineTurnTolerance | 0x802776c4 | – | Per-machine max-turn angle from 0x804b8f30 |
| Rider_CPUGetAbilityPressHold | 0x802765d4 | – | Per-difficulty ability press-hold frames from 0x804b7f30 |
| Rider_CPUSeedDesire | 0x802762dc | 0x6c | OR inhibitor bits into `desire_flags` (+0x1c) from the rider's action state (tables 0x804b7b18 / 0x804b7c00) |
| Rider_CPUDifficultyScale | 0x80276f00 | 0x7c | Difficulty-scaled factor from `difficulty_level` (+0x22); scales personality rolls |
| Rider_CPUPushCmd1/2/3 | 0x80275fcc / 0x80276050 / 0x80276118 | – | Append an opcode (+1 / +2 operands) to `cmd_buffer` |
| Rider_CPUProcessCmd | 0x80275cbc | 0x2e0 | Stage 4: bytecode interpreter, sole writer of pad buttons/stick_x/stick_y |
| Rider_GetCPUButtons | 0x80275cb0 | 0x0c | Read pad buttons (CpuData+0x00) |
| Rider_GetCPUStickX | 0x80275c90 | 0x10 | Read pad stick X (CpuData+0x04) |
| Rider_GetCPUStickY | 0x80275ca0 | 0x10 | Read pad stick Y (CpuData+0x06) |
| Rider_InputThink | 0x8018ee28 | 0x6c0 | Rider proc: select effective input (human/CPU/replay) |
| Rider_CopyInputToMachine | 0x80190c54 | 0x144 | Copy rider input to machine |
| plGetPlayerKind | 0x8022c858 | 0x14 | Controller slot → PKIND (CPU = 1) |
| **CPU Passive Stat Growth (City Trial)** | | |
| CityTrial_GrowCpuStats | 0x80015a00 | 0x1b4 | Per-tick (every `gmGameParams.ct_cpu_stat_interval`=180 CT frames) drain of each CPU's seeded stat budget (`GameData.city.cpu_stat_budget`) into random `stat_aux` entries; called from `Game_Think` (0x80011f48) |
| SceneLoad_3D (seed block) | 0x8001442c | – | At CT load, seeds `GameData.city.cpu_stat_budget[slot]` = `gmGameParams.ct_cpu_stat_seed[cpu_level]` per CPU (0 for humans) |
| Ply_GetStatAux / Ply_SetStatAux | 0x8022d0cc / 0x8022d128 | 0x5c / 0x98 | Read / write a slot's 9-entry `stat_aux` block (`PlayerData+0x68`); the setter also clamps it into the machine (`cityTrial_setMasterStats`) while riding. Distinct from the `stats`-union pair `Ply_GetAllStats`/`Ply_SetAllStats` (0x8022cf6c / 0x8022cfc8, `PlayerData+0x44`) |
| Ply_GetCpuLevel / Ply_SetCpuLevel | 0x8022d7b0 / 0x8022d798 | 0x18 / 0x18 | Slot ↔ cpu_level (signed `PlayerData+0xAC`); −1 = not a CPU, else 0..8. Copied from `ply_desc[slot].cpu_level_eff` by `Player_Create` |
| cityTrial_getMasterStats / cityTrial_setMasterStats | 0x801c81c0 / 0x801c8258 | 0x50 / 0x38 | Copy the machine master stat block (`MachineData+0x94c`) onto a rider buffer / write a buffer back through the patch-apply path |
| Machine_SetStatBlockClamped | 0x80194f64 | 0x144 | Write 9 stats into the machine added-patch block (`MachineData+0x9e8`), each clamped to `Patch_GetMinValue`/`MaxValue` |
| Stat_AddClamped / Stat_AddClampedAll | 0x80194d80 / 0x80194e60 | 0xe0 / 0x104 | Add a delta to one / all nine stats, clamped to patch min/max (the patch-pickup primitives) |
| Gm_GetCityTrialFrame | 0x800132b8 | 0x0c | City Trial frame counter (`*0x805dd570`) |
| **Top Ride CPU AI** (`a2d_cpu_kirby.cpp`) | | |
| TopRide_CpuInputThink | 0x802eee90 | 0x97c | CPU input-reader `vt[0x0c]`: per-frame steering brain; writes `*steer_out`, returns charge byte. **The Top Ride replace hook.** |
| TopRide_CpuInputPoll | 0x80291dec | 0x6c | CPU input-reader `vt[0x14]`: calls the brain, packs to history |
| TopRide_CpuInputReaderInit | 0x802eed00 | 0x190 | CPU reader ctor; seeds `difficulty` (+0x1c) from handicap byte (asserts ≤ 4) |
| TopRide_CpuPerceive | 0x802eb094 | 0xb04 | CPU brain: per-frame perception scan → stack blackboard (sector look-ahead, nearest rival/item/obstacle) |
| TopRide_CpuDetectHazard | 0x802ed434 | 0x574 | Detector 1: incoming projectiles → dodge swerve |
| TopRide_CpuDetectItem | 0x802ecc54 | 0x580 | Detector 2: `ItemMgr` → steer toward good / away from bad |
| TopRide_CpuDetectRival | 0x802eda78 | 0x798 | Detector 3: rival kirbys → block / ram intercept |
| TopRide_CpuDetectObstacle | 0x802ee210 | 0x57c | Detector 4: `CpuObstacleMgr` walls / air-currents → avoid or boost-into |
| TopRide_CpuRouteFollowFull | 0x802ebb98 | 0x450 | Selector A: perfect-line route follow (reaction_budget==0; dead in normal play) |
| TopRide_CpuRouteFollowLagged | 0x802ec890 | 0x2ec | Selector B: reaction-lagged route follow (the normal-play path) |
| TopRide_CpuCommitSwerve | 0x802eec54 | 0xac | Reseed steer_noise (+0x20), reset reseed timer (+0x6c=180), set commit hold (+0x28=5) |
| TopRide_ClassifyItem | 0x802ecb7c | 0x30 | Item kind (`Item+0x60`) → +good / 0 neutral / −bad |
| TopRide_CpuAggressionRoll | 0x802eebe4 | – | Per-detector aggression-gate roll |
| TopRide_ApexGridScan | 0x802cf83c | – | Reads/evaluates the per-sector apex grid (`EnemyMgr+0x30dc`) |
| TopRide_PadInputRead | 0x802d4c4c | 0x110 | Human input-reader `vt[0x0c]`: reads the GC pad (AutoRepeat + `stc_engine_pads`) |
| TopRide_PadInputPoll | 0x802a06bc | 0x98 | Human input-reader `vt[0x14]` |
| TopRide_KirbyGetSlot | 0x802d4d5c | 0x08 | Kirby `vt[0x30]`: returns `player_slot` (kirby+0x0c) |
| TopRide_PackStickInput / UnpackStickInput | 0x80311db0 / 0x80311ea0 | 0xf0 / 0x8c | Steer floats (+ flag) ⇄ packed history short |
| TopRide_KirbyPhysUpdate | 0x802d5ec0 | 0xdcc | Per-frame kirby update; polls the input reader when `r4 != 0` |

## Data Addresses

| Data | Address | Description |
|------|---------|-------------|
| Strategic state jump table | 0x804b7a28 | Switch table for `ai_state` (+0x08) in `Rider_CPUDecideState` |
| Tactical maneuver jump table | 0x804b79b8 | 22-entry switch table for `maneuver` (+0x10, 0..0x15) in `Rider_ProcessCPUManeuver`; index 0x11 + out-of-range → Coast |
| City-kind → profile table | 0x804b7f78 | 16 `(city_kind, ai_state)` int pairs, −1 terminated, scanned by `Rider_CPUSelectProfile` (default 2) |
| Ability press-hold table | 0x804b7f30 | per-difficulty ability-press hold frames `{15,12,10,8,6,4,2,1,0}` (idx by `+0x22`), read by `Rider_CPUGetAbilityPressHold` |
| Steer envelope table | 0x804b7f54 | per-difficulty stick `(step, cap)` pairs (9 entries), read by `Rider_CPUGetSteerEnvelope` |
| Machine attack-score table | 0x804b8000 | per-machine, stride 0x14: +4/+6/+8 attack scores (base / damage-off / damage-on), +0xc range weight, +0x10 bit 0x40 = priority-target |
| Machine capability tables | 0x804b8854 / 0x804b89d0 | per-machine CPU capability flags (non-bike / bike), stride 0x14; +8 flag bits gate charge/attack/dodge; selected by `is_bike` |
| Machine turn-tolerance table | 0x804b8f30 | per-machine max-turn angle (stride 0x14, +0xc ≈ 0.785 rad for entry 0), read by `Rider_CPUGetMachineTurnTolerance` |
| Desire-flag seed tables | 0x804b7b18 / 0x804b7c00 | `{u32 id; u32 inhibitor_flags}`, stride 8; indexed by `RiderData.state_idx` (+0x1c). Table 1 = 29 entries (ids 0x00..0x1c, all kinds); Table 2 = 102 entries (ids 0x1d..0x82, `kind==0` only, indexed by `state_idx-29`). Fully enumerated in § Behavior & desire flags |
| Course path-graph object | `stc_grobj_ptr` 0x805dd6cc (r13[0x5ec]) | Per-stage spline node array (`[grobj+0x120]+id*0x1c`); the id space for `target_primary/secondary` |
| CpuData registry / count | 0x8055de08 / 0x8055de1c | Up to 5 allocated `CpuData*` + a count byte. **Never freed per-rider** (bulk-freed at scene teardown); iterated **only** by a debug-text overlay, never by gameplay |
| CPU stat-growth budget | `GameData.city.cpu_stat_budget` (`+0x46c`) | `float[5]`, per-slot remaining stat pool. Seeded by `SceneLoad_3D` from `cpu_level`, drained by `CityTrial_GrowCpuStats`. 0 for humans |
| CPU stat-growth tables | `gmGameParams.ct_cpu_stat_{seed,rate,interval}` (`+0x154 / +0x178 / +0x19c`) | `float[9]` seed pool `{3,5,10,15,20,25,30,35,42}` / `float[9]` drain rate `{1,1,1,1,1.2,1.5,2,2.5,3}` / `int` tick interval `180`. In `gmGameParams` (`gmDataAll->game_params`), indexed by `cpu_level` 0..8 |
| Strategic (decide) handlers | 0x80271xxx–0x80273xxx | 9 handlers; set behavior flags + targets, then tail-call `Rider_CPUArbitrateManeuver` |
| Tactical (maneuver) handlers | 0x8026c0a0–0x802715e0 | ~22 handlers dispatched on `maneuver` (+0x10); emit command streams |
| Per-frame scratch buffers (typed) | 0x8055e964 / 0x8055e698 / 0x8055e8b4 | Three distinct single-rider structures, zeroed each perceive: **route waypoints** (0x2f0, ≤4 lane-midpoint waypoints from `Rider_CPUBuildRoute`; header byte 4/5 = valid route); **hazard/threat list** (0x210, stride 0x40, ≤8, count at 0x8055e898, +0x38 time-to-impact / +0x3c bit 0x80 = imminent); **forward-collision list** (0xb0, stride 0x14, ≤8, count at 0x8055e954). **Not** a multi-rider world model |
| Per-machine envelope cache / self-vel basis | 0x8055e68c·690·694 / 0x8055e89c | three per-machine turn/speed-envelope scalars (feed route-build + steer) and the cached self-velocity Vec3 basis (hazard avoidance), all written each perceive |
| Top Ride code region (C++) | ~0x8031xxxx–0x803bxxxx | Separate C++ mode (real classes `Kirby`/`ChickMgr`). CPU AI is `a2d_cpu_kirby.cpp` (mapped, see § Top Ride). |
| TR CPU input-reader vtable | 0x804d8710 | vtable for the CPU `TopRideCpuInputReader`; human readers use 0x804d25e0. `vt[0x0c]` = brain, `vt[0x14]` = poll |
| TR per-slot CPU level scratch | 0x804d8040 | `float[slot]`; written by the CPU reader ctor = handicap byte, read back into `reader->difficulty` (+0x1c) each frame |
| TR per-level tuning tables | 0x804d80d0 / 0x804d8058 / 0x804d80bc | Indexed by `reader->difficulty` (0..4): steer gain (80→−70) / commit-hold frames (60→0) / commit threshold (0→100). Per-detector aggression gates: item 0x804d80a8, rival 0x804d8094, ram 0x804d806c |
| TR reaction-budget table | 0x804d7f90 | **floats** `[4.0, 1.0, 2.0, 3.0, 5.0]` (IEEE words `40800000 3F800000 40000000 40400000 40A00000` — *not* ints); indexed by `reader->x18` (not difficulty) → `reaction_budget` (+0x14). Min is 1.0, never 0, so `TopRide_CpuRouteFollowFull` is dead in normal play |
| TR blackboard aggression weights | 0x804d7fb0 / 0x804d7ff8 / 0x804d7fd4 / 0x804d801c | four float weight tables indexed by `reaction_budget` (+0x14), copied into `blackboard[0x2a..0x2d]`: `[30,27,30,35,25]` / `[1,.5,.1,1,1]` / `[1,.5,1,1,.1]` / `[1,.5,1,.1,1]` |
| TR kirby spawner | `TopRide_SpawnKirby` 0x802db518 | One kirby per slot; picks pad-vs-CPU reader for `TopRideKirby+0x48` from `game_config+slot*9+0x60` (demo branch uses `DAT_8048a250`) |
