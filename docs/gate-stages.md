# Stage Gating

Air Ride's 9 courses and Top Ride's 7 courses can each be individually locked behind an Archipelago unlock item. AP items 870-878 and 890-896 route through `ap_item_handler.c` to `GateAirRideStages_UnlockStage(kind, announce)` / `GateTopRideStages_UnlockStage(course)`, which set a bit in `APSave.airride_stage_unlocked_mask` / `topride_stage_unlocked_mask` (both `u16`) and post an `"Unlocked Course: <name>"` textbox with `tb_api->StageColor`. The Air Ride entry point takes an `announce` flag so other grant paths can unlock silently; the Top Ride one always announces.

The two modes share nothing but the idea. Air Ride has a vanilla per-course unlock check to repurpose; Top Ride has none, so its course select is gated with input and cursor hooks instead. Course names and counts come from `AirRideCourse_Names` / `TopRideCourse_Names` in `externals/hoshi/include/stage.h`.

Both masks are exposed through `ArchipelagoAPI` as `AP_UNLOCK_AIRRIDE_STAGE` / `AP_UNLOCK_TOPRIDE_STAGE`. When `airride_stage_gating_enabled` / `topride_stage_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` (`main.c`) pre-fills the corresponding mask with all-1s at connect.

| StageKind | Air Ride course | Grid position | AP item |
|----------:|-----------------|--------------:|--------:|
| 0 | Fantasy Meadows | 0 | 870 |
| 1 | Magma Flows | 4 | 871 |
| 2 | Sky Sands | 2 | 872 |
| 3 | Frozen Hillside | 3 | 873 |
| 4 | Beanstalk Park | 5 | 874 |
| 5 | Celestial Valley | 1 | 875 |
| 6 | Machine Passage | 6 | 876 |
| 7 | Checker Knights | 7 | 877 |
| 8 | Nebula Belt | 8 | 878 |

Grid order comes from the table at 0x80496e60: `{0, 5, 2, 3, 1, 4, 6, 7, 8, 9}` (position 9 = the random button), so position N is StageKind `grid[N]`.

| Course | Top Ride course | Grid position | AP item |
|-------:|-----------------|--------------:|--------:|
| 0 | Grass | 0 | 890 |
| 1 | Sand | 1 | 891 |
| 2 | Sky | 2 | 892 |
| 3 | Fire | 3 | 893 |
| 4 | Light | 4 | 894 |
| 5 | Water | 5 | 895 |
| 6 | Metal | 6 | 896 |

The Top Ride grid is a fixed 4x2 with 8 positions; the grid-to-course table at 0x805d51a8 is `{0,1,2,3,4,5,6,8}` - identity for 0-6, with position 7 mapping to value 8 (the random button).

## Air Ride Courses

**File:** `mods/archipelago/src/gate_airride_stages.c`.

The vanilla game locks exactly one Air Ride course: Nebula Belt (stage 8), gated by a checklist reward. `AirRide_CheckCourseUnlocked` (0x8000c0e0) checks only stage 8 against the checklist, and every caller wraps the call in an `if (stage_kind == 8)` guard, so no other stage ever reaches it.

`CODEPATCH_REPLACEFUNC` installs `GateAirRideStages_CheckCourseUnlocked(s8 stage_kind)`, which checks all stages against the mask: a negative kind or a missing `ap_save` returns locked, `stage_kind >= AIRRIDE_NUM` (the random button, kind 9) returns unlocked iff the mask is nonzero, and anything else returns its mask bit. Blocking the random button on an empty mask is what keeps `AirRide_RandomStageSelect` (0x8000dd4c) from soft-locking with no candidates to pick from.

The four caller sites then need their `stage_kind == 8` guard removed. The caller functions are large, so rather than replacing them the gate patches the 3-5 guard instructions at each site: the vanilla `cmpwi rX, 8` / `bne` / `li r3, 8` becomes `mr r3, rX` / `nop` / `nop`.

| Function | Patch addresses | Register moved into r3 |
|----------|-----------------|------------------------|
| `AirRideSelect_Init` (0x8003c114) | 0x8003c210-0x8003c218 | r0 |
| Course init + random (0x8003b4e8) | 0x8003b520-0x8003b528 | r0 |
| `AirRide_RandomStageSelect` (0x8000dd4c) | 0x8000ddc4-0x8000ddcc | r27 |
| `gmLanMenu_RenderMainMenuUI` (0x80052028) | 0x80052070-0x80052080 | r28 |

The LAN menu site has a longer guard (`cmpwi r28, 8` / `beq` / `li r0, 1` / `b` / `li r3, 8`), so it takes five instructions: `mr r3, r28` plus four NOPs.

## Top Ride Courses

**File:** `mods/archipelago/src/gate_topride_stages.c`.

Unlike Air Ride, the Top Ride course select is a **separate minor scene** (major 5 / minor 7) from the pre-game lobby (major 5 / minor 9), and all three Top Ride entry points - Start Game, Free Run, Time Attack - share it. `TopRide_CourseSelectThink` (0x8003c8bc) drives the grid: the cursor is a byte at `GameData[0xf8]` (`topride_course_select.cursor`), the A-button (mask 0x1160) selects the course, sets `GameData[0x374]` and transitions to the lobby, and D-pad movement wraps around the grid with every path converging at 0x8003cd18. `TopRide_LobbyThink` (0x8002dd34) then dispatches on `topride_select_ply.init_flag` (`GameData+0x198`) to `TopRide_PreGameThink` (0x8002c06c, multiplayer race) or `TopRide_OnCourseSelect` (0x8002cc30, solo Free Run / Time Attack) - by then the course is already chosen.

Vanilla does have a course unlock check, using a lookup table at 0x805d51a0 (course to checklist clear_kind, `{0x1a, 0x1f, 0x1b, 0x1c, 0x20, 0x1d, 0x1e}`), but it only fires from `TopRide_PreGameThink` when launching from the lobby. The course select screen itself has no unlock check, no locked-course visual and no lock icon - the game assumes all 7 courses are always available. So the gate intercepts the launch press and the cursor rather than rebuilding the grid.

**Launch block - `CODEPATCH_HOOKCONDITIONALCREATE` at 0x8003ca78.** The clobbered instruction is `andi. r0, r7, 0x1160` (the launch-button test); the following `beq 0x8003cc18` skips to cursor movement, and 0x8003ca80 is the confirm-sound call (`playSoundFX_menuSound`, 0x80061658) on the launch path. The prologue stashes r7 (combined launch buttons, needed by the clobbered `andi.`) and r5 (direction bits, needed by the 0x8003cc18 D-pad path) on a scratch frame and passes r7 as the C argument; the epilogue restores both. Returning 0 runs the clobbered `andi.` and tests the launch normally; returning 1 jumps to 0x8003cc18, bypassing the launch.

The hook sits on the per-frame input-dispatch instruction, so it runs **every frame**, not only on a launch press. `GateTopRideStages_CourseSelectCanLaunch` therefore gates its own feedback on the launch mask: no launch press returns 0 immediately (falling through to the D-pad handler unchanged), a launch press on a selectable cursor returns 0, and a launch press on a locked cursor plays `playSoundFX_errorNoise()`, posts an `"Unlock the <course> course to play!"` (or `"Unlock a Top Ride course to play!"` when nothing is unlocked) textbox, and returns 1. A missing `ap_save` also blocks. Gating on the launch mask is essential: the cursor can only ever rest on a locked course when *all* courses are locked - the cursor hook has nowhere selectable to move it to - so without it the buzzer and textbox would retrigger every frame the cursor sits there.

**Cursor skip - `CODEPATCH_HOOKCREATE` at 0x8003cd18.** All D-pad movement paths write `GameData[0xf8]` and then converge here, at `lbz r0, 0x2(r31)` (the read-back of the new cursor position). `GateTopRideStages_SkipLockedCursor` calls `AdjustCursorToUnlocked()`, which - if the cursor landed on a non-selectable position - scans forward with wrapping `(pos + i) % 8` to the next selectable one and writes it back. A position is selectable if its course is unlocked, or it is the random button and any course is unlocked. The clobbered `lbz` then reads the adjusted cursor and the game's existing `cmpw r3, r0` at 0x8003cd24 sees the change and updates visuals.

**Random pick - two `CODEPATCH_REPLACECALL`s** over the vanilla `HSD_Randi(7)` calls (`HSD_Randi` at 0x8041e668): 0x8003c798 in `TopRide_CourseSelectRandomInit` (0x8003c754, the pick on scene init when random was previously selected) and 0x8003cac0 in `TopRide_CourseSelectThink` (A pressed on the random button). Both go to `GateTopRideStages_RandomPick`, which filters candidates by the mask and also respects the used-history bitmask at `GameData+0xFE` (`topride_course_select.used_history_mask`, a `u16`) that vanilla uses to avoid repeats: candidates must be both unlocked and not recently used, and if every unlocked course is already used it clears the used bits for unlocked courses and restarts the cycle. Because the returned pick is guaranteed unused, the vanilla used-mask re-check after the call never re-rolls.

## Known Limitations

Locked Top Ride courses stay visually present in the grid - the cursor simply skips them. Top Ride has no built-in "locked course" visual state, and hiding locked entries would mean reimplementing the hardcoded 4x2 grid layout and its 2D cursor navigation.
