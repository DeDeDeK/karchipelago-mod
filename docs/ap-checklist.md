# AP Checklist (a custom checklist tab)

The **AP checklist** is the archipelago mod's custom checklist tab, registered on the
custom_checklist framework. Its cells are AP *locations* - objectives tracked by mod
code - and completing one sends an AP location check exactly like a vanilla checkbox;
multiworld items can be placed *on* its cells for display.

The framework owns the presentation (synthetic-mode plumbing, minor scene, grid build,
theme recolor, banner/emblem swap) and polls every check predicate once per frame. This
doc covers only the AP-specific wiring - `mods/archipelago/src/ap_checklist.c`, with the
AP-side reward/check integration in `checklist_rewards.c` / `ap_checks.c`.

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

`ChecklistModeRow(mode)` in `main.h` maps a runtime mode to its row, returning
`-1` for a checklist mode this mod does not record (`ChecklistRowMode(row)` goes back, for
the game APIs like `gmGetClearcheckerTypeP` that index by runtime mode). It is the single
answer to "which row is this mode" - consumers call it rather than re-deriving the mapping.
So registration order across mods does not matter and nothing depends on the tab being
exactly mode 3.

## What the AP Mod Supplies

`APChecklist_Register` (called from `OnSaveLoaded`, after the framework mod has exported
its API) imports `CustomChecklistAPI` and hands it a descriptor with:

- **Checks** - a static `{ clear_kind, label, predicate }` table covering all 98
  objectives, pinned to `APCK_NUM` by a `_Static_assert`. Every predicate is a single
  `APCheckDetect_IsSet(ck)` call; the detection itself lives in `ap_check_detect.c`.
- **Theme** - blue (`AP_CHECKLIST_NAME` / `AP_THEME_*` in `ap_checklist.h`, shared with
  every textbox that names the tab so the wording and tint stay one value; the tab's runtime
  mode is `>= GMMODE_NUM` and so has no `ModeColors[]` slot of its own).
- **Tab art** - `ApChecklistTex` (below).
- **Persistence callbacks** - `is_recorded` / `record_complete` (below); the AP tab owns its
  storage because it routes to a wire field.
- **Readiness** - `is_ready` returns `ap_data && ap_data->game_ready`. The framework's
  evaluator no-ops until it holds, so `record_complete`'s textbox enqueue is safe.

A label is the in-game cell text and is also the box's AP location name minus the leading
`Archipelago: ` - the same relationship the three vanilla tabs have to their location
names, so the wording is identical on both sides and `APLocation` in the apworld restates
it by hand. They follow the vanilla checklist's wording: a title-case category prefix
(`City Trial: `, `Stadium: `, `Air Ride: `, `Top Ride: `, and vanilla's own `Time Attack: ` /
`Free Run: ` for those modes of either racing mode, where the course name says which), the
stadium or course name in caps carrying no colon of its own, and a trailing `!`. Vanilla's own
Top Ride cells carry no mode prefix, since their tab says it; the AP tab mixes modes, so its
Top Ride boxes add one, and the one Top Ride Time Attack box that names no course says
`Top Ride: In Time Attack, ...` instead.

The framework composes each label into a fixed 160-byte SIS entry at 2 bytes per character
and 1 per space or break, truncating silently once past byte 157; the longest label spends
139 including its terminator. The cell box holds exactly two lines and the engine squeezes
an over-wide line rather than breaking it, so every label places its break itself as a
`\n` - the framework's fallback split balances on width alone and would part a stadium name
from its number (`SINGLE RACE / 8 Finish in 1st place!`). Where the objective is too long
for a break after the designation (`KIRBY MELEE 1`, both photo finishes, the machine-tier
Time Attack and Free Run cells) the break moves to wherever balances the two lines. Most
lines run 18-37 characters, around the ~25 that renders at full size and no wider than
vanilla's widest *unsqueezed* line of 37. Five run over: the two Destruction Derby labels
reach 41 on their second line (the first restates vanilla's own DD cell verbatim, breaks
included, and the second matches its shape), the two `KIRBY MELEE (All)` labels reach 39, and the
Checker Knights Flight Warp Star cell 38. Every printable ASCII character in the
labels maps through `Text_CharToCommand`, including `!`, `:` and the `(All)` parentheses -
an unmapped one would be dropped without warning.

The framework's evaluator runs from its own `OnFrameStart`, so every predicate is polled
every frame in every scene. Predicates must be cheap pure reads that are safe outside
gameplay.

## Recording a Completion

The descriptor's callbacks bind the framework's presentation to AP's authoritative record:

- `is_recorded(clear_kind)` reads `ap_save->sent_checks[AP_CHECKLIST_ROW]` (recorded =
  permanently complete, shown with no replay on a later boot). An out-of-range `clear_kind`
  reports "done", so the framework never tries to complete it.
- `record_complete(clear_kind)` calls `ClearChecker_SetNewUnlock(ap_checklist_mode,
  clear_kind)`, which `ap_checks`'s `CODEPATCH_REPLACEFUNC`
  (`APChecks_SetNewUnlockReplacement`) intercepts for `ap_checklist_mode`: on a fresh
  cell it runs `RecordCheck`, which resolves the row via `ChecklistModeRow`, sets the
  `sent_checks` bit, fires the "Check sent" textbox and re-evaluates goals - and, outside a
  LAN session (`Net_IsSessionActive`), sets `clear[].is_new` and plays the unlock SFX. The
  framework seeds the cell's `is_new` afterward regardless, so the flip-and-sparkle runs on
  the next tab entry even when the replacement skipped the store.

So the AP tab's completion path is unchanged from a plain checklist objective:
predicate -> `ClearChecker_SetNewUnlock` -> `ap_checks` -> `sent_checks` row -> AP.
The framework only adds the cell flags and animation around it.

`ap_checks` reads and writes the AP cells through
`gmGetClearcheckerTypeP(ap_checklist_mode)`, which the framework serves from the AP tab's
`GameClearData` block - so the AP record path and the framework presentation operate on the
same block.

## Revealing the Board

The tab's board starts blank and reveals outward, exactly as the vanilla checklists do: a
cell draws only once it is completed, or once a completed orthogonal neighbour has revealed
it. The reveal is positional, so with only 98 of the 120 clear_kinds carrying a check it
will surface boxes that have no objective behind them, and cells whose only neighbours are
undefined stay dark until their own check fires.

`APChecklist_RevealAll` is the exception. Reveal is per checklist-mode row: the
`reveal_checklists[row]` slot option asks for one row at a time, and `RevealChecklist(row)`
opens either a vanilla mode's 120 cells or, for `AP_CHECKLIST_ROW`, the AP tab.
`RevealAllChecklists` is that call over every row. The debug menu drives both: its Reveal Checklists page has an "All Checklists" row plus one per mode row. The AP
tab reveals only the cells in `ap_checks[]` - the other 22 have no objective behind them, so
revealing them would show boxes that can never be checked. It sets `is_visible` only, leaving
`is_unlocked` to the normal completion path, and no-ops when the framework never registered
the tab.

The AP cells are opened through the framework's `RevealAll(mode)` rather than by writing
`is_visible` from here, because the framework latches the request per tab and re-applies it
after its own grid shuffle, which drops every `is_visible` bit. Reveal state is also
**re-applied on every boot**, from `OnSaveLoaded` once the tab is registered, gated on
`options_received`: the option transfer runs once per save file, and the vanilla modes'
reveal survives in the game's own clear data, but a custom tab's cells live in RAM and come
up blank. Without the re-apply the AP tab would be revealed only during the session the
client first connected in.

## Wire Layout

`APData` is read by the Python client *by field offset*, so its layout is a contract. Every
per-checklist-mode array in `APData` / `APSlotOptions` / `APSave` is `CHECKLIST_MODE_NUM`
wide (`reveal_checklists`, `goal`, `checklist_amount`, `goal_checks`, `sent_checks`,
`client_backfill`, `goal_announced`), with the AP tab's entry at `AP_CHECKLIST_ROW` - a
regular row, not an appended tail field. The client never sees the framework-assigned mode
number; it indexes the AP row directly, which is why the runtime mode can move freely.

A block of `_Static_assert(offsetof(APData, f) == 0xNNN, "")` in `main.c` pins the block
boundaries and the row-0 / AP-row offsets of the per-mode arrays. Nothing else checks the
layout - the compiler picks the offsets and the client restates them by hand - so extend
that block when adding fields, and move both sides together.

`APSave` is not part of that contract - the client never reads it - so it grows freely.
Objectives whose predicate counts across boots keep their progress in `APSave.checks`, an
`APCheckProgress` struct sitting at the end of `APSave`, away from the unlock masks: today
`allup_collect_total` (clear_kind 3), `purple_sr1_wins` (clear_kind 26),
`race_color_mask` (clear_kind 29) and `tr_color_mask` (clear_kind 87), each one bit per
`KirbyColor`, and `tr_steer_win_mask` (clear_kind 91, one bit per `TopRideCourse`), with their
targets as `AP_ALLUP_TOTAL_NEED` (5) / `AP_PURPLE_SR1_NEED` (3) / `AP_RACE_COLOR_MASK_ALL`
(`0xFF`) / `AP_TR_COURSE_MASK_ALL` (`0x7F`) in `main.h` so the counter that stops incrementing and the predicate that reads it share one
value. Grouping them means a new counting objective adds a field to that struct rather than
another loose scalar in `APSave`. An objective satisfiable within one run needs nothing here
- it latches in `ap_check_detect.c`'s transient `ap_observed` bitmask and records through
`sent_checks`.

Only clear_kinds 0-97 back an AP location, but the tab's grid is 120 cells and the
checkbox-filler cursor can reach the blank ones. `RecordCheck` rejects `clear_kind >=
APCK_NUM` on the AP row so a spent filler can't send a location code the multiworld has
never heard of.

## Cross-Mode Rewards

`ChecklistRewards_ApplyCrossModeHasReward` (the post-reward-loop hook at `0x8017e07c`)
resolves its row with `ChecklistModeRow` and returns early on `-1`, so the AP tab hosts
cross-mode rewards like any other mode while any *other* custom tab - which has no row - is
skipped. `cross_mode_slots` is `CHECKLIST_MODE_NUM` wide to match.

`RebuildRewardTablesFromShuffle` is the one place that must *not* call `ChecklistModeRow`:
the wire encodes a reward's target as a checklist-mode **row** already (the client writes
`KARData.GameMode`, whose `ARCHIPELAGO` member is `AP_CHECKLIST_ROW` by definition), so it
bounds-checks the value against `CHECKLIST_MODE_NUM` instead of mapping it.

## Tab Artwork

`mods/archipelago/assets/ApChecklistTex.dat` is a loadable HSD archive (staged to the FST
root) exporting two `_HSD_ImageDesc` publics:

- `apBannerImg` - RGB5A3 248x128 panel that backs the checkbox grid, the AP logo baked in
  as a faint watermark so the panel stays opaque under the grid.
- `apEmblemImg` - I4 64x64 intensity map of the logo for the top-right tab indicator;
  intensity doubles as alpha and the quad takes the blue theme tint. (The framework finds
  the *vanilla* emblem TObj by its 40x40 I4 signature, then repoints it here; the
  replacement's own size is unconstrained.)

`scripts/authoring/make_checklist_textures.py` authors the archive from `art/ap-icon.png`
(`uv run --with pillow python scripts/authoring/make_checklist_textures.py`). The framework loads
it by name (`tex_file = "ApChecklistTex"`) per tab build and swaps the checklist's
banner/emblem TObjs onto these descriptors.

## Objectives and Detection

There are 98 objectives, `clear_kind` 0-97, enumerated as `APCheckKind` in
`ap_check_detect.h`. The numbering is a cross-repo wire contract - the AP location code is
`361 + clear_kind`, and `APLocation` in the apworld's `KARLocations.py` restates the same
order and the same label text by hand, with nothing mechanically catching a desync. The band
is 98 wide rather than the 120 a full row would hold, because the AP Patch location block
starts at 459: growing past 98 objectives moves every patch code, so the two counts have to
be raised together. `ap_observed` is packed two `u64` words wide like a `sent_checks` row, and
the debug force-mark's AP-row mask assumes the count sits between the two word boundaries, 64
and 128, pinned by `_Static_assert`s. The blank cells above `APCK_NUM` are held clear everywhere a row mask is
written - `RecordCheck`, the backfill, and the debug force-mark - so one can never encode
into a patch's code. An AP box
whose check never fires is worse than one that doesn't exist: the location still exists in the
multiworld and logic still treats it as reachable, so fill can strand progression on it.

Every objective is an in-game achievement. There is no box for booting the game or for
receiving a multiworld item - those complete without playing, so as AP locations they were
free checks the fill could hide progression behind.

Predicates never sample. The framework polls all 98 every frame, in every scene, including
menus and loads - so each one is a single read of state latched elsewhere, and the sampling
lives in the seams below, all in `ap_check_detect.c`.

**`On3DExit` - stadium and Air Ride results.** hoshi installs the hook at `0x80015274`,
which is the epilogue of `Stadium_ExitMinor` (`0x80014d5c`), the very function whose copy
loop at `0x80015164` latches `GameData.stadium_results` from the live result arrays. So the
block is final and complete when the hook runs. The loop runs `p = 0..3` unconditionally and
`Stadium_ComputeRankByTime` / `ByPoints` / `ByDistance` rank CPU racers alongside humans,
which is what makes the photo-finish boxes solo-achievable. The latch is skipped entirely for
a replay (`GameData.is_replay`) and for the title-screen demo, leaving the previous round's
values in place, so `is_replay` is checked before reading. Per-slot, `StadiumResults.xc00[p]`
must be `0` - the same gate the rankers use - or that slot's placement and time are stale.

A stadium counts however it was reached. `CityMode` is the menu selection, so the stadium
closing a City Trial round still runs under `CITYMODE_TRIAL`, and only one entered from the
Stadium menu reads `CITYMODE_STADIUM`. The test is `InStadium()` instead:
`MJRKIND_CITY` with `CityTrial_IsInStadium()` (`0x8000ad48`, `city_kind` 7-18), which every
stadium passes in both modes because `Stadium_ApplyDescConfig` (`0x800404c4`) writes
`city_kind` from the stadium's descriptor either way. The City Trial city itself is
`city_kind` 5 and fails it. The Mic box (44) latches its KIRBY MELEE flag through the same
test.

| clear_kind | Objective | Detection |
|---|---|---|
| 12-20 | SINGLE RACE 1-9 finish 1st | `ply_finished[p] && ply_placement[p] == 0` with at least one opponent |
| 21 | HIGH JUMP over 1,500 ft | `ply_dist[p] / 0.3048 > 1500` (`ply_dist` is metres) |
| 22 | AIR GLIDER over 2,000 ft | `ply_dist[p] / 0.3048 > 2000` |
| 23 / 24 | KIRBY MELEE 1 / 2 KOs | `ply_points[p] > 100` / `> 60` (the field is a polymorphic score; for Melee it is the KO count) |
| 80 | KIRBY MELEE (All) KO 30 enemies as King Dedede | `ply_points[p] >= 30` on `STKIND_MELEE1` or `STKIND_MELEE2` + `ply_desc[p].rider_kind == RDKIND_DEDEDE` |
| 41 | DESTRUCTION DERBY 3 KO a rival 10x | `ply_points[p] >= 10` on `STKIND_DESTRUCTION3`. For a derby the polymorphic score is `GameData.destruction_derby_ko_num[p]`, which is exactly what the vanilla DD cells count, so this reads the same number vanilla's own DD 3 cell does |
| 25 | SINGLE RACE 1 1st on Bulk Star | placement + an opponent + `Ply_GetMachineKindAbs(p) == VCKIND_BULK` |
| 26 | SINGLE RACE 1 1st 3x as Purple | placement + an opponent + `rider_kind == RDKIND_KIRBY` + `Ply_GetDescColor(p) == KIRBYCOLOR_PURPLE`, counted in `APSave.checks.purple_sr1_wins`. The rider-kind test is required because `Ply_GetDescColor` reads `PlayerDesc.color`, which is only a `KirbyColor` for a Kirby rider, and the stadiums are reachable from a Dedede match |
| 27 | Photo finish in any DRAG RACE | a human and any other finisher with `ply_race_time` within 6 frames (0.10 s at 60 fps), on any of `STKIND_DRAG1`-`STKIND_DRAG4` |
| 28 | Photo finish on any Air Ride course | same pairing, gated on `MJRKIND_AIR` + `AIRRIDEMODE_RACE` and not looking at the course |
| 29 | Finish an Air Ride race as every Kirby color | per human `RDKIND_KIRBY` slot with `ply_finished[p]`, `1 << Ply_GetDescColor(p)` OR-ed into `APSave.checks.race_color_mask`; the predicate wants all 8 bits. Same `MJRKIND_AIR` + `AIRRIDEMODE_RACE` gate, and the same rider-kind test box 26 needs. A player with no color unlocked rides Pink, so Pink can latch unowned - harmless, since the apworld requires all 8 color items to reach the box |
| 34 / 35 | Air Ride 1st place as Meta Knight / King Dedede | `won` + `ply_desc[p].rider_kind == RDKIND_METAKNIGHT` / `RDKIND_DEDEDE`, on any course |
| 79 | Air Ride 1st place on Archipelago Star | `won` + `PlyMachineKind(p) == GateApStar_MachineKind()`, on any course |
| 36 | NEBULA BELT finish 1st | `won` |
| 37 | NEBULA BELT over 5,500 ft in 2 minutes | `Gm_GetCityKind() == AIRRIDE_RULE_TIME` + `Gm_GetRaceTimeLimitSeconds() == 120` + `Gm_GetPlayerRaceDistance(p) / 0.3048 >= 5500` |
| 38 | NEBULA BELT 2 laps under 02:30:00 | `Gm_GetCityKind() == AIRRIDE_RULE_LAPS` + `Gm_GetRaceLapTotal() == 2` + `ply_race_time[p]` nonzero and `<= 9000` frames |
| 52 | NEBULA BELT 2 laps under 02:06:00 | the same gates as 38, `<= 7560` frames |
| 39 | NEBULA BELT 1st on Wheelie Scooter | `won` + `Ply_GetMachineKindAbs(p) == VCKIND_WHEELIESCOOTER` |
| 40 | NEBULA BELT airborne over 10 s on a flight machine | `Ply_GetMachineKindAbs(p)` in `VCKIND_DRAGOON` / `VCKIND_FLIGHT` / `VCKIND_WINGED` + `Ply_GetStats(p)->max_time_spent_airborne > 600` frames |

Boxes 36-40 and 52 are gated on `in_nebula`, latched at `On3DLoadEnd` from `Gr_GetCurrentGrKind() ==
GR_SPACE2`. That is the loaded terrain rather than `GameData.stage_kind`, for the same reason
the Fantasy Meadows path below uses it - the stage field is the menu's course selection and
goes stale outside a race. It has to be captured at load because the results sampler that
reads it does not run until the round is already over. `won` means `ply_finished[p] &&
ply_placement[p] == 0` with at least one opponent, the same three-part test the SINGLE RACE boxes
use - an Air Ride race can be started with no CPUs, where 1st place is free the moment the
player crosses the line.

Boxes 37, 38 and 52 copy the gates vanilla puts on its own per-course cells rather than inventing
looser ones, so the demand on the player is the one the other eight courses already make.
`AirRide_CheckRaceDistanceObjectives` (`0x8004d454`) tests `Gm_GetRaceTimeLimitSeconds() ==
120` for its "Race over N feet in 2 minutes!" cells, and its caller
`AirRide_CheckRaceFinishObjectives` (`0x8004aa58`) only calls it when `Gm_GetCityKind() == 1`
- so the race has to be a *timed* one set to 2 minutes, not a lap race that happens to leave
the rules menu's time field at its 2:00 default. `AirRide_CheckRaceLapObjectives`
(`0x8004d248`) likewise keys its "Finish N laps in under MM:SS:FF!" cells off the *configured*
lap total, not laps completed, so a longer race cannot pay out the 2-lap time.

`max_time_spent_airborne` (`PlayerStats+0x5f4`) is the longest single airborne stretch, not a total -
`current_time_spent_airborne` accumulates consecutive frames and feeds it as a running maximum.
`Player_InitAll` zeroes `PlayerStats` on the next 3D scene load, so at this hook it still
holds the race that just ended.

Placement alone does not mean a win: `Stadium_ComputeRankByTime` (`0x800108b0`) ranks slots
that never crossed the line as well, falling through to a `GameData.player_race_distance`
comparison when neither slot has its finished flag set. So a Single Race abandoned while the
human led on distance latches `ply_placement == 0`. The 1st-place boxes therefore require
`ply_finished[p]` too - which matters most for `checks.purple_sr1_wins`, a persistent counter
with no way back down.

`ply_race_time == 0` is a DNF and is excluded before pairing, or two non-finishers read as a
perfect photo finish. In the time-metric modes `Stadium_ExitMinor` *projects* a finish time
for a CPU that didn't cross the line, so such a CPU appears as a finisher with an
extrapolated time - consistent with what the game's own results screen shows.

One side of the pair must be a `PKIND_HMN` slot. The other may be a CPU, which is what keeps
these objectives solo-achievable, but requiring the human means two CPUs finishing together
while the player trails behind does not award the box.

Every stadium 1st-place box (12-20, 25, 26) also requires at least one other racer - a `PKIND_HMN` or
`PKIND_CPU` slot, other than the winner, that passes the same `xc00[p] == 0` gate the rankers
use. Stadium modes reached from the Stadium menu rather than from the end of a City Trial
round can be started with no CPUs at all, and with an empty field these boxes would check
themselves the moment the player crossed the line.

The machine-specific boxes (25, 39, 40 and the machine tiers among 53-63) resolve the rider's
machine through `PlyMachineKind`, which hands `is_bike` and the class index to
custom_machines' `MachineKind_Resolve`, rather than reading `Ply_GetMachineKind` raw.
`PlayerData.machine_kind` (+0x8F) is a class-relative index paired with `is_bike` (+0x8E) - it
selects an entry in one half of `vcDataLookup`'s `data[2][19]`, so it equals the
`MachineKind` only for the 19 stars. The seven bikes count from 0 again, which puts Wheelie
Scooter at 4 rather than `VCKIND_WHEELIESCOOTER`; compared raw, box 39 is unreachable and box
25 also matches King Dedede's wheelie (bike index 5, the same number as `VCKIND_BULK`).
hoshi's `Ply_GetMachineKindAbs` adds `VCKIND_WHEELNORMAL` back for a bike but folds an
appended custom star slot onto the bike range too, where it would match a vanilla bike; the
registry resolves both.

Both per-frame procs below are attached by `AttachSamplers`, which walks the five player
slots and hangs the proc on every `PKIND_HMN` rider's GObj at `RDPRI_HITCOLL + 1`.

`APCheckDetect_On3DLoadEnd` returns without arming anything when `Gm_IsAutoDemo()` - the
title screen's attract demo, a real 3D round run inside `MJRKIND_TITLE` - City Trial on one of
its rotating slots, Air Ride or Top Ride on the others - with a CPU in
every slot. The samplers would find no human to attach to anyway, but the coral objective
counts a break whoever made it, so the whole round is skipped rather than each hook.

**A per-frame proc on each human rider - the City Trial objectives.** Attached from
`On3DLoadEnd`, and only for `Gm_IsInCity() && Gm_GetCityMode() == CITYMODE_TRIAL`, since "in
one game" means one CT Trial run. The per-run item objectives read `item_collect` raw, with
no baseline, the same way vanilla's own patch cells do; permanent patches applied at round
start count toward them.

"One game" here is one city segment: `SceneLoad_3D` calls `Player_InitAll` on every 3D scene
load, which memsets all five `PlayerData` slots and so zeroes `item_collect` and
`yakumono_break`. A stadium trip mid-trial therefore restarts these counters. That is the
same scope the vanilla City Trial cells use.

| clear_kind | Objective | Detection |
|---|---|---|
| 0 | Visit the flower on top of Castle Hall on foot | `foot_visit_checks[]`: `!Rider_IsOnMachine(rd)` and `rd->pos` within 2 units of the flower, at `(408.7, 370.8, -564.6)`. The flower sits on a very small platform, and the stage's out-of-bounds box spans 2600 units in X and Z, so the sphere is tight. The on-foot requirement stops a machine flying through the spot from counting. |
| 2 | 10+ HP Patches in one game | `item_collect[ITKIND_HP] >= 10` |
| 51 | 10+ Offense Patches in one game | `item_collect[ITKIND_OFFENSE] >= 10` |
| 3 | Collect 5 All Ups in total | frame deltas of `item_collect[ITKIND_ALLUP]` fold into `APSave.checks.allup_collect_total` |
| 4-11 | Eat 3+ of each of 8 foods | `item_collect[ITKIND_FOOD*] >= 3` |
| 30 | Visit the model city on foot | the second `foot_visit_checks[]` entry: within 10 units of `(-422.7, 12.7, -168.9)`, on foot. The model sits on open ground rather than a ledge, so the sphere is wide enough to cover standing anywhere on it. |
| 31 | Visit the flower on top of the volcanic cliffs on foot | the third `foot_visit_checks[]` entry: within 5 units of `(-107.0, 205.1, -847.3)`, on foot. The flower sits on the cliff top, reachable on foot from the surrounding terrain, so the sphere is the same size as the sky garden's rather than the tight one Castle Hall's platform needs. |
| 32 | Visit the top of the garden in the sky on foot | the fourth `foot_visit_checks[]` entry: within 5 units of `(-67.9, 463.8, -0.3)`, on foot. Vanilla's own "Make your way to the garden in the sky!" cell only asks the player to reach the garden, so the sphere sits on the top surface rather than anywhere on the structure. |
| 33 | Fly to the highest point possible | `rd->pos.Y >= AP_MAX_ALTITUDE_Y` (1000). A climb into the city's ceiling stops at 1040.3 - a collision, not an apex: vertical velocity is zeroed in one frame and the fall that follows is exactly the stage's `gravity_strength` of 0.025/frame. That ceiling is 460 below `StageNode.oob_max.Y` (1500), so the out-of-bounds lid is never what stops the climb and `calcDistanceFromOOB` cannot measure this. The threshold's 40-unit margin means the contact frame need not be sampled, and it sits far above the sky garden at 464, the highest place reachable without flying. |
| 43 | Get the Mic ability from the Copy Chance Wheel | `PlayerStats.copy_chance_mask & COPY_CHANCE_BIT(COPYKIND_MIKE)`. Only `Ply_MarkCopyAbilityObtained` (`0x8022f150`) sets that mask, and only the two copy-wheel paths call it (`randomAbility_aPress` `0x801ae7f4`, `randomAbility_autoSelect` `0x801ae890`) - so a Mic panel picked up off the ground does not satisfy it, the same wheel-only demand vanilla's Bomb and Sleep cells make. The mask is MSB-first, bit `15 - CopyKind`. |
| 74 | In one game, get the same copy ability 3 times in a row | the last three entries of `PlayerStats.copy_history` (`+0x360`) name one `CopyKind`. `Ply_RecordCopyAbility` (`0x8022ee00`) appends every grant whatever its source - a panel, the Copy Chance Wheel, a queued grant, an Archipelago item - so any mix counts. The history holds the last 6, oldest first, dropping the oldest once full; its entry count is the high 5 bits of `copy_history_num` (`+0x378`, `COPY_HISTORY_NUM`). The count has to bound the test: a zeroed history reads `COPYKIND_FIRE` in every entry |
| 45-47 | Break 20 blue / 10 green / 10 red boxes in one game | `item_collect[ITKIND_BOXBLUE/GREEN/RED]` - `ItemKind` 0/1/2 *are* the three box colors, and a break bumps the array the same way a pickup does. Vanilla counts boxes only as an all-colors lifetime total (`CityTrialClearRecords.box_total`, its 500/1000 cells), so per-color counts are unclaimed. The thresholds are unequal because the colors are: `GrCity1`'s 9-entry `box_spawn_chances` table rolls blue 45/71, red 14/71 and green 12/71 |

`item_collect` is bumped by `Ply_IncrementItemCollectNum`, which `Machine_OnTouchItem` calls
for every item application. That includes patches an Archipelago item spawns -
`SpawnItemPlayer` calls `Machine_OnTouchItem` directly to force a same-frame pickup - so an
All Up or HP Patch **received from another world counts**. That is deliberate: the player sees
the pickup happen, and a box that fires too readily is the safe failure direction.

Permanent patches are the one other writer. They land through `Machine_GiveAllUp` /
`Machine_GivePatch`, which never touch `item_collect`, so `PermanentPatch_DoApply` credits each
stat's applied amount (measured as the stat's change, so a patch the cap swallowed does not
count) straight into `item_collect` - on the City Trial map only, since the stadium reload
zeroes the array again and Air Ride has no patch cells. The write is direct rather than through
`Ply_IncrementItemCollectNum` so it stays out of the first-20-seconds aggregate. This feeds
vanilla's seven "10+ X Patches" cells and the HP/Offense boxes alike, and also the item totals
`Ply_GetItemCollectTotal` sums. Crediting also keeps drops honest: a rider who sheds a patch
runs `Ply_DecrementItemCollectNum` whether or not that patch was ever collected, so without the
credit a hit would pull the count below the real pickups.

**A per-frame proc on each human rider - the Fantasy Meadows shortcut.** The second
`On3DLoadEnd` attach path, taken when `Scene_GetCurrentMajor() == MJRKIND_AIR` and
`Gr_GetCurrentGrKind() == GR_PLANTS1`. It gates on the *loaded terrain* rather than
`Gm_GetCurrentStageKind()`, because that field is the menu's course selection and holds a
stale value outside a race - it reads 43 in Free Run on Fantasy Meadows, where the ground
correctly reads `GR_PLANTS1`. `GrPlants1` is Fantasy Meadows and nothing else, in every Air
Ride mode. The mode is deliberately unconstrained: the label carries no mode prefix, unlike
vanilla's `TA:` and `FR:` cells, so Race, Time Attack and Free Run all count.

| clear_kind | Objective | Detection |
|---|---|---|
| 48 | FANTASY MEADOWS take the shortcut | `rd->pos` within 25 units of `(249.7, 120.0, 19.2)`. Riders are always on a machine in Air Ride, so unlike the city visits there is no on-foot test |

The shortcut is an elevated arc over the normal racing line, peaking near `(255, 135, 6)`;
it is the lap's only route divergence. The sphere sits on the arc's descent rather than at
its apex because one ball has to cover two things 16 units apart: the surface a machine can
come to rest on, and the higher line a machine carrying speed flies through the same stretch.
The 25-unit radius covers both and still leaves 26 units of clearance to the closest point of
the normal racing line, which passes 51 units away and 40 below. A machine crosses the sphere
at roughly 3 units per frame and so spends about fifteen frames inside it, far too many to
skip between frames.

**The Time Attack and Free Run dispatchers - the timed-run boxes.** Vanilla runs its per-course
Time Attack and Free Run cells from two dispatchers, each with exactly one call site:
`AirRide_DispatchRaceTimeAttackObjectives` (`0x8004a994`) from `race3D_isFinished` (`bl` at
`0x80010d68`) as each player crosses the line, and `AirRide_DispatchFreeRunObjectives`
(`0x8004a90c`) from `AirRide_OnFinishRace` (`bl` at `0x80010418`) once per completed lap.
`APCheckDetect_OnBoot` repoints both `bl`s at wrappers that call the vanilla dispatcher and then
walk `timed_run_checks[]`, one `{ clear_kind, mode, GroundKind, machine, frames }` row per box.

A wrapper counts a player the way the dispatcher does - not a replay (`Gm_IsReplay`),
`MJRKIND_AIR`, a `PKIND_HMN` slot, and `Gm_GetAirRideMode()` equal to the row's mode - but
without the dispatcher's `Net_IsSessionActive` bail, which only concerns the vanilla cells.
The course is `Gr_GetCurrentGrKind()`, as for the Nebula and Fantasy Meadows gates. The time
is the value the matching vanilla evaluator reads: `Gm_GetPlayerFinishTime` (`0x800097d0`) for
Time Attack, and for Free Run `Gm_GetPlayerFreeRunTime` (`0x80009fb8`), the best lap so far,
which `AirRide_OnFinishRace` lowers before it dispatches. A zero time never counts.

| clear_kind | Objective | Mode | Course | Machine | Target |
|---|---|---|---|---|---|
| 53 / 54 | NEBULA BELT finish | Time Attack | `GR_SPACE2` | any | 03:35:00 / 03:10:00 |
| 55 | NEBULA BELT finish on Hydra | Time Attack | `GR_SPACE2` | `VCKIND_HYDRA` | 03:15:00 |
| 56 / 57 | NEBULA BELT 1 lap | Free Run | `GR_SPACE2` | any | 01:15:00 / 01:03:00 |
| 58 | NEBULA BELT 1 lap on Warpstar | Free Run | `GR_SPACE2` | `VCKIND_WARP` | 01:10:00 |
| 59 | CELESTIAL VALLEY 1 lap on Archipelago Star | Free Run | `GR_VALLEY2` | `GateApStar_MachineKind()` | 01:00:00 |
| 60 | CHECKER KNIGHTS finish on Flight Warp Star | Time Attack | `GR_CHECK2` | `VCKIND_FLIGHT` | 04:00:00 |
| 61 | MACHINE PASSAGE 1 lap on Compact Star | Free Run | `GR_MACHINE2` | `VCKIND_COMPACT` | 01:05:00 |
| 62 | FROZEN HILLSIDE finish on Wheelie Bike | Time Attack | `GR_ICE1` | `VCKIND_WHEELIEBIKE` | 03:00:00 |
| 63 | SKY SANDS finish on Flight Warp Star | Time Attack | `GR_DESERT1` | `VCKIND_FLIGHT` | 02:50:00 |
| 78 | MAGMA FLOWS finish as Meta Knight | Time Attack | `GR_HEAT2` | `VCKIND_WINGMETAKNIGHT` | 03:15:00 |

Boxes 52-58 give Nebula Belt the nine mode cells every vanilla course has - a distance cell and
two lap-time tiers in Race, and two open tiers plus one machine tier in each of Time Attack and
Free Run - which vanilla's evaluators never supply because `GR_SPACE2` has no case in any of
them. Its targets are scaled from the existing 02:30:00 two-lap box by the ratios the seven
two-lap vanilla courses share: the faster two-lap tier is about 0.84 of the slower, a Free Run
lap tier is half the matching two-lap tier, and the Time Attack tiers are about 2.86x and 3.0x
the slower and faster lap tiers. That last ratio holds on Nebula Belt because every Time Attack
is three laps: `MinorExit_AirRideMachineSelect` (`0x8003dcd8`) sets `race_lap_total` to 3 for
`AIRRIDEMODE_TIME` whatever the course. Box 55 and boxes 59-63 give a machine tier to machines
no vanilla machine cell names - Hydra, the Archipelago Star, Compact Star, Wheelie Bike and,
twice, Flight Warp Star. Box 78 gives Meta Knight one, at the 03:15:00 vanilla asks of the
Shadow Star on the same course; his wing, `VCKIND_WINGMETAKNIGHT`, has no other rider. The
Archipelago Star's `MachineKind` is whatever the registry assigned this boot, so its row carries
the sentinel `MACHINE_AP_STAR` and resolves it through `GateApStar_MachineKind()`, never matching
while the star is unregistered.

### The Archipelago Star

| clear_kind | Objective | Detection |
|---|---|---|
| 49 | City Trial: Collect all 6 spheres and assemble the Archipelago Star! | Latched by the `ap_star` assemble handler in `gate_ap_star.c`, which fires for every rider and keeps only `PKIND_HMN` |
| 50 | City Trial: In one game, assemble Dragoon, Hydra and Archipelago Star! | A per-frame poll over the human players: `PlayerStats.flags_84d` bits `0x04` and `0x08`, the per-round flags `Ply_MarkLegendaryMachineAssembled` (`0x80231198`) sets, plus `GateApStar_AssembledThisRound(ply)` |

The six spheres are custom items scheduled into City Trial's forced-content red boxes
against match progress, the way the Hydra and Dragoon parts are, and each enters the pool
only once its own sphere item has arrived. The flag survives the round, so the cell also
fills in on a later load rather than only in the session that earned it.

Cell 50 is scoped to one round because all three of its inputs are: `flags_84d` lives in
`PlayerStats`, which is zeroed on every 3D scene load, and the star's assembly mask is
cleared at the same point. It is polled from `APCheckDetect_OnFrameStart` rather than the
per-rider sampler the other City Trial objectives use, because assembling the star ends in
`Rider_RespawnFullRecreate` (`0x80193900`) - it destroys the rider's `machine_gobj` and
calls `Machine_Create` for the legendary, tearing the machine down under the sampler, and a
poll keyed to the mod's own frame callback is unaffected.

**The rival KO recorder - the Destruction Derby, bust, sphere shot and VS. King Dedede boxes.** `custom_machines` owns the
`REPLACECALL` on the single `bl Ply_AddDeath` at `0x801e1f74`, inside `Machine_GiveDamage`
(`0x801e1ee8`), and hands the KO on through its death-handler seam; `main.c` registers
`APCheckDetect_AddDeath` there with `cm_api->AddDeathHandler` once the registry resolves. The Dedede and Mic tallies are `[5]` arrays indexed by the crediting player, because both
cells read "in one game" of a single player's KOs. `Ply_AddDeath` (`0x8022f648`) is the
engine's unified KO-event recorder, reached only from that one call site - where a machine's
HP crosses zero - and it is the only place the KO'd rider is named: its first argument is the
ply riding the destroyed machine and `dmg_log->attacker_ply` (`MachineData.dmg_log` + 0x1c)
is the killer. The per-player tally the DD cells read,
`GameData.destruction_derby_ko_num[p]`, records the killer alone. The handler is also handed
the widened `MachineKind` the victim was riding, which is the only place that machine is
named.

| clear_kind | Objective | Detection |
|---|---|---|
| 42 | As King Dedede, KO 10 Kirbys in one derby | gated on `Gm_IsDestructionDerby()` (`Gm_GetCityKind() == 14`, so any of DD 1-5); counts a KO whose killer is a `PKIND_HMN` slot with `Ply_GetRiderKind == RDKIND_DEDEDE` and whose victim is a different slot with `RDKIND_KIRBY`. The counter is per game, reset in `On3DLoadEnd` alongside the other per-round counters |
| 75-77 | In the city, bust Rex Wheelie on Wheelie Scooter / Winged Star on Flight Warp Star / Shadow Star on Archipelago Star | gated on `in_city_trial`; a `bust_checks[]` row matches the victim's `MachineKind` against the busted machine and `PlyMachineKind(killer)` against the ridden one, with the killer on its machine (`Rider_IsOnMachine`) as the KO lands. The Archipelago Star resolves through `GateApStar_MachineKind()` |
| 81 | VS. KING DEDEDE KO King Dedede as Meta Knight | a KO of slot 4 - King Dedede in his own stadium, the victim `Ply_AddDeath` stamps `king_dedede_ko_frame` (`PlayerStats+0x848`) for - under `InStadium()` on `STKIND_VSKINGDEDEDE`, by a human killer whose `Ply_GetRiderKind` is `RDKIND_METAKNIGHT` |
| 97 | In the city, KO a CPU with a sphere shot from the Archipelago Star | gated on `in_city_trial`; the victim is a `PKIND_CPU` slot and `GateApStar_IsShotAttack(dmg_log->credited_attack)` holds - the credited hit was a sphere shot (below) |

Testing the victim's rider kind is not a formality. A stadium CPU draws its character from the
gated select grid, so once King Dedede or Meta Knight is unlocked - and this box needs Dedede
unlocked - a rival can be one of them rather than a Kirby. The player can assign each CPU a
machine from that grid, so an all-Kirby field stays arrangeable.

The sphere shot is a projectile kind of its own, and its state's attack word carries an
attack cause no vanilla attack uses, `AP_STAR_SHOT_ATTACK_CAUSE` (`0x03`, in `ap_star_api.h`).
`Machine_StoreAttacker` (`0x80231d90`) writes the credited attack's word to the victim's
`DmgLog.credited_attack` in the same call that writes `attacker_ply`, so the KO's damage log names the
shot directly: `GateApStar_IsShotAttack` compares the word's low byte, with no runtime import of
`ap_star`. The log names the credited attack, the one the game credits the KO to, so a CPU a
shot wore down and something that credits nobody finished off still counts, exactly as the
killer is still credited.

The bust boxes use their own table rather than vanilla's. `Ply_AddDeath` records vanilla's eight
City Trial bust pairs into `PlayerStats+0x4c4` from a fixed ten-entry `{busted, riding}` table at
`0x804B4C68` (its last two entries, Dragoon and Hydra busting each other, are written and never
read), comparing vanilla kinds only - an appended machine like the Archipelago Star is swapped for
a scapegoat kind before the engine call. "In the city" is the Trial city itself, the scope of
vanilla's own bust cells; a stadium closing the trial reloads with `in_city_trial` clear.

Boxes 80 and 81 are played from the Stadium menu. The Trial start never gives a human King Dedede
or Meta Knight - their riders' 3D HUD is short-circuited in the Trial city - so a trial's closing
stadium always has a Kirby rider.

**The yakumono break recorder - the coral box.** `YakumonoGObj_IncrementBreakCount`
(`0x80105d80`) is where every break family credits its break, and its single
`bl Ply_IncrementYakumonoBreakCount` at `0x80105da0` is the one call site that function has,
so `APCheckDetect_OnBoot` repoints it at a wrapper the same way as the two recorders above.

| clear_kind | Objective | Detection |
|---|---|---|
| 1 | Break all the coral in one game | a per-round count of breaks of `YAKUKIND_CORAL` (33) reaching `Gr_GetYakumonoSpawnTotal(33)`, which is 10 on `GrCity1`. The total is read from the stage rather than hardcoded, exactly as the vanilla Sky Sands "break all coral" cell does for its own coral, which is a different kind (24, `YAKUKIND_BREAKCORAL`, read by `Ply_GetAllCoralBrokenFlag` `0x8022fd48`). The counter is reset in `On3DLoadEnd` alongside the other per-game ones, and gated on a nonzero total, which scopes it to City Trial |

Counting at the credit path rather than reading `PlayerStats.yakumono_break[33 - 0x15]` is what makes
the box mean "the coral is gone", not "one player broke all of it".
`Ply_IncrementYakumonoBreakCount` bumps only the crediting player's record - and a break with
no identifiable attacker falls back to the first occupied slot (`GrYakuBreak_GetAttackerPly`,
`0x80105cb0`) - so with CPUs in the city the coral can all be broken with no single player's
counter ever reaching 10. The wrapper is also above the vanilla function's own
`PlayerStats.flags_855` bit-6 suppression, so a break that records against nobody still counts.

**The enemy-defeat recorder - the Mic count.** `Ply_RecordEnemyDefeat` (`0x8023205c`) is the
enemy-side counterpart of `Ply_AddDeath`: it credits a player with an enemy kill, bumping
`PlayerStats.enemies_defeated`, `enemy_defeat_by_kind[]` and
`enemy_defeat_by_method[]`. Like the rival recorder it has exactly one call site
(`0x802022ec`), so `APCheckDetect_OnBoot` repoints that `bl` at a wrapper the same way.

| clear_kind | Objective | Detection |
|---|---|---|
| 44 | KIRBY MELEE (All): KO 10 enemies as Mic Kirby in one game | gated on a loaded KIRBY MELEE round (`InStadium()` with `Gm_GetCurrentStadiumKind()` of `STKIND_MELEE1`/`STKIND_MELEE2`, from the Stadium menu or closing a trial), latched in `On3DLoadEnd`; counts a defeat credited to a `PKIND_HMN` slot whose rider holds `COPYKIND_MIKE` and is in action state `RDSTATE_MIKESING` (`0x61`) or `RDSTATE_MIKEEND` (`0x62`). The counter is per game, reset in `On3DLoadEnd` alongside the Destruction Derby one |

The rider's live state is what identifies the blast, not the attack-method index the
recorder itself keys off. That index - the attacker log's `attack_data.kind` - is what vanilla's own
ability cells read back out of `enemy_defeat_by_method[]` (`0xe` Tornado, `0xf`/`0x15`
exhaled star, `0x10` Quick Spin), and it would be the tighter signal, but the Mic's index is
not identified: `ability_Mike` (`0x801b3dac`) installs no hitbox of its own, and the rider's
single `TriggerData` (`RiderData+0x674`) takes its cause from the rider archetype once at
`Rider_Create`. Reading the state instead means a *ram* kill landing inside the blast
animation also counts - the same direction of error the item-collect objectives accept, and
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
carries Walky as `ENEMYKIND_T1_WALKY` (`0x2D`) across 54 of its 285 positions - four by direct
reference (weight 5/100) and the rest through meta-groups `0x53`/`0x55` - which works out to
roughly 0.26% of the enemies that actually spawn. `GrPasture1`'s 28-entry mode-2 table
contains no Walky of any tier, and mode 2 does no meta expansion that could introduce one, so
KIRBY MELEE 1 can never satisfy the box. That is why the apworld's location sits in the KM2
region while the mod's sampler accepts either stadium.

**The City Trial events - boxes 64-73.** One box per event vanilla writes no cell about, each
earned while that event is running. "Running" is `CityEvent_GetActiveKind()` (`0x800ee8c4`), which
reports `cur_kind` in state 2 only. That covers both a natural event and one an Archipelago
trigger item forces, since `Event_Do` hands the kind to the same state machine, and
custom_events' kinds are `>= EVKIND_NUM`, so they never compare equal. `stc_eventcheck_gobj`
still points at the last round's GObj between scenes, so every read is gated on `in_city_trial`
(latched at `On3DLoadEnd`) or made from a per-rider proc, never from `OnFrameStart`.

Progress is per human in `event_runs[4]`, an `EventRun` of counters that `SyncEventRun` clears
whenever the active kind changes. One run of an event is therefore one attempt, and a repeat of
the same kind re-arms, because the kind passes through -1 in states 3 and 0. The City Trial
per-rider proc and every hook below call `SyncEventRun` first. `On3DLoadEnd` resets all four
runs, because a round that ends mid-event tears the event GObj down without its `end`.

| clear_kind | Event | Objective | Detection |
|---|---|---|---|
| 64 | Run Amok | travel over 1,000 feet | per-rider proc: `total_distance_grounded + total_distance_airborne` (`PlayerStats+0x60c/+0x610`) minus the value snapshotted when the event began, in feet at vanilla's mileage factor (1 unit = `11.4285717 * 5280 / 160934.4` ft, about 0.375). `Ply_UnkUpdate` (`0x80231340`) accumulates `|world_velocity|` into them while the rider is on a machine |
| 65 | Rail Fire | catch fire at all 5 stations | `Ply_PlayRailFireHitSFX` (`0x8027aa1c`), the burn sound, is the one thing a station's hit adds. Its two `bl`s - in `Machine_ActOnHitCollision` (`0x801d741c`) and in the on-foot `Rider_ActOnHitCollision` (`0x80196668`) - are repointed at a wrapper that classifies the burn by the nearest of `rail_stations[]` |
| 66 | Same Item | get 20 of the box item | pickups per `ItemKind` in the item-collect wrapper, any kind reaching 20 |
| 67 | Lighthouse | go under both lights | per-rider proc: `CityLighthouse_InBeam` per light, while the lighthouse (`stc_lighthouse_gobj`) is in yakumono state 3 |
| 68 | Prediction | see a wrong prediction | a wrapper on the random arm's `HSD_Randi`, judged when the trial's stadium loads |
| 69 | UFO | get its All Up | the item-collect wrapper: `ITKIND_ALLUP` with `src_tag == ITSPAWN_UFO` while `CityEvent_GetCurrentKind()` is UFO |
| 70 | Machine Formation | bump a formation machine | a hook in `Machine_CheckMachineBumpCollision` (below) |
| 71 | Bounce | get over 10 items | pickups in the item-collect wrapper, box breaks excluded |
| 72 | Fog | KO a rival | `APCheckDetect_AddDeath`: a human killer, a different victim slot, and the active kind Fog |
| 73 | Fake Powerups | get 5 good patches and touch no fake | the item-collect wrapper counts `CityItem_IsGoodPatch` pickups and flags any `ITKIND_*FAKE`; judged by `SyncEventRun` as the kind leaves Fake Powerups |

Most of these hang off one seam, **the item-collect wrapper**. `Ply_IncrementItemCollectNum`
(`0x8022fbcc`) has exactly one call site, the `bl` at `0x801db928` in `Machine_OnTouchItem`. Every
pickup and every box break passes through it, with the kind folded to its vanilla base and
`ItemData.spawn_type` as `src_tag`. `APCheckDetect_OnBoot` repoints that `bl`. `ap_patches`'
conditional hook at `0x801db91c`, one instruction up, jumps past the call for the Archipelago
kinds and falls into it otherwise, so the two coexist. Kinds 0-2 are box breaks and are skipped.

Counting pickups rather than `item_collect` deltas matters. A hit sheds patches through
`Ply_DecrementItemCollectNum`, and a fake's hurt does exactly that, so deltas can go down. Only
machines collect items (`Machine_CheckPatchColl`), so none of these fire on foot.

`ItemSpawnType` (`item.h`) is what tells the sources apart. The UFO is the only spawner of type 9.
Archipelago items spawn `ITSPAWN_DIRECT` (0) and are picked up on the spot, so Fake Powerups
ignores type 0: a fake-patch trap cannot fail the run and a patch sent from another world cannot
pad it. Fakes spawn from boxes (2) and the sky (1).

**Rail Fire.** `event_stationFire_start` creates one yakumono of kind 65 (`YAKUKIND_RAILFIRE`) per
event position - `bgm_sky[6]` holds 10 positions in 5 pairs, one per station, and nothing else
creates kind 65. The hit is static for the whole event. The `rail_stations[]` centres are each
pair's midpoint:

| Station | Positions | Centre |
|---|---|---|
| 0 (NW, raised) | 60 / 61 | (-735.2, 172.7, -569.1) |
| 1 (NE) | 62 / 63 | (662.6, 100.3, -801.7) |
| 2 (E) | 64 / 65 | (673.0, 42.5, -9.1) |
| 3 (S) | 66 / 67 | (360.2, 32.1, 976.8) |
| 4 (SW) | 68 / 69 | (-668.9, 23.2, 651.5) |

Every fire is within 75 units of its own centre and the closest two centres are 793 apart, so
the rider's nearest centre names the station unambiguously. The shortest tour of all five is
about 4130 units, some 1650 frames at 2.5 units/frame, against a 3000-frame event.

**Lighthouse.** The lighthouse is permanent stage yakumono kind 68 (`YAKUKIND_LIGHTHOUSE`). The event's start stores it at
`stc_lighthouse_gobj` (`r13+0x670`) and turns it on, and its lights are lit in yakumono state 3.
Its parameter arm names the lamp joints (`light_joints`, 149 and 153 on GrCity1, `light_num` 2)
and the beam shape (`beam_len` 250, `beam_slope` 40, `beam_base` 0.2). The game's heal test walks
the lights but stops at the first that holds the point and never says which, so the box repeats
it per light. The origin is `Gr_GetNodeWorldPos(joint)`, the end is that minus the normalized
`Gr_GetNodeWorldAxisY(joint)` times `beam_len`, and `CityLighthouse_InBeam` (`0x8010d910`) is the
cone test. The two lamps hang off the same rotating head on opposite sides, so they sweep one
ground ring about 110 units from the tower, 180 degrees apart.

**Prediction.** PREDICTION has no event functions; it is only `stadiumPrediction`'s popup, and
nothing reads the prediction back. When it names a stadium, `HSD_Randi(5)` picks: a roll of 0
names a random `StadiumKind` through `HSD_Randi(24)` (`bl` at `0x801279a0`), and the other four
name `Gm_GetCurrentStadiumKind()`. So about 1 in 5 x 23/24, some 19%, of predictions are wrong. The
trial's stadium is fixed before its city loads (`CityTrial_DecideStadium`, replaced by
`GateStadiums_DecideStadium`), and nothing changes it mid-trial.

`APCheckDetect_PredictRandom` wraps that second roll and remembers what it picked. The comparison
waits for the trial's own stadium load - `InStadium()` under `CITYMODE_TRIAL`, since a Stadium
menu round reads `CITYMODE_STADIUM` - so the player sees the miss, and a random pick that lands on the real stadium does not count. Each
trial's city load clears the guess. PREDICTION carries weight 200 in every stadium group against
10-60 for the rest, and is once-only, so most trials make one prediction.

**UFO.** The UFO (yakumono kind 0x43, `YAKUKIND_UFO`) flies one of the stage's paths through a five-state script
and ends the event itself when it leaves; the player cannot stop it. Each state drops a ring of
items relative to a UFO joint, all with `spawn_type` 9 and a lifetime that ends with the stop.
Slot 0 of every ring is a hardcoded `ITKIND_ALLUP` (`li r29, 20`), the others come from the UFO
event pool. `gate_items.c` gates that slot, so the box needs the All Up unlock in game as well as
in logic. The kind test
reads `CityEvent_GetCurrentKind()`, in any state, because the last stop's items outlive the UFO by
a few frames. It also keeps out `City_SpawnMiscItemsRing` and `shootPowerUps`, whose spawn type
comes from stage data.

**Machine Formation.** The formation is five riderless `Machine_Create` machines. While one flies,
`MachineData.formation_slot` (`+0x19`) holds its slot 0-4; every other machine reads
`MACHINE_FORMATION_NONE` (5), set by `MachineGObj_StoreVcDataPtr`. A bump, hit, boarding or destroy
takes a machine out of the formation and back to 5. A real bump in
`Machine_CheckMachineBumpCollision` calls `Machine_EnterHitReaction` on the other machine
(`0x801dacb8`) and then on this one (`0x801dacc0`). The first of those takes the formation machine
out, so the box's `CODEPATCH_HOOKCREATE` sits on the instruction before it (`0x801dacb4`, `mr r3,
r28`, re-executed after) and passes both machines while their slots still hold. An attack hit that
knocks a formation machine out, or boarding one, does not count.

The formation flies a straight line between one of seven position pairs: a fast entry, a hover
of about 10 units/s for 69-131 s, then a fast exit. Four of the paths hover 20-120 units above the
street, in reach of jumps and rooftops. Two need a raised launch or a glide, and one sits at
about 265 and wants flight or a glide down from the cliffs. So the box asks for no airborne state
or flying machine.

**Same Item, Bounce, Fake Powerups.**
- **Same Item** sets `grBoxGeneInfo.event_active_flags` bit 4. The first box opened latches
  `same_item_it_kind`, and every box after gives that kind, 2 copies from a medium box and 4 from a
  large one.
- **Bounce** clears the city's items and respawns up to 50 every 1-5 frames from the normal pools.
- **Fake Powerups** spawns about 59% fakes among patch-type drops.
- **Why five good patches.** Touching nothing is free, so Fake Powerups also demands five good
  patches.
- **Why it is judged at the end.** The run can only be judged once the event is over, and an
  event leaves state 2 only through its own end. No event can start over a running one: `Event_Do`
  refuses a nonzero state. So a change away from Fake Powerups means the event ran its course. A
  round that ends first never judges it.

**The Top Ride sampler - boxes 82-96.** Top Ride loads through minor 19 and builds none of the
3D Player/Rider/Machine objects, so none of the seams above reach it. Its seam is vanilla's own
per-kirby checklist evaluator: `TopRide_CheckForNewUnlocks` (`0x802ac850`) calls
`TopRide_CheckPerCourseObjectives` (`0x802b88f4`) from a single `bl` at `0x802acd4c`, once a frame
for every occupied slot, right after `TopRide_KirbyMgrUpdate` (`0x802db74c`) has moved, lapped and
ranked the kirbys. `APCheckDetect_OnBoot` repoints that `bl` at `APCheckDetect_TopRideKirby`, which
samples the record's kirby and then calls the vanilla evaluator. Sampling there rather than from a
GObj proc matters for one field: `active_item_kind` (+0x11) is written with every kind
`TopRide_KirbyApplyItem` (`0x802d8cb4`) applies, and the vanilla evaluator reads a human's through
Kirby `vt[0x118]` (`0x802da368`), which resets it to 0xFF. Just before that call it is exactly the
item applied since the last frame - natural pickups and Archipelago item gives alike, the same
direction the City Trial item counts take.

Everything is gated on `round_state == 2` and on `tr_armed`, which `APCheckDetect_OnTopRideLoadEnd`
(from `OnTopRideLoadEnd`) sets unless `Gm_IsAutoDemo()`, the title screen's all-CPU demo; that
callback also clears the per-slot `tr_runs[4]`. A kirby's state is its state-handler vtable
(`TopRide_KirbyStateVtable`), and entering one is a change from the last sample's.

"1st place" is what vanilla's own cells test: on the frame `finished` (+0x3E) is first seen,
`standing` (+0x10) is 0 - the results order, ranked by finish time with an unfinished kirby's
projected time - plus at least one other occupied slot. A Race round only; `standing` is never
ranked in the solo modes. The course is `TopRide_GetSelectedCourse()` and the machine
`TopRide_GetMachineKind(slot)`.

| clear_kind | Objective | Detection |
|---|---|---|
| 82 | Freeze 3 rivals with one Freeze Fan | any kirby entering `KirbyFreeze` credits each other human in the Freeze Fan power state (`TR_ITEMPOWER_VT_FREEZE_FAN`) with that slot; a human's victim mask clears whenever it is out of that state, so one mask is one fan |
| 83 | FIRE 1st without getting burned | a win on `TOPRIDE_FIRE` with no frame in `KirbyBurn` |
| 84 | SAND 1st without dropping into Ant Doom | a win on `TOPRIDE_SAND` with no frame in either Ant Doom state, `KirbyDoodlebug` (swallowed) or `KirbyDoodlebugOut` (thrown back) |
| 85 | 1st against 3 level-5 CPUs | a win with every other slot `TR_PKIND_CPU` at `TopRideSlot.handicap` 4, the byte vanilla's per-course level-5 cells test |
| 86 | Photo finish | in a Race, a finished human and any other finished kirby within 12 frames (0.20 s) of `finish_time`. Both must have finished: until then `finish_time` is a projection |
| 87 | Finish a race as every color | each human Race finish ORs `1 << TopRide_GetColor(slot)` into `APSave.checks.tr_color_mask`, placement aside; the lobby colors are `KirbyColor` indices |
| 88 / 89 | Time Attack GRASS 00:33:00 / METAL 00:57:00 on Steer Star | `tr_timed_checks[]`: the finish's `finish_time` against the frame target on the named course and `TR_MACHINE_STEER` |
| 90 | Free Run SKY 1 lap under 00:11:00 on Steer Star | the same table, `prev_lap_frames` at each completed lap |
| 91 | 1st on every course on Steer Star | each Steer Star win ORs its course into `APSave.checks.tr_steer_win_mask` |
| 92 | 1st without getting hit | a win with no hit-state entry, and the config's `item_rule` not the Zero Items rule (3), with which only rams and the course's own hazards are left |
| 93 | 1st after 5 hits | a win with 5 or more hit-state entries |
| 94 | Fire, Freeze Fan, Bomb and Walky in one race | the applied item, one bit per item, any mode |
| 95 | 3 Speed Downs and still 1st | a win with 3 or more `TRITEM_SPEED_DOWN` applied |
| 96 | Time Attack laps within 1 second | at the Time Attack finish, the slowest and fastest `prev_lap_frames` of the run no more than 60 frames apart |

The hit states are the damage reactions: Press, Crush, Explode, Strike, Spin, SandSpin, Numb,
Elec, Burn, Freeze and Confuse. Ant Doom and the WATER falls (`KirbyWhirlpool`) are left out
because they are course routes vanilla asks the player to take ("drop into Ant Doom 50 times",
"enter the falls 5 times"), and Speed Down because it is an item the player picks up.

The Freeze Fan freezes through two effectors that live inside its power state
(`FreezeFan` state `+0x2c` / `+0x78`, repositioned each frame by `0x8030bb24`), so a rival can only
be frozen by it while its holder is in that state. The attribution does not see who owns the
effector, so a second fan or a course's ice freezing a rival in the same window also counts.

Laps come from `lap_progress` rising. Each rise to 1 or more snapshots the lap into
`prev_lap_frames`; lap 1 is timed from the start signal, since the per-lap counter only ticks
while `round_state == 2`. `finished` is set on the same pass as the last lap's rise, and the
sampler records the lap before judging the finish. The time targets are frames at 60fps, the same
unit the vanilla thresholds in the course table at `0x8048a028` use: boxes 88-90 sit at the slower
of vanilla's two open tiers for their course and mode.

### The apworld's location range

Every one of the 98 clear_kinds latches, and every one backs an AP location. The apworld's
codes run contiguously from 361 to 458, and `ArchipelagoChecklistAmount` covers all 98.

Each box takes the region where its activity happens rather than a flat Archipelago region,
so it inherits that region's entrance chain (stadium unlocks, course unlocks, the DD/KM/DR
prerequisite chains) instead of restating them as item rules. Only item-spawn dependencies
are written out by hand - the Mic ability for the two Mic boxes, a City Trial machine for the
boxes that need something to ride, the named machine for each machine-tier box (plus Charge for
Hydra and the Archipelago Star, which cannot be steered without it), the eight colors for the all-colors
race. The Time Attack and Free Run boxes sit in the matching `Air Ride: Time Attack: <course>` /
`Air Ride: Free Run: <course>` regions, and form the `Archipelago: Time Attack` and
`Archipelago: Free Run` location groups, which the Archipelago progression option's `Time
Attack` / `Free Run` categories key off exactly as Air Ride's own do.

The ten event boxes sit in the City Trial region and form the `Archipelago: Events` group, part
of `Archipelago: RNG` and keyed by the `RNG: Events` progression category, as `City Trial: Events`
is for City Trial. While events are gated each needs its event's unlock item. Those ten unlocks
were the event unlocks no location named, and they are now progression like the other six. The
other requirements:
- A City Trial machine for Run Amok, Rail Fire and the formation.
- A real damage source for the Fog KO, the same rule as breaking a CPU's machine.
- A patch unlock for Same Item and Fake Powerups.
- A box color able to spawn for Same Item.
- An item able to spawn for Bounce.
- The All Up unlock for the UFO.

The three bust boxes (75-77) sit in the City Trial region and take the rules vanilla's bust
cells do: both machine unlocks, a real damage source, and Charge to steer the Archipelago Star.
King Dedede and Meta Knight are not damage sources here, since a bust names the machine the
player rides. The three form `Archipelago: Bust Vehicle on Vehicle`, keyed by the Archipelago
progression option's `Bust Vehicle on Vehicle` category the way City Trial's own bust cells are
keyed by City Trial's.

The character and star boxes need their unlock: Meta Knight for 78 (in
`Air Ride: Time Attack: Magma Flows`) and 81 (in VS. KING DEDEDE's region), King Dedede for 80
(in the `KIRBY MELEE (All)` region), and the Archipelago Star plus Charge for 79 (in
`Air Ride`). The KIRBY MELEE and VS. KING DEDEDE entrances already require a damage source,
which the character is himself. 78, 80 and 81 join `Archipelago: Characters`, and 81 is in `Archipelago: High Effort`.

The sphere shot box (97) sits in the City Trial region and joins only
`Archipelago: City Trial`. It needs the Archipelago Star's unlock and Charge, since the shot is
a full-charge release and the star cannot be steered without Charge. It needs no damage source,
because the shot is one. It stays out of `Archipelago: Rivals`, which is part of
`Archipelago: RNG`.

The copy-streak box (74) sits in the City Trial region beside the Mic wheel box, and joins it in
`Archipelago: Copy Chance Wheel`, so the `RNG: Copy Chance Wheel` category keys both. While
abilities are gated it needs any one ability unlock. The city's Copy Chance Wheel is always
there, and `GateAbilities_RandomGiveAbility` turns a locked result into an unlocked one, so
with a single ability unlocked every spin gives that ability.

The fifteen Top Ride boxes (82-96) sit in Top Ride regions and form `Archipelago: Top Ride`.
Box 83 is in `Top Ride: FIRE`, 84 in `Top Ride: SAND`, 88 / 89 / 90 in their Time Attack or Free
Run course region, 96 in `Top Ride: Time Attack`, and the rest in `Top Ride`, which takes any
course while courses are gated; 91 takes all seven. Their rules:
- The Freeze Fan for 82, Speed Down for 95, all four ability-themed items for 94, and any item
  that lands a hit for 93 - each ability-themed item by its own unlock or, while abilities are
  gated, its copy ability.
- Charge for 85, as vanilla's per-course level-5 cells have.
- Steer Star for 88-91, only while Top Ride has a goal. Without one the apworld ships neither
  control type and the mod unlocks both.
- All eight colors for 87.

88, 89 and 96 also join `Archipelago: Time Attack` and 90 `Archipelago: Free Run`, so the
Archipelago progression option's categories cover them as they do Air Ride's. 86 joins
`Archipelago: Rivals` with the other two photo finishes, 87 `Archipelago: Kirby Colors`, and 85 and
91 `Archipelago: High Effort`.

## Files

- `mods/archipelago/src/ap_checklist.c` / `.h` - the AP descriptor (checks + labels, blue
  theme, tab art, `is_recorded` / `record_complete` / `is_ready` callbacks) and
  `APChecklist_Register` / `APChecklist_RevealAll`.
- `mods/archipelago/src/ap_check_detect.c` / `.h` - `APCheckKind`, the sampling seams
  (`APCheckDetect_On3DExit`, the City Trial and Fantasy Meadows per-frame procs, and the
  `Ply_RecordEnemyDefeat`, yakumono-break, Time Attack / Free Run dispatch, item-collect, Rail
  Fire, machine-bump, projectile-hit, prediction and Top Ride evaluator interceptions
  `APCheckDetect_OnBoot` installs), and `APCheckDetect_IsSet`.
- `mods/archipelago/assets/ApChecklistTex.dat` - the AP banner/emblem archive.
- `scripts/authoring/make_checklist_textures.py` - authors `ApChecklistTex.dat`.
- `mods/archipelago/src/main.h` / `main.c` - the wire structs, `CHECKLIST_MODE_NUM` /
  `AP_CHECKLIST_ROW`, the runtime `ap_checklist_mode`, and the offset assertions.
- `mods/archipelago/src/ap_checks.c` / `.h` - the `ClearChecker_SetNewUnlock` REPLACEFUNC
  that records AP completions, and the client backfill.
- `mods/archipelago/src/ap_goal.c` / `.h` - goal evaluation and the filler gate that keeps
  a filler token off a goal cell.
- `mods/archipelago/src/checklist_rewards.c` - cross-mode reward placement onto AP cells.
- `mods/archipelago/src/gate_ap_star.c` - the `ap_star` mod import behind clear_kinds 49, 50 and 97.
- `mods/custom_checklist/` - the framework that renders the tab.
