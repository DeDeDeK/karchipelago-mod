# CPU Rider AI System

How the AI-controlled riders (the CPU opponents in **City Trial and Air Ride**)
make decisions and drive their machines. This is the **rider** AI — the players
you race against — and is entirely separate from the enemy/event-actor AI (see
[enemy-ai-system.md](enemy-ai-system.md)), which drives inhalable Air Ride enemies.

**Scope:** this doc covers the `RiderData`-based modes (City Trial, Air Ride),
which are plain C. **Top Ride is a separate C++ mode** operating on `TopRideKirby`,
with its own CPU handling that is **still largely unmapped** (and was previously
misdescribed — see the warning in the Top Ride section below). The two share no code.

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
   `RiderData.machine_gobj` (`CpuData+0x0c/+0x0d`), records position/velocity, and
   runs two **anti-stuck detectors** — a position one (no movement away from
   `recorded_pos` for 241 frames → `status_flags` bit `0x04`) and a velocity one
   (too slow or moving against facing for 60 frames → bit `0x02`). It also ticks
   the frame counter (`CpuData+0x28`) and a suppression timer (`CpuData+0x20`) that
   forces the secondary target off while active. It zeroes three global per-frame
   scratch buffers (`0x8055e964`/`0x8055e698`/`0x8055e8b4`) — transient working
   space reused for each rider's pass, **not** a persistent multi-rider world model.
2. **Decide** — `Rider_CPUDecideState` (0x802716e8) dispatches on the **strategic
   state** `CpuData+0x08` (1..10; **state 0 asserts** — `cpu.c:0x21f1`) to one of 9
   handlers (`0x80271xxx`–`0x80273xxx`; states 2/4/`>10` share `0x80271b24`). The
   `0x804b7a28` table is the compiler's switch jump table, not an array of handler
   pointers. Each handler sets the **behavior flags** (`CpuData+0x2c/+0x2d`),
   computes the navigation target (see below), selects the target entities
   (`target_primary`/`target_secondary`), and chooses the **tactical maneuver**
   (`CpuData+0x10`). It clears the scratch words `CpuData+0x18`/`+0x1c` at entry.
3. **Process** — `Rider_ProcessCPUManeuver` (0x8026bf30) dispatches on the
   **tactical maneuver** `CpuData+0x10` (0..0x15, ~22 handlers) to emit a fresh
   command stream — **but only when the command VM is idle** (`cmd_read_ptr == 0 &&
   cmd_timer == 0`). So one maneuver's command stream plays to completion before
   the next maneuver is chosen. It first resets the write pointer
   (`cmd_write_ptr = &cmd_buffer`), runs the maneuver handler (which appends
   opcodes via `cmd_write_ptr`), then a steering controller (`0x80273d1c`) converts
   the desired heading into stick opcodes.
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
  concrete maneuver that *emits* a command stream, re-chosen by the decide handler
  (and by maneuvers handing off to each other) but only when the previous command
  stream finishes, giving each maneuver a minimum dwell time.

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
  resolved to a world position by `Rider_CPUResolveTargetPos` (0x80263120) with a
  lead/predict time (`target_lead` +0x3c). The handler steers at whichever is
  *closer* — the track look-ahead or the target entity. `CpuData+0xe4/+0xe8` is a
  small packed cache of upcoming route nodes.

Both target ids live in the **course nav-node id space** (resolved through the
`0x800ce/cf` course-path module), not the raw rider/item lists — so meaningfully
driving the vanilla AI's targeting from outside means understanding that path
graph. For custom behavior, computing our own steering and writing the pad
directly (below) sidesteps it entirely.

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
the timer-setting opcodes (150/152/180) mean "hold the current pad state for N
frames." Steering (`stick_x`) has both relative (190) and ramp-to-target (192)
primitives for smoothing; accel/brake (`stick_y`) only has absolute set (130).

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
| +0x2c | behavior_flags | rewritten per strategic state |
| +0x2d | status_flags | bit 0x02 velocity-stuck, bit 0x04 position-stuck |
| +0x38 | target_primary | primary nav target id (-1 = none) |
| +0x3c | target_lead | lead/predict time for target resolution |
| +0x44 | target_secondary | secondary nav target id (-1 = none) |
| +0xa4 | nav_target_ptr | → steering target (track look-ahead point) |
| +0xb8 | nav_target_pos | resolved navigation target (Vec3) |
| +0x110 | cmd_timer | command VM frame countdown |
| +0x114 | cmd_read_ptr | command VM playback position (0 = idle) |
| +0x118 | cmd_write_ptr | where maneuver handlers append opcodes |
| +0x11c | cmd_buffer[0x80] | command opcode stream |

The getters are trivial struct reads (e.g. `Rider_GetCPUStickX` is
`lwz r3,0x778(r3); lha r0,4(r3); extsb r3,r0`), so the pad layout above is exact.

## Strategic states (`ai_state`, CpuData+0x08)

The 9 decide handlers. Each one: seeds intent bits into `desire_flags` via
`Rider_CPUSeedDesire` (0x802762dc), rewrites `behavior_flags` (+0x2c), computes the
nav target, picks the tactical maneuver, and scales its probability rolls by
`Rider_CPUDifficultyScale` (0x80276f00) — which reads the difficulty level at
`CpuData+0x22` (0 = easiest .. 8 = hardest). `base_maneuver` (+0x14) is the maneuver
a state parks on (states write 1 or 2); handlers fall back to it.

| State | Addr | Name | Behavior |
|-------|------|------|----------|
| 1 | 0x80271790 | **Cruise** | Follow the racing line; leanest flag set. Steers at whichever is closer: the track look-ahead or `target_primary`. |
| 2,4,>10 | 0x80271b24 | **Cruise+ (default)** | Catch-all cruise; same targeting as 1 but enables the item/attack desire bits. |
| 3 | 0x80271eb4 | **Navigate** | Heavyweight city path-finding; all behaviors enabled. Holds the **position-stuck recovery sweep** (the get-unstuck workhorse): on stuck + unreachable target it sweeps the reversed forward vector for an escape node and clears targets. |
| 5 | 0x802726fc | **RouteFollow** | Follow a precomputed route (3-slot cache) toward a goal; no spline look-ahead. |
| 6 | 0x80272888 | **RouteFollow+City** | Like 5, plus nearest-city-object override and the same anti-stuck escape as state 3. |
| 7 | 0x80272dd0 | **Charge** *(med)* | Drive to a fixed world anchor; on a difficulty-scaled `HSD_Randf` roll when close, commit maneuver 0x15. |
| 8 | 0x802735dc | **Attack** *(high)* | Acquire the nearest in-cone rival (loops players 0..4, distance minus collision radii), then on rolls fire a scripted spin-attack input burst (L/R variant by coin-flip). |
| 9 | 0x80273228 | **Reposition** *(med)* | Branches on `stage_kind` (+0xf) to nudge its own position by stage-specific offsets (sidestep / back off / climb), then resume. |
| 10 | 0x80273b48 | **Patrol** *(low-med)* | Timed ~900-frame toggle of a 2-bit sub-state (`+0x2d` bits 0x18); follows nav target or a fixed rival. |

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

## Tactical maneuvers (`maneuver`, CpuData+0x10)

The ~20 process handlers that emit command streams. Steering opcodes mostly come
from the shared executor `Rider_CPUEmitSteer` (0x8026d6a0 → 0x8026c4ec: emits 190
nudge / 192 ramp for `stick_x`, 129 for `stick_y`); `"+0x10 = base_maneuver"` or
`"= 10"` is how a maneuver hands control onward.

| Man | Addr | Name | Pad pattern |
|-----|------|------|-------------|
| 0 | 0x8026c0a0 | **Coast** | Ramp stick toward heading, `stick_y = 0`, no button. |
| 1 | 0x8026dd4c | **RecoverForward** | Steer to arc point; charge-tap stutter (helper 0x8026da40) when velocity-stuck; bail to maneuver 10 if forward speed lost. |
| 2 | 0x8026de68 | **SteerToNav** | Steer to `nav_target_pos`; same stutter / bail. |
| 3 | 0x8026e448 | **RamCharge** | Intercept a rival via angle-cone gates; brief timed press (hold 4) on contact/charge; else steer. |
| 4 | 0x8026e97c | **PursueLOS** | Steer to rival while it stays in the cone; never presses. |
| 5,6 | 0x8026ee7c | **ApproachWaypoint** | Far → delegate to maneuver 1; close + aligned → press+hold 10 on arrival. |
| 7 | 0x8026ed4c | **ChargeHold** | Steer, hold the charge button, release when charge tops off. |
| 8 | 0x8026fbe0 | **AvoidObstacle** | 3-point avoidance scan around the nav target; terrain-shaped accel (helper 0x8026f0e4); no button. |
| 9 | 0x8026fe7c | **DodgeProjectile** | On a detected threat, RNG-gated evasive burst (set stick + press/hold 60 / release 20). |
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

## This brain is self-contained C — Top Ride is separate

The pipeline above is plain C and self-contained. Its decide handlers call only
math / utility helpers — never any C++ class. Verified by disassembling every
handler and resolving their `bl` targets; the only non-`Rider_*` calls are:

- `PSVECMagnitude` (0x803d2158), `Vec2_Dist` (0x803d22cc) — perception distances.
- `splArcLengthPoint` (0x80415958) — spline arc-length, i.e. track position.
- `HSD_Randf` (0x8041e610) — the RNG that drives the personality probability rolls.
- `_savegpr_*` (e.g. 0x803adb48) — compiler register-save prologue helpers.

### Top Ride's CPU AI — what's actually known

> **Warning — the Top Ride CPU AI is not yet mapped, and a signature matcher will lie to
> you here.** A signature-based symbol matcher pairs KAR against another HAL title that
> shares the HSD/CodeWarrior engine: shared engine and runtime functions (`HSD_*`, `pow`,
> `atan2`, `__dynamic_cast`, the TRK debugger) match correctly, but KAR's *game-logic*
> functions get stamped with the other game's symbols — which happen to be a
> **soccer/football game**. That is the sole origin of names like `cAIPad`, `FormationPos`,
> `FormationOffensive`, `CommonDesireData`, `cFielder::IsStriker`, `IChooseCaptain`,
> `Cup<…>::GetTeamStats`, `Jumbotron`, `GoalOverlay`, `WorldLoader::GetStadiumTerrain`.
> None of those class-name strings exist anywhere in the KAR binary (each returns **0
> hits**; only the real RTTI names `Kirby` and `ChickMgr` are present), and the underlying
> functions are unrelated to the names — e.g. the one labeled "cFielder::UpdatePlay"
> (0x8032a998) is just an entry in an `EffectDesert` vtable, and the "cAIPad::__ct"
> (0x803589f4) is the generic `li r0,0; stw r0,0(r3); blr` stub that matches any
> zero-a-word constructor. **There is no cAIPad / Formation / Desire Top Ride AI.** These
> false names have been cleared from the symbol map (reverted to `zz_<addr>_`); if you
> re-run such a tool, discard any soccer-vocabulary result for this region.

What *is* confirmed about Top Ride:

- It is genuinely a **C++ mode** (real RTTI classes `Kirby`, `ChickMgr`,
  `ChickMgr::Chick`, plus the Kirby state machine — see `topride-system.md` and
  `topride-kirby-states.md`), operating on `TopRideKirby`, not `RiderData`.
- **Per-slot CPU difficulty** is `cpu_level` 0..4, stored at `GameData[slot*9 + 0xD22]`
  and written by `TopRide_SetCpuLevel` (0x8000be74). Where the AI *reads* it back has
  not been found.
- Each kirby is spawned by `TopRide_SpawnKirby` (0x802db518), one per slot: it allocates
  the 0x13d8-byte kirby and calls `TopRide_KirbyInit` (0x802d4d64), passing an
  **input-source value** that `KirbyInit` stores at `TopRideKirby+0x48`
  (`input_reader`). The source is chosen by branch:
  - `game_config+0x38 == 3` (an attract/demo-style submode): a **pointer** from the
    4-entry table at `DAT_8048a250` — i.e. canned / replay inputs.
  - otherwise: a small **per-slot config byte** from `game_config + slot*9 + 0x60`.
- The kirby's input is polled in `TopRide_KirbyPhysUpdate` (0x802d5ec0) as
  `kirby->input_reader->vtable[+0x14](&out_stick, &out_flag)` — **but only when that
  function's mode arg (`r4`) is non-zero** (a `beq` skips it otherwise). That, plus the
  fact that `+0x48` holds a real object only in the demo/replay branch, indicates this
  `+0x48` poll is the **demo / replay-input path, not the normal-play CPU brain.**

**Still open — needs live debugging, not static analysis.** Where normal-play CPU slots
get their steering / charge decisions, and how `cpu_level` modulates them, is not
resolved. The relevant control flow is runtime-mode-dependent and `scripts/mem1.raw` is
a *main-menu* snapshot, so static disassembly can't settle it. Next step: a Dolphin
session — break in `TopRide_KirbyPhysUpdate` (0x802d5ec0) during a real Top Ride race
that includes a CPU racer, inspect the mode arg (`r4`) and `kirby+0x48`, and trace where
the CPU slot's per-frame stick / charge values are written. The pad-injection strategy
below (for CT/AR) does not transfer until that path is mapped.

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
- **Per-state probability rolls** — the CT/AR handlers gate decisions on
  `HSD_Randf` against static thresholds. Those thresholds are the fine knobs for
  nudging vanilla behavior, but locating each one means REing the individual state
  handlers (`0x80271xxx`–`0x80273xxx`). Deferred. (Top Ride is a *separate* C++
  mode — see above — and none of this applies there.)
- **Re-profile via `ai_state`** — because `ai_state` (+0x08) is a *fixed profile*,
  not a transitioning state, simply writing a new value (e.g. 8 = Attack, 5 =
  RouteFollow) cleanly swaps the CPU's whole personality for the rest of the match —
  the highest-value tweak and exactly how our custom presets could map. Easiest hook
  is right after `Rider_CPUInit`, or override `Rider_CPUSelectProfile`'s return.
- **Maneuver / target overrides** — forcing `maneuver` (+0x10) or the target ids
  (`target_primary` +0x38 / `target_secondary` +0x44) nudges the tactical layer:
  e.g. pin `maneuver` to 0x15 (ChargeStill) or 8 (AvoidObstacle). The catch on
  *targets*: the ids are **course nav-node ids**, not rider/item handles, so
  producing a useful value means going through the path-graph module. Lower effort
  to just replace the pad (below).

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
| Rider_CPUPickManeuver | 0x8026a734 | 0xc74 | Choose the tactical maneuver from geometry (shared by cruise states) |
| Rider_CPUEmitSteer | 0x8026d6a0 | 0x3a0 | Steer executor: desired direction → stick_x nudge/ramp + stick_y opcodes |
| Rider_CPUSeedDesire | 0x802762dc | 0x6c | Seed intent bits into `desire_flags` (+0x1c) from a script table |
| Rider_CPUDifficultyScale | 0x80276f00 | 0x7c | Difficulty-scaled factor from `difficulty_level` (+0x22); scales personality rolls |
| Rider_CPUPushCmd1/2/3 | 0x80275fcc / 0x80276050 / 0x80276118 | – | Append an opcode (+1 / +2 operands) to `cmd_buffer` |
| Rider_CPUProcessCmd | 0x80275cbc | 0x2e0 | Stage 4: bytecode interpreter, sole writer of pad buttons/stick_x/stick_y |
| Rider_GetCPUButtons | 0x80275cb0 | 0x0c | Read pad buttons (CpuData+0x00) |
| Rider_GetCPUStickX | 0x80275c90 | 0x10 | Read pad stick X (CpuData+0x04) |
| Rider_GetCPUStickY | 0x80275ca0 | 0x10 | Read pad stick Y (CpuData+0x06) |
| Rider_InputThink | 0x8018ee28 | 0x6c0 | Rider proc: select effective input (human/CPU/replay) |
| Rider_CopyInputToMachine | 0x80190c54 | 0x144 | Copy rider input to machine |
| plGetPlayerKind | 0x8022c858 | 0x14 | Controller slot → PKIND (CPU = 1) |

## Data Addresses

| Data | Address | Description |
|------|---------|-------------|
| Strategic state jump table | 0x804b7a28 | Switch table for `ai_state` (+0x08) in `Rider_CPUDecideState` |
| City-kind → profile table | 0x804b7f78 | `int` pairs `(city_kind, ai_state)`, scanned by `Rider_CPUSelectProfile` (default 2) |
| CpuData registry / count | 0x8055de08 / 0x8055de1c | Global array of up to 5 allocated `CpuData*` and the live count |
| Strategic (decide) handlers | 0x80271xxx–0x80273xxx | 9 handlers; set behavior flags + targets + maneuver, write the scratch buffer |
| Tactical (maneuver) handlers | 0x8026c0a0–0x802715e0 | ~22 handlers dispatched on `maneuver` (+0x10); emit command streams |
| Per-frame command-build scratch | 0x8055e964 / 0x8055e698 / 0x8055e8b4 | Global scratch (0x2f0 / 0x210 / 0xb0 bytes), zeroed each perceive; transient, **not** a multi-rider world model |
| Top Ride code region (C++) | ~0x8031xxxx–0x803bxxxx | Separate C++ mode (real classes `Kirby`/`ChickMgr`); CPU AI still unmapped. Signature matchers mislabel functions here with a soccer game's symbols (`cAIPad`/`Formation`/`Desire`/`cFielder`/…) — discard any such result |
| TR kirby spawner / input poll | `TopRide_SpawnKirby` 0x802db518 / `TopRide_KirbyPhysUpdate` 0x802d5ec0 | Spawner sets `TopRideKirby+0x48` (`input_reader`); PhysUpdate polls it via `vt[+0x14]` (demo/replay path) |
