# Stage Gating

Individually locks Air Ride courses and Top Ride courses behind Archipelago item grants. Two independent implementations:

| Mode | Source | Boot entry | Grant entry | Save mask |
|------|--------|------------|-------------|-----------|
| Air Ride | `mods/archipelago/src/gate_airride_stages.c` | `GateAirRideStages_OnBoot()` | `GateAirRideStages_UnlockStage(stage_kind, announce)` | `airride_stage_unlocked_mask` (u16) |
| Top Ride | `mods/archipelago/src/gate_topride_stages.c` | `GateTopRideStages_OnBoot()` | `GateTopRideStages_UnlockStage(course)` | `topride_stage_unlocked_mask` (u16) |

Course names and counts come from `AirRideCourse_Names` / `TopRideCourse_Names` in `externals/hoshi/include/stage.h` (`AIRRIDE_NUM` = 9, `TOPRIDE_NUM` = 7). Both masks live in the `APSave` struct (`mods/archipelago/src/main.h`).

## Air Ride Stages — Implemented

### Overview

9 Air Ride courses (StageKind 0–8) can be individually locked. When locked, the course cannot be selected on the course select screen and is excluded from random stage selection.

| StageKind | Course | Grid Position |
|-----------|--------|---------------|
| 0 | Fantasy Meadows | 0 |
| 1 | Magma Flows | 4 |
| 2 | Sky Sands | 2 |
| 3 | Frozen Hillside | 3 |
| 4 | Beanstalk Park | 5 |
| 5 | Celestial Valley | 1 |
| 6 | Machine Passage | 6 |
| 7 | Checker Knights | 7 |
| 8 | Nebula Belt | 8 |

Grid order from table at `0x80496e60`: `{0, 5, 2, 3, 1, 4, 6, 7, 8, 9}` (position 9 = random button), so position N → StageKind = grid[N].

### Game System

The vanilla game only has one locked stage: Nebula Belt (stage 8), gated by a checklist reward. `AirRide_CheckCourseUnlocked` (0x8000c0e0) checks only stage 8 against the checklist. All callers have an `if (stage_kind == 8)` guard that short-circuits for other stages — they never even call the unlock check.

### Implementation

**Files:** `gate_airride_stages.c` / `gate_airride_stages.h`

`CODEPATCH_REPLACEFUNC` on `AirRide_CheckCourseUnlocked` (0x8000c0e0) installs `GateAirRideStages_CheckCourseUnlocked(s8 stage_kind)`, which checks ALL stages against `airride_stage_unlocked_mask` instead of the vanilla checklist:

| Input | Return |
|-------|--------|
| `!ap_save` or `stage_kind < 0` | 0 (locked) |
| `stage_kind >= AIRRIDE_NUM` (random button, kind 9) | 1 if `airride_stage_unlocked_mask != 0`, else 0 |
| otherwise | 1 if bit `stage_kind` is set in the mask, else 0 |

The random button is blocked when every stage is locked, which prevents a soft-lock in `AirRide_RandomStageSelect` (no candidates to pick from). Stages are granted via `GateAirRideStages_UnlockStage(stage_kind, announce)`, which sets the mask bit and — when `announce` is set — posts an `"Unlocked Course: <name>"` textbox.

The four caller sites need instruction patching to remove the `stage_kind == 8` guard. Each has a vanilla `cmpwi rX, 8` / `bne` / `li r3, 8` that is patched to `mr r3, rX` / `nop` / `nop`:

| # | Function | Patch Addresses | Description |
|---|----------|-----------------|-------------|
| 1 | `AirRideSelect_Init` (0x8003c114) | 0x8003c210–0x8003c218 | Course select screen init |
| 2 | Course init + random (0x8003b4e8) | 0x8003b520–0x8003b528 | Course init with default random pick |
| 3 | `AirRide_RandomStageSelect` (0x8000dd4c) | 0x8000ddc4–0x8000ddcc | Random course picker (excludes locked from pool) |
| 4 | `gmLanMenu_RenderMainMenuUI` (0x80052028) | 0x80052070–0x80052080 | LAN menu course availability |

Site 4 has a slightly different layout (`cmpwi r28, 8` / `beq` / `li r0, 1` / `b` / `li r3, 8`), so it patches 5 instructions: `mr r3, r28` followed by four NOPs.

### Save Data

`u16 airride_stage_unlocked_mask` in `APSave` — bit N = StageKind N.

### AP Items

9 items, `AP_STAGE_UNLOCK_AIRRIDE_BASE` (870) + StageKind index. IDs 870–878.

### Design Decisions

**Instruction patching over function replacement:** The caller functions are large and complex. Rather than replacing them entirely (which would require re-implementing hundreds of lines of unrelated logic), the gating surgically patches the 2–3 guard instructions at each call site. This is minimal and doesn't risk breaking unrelated behavior in those functions.

---

## Top Ride Stages — Implemented

### Overview

7 Top Ride courses: Grass (0), Sand (1), Sky (2), Fire (3), Light (4), Water (5), Metal (6).

### Game Architecture

Unlike Air Ride, the Top Ride course select is a **separate minor scene** (major 5 / minor 7) from the pre-game lobby (major 5 / minor 9). All three Top Ride entry points — Start Game, Free Run, and Time Attack — share this same course select screen.

**Course select screen** (`TopRide_CourseSelectThink`, 0x8003c8bc, minor scene 7):
- Fixed 4×2 grid with 8 positions: 0–6 = courses, 7 = random button.
- Grid-to-course mapping table at `DAT_805d51a8`: identity for 0–6 (`{0,1,2,3,4,5,6,8}`), position 7 maps to value 8 (random).
- Cursor position stored as a byte at `GameData[0xf8]`.
- A-button (mask `0x1160`) selects the course, sets `GameData[0x374]`, and transitions to the pre-game lobby.
- Cursor movement (D-pad) navigates the grid with wrapping. All movement paths converge at `0x8003cd18`.

**Pre-game lobby** (`TopRide_LobbyThink`, 0x8002dd34, minor scene 9):
- Dispatches to `TopRide_PreGameThink` (phase 0) and `TopRide_OnCourseSelect` (phase 1).
- By the time the player reaches the lobby, the course is already selected.

**Key difference from Air Ride:** In vanilla, no Top Ride courses are ever locked. There is no built-in locked-course visual or lock icon. The game assumes all 7 courses are always available.

### Vanilla Course Unlock Mechanism

The pre-game lobby has a course unlock check using a lookup table at `DAT_805d51a0` (course → checklist clear_kind), but this only fires from `TopRide_PreGameThink` when launching from the lobby. The course select screen itself has no unlock check.

### Implementation

**Files:** `gate_topride_stages.c` / `gate_topride_stages.h`

Two hooks in the course select screen (`TopRide_CourseSelectThink`, 0x8003c8bc):

**Hook 1 — A-button block at 0x8003ca78:**

Prevents launching a locked course. The clobbered instruction is `andi. r0, r7, 0x1160` (A-button test).

```asm
8003ca78: andi.  r0, r7, 0x1160        # test launch buttons (clobbered)
8003ca7c: beq    0x8003cc18            # if not pressed, skip to cursor movement
8003ca80: bl     playSoundFX_menuSound # confirm sound → launch path (0x80061658)
```

**Hook strategy:** `CODEPATCH_HOOKCONDITIONALCREATE` at `0x8003ca78`:
- Prologue: save r7 (combined launch buttons, needed by the clobbered `andi.`) and r5 (direction bits, needed by the `0x8003cc18` D-pad path), then `mr r3, r7` to pass the launch buttons as the C argument.
- Hook: `GateTopRideStages_CourseSelectCanLaunch(u32 launch_buttons)`.
- Epilogue: restore r7 and r5.
- Normal exit (return 0): clobbered `andi.` runs, launch tested normally.
- Alt exit (return 1): jumps to `0x8003cc18` (cursor-movement path, bypassing launch).

The hook sits on the per-frame input-dispatch instruction, so it runs **every frame**, not only on a launch press. The function gates on the launch mask (`0x1160`, the same A/Start rising-edge bits the clobbered `andi.` tests):

| Condition | Return | Effect |
|-----------|--------|--------|
| `!ap_save` | 1 | block (save not ready) |
| no launch press (`!(launch_buttons & 0x1160)`) | 0 | allow — falls through to the D-pad handler unchanged |
| launch press, cursor selectable | 0 | allow — vanilla launch |
| launch press, cursor locked | 1 | `playSoundFX_errorNoise()` + an `"Unlock the <course> course to play!"` / `"Unlock a Top Ride course to play!"` textbox, skip launch |

"Selectable" (`IsGridPosSelectable`) means an unlocked course, or the random button (pos ≥ `TOPRIDE_NUM`) when at least one course is unlocked. Gating on the launch mask is essential: the cursor can only ever rest on a locked course when ALL courses are locked (Hook 2 then has nowhere selectable to skip to), so without this gate the buzzer/textbox would retrigger every frame the cursor sits on it.

**Hook 2 — Cursor skip at 0x8003cd18:**

Makes the cursor skip locked courses during navigation. All D-pad movement paths write to `GameData[0xf8]` then converge at `0x8003cd18`. The clobbered instruction is `lbz r0, 0x2(r31)` (reads the new cursor position).

**Hook strategy:** `CODEPATCH_HOOKCREATE` at `0x8003cd18`:
- Hook: `GateTopRideStages_SkipLockedCursor()` → `AdjustCursorToUnlocked()` — if `topride_course_select.cursor` (`GameData[0xf8]`) is on a locked, non-selectable position, scans forward (wrapping `(pos + i) % 8`) to the next selectable position (unlocked course or the random button) and writes it back.
- The clobbered `lbz` then reads the (potentially adjusted) cursor, and the game's existing `cmpw r3, r0` at `0x8003cd24` detects the change and updates visuals accordingly.

**Limitation:** Locked courses remain visually present in the grid — the cursor simply skips them. Top Ride has no built-in "locked course" visual state, and fully hiding locked entries would require reimplementing the course select grid layout (hardcoded 4×2 with fixed 2D cursor navigation).

### Save Data

`u16 topride_stage_unlocked_mask` in `APSave` — bit N = course N.

### Random Pick Hooks

Two `CODEPATCH_REPLACECALL` hooks replace the vanilla `HSD_Randi(7)` calls (`HSD_Randi` @ 0x8041e668) that implement random course selection:

| Address | Function | Purpose |
|---------|----------|---------|
| `0x8003c798` | `TopRide_CourseSelectRandomInit` (0x8003c754) | Random pick on scene init when "random" was previously selected |
| `0x8003cac0` | `TopRide_CourseSelectThink` (0x8003c8bc) | Random pick when A is pressed on the random button (main path) |

Both are replaced with `GateTopRideStages_RandomPick`, which filters the selection by `topride_stage_unlocked_mask`. It also respects the used-history bitmask at `GameData+0xFE` (`topride_course_select.used_history_mask`, a `u16`) that vanilla uses to avoid repeats: it builds its candidate set from courses that are both unlocked AND not recently used. If every unlocked course is already used, it clears the used bits for unlocked courses and restarts the cycle. Because the returned pick is guaranteed unused, the vanilla used-mask re-check after the call never re-rolls.

### AP Items

7 items, `AP_STAGE_UNLOCK_TOPRIDE_BASE` (890) + course index. IDs 890–896.
