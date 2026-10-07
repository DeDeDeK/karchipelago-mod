# Stage Gating

Air Ride's 9 courses and Top Ride's 7 courses can each be individually locked behind an Archipelago unlock item. AP items 870-878 and 890-896 route through `ap_item_handler.c` to `GateAirRideStages_UnlockStage(kind, announce)` / `GateTopRideStages_UnlockStage(course)`, which set a bit in `APSave.airride_stage_unlocked_mask` / `topride_stage_unlocked_mask` (both `u16`) and announce `"Unlocked Course: <name>"` in `tb_api->StageColor` through `APAnnounce_Grant` (shown only with Messages -> Local -> Items on, default Off). The Air Ride entry point takes an `announce` flag so other grant paths can unlock silently; the Top Ride one always announces.

The two modes share nothing but the idea. Air Ride has a vanilla per-course unlock check to repurpose; Top Ride has none, so its course select is gated by rewriting the grid table the scene reads, plus an input hook. Both screens end up looking and behaving the same: a locked course shows a gray "no" icon and a blank preview, the cursor can rest on it, and confirming it is refused. Course names and counts come from `AirRideCourse_Names` / `TopRideCourse_Names` in `externals/hoshi/include/stage.h`.

Both masks are exposed through `ArchipelagoAPI` as `AP_UNLOCK_AIRRIDE_STAGE` / `AP_UNLOCK_TOPRIDE_STAGE`. When `airride_stage_gating_enabled` / `topride_stage_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` (`ap_options.c`) pre-fills the corresponding mask with all-1s when the first slot options arrive.

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

`CODEPATCH_REPLACEFUNC` installs `GateAirRideStages_CheckCourseUnlocked(s8 stage_kind)`, which checks all stages against the mask: a negative kind returns locked, `stage_kind >= AIRRIDE_NUM` (the random button, kind 9) returns unlocked iff the mask is nonzero, and anything else returns its mask bit. Blocking the random button on an empty mask is what keeps `AirRide_RandomStageSelect` (0x8000dd4c) from soft-locking with no candidates to pick from.

The four caller sites then need their `stage_kind == 8` guard removed. The caller functions are large, so rather than replacing them the gate patches the 3-5 guard instructions at each site: the vanilla `cmpwi rX, 8` / `bne` / `li r3, 8` becomes `mr r3, rX` / `nop` / `nop`.

| Function | Patch addresses | Register moved into r3 |
|----------|-----------------|------------------------|
| `AirRideSelect_Init` (0x8003c114) | 0x8003c210-0x8003c218 | r0 |
| `AirRideSelect_StartRandomCourse` (0x8003b4e8) | 0x8003b520-0x8003b528 | r0 |
| `AirRide_RandomStageSelect` (0x8000dd4c) | 0x8000ddc4-0x8000ddcc | r27 |
| `gmLanMenu_RenderMainMenuUI` (0x80052028) | 0x80052070-0x80052080 | r28 |

The LAN menu site has a longer guard (`cmpwi r28, 8` / `beq` / `li r0, 1` / `b` / `li r3, 8`), so it takes five instructions: `mr r3, r28` plus four NOPs.

## Top Ride Courses

**File:** `mods/archipelago/src/gate_topride_stages.c`.

Unlike Air Ride, the Top Ride course select is a **separate minor scene** (major 5 / minor 7) from the pre-game lobby (major 5 / minor 9), and all three Top Ride entry points - Start Game, Free Run, Time Attack - share it. `TopRide_CourseSelectThink` (0x8003c8bc) drives the grid: the cursor is a byte at `GameData[0xf8]` (`topride_course_select.cursor`), a rising edge of A, Start, L or R (mask 0x1160) selects the course, sets `GameData[0x374]` and transitions to the lobby, and D-pad movement wraps around the grid with every path converging at 0x8003cd18. `TopRide_LobbyThink` (0x8002dd34) then dispatches on `topride_select_ply.init_flag` (`GameData+0x198`) to `TopRide_PreGameThink` (0x8002c06c, multiplayer race) or `TopRide_OnCourseSelect` (0x8002cc30, solo Free Run / Time Attack) - by then the course is already chosen.

Vanilla does have a course unlock check, using a lookup table at 0x805d51a0 (course to checklist clear_kind, `{0x1a, 0x1f, 0x1b, 0x1c, 0x20, 0x1d, 0x1e}`), but it only fires from `TopRide_PreGameThink` when launching from the lobby. The course select screen itself assumes all 7 courses are always available.

**The grid table.** Every per-position decision the screen makes goes through `stc_topride_course_grid` (0x805d51a8, r13-0x7f38), the grid-to-course table `{0,1,2,3,4,5,6,8}`. Its only readers are `TopRide_CourseSelectInit` (0x8003d0dc), `TopRide_CourseSelectThink` and `TopRide_CourseSelectRandomInit` (0x8003c754). The value read for a position is also used directly as an animation frame:

- `TopRide_CourseSelectCreateIcon` (0x801339f0 -> 0x8014e8d4), called once per position from `TopRide_CourseSelectInit`, builds the grid icon from `ScMenSelmapCursorm2d_scene_models` (`MnSelmapm2dAll.dat`) and sets its TexAnim frame to the value with `MainMenu_SetTexAnimFrame` (0x80138c1c). The icon GObjs are stored per position at `ScMenuCommon.main.topride_course_select.icon_gobj[8]` (0x488).
- `TopRide_CourseSelectSetPanorama` (0x8014e0d0), reached from the cursor-move path through 0x80133a8c, sets the frame of the preview panorama `ScMenSelmapPanoramam2d_scene_models` to the value. At load the same frame goes through 0x80133a30 -> 0x8014e048.

Both models key the same frames: 0-6 are the courses, 8 the random button, and **9 a locked state vanilla never uses** - the icon TexAnim maps it to a gray circle-and-slash image (the same art as Air Ride's locked map icon, which `MnSelmapAll.dat` keys at frame 10), and every panorama TexAnim maps it to an 8x8 blank. hoshi names these values `TOPRIDE_GRID_RANDOM` (8) and `TOPRIDE_GRID_LOCKED` (9).

**Scene wrappers.** `gate_topride_stages.c` wraps three of minor 7's `MinorSceneDesc` callbacks:

- `cb_Load` writes `TOPRIDE_GRID_LOCKED` into the table for every position that is not selectable, then runs `TopRide_CourseSelectInit`. A position is selectable if its course is unlocked, or it is the random button and any course is unlocked. Vanilla then draws the lock icon and blank panorama by itself.
- `cb_ThinkPreGObjProc` (`TopRide_CourseSelectDispatch`, 0x8003d574) compares the table against the live mask every frame before `TopRide_CourseSelectThink` runs. A position whose state changed (a course unlocked while the screen is up) gets its table entry, its per-position laps (`TopRide_GetCourseDefaultLaps`, 0x80312680, or -1), its icon frame and - if the cursor is on it - the panorama rewritten. This keeps the table in step with the mask before the launch path reads it.
- `cb_Exit` (`TopRide_CourseSelectExit`, 0x8003d550) restores the vanilla table. With course selection Off, `TopRide_MinorExit` (0x8003eed8) calls `TopRide_CourseSelectRandomInit` in place of this scene, and that maps its pick through the table - a course unlocked since the last visit would otherwise still read 9 and reach `TopRide_SetSelectedCourse`.

**Random-path branches.** Wherever the scene indexes per-course data with a grid value, it first tests the value with `cmpwi 8` / `bne` and gives the random button a placeholder. The gate turns those five `bne`s into `blt` so 9 takes the random path too, rather than indexing past the 7-course tables:

| Address | Function | Data guarded |
|---------|----------|--------------|
| 0x8003d150 | `TopRide_CourseSelectInit` | per-position default laps, `GameData.topride_course_select.laps[]` (0x101); -1 for random |
| 0x8003d20c | `TopRide_CourseSelectInit` | Time Attack best time (`GameData+0x1224 + course*0x10`) |
| 0x8003d354 | `TopRide_CourseSelectInit` | Free Run best time (`GameData+0x12a4 + course*0x10`) |
| 0x8003cdbc | `TopRide_CourseSelectThink` | Time Attack best time on cursor move |
| 0x8003cf04 | `TopRide_CourseSelectThink` | Free Run best time on cursor move |

A locked position therefore shows the random button's lap readout and blank best time, the way Air Ride blanks both for its locked kind 10.

**Launch block - `CODEPATCH_HOOKCONDITIONALCREATE` at 0x8003ca78.** The clobbered instruction is `andi. r0, r7, 0x1160` (the launch-button test); the following `beq 0x8003cc18` skips to cursor movement, and 0x8003ca80 is the confirm-sound call (`Gm_PlayPauseSFX`, 0x80061658) on the launch path. The prologue stashes r7 (combined launch buttons, needed by the clobbered `andi.`) and r5 (direction bits, needed by the 0x8003cc18 D-pad path) on a scratch frame and passes r7 as the C argument; the epilogue restores both. Returning 0 runs the clobbered `andi.` and tests the launch normally; returning 1 jumps to 0x8003cc18, bypassing the launch.

The hook sits on the per-frame input-dispatch instruction, so it runs **every frame**, not only on a launch press. `GateTopRideStages_CourseSelectCanLaunch` therefore gates its own feedback on the launch mask: no launch press returns 0 immediately (falling through to the D-pad handler unchanged), a launch press on a selectable cursor returns 0, and a launch press on a locked cursor plays `playSoundFX_errorNoise()` and returns 1. The lock icon says why, so there is no message, matching Air Ride. The cursor moves freely over locked positions, so without the launch-mask gate the buzzer would retrigger every frame the cursor rests on one.

**Random pick - two `CODEPATCH_REPLACECALL`s** over the vanilla `HSD_Randi(7)` calls (`HSD_Randi` at 0x8041e668): 0x8003c798 in `TopRide_CourseSelectRandomInit` (0x8003c754, which `TopRide_MinorExit` calls in place of the course select when course selection is Off) and 0x8003cac0 in `TopRide_CourseSelectThink` (A pressed on the random button). Both go to `GateTopRideStages_RandomPick`, which filters candidates by the mask and also respects the used-history bitmask at `GameData+0xFE` (`topride_course_select.used_history_mask`, a `u16`) that vanilla uses to avoid repeats: candidates must be both unlocked and not recently used, and if every unlocked course is already used it clears the used bits for unlocked courses and restarts the cycle. Because the returned pick is guaranteed unused, the vanilla used-mask re-check after the call never re-rolls - and when no course is unlocked at all, the fallback `return 0` first clears bit 0 of the used mask, since the caller spins on that re-check (`and. r0,r3,r4` / `bne` at 0x8003c7a8) until the returned index reads unused.
