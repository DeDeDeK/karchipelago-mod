# Stage Gating

Air Ride courses and Top Ride courses can each be individually locked behind Archipelago unlock items. The two modes share nothing but the idea: Air Ride has a vanilla per-course unlock check to repurpose, Top Ride has none, so its course-select screen is gated with input and cursor hooks instead.

## What Is Gated

9 Air Ride courses and 7 Top Ride courses, in two independent masks. Course names and counts come from `AirRideCourse_Names` / `TopRideCourse_Names` in `externals/hoshi/include/stage.h` (`AIRRIDE_NUM` = 9, `TOPRIDE_NUM` = 7). Both masks live in `APSave` (`mods/archipelago/src/main.h`).

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

Grid order comes from the table at `0x80496e60`: `{0, 5, 2, 3, 1, 4, 6, 7, 8, 9}` (position 9 = the random button), so position N → StageKind = `grid[N]`.

| Course | Top Ride course | Grid position | AP item |
|-------:|-----------------|--------------:|--------:|
| 0 | Grass | 0 | 890 |
| 1 | Sand | 1 | 891 |
| 2 | Sky | 2 | 892 |
| 3 | Fire | 3 | 893 |
| 4 | Light | 4 | 894 |
| 5 | Water | 5 | 895 |
| 6 | Metal | 6 | 896 |

The Top Ride grid is a fixed 4×2 with 8 positions; the grid-to-course table at `0x805d51a8` is `{0,1,2,3,4,5,6,8}` — identity for 0–6, with position 7 mapping to value 8 (the random button).

## Entry Points

| Symbol | Kind | Where | Role |
|--------|------|-------|------|
| `GateAirRideStages_OnBoot()` | mod | gate_airride_stages.c | Installs the `REPLACEFUNC` plus 14 instruction patches. |
| `GateAirRideStages_CheckCourseUnlocked(s8)` | mod (static) | gate_airride_stages.c | Replacement for `AirRide_CheckCourseUnlocked`. |
| `GateAirRideStages_UnlockStage(int stage_kind, int announce)` | mod | gate_airride_stages.c | Sets the mask bit; optional textbox. Called from `ap_item_handler.c`. |
| `GateTopRideStages_OnBoot()` | mod | gate_topride_stages.c | Installs two hooks and two `REPLACECALL`s. |
| `GateTopRideStages_CourseSelectCanLaunch(u32)` | mod (static) | gate_topride_stages.c | Conditional-hook body blocking a locked-course launch. |
| `GateTopRideStages_SkipLockedCursor()` | mod (static) | gate_topride_stages.c | Hook body that walks the cursor off a locked grid position. |
| `GateTopRideStages_RandomPick(int)` | mod (static) | gate_topride_stages.c | Replacement for the vanilla `HSD_Randi(7)` random-course picks. |
| `IsGridPosSelectable(int pos)` | mod (static) | gate_topride_stages.c | Unlocked course, or the random button when any course is unlocked. |
| `GateTopRideStages_UnlockStage(int course)` | mod | gate_topride_stages.c | Sets the mask bit and posts a textbox. Called from `ap_item_handler.c`. |
| `AirRide_CheckCourseUnlocked` | game | 0x8000c0e0 | Vanilla per-course unlock check (replaced). |
| `AirRide_RandomStageSelect` | game | 0x8000dd4c | Random course picker. |
| `AirRideSelect_Init` | game | 0x8003c114 | Course select screen init. |
| `gmLanMenu_RenderMainMenuUI` | game | 0x80052028 | LAN menu course availability. |
| `TopRide_CourseSelectThink` | game | 0x8003c8bc | Top Ride course select screen (minor 7). |
| `TopRide_CourseSelectRandomInit` | game | 0x8003c754 | Random pick on scene init. |
| `TopRide_LobbyThink` | game | 0x8002dd34 | Top Ride pre-game lobby (minor 9). |
| `HSD_Randi` | game | 0x8041e668 | Replaced at the two random-course call sites. |

## Air Ride Courses

### Game System

The vanilla game locks exactly one Air Ride course: Nebula Belt (stage 8), gated by a checklist reward. `AirRide_CheckCourseUnlocked` (0x8000c0e0) checks only stage 8 against the checklist, and every caller wraps the call in an `if (stage_kind == 8)` guard, so no other stage ever reaches it.

### Implementation

**Files:** `mods/archipelago/src/gate_airride_stages.c` / `gate_airride_stages.h`

`CODEPATCH_REPLACEFUNC` on `AirRide_CheckCourseUnlocked` installs `GateAirRideStages_CheckCourseUnlocked(s8 stage_kind)`, which checks *all* stages against `airride_stage_unlocked_mask`:

| Input | Return |
|-------|--------|
| `!ap_save` or `stage_kind < 0` | 0 (locked) |
| `stage_kind >= AIRRIDE_NUM` (random button, kind 9) | 1 if `airride_stage_unlocked_mask != 0`, else 0 |
| otherwise | 1 if bit `stage_kind` is set in the mask, else 0 |

The random button is blocked when every stage is locked, which prevents a soft-lock in `AirRide_RandomStageSelect` (no candidates to pick from).

The four caller sites need instruction patching to remove the `stage_kind == 8` guard. Each has a vanilla `cmpwi rX, 8` / `bne` / `li r3, 8` that is patched to `mr r3, rX` / `nop` / `nop`:

| # | Function | Patch addresses | Register moved into r3 |
|---|----------|-----------------|------------------------|
| 1 | `AirRideSelect_Init` (0x8003c114) | 0x8003c210–0x8003c218 | r0 |
| 2 | Course init + random (0x8003b4e8) | 0x8003b520–0x8003b528 | r0 |
| 3 | `AirRide_RandomStageSelect` (0x8000dd4c) | 0x8000ddc4–0x8000ddcc | r27 |
| 4 | `gmLanMenu_RenderMainMenuUI` (0x80052028) | 0x80052070–0x80052080 | r28 |

Site 4 has a slightly different layout (`cmpwi r28, 8` / `beq` / `li r0, 1` / `b` / `li r3, 8`), so it patches 5 instructions: `mr r3, r28` followed by four NOPs.

## Top Ride Courses

### Game System

Unlike Air Ride, the Top Ride course select is a **separate minor scene** (major 5 / minor 7) from the pre-game lobby (major 5 / minor 9). All three Top Ride entry points — Start Game, Free Run, and Time Attack — share this same course select screen.

**Course select screen** (`TopRide_CourseSelectThink`, 0x8003c8bc, minor 7):

- Fixed 4×2 grid with 8 positions: 0–6 = courses, 7 = random button; grid-to-course table at `0x805d51a8`.
- Cursor position stored as a byte at `GameData[0xf8]` (`topride_course_select.cursor`).
- A-button (mask `0x1160`) selects the course, sets `GameData[0x374]`, and transitions to the pre-game lobby.
- Cursor movement (D-pad) navigates the grid with wrapping. All movement paths converge at `0x8003cd18`.

**Pre-game lobby** (`TopRide_LobbyThink`, 0x8002dd34, minor 9): dispatches on `topride_select_ply.init_flag` (`GameData+0x198`) — 0 → `TopRide_PreGameThink` (0x8002c06c, the multiplayer race lobby), nonzero → `TopRide_OnCourseSelect` (0x8002cc30, the solo Free Run / Time Attack path). By the time the player reaches the lobby the course is already selected.

**Vanilla unlock mechanism.** The pre-game lobby has a course unlock check using a lookup table at `0x805d51a0` (course → checklist clear_kind, `{0x1a, 0x1f, 0x1b, 0x1c, 0x20, 0x1d, 0x1e}`), but it only fires from `TopRide_PreGameThink` when launching from the lobby. The course select screen itself has **no** unlock check, no locked-course visual, and no lock icon — the game assumes all 7 courses are always available.

### Implementation

**Files:** `mods/archipelago/src/gate_topride_stages.c` / `gate_topride_stages.h`

Two hooks in the course select screen plus two random-pick replacements.

**Launch block — `CODEPATCH_HOOKCONDITIONALCREATE` at `0x8003ca78`.** The clobbered instruction is `andi. r0, r7, 0x1160` (the launch-button test); the next instruction `beq 0x8003cc18` skips to cursor movement, and `0x8003ca80` is the confirm-sound call (`playSoundFX_menuSound`, `0x80061658`) on the launch path.

- Prologue: stash r7 (combined launch buttons, needed by the clobbered `andi.`) and r5 (direction bits, needed by the `0x8003cc18` D-pad path) on a scratch frame, then `mr r3, r7` to pass the launch buttons as the C argument.
- Epilogue: restore r7 and r5.
- Normal exit (return 0): the clobbered `andi.` runs, launch tested normally.
- Alt exit (return 1): jumps to `0x8003cc18` (cursor-movement path, bypassing launch).

The hook sits on the per-frame input-dispatch instruction, so it runs **every frame**, not only on a launch press. `GateTopRideStages_CourseSelectCanLaunch` therefore gates its own feedback on the launch mask:

| Condition | Return | Effect |
|-----------|--------|--------|
| `!ap_save` | 1 | block (save not ready) |
| no launch press (`!(launch_buttons & 0x1160)`) | 0 | allow — falls through to the D-pad handler unchanged |
| launch press, cursor selectable | 0 | allow — vanilla launch |
| launch press, cursor locked | 1 | `playSoundFX_errorNoise()` + an `"Unlock the <course> course to play!"` / `"Unlock a Top Ride course to play!"` textbox, skip launch |

Gating on the launch mask is essential: the cursor can only ever rest on a locked course when ALL courses are locked (the cursor hook then has nowhere selectable to skip to), so without this the buzzer and textbox would retrigger every frame the cursor sits there.

**Cursor skip — `CODEPATCH_HOOKCREATE` at `0x8003cd18`.** All D-pad movement paths write `GameData[0xf8]` then converge here; the clobbered instruction is `lbz r0, 0x2(r31)` (reads the new cursor position). `GateTopRideStages_SkipLockedCursor()` → `AdjustCursorToUnlocked()`: if the cursor is on a non-selectable position, scan forward (wrapping `(pos + i) % 8`) to the next selectable position and write it back. The clobbered `lbz` then reads the adjusted cursor, and the game's existing `cmpw r3, r0` at `0x8003cd24` detects the change and updates visuals accordingly.

**Random pick — two `CODEPATCH_REPLACECALL`s** over the vanilla `HSD_Randi(7)` calls (`HSD_Randi` at 0x8041e668):

| Address | Function | Purpose |
|---------|----------|---------|
| `0x8003c798` | `TopRide_CourseSelectRandomInit` (0x8003c754) | Random pick on scene init when "random" was previously selected |
| `0x8003cac0` | `TopRide_CourseSelectThink` (0x8003c8bc) | Random pick when A is pressed on the random button (main path) |

Both go to `GateTopRideStages_RandomPick`, which filters the selection by `topride_stage_unlocked_mask` and also respects the used-history bitmask at `GameData+0xFE` (`topride_course_select.used_history_mask`, a `u16`) that vanilla uses to avoid repeats: it builds its candidate set from courses that are both unlocked AND not recently used. If every unlocked course is already used, it clears the used bits for unlocked courses and restarts the cycle. Because the returned pick is guaranteed unused, the vanilla used-mask re-check after the call never re-rolls.

## Save Data

| Mask | Type | Field | Category |
|------|------|-------|----------|
| Air Ride | `u16` | `airride_stage_unlocked_mask` in `APSave` — bit N = StageKind N | `AP_UNLOCK_AIRRIDE_STAGE` |
| Top Ride | `u16` | `topride_stage_unlocked_mask` in `APSave` — bit N = course N | `AP_UNLOCK_TOPRIDE_STAGE` |

Both are exposed through `ArchipelagoAPI`. When `airride_stage_gating_enabled` / `topride_stage_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` in `main.c` pre-fills the corresponding mask with all-1s at connect.

## AP Items

| Mode | Base | IDs | Handler |
|------|-----:|-----|---------|
| Air Ride | `AP_STAGE_UNLOCK_AIRRIDE_BASE` = 870 | 870–878 | `GateAirRideStages_UnlockStage(id - base, /*announce=*/1)` |
| Top Ride | `AP_STAGE_UNLOCK_TOPRIDE_BASE` = 890 | 890–896 | `GateTopRideStages_UnlockStage(id - base)` |

Both set the mask bit, log, and enqueue `"Unlocked Course: <name>"` via `tb_api->EnqueueColoredNoun` with `tb_api->StageColor`. The Air Ride entry point takes an `announce` flag so other grant paths can unlock silently; the Top Ride one always announces.

## Design Decisions

**Instruction patching over function replacement (Air Ride):** the caller functions are large and complex. Rather than replacing them entirely — which would mean re-implementing hundreds of lines of unrelated logic — the gate surgically patches the 3–5 guard instructions at each call site. This is minimal and doesn't risk breaking unrelated behavior.

**Input gating over screen rebuild (Top Ride):** Top Ride's course select has no unlock concept at all, so the gate intercepts the launch press and the cursor instead of rebuilding the grid.

## Known Limitations

Locked Top Ride courses remain visually present in the grid — the cursor simply skips them. Top Ride has no built-in "locked course" visual state, and fully hiding locked entries would require reimplementing the course select grid layout (hardcoded 4×2 with fixed 2D cursor navigation).
