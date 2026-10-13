# Checklist Stat Tracking

How the game decides that a checklist cell is complete. Gameplay writes per-player counters
into each player's `PlayerStats` record; a handful of per-mode evaluators read them at fixed
moments, fold some into cross-game totals kept beside each mode's clear data, test every cell's
threshold, and complete the cell through `ClearChecker_SetNewUnlock` (Top Ride:
`ClearChecker_SetNewUnlockSilent`). Knowing which counter feeds a cell, when it is read, and
whose play it counts is what a modder needs to add an objective, detect one from outside, or
explain why a cell never fires.

## The three layers

1. **`PlayerStats`** - one per player slot, `PlayerData.stat_record` at `+0xB0` (five
   `PlayerData` of 0x90c bytes from 0x8055aaa0). `Ply_GetStats(ply)` (0x8022d248) returns it.
   `SceneLoad_3D` runs `Player_InitAll` (0x8022d2ac) on every 3D scene load, which zeroes it, so
   everything in it is per game: one race, one City Trial city segment, one stadium. A stadium
   visited mid-trial therefore starts the counters again.
2. **Evaluators** - per-mode functions that read the counters, decide, and complete cells. They
   all bail during a LAN session (`Net_IsSessionActive`) and skip a cell whose
   `ClearChecker_GetKindClear(mode, ck) & 5` is already set.
3. **Records** - each mode's cross-game accumulators, stored right after its `GameClearData`
   (+0xF4) and saved with it: `CityTrialClearRecords`, `AirRideClearRecords` and Top Ride's
   `TopRideStats`. Lifetime cells ("break more than 1000 boxes") read these.

## Evaluators

| Mode | Function | Address | Called from | Cells |
|---|---|---|---|---|
| City Trial | `CityTrial_CheckForNewUnlocks` | 0x8004db74 | `CityTrial_MinorExit` | The city cells: items, patches, food, KO causes, props, events, busts |
| City Trial | `CityTrial_CheckFreeRunObjectives` | 0x8004e660 | `Game_Think` | Free Run machine changes and drive time |
| City Trial | `CityTrial_CheckStadiumPlayedObjectives` | 0x8004e748 | `Checklist_ProcessUnlock` | 10 / 20 stadium modes |
| City Trial | `CityTrial_CheckStadiumScoreObjectives` | 0x8004e810 | `Game_Think` | High Jump, Target Flight score and airtime |
| City Trial | `CityTrial_CheckStadiumResultObjectives` | 0x8004e998 | `CityTrial_MinorExit` | Drag times, Air Glider, Target Flight results, KO totals, VS. King Dedede |
| City Trial | `CityTrial_CheckStadiumKOObjectives` | 0x8004f03c | `Game_Think` | One-game Destruction Derby and Kirby Melee KO counts |
| Air Ride | `AirRide_CheckRaceFinishObjectives` | 0x8004aa58 | `MinorExit_AirRideMachineSelect` | Table A (30 handlers), then the distance cells and the records |
| Air Ride | `AirRide_CheckObjectivesPerFrame` | 0x8004a7f0 | `Game_Think` | Table B (17 handlers), four per frame round-robin (`GameData+0xa88`) |
| Air Ride | `AirRide_CheckRaceLapObjectives` | 0x8004d248 | `AirRide_DispatchRaceTimeAttackObjectives` | Race "finish N laps under T" |
| Air Ride | `AirRide_CheckTimeAttackObjectives` | 0x8004d5d4 | `AirRide_DispatchRaceTimeAttackObjectives` | Time Attack course times |
| Air Ride | `AirRide_CheckFreeRunLapObjectives` | 0x8004d8a8 | `AirRide_DispatchFreeRunObjectives` | Free Run lap times |
| Air Ride | `AirRide_CheckRaceDistanceObjectives` | 0x8004d454 | `AirRide_CheckRaceFinishObjectives` | "Race over N feet in 2 minutes" |
| Top Ride | `TopRide_CheckPerCourseObjectives` | 0x802b88f4 | the session classes, per kirby per frame | Finish cells, per-course cells, Time Attack |
| Top Ride | `TopRide_CheckPerCourseObjectives_B` | 0x802b7dac | the same, per kirby per frame | Lap and item totals, Free Run laps, some course gimmicks |
| Top Ride | `TopRide_CheckSessionFinalizeObjectives` | 0x802b777c | `Singleton_GlobalDestructor` at session end | Goal and multiplayer totals, "5 s faster than #2" |

Air Ride's two driver tables pair handler pointers with clear kinds: Table A at 0x804975b8 /
0x80497674 and Table B at 0x80497630 / 0x80497694. A handler returns 1 when some player meets
it and the driver completes the cell; one handler may serve several cells, receiving the
clear_kind to pick its threshold. Every Table A handler that says "take 1st" first requires Air
Ride Race mode and a finished round - a lap race some human has completed, or a timed race whose
clock has run out - then a human with `player_finish_rank` 0.

The two dispatchers each have one call site: `AirRide_DispatchRaceTimeAttackObjectives`
(0x8004a994) runs from `race3D_isFinished` (`bl` at 0x80010d68) as each player crosses the line,
choosing the lap or Time Attack evaluator by `Gm_GetAirRideMode()`;
`AirRide_DispatchFreeRunObjectives` (0x8004a90c) runs from `AirRide_OnLapComplete` (`bl` at
0x80010418) after each Free Run lap. Top Ride's per-frame evaluators are called from
`TopRide_GameModeNormalUpdate` (0x8029c650, Race, Time Attack and Free Run) and
`TopRide_GameModeTuningUpdate` (0x802ac850), once per occupied slot after
`TopRide_KirbyMgrUpdate` has moved and ranked the kirbys.

## Whose play counts

The scope differs from cell to cell, and it is not visible in the cell text:

- Most per-game City Trial cells test each human (`Ply_GetPKind == PKIND_HMN`) alone, and so do
  the City Trial record sums: only humans' totals are added.
- Prop cells that mean "all of them" - every tree, rock, house, volcano hole - sum
  `yakumono_break` over all present players, CPUs included, as do the two timeout cells.
- Air Ride's lifetime accumulators add humans always and CPUs only in Time Attack.
- Top Ride's finalize evaluator adds nothing for a session that a human did not finish.

## PlayerStats fields the checklist reads

| Field | Written by | What it holds |
|---|---|---|
| `item_collect[ITKIND_NUM]` | `Ply_IncrementItemCollectNum` | Pickups per `ItemKind`; kinds 0-2 are box breaks |
| `item_collect_20s` | the same | Non-box pickups while `timer_20s` (seeded 1200) runs |
| `tac_stolen_items` | the same | Pickups whose source tag is 4, Tac's drops |
| `yakumono_break[kind - 0x15]` | `Ply_IncrementYakumonoBreakCount` | Breaks per `YakuKind` 0x15-0x28 |
| `ko_cpu_machine_broken`, `ko_firework`, `ko_gold_spike`, `ko_sensor_bomb` | `Ply_AddDeath` | KOs by cause |
| `vehicle_bust_mask` | `Ply_AddDeath` | One bit per bust pair, MSB first |
| `king_dedede_ko_frame` | `Ply_AddDeath` | Frame of the first KO of slot 4 |
| `enemies_defeated`, `enemy_defeat_by_method[]`, `enemy_defeat_by_kind[]` | `Ply_RecordEnemyDefeat` | Enemy defeats |
| `enemies_swallowed`, `swallow_by_actor[]`, `swallowed_this_race`, `consec_garbage_swallow` | `Ply_RecordEnemySwallow` | Inhale captures |
| `rival_hit_num`, `rival_hit_by_method[]` | `Machine_StoreAttacker` | Hits on rivals, by attack cause |
| `copy_obtain_count[]`, `copy_history[6]` | `Ply_RecordCopyAbility` (0x8022ee00) | Every copy grant, any source |
| `copy_chance_mask` | `Ply_MarkCopyAbilityObtained` (0x8022f150) | Copy Chance Wheel grants only; bit `15 - CopyKind` |
| `machine_mount_kind_num[]` | `Ply_IncrementGetOnMachineNum` (0x8022f5bc) | Boardings by machine |
| `total_time_spent_grounded` / `_airborne` | `Ply_TickTimeStats` (0x80231200) | Moving frames, split by the machine's airborne flag |
| `total_distance_grounded` / `_airborne` | `Ply_UnkUpdate` (0x80231340) | Distance, same split |
| `max_time_spent_airborne` | `Ply_TickTimeStats` | Longest airborne streak in frames |
| `flags_84c`, `flags_84d`, `flags_854`, `flags_855` | many | Per-game event bits: Dyna Blade, sky garden, legendary assembled, finish states |

The rest - sky rings, waterwheel rides, pillar times, course gimmick counters - are single
fields named for their cell in hoshi's `PlayerStats`.

## Recorders

- **`Ply_AddDeath` (0x8022f648)** is the KO recorder, reached only from `Machine_GiveDamage`
  (`bl` at 0x801e1f74) when a machine's HP crosses zero. Its first argument is the KO'd player,
  and the killer is the damage log's `attacker_ply`, which `Machine_StoreAttacker` (0x80231d90)
  writes along with the cause from the attacker's attack data. It credits the killer's
  kills-by-machine and the four KO-by-cause counters, sets the bust bit when the busted and
  ridden machines match a row of the ten-pair table at 0x804b4c68 (rows 8 and 9, Dragoon and
  Hydra busting each other, are written but no cell reads them), and stamps
  `king_dedede_ko_frame` for a KO of slot 4. A fall never reaches it.
- **`Ply_RecordEnemyDefeat` (0x8023205c)** is the enemy-side counterpart, reached only from
  `EventActor_ResolveHit` (0x802021fc, `bl` at 0x802022ec), which runs for every hit with
  knockback on an enemy - so a defeat is credited at hit time. `EventActor_ResolveHit` then
  credits the stadium enemy-KO count through `EventActor_CreditStadiumKO` (0x802025dc). Inhaling never reaches it: `Rider_InhaleCaptureScan` captures by
  its own scan and `Ply_RecordEnemySwallow` writes the swallow fields.
- **`Ply_IncrementItemCollectNum` (0x8022fbcc)** has one caller, `Machine_OnTouchItem`
  (0x801db34c, `bl` at 0x801db928), on the path every collected item and every box break takes,
  with the item's kind and spawn type. `Ply_DecrementItemCollectNum` (0x8022fb58) undoes a pickup
  when a rider sheds patches; it never touches the Tac count. `Ply_GetItemCollectTotal`
  (0x8022f920) sums kinds 3-0x43, every kind but the boxes and the Weight fake, and
  `Ply_GetBoxCollectTotal` (0x8022f9bc) kinds 0-2.
- **`Ply_IncrementYakumonoBreakCount` (0x8022fed8)** has one caller,
  `YakumonoGObj_IncrementBreakCount` (0x80105d80, `bl` at 0x80105da0), through which every
  breakable prop credits its break to a player. For `YAKUKIND_EVENTPILLAR` it also records the
  fastest pillar break. A bucket names a prop kind, not a stage, so its meaning depends on where
  the round is: `BREAKFLOOR` is the forest pitfall in the city and the ice platforms on Frozen
  Hillside. There are two corals: Sky Sands' `BREAKCORAL` (24) and the city's `CORAL` (33),
  which no vanilla cell reads.

## Units and conversions

| Quantity | Unit | Conversion |
|---|---|---|
| Times | frames at 60 per second | 2 minutes = 7200 |
| Stadium distance (`GameData.player_race_distance[p]`) | metres | feet = m / 0.3048 |
| City Trial distance (`total_distance_grounded` + `total_distance_airborne`) | world units | miles = units * 11.4285717 / 160934.4 |
| Air Ride speed | world units per frame | mph = units per frame * 15.339 |
| Stadium score (`GameData+0xa38[p]`) | per stadium | Target Flight points, Destruction Derby rival KOs, Kirby Melee enemy KOs |

The stadium score array is what hoshi names `GameData.destruction_derby_ko_num`; `Game_Think`
refreshes it per stadium kind, so its meaning follows the stadium being played. Drag times are
`GameData.player_finish_time[p]`. A machine in a drag cell is matched by
(`Ply_GetMachineIsBike`, `Ply_GetMachineKind`), a per-category index (stars and bikes each count
from 0), not a `MachineKind`.

## Quirks a reader trips over

- **"Play in over 10 stadium modes"** counts stadiums unlocked, not played:
  `Gm_StadiumIsAvailable` (0x8000c228) is an unlock check and the game keeps no played bit.
- **Air Ride distance cells** run only for a timed race whose limit is set to exactly 120
  seconds (`Gm_GetRaceTimeLimitSeconds`, the configured limit), reading
  `Gm_GetPlayerRaceDistance` live. Nebula Belt's case leaves the target at 0, so it has no such
  cell.
- **Air Ride lap-time cells** run only when the race's lap total equals the course's default
  (`stGetCurrentStageKind_DefaultLapNum`), so a longer race cannot pay out a 2-lap time.
- **Fantasy Meadows' 20 mph cell** uses two bits. `AirRide_TrackMinLapSpeed` (0x80231670), run
  per frame only on Fantasy Meadows, clears `flags_84c` bit 4 whenever the machine's measured
  per-frame displacement (`MachineData.world_velocity`) drops below 1.303867 units, or the rider
  is on foot. At each lap boundary `AirRide_TrackLapRank` (0x8022e88c) copies bit 4 into bit 5,
  which the cell reads, and `AirRide_ArmLapStats` (0x8022ea10) re-arms bit 4. Lap 1 starts from
  a standstill, so it never counts.
- **Air Ride finish states** (`flags_854`, `flags_855`) are all written by
  `AirRide_RecordFinishStats` (0x8022e4e0) as the player crosses the line; the ability bits read
  the rider's live `copy_kind`. "Spinning" means `MachineGObj_IsQuickSpinning` (0x801c7b00),
  which only a real Quick Spin sets - spin panels and forced spins do not.
- **Machine changes** count a boarding only when the machine differs from the one the rider
  last respawned on, per machine object rather than per kind.
- **Top Ride lap gaps.** "1 lap between you and #2" requires every rival's lap counter to trail
  the winner's by at least 2 at the finish, and "2 laps" by at least 3.
- **Top Ride course order.** `TopRide_GetSelectedCourse` numbers the courses Grass, Sand, Sky,
  Fire, Light, Water, Metal. The descriptor table at 0x8048a028 (stride 0x28) is in that order:
  `+0x00` the course's first per-course cell, `+0x04` the lap-time target, `+0x08`/`+0x0C`
  Time Attack targets with their cells at `+0x10`/`+0x11`, `+0x14`/`+0x18` Free Run targets with
  cells at `+0x1C`/`+0x1D`, `+0x20` the 300-frame "5 s faster" gap and `+0x24` its cell. The
  per-course cell blocks themselves run Grass, Sand, Sky, Fire, Water, Light, Metal, and the
  Time Attack and Free Run cells another order again.
