# AP Checklist (a custom checklist tab)

The **AP checklist** is the archipelago mod's custom checklist tab, registered on the
custom_checklist framework. Its cells are AP *locations* — objectives tracked by mod
code — and completing one sends an AP location check exactly like a vanilla checkbox;
multiworld items can be placed *on* its cells for display.

The framework owns the presentation (synthetic-mode plumbing, minor scene, grid build,
theme recolor, banner/emblem swap) and the per-frame check evaluation. This doc covers
only the AP-specific wiring — `mods/archipelago/src/ap_checklist.c`, with the AP-side
reward/check integration in `checklist_rewards.c` / `check_detection.c`.

## Two mode identities

`GMMODE_NUM` (3) means "the three real game modes". It still sizes the reward tables,
because the AP tab awards no native rewards of its own.

Per-checklist-mode *recorded* state is one row wider: `CHECKLIST_MODE_NUM` (4), with the
AP tab always at the fixed row `AP_CHECKLIST_ROW` (== `GMMODE_NUM`). That row index is
compile-time constant and is what the wire and the save both store.

The tab's *runtime* mode is different and is assigned dynamically: the framework appends
each registered tab to the next free mode index, so the AP tab's mode is whatever slot it
lands on, `>= GMMODE_NUM`. `APChecklist_Register` stores it in the global
`ap_checklist_mode` (defaulting to `GMMODE_NUM`).

`ChecklistModeRow(mode)` in `check_detection.h` maps a runtime mode to its row (and
`ChecklistRowMode(row)` back, for the game APIs like `gmGetClearcheckerTypeP` that index
by runtime mode). It is the single answer to "which row is this mode" — consumers call it
rather than re-deriving the mapping. So registration order across mods does not matter and
nothing depends on the tab being exactly mode 3.

## What the AP mod supplies

`APChecklist_Register` (called from `OnSaveLoaded`, after the framework mod has exported
its API) imports `CustomChecklistAPI` and hands it a descriptor with:

- **Checks** — a static `{ clear_kind, label, predicate }` table covering all 33
  objectives. Labels are the in-game cell text and are independent of the apworld's
  location names; they follow the vanilla checklist's wording — a title-case category
  prefix (`City Trial: `, `Stadium: `, `Air Ride: `), the stadium name in caps carrying
  no colon of its own, and a trailing `!`. The framework composes each into a fixed
  128-byte SIS entry with a 125-byte glyph budget (2 bytes per character, 1 per space)
  and truncates silently past it; the longest label spends 82. Each label places its own
  line break as a `\n`, after the category and stadium designation the way vanilla breaks
  its entries — the framework's fallback split balances on width alone and would part a
  stadium name from its number (`SINGLE RACE / 8 Finish in 1st!`). Every line comes out
  at 23 characters or fewer, inside the ~25 that renders at full size. Every printable
  ASCII character in the labels maps
  through `Text_CharToCommand`, including `!` and `:` — an unmapped one would be dropped
  without warning.
  Every predicate is a single `APCheckDetect_IsSet(ck)` call; the detection
  itself lives in `ap_check_detect.c` (see Objectives and detection below).
- **Theme** — blue (`AP_CHECKLIST_NAME` / `AP_THEME_*` in `ap_checklist.h`, shared with
  every textbox that names the tab so the wording and tint stay one value; the tab's
  runtime mode is `>= GMMODE_NUM` and so has no `ModeColors[]` slot of its own).
- **Tab art** — `ApChecklistTex` (see Tab artwork below).
- **Persistence callbacks** — `is_recorded` / `record_complete` (below); the AP tab owns
  its storage because it routes to a wire field.
- **Readiness** — `is_ready` returns `ap_data && ap_data->game_ready`. The framework's
  evaluator no-ops until it holds, so `record_complete`'s textbox enqueue is safe.

The framework's evaluator runs from its own `OnFrameStart`, so every predicate is polled
every frame in every scene. Predicates must be cheap pure reads that are safe outside
gameplay.

## Recording a completion

The descriptor's callbacks bind the framework's presentation to AP's authoritative
record:

- `is_recorded(clear_kind)` reads `ap_save->sent_checks[AP_CHECKLIST_ROW]` (recorded =
  permanently complete, shown with no replay on a later boot).
- `record_complete(clear_kind)` calls `ClearChecker_SetNewUnlock(ap_checklist_mode,
  clear_kind)`, which `check_detection`'s `CODEPATCH_REPLACEFUNC`
  (`CheckDetection_SetNewUnlockReplacement`) intercepts for `ap_checklist_mode`: on a
  fresh cell it runs `RecordCheck`, which resolves the row via `ChecklistModeRow`, sets
  the `sent_checks` bit, fires the "Check sent" textbox and re-evaluates goals — and,
  mid-run (unlock cache invalid), sets `clear[].is_new` and plays the unlock SFX. The
  framework seeds the cell's `is_new`/`is_visible` afterward so the flip-and-sparkle runs
  even for a check that lands outside a gamemode — a stadium objective latched at
  `On3DExit`, say — which hits the cache-valid short-circuit.

So the AP tab's completion path is unchanged from a plain checklist objective:
predicate → `ClearChecker_SetNewUnlock` → `check_detection` → `sent_checks` row → AP.
The framework only adds the cell flags and animation around it.

`check_detection` reads and writes the AP cells through
`gmGetClearcheckerTypeP(ap_checklist_mode)`, which the framework serves from the AP tab's
`GameClearData` block — so the AP record path and the framework presentation operate on
the same block.

The tab's board starts blank and reveals outward, exactly as the vanilla checklists do:
a cell draws only once it is completed, or once a completed orthogonal neighbour has
revealed it. The reveal is positional, so with only 33 of the 120 clear_kinds carrying a
check it will surface boxes that have no objective behind them and cells whose only
neighbours are undefined stay dark until their own check fires. That resolves itself as
the tab fills out toward 120.

`APChecklist_RevealAll` is the exception: the `reveal_checklists` slot option (applied when
the client hands over its options) and the debug menu's "Reveal All Checklists" both route
through `RevealAllChecklists`, which reveals all 120 cells of each vanilla mode and then
calls into the AP tab. The AP tab reveals only the cells in `ap_checks[]` — the other 87
have no objective behind them, so revealing them would show boxes that can never be
checked. It sets `is_visible` only, leaving `is_unlocked` to the normal completion path,
and no-ops when the framework never registered the tab. The framework clears `is_visible`
only in `CC_ApplyLayout`, which runs once per session at boot, long before either caller.

## Wire layout

`APData` is read by the Python client *by field offset*, so its layout is a contract.
Every per-checklist-mode array in `APData` / `APSlotOptions` / `APSave` is
`CHECKLIST_MODE_NUM` wide (`goal`, `checklist_amount`, `goal_checks`, `sent_checks`,
`client_backfill`, `goal_announced`), with the AP tab's entry at `AP_CHECKLIST_ROW` — a
regular row, not an appended tail field. The client never sees the framework-assigned mode
number; it indexes the AP row directly, which is why the runtime mode can move freely.

A block of `_Static_assert(offsetof(APData, f) == 0xNNN, "")` in `main.c` pins the block
boundaries and both edges of every per-mode array. Nothing else checks the layout — the
compiler picks the offsets and the client restates them by hand — so extend that block
when adding fields, and move both sides together.

`APSave` is not part of that contract — the client never reads it — so it grows freely.
Objectives whose predicate counts across boots keep their progress in `APSave.checks`, an
`APCheckProgress` struct sitting at the end of `APSave`, away from the unlock masks: today
`allup_collect_total` (clear_kind 4) and `purple_sr1_wins` (clear_kind 27), with their
targets as `AP_ALLUP_TOTAL_NEED` / `AP_PURPLE_SR1_NEED` in `main.h` so the counter that
stops incrementing and the predicate that reads it share one value. Grouping them means a
new counting objective adds a field to that struct rather than another loose scalar in
`APSave`. An objective satisfiable within one run needs nothing here — it latches in
`ap_check_detect.c`'s transient `ap_observed` and records through `sent_checks`.

Only clear_kinds 0–32 back an AP location, but the tab's grid is 120 cells and the
checkbox-filler cursor can reach the blank ones. `RecordCheck` rejects `clear_kind >=
APCK_NUM` on the AP row so a spent filler can't send a location code the multiworld has
never heard of.

## Cross-mode rewards

`ChecklistRewards_ApplyCrossModeHasReward` (the post-reward-loop hook at `0x8017e07c`)
resolves its row with `ChecklistModeRow` and returns early on `-1`, so the AP tab hosts
cross-mode rewards like any other mode while any *other* custom tab — which has no row —
is skipped. `cross_mode_slots` is `CHECKLIST_MODE_NUM` wide to match.

`RebuildRewardTablesFromShuffle` is the one place that must *not* call `ChecklistModeRow`:
the wire encodes a reward's target as a checklist-mode **row** already (the client writes
`KARData.GameMode`, whose `ARCHIPELAGO` member is `AP_CHECKLIST_ROW` by definition), so it
bounds-checks the value instead of mapping it.

## Tab artwork

`mods/archipelago/assets/ApChecklistTex.dat` is a loadable HSD archive (staged to
the FST root) exporting two `_HSD_ImageDesc` publics:

- `apBannerImg` — RGB5A3 248×128 panel that backs the checkbox grid, the AP logo
  baked in as a faint watermark so the panel stays opaque under the grid.
- `apEmblemImg` — I4 64×64 intensity map of the logo for the top-right tab
  indicator; intensity doubles as alpha and the quad takes the blue theme tint.

`scripts/hsd/make_checklist_textures.py` authors the archive from
`mods/archipelago/assets/ap-icon.png`
(`uv run --with pillow python scripts/hsd/make_checklist_textures.py`). The framework
loads it by name (`tex_file = "ApChecklistTex"`) per tab build and swaps the
checklist's banner/emblem TObjs onto these descriptors.

## Objectives and detection

There are 33 objectives, `clear_kind` 0–32, enumerated as `APCheckKind` in
`ap_check_detect.h`. The numbering is a cross-repo wire contract — the AP location code is
`361 + clear_kind`, and `APLocation` in the apworld's `KARLocations.py` restates the same
order by hand, with nothing mechanically catching a desync. An AP box whose check never
fires is worse than one that doesn't exist: the location still exists in the multiworld and
logic still treats it as reachable, so fill can strand progression on it. A
`_Static_assert` pins `ap_checks[]` to `APCK_NUM` so a cell can't go missing on this side.

Every objective is an in-game achievement. There is no box for booting the game or for
receiving a multiworld item — those complete without playing, so as AP locations they were
free checks the fill could hide progression behind.

Predicates never sample. The framework polls all 33 every frame, in every scene, including
menus and loads — so each one is a single read of state latched elsewhere, and the sampling
lives in two hooks in `ap_check_detect.c`.

**`On3DExit` — stadium and Air Ride results.** hoshi installs the hook at `0x80015274`,
which is the epilogue of `Stadium_ExitMinor` (`0x80014d5c`), the very function whose copy
loop at `0x80015164` latches `GameData.stadium_results` from the live result arrays. So the
block is final and complete when the hook runs. The loop runs `p = 0..3` unconditionally
and `Stadium_ComputeRankByTime` / `ByPoints` / `ByDistance` rank CPU racers alongside
humans, which is what makes the photo-finish boxes solo-achievable. The latch is skipped
entirely for a replay (`GameData.is_replay`) and for the title-screen demo, leaving the
previous round's values in place, so `is_replay` is checked before reading. Per-slot,
`StadiumResults.xc00[p]` must be `0` — the same gate the rankers use — or that slot's
placement and time are stale.

| clear_kind | Objective | Detection |
|---|---|---|
| 13–21 | SINGLE RACE 1–9 finish 1st | `ply_finished[p] && ply_placement[p] == 0` |
| 22 | HIGH JUMP over 1,500 ft | `ply_dist[p] / 0.3048 > 1500` (`ply_dist` is metres) |
| 23 | AIR GLIDER over 2,000 ft | `ply_dist[p] / 0.3048 > 2000` |
| 24 / 25 | KIRBY MELEE 1 / 2 KOs | `ply_points[p] > 100` / `> 60` (the field is a polymorphic score; for Melee it is the KO count) |
| 26 | SINGLE RACE 1 1st on Bulk Star | placement + `Ply_GetMachineKind(p) == VCKIND_BULK` |
| 27 | SINGLE RACE 1 1st 3× as Purple | placement + `Ply_GetColor(p) == KIRBYCOLOR_PURPLE`, counted in `APSave.checks.purple_sr1_wins` |
| 28–31 | DRAG RACE 1–4 photo finish | two finishers' `ply_race_time` within 6 frames (0.10 s at 60 fps) |
| 32 | Air Ride photo finish | same, gated on `MJRKIND_AIR` + `AIRRIDEMODE_RACE` |

Placement alone does not mean a win: `Stadium_ComputeRankByTime` (`0x800108b0`) ranks
slots that never crossed the line as well, falling through to a
`GameData.player_race_distance` comparison when neither slot has its finished flag set. So
a Single Race abandoned while the human led on distance latches `ply_placement == 0`. The
1st-place boxes therefore require `ply_finished[p]` too — which matters most for
`checks.purple_sr1_wins`, a persistent counter with no way back down.

`ply_race_time == 0` is a DNF and is excluded before pairing, or two non-finishers read as
a perfect photo finish. In the time-metric modes `Stadium_ExitMinor` *projects* a finish
time for a CPU that didn't cross the line, so such a CPU appears as a finisher with an
extrapolated time — consistent with what the game's own results screen shows.

**A per-frame proc on each human rider — the City Trial objectives.** Attached from
`On3DLoadEnd`, and only for `Gm_IsInCity() && Gm_GetCityMode() == CITYMODE_TRIAL`, since
"in one game" means one CT Trial run. Counters baseline on the first frame where
`Gm_GetIntroState() == GMINTRO_END`, so patches applied at round start — including
permanent ones an Archipelago item grants — are not read as a collection.

"One game" here is one city segment: `SceneLoad_3D` calls `Player_InitAll` on every 3D
scene load, which memsets all five `PlayerData` slots and so zeroes `item_collect` and
`yakumono_break`. A stadium trip mid-trial therefore restarts these counters. That is the
same scope the vanilla City Trial cells use.

| clear_kind | Objective | Detection |
|---|---|---|
| 0 | Visit the castle flower on foot | **proxy**: `!Rider_IsOnMachine(rd)` and `rd->pos.Y >= 400` held for 30 frames. The flower's real coordinates are not yet known; the dwell requirement is what keeps a dismount-and-fall through the same altitude from counting. Both numbers want tuning against the real spot, ideally into an XZ box around the castle. |
| 1 | Break all the coral in one game | `PlayerStats.yakumono_break[33] >= Gr_GetYakumonoSpawnTotal(33)`. Coral is yakumono descriptor 33 and GrCity1 places 10; the total is read from the stage rather than hardcoded, exactly as the vanilla Sky Sands "break all coral" cell does. `yakumono_break` is zeroed per game, so no baseline is needed. |
| 2 | Go out of bounds | `calcDistanceFromOOB(&rd->pos) < 0` — the engine's own definition, the condition that makes `Machine_CheckFallDeath` respawn the player. |
| 3 | 10+ HP Patches in one game | per-run delta of `item_collect[ITKIND_HP]` |
| 4 | Collect 10 All Ups in total | frame deltas of `item_collect[ITKIND_ALLUP]` fold into `APSave.checks.allup_collect_total` |
| 5–12 | Eat 3+ of each of 8 foods | per-run delta of `item_collect[ITKIND_FOOD*]` |

`item_collect` is bumped by `Ply_IncrementItemCollectNum`, which `Machine_OnTouchItem`
calls for every item application. That includes patches an Archipelago item spawns —
`SpawnItemPlayer` calls `Machine_OnTouchItem` directly to force a same-frame pickup — so an
All Up or HP Patch **received from another world counts**. That is deliberate: the player
sees the pickup happen, and a box that fires too readily is the safe failure direction.

## Files (mod side)

- `mods/archipelago/src/ap_checklist.c` / `.h` — the AP descriptor (checks + labels,
  blue theme, tab art, `is_recorded` / `record_complete` / `is_ready` callbacks) and
  `APChecklist_Register`.
- `mods/archipelago/src/ap_check_detect.c` / `.h` — `APCheckKind`, the sampling hooks,
  and `APCheckDetect_IsSet`.
- `mods/archipelago/assets/ApChecklistTex.dat` — the AP banner/emblem archive.
- `scripts/hsd/make_checklist_textures.py` — authors `ApChecklistTex.dat`.
- `mods/archipelago/src/main.h` / `main.c` — the wire structs, `CHECKLIST_MODE_NUM` /
  `AP_CHECKLIST_ROW`, the runtime `ap_checklist_mode`, and the offset assertions.
- `mods/archipelago/src/check_detection.c` / `.h` — `ChecklistModeRow` /
  `ChecklistRowMode`, and the `ClearChecker_SetNewUnlock` REPLACEFUNC that records AP
  completions.
- `mods/archipelago/src/checklist_rewards.c` — cross-mode reward placement onto AP cells.
- `mods/custom_checklist/` — the framework that renders the tab.
