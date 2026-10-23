# Top Ride CPU AI

How Top Ride's computer players steer. Top Ride is a separate C++ mode built on `TopRideKirby`
objects, with no `RiderData` and none of City Trial and Air Ride's CPU code; its CPU lives in
`a2d_cpu_kirby.cpp` and produces a lateral steer value and a press/charge flag instead of a full
pad. A modder retuning or replacing Top Ride CPUs works here.

## Input readers

Every kirby holds an input reader at `TopRideKirby+0x48`, and the reader's class decides where
its input comes from - that is the whole human/CPU seam:

| Reader | Vtable | `vt[0x14]` poll | `vt[0x0c]` read |
|---|---|---|---|
| Human | 0x804d25e0 | `TopRide_PadInputPoll` (0x802a06bc) | `TopRide_PadInputRead` (0x802d4c4c), the controller |
| CPU | 0x804d8710 | `TopRide_CpuInputPoll` (0x80291dec) | `TopRide_CpuInputThink` (0x802eee90), the brain |

`TopRide_KirbyPhysUpdate` (0x802d5ec0) polls the reader each frame when its mode argument is
non-zero; the poll calls the read and packs the result into the kirby's input history. Which
reader a slot gets comes from the per-slot config byte at `game_config + slot*9 + 0x60`.

## Difficulty

The lobby shows one skill control per slot, labelled "Handicap" for humans and "CPU Level" for
CPUs. It is a single field: `TopRide_SetHandicap` (0x8000bf04) writes it to `GameData[slot*9 +
0xD23]` (`game_config + slot*9 + 0x5b`), and the CPU reader is constructed from that byte by
`TopRide_CpuInputReaderInit` (0x802eed00), which asserts it is at most 4 and stores it as
`reader->difficulty` (+0x1c, 0-4 for the lobby's 1-5) and in a per-slot float array at
0x804d8040. The neighboring byte `TopRide_SetCpuLevel` (0x8000be74) writes (`+0x5a`) is not read
by the brain.

`difficulty` indexes the brain's per-level float tables:

| Table | Address | Level 0 -> 4 |
|---|---|---|
| Steer gain | 0x804d80d0 | 80, 65, 50, 0, -70 |
| Commit hold frames | 0x804d8058 | 60, 45, 30, 10, 0 |
| Commit threshold | 0x804d80bc | 0, 20, 50, 70, 100 |
| Item aggression | 0x804d80a8 | 0, 20, 40, 70, 100 |
| Rival aggression | 0x804d8094 | 0, 8, 15, 40, 100 |
| Ram aggression | 0x804d806c | 0, 10, 20, 40, 100 |

A higher level turns harder, re-decides faster and grabs items, blocks and rams more often.
`TopRide_CpuDetectObstacle` also reads the ram aggression row, so it governs how readily a CPU
reacts to walls, edges and air currents as well as rams. A
separate reaction budget (`reader->reaction_budget`, +0x14) comes from the table at 0x804d7f90
(4, 1, 2, 3, 5) indexed by `reader->x18`, not by difficulty.

## The brain

`char TopRide_CpuInputThink(TopRideCpuInputReader *reader, float *steer_out)` runs each frame
for CPU slots only:

1. `TopRide_CpuPerceive` (0x802eb094) fills a stack blackboard: the kirby's position, facing and
   velocity, the current and next track sector (a binary search of the route's sector ring,
   line-of-sight tested), the heading error, the nearest rival, item and obstacle, and four
   aggression weights picked by the reaction budget.
2. Four detectors run in order, and the first to commit skips the rest and the route-follow:

   | Order | Detector | Reacts to |
   |---:|---|---|
   | 1 | `TopRide_CpuDetectHazard` (0x802ed434) | Incoming projectiles: an evasive swerve by time to impact |
   | 2 | `TopRide_CpuDetectItem` (0x802ecc54) | Items, classified good or bad by `TopRide_ClassifyItem` (0x802ecb7c): steer toward good, away from bad, on an aggression roll |
   | 3 | `TopRide_CpuDetectRival` (0x802eda78) | Rival kirbys: block or ram depending on both kirbys' states |
   | 4 | `TopRide_CpuDetectObstacle` (0x802ee210) | Walls and edges to avoid, air currents to steer into while charging |

   A commit (`TopRide_CpuCommitSwerve`, 0x802eec54) reseeds the steering jitter and holds the
   decision for a few frames.
3. If nothing committed, a route-follower steers the racing line:
   `TopRide_CpuRouteFollowLagged` (0x802ec890) in normal play. The "perfect line"
   `TopRide_CpuRouteFollowFull` (0x802ebb98) is selected only by a zero reaction budget, which
   the shipped table never yields. Its apex data is a grid inside the `EnemyMgr` singleton
   rebuilt every frame (`TopRide_ApexGridScan`, 0x802cf83c), not a static asset.
4. A final switch on the kirby's own state decides whether it can act at all: stunned states
   flail with charge off, grinding holds the rail with charge on, and the normal state turns
   the desired heading into `*steer_out` (clamped to +/-0.98, damped against a 9-entry history
   of recent steer changes). The return value is the press/charge flag.

## Notes for modders

- **Difficulty for every CPU**: write the per-slot handicap byte before the round, or
  `reader->difficulty` live; patching the per-level tables retunes all CPUs at once.
- **Replacing or biasing the brain**: hook `TopRide_CpuInputThink` itself. It is CPU-only by
  construction, with the reader in `r3`, the steer output in `r4` and the kirby at
  `reader->kirby` (+0x04); adjust `*steer_out` and the returned flag after the vanilla call, or
  skip it.
