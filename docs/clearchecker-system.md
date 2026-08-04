# Clear Checker (Checklist) System

The clear-checker (`plclearcheckerlib`) is the game layer that stores checklist cell
completion, resolves each cell's reward, and drives the unlock presentation. This doc
covers the vanilla machinery plus the AP mod's two integration surfaces: **inbound
rewards** (`checklist_rewards.c`, AP item delivery and cross-mode reward shuffle) and
**outbound checks** (`check_detection.c`, which makes the mod the source of truth for
which checkboxes the player has completed). The per-player item/box collect counters
that feed the "pick up N items" cells are in `checklist-stat-tracking.md`; the cell
layout arithmetic is in `checklist-grid-geometry.md`.

## Entry Points

| Concern | Function / hook | Address / file |
|---------|-----------------|----------------|
| Mod boot — inbound | `ChecklistRewards_OnBoot` (`AllocateRewardTables` + REPLACEFUNCs) | `checklist_rewards.c` |
| Mod boot — outbound | `CheckDetection_OnBoot` | `check_detection.c` |
| AP item received | `ChecklistRewards_Grant` | `checklist_rewards.c` |
| Player completes a box | `CheckDetection_SetNewUnlockReplacement` (`0x8004a054`) | `check_detection.c` |
| Goal evaluation | `CheckDetection_EvaluateGoal` | `check_detection.c` |

## Terminology

- **Location**: An in-game event that can be "checked" to send an item into the
  multiworld. Every vanilla checkbox (all 360: 120 per mode) is a location.
- **Item**: A reward that can be placed at any location in the multiworld. KAR items
  include machines, characters, music, sound tests, courses, stadiums, etc.
- **clear_kind**: Index 0–119 identifying a checkbox within a mode's
  `GameClearData.clear[120]` array. Each checkbox has an objective ("Finish 1st while
  flying") and optionally a reward ("Machine: Winged Star").
- **reward_index**: Index into the per-mode `RewardEntry` table. Each entry has a
  `clear_kind` (which checkbox), `reward_type`, and `reward_param`.
- **row**: Index into the mod's per-checklist-mode arrays. `CHECKLIST_MODE_NUM` (4) =
  the three game modes plus the AP checklist tab at `AP_CHECKLIST_ROW` (3).
  `ChecklistModeRow(mode)` maps a runtime checklist mode to its row.

## Game System

### GameClearData (0xF4 bytes per mode)

| Offset | Field | Description |
|--------|-------|-------------|
| 0x00 | `new_unlock_flag` | Nonzero when new unlocks exist requiring visual update |
| 0x01 | `display_state` | High nibble = pending new unlocks, low nibble bit 0 = shown/acknowledged |
| 0x02 | `checkbox_filler_num` | Number of checkbox fillers available to use |
| 0x03 | `checkbox_filler_list_len` | Length of filler list shown in UI (max 5) |
| 0x04 | `grid_mapping[120]` | Maps clear_kind → visual grid position (0–119) |
| 0x7C | `clear[120]` | Per-checkbox status bitfield (1 byte each) |

Each mode's block is embedded in `GameData` (Air Ride `+0xd68`, Top Ride `+0xe80`,
City Trial `+0xfa8`), immediately followed by that mode's cross-game records tail at
`+0xF4`.

### Clear byte bitfield (`clear[clear_kind]`)

| Bit | Mask | Field | Description |
|-----|------|-------|-------------|
| 7 | 0x80 | x0_80 | Unknown |
| 6 | 0x40 | x0_40 | Unknown |
| 5 | 0x20 | x0_20 | Unknown |
| 4 | 0x10 | `is_visible` | Visible in the checklist grid |
| 3 | 0x08 | `has_reward` | Set by `Checklist_SetRewardFlagOnUnlocks` when slot is unlocked and has a reward |
| 2 | 0x04 | `is_unlocked` | Raised after displaying the unlocked animation |
| 1 | 0x02 | `is_filler` | Checkbox filler was used on this slot |
| 0 | 0x01 | `is_new` | Objective completed, pending acknowledgement animation |

### Key Vanilla Functions

| Address | Function | Description |
|---------|----------|-------------|
| `0x800076a0` | `gmGetClearcheckerTypeP(mode)` | Returns `GameClearData*` for a mode; asserts for `mode >= 3` |
| `0x80049c20` | `Checklist_GetRewardNum(mode)` | Returns reward count for a mode (AR 46 / TR 33 / CT 44) |
| `0x80049c84` | `Checklist_GetClearKindFromRewardIndex(mode, idx)` | Returns `clear_kind` for a reward_index |
| `0x80049e24` | `ClearChecker_CheckUnlocked(mode, idx)` | Vanilla checks the `has_reward` bit for a reward's clear_kind. **Replaced** by `ChecklistRewards_CheckUnlocked`, which keys solely on the AP `received_checklist_rewards` bitfield (no `has_reward` fallback) |
| `0x80049ec4` | `ClearChecker_GetRewardFromClearKind(mode, ck, &idx, &param)` | Reverse lookup: clear_kind → reward_index + reward_param. Sole caller is the audio/ending preview path in `Checklist_Think` (`0x801804dc`). **Replaced** by `ChecklistRewards_GetRewardFromClearKind` to avoid the clear_kind=0 sentinel aliasing under shuffle |
| `0x80049fcc` | `ClearChecker_SetNewUnlockSilent(mode, ck)` | Sets `is_new` with no SFX; the Top Ride evaluators' funnel |
| `0x8004a054` | `ClearChecker_SetNewUnlock(mode, ck)` | Sets `is_new` + unlock SFX; the main gameplay funnel |
| `0x8004a1a4` | `ClearChecker_CheckForNewUnlocks(mode)` | Scans a mode for `is_new && !is_unlocked` |
| `0x8004a2bc` | `Checklist_InitGridMapping(mode)` | Fills `grid_mapping[120]` — meta cells pre-placed, remainder randomized with `HSD_Randi` |
| `0x80049d10` | Reward type lookup (unnamed) | Returns `stc_reward_table_ptrs[mode][reward_index].reward_type`. Called from the icon display function at `0x80182178` |
| `0x80180508` | Audio preview scan | Inside `Checklist_Think`. Scans `stc_audio_preview_tables[current_mode]` for `reward_index`, calls `BGM_Play`, persists song to `MainMenuData.soundtest_bgm_kind` (`GameData+0x4e`). **Hooked** to redirect to the source mode's table for cross-mode music rewards |
| `0x8017df5c` | `Checklist_SetRewardFlagOnUnlocks()` | Sets `has_reward` on unlocked slots, rebuilds the grid, manages filler counters |
| `0x8017e490` | `Checklist_ProcessUnlock()` | Called from `Checklist_Think` case 1 on checklist entry. Animates one pending unlock and reveals its neighbours, then writes the 5 meta auto-unlock bytes via direct `stb` instructions that bypass `SetNewUnlock`. Returns 1 while an unlock was processed, 0 once none remain (and then requests a card save) |
| `0x8017f3bc` | `Checklist_Think()` | Checklist state machine (filler placement, unlock animations, cursor) |
| `0x80181d70` | `Checklist_UpdateCellInfo()` | Per-frame hover display: looks up the reward for the hovered cell, displays text/icon |
| `0x801822f4` | `Checklist_Init()` | Loads SIS, creates grid cells, calls `SetRewardFlagOnUnlocks` |
| `0x80007af0` | `Checklist_BuildUnlockBitfields()` | Caches unlock status into the `GameData+0xd50` bitfields, via `ClearChecker_CheckUnlocked` |
| `0x8007b650` | `Checklist_IsCacheValid()` | 1 when the unlock bitfield cache is valid; the short-circuit both `SetNewUnlock` variants take |

### Cell visibility: the board expands outward

The grid builder draws a cell only for the bits it finds, in this priority:
`is_filler` → filler tile, `has_reward` → reward tile, `is_unlocked` → checked box,
`is_visible` → empty box, **none of them → no cell at all**. So a checklist starts as
an almost-blank board and grows: `is_visible` is the "you can see this objective
exists" bit, and nothing sets it up front.

`Checklist_ProcessUnlock` is the sole source of it. Per call it finds the first
`is_new && !is_unlocked` cell, overwrites its clear byte with `0x04` (`is_unlocked`
alone), then maps it through `grid_mapping` to a physical slot and ORs `0x10`
(`is_visible`) into the **four orthogonal neighbours** of that slot — left/right gated
on `col != 0` / `col < 11`, up/down on `row != 0` / `row < 9` — each resolved back to a
`clear_kind` by a reverse scan of `grid_mapping`. It then re-runs
`Checklist_SetRewardFlagOnUnlocks` so the newly revealed tiles appear, moves the cursor
onto the unlocked cell, and returns 1. `Checklist_Think` case 1 re-enters it every 45
frames until it returns 0, so a run's whole batch of unlocks animates in sequence.

Neither `ClearChecker_SetNewUnlock` nor the filler path touches `is_visible`; a
completion that is never animated (recorded outside the checklist screen, or restored
from a save into freshly zeroed clear data) therefore reveals nothing around itself.

### Checklist_UpdateCellInfo display flow (`0x80181d70`)

Per-frame function that handles the hover tooltip for the currently selected checkbox.

Register assignments, stable throughout the function:

- `r29` = cell info struct (`+0x0c` text object, `+0x10` previous reward_index,
  `+0x11` previous clear_kind, `+0x12` display state counter)
- `r30` = `ClearCheckerUI` (`+0x14` current mode, `+0x15` phase)
- `r31` = main menu instance
- `r28` = icon display object
- `r26` = clear_kind of hovered cell (after grid_mapping lookup)
- `r27` = reward_index result (or -1 if none)

Flow:

1. **Grid lookup** (`0x80181df8`–`0x80181ed0`): unrolled loop searches `grid_mapping`
   for the visual position matching the cursor. Result: `r26` = clear_kind.
2. **Same-cell check** (`0x80181edc`): compares `r26` with the saved clear_kind at
   `+0x11(r29)`. If same, skips reward lookup and jumps to the display-state logic at
   `0x80181f74`.
3. **Reward lookup** (`0x80181ee4`–`0x80181f5c`): searches the current mode's reward
   table for a `reward_index` whose `clear_kind` matches `r26`, calling
   `ClearChecker_CheckUnlocked` to verify. Result: `r27` = reward_index (if found and
   unlocked) or -1.
4. **Display state change** (`0x80181f74`–`0x80181ffc`): compares the new `r27` with
   the previous (from `+0x10`). If different, triggers either:
   - **No-reward → reward** (previous was -1): icon animation at `0x80181fc8`,
     display state = 0
   - **Reward → different reward**: display state = 5 (immediate text)
   - **Reward → no-reward** (`r27 < 0`): blank text at `0x80181f8c`
5. **Text display** (`0x80182000`–`0x80182030`): when the display state reaches 5,
   calls `Text_InitPremadeText(text, reward_index + 0x7d)` to set up the reward text.
   The state increments each frame (0→1→2→3→4→5→6); text displays on state 5 and stops
   at state 6.
6. **Icon display** (`0x80181fc8`–`0x80181fe4`): called at step 4 when transitioning
   from no-reward to reward. Uses `mode * 2` as the icon index via the function at
   `0x80138b10`.

### SIS text indices

The checklist reads its strings out of the SIS (String Information System) slots. The
system supports 5 simultaneous slots (`stc_sis_archives[5]` at `0x8059a848`,
`stc_sis_data[5]` at `0x8059a85c`); vanilla only uses slot 0.

SIS file per mode: `SisClrChk3D.dat` (Air Ride), `SisClrChk2D.dat` (Top Ride),
`SisClrChkCT.dat` (City Trial).

Text indices within a checklist SIS file:

| Index | Content |
|---|---|
| `clear_kind + 4` | Objective text ("Finish 1st while flying") |
| `reward_index + 0x7d` | Reward text ("Machine: Winged Star") |
| `0x7c` | Blank text (shown when there is no reward to display) |

`Text_InitPremadeText(text, text_index)` (`0x8044f8c8`) reads
`stc_sis_data[text->sis_id][text_index]` and stores a command-data pointer at
`text->command_data` (offset `0x5c`). If `stc_sis_data[sis_id]` is NULL the lookup is
silently skipped. `Text.sis_id` (offset `0x4f`, u8) selects the slot; rendering
(`Text_GX`) instead uses the canvas's `sis_idx` for glyph/font data.

### Checkbox fillers

- Filler grants increment `checkbox_filler_num` (uncapped u8) and
  `checkbox_filler_list_len` (capped at 5). `Checklist_GrantFiller(mode)` is a static
  inline in `game.h`.
- Filler placement is handled by `Checklist_Think` states 5–9; state 8 validates that
  the target slot is empty.
- `checkbox_filler_num` lives in `GameClearData` (the game's native clear data), **not**
  in `APSave`. It is consumed when the player spends a filler, and `is_filler` cells
  (the spend record) have no mod-side reconstruction path — so the mod treats
  `GameClearData` as persisting across boots via the game's native save.

### Auto-unlock objectives

Five "meta" cells are set by vanilla with direct `stb` instructions inside
`Checklist_ProcessUnlock`, bypassing `SetNewUnlock` entirely. At every site
`r30 = gmGetClearcheckerTypeP(mode)` and the store immediate is `0x7c + clear_kind`.

| Mode | clear_kind | Description | Store site |
|------|-----------|-------------|-----------|
| Air Ride | 0x18 (24) | Complete 100 checkboxes | `0x8017efc0` (`stb r4,148(r30)`) |
| Top Ride | 0x77 (119) | Complete 100 checkboxes | `0x8017eff8` (`stb r4,243(r30)`) |
| City Trial | 0x37 (55) | Complete 100 checkboxes | `0x8017f030` (`stb r4,179(r30)`) |
| City Trial | 0x6D (109) | Unlock Dragoon Parts on the Checklist (all 3 parts) | `0x8017f0ac` (`stb r0,233(r30)`) |
| City Trial | 0x6E (110) | Unlock Hydra Parts on the Checklist (all 3 parts) | `0x8017f120` (`stb r0,234(r30)`) |

### Air Ride distance objectives ("Race over N feet in 2 minutes!")

`AirRide_CheckRaceDistanceObjectives` (`0x8004d454`) evaluates all eight of these cells.
`AirRide_CheckRaceFinishObjectives` (`0x8004aa58`) calls it once per Air Ride minor-scene
exit — that function is the Air Ride major's `cb_ExitMinor`, entry 4 of the major
scene-desc table at `0x80495058` — so it is a single snapshot at race end, not a per-frame
poll.

It switches on `Stage_GetGrKindFromStageKind(Gm_GetCurrentStageKind())`, a **GroundKind**
(file order), to pick the per-course threshold and `clear_kind`:

| GroundKind | Course | Feet | clear_kind |
|---|---|---|---|
| 0 `GR_PLANTS1` | Fantasy Meadows | 4,500 | 20 |
| 1 `GR_HEAT2` | Magma Flows | 4,800 | 12 |
| 2 `GR_DESERT1` | Sky Sands | 4,000 | 22 |
| 3 `GR_CHECK2` | Checker Knights | 5,500 | 11 |
| 4 `GR_VALLEY2` | Celestial Valley | 6,000 | 21 |
| 5 `GR_MACHINE2` | Machine Passage | 4,500 | 33 |
| 6 `GR_SPACE2` | **Nebula Belt** | — | — |
| 7 `GR_SKY2` | Beanstalk Park | 5,500 | 34 |
| 8 `GR_ICE1` | Frozen Hillside | 5,300 | 23 |

Nebula Belt's jump-table slot points straight at the loop, leaving the required-frames
local at 0, so the objective can never fire there — the course has no cell of this kind.

"In 2 minutes" is the **configured race time limit**, not a mid-race sample: the gate is
`Gm_GetRaceTimeLimitSeconds() * 60 == 7200`, i.e. `GameData.time_seconds` (`0xa9c`) is
exactly 120. That field holds the limit the rules menu set, which `Game_Think` compares an
elapsed counter against to end the round; the player has to run a 2-minute timed race.

Per slot the loop takes `p = 0..4` where `plGetPlayerKind(p) == 0` (human), reads
`Gm_GetPlayerRaceDistance(p)` (metres), divides by `0.3048` and awards when the result is
**`>=`** the threshold. `ClearChecker_GetKindClear(GMMODE_AIRRIDE, clear_kind) & 5`
(`is_new | is_unlocked`) suppresses a repeat before `ClearChecker_SetNewUnlock`. Because it
reads `GameData` live rather than the latched results block, there is no `xc00[p]` validity
gate.

### Unlock-gated settings menus (City Trial rules)

The City Trial pre-game **settings/rules menu** (minor scene `MNRKIND_CITYSETTINGS`,
minor 5) is a *consumer* of `ClearChecker_CheckUnlocked`, distinct from the checklist
grid. Some rule-option rows (the "Extra Rules" reward category) only appear once
unlocked, so the menu builder gates them on the clear-checker.

| Address | Function | Role |
|---------|----------|------|
| `0x8001eaac` | `CitySettings_BuildMenu` | Build/enter handler for the settings menu (registered in the menu's `{build, update, destroy}` handler table at `0x807e0508`). Clears menu state, calls `CitySettings_BuildCellList`, then renders each cell via `CitySettings_RenderCellValue`. |
| `0x8001da14` | `CitySettings_BuildCellList` | Builds the list of visible setting rows. For the unlock-gated control-kinds it includes a row only if at least one of its candidate `clear_kind`s passes `ClearChecker_CheckUnlocked(mode=2, …)`; ungated kinds are copied verbatim. |
| `0x8001ddf8` | `CitySettings_RenderCellValue` | Draws (and normalizes) one row's value widget, reading/writing the packed setting bitfields at `GameData+0xb4`/`+0xb5`. |
| `0x8001ed14` | `CitySettings_UpdateCellHighlight` | Re-applies a cell's highlight / at-limit color after its value changes. |

Because `ClearChecker_CheckUnlocked` is `REPLACEFUNC`'d to
`ChecklistRewards_CheckUnlocked` (keyed on AP `received_checklist_rewards`), these rule
options unlock on AP delivery like every other non-gated reward category. The Top Ride
rules-menu builder is the analogous consumer on that side.

## AP Rewards (Inbound)

**Files:** `checklist_rewards.c` / `checklist_rewards.h`.

### AP ↔ game reward-index translation

The apworld numbers each mode's rewards in **clear_kind-sorted order** (the order the
checkboxes appear — exactly what `archipelago_api.h`'s `AP_REWARD_*` enum and
`docs/checklist-mappings.csv` use). The game's internal reward table
(`stc_reward_table_ptrs`) is in a different, ROM-defined order, and the entire mod
machinery — `shuffled_rewards`, `received_checklist_rewards`, the parallel
audio-preview / stadium lookup tables, the debug `GrantReward`/`GetShuffledReward`
API — is keyed on that **internal game index**. Reordering the table in place is unsafe
(it would desync the parallel hardcoded tables), so the mod keeps game-table order
internally and translates at the two AP-client wire boundaries only.

- `BuildRewardIndexMaps()` (called from `AllocateRewardTables`, while the copied tables
  still hold native clear_kinds) builds `ap_to_game_ri[mode][ap_ri]` by ranking each
  reward's native clear_kind ascending. `ChecklistRewards_ApToGameIndex(mode, ap_ri)`
  exposes it; out-of-range inputs pass through unchanged. It reproduces the apworld's
  numbering exactly for all three modes (AR 46 / TR 33 / CT 44 entries).
- **Boundary 1 — incoming item ID** (`ap_item_handler.c`): `reward_index =
  (id - AP_CHECKLIST_REWARD_BASE) % 50` is the apworld's clear_kind-sorted index;
  translate via `ApToGameIndex` before `ChecklistRewards_Grant`.
- **Boundary 2 — `locations[]`** (`ChecklistRewards_ApplyLocations`): the client writes
  `locations[m][ap_ri]`; store into `shuffled_rewards[m][ap_to_game_ri[m][ap_ri]]`. The
  bijection covers `[0, count)` so each game index is written once.
- Everything inside those boundaries (Grant, the debug API, `ResolveCell`,
  `GetShuffledReward`) stays in **game-index** space and needs no translation. Without
  this, a received reward would decode to the wrong game-table row (e.g. the client's
  "TR Filler 5" → game index 25 = Ending), and `locations[]` placement would be
  scrambled.

**AP item IDs:** `AP_CHECKLIST_REWARD_BASE (500) + mode*50 + ap_reward_index`.

### Reward table management

- `AllocateRewardTables()` (OnBoot): allocates new `RewardEntry` tables via
  `HSD_MemAlloc`, copies the originals, and redirects `stc_reward_table_ptrs[mode]`.
  All game queries then go through the mod's copies.
- For same-mode rewards, `clear_kind` in the table is set to the assigned checkbox. For
  cross-mode-out rewards and remote rewards, `clear_kind` is set to 0 (safe sentinel —
  any value `>= 120` trips the vanilla OOB assert at `0x8004a08c`; 0 is the smallest
  valid index). The sentinel is never *semantically* read: every vanilla-facing code
  path that would index `clear[]` by `RewardEntry.clear_kind` is gated to run only for
  same-mode local placements.
- **Same-mode placement predicate:** `IsSameModeLocalPlacement(mode, ri)` =
  `shuffled_rewards[mode][ri] != 0xFFFF` AND `(shuffled_rewards[mode][ri] >> 8) == mode`.
  This is the authoritative "this reward is actually placed in THIS mode's checklist"
  check. Cross-mode source rows fail because their saved encoding has
  `target_mode != source_mode`; remote rewards fail because their encoding is `0xFFFF`.

### Function replacements

**`ClearChecker_CheckUnlocked`** (CODEPATCH_REPLACEFUNC at `0x80049e24`):
`ChecklistRewards_CheckUnlocked(mode, reward_index)` returns true **iff** the reward's
bit is set in AP `received_checklist_rewards` — i.e. AP delivery is the sole authority
for a checklist reward being unlocked. There is intentionally **no `has_reward`
fallback**: `has_reward` is also raised by in-game checkbox completion (vanilla
`SetRewardFlagOnUnlocks`), so a fallback would unlock the reward the moment the player
earns the box, *before* the AP server delivers the item. Gated categories
(machines/colors/stadiums/stages/TR items/…) don't notice — their vanilla availability
checks are `REPLACEFUNC`'d to read mod gate masks and never call this function. But the
non-gated "cosmetic" categories (Sound Test, Music, Endings, Bonus Movie, Pause
Power-ups, Extra Rules) read this function live (via `MainMenu_SoundTestThink`,
`setLevelMusic`, `AirRide_CheckBonusUnlocked`, `MainMenu_OptionsThink`,
`Pause_CheckStatsUnlocked`, and the TR rules-menu builder), so a `has_reward` fallback
would let *those* unlock pre-delivery for same-mode-local placements. Keying purely on
the received bitfield closes that leak. The reward icon still appears on in-game
completion via `ChecklistRewards_FindRewardForCell`, which is independent of this
function.

**`ClearChecker_GetRewardFromClearKind`** (CODEPATCH_REPLACEFUNC at `0x80049ec4`):
`ChecklistRewards_GetRewardFromClearKind(mode, clear_kind, &out_reward_index,
&out_reward_param)`. Vanilla's sole caller is the audio/ending preview path at
`0x801804dc` inside `Checklist_Think`. A vanilla scan of `RewardEntry.clear_kind` would
alias against the cross-mode/remote `clear_kind=0` sentinel and return the wrong row
(e.g. play the wrong music when hovering the legitimate same-mode placement at
`clear_kind=0`). The replacement resolves via `ChecklistRewards_ResolveCell(mode,
clear_kind)` (cross_mode_slots → save_shuffled u16 scan) and returns the **source**
mode's reward_index plus that row's `reward_param`, honoring vanilla's early-exit (it
only resolves when `is_unlocked || is_filler` on the target cell) and writing
`0xFF` otherwise.

### Grant path

`ChecklistRewards_Grant(mode, reward_index, announce)`: sets the AP bitfield, optionally
announces, routes vanilla reward types into the mod's gate masks, then decodes the
placement cell directly from `shuffled_rewards[mode][reward_index]` (high byte = target
mode, low byte = target clear_kind, `0xFFFF` = remote) and writes `has_reward` to the
correct mode's `GameClearData.clear[]`. It **does not set `is_unlocked`** — that bit is
reserved as the source of truth for "the player completed this checkbox in gameplay" and
is owned by `check_detection.c`. The reward icon still appears because it is driven by
`has_reward`. It also **does not write** the `GameData+0xd50` unlock cache:
`Checklist_BuildUnlockBitfields` rebuilds that from the replaced
`ClearChecker_CheckUnlocked`, so it picks up the new bit on its own.

`ApplyVanillaRewardUnlock(mode, reward_index, reward_type)` is invoked from Grant so the
gate-mask bit flips regardless of whether the reward arrived as an AP item or was earned
by completing the originally-intended checkbox. Without this, vanilla rewards would write
only to the dead in-game cache that our gate hooks bypass. Routing:

- `REWARD_MACHINE_*` (13 values) → `GateMachines_UnlockMachine(VCKIND_*)`.
- `REWARD_KING_DEDEDE` → `VCKIND_WHEELDEDEDE` (the player-facing Dedede;
  `VCKIND_WHEELVSDEDEDE` is the stadium CPU-only machine and has no AP unlock).
  `REWARD_META_KNIGHT` → `VCKIND_WINGMETAKNIGHT`. (Character→machine resolution is via
  `CharacterDesc_GetMachineKind` inside the AR-character availability gate.)
- `REWARD_DRAGOON` / `REWARD_HYDRA` → `VCKIND_DRAGOON` / `VCKIND_HYDRA`.
- `REWARD_COLOR_*` (4 values) → `GateColors_UnlockColor(KIRBYCOLOR_*)`.
- `REWARD_ITEM_CHICKIE` / `WHO_PAINT` / `LANTERN` →
  `GateTopRideItems_UnlockItem(TRITEM_*)`. Vanilla reads the checklist `has_reward` for
  these reward indices (8/9/10) in `TopRide_OnCourseSelect` to drive
  `ItemMgr.enabled_mask` bits 20/18/15.
- `REWARD_COURSE` → `GateAirRideStages_UnlockStage(AIRRIDE_NEBULA_BELT)` (only one
  course reward exists).
- `REWARD_STADIUM` (CT) → `GateStadiums_UnlockStadium(...)` via
  `CtRewardIndexToStadium(reward_index)`. Vanilla `Checklist_ProcessUnlock` hardcodes the
  index→stadium mapping (`reward_param` is 0 for every stadium row), so the mod
  re-derives it: `37→DRAG4, 38→MELEE2, 39→DESTRUCTION3, 40→DESTRUCTION4,
  41→DESTRUCTION5, 42→SINGLERACE9 (Nebula Belt)`.
- `REWARD_DRAGOON_PART_*` / `REWARD_HYDRA_PART_*` → **not** routed to a gate. These are
  checklist-internal markers for the "all parts collected" meta-checkbox (CT clear_kind
  0x6D Dragoon / 0x6E Hydra), distinct from the in-round legendary-piece spawn gates
  (`ITUNLOCK_DRAGOON1-3` / `ITUNLOCK_HYDRA1-3`).
- `REWARD_FILLER` / `BONUS_MOVIE` / `EXTRA_RULE` / `SOUND_TEST` / `MUSIC` / `ENDING` /
  `PAUSE_POWERUPS` → not gated, left to vanilla.

### Reward announcements (textbox)

- `AnnounceChecklistReward` is the single announce site — the gate handlers
  `ApplyVanillaRewardUnlock` invokes are passed `announce=0`, so each reward is named
  exactly once here however it arrives.
- `stc_checklist_reward_names[mode][reward_index]` is the per-reward display-name table
  (joined from the vanilla reward tables; filler rows are `NULL` and handled before the
  lookup).
- `ChecklistRewardStyle(reward_type)` picks the prefix + noun color. Gate-unlock
  categories (Stadium / Course / Machine / Character / Color / TR Item) read
  `Unlocked <category>: <name>` and reuse the noun color their direct-unlock gate handler
  prints, so a reward looks identical whichever way it arrives. Everything else reads
  `Received…`: non-gated extras keep a category (`Received Sound Test: <name>`,
  `Received Music:`, `Received Extra Rule:`); single-instance features (Bonus Movie,
  Ending, Pause Power-ups) and the legendary parts carry none (`Received: <name>`).
- Fillers use `Checklist_AnnounceFiller(mode)`: `Received: Checkbox Filler (<Mode>)`,
  shared with the direct AP filler-item path so wording and coloring stay identical. The
  AP tab's runtime mode has no `ModeColors[]` slot, so it supplies its own name and tint.

### Checkbox filler grants (AP receipt is the sole authority)

A `REWARD_FILLER` arriving from AP grants exactly **one** usable filler token, in
`ChecklistRewards_Grant`, to the **reward's own mode** (matching the `(<Mode>)` the
textbox names) — independent of where the reward is placed (`shuffled_rewards`).
Placement only drives the `has_reward` display badge.

- The grant fires only on a **real receipt** (`announce=1`): the AP item handler and the
  debug `GrantReward` API. The replay path (`RegrantAllReceivedRewards`, `announce=0`,
  run on every save-load and after `ApplyLocations`) must not re-grant, or
  `checkbox_filler_num` would inflate every boot.
- Both cell-COMPLETION grant paths are disabled so this is the single grant site:
  vanilla's reward-loop grant (REPLACEINSTRUCTION at `0x8017e00c`) and
  `ApplyCrossModeHasReward`. They would key off cell completion, not AP receipt; and
  because `Grant` writes `has_reward` on receipt, it pre-empts vanilla's "grant on
  `has_reward` 0→1 transition" anyway.
- Direct filler items (`AP_ITEM_CHECKBOX_FILLER_*`, used by the EnergyLink filler-buy and
  the debug filler-give) bypass `Grant` entirely — they call `Checklist_GrantFiller`
  directly in `ap_item_handler.c` — and are unaffected.

### Cosmetic ungating (`checklist_rewards_gating_enabled` off)

- When the slot option is off, the AP world removes the non-progression reward categories
  from the item pool and frees their checkboxes to hold ordinary items. The mod
  compensates at connect via `ChecklistRewards_GrantAllCosmetic()`, which sets the
  `received_checklist_rewards` bit for every "cosmetic" reward across all three modes —
  the complete unlock for these types (`ChecklistRewards_CheckUnlocked` reads the bit;
  `ApplyVanillaRewardUnlock`'s `default` is a no-op for them).
- `IsCosmeticRewardType(reward_type)` defines the set: `REWARD_FILLER`, `BONUS_MOVIE`,
  `EXTRA_RULE`, `SOUND_TEST`, `MUSIC`, `ENDING`, `PAUSE_POWERUPS` — exactly the reward
  types with **no gate mask of their own**.
- Deliberately excluded: the gated categories (`REWARD_STADIUM`/`COURSE`/`MACHINE_*`/
  characters/colors/TR items), governed by their own `*_gating_enabled` flags (granting
  them here would badge them "received" without flipping their gate mask); and
  `REWARD_DRAGOON_PART_*`/`HYDRA_PART_*`, which are progression (they assemble the
  legendary machines) and must stay in the AP pool.
- `GrantAllCosmetic` does **not** call `ChecklistRewards_Grant`: no per-reward textbox,
  and no `Checklist_GrantFiller` bump for `REWARD_FILLER` (an ungated reward yields no
  extra remote filler token).

### Reward-loop and unlock hooks

**Reward loop filter** (HOOKCONDITIONALCREATE at `0x8017dfd8`):
`ChecklistRewards_ShouldSkipReward(mode, reward_index)` skips every reward that fails
`IsSameModeLocalPlacement` — both remote rewards and cross-mode source rows. Vanilla's
reward loop at `0x8017df5c` then only ever touches `clear[mode][ck]` for same-mode
placements, so it cannot spuriously set `has_reward` on `clear[m][0]` via the sentinel.
Cross-mode placements get their `has_reward` from the post-loop hook.

**Post-reward-loop hook** (HOOKCREATE at `0x8017e07c`):
`ChecklistRewards_ApplyCrossModeHasReward(current_mode)` iterates
`cross_mode_slots[row]` and on any checkbox where `(is_unlocked || is_filler) &&
!has_reward` sets `has_reward = 1` for the display badge. It resolves its row with
`ChecklistModeRow` and returns early on `-1`, so the AP tab is covered and any other
custom tab is skipped. It does **not** grant a checkbox filler when the source is a
`REWARD_FILLER` — that would double-count and credit the wrong mode. Clobbered
instruction: `lbz r0, 0(r31)` (re-executed in the trampoline epilogue).

**Vanilla reward-loop filler grant — neutralized** (REPLACEINSTRUCTION at `0x8017e00c`):
the vanilla reward loop bumps `checkbox_filler_num`/`checkbox_filler_list_len` for reward
indices in `stc_special_rewards[mode]` (the hardcoded `{0,1,2,3,4}` filler rows). Its
first instruction (`li r0,5`) is replaced with `b +0x58` (→ `0x8017e064`, the loop
increment), skipping the entire grant block while leaving the preceding `has_reward` store
(`0x8017e000`–`0x8017e008`) intact.

**Legendary-part assembly — placement-independent** (two HOOKCONDITIONALCREATE in
`Checklist_ProcessUnlock`, City Trial branch): vanilla decides "all 3 Dragoon parts
collected" → mark cell `0x6D` (at `0x8017f044`–`0x8017f094`) and "all 3 Hydra parts
collected" → mark cell `0x6E` (at `0x8017f0b4`–`0x8017f108`) by ANDing the `has_reward`
bit of the three part reward cells, resolved via `Checklist_GetClearKindFromRewardIndex`
(CT reward indices 27/28/29 Dragoon, 31/32/33 Hydra). Under shuffle a cross-mode/remote
part resolves to the `clear_kind=0` sentinel, so vanilla reads `clear[0]` instead — a
false NEGATIVE (assembly never fires, the 0x6D/0x6E check is never sent), or a false
POSITIVE if another reward sits at CT `clear_kind=0`.
`Legendary_DragoonPartsReceived` / `Legendary_HydraPartsReceived` (hooks at `0x8017f044`
/ `0x8017f0b4`) replace the `has_reward`-AND condition with
`(received_checklist_rewards[CITYTRIAL] & {3 part bits}) == {3 part bits}` —
placement-independent, matching how `ClearChecker_CheckUnlocked` treats every other
reward. On "all received" they fall into vanilla's own set-cell logic, which still runs
the `0x8017f0ac`/`0x8017f120` meta-unlock store hooks that send the 0x6D/0x6E check. The
block is City-Trial-only (mode guard `cmplwi r3,2` at `0x8017f00c`).

**Audio preview hook** (HOOKCONDITIONALCREATE at `0x80180508`):
`ChecklistRewards_AudioPreview(reward_index)` replaces the vanilla per-entry scan that
walks `stc_audio_preview_tables[current_mode]`. It reads `hover.source_mode` (set by the
FindRewardForCell hook in `Checklist_UpdateCellInfo` on the prior frame for the hovered
cell; `0xFF` until one resolves), looks up `reward_index` in the **source** mode's audio
table, calls `BGM_Play(song_id)`, and persists the song_id to
`MainMenuData.soundtest_bgm_kind` (`GameData+0x4e`). Reached only when `reward_param ==
REWARDPARAM_AUDIO`. It always alt-exits to `0x80180560`, past the vanilla scan +
`BGM_Play` + persist sequence. For same-mode placements `hover.source_mode ==
current_mode`, so behavior matches vanilla; when `hover.source_mode` is not one of the
three real modes (nothing resolved yet, or a custom tab) the hook alt-exits without
playing anything. Cross-mode ending previews (`REWARDPARAM_ENDING`) are safe to route
through the unhooked vanilla path at `0x80180554` — vanilla's "ending preview" only sets
a UI state byte and plays a menu click; no actual ending movie plays.

### Cross-mode protocol

**u16 location encoding:**

- `(target_row << 8) | clear_kind` — local reward at that checklist row's board
- `0xFFFF` — remote reward (no local slot)
- Same-mode example: `(0 << 8) | 42 = 0x002A` — Air Ride reward at Air Ride checkbox 42
- Cross-mode example: `(2 << 8) | 10 = 0x020A` — Air Ride reward at City Trial checkbox 10

The high byte is a checklist-mode **row**, not a runtime mode, so `3` targets the AP tab.
`RebuildRewardTablesFromShuffle` bounds-checks it against `CHECKLIST_MODE_NUM` rather
than mapping it, and treats a malformed value as remote.

**CrossModeSlot mapping:**

- `cross_mode_slots[CHECKLIST_MODE_NUM][120]` maps (target row, clear_kind) → (source
  mode, source reward_index). `source_mode == 0xFF` = empty. Row `AP_CHECKLIST_ROW` is the
  AP checklist tab: it awards no native rewards, so every reward it hosts is a cross-mode
  placement and lives only here.
- Populated by `ChecklistRewards_ApplyLocations()` (from AP client data) and rebuilt by
  `RebuildRewardTablesFromShuffle()` (from the persisted u16 arrays in `APSave`).
- Used by `ResolveCell` / `FindRewardForCell` for forward (cell → source reward) lookups
  on the UI hover path. Reverse lookups (reward → target cell) decode directly from
  `shuffled_rewards[source_mode][reward_index]`, which already encodes
  `(target_row << 8) | target_clear_kind`.

**Save data (`APSave`, accessed via the global `ap_save`; defined in
`mods/archipelago/src/main.h`):**

- `u16 shuffled_rewards[GMMODE_NUM][REWARD_COUNT_MAX]` — persisted u16 location encoding
  per reward_index. `0xFFFF` = remote. Sized by game mode, since only real modes own
  reward tables.
- `u64 received_checklist_rewards[3]` — bit N = reward_index N received for that mode.

### Cross-mode display

**Multi-SIS loading** (HOOKCREATE at `0x801823c4`): NOPs the 3 original per-mode
`Text_LoadSisFile` calls (`0x80182378`, `0x8018238c`, `0x801823a0`).
`LoadAllChecklistSIS(current_mode)` loads the current mode's SIS into slot 0 (vanilla code
works unchanged) and the other two modes into slots 1 and 2.
`mode_to_sis_slot[GMMODE_NUM]` maps GameMode → slot index.

**Reward reverse lookup replacement** (HOOKCREATE at `0x80181ee4`):
`ChecklistRewards_FindRewardForCell(current_mode, clear_kind)` replaces vanilla's
reward-table scan entirely — it always alt-exits to `0x80181f5c`, so vanilla's scan never
runs. This is required because a raw scan of `RewardEntry.clear_kind` would alias
cross-mode source rows (sentinel 0) against a real same-mode placement at `clear_kind=0`.

- Resolves via `ChecklistRewards_ResolveCell` (cross_mode_slots first, then a full-u16
  match against `shuffled_rewards[current row]`). Comparing against the full u16 avoids
  the sentinel aliasing.
- An AP `received_checklist_rewards` bit returns the reward_index unconditionally.
  Otherwise: same-mode cells require `has_reward` set; cross-mode cells require
  `is_unlocked || has_reward` (the `is_unlocked` term covers the
  newly-completed-this-session window before the post-loop hook has mirrored has_reward).
- Snapshots `source_mode` into the static `hover` struct; downstream text/icon/audio hooks
  read it to pick the correct SIS slot and reward table. It is the one piece that cannot
  be recomputed on demand, since it comes from a placement resolve rather than the UI. The
  hovered cell itself is *not* snapshotted — `ChecklistRewards_GetHoveredCell` reads
  `ClearCheckerUI.cursor_col`/`cursor_row` live and reverse-maps through `grid_mapping`.
- Returns `reward_index + 1` for a visible reward, `-1` for empty/locked. The epilogue
  decrements r3 by 1 (or sets -1 on negative) and stores to r0 so vanilla's post-alt-exit
  `mr r27, r0` lands the right value.

**Reward text display** (HOOKCREATE at `0x8018201c`):
`ChecklistRewards_DisplayRewardText(text, reward_index, current_mode)` temporarily sets
`text->sis_id` to `mode_to_sis_slot[hover.source_mode]`, calls
`Text_InitPremadeText(text, reward_index + 0x7d)`, then restores `sis_id` to 0. Command
data comes from the source mode's SIS; glyph rendering uses slot 0's font (all checklist
SIS files share the same font).

**Blank text fix** (HOOKCREATE at `0x80181f8c`):
`ChecklistRewards_SetBlankTextSisId(text, current_mode)` resets `sis_id` to 0 before
displaying blank text (`0x7c`), in case a previous cross-mode hover left it changed.

**Reward type icon** (HOOKCREATE at `0x80182170`): the reward-type icon (machine, music,
color, …) is displayed by a separate function at `0x801820b4`. It calls the reward-type
lookup at `0x80049d10(mode, reward_index)` from `0x80182178` to read
`stc_reward_table_ptrs[mode][reward_index].reward_type`. For a cross-mode reward the mode
must be the source mode, so the hook at `0x80182170` replaces the mode in r3 with
`hover.source_mode` (returned by `ChecklistRewards_GetHoverSourceMode()`), then skips the
vanilla mode load at `0x80182174`, exiting to `0x80182178`.

**Cross-mode objective text:** the objective text shown when hovering still comes from the
target mode's SIS (it uses `clear_kind + 4` with `sis_id = 0`). That is correct — the
objective belongs to the target mode's checklist.

### Lifecycle (rewards)

```
OnBoot:
  AllocateRewardTables()          - allocate mod reward tables, redirect pointers,
                                    BuildRewardIndexMaps()
  REPLACEFUNC ClearChecker_CheckUnlocked            -> ChecklistRewards_CheckUnlocked
  REPLACEFUNC ClearChecker_GetRewardFromClearKind   -> ChecklistRewards_GetRewardFromClearKind
  HOOKAPPLY 0x8017dfd8            - skip remote/cross-mode rewards in SetRewardFlagOnUnlocks
  HOOKAPPLY 0x8017e07c            - post-loop: apply cross-mode has_reward (badge only)
  REPLACEINSTRUCTION 0x8017e00c   - neutralize vanilla reward-loop filler grant (b 0x8017e064)
  HOOKAPPLY 0x8017f044            - legendary Dragoon-parts assembly: received-based (cell 0x6D)
  HOOKAPPLY 0x8017f0b4            - legendary Hydra-parts assembly: received-based (cell 0x6E)
  HOOKAPPLY 0x80180508            - cross-mode audio preview (source mode's audio table)
  NOP 0x80182378/8c/a0            - disable vanilla per-mode SIS loading
  HOOKAPPLY 0x801823c4            - multi-SIS loading
  HOOKAPPLY 0x80181ee4            - cross-mode reward lookup (also snapshots hover state)
  HOOKAPPLY 0x8018201c            - cross-mode reward text display
  HOOKAPPLY 0x80181f8c            - blank text sis_id fix
  HOOKAPPLY 0x80182170            - cross-mode reward type icon
  ClearCrossModeSlots()

OnSaveInit (fresh save creation):
  main.c OnSaveInit() does memset(ap_save, 0), then calls
  ChecklistRewards_OnSaveInit(), which fills ap_save->shuffled_rewards[*][*]
  with 0xFFFF (remote sentinel) - required because zero would alias a valid
  (row=AR, clear_kind=0) placement.

OnSaveLoaded:
  RebuildRewardTablesFromShuffle()  - rebuild stc_reward_table_ptrs[m][i].clear_kind
                                      + cross_mode_slots from saved shuffled_rewards
  RegrantAllReceivedRewards()       - replays Grant(announce=0) for every bit in
                                      received_checklist_rewards (restores has_reward
                                      and gate-mask routing)

ApplyLocations (from AP client, when ap_data->location_data_valid is set):
  Copy ap_data->locations[m][ap_ri] into ap_save->shuffled_rewards[m][game_ri]
  RebuildRewardTablesFromShuffle()
  RegrantAllReceivedRewards()       - re-applies grants for rewards already received
                                      before the assignment arrived
  ap_data->location_data_valid = 0
  Hoshi_WriteSave()
```

## Check Detection (Outbound)

**Files:** `check_detection.c` / `check_detection.h`.

The mod is the source of truth for which checkboxes the player has completed. The Python
AP client reads `ap_data->sent_checks[CHECKLIST_MODE_NUM][2]` (a per-row bitmask, packed
as 2× `u64` to cover the 0..119 clear_kind range) and forwards new bits as AP location
checks. The bitmask is also persisted to `APSave` as
`ap_save->sent_checks[CHECKLIST_MODE_NUM][2]` so completions survive reboots. Row 3
(`AP_CHECKLIST_ROW`) is the AP checklist tab.

### How completions are detected

**Primary path: REPLACEFUNC on `ClearChecker_SetNewUnlock` (`0x8004a054`).** This is the
central funnel most in-game gameplay code uses to flag completed objectives — ~126 call
sites covering Air Ride / City Trial completion paths plus stadium results and free-run
trackers. It accepts the AP checklist tab's runtime mode as well, since the AP tab's
`record_complete` drives completions through it.

`CheckDetection_SetNewUnlockReplacement(mode, clear_kind)`:

1. Reads the current `clear[mode][clear_kind]` byte. If neither `is_new` nor
   `is_unlocked` is already set, this is a true transition — call
   `RecordCheck(mode, clear_kind)`. **Transition detection runs regardless of the vanilla
   cache-valid short-circuit** so AP never misses a check.
2. `RecordCheck()` resolves the row via `ChecklistModeRow` (bailing on `-1`, and on the AP
   row rejecting `clear_kind >= APCK_NUM` so a spent filler cannot send a location code
   the multiworld has never heard of), sets the bit in `ap_save->sent_checks` and the
   shared `ap_data->sent_checks` mirror, logs the placement (resolving the cell via
   `ChecklistRewards_ResolveCell` to print `[Check] mode=… clear_kind=… type=… recorded`
   with the source reward type — or a "no local reward placement" line for remote/empty
   cells), enqueues the "Check sent" textbox, and calls `EvaluateGoal()`. It does **not**
   write the memory card.
3. Reimplements the vanilla SetNewUnlock logic: bail if the cache is valid, OOB clamp,
   play the unlock SFX (`SFX_PlayFullVolume(0x10008)`) guarded by the one-frame cooldown
   at `*stc_clearchecker_sfx_last_frame`, then set the `is_new` bit.

**Companion path: REPLACEFUNC on `ClearChecker_SetNewUnlockSilent` (`0x80049fcc`).** The
Top Ride checklist evaluator (the cluster of functions at `0x802b7xxx`) does **not** use
`SetNewUnlock` — it commits every gameplay objective through this "silent" variant (~51
call sites, all Top Ride). Each call site plays its own unlock SFX and prints
`ClearChecker(<clear_kind+1>)` before calling, and `SetNewUnlockSilent` just sets the
`is_new` bit. Without replacing it, **every Top Ride gameplay check is silently dropped**
— never added to `sent_checks`, never sent to the server, and TR `GOAL_CHECKLIST_LIST`
goals (e.g. "Cross the goal 20 or more times!" = TR clear_kind 0, "Compete in more than 10
multiplayer races!" = TR clear_kind 2) could never complete.
`CheckDetection_SetNewUnlockSilentReplacement` mirrors the SetNewUnlock replacement
(transition detect → `RecordCheck` → run the vanilla silent body) but omits the SFX block,
since the caller already played it. It accepts the three real modes only.

**Companion path: filler-apply hook (`CODEPATCH_HOOKCREATE` at `0x80180dc4`).** When the
player *spends* a checkbox filler, `Checklist_Think` sets `clear[k].is_filler` directly via
`ori r0,r0,2; stb r0,124(r3)` at `0x80180dbc` and **does not** call
`ClearChecker_SetNewUnlock` — so neither funnel replacement sees it, and the spent cell
would never be recorded. `CheckDetection_OnFillerApplied(mode, clear_kind)` hooks the
following instruction (`lbz r3,2(r29)`, the start of the `checkbox_filler_num` decrement),
reads `mode` from `r31+0x14` and `clear_kind` from the non-volatile `r18`, and calls
`RecordCheck` (idempotent, so repeated firings are harmless). A *separate* filler-related
`SetNewUnlock` call does exist at `0x8017fae4`, but that is not the spend path this hook
covers.

### Meta auto-unlock hooks

**5 `CODEPATCH_HOOKCONDITIONALCREATE` hooks inside `Checklist_ProcessUnlock`
(`0x8017e490`).** The five meta auto-unlock checkboxes listed under "Auto-unlock
objectives" do NOT go through `SetNewUnlock` — vanilla sets their `clear[]` byte via direct
`stb` instructions, which `Checklist_Think` case 1 executes when the checklist is entered.
The mod hooks each store site directly so detection fires synchronously at the moment
vanilla commits the unlock, and so the `stb` can be conditionally suppressed when the cell
has already been filler-completed.

Each hook has an empty prologue, calls a thin handler that invokes
`RecordCheck(mode, clear_kind)` with the hardcoded pair, then re-materializes the stored
value (`li r4,1` or `li r0,1`) in the epilogue so the trampoline's auto-re-execute of the
clobbered `stb` still lands a 1 after `bl` clobbered the volatile register. `RecordCheck`
is idempotent, so replays on subsequent `Checklist_ProcessUnlock` invocations are harmless.

**The handler return value controls whether vanilla's `stb` runs:**

- **Return 0 (accept)** — the usual path. The clobbered `stb` auto-re-executes (with the
  epilogue's restored register value), writing `0x01` to `clear[k]`, and control continues
  to the vanilla display_state update sequence and then the function tail.
- **Return 1 (skip)** — taken iff `clear[k].is_filler` is already set at hook entry. The
  handler sets `clear[k].is_unlocked` itself, then control branches directly to
  `0x8017f394` (the function tail), bypassing both the `stb` and the subsequent
  display_state update. This preserves the filler byte: the vanilla store would overwrite
  `is_filler`, `has_reward` and `is_visible` with a bare `0x01`, and `is_filler` in
  particular has no other code path to restore it. The filler code path already drove the
  cell to a completed state through `SetNewUnlock`, so the display_state increment is also
  redundant. The result: a cell that was filler'd before its auto-unlock condition became
  true retains its filler visual marker instead of flipping to an "auto-unlocked"
  appearance (`is_filler` outranks `is_unlocked` in the grid builder's cell-model
  priority).

**Why the skip must set `is_unlocked`.** Each of the five store sites is guarded by
`!clear[k].is_unlocked`, and the branch to `0x8017f394` returns 1 — "an unlock was
processed" — *without* arming the phase timer at `ClearCheckerUI+0x1c`. `Checklist_Think`
therefore calls `Checklist_ProcessUnlock` again on the very next frame (both call sites,
`0x8017f4c8` and `0x801815b4`, only advance the phase when it returns 0). In vanilla the
`stb` itself falsifies the guard on the following pass, so the loop terminates. A skip that
changed nothing would leave the guard true forever: `ProcessUnlock` returns 1 every frame,
the checklist phase never advances, and the screen accepts no input — a softlock reachable
by spending a checkbox filler on any of the five meta cells (vanilla's hardcoded filler
rejects, which the AP filler gate replaces, existed to prevent exactly that). Setting
`is_unlocked` in the handler falsifies the guard the same way the vanilla store would, and
the sequence completes after one extra frame.

### Filler gate (AP goal protection)

Vanilla hardcodes filler rejects for 3 physical grid slots via immediate compares in
`Checklist_Think` at `0x80180a74`–`0x80180a98`: slot 0 and slot 11 rejected only when
`mode == 2` (City Trial), slot 119 rejected in every mode. Under the fixed vanilla
`grid_mapping[]` those 3 slots cover the 5 meta auto-unlock cells (AR/TR/CT 100-checklist,
CT Dragoon, CT Hydra); vanilla blocks them so the player can't cheese an auto-unlock by
spending a filler.

Under reward shuffle that reasoning is wrong — any of those cells may hold a legitimate
shuffled reward, and the player must be free to filler it. The mod installs a single
`HOOKCONDITIONALCREATE` at `0x80180a64` (the vanilla `lbz r3, 20(r31)` mode-load) that:

- Replays vanilla's phys_slot computation (`col + row*12`, from `ClearCheckerUI.cursor_col`
  at `+0x17` and `cursor_row` at `+0x18`) in the prologue, stashing the result in r18
  (non-volatile) where downstream code at `0x80180aa4` expects it.
- Calls `FillerGate_IsRejected(mode, phys_slot)` — returns 1 to reject, 0 to accept.
- On accept: the clobbered `lbz r3, 20(r31)` auto-re-executes (restoring r3 = mode for the
  downstream `bl gmGetClearcheckerTypeP` at `0x80180a9c`), then branches past all three
  vanilla immediate rejects.
- On reject: branches to `0x80180c24`, vanilla's sole `playSoundFX_errorNoise` call site.

`FillerGate_IsRejected` maps `mode` to a row (any custom tab other than the AP one returns
0 immediately), reads `ap_save->options.goal[row]` and protects only goal cells, translated
from clear_kind to physical slot via `cd->grid_mapping[]`:

| `GoalKind` | Protected cells |
|------------|-----------------|
| `GOAL_100_CHECKLIST` | that row's "Fill in over 100 Checklist blocks!" cell, from `Fill100ClearKind(row)` (AR `0x18`, TR `0x77`, CT `0x37`) — a filler there would satisfy the goal without filling 100 boxes. Nothing to protect on the AP row, which has no such cell (`Fill100ClearKind` returns `0xFF`) |
| `GOAL_HYDRA_AND_DRAGOON` | (CT only) CT clear_kind `0x77` ("In one match, complete both Dragoon and Hydra!"). On non-CT rows the gate returns 0 |
| `GOAL_BEAT_KING_DEDEDE`  | (CT only) CT clear_kind `KD_CLEAR_KIND` (`0x2F`). On non-CT rows the gate returns 0 |
| `GOAL_CHECKLIST_LIST` | every clear_kind whose bit is set in `options.goal_checks[row]` (iterated via `__builtin_ctzll` over both u64 words). Per-row — protects exactly the cells the AP slot listed as required |
| `GOAL_N_CHECKLIST` | none — a count threshold, and filler'ing any cell still costs a filler token |
| `GOAL_MAX_STATS_CT` | none — the goal is a runtime save bit independent of any specific cell |
| `GOAL_NONE` | none |

Implemented in `check_detection.c` alongside the goal-evaluation logic.

### Backfill (client → mod)

The client can write to `ap_data->client_backfill[CHECKLIST_MODE_NUM][2]` to back-fill
checks the AP server already knows about (e.g. a fresh save or a slot takeover).
`ProcessBackfill()` runs each frame in `OnFrameStart`:

1. Computes `new_bits = client_backfill & ~sent_checks` per row/word, and resolves the
   row's runtime mode with `ChecklistRowMode` to fetch its `GameClearData`.
2. For each newly set bit at clear_kind `k`:
   - Sets the bit in `ap_save->sent_checks` and the shared mirror.
   - Sets `clear[mode][k].is_unlocked = 1` and `clear[mode][k].is_visible = 1` for visual
     consistency. (`is_visible` is what actually drives the grid to render the cell as
     revealed; `is_unlocked` alone leaves the cell hidden until the player next opens the
     checklist.)
   - If `ChecklistRewards_CellHasReceivedReward(mode, k)` returns true (i.e. a local AP
     reward — same-mode or cross-mode — is placed at this cell and its source bit is set
     in `received_checklist_rewards`), also sets `clear[mode][k].has_reward = 1`.
3. Calls `EvaluateGoal()` once at the end if any bit was actually processed.
4. Zeros `client_backfill` (single-writer protocol — the mod consumes, then zeros).

### Goal evaluation

`EvaluateGoal()` runs after every check transition (and on save load). It is sticky — once
`goal_complete` is set, it never re-evaluates. It loops all `CHECKLIST_MODE_NUM` rows, so
the AP tab carries a goal like any game mode. The per-row predicate is
`goal_satisfied(goal, row, count, n)`:

- **`GOAL_NONE`**: vacuously satisfied for that row.
- **`GOAL_100_CHECKLIST`**: the row's **"Fill in over 100 Checklist blocks!"** cell is
  checked in `sent_checks[row]` — NOT a popcount. The clear_kind is `Fill100ClearKind(row)`:
  AR `0x18` (`AR_CLEAR_FILL_100_BLOCKS`), TR `0x77` (`TR_CLEAR_FILL_100_BLOCKS`), CT `0x37`
  (`CT_CLEAR_FILL_100_BLOCKS`), `0xFF` (never satisfiable) for the AP row — the same vanilla
  auto-unlock cells the game flips once over 100 boxes are filled. This binds the goal to a
  real checkbox the same way `GOAL_HYDRA_AND_DRAGOON`/`GOAL_BEAT_KING_DEDEDE` bind to theirs.
- **`GOAL_N_CHECKLIST`**: `popcount(sent_checks[row]) >= options.checklist_amount[row]`.
  This is the synthetic count-threshold goal.
- **`GOAL_HYDRA_AND_DRAGOON`** (CT-anchored): bit `0x77` (`HYDRA_DRAGOON_CLEAR_KIND`) set in
  `sent_checks[CITYTRIAL]` — the single "In one match, complete both Dragoon and Hydra!"
  gameplay checkbox. This is NOT the two "Unlock Parts on the Checklist" cells (`0x6D`/`0x6E`),
  which are unrelated part-reward markers. The predicate is hardcoded against `CITYTRIAL` —
  evaluating it on a different row still queries the CT word.
- **`GOAL_BEAT_KING_DEDEDE`** (CT-anchored): bit `0x2F` (`KD_CLEAR_KIND`) set in
  `sent_checks[CITYTRIAL]`. Set via `SetNewUnlock(CITYTRIAL, 0x2F)` from
  `CityTrial_CheckStadiumResultObjectives` (`0x8004e998`) when the King Dedede KO time is
  nonzero and `<= 3600` (`0xE10`). At `0x8004eee0` the code calls `Ply_GetKingDededeKOTime`
  (`0x8022f568`), bails on `== 0`, skips on `> 3600` (`bgt`), otherwise loads `li r4,47`
  (`0x2F`) and `bl 0x8004a054`.
- **`GOAL_CHECKLIST_LIST`**: `(sent_checks[row] & goal_checks[row]) == goal_checks[row]` on
  both u64 words. `options.goal_checks[CHECKLIST_MODE_NUM][2]` is an AP-supplied per-row
  bitmask of required clear_kinds — every set bit must be checked. This lets the AP slot
  dictate exact required-checks lists, not just a count threshold.
- **`GOAL_MAX_STATS_CT`** (row-independent): `ap_save->max_stats_ct_achieved`, a sticky save
  bit. Set by a per-rider GOBJ proc in `goal_max_stats_ct.c` when a human player's CT stats
  all hit the per-slot patch-cap ceiling (`ap_save->options.city_trial_patch_cap_max`, **not**
  the hard `PATCH_STAT_MAX`) in one trial round. This goal is detected outside the
  `sent_checks` flow, so `goal_max_stats_ct.c` calls `CheckDetection_EvaluateGoal()` after
  flipping the bit.

Victory fires only if at least one row has a non-NONE goal AND every row's goal is satisfied.
When it fires, `ap_save->goal_complete = 1` is set, mirrored to `ap_data->goal_complete`, and
an aggregate "All Goals complete!" textbox is enqueued.

Short of victory, each row whose goal *just* became satisfied gets a one-shot
"<Mode> goal complete!" textbox from `AnnounceModeGoal(row)`, latched by
`ap_save->goal_announced[CHECKLIST_MODE_NUM]`. The AP row supplies its own name and theme
color, since `ModeColors[]` is sized `GMMODE_NUM`. When victory fires, every non-NONE row is
marked announced so the final row's per-mode message does not double up with the aggregate
one.

`CheckDetection_ResetAll()` clears `sent_checks`, `goal_announced`, `goal_complete` and
`max_stats_ct_achieved`; `CheckDetection_DebugForceMarkAll()` sets all of them.

### Collect / release semantics

When another player releases items destined for us, the items arrive via the existing
`incoming_item_id` mailbox path — `Grant()` sets `has_reward` on the target checkbox if there
is a local placement. **No effect on `sent_checks`** — release does not represent a check
completion on our side.

When another player collects items they own from our world, the AP server marks those
locations as checked from its authoritative view. The Python client sees the new entries in
`RoomUpdate.checked_locations` and writes them into `client_backfill`. `ProcessBackfill()`
consumes them on the next frame, setting `sent_checks` bits, `is_unlocked` for visual
consistency, `has_reward` where applicable, and re-evaluating the goal. So a passive collect
can trigger `goal_complete` without the player ever pressing a button — matching standard AP
semantics.

### When the card is written

`Hoshi_WriteSave()` mounts the card and rewrites the whole `"hoshi"` file **synchronously**,
stalling the frame for as long as the card I/O takes. Checks are recorded during gameplay —
including from per-frame GOBJ procs — so no detection path writes the card. Instead:

- `ap_save` is mutated in place; the completion is durable in RAM immediately.
- `ap_data` is mirrored in the same breath, so the AP client sees and forwards the location
  right away regardless of when the card is written.
- hoshi writes the card at every point the game requests its own save — it hooks each call
  site of `Memcard_ReqSave` (`0x80078990`), so a result screen, a stage select, a checklist
  unlock or main-menu entry all flush it, hash-gated so an unchanged save costs nothing.

Writing at the *same* instants as the vanilla save is what keeps the two files consistent, and
it matters here specifically. A check's completion is recorded in two places: `sent_checks` in
`APSave`, and `is_new`/`is_unlocked` on the cell in `GameClearData`, which rides the vanilla
save. `SetNewUnlockReplacement` detects a check by the `!is_new && !is_unlocked` transition, so
if the vanilla file were to persist the unlock while `APSave` rewound, the cell would read as
already complete and the check could never re-fire from gameplay — recoverable only by client
backfill, and not at all if the check was earned with no client attached.

The paths that still write immediately are one-shot and outside gameplay: the slot-options copy
at save load, `ApplyLocations` (once per client connection), the EnergyLink purchase (the pool
withdrawal reaches the server immediately, so the queued goods must not be able to rewind), and
the debug menu commands.

### Lifecycle (check detection)

```
OnBoot:
  CheckDetection_OnBoot()
    REPLACEFUNC ClearChecker_SetNewUnlock       -> CheckDetection_SetNewUnlockReplacement
    REPLACEFUNC ClearChecker_SetNewUnlockSilent -> CheckDetection_SetNewUnlockSilentReplacement
    HOOKAPPLY 0x8017efc0  - AR 100-checklist meta store
    HOOKAPPLY 0x8017eff8  - TR 100-checklist meta store
    HOOKAPPLY 0x8017f030  - CT 100-checklist meta store
    HOOKAPPLY 0x8017f0ac  - CT Dragoon assembly meta store
    HOOKAPPLY 0x8017f120  - CT Hydra assembly meta store
    HOOKAPPLY 0x80180a64  - filler gate (replaces vanilla immediate rejects)
    HOOKAPPLY 0x80180dc4  - filler-apply: RecordCheck when the player spends a filler

OnSaveLoaded:
  CheckDetection_OnSaveLoaded()
    Mirror ap_save->sent_checks -> ap_data->sent_checks (all CHECKLIST_MODE_NUM rows)
    Mirror ap_save->goal_complete -> ap_data->goal_complete
    EvaluateGoal()    - covers new options or already-saved sent_checks
                        satisfying the goal as of this boot

OnFrameStart:
  CheckDetection_OnFrameStart()
    ProcessBackfill()    - consume client_backfill

On gameplay-driven SetNewUnlock(mode, kind):
  CheckDetection_SetNewUnlockReplacement(mode, kind)
    fresh = !is_new && !is_unlocked   (always evaluated, even when cache valid)
    if fresh:
        RecordCheck(mode, kind)
            row = ChecklistModeRow(mode)
            SetSentCheck(row, kind)               - save + ap_data mirror
            log [Check] line with reward type
            "Check sent" textbox
            EvaluateGoal()
    if cache valid: return
    if fresh: SFX (one-frame cooldown)
    cd->clear[kind].is_new = 1

External save-bit goal triggers (e.g. goal_max_stats_ct.c):
  CheckDetection_EvaluateGoal()       - public re-eval entry point
```
