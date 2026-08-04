# AP Checklist (a custom checklist tab)

The **AP checklist** is the archipelago mod's custom checklist tab, registered on the
custom_checklist framework. Its cells are AP *locations* — objectives tracked by mod
code — and completing one sends an AP location check exactly like a vanilla checkbox;
multiworld items can be placed *on* its cells for display.

The framework owns the presentation (synthetic-mode plumbing, minor scene, grid build,
theme recolor, banner/emblem swap) and the per-frame check evaluation. This doc covers
only the AP-specific wiring — `mods/archipelago/src/ap_checklist.c`, with the AP-side
reward/check integration in `checklist_rewards.c` / `check_detection.c`.

## Two Mode Identities

`GMMODE_NUM` (3) means "the three real game modes". It still sizes the reward tables,
because the AP tab awards no native rewards of its own.

Per-checklist-mode *recorded* state is one row wider: `CHECKLIST_MODE_NUM` (4), with the
AP tab always at the fixed row `AP_CHECKLIST_ROW` (== `GMMODE_NUM` == 3). That row index
is compile-time constant and is what the wire and the save both store.

The tab's *runtime* mode is different and is assigned dynamically: the framework appends
each registered tab to the next free mode index, so the AP tab's mode is whatever slot it
lands on, `>= GMMODE_NUM`. `APChecklist_Register` stores it in the global
`ap_checklist_mode` (defaulting to `GMMODE_NUM`).

`ChecklistModeRow(mode)` in `check_detection.h` maps a runtime mode to its row, returning
`-1` for a checklist mode this mod does not record (`ChecklistRowMode(row)` goes back, for
the game APIs like `gmGetClearcheckerTypeP` that index by runtime mode). It is the single
answer to "which row is this mode" — consumers call it rather than re-deriving the mapping.
So registration order across mods does not matter and nothing depends on the tab being
exactly mode 3.

## What the AP Mod Supplies

`APChecklist_Register` (called from `OnSaveLoaded`, after the framework mod has exported
its API) imports `CustomChecklistAPI` and hands it a descriptor with:

- **Checks** — a static `{ clear_kind, label, predicate }` table covering all 50
  objectives, pinned to `APCK_NUM` by a `_Static_assert`. A label is the in-game cell text
  and is also the box's AP location name minus the leading `Archipelago: ` — the same
  relationship the three vanilla tabs have to their location names, so the wording is
  identical on both sides and `APLocation` in the apworld restates it by hand. They follow
  the vanilla checklist's wording — a title-case category prefix (`City Trial: `,
  `Stadium: `, `Air Ride: `), the stadium or course name in caps carrying no colon of its
  own, and a trailing `!`. The framework composes each into a fixed 160-byte SIS entry at 2
  bytes per character and 1 per space or break, truncating silently once past byte 157; the
  longest label spends 137 including its terminator. The 49 labels that need two lines place
  the break themselves as a `\n`, after the category and stadium designation the way vanilla
  breaks its entries — the framework's fallback split balances on width alone and would part
  a stadium name from its number (`SINGLE RACE / 8 Finish in 1st place!`). Where the
  objective is too long for that (`KIRBY MELEE 1`, both photo finishes) the break moves to
  wherever balances the two lines, since the cell holds only two and the engine squeezes an
  over-wide one. Most lines run 18–35 characters, above the ~25 that renders at full size and
  below vanilla's widest *unsqueezed* line of 37; the one single-line label is 29 characters.
  The two Destruction Derby labels reach 41 on their second line: the first restates vanilla's
  own DD cell verbatim, breaks included, and the second matches its shape. Every printable
  ASCII character in the labels maps through `Text_CharToCommand`, including `!`, `:` and the
  `(All)` parentheses — an unmapped one would be dropped without warning. Every predicate is a
  single `APCheckDetect_IsSet(ck)` call; the detection itself lives in `ap_check_detect.c`.
- **Theme** — blue (`AP_CHECKLIST_NAME` / `AP_THEME_*` in `ap_checklist.h`, shared with
  every textbox that names the tab so the wording and tint stay one value; the tab's runtime
  mode is `>= GMMODE_NUM` and so has no `ModeColors[]` slot of its own).
- **Tab art** — `ApChecklistTex` (below).
- **Persistence callbacks** — `is_recorded` / `record_complete` (below); the AP tab owns its
  storage because it routes to a wire field.
- **Readiness** — `is_ready` returns `ap_data && ap_data->game_ready`. The framework's
  evaluator no-ops until it holds, so `record_complete`'s textbox enqueue is safe.

The framework's evaluator runs from its own `OnFrameStart`, so every predicate is polled
every frame in every scene. Predicates must be cheap pure reads that are safe outside
gameplay.

## Recording a Completion

The descriptor's callbacks bind the framework's presentation to AP's authoritative record:

- `is_recorded(clear_kind)` reads `ap_save->sent_checks[AP_CHECKLIST_ROW]` (recorded =
  permanently complete, shown with no replay on a later boot). An out-of-range `clear_kind`
  reports "done", so the framework never tries to complete it.
- `record_complete(clear_kind)` calls `ClearChecker_SetNewUnlock(ap_checklist_mode,
  clear_kind)`, which `check_detection`'s `CODEPATCH_REPLACEFUNC`
  (`CheckDetection_SetNewUnlockReplacement`) intercepts for `ap_checklist_mode`: on a fresh
  cell it runs `RecordCheck`, which resolves the row via `ChecklistModeRow`, sets the
  `sent_checks` bit, fires the "Check sent" textbox and re-evaluates goals — and, mid-run
  (unlock cache invalid), sets `clear[].is_new` and plays the unlock SFX. The framework
  seeds the cell's `is_new` afterward so the flip-and-sparkle runs even for a check that
  lands outside a gamemode — a stadium objective latched at `On3DExit`, say — which hits the
  cache-valid short-circuit.

So the AP tab's completion path is unchanged from a plain checklist objective:
predicate → `ClearChecker_SetNewUnlock` → `check_detection` → `sent_checks` row → AP.
The framework only adds the cell flags and animation around it.

`check_detection` reads and writes the AP cells through
`gmGetClearcheckerTypeP(ap_checklist_mode)`, which the framework serves from the AP tab's
`GameClearData` block — so the AP record path and the framework presentation operate on the
same block.

The tab's board starts blank and reveals outward, exactly as the vanilla checklists do: a
cell draws only once it is completed, or once a completed orthogonal neighbour has revealed
it. The reveal is positional, so with only 50 of the 120 clear_kinds carrying a check it
will surface boxes that have no objective behind them, and cells whose only neighbours are
undefined stay dark until their own check fires. That resolves itself as the tab fills out
toward 120.

`APChecklist_RevealAll` is the exception: the `reveal_checklists` slot option (applied when
the client hands over its options) and the debug menu's "Reveal All Checklists" both route
through `RevealAllChecklists`, which reveals all 120 cells of each vanilla mode and then
calls into the AP tab. The AP tab reveals only the cells in `ap_checks[]` — the other 70
have no objective behind them, so revealing them would show boxes that can never be checked.
It sets `is_visible` only, leaving `is_unlocked` to the normal completion path, and no-ops
when the framework never registered the tab. The framework clears `is_visible` only in its
layout shuffle, which runs once per tab per session, before either caller.

## Wire Layout

`APData` is read by the Python client *by field offset*, so its layout is a contract. Every
per-checklist-mode array in `APData` / `APSlotOptions` / `APSave` is `CHECKLIST_MODE_NUM`
wide (`goal`, `checklist_amount`, `goal_checks`, `sent_checks`, `client_backfill`,
`goal_announced`), with the AP tab's entry at `AP_CHECKLIST_ROW` — a regular row, not an
appended tail field. The client never sees the framework-assigned mode number; it indexes
the AP row directly, which is why the runtime mode can move freely.

A block of `_Static_assert(offsetof(APData, f) == 0xNNN, "")` in `main.c` pins the block
boundaries and the row-0 / AP-row offsets of the per-mode arrays. Nothing else checks the
layout — the compiler picks the offsets and the client restates them by hand — so extend
that block when adding fields, and move both sides together.

`APSave` is not part of that contract — the client never reads it — so it grows freely.
Objectives whose predicate counts across boots keep their progress in `APSave.checks`, an
`APCheckProgress` struct sitting at the end of `APSave`, away from the unlock masks: today
`allup_collect_total` (clear_kind 4), `purple_sr1_wins` (clear_kind 27) and
`race_color_mask` (clear_kind 30, one bit per `KirbyColor`), with their targets as
`AP_ALLUP_TOTAL_NEED` (5) / `AP_PURPLE_SR1_NEED` (3) / `AP_RACE_COLOR_MASK_ALL` (`0xFF`) in
`main.h` so the counter that stops incrementing and the predicate that reads it share one
value. Grouping them means a new counting objective adds a field to that struct rather than
another loose scalar in `APSave`. An objective satisfiable within one run needs nothing here
— it latches in `ap_check_detect.c`'s transient `ap_observed` bitmask and records through
`sent_checks`.

Only clear_kinds 0–49 back an AP location, but the tab's grid is 120 cells and the
checkbox-filler cursor can reach the blank ones. `RecordCheck` rejects `clear_kind >=
APCK_NUM` on the AP row so a spent filler can't send a location code the multiworld has
never heard of.

## Cross-Mode Rewards

`ChecklistRewards_ApplyCrossModeHasReward` (the post-reward-loop hook at `0x8017e07c`)
resolves its row with `ChecklistModeRow` and returns early on `-1`, so the AP tab hosts
cross-mode rewards like any other mode while any *other* custom tab — which has no row — is
skipped. `cross_mode_slots` is `CHECKLIST_MODE_NUM` wide to match.

`RebuildRewardTablesFromShuffle` is the one place that must *not* call `ChecklistModeRow`:
the wire encodes a reward's target as a checklist-mode **row** already (the client writes
`KARData.GameMode`, whose `ARCHIPELAGO` member is `AP_CHECKLIST_ROW` by definition), so it
bounds-checks the value against `CHECKLIST_MODE_NUM` instead of mapping it.

## Tab Artwork

`mods/archipelago/assets/ApChecklistTex.dat` is a loadable HSD archive (staged to the FST
root) exporting two `_HSD_ImageDesc` publics:

- `apBannerImg` — RGB5A3 248×128 panel that backs the checkbox grid, the AP logo baked in
  as a faint watermark so the panel stays opaque under the grid.
- `apEmblemImg` — I4 64×64 intensity map of the logo for the top-right tab indicator;
  intensity doubles as alpha and the quad takes the blue theme tint. (The framework finds
  the *vanilla* emblem TObj by its 40×40 I4 signature, then repoints it here; the
  replacement's own size is unconstrained.)

`scripts/hsd/make_checklist_textures.py` authors the archive from
`mods/archipelago/assets/ap-icon.png`
(`uv run --with pillow python scripts/hsd/make_checklist_textures.py`). The framework loads
it by name (`tex_file = "ApChecklistTex"`) per tab build and swaps the checklist's
banner/emblem TObjs onto these descriptors.

## Objectives and Detection

There are 50 objectives, `clear_kind` 0–49, enumerated as `APCheckKind` in
`ap_check_detect.h`. The numbering is a cross-repo wire contract — the AP location code is
`361 + clear_kind`, and `APLocation` in the apworld's `KARLocations.py` restates the same
order and the same label text by hand, with nothing mechanically catching a desync. An AP box
whose check never fires is worse than one that doesn't exist: the location still exists in the
multiworld and logic still treats it as reachable, so fill can strand progression on it.

Every objective is an in-game achievement. There is no box for booting the game or for
receiving a multiworld item — those complete without playing, so as AP locations they were
free checks the fill could hide progression behind.

Predicates never sample. The framework polls all 50 every frame, in every scene, including
menus and loads — so each one is a single read of state latched elsewhere, and the sampling
lives in five seams in `ap_check_detect.c`.

**`On3DExit` — stadium and Air Ride results.** hoshi installs the hook at `0x80015274`,
which is the epilogue of `Stadium_ExitMinor` (`0x80014d5c`), the very function whose copy
loop at `0x80015164` latches `GameData.stadium_results` from the live result arrays. So the
block is final and complete when the hook runs. The loop runs `p = 0..3` unconditionally and
`Stadium_ComputeRankByTime` / `ByPoints` / `ByDistance` rank CPU racers alongside humans,
which is what makes the photo-finish boxes solo-achievable. The latch is skipped entirely for
a replay (`GameData.is_replay`) and for the title-screen demo, leaving the previous round's
values in place, so `is_replay` is checked before reading. Per-slot, `StadiumResults.xc00[p]`
must be `0` — the same gate the rankers use — or that slot's placement and time are stale.

| clear_kind | Objective | Detection |
|---|---|---|
| 13–21 | SINGLE RACE 1–9 finish 1st | `ply_finished[p] && ply_placement[p] == 0` with at least one opponent |
| 22 | HIGH JUMP over 1,500 ft | `ply_dist[p] / 0.3048 > 1500` (`ply_dist` is metres) |
| 23 | AIR GLIDER over 2,000 ft | `ply_dist[p] / 0.3048 > 2000` |
| 24 / 25 | KIRBY MELEE 1 / 2 KOs | `ply_points[p] > 100` / `> 60` (the field is a polymorphic score; for Melee it is the KO count) |
| 42 | DESTRUCTION DERBY 3 KO a rival 10× | `ply_points[p] >= 10` on `STKIND_DESTRUCTION3`. For a derby the polymorphic score is `GameData.destruction_derby_ko_num[p]`, which is exactly what the vanilla DD cells count, so this reads the same number vanilla's own DD 3 cell does |
| 26 | SINGLE RACE 1 1st on Bulk Star | placement + an opponent + `Ply_GetMachineKindAbs(p) == VCKIND_BULK` |
| 27 | SINGLE RACE 1 1st 3× as Purple | placement + an opponent + `rider_kind == RDKIND_KIRBY` + `Ply_GetColor(p) == KIRBYCOLOR_PURPLE`, counted in `APSave.checks.purple_sr1_wins`. The rider-kind test is required because `Ply_GetColor` reads `PlayerDesc.color`, which is only a `KirbyColor` for a Kirby rider, and the stadiums are reachable from a Dedede match |
| 28 | Photo finish in any DRAG RACE | a human and any other finisher with `ply_race_time` within 6 frames (0.10 s at 60 fps), on any of `STKIND_DRAG1`–`STKIND_DRAG4` |
| 29 | Photo finish on any Air Ride course | same pairing, gated on `MJRKIND_AIR` + `AIRRIDEMODE_RACE` and not looking at the course |
| 30 | Finish an Air Ride race as every Kirby color | per human `RDKIND_KIRBY` slot with `ply_finished[p]`, `1 << Ply_GetColor(p)` OR-ed into `APSave.checks.race_color_mask`; the predicate wants all 8 bits. Same `MJRKIND_AIR` + `AIRRIDEMODE_RACE` gate, and the same rider-kind test box 27 needs. A player with no color unlocked rides Pink, so Pink can latch unowned - harmless, since the apworld requires all 8 color items to reach the box |
| 35 / 36 | Air Ride 1st place as Meta Knight / King Dedede | `won` + `ply_desc[p].rider_kind == RDKIND_METAKNIGHT` / `RDKIND_DEDEDE`, on any course |
| 37 | NEBULA BELT finish 1st | `won` |
| 38 | NEBULA BELT over 5,500 ft in 2 minutes | `Gm_GetCityKind() == AIRRIDE_RULE_TIME` + `Gm_GetRaceTimeLimitSeconds() == 120` + `Gm_GetPlayerRaceDistance(p) / 0.3048 >= 5500` |
| 39 | NEBULA BELT 2 laps under 02:30:00 | `Gm_GetCityKind() == AIRRIDE_RULE_LAPS` + `Gm_GetRaceLapTotal() == 2` + `ply_race_time[p]` nonzero and `<= 9000` frames |
| 40 | NEBULA BELT 1st on Wheelie Scooter | `won` + `Ply_GetMachineKindAbs(p) == VCKIND_WHEELIESCOOTER` |
| 41 | NEBULA BELT airborne over 10 s on a flight machine | `Ply_GetMachineKindAbs(p)` in `VCKIND_DRAGOON` / `VCKIND_FLIGHT` / `VCKIND_WINGED` + `Ply_GetItemCollectArray(p)->airborne_time > 600` frames |

Boxes 37–41 are gated on `in_nebula`, latched at `On3DLoadEnd` from `Gr_GetCurrentGrKind() ==
GR_SPACE2`. That is the loaded terrain rather than `GameData.stage_kind`, for the same reason
the Fantasy Meadows path below uses it — the stage field is the menu's course selection and
goes stale outside a race. It has to be captured at load because the results sampler that
reads it does not run until the round is already over. `won` means `ply_finished[p] &&
ply_placement[p] == 0` with at least one opponent, the same three-part test boxes 26 and 27
use — an Air Ride race can be started with no CPUs, where 1st place is free the moment the
player crosses the line.

Boxes 38 and 39 copy the gates vanilla puts on its own per-course cells rather than inventing
looser ones, so the demand on the player is the one the other eight courses already make.
`AirRide_CheckRaceDistanceObjectives` (`0x8004d454`) tests `Gm_GetRaceTimeLimitSeconds() ==
120` for its "Race over N feet in 2 minutes!" cells, and its caller
`AirRide_CheckRaceFinishObjectives` (`0x8004aa58`) only calls it when `Gm_GetCityKind() == 1`
— so the race has to be a *timed* one set to 2 minutes, not a lap race that happens to leave
the rules menu's time field at its 2:00 default. `AirRide_CheckRaceLapObjectives`
(`0x8004d248`) likewise keys its "Finish N laps in under MM:SS:FF!" cells off the *configured*
lap total, not laps completed, so a longer race cannot pay out the 2-lap time.

`airborne_time` (`PlayerStats+0x5f4`) is the longest single airborne stretch, not a total —
`airborne_streak` accumulates consecutive frames and feeds it as a running maximum.
`Player_InitAll` zeroes `PlayerStats` on the next 3D scene load, so at this hook it still
holds the race that just ended.

Placement alone does not mean a win: `Stadium_ComputeRankByTime` (`0x800108b0`) ranks slots
that never crossed the line as well, falling through to a `GameData.player_race_distance`
comparison when neither slot has its finished flag set. So a Single Race abandoned while the
human led on distance latches `ply_placement == 0`. The 1st-place boxes therefore require
`ply_finished[p]` too — which matters most for `checks.purple_sr1_wins`, a persistent counter
with no way back down.

`ply_race_time == 0` is a DNF and is excluded before pairing, or two non-finishers read as a
perfect photo finish. In the time-metric modes `Stadium_ExitMinor` *projects* a finish time
for a CPU that didn't cross the line, so such a CPU appears as a finisher with an
extrapolated time — consistent with what the game's own results screen shows.

One side of the pair must be a `PKIND_HMN` slot. The other may be a CPU, which is what keeps
these objectives solo-achievable, but requiring the human means two CPUs finishing together
while the player trails behind does not award the box.

Every 1st-place box (13–21, 26, 27) also requires at least one other racer — a `PKIND_HMN` or
`PKIND_CPU` slot, other than the winner, that passes the same `xc00[p] == 0` gate the rankers
use. Stadium modes reached from the Stadium menu rather than from the end of a City Trial
round can be started with no CPUs at all, and with an empty field these boxes would check
themselves the moment the player crossed the line.

The two machine-specific boxes (26, 40) go through `Ply_GetMachineKindAbs` rather than
`Ply_GetMachineKind`. `PlayerData.machine_kind` (+0x8F) is a class-relative index paired with
`is_bike` (+0x8E) — it selects an entry in one half of `vcDataLookup`'s `data[2][19]`, so it
equals the `MachineKind` only for the 19 stars. The seven bikes count from 0 again, which puts
Wheelie Scooter at 4 rather than `VCKIND_WHEELIESCOOTER`; compared raw, box 40 is unreachable
and box 26 also matches King Dedede's wheelie (bike index 5, the same number as
`VCKIND_BULK`). The helper adds `VCKIND_WHEELNORMAL` back for a bike.

Both per-frame procs below are attached by `AttachSamplers`, which walks the five player
slots and hangs the proc on every `PKIND_HMN` rider's GObj at `RDPRI_HITCOLL + 1`.

**A per-frame proc on each human rider — the City Trial objectives.** Attached from
`On3DLoadEnd`, and only for `Gm_IsInCity() && Gm_GetCityMode() == CITYMODE_TRIAL`, since "in
one game" means one CT Trial run. Counters baseline on the first frame where
`Gm_GetIntroState() == GMINTRO_END`, so patches applied at round start — including permanent
ones an Archipelago item grants — are not read as a collection.

"One game" here is one city segment: `SceneLoad_3D` calls `Player_InitAll` on every 3D scene
load, which memsets all five `PlayerData` slots and so zeroes `item_collect` and
`yakumono_break`. A stadium trip mid-trial therefore restarts these counters. That is the
same scope the vanilla City Trial cells use.

| clear_kind | Objective | Detection |
|---|---|---|
| 0 | Visit the flower on top of Castle Hall on foot | `foot_visit_checks[]`: `!Rider_IsOnMachine(rd)` and `rd->pos` within 2 units of the flower, at `(408.7, 370.8, -564.6)`. The flower sits on a very small platform, and the stage's out-of-bounds box spans 2600 units in X and Z, so the sphere is tight. The on-foot requirement stops a machine flying through the spot from counting. |
| 1 | Break all the coral in one game | `PlayerStats.yakumono_break[33] >= Gr_GetYakumonoSpawnTotal(33)`. Coral is yakumono descriptor 33 and GrCity1 places 10; the total is read from the stage rather than hardcoded, exactly as the vanilla Sky Sands "break all coral" cell does. `yakumono_break` is zeroed per game, so no baseline is needed. |
| 2 | Go out of bounds | `calcDistanceFromOOB(&rd->pos) < 0` — the engine's own definition, the condition that makes `Machine_CheckFallDeath` respawn the player. |
| 3 | 10+ HP Patches in one game | per-run delta of `item_collect[ITKIND_HP]` |
| 4 | Collect 5 All Ups in total | frame deltas of `item_collect[ITKIND_ALLUP]` fold into `APSave.checks.allup_collect_total` |
| 5–12 | Eat 3+ of each of 8 foods | per-run delta of `item_collect[ITKIND_FOOD*]` |
| 31 | Visit the model city on foot | the second `foot_visit_checks[]` entry: within 10 units of `(-422.7, 12.7, -168.9)`, on foot. The model sits on open ground rather than a ledge, so the sphere is wide enough to cover standing anywhere on it. |
| 32 | Visit the flower on top of the volcanic cliffs on foot | the third `foot_visit_checks[]` entry: within 5 units of `(-107.0, 205.1, -847.3)`, on foot. The flower sits on the cliff top, reachable on foot from the surrounding terrain, so the sphere is the same size as the sky garden's rather than the tight one Castle Hall's platform needs. |
| 33 | Visit the top of the garden in the sky on foot | the fourth `foot_visit_checks[]` entry: within 5 units of `(-67.9, 463.8, -0.3)`, on foot. Vanilla's own "Make your way to the garden in the sky!" cell only asks the player to reach the garden, so the sphere sits on the top surface rather than anywhere on the structure. |
| 34 | Fly to the highest point possible | `rd->pos.Y >= AP_MAX_ALTITUDE_Y` (1000). A climb into the city's ceiling stops at 1040.3 — a collision, not an apex: vertical velocity is zeroed in one frame and the fall that follows is exactly the stage's `gravity_strength` of 0.025/frame. That ceiling is 460 below `StageNode.oob_max.Y` (1500), so the out-of-bounds lid is never what stops the climb and `calcDistanceFromOOB` cannot measure this. The threshold's 40-unit margin means the contact frame need not be sampled, and it sits far above the sky garden at 464, the highest place reachable without flying. |
| 44 | Get the Mic ability from the Copy Chance Wheel | `PlayerStats.copy_chance_mask & COPY_CHANCE_BIT(COPYKIND_MIC)`. Only `Rider_MarkCopyAbilityObtained` (`0x8022f150`) sets that mask, and only the two copy-wheel paths call it (`randomAbility_aPress` `0x801ae874`, `randomAbility_autoSelect` `0x801ae910`) — so a Mic panel picked up off the ground does not satisfy it, the same wheel-only demand vanilla's Bomb and Sleep cells make. The mask is MSB-first, bit `15 - CopyKind`. |
| 46–48 | Break 20 blue / 10 green / 10 red boxes in one game | per-run delta of `item_collect[ITKIND_BOXBLUE/GREEN/RED]` — `ItemKind` 0/1/2 *are* the three box colors, and a break bumps the array the same way a pickup does. Vanilla counts boxes only as an all-colors lifetime total (`CityTrialClearRecords.box_total`, its 500/1000 cells), so per-color counts are unclaimed. The thresholds are unequal because the colors are: `GrCity1`'s 9-entry `box_spawn_chances` table rolls blue 45/71, red 14/71 and green 12/71 |

`item_collect` is bumped by `Ply_IncrementItemCollectNum`, which `Machine_OnTouchItem` calls
for every item application. That includes patches an Archipelago item spawns —
`SpawnItemPlayer` calls `Machine_OnTouchItem` directly to force a same-frame pickup — so an
All Up or HP Patch **received from another world counts**. That is deliberate: the player sees
the pickup happen, and a box that fires too readily is the safe failure direction.

**A per-frame proc on each human rider — the Fantasy Meadows shortcut.** The second
`On3DLoadEnd` attach path, taken when `Scene_GetCurrentMajor() == MJRKIND_AIR` and
`Gr_GetCurrentGrKind() == GR_PLANTS1`. It gates on the *loaded terrain* rather than
`Gm_GetCurrentStageKind()`, because that field is the menu's course selection and holds a
stale value outside a race — it reads 43 in Free Run on Fantasy Meadows, where the ground
correctly reads `GR_PLANTS1`. `GrPlants1` is Fantasy Meadows and nothing else, in every Air
Ride mode. The mode is deliberately unconstrained: the label carries no mode prefix, unlike
vanilla's `TA:` and `FR:` cells, so Race, Time Attack and Free Run all count.

| clear_kind | Objective | Detection |
|---|---|---|
| 49 | FANTASY MEADOWS take the shortcut | `rd->pos` within 25 units of `(249.7, 120.0, 19.2)`. Riders are always on a machine in Air Ride, so unlike the city visits there is no on-foot test |

The shortcut is an elevated arc over the normal racing line, peaking near `(255, 135, 6)`;
it is the lap's only route divergence. The sphere sits on the arc's descent rather than at
its apex because one ball has to cover two things 16 units apart: the surface a machine can
come to rest on, and the higher line a machine carrying speed flies through the same stretch.
The 25-unit radius covers both and still leaves 26 units of clearance to the closest point of
the normal racing line, which passes 51 units away and 40 below. A machine crosses the sphere
at roughly 3 units per frame and so spends about fifteen frames inside it, far too many to
skip between frames.

**The rival KO recorder — the Destruction Derby box.** `APCheckDetect_OnBoot` repoints the
single `bl Ply_AddDeath` inside `Machine_GiveDamage` (`0x801e1f74`) at a wrapper that runs the
vanilla recorder and then counts. `Ply_AddDeath` (`0x8022f648`) is the engine's unified
KO-event recorder, reached only from that one call site — where a machine's HP crosses zero —
and it is the only place the KO'd rider is named: its first argument is the ply riding the
destroyed machine and `dmg_log->attacker_ply` (`MachineData.dmg_log` + 0x1c) is the killer.
The per-player tally the DD cells read, `GameData.destruction_derby_ko_num[p]`, records the
killer alone.

| clear_kind | Objective | Detection |
|---|---|---|
| 43 | As King Dedede, KO 10 Kirbys in one derby | gated on `Gm_IsDestructionDerby()` (`Gm_GetCityKind() == 14`, so any of DD 1–5); counts a KO whose killer is a `PKIND_HMN` slot with `Ply_GetRiderKind == RDKIND_DEDEDE` and whose victim is a different slot with `RDKIND_KIRBY`. The counter is per game, reset in `On3DLoadEnd` alongside the City Trial baselines |

Testing the victim's rider kind is not a formality. A stadium CPU draws its character from the
gated select grid, so once King Dedede or Meta Knight is unlocked — and this box needs Dedede
unlocked — a rival can be one of them rather than a Kirby. The player can assign each CPU a
machine from that grid, so an all-Kirby field stays arrangeable.

**The enemy-defeat recorder — the Mic count.** `Ply_RecordEnemyDefeat` (`0x8023205c`) is the
enemy-side counterpart of `Ply_AddDeath`: it credits a player with an enemy kill, bumping
`PlayerStats.enemies_defeated`, the per-ACTORID defeat counter and
`enemy_defeat_by_method[]`. Like the rival recorder it has exactly one call site
(`0x802022ec`), so `APCheckDetect_OnBoot` repoints that `bl` at a wrapper the same way.

| clear_kind | Objective | Detection |
|---|---|---|
| 45 | KIRBY MELEE (All): KO 10 enemies as Mic Kirby in one game | gated on a loaded KIRBY MELEE round (`Scene_GetCurrentMajor() == MJRKIND_CITY`, `Gm_GetCityMode() == CITYMODE_STADIUM`, `Gm_GetCurrentStadiumKind()` of `STKIND_MELEE1`/`STKIND_MELEE2`), latched in `On3DLoadEnd`; counts a defeat credited to a `PKIND_HMN` slot whose rider holds `COPYKIND_MIC` and is in action state `RIDERSTATE_MIC_SING` (`0x61`) or `RIDERSTATE_MIC_END` (`0x62`). The counter is per game, reset in `On3DLoadEnd` alongside the Destruction Derby one |

The rider's live state is what identifies the blast, not the attack-method index the
recorder itself keys off. That index — byte 3 of the attacker log — is what vanilla's own
ability cells read back out of `enemy_defeat_by_method[]` (`0xe` Tornado, `0xf`/`0x15`
exhaled star, `0x10` Quick Spin), and it would be the tighter signal, but the Mic's index is
not identified: `ability_Mic` (`0x801b3dac`) installs no hitbox of its own, and the rider's
single `TriggerData` (`RiderData+0x674`) takes its cause from the rider archetype once at
`Rider_Create`. Reading the state instead means a *ram* kill landing inside the blast
animation also counts — the same direction of error the item-collect objectives accept, and
the blast window is short next to the 10 kills the box asks for.

KIRBY MELEE 1 and 2 are the only City Trial contexts that spawn the regular AI enemy pool -
the open city, Free Run and every other stadium ship an empty or NULL enemy-spawn array and
produce event actors only (TAC, Dyna Blade, Event Gordo, Meteor). That is what scopes the
box to the melee stadiums rather than to City Trial at large.

Neither Mic box needs an Archipelago item to be satisfiable: the wheel one takes the ability
off the Copy Chance Wheel in the city, and the melee one off a swallowed Walky - so the
apworld gates it on the inhale base ability as well as the Mic unlock. A melee round has no
other Mic source: neither `GrPasture1` nor `GrColosseum5` ships an `ItemNode`, so no copy
panels spawn there.

Of the two, **only KIRBY MELEE 2 can supply the ability**. `GrColosseum5`'s spawn table
carries Walky as `ACTORID_T1_WALKY` (`0x2D`) across 54 of its 285 positions - four by direct
reference (weight 5/100) and the rest through meta-groups `0x53`/`0x55` - which works out to
roughly 0.26% of the enemies that actually spawn. `GrPasture1`'s 28-entry mode-2 table
contains no Walky of any tier, and mode 2 does no meta expansion that could introduce one, so
KIRBY MELEE 1 can never satisfy the box. That is why the apworld's location sits in the KM2
region while the mod's sampler accepts either stadium.

### The apworld's location range

Every one of the 50 clear_kinds latches, and every one backs an AP location. The apworld's
codes run contiguously from 361 to 410, and `ArchipelagoChecklistAmount` has `range_end` 50
to match — a data-integrity test pins that option to the table's real size, so both move
together when a box is added.

Each box takes the region where its activity happens rather than a flat Archipelago region,
so it inherits that region's entrance chain (stadium unlocks, course unlocks, the DD/KM/DR
prerequisite chains) instead of restating them as item rules. Only item-spawn dependencies
are written out by hand — the Mic ability for the two Mic boxes, a City Trial machine for the
boxes that need something to ride, the eight colors for the all-colors race.

## Files

- `mods/archipelago/src/ap_checklist.c` / `.h` — the AP descriptor (checks + labels, blue
  theme, tab art, `is_recorded` / `record_complete` / `is_ready` callbacks) and
  `APChecklist_Register` / `APChecklist_RevealAll`.
- `mods/archipelago/src/ap_check_detect.c` / `.h` — `APCheckKind`, the sampling seams
  (`APCheckDetect_On3DExit`, the City Trial and Fantasy Meadows per-frame procs, and the
  `Ply_AddDeath` / `Ply_RecordEnemyDefeat` interceptions `APCheckDetect_OnBoot` installs),
  and `APCheckDetect_IsSet`.
- `mods/archipelago/assets/ApChecklistTex.dat` — the AP banner/emblem archive.
- `scripts/hsd/make_checklist_textures.py` — authors `ApChecklistTex.dat`.
- `mods/archipelago/src/main.h` / `main.c` — the wire structs, `CHECKLIST_MODE_NUM` /
  `AP_CHECKLIST_ROW`, the runtime `ap_checklist_mode`, and the offset assertions.
- `mods/archipelago/src/check_detection.c` / `.h` — `ChecklistModeRow` / `ChecklistRowMode`,
  and the `ClearChecker_SetNewUnlock` REPLACEFUNC that records AP completions.
- `mods/archipelago/src/checklist_rewards.c` — cross-mode reward placement onto AP cells.
- `mods/custom_checklist/` — the framework that renders the tab.
