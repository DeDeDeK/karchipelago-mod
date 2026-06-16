# Clear Checker (Checklist) System

The clear-checker (`plclearcheckerlib`) is the game layer that detects checklist
cell completion and renders the checklist grid. This doc covers both the vanilla
machinery and the AP mod's two integration surfaces:

- **Inbound rewards** — `checklist_rewards.c` / `.h`: AP item delivery, cross-mode
  reward shuffle, the reward-table copies, and all the UI hooks that display
  shuffled rewards. (The "AP Integration", "Cross-Mode Protocol", and
  "Cross-Mode Display" sections below.)
- **Outbound checks** — `check_detection.c` / `.h`: the mod is the source of
  truth for which checkboxes the player has completed; it funnels completions
  into `ap_save->sent_checks` / `ap_data->sent_checks` and evaluates the slot's
  victory goal. (The "Check Detection" section below.)

**Entry points (where to start reading code):**

| Concern | Function / hook | Address / file |
|---------|-----------------|----------------|
| Mod boot — inbound | `AllocateRewardTables` + REPLACEFUNCs | `checklist_rewards.c` |
| Mod boot — outbound | `CheckDetection_OnBoot` | `check_detection.c` |
| AP item received | `ChecklistRewards_Grant` | `checklist_rewards.c` |
| Player completes a box | `CheckDetection_SetNewUnlockReplacement` (`0x8004A054`) | `check_detection.c` |
| Goal evaluation | `CheckDetection_EvaluateGoal` | `check_detection.c` |

The stat-measurement layer *beneath* the clear bits — the per-player item/box
collect counters that feed the "pick up N items" / "break N boxes" cells — is
documented in the sister doc **`docs/checklist-stat-tracking.md`**.

## Terminology

- **Location**: An in-game event that can be "checked" to send an item into the multiworld. In KAR, every checkbox (all 360: 120 per mode) is a location.
- **Item**: A reward that can be placed at any location in the multiworld. KAR items include machines, characters, music, sound tests, courses, stadiums, etc.
- **clear_kind**: Index 0–119 identifying a checkbox within a mode's `GameClearData.clear[120]` array. Each checkbox has an objective ("Finish 1st while flying") and optionally a reward ("Machine: Winged Star").
- **reward_index**: Index into the per-mode `RewardEntry` table. Each entry has a `clear_kind` (which checkbox), `reward_type`, and `reward_param`.

## Vanilla Game Structures

### GameClearData (0xF4 bytes per mode)

| Offset | Field | Description |
|--------|-------|-------------|
| 0x00 | `new_unlock_flag` | Nonzero when new unlocks exist requiring visual update |
| 0x01 | `display_state` | High nibble = pending new unlocks, low nibble bit 0 = shown/acknowledged |
| 0x02 | `checkbox_filler_num` | Number of checkbox fillers available to use |
| 0x03 | `checkbox_filler_list_len` | Length of filler list shown in UI (max 5) |
| 0x04 | `grid_mapping[120]` | Maps clear_kind → visual grid position (0–119) |
| 0x7C | `clear[120]` | Per-checkbox status bitfield (1 byte each) |

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
| `0x800076a0` | `gmGetClearcheckerTypeP(mode)` | Returns `GameClearData*` for a mode |
| `0x80049C20` | `Checklist_GetRewardNum(mode)` | Returns reward count for a mode |
| `0x80049C84` | `Checklist_GetClearKindFromRewardIndex(mode, idx)` | Returns `clear_kind` for a reward_index |
| `0x80049E24` | `ClearChecker_CheckUnlocked(mode, idx)` | Vanilla checks the `has_reward` bit for a reward's clear_kind. **Replaced** by `ChecklistRewards_CheckUnlocked`, which keys solely on the AP `received_checklist_rewards` bitfield (no `has_reward` fallback — see below). |
| `0x80049EC4` | `ClearChecker_GetRewardFromClearKind(mode, ck, &idx, &param)` | Reverse lookup: clear_kind → reward_index + reward_param. Sole caller is the audio/ending preview path in `Checklist_Think` (0x801804dc). **Replaced** by `ChecklistRewards_GetRewardFromClearKind` to avoid the clear_kind=0 sentinel aliasing under shuffle. |
| `0x80049D10` | Reward type lookup | Returns `stc_reward_table_ptrs[mode][reward_index].reward_type`. Called from the icon display function at `0x80182178`. |
| `0x80180508` | Audio preview scan | Inside `Checklist_Think`. Scans `stc_audio_preview_tables[current_mode]` for `reward_index`, calls `BGM_Play`, persists song to `GameData+0x4E`. **Hooked** to redirect to the source mode's table for cross-mode music rewards. |
| `0x8017DF5C` | `Checklist_SetRewardFlagOnUnlocks()` | Sets `has_reward` on unlocked slots, rebuilds grid, manages filler counters |
| `0x8017F3BC` | `Checklist_Think()` | Checklist state machine (filler placement, unlock animations, etc.) |
| `0x80181D70` | `Checklist_UpdateCellInfo()` | Per-frame hover display: looks up reward for hovered cell, displays text/icon |
| `0x801822F4` | Checklist init function | Loads SIS, creates grid cells, calls `SetRewardFlagOnUnlocks` |
| `0x80007AF0` | `Checklist_BuildUnlockBitfields()` | Caches unlock status into `GameData` + 0xD50 bitfields |
| `0x8017E490` | `Checklist_ProcessUnlock()` | Called from `Checklist_Think` case 1 on checklist entry. Scans clear[] to count completions, then writes the 5 meta auto-unlock bytes (100-checklist per mode, Dragoon, Hydra) via direct `stb` instructions that bypass `SetNewUnlock`. |

### Checklist_UpdateCellInfo Display Flow (0x80181D70)

Per-frame function that handles the hover tooltip for the currently selected checkbox.

**Register assignments (stable throughout function):**
- `r29` = cell info struct (+0x0C: text object, +0x10: previous reward_index, +0x11: previous clear_kind, +0x12: display state counter)
- `r30` = checklist UI struct (+0x14: current mode)
- `r31` = main menu instance
- `r28` = icon display object
- `r26` = clear_kind of hovered cell (after grid_mapping lookup)
- `r27` = reward_index result (or -1 if none)

**Flow:**
1. **Grid lookup** (0x80181DF8–0x80181ED0): Unrolled loop searches `grid_mapping` for the visual position matching the cursor. Result: `r26` = clear_kind.
2. **Same-cell check** (0x80181EDC): Compares `r26` with saved clear_kind at +0x11(r29). If same, skips reward lookup and jumps to display state logic at 0x80181F74.
3. **Reward lookup** (0x80181EE4–0x80181F5C): Searches the current mode's reward table for a `reward_index` whose `clear_kind` matches `r26`. Calls `ClearChecker_CheckUnlocked` to verify. Result: `r27` = reward_index (if found and unlocked) or -1.
4. **Display state change** (0x80181F74–0x80181FFC): Compares new `r27` with previous (from +0x10). If different, triggers either:
   - **No-reward → reward** (previous was -1): Icon animation at 0x80181FC8, display state = 0
   - **Reward → different reward**: Display state = 5 (immediate text)
   - **Reward → no-reward** (r27 < 0): Blank text at 0x80181F8C
5. **Text display** (0x80182000–0x80182030): When display state reaches 5: calls `Text_FinalizeSisText(text, reward_index + 0x7D)` to set up reward text. State increments each frame (0→1→2→3→4→5→6), text displays on state 5, stops at state 6.
6. **Icon display** (0x80181FC8–0x80181FE4): Called at step 4 when transitioning from no-reward to reward. Uses `mode * 2` as icon index via function at 0x80138B10.

### SIS Text System

SIS (String Information System) files contain pre-authored text commands for the checklist UI. The system supports 5 simultaneous SIS slots (`stc_sis_archives[5]` at `0x8059a848`, `stc_sis_data[5]` at `0x8059a85c`). Vanilla only uses slot 0.

**SIS files per mode:**
- `SisClrChk3D.dat` — Air Ride
- `SisClrChk2D.dat` — Top Ride
- `SisClrChkCT.dat` — City Trial

**Text indices within a SIS file:**
- `clear_kind + 4` — Objective text ("Finish 1st while flying")
- `reward_index + 0x7D` — Reward text ("Machine: Winged Star")
- `0x7C` — Blank text (shown when no reward to display)

**Text object fields:**
- `Text.sis_id` (offset 0x4F, u8): Selects which SIS slot to read from in `Text_FinalizeSisText`. Rendering (`Text_GX`) uses the canvas's `sis_idx` for glyph/font data.

`Text_FinalizeSisText(text, text_index)` reads `stc_sis_data[text->sis_id][text_index]` and stores a command data pointer at `text->command_data` (offset 0x5C). If `stc_sis_data[sis_id]` is NULL, the lookup is silently skipped.

### Unlock-gated settings menus (City Trial rules)

The City Trial pre-game **settings/rules menu** (minor scene `MNRKIND_CITYSETTINGS`, minor 5) is a *consumer* of `ClearChecker_CheckUnlocked` — distinct from the checklist grid above. Some rule-option rows (the "Extra Rules" reward category) only appear once unlocked, so the menu builder gates them on the clear-checker. This is the City Trial analog of the Top Ride rules-menu builder noted under `ChecklistRewards_CheckUnlocked` below.

| Address | Function | Role |
|---------|----------|------|
| `0x8001eaac` | `CitySettings_BuildMenu` | Build/enter handler for the settings menu (registered in the menu's `{build, update, destroy}` handler table at `0x807e0508`). Clears menu state, calls `CitySettings_BuildCellList`, then renders each cell via `CitySettings_RenderCellValue`. |
| `0x8001da14` | `CitySettings_BuildCellList` | Builds the list of visible setting rows. For the unlock-gated control-kinds it includes a row only if at least one of its candidate `clear_kind`s passes `ClearChecker_CheckUnlocked(mode=2, …)`; ungated kinds are copied verbatim. |
| `0x8001ddf8` | `CitySettings_RenderCellValue` | Draws (and normalizes) one row's value widget, reading/writing the packed setting bitfields at `GameData+0xb4`/`+0xb5`. |
| `0x8001ed14` | `CitySettings_UpdateCellHighlight` | Re-applies a cell's highlight / at-limit color after its value changes (called from `CitySettings_MinorThink` / `CitySettings_MinorLoad`). |

Because `ClearChecker_CheckUnlocked` is `REPLACEFUNC`'d to `ChecklistRewards_CheckUnlocked` (keyed on AP `received_checklist_rewards`), these rule options unlock on AP delivery like every other non-gated reward category.

## AP Integration — Implementation

**Files:** `checklist_rewards.c` / `checklist_rewards.h`.

### Core Systems

**AP ↔ game reward-index translation:**
- The apworld numbers each mode's rewards in **clear_kind-sorted order** (the order the checkboxes appear — exactly what `archipelago_api.h`'s `AP_REWARD_*` enum and `docs/checklist-mappings.csv` use). The game's internal reward table (`stc_reward_table_ptrs`) is in a different, ROM-defined order, and the entire mod machinery — `shuffled_rewards`, `received_checklist_rewards`, the parallel audio-preview / stadium lookup tables, the debug `GrantReward`/`GetShuffledReward` API — is keyed on that **internal game index**. Reordering the table in place is unsafe (it would desync the parallel hardcoded tables), so the mod keeps game-table order internally and translates at the two AP-client wire boundaries only.
- `BuildRewardIndexMaps()` (called from `AllocateRewardTables`, while the copied tables still hold native clear_kinds) builds `ap_to_game_ri[mode][ap_ri]` by ranking each reward's native clear_kind. `ChecklistRewards_ApToGameIndex(mode, ap_ri)` exposes it. It reproduces the apworld's numbering exactly for all three modes (AR 46 / TR 33 / CT 44 entries).
- **Boundary 1 — incoming item ID** (`ap_item_handler.c`): `reward_index = (id-500)%50` is the apworld's clear_kind-sorted index; translate via `ApToGameIndex` before `ChecklistRewards_Grant`.
- **Boundary 2 — `locations[]`** (`ChecklistRewards_ApplyLocations`): the client writes `locations[m][ap_ri]`; store into `shuffled_rewards[m][ap_to_game_ri[m][ap_ri]]`. The bijection covers `[0, count)` so each game index is written once.
- Everything inside those boundaries (Grant, the debug API, `ResolveCell`, `GetShuffledReward`) stays in **game-index** space and needs no translation. Without this, a received reward would decode to the wrong game-table row (e.g. the client's "TR Filler 5" → game index 25 = Ending), and `locations[]` placement would be scrambled.

**Reward table management:**
- `AllocateRewardTables()` (OnBoot): Allocates new `RewardEntry` tables via `HSD_MemAlloc`, copies originals, redirects `stc_reward_table_ptrs[mode]`. All game queries go through mod's copies.
- For same-mode rewards, `clear_kind` in the table is set to the assigned checkbox. For cross-mode-out rewards and remote rewards, `clear_kind` is set to 0 (safe sentinel — any value `>= 120` trips the vanilla OOB assert at 0x8004a08c; 0 is the smallest valid index). The sentinel is never *semantically* read — every vanilla-facing code path that would index `clear[]` by `RewardEntry.clear_kind` is now gated to run only for same-mode local placements.

**Same-mode placement predicate:**
- `IsSameModeLocalPlacement(mode, ri)` = `shuffled_rewards[mode][ri] != 0xFFFF` AND `(shuffled_rewards[mode][ri] >> 8) == mode`. This is the authoritative "this reward is actually placed in THIS mode's checklist" check. Cross-mode source rows fail because their saved encoding has `target_mode != source_mode`. Remote rewards fail because their saved encoding is `0xFFFF`.

**ClearChecker_CheckUnlocked replacement** (CODEPATCH_REPLACEFUNC):
- `ChecklistRewards_CheckUnlocked(mode, reward_index)`: returns true **iff** the reward's bit is set in AP `received_checklist_rewards` — i.e. AP delivery is the sole authority for a checklist reward being unlocked. There is intentionally **no `has_reward` fallback**: `has_reward` is also raised by in-game checkbox completion (vanilla `SetRewardFlagOnUnlocks`), so a fallback would unlock the reward the moment the player earns the box, *before* the AP server delivers the item. Gated categories (machines/colors/stadiums/stages/TR items/…) don't notice — their vanilla availability checks are `REPLACEFUNC`'d to read mod gate masks and never call this function. But the non-gated "cosmetic" categories (Sound Test, Music, Endings, Bonus Movie, Pause Power-ups, Extra Rules) read this function live (via `MainMenu_SoundTestThink`, `setLevelMusic`, `AirRide_CheckBonusUnlocked`, `MainMenu_OptionsThink`, `Pause_CheckStatsUnlocked`, and the TR rules-menu builder), so a `has_reward` fallback would let *those* unlock pre-delivery for same-mode-local placements. Keying purely on the received bitfield closes that leak. The reward icon still appears on in-game completion via `ChecklistRewards_FindRewardForCell`, which is independent of this function.

**ClearChecker_GetRewardFromClearKind replacement** (CODEPATCH_REPLACEFUNC at `0x80049EC4`):
- `ChecklistRewards_GetRewardFromClearKind(mode, clear_kind, &out_reward_index, &out_reward_param)`: Vanilla's sole caller is the audio/ending preview path at `0x801804dc` inside `Checklist_Think`. A vanilla scan of `RewardEntry.clear_kind` would alias against the cross-mode/remote `clear_kind=0` sentinel and return the wrong row (e.g. play the wrong music when hovering the legitimate same-mode placement at `clear_kind=0`).
- Resolves via `ChecklistRewards_ResolveCell(mode, clear_kind)` (cross_mode_slots → save_shuffled u16 scan). Returns the **source** mode's reward_index plus that row's `reward_param`. Honors vanilla's early-exit: only resolves when `is_unlocked || is_filler` on the target cell.

**Audio preview hook** (HOOKCONDITIONALCREATE at `0x80180508`):
- `ChecklistRewards_AudioPreview(reward_index)`: Replaces the vanilla per-entry scan that walks `stc_audio_preview_tables[current_mode]`. Reads `hover.source_mode` (set by the FindRewardForCell hook in `Checklist_UpdateCellInfo` on the prior frame for the hovered cell), looks up `reward_index` in the **source** mode's audio table, calls `BGM_Play(song_id)`, and persists the song_id to `MainMenuData.soundtest_bgm_kind` (`GameData+0x4E`).
- Reached only when `reward_param == REWARDPARAM_AUDIO`. Always alt-exits to `0x80180560` past the vanilla scan + `BGM_Play` + persist sequence. For same-mode placements `hover.source_mode == current_mode`, so behavior matches vanilla. If `hover.valid == 0` (no cell ever hovered) the hook alt-exits without playing anything — the audio-preview button is unreachable in practice on a never-hovered cell. Cross-mode ending previews (`REWARDPARAM_ENDING`) are safe to route through the unhooked vanilla path at `0x80180554` — vanilla's "ending preview" only sets a UI state byte and plays a menu click; no actual ending movie plays.

**Grant path:**
- `ChecklistRewards_Grant(mode, reward_index)`: Sets AP bitfield, decodes the placement cell directly from `shuffled_rewards[mode][reward_index]` (high byte = target mode, low byte = target clear_kind, `0xFFFF` = remote), writes `has_reward` to the correct mode's `GameClearData.clear[]`, updates the per-mode unlock bitfield cache (Air Ride / City Trial only — Top Ride has no cache slot). **Does not set `is_unlocked`** — that bit is reserved as the source of truth for "the player completed this checkbox in gameplay" and is owned by the check-detection system (`check_detection.c`). The reward icon still appears because it is driven by `has_reward`, not `is_unlocked`.
- `ApplyVanillaRewardUnlock(mode, reward_index, reward_type)` is invoked from Grant to route vanilla reward types into the mod's gate masks so the mask bit flips regardless of whether the reward arrived as an AP item or was earned by completing the originally-intended checkbox. Without this, vanilla rewards would write only to the dead in-game cache that our gate hooks bypass. Routing:
  - `REWARD_MACHINE_*` (13 values) → `GateMachines_UnlockMachine(VCKIND_*)`
  - `REWARD_KING_DEDEDE` → unlocks `VCKIND_WHEELDEDEDE` (the player-facing Dedede; `VCKIND_WHEELVSDEDEDE` is the stadium CPU-only machine and has no AP unlock). `REWARD_META_KNIGHT` → `VCKIND_WINGMETAKNIGHT`. (Character→machine resolution is via `CharacterDesc_GetMachineKind` inside the AR-character availability gate.)
  - `REWARD_DRAGOON` / `REWARD_HYDRA` → `VCKIND_DRAGOON` / `VCKIND_HYDRA`.
  - `REWARD_COLOR_*` (4 values) → `GateColors_UnlockColor(KIRBYCOLOR_*)`.
  - `REWARD_ITEM_CHICKIE` / `WHO_PAINT` / `LANTERN` → `GateTopRideItems_UnlockItem(TRITEM_*)`. These are TR-specific item unlocks; vanilla reads the checklist `has_reward` for these reward indices in `TopRide_OnCourseSelect` to drive `ItemMgr.enabled_mask` bits 20/18/15.
  - `REWARD_COURSE` → `GateAirRideStages_UnlockStage(AIRRIDE_NEBULA_BELT)` (only one course reward exists).
  - `REWARD_STADIUM` (CT) → `GateStadiums_UnlockStadium(...)` via `CtRewardIndexToStadium(reward_index)`. Vanilla `Checklist_ProcessUnlock` hardcodes the index→stadium mapping (reward_param is 0 for every stadium row), so the mod re-derives it: `37→DRAG4, 38→MELEE2, 39→DESTRUCTION3, 40→DESTRUCTION4, 41→DESTRUCTION5, 42→SINGLERACE9 (Nebula Belt)`.
  - `REWARD_DRAGOON_PART_*` / `REWARD_HYDRA_PART_*` → **not** routed to a gate. These are checklist-internal markers for the "all parts collected" meta-checkbox (CT clear_kind 0x6D Dragoon / 0x6E Hydra), distinct from the in-round legendary-piece spawn gates (`ITUNLOCK_DRAGOON1-3` / `ITUNLOCK_HYDRA1-3`). Vanilla's `Checklist_ProcessUnlock` marks the "parts collected" cell once all 3 of a set are collected — but it decides "collected" by reading the `has_reward` bit of the three part reward cells, which is placement-dependent and breaks under shuffle (see **Legendary-part assembly** below). The mod replaces that decision with a `received_checklist_rewards` check.
  - `REWARD_FILLER` / `BONUS_MOVIE` / `EXTRA_RULE` / `SOUND_TEST` / `MUSIC` / `ENDING` / `PAUSE_POWERUPS` → not gated, left to vanilla.

**Reward loop filter** (HOOKCONDITIONALCREATE at 0x8017DFD8):
- `ChecklistRewards_ShouldSkipReward(mode, reward_index)`: Skips every reward that fails `IsSameModeLocalPlacement` — both remote rewards and cross-mode source rows. Vanilla's reward loop at 0x8017df5c only ever touches `clear[mode][ck]` for same-mode placements, so it cannot spuriously set `has_reward` on `clear[m][0]` via the sentinel. Cross-mode placements get their `has_reward` set by the post-loop `ApplyCrossModeHasReward` hook at 0x8017e07c.

**Post-reward-loop hook** (HOOKCREATE at 0x8017E07C):
- `ChecklistRewards_ApplyCrossModeHasReward(current_mode)`: After the vanilla same-mode reward loop, iterates `cross_mode_slots[current_mode]` and on any checkbox where `(is_unlocked || is_filler) && !has_reward`, sets `has_reward = 1` for the display badge (cross-mode rewards are skipped by the vanilla loop, so their badge is set here).
- It does **not** grant a checkbox filler when the source is a `REWARD_FILLER`: filler tokens are granted once at AP receipt in `ChecklistRewards_Grant` (keyed off the reward's own mode). Granting here too would double-count (the reward is also delivered as an AP item) and would credit the wrong mode (`current_mode`, not the reward's mode). See **Checkbox filler grants** below.
- Clobbered instruction: `lbz r0, 0(r31)` (re-executed in the trampoline epilogue).

**Vanilla reward-loop filler grant — neutralized** (REPLACEINSTRUCTION at 0x8017E00C):
- The vanilla reward loop bumps `checkbox_filler_num`/`checkbox_filler_list_len` for reward indices in `stc_special_rewards[mode]` (the hardcoded `{0,1,2,3,4}` filler rows). Its first instruction (`li r0,5`) is replaced with `b +0x58` (→ `0x8017e064`, the loop increment), skipping the entire grant block while leaving the preceding `has_reward` store (0x8017e000-08) intact. This makes AP receipt the single filler-grant site. See **Checkbox filler grants** below.

**Legendary-part assembly — placement-independent** (two HOOKCONDITIONALCREATE in `Checklist_ProcessUnlock`, City Trial branch):
- Vanilla decides "all 3 Dragoon parts collected" → mark cell `0x6D` (at `0x8017f044`-`0x8017f094`) and "all 3 Hydra parts collected" → mark cell `0x6E` (at `0x8017f0b4`-`0x8017f108`) by ANDing the `has_reward` bit of the three part reward cells, resolved via `Checklist_GetClearKindFromRewardIndex` (CT reward indices 27/28/29 Dragoon, 31/32/33 Hydra). Under shuffle a cross-mode/remote part resolves to the `clear_kind=0` sentinel, so vanilla reads `clear[0]` instead — a false NEGATIVE (assembly never fires, the 0x6D/0x6E check is never sent), or a false POSITIVE if another reward sits at CT `clear_kind=0`.
- `Legendary_DragoonPartsReceived` / `Legendary_HydraPartsReceived` (hooks at `0x8017f044` / `0x8017f0b4`) replace the `has_reward`-AND condition with `(received_checklist_rewards[CITYTRIAL] & {3 part bits}) == {3 part bits}` — placement-independent, matching how `ClearChecker_CheckUnlocked` treats every other reward. On "all received" they fall into vanilla's own set-cell logic, which still runs the `0x8017f0ac`/`0x8017f120` MetaUnlock store hooks (check_detection.c) that send the 0x6D/0x6E check. The block is City-Trial-only (mode guard `cmplwi r3,2` at `0x8017f00c`).

### Cross-Mode Protocol

**u16 location encoding:**
- `(target_mode << 8) | clear_kind` — local reward at target_mode's checklist
- `0xFFFF` — remote reward (no local slot)
- Same-mode example: `(0 << 8) | 42 = 0x002A` — Air Ride reward at Air Ride checkbox 42
- Cross-mode example: `(2 << 8) | 10 = 0x020A` — Air Ride reward at City Trial checkbox 10

**CrossModeSlot mapping:**
- `cross_mode_slots[GMMODE_NUM][120]` maps (target_mode, clear_kind) → (source_mode, source_reward_index). `source_mode == 0xFF` = empty.
- Populated by `ChecklistRewards_ApplyLocations()` (from AP client data) and rebuilt by `RebuildRewardTablesFromShuffle()` (from persisted u16 arrays in `APSave`).
- Used by `ResolveCell` / `FindRewardForCell` for forward (cell → source reward) lookups on the UI hover path. Reverse lookups (reward → target cell) decode directly from `shuffled_rewards[source_mode][reward_index]`, which already encodes `(target_mode << 8) | target_clear_kind`.

**Save data (`APSave`, accessed via the global `ap_save`; defined in `mods/archipelago/src/main.h`):**
- `u16 shuffled_rewards[GMMODE_NUM][REWARD_COUNT_MAX]` — persisted u16 location encoding per reward_index. `0xFFFF` = remote.
- `u64 received_checklist_rewards[3]` — bit N = reward_index N received for that mode

**AP item IDs:** `AP_CHECKLIST_REWARD_BASE (500) + mode*50 + reward_index`

### Cross-Mode Display

**Multi-SIS loading** (HOOKCREATE at 0x801823C4):
- NOPs the 3 original per-mode `Text_LoadSisFile` calls (`0x80182378`, `0x8018238c`, `0x801823a0`).
- `LoadAllChecklistSIS(current_mode)`: Loads current mode's SIS into slot 0 (vanilla code works unchanged), other two modes into slots 1 and 2. `mode_to_sis_slot[GMMODE_NUM]` maps GameMode → slot index.

**Reward reverse lookup replacement** (HOOKCREATE at 0x80181EE4):
- `ChecklistRewards_FindRewardForCell(current_mode, clear_kind)`: Replaces vanilla's reward-table scan entirely — always alt-exits to 0x80181F5C, vanilla's scan never runs. This is required because a raw scan of `RewardEntry.clear_kind` would alias cross-mode source rows (sentinel 0) against a real same-mode placement at `clear_kind=0`.
- Resolves via `ChecklistRewards_ResolveCell` (cross_mode_slots first, then full-u16 match against `save_shuffled[current_mode]`). Comparing against the full u16 avoids the sentinel aliasing.
- AP `received_checklist_rewards` bit → return reward_index unconditionally. Otherwise: same-mode cells require `has_reward` set; cross-mode cells require `is_unlocked || has_reward` (the `is_unlocked` term covers the newly-completed-this-session window before the post-loop hook has mirrored has_reward).
- Snapshots `(mode, clear_kind, source_mode, valid=1)` into the static `hover` struct (also exposed to other modules via `ChecklistRewards_GetHoveredCell`). Downstream text/icon hooks read `hover.source_mode` to pick the correct SIS slot and reward table.
- Returns `reward_index + 1` for a visible reward, `-1` for empty/locked. Epilogue decrements r3 by 1 (or sets -1 on negative), stores to r0 so vanilla's post-alt-exit `mr r27, r0` lands the right value.

**Reward text display** (HOOKCREATE at 0x8018201C):
- `ChecklistRewards_DisplayRewardText(text, reward_index, current_mode)`: Temporarily sets `text->sis_id` to `mode_to_sis_slot[hover.source_mode]`, calls `Text_FinalizeSisText(text, reward_index + 0x7D)`, restores `sis_id` to 0. Command data comes from the source mode's SIS; glyph rendering uses slot 0's font (all checklist SIS files share the same font).

**Blank text fix** (HOOKCREATE at 0x80181F8C):
- `ChecklistRewards_SetBlankTextSisId(text, current_mode)`: Ensures `sis_id` is reset to 0 before displaying blank text (`0x7C`), in case a previous cross-mode hover left it changed.

**Reward type icon** (HOOKCREATE at 0x80182170):
- The reward type icon (machine, music, color, etc.) is displayed by a separate function at `0x801820B4`. It calls the reward-type lookup at `0x80049D10(mode, reward_index)` from `0x80182178` to read `stc_reward_table_ptrs[mode][reward_index].reward_type`. For cross-mode rewards, the mode must be the source mode. Hook at `0x80182170` replaces the mode in r3 with `hover.source_mode` (returned by `ChecklistRewards_GetHoverSourceMode()`), then skips the vanilla mode load at `0x80182174`, exiting to `0x80182178`.

### Lifecycle

```
OnBoot:
  AllocateRewardTables()          — allocate mod reward tables, redirect pointers
  REPLACEFUNC ClearChecker_CheckUnlocked            → ChecklistRewards_CheckUnlocked
  REPLACEFUNC ClearChecker_GetRewardFromClearKind   → ChecklistRewards_GetRewardFromClearKind
  HOOKAPPLY 0x8017dfd8            — skip remote/cross-mode rewards in SetRewardFlagOnUnlocks
  HOOKAPPLY 0x8017e07c            — post-loop: apply cross-mode has_reward (badge only)
  REPLACEINSTRUCTION 0x8017e00c  — neutralize vanilla reward-loop filler grant (b 0x8017e064)
  HOOKAPPLY 0x8017f044            — legendary Dragoon-parts assembly: received-based (cell 0x6D)
  HOOKAPPLY 0x8017f0b4            — legendary Hydra-parts assembly: received-based (cell 0x6E)
  HOOKAPPLY 0x80180508            — cross-mode audio preview (source mode's audio table)
  NOP 0x80182378/8c/a0            — disable vanilla per-mode SIS loading
  HOOKAPPLY 0x801823c4            — multi-SIS loading
  HOOKAPPLY 0x80181ee4            — cross-mode reward lookup (also snapshots hover state)
  HOOKAPPLY 0x8018201c            — cross-mode reward text display
  HOOKAPPLY 0x80181f8c            — blank text sis_id fix
  HOOKAPPLY 0x80182170            — cross-mode reward type icon
  ClearCrossModeSlots()

OnSaveInit (fresh save creation):
  main.c OnSaveInit() does memset(ap_save, 0), then calls
  ChecklistRewards_OnSaveInit(), which fills ap_save->shuffled_rewards[*][*]
  with 0xFFFF (remote sentinel) — required because zero would alias a valid
  (mode=AR, clear_kind=0) placement.

OnSaveLoaded:
  RebuildRewardTablesFromShuffle()  — rebuild stc_reward_table_ptrs[m][i].clear_kind + cross_mode_slots
                                      from saved shuffled_rewards u16 arrays
  RegrantAllReceivedRewards()       — replays Grant() for every bit in
                                      received_checklist_rewards (restores has_reward,
                                      gate-mask routing, and cache bits)

ApplyLocations (from AP client, when ap_data->location_data_valid is set):
  Copy ap_data->locations[m][i] into ap_save->shuffled_rewards[m][i]
  RebuildRewardTablesFromShuffle()
  RegrantAllReceivedRewards()       — re-applies grants for rewards already received
                                      before the assignment arrived
  ap_data->location_data_valid = 0
  Hoshi_WriteSave()
```

## Check Detection (Outbound)

**Files:** `check_detection.c` / `check_detection.h`.

The mod is the source of truth for which checkboxes the player has completed.
The Python AP client reads `ap_data->sent_checks[3][2]` (a per-mode
bitmask, packed as 2× `u64` to cover the 0..119 clear_kind range) and forwards
new bits as AP location checks. The bitmask is also persisted to `APSave`
(global `ap_save`) as `ap_save->sent_checks[3][2]` so completions survive reboots.

### How completions are detected

**Primary path: REPLACEFUNC on `ClearChecker_SetNewUnlock` (`0x8004A054`).**
This is the central funnel that most in-game gameplay code uses to flag
completed objectives — 126 call sites (Ghidra xref count) covering Air Ride /
City Trial completion paths plus stadium results and free-run trackers.

**Companion path: REPLACEFUNC on `ClearChecker_SetNewUnlockSilent` (`0x80049FCC`).**
The Top Ride checklist evaluator (the cluster of functions at `0x802b7xxx`)
does **not** use `SetNewUnlock` — it commits every gameplay objective through
this "silent" variant (51 call sites, all Top Ride). Each call site plays its
own unlock SFX and prints `ClearChecker(<clear_kind+1>)` before calling, and
`SetNewUnlockSilent` just sets the `is_new` bit (no SFX, no cache-aware SFX
cooldown). Without replacing it, **every Top Ride gameplay check is
silently dropped** — never added to `sent_checks`, never sent to the server, and
TR `GOAL_CHECKLIST_LIST` goals (e.g. "Cross the goal 20 or more times!" = TR
clear_kind 0, "Compete in more than 10 multiplayer races!" = TR clear_kind 2)
could never complete. `CheckDetection_SetNewUnlockSilentReplacement` mirrors the
SetNewUnlock replacement (transition detect → `RecordCheck` → run the vanilla
silent body) but omits the SFX block, since the caller already played it.

**Companion path: filler-apply hook (`CODEPATCH_HOOKCREATE` at `0x80180dc4`).**
When the player *spends* a checkbox filler, `Checklist_Think` sets
`clear[k].is_filler` directly via `ori r0,r0,2; stb r0,124(r3)` at
`0x80180dbc` and **does not** call `ClearChecker_SetNewUnlock` — so neither of
the funnel replacements above sees it, and the spent cell would never be
recorded. `CheckDetection_OnFillerApplied(mode, clear_kind)` hooks the
following instruction (`lbz r3,2(r29)`, the start of the
`checkbox_filler_num` decrement), reads `mode` from `r31+0x14` and
`clear_kind` from the non-volatile `r18`, and calls `RecordCheck` (idempotent,
so repeated firings are harmless). (Note: a *separate* filler-related
`SetNewUnlock` call does exist at `0x8017FAE4`, but that is not the spend path
this hook covers.)

`CheckDetection_SetNewUnlockReplacement(mode, clear_kind)`:
1. Reads the current `clear[mode][clear_kind]` byte. If neither `is_new` nor
   `is_unlocked` is already set, this is a true transition — call
   `RecordCheck(mode, clear_kind)`. **Transition detection runs regardless of
   the vanilla cache-valid short-circuit** so AP never misses a check.
2. `RecordCheck()` sets the bit in `ap_save->sent_checks` and the shared
   `ap_data->sent_checks` mirror, logs the placement (resolves the
   cell via `ChecklistRewards_ResolveCell` to print `[Check] mode=… clear_kind=…
   type=… recorded` with the source reward type — or a "no local reward
   placement" line for remote/empty cells), calls `EvaluateGoal()`, and persists
   via `Hoshi_WriteSave()`.
3. Reimplements the vanilla SetNewUnlock logic: bail if cache valid, OOB
   clamp, play the unlock SFX (`SFX_PlayFullVolume(0x10008)`) guarded by the
   one-frame cooldown at `*stc_clearchecker_sfx_last_frame`, then set the
   `is_new` bit on the clear[] byte.

**Secondary path: 5 `CODEPATCH_HOOKCONDITIONALCREATE` hooks inside
`Checklist_ProcessUnlock` (0x8017e490).** Five "meta" auto-unlock checkboxes
do NOT go through `SetNewUnlock` — vanilla sets their `clear[]` byte via
direct `stb` instructions inside `Checklist_ProcessUnlock`, which
`Checklist_Think` case 1 calls when the checklist is entered. We hook each
store site directly so detection fires synchronously at the moment vanilla
commits the unlock, and so we can conditionally suppress the `stb` when the
cell has already been filler-completed:

| Mode | clear_kind | Description | Hook site |
|------|-----------|-------------|-----------|
| Air Ride | 0x18 | Complete 100 checkboxes | `0x8017efc0` (`stb r4,148(r30)`) |
| Top Ride | 0x77 | Complete 100 checkboxes | `0x8017eff8` (`stb r4,243(r30)`) |
| City Trial | 0x37 | Complete 100 checkboxes | `0x8017f030` (`stb r4,179(r30)`) |
| City Trial | 0x6D | Unlock Dragoon Parts on the Checklist (all 3 parts received) | `0x8017f0ac` (`stb r0,233(r30)`) |
| City Trial | 0x6E | Unlock Hydra Parts on the Checklist (all 3 parts received) | `0x8017f120` (`stb r0,234(r30)`) |

At every site `r30 = gmGetClearcheckerTypeP(mode)` and the store immediate is
`0x7C + clear_kind`. Each hook has an empty prologue, calls a thin handler
that invokes `RecordCheck(mode, clear_kind)` with the hardcoded pair, then
re-materializes the stored value (`li r4,1` or `li r0,1`) in the epilogue so
the trampoline's auto-re-execute of the clobbered `stb` still lands a 1 after
`bl` clobbered the volatile register. `RecordCheck` is idempotent, so replays
on subsequent `Checklist_ProcessUnlock` invocations are harmless.

**Handler return value controls whether vanilla's `stb` runs:**
- **Return 0 (accept)** — the usual path. The clobbered `stb` auto-re-executes
  (with the epilogue's restored register value), writing `0x01` to `clear[k]`,
  and control continues to the vanilla display_state update sequence and then
  the function tail.
- **Return 1 (skip)** — taken iff `clear[k].is_filler` is already set at hook
  entry. Control branches directly to `0x8017f394` (the function tail),
  bypassing both the `stb` and the subsequent display_state update. This
  preserves the filler byte: the vanilla store would overwrite `is_filler`,
  `has_reward`, and `is_visible` with a bare `0x01`, and `is_filler` in
  particular has no other code path to restore it. The filler code path
  already drove the cell to a completed state through `SetNewUnlock`, so the
  display_state increment is also redundant. The result: a cell that was
  filler'd before its auto-unlock condition became true retains its filler
  visual marker instead of flipping to an "auto-unlocked" appearance.

### Backfill (client → mod)

The client can write to `ap_data->client_backfill[3][2]` to back-fill
checks the AP server already knows about (e.g., fresh save or slot takeover).
`ProcessBackfill()` runs each frame in `OnFrameStart`:

1. Computes `new_bits = client_backfill & ~sent_checks` per mode/word.
2. For each newly set bit at clear_kind `k`:
   - Sets the bit in `ap_save->sent_checks` and the shared mirror.
   - Sets `clear[mode][k].is_unlocked = 1` and `clear[mode][k].is_visible = 1`
     for visual consistency. (`is_visible` is what actually drives the grid
     to render the cell as revealed; `is_unlocked` alone leaves the cell
     hidden until the player next opens the checklist.)
   - If `ChecklistRewards_CellHasReceivedReward(mode, k)` returns true (i.e. a
     local AP reward — same-mode or cross-mode — is placed at this cell and
     its source bit is set in `received_checklist_rewards`), also sets
     `clear[mode][k].has_reward = 1`.
3. Calls `EvaluateGoal()` once at the end if any bit was actually processed.
4. Zeros `client_backfill` (single-writer protocol — mod consumes, then zeros).
5. Saves to memory card via `Hoshi_WriteSave()`.

### Goal evaluation

`EvaluateGoal()` runs after every check transition (and on save load). It is
sticky — once `goal_complete` is set, it never re-evaluates. The per-mode
predicate is `goal_satisfied(goal, mode, count, n)`:

- **`GOAL_NONE`**: vacuously satisfied for that mode.
- **`GOAL_100_CHECKLIST`**: the per-mode **"Fill in over 100 Checklist blocks!"** cell is
  checked in `sent_checks[mode]` — NOT a popcount. The clear_kind is `Fill100ClearKind(mode)`:
  AR `0x18` (`AR_CLEAR_FILL_100_BLOCKS`), TR `0x77` (`TR_CLEAR_FILL_100_BLOCKS`), CT `0x37`
  (`CT_CLEAR_FILL_100_BLOCKS`) — the same vanilla auto-unlock cells the game flips once over 100
  boxes are filled (see the `MetaUnlock_*100` hooks). This binds the goal to a real checkbox the
  same way `GOAL_HYDRA_AND_DRAGOON`/`GOAL_BEAT_KING_DEDEDE` bind to their cells. (`GOAL_N_CHECKLIST`
  below is the synthetic popcount-threshold goal.)
- **`GOAL_N_CHECKLIST`**: `popcount(sent_checks[mode]) >= options.checklist_amount[mode]`.
- **`GOAL_HYDRA_AND_DRAGOON`** (CT-anchored): bit `0x77` (`HYDRA_DRAGOON_CLEAR_KIND`)
  set in `sent_checks[CITYTRIAL]` — the single "In one match, complete both Dragoon
  and Hydra!" gameplay checkbox. This is NOT the two "Unlock Parts on the Checklist"
  cells (`0x6D`/`0x6E`), which are unrelated part-reward markers. The predicate is
  hardcoded against `CITYTRIAL` — evaluating it on a different mode still queries the
  CT word.
- **`GOAL_BEAT_KING_DEDEDE`** (CT-anchored): bit `0x2F` (`KD_CLEAR_KIND`) set in
  `sent_checks[CITYTRIAL]`. Set via `SetNewUnlock(CITYTRIAL, 0x2F)` from
  `CityTrial_CheckStadiumResultObjectives` (`0x8004e998`) when the King Dedede KO
  time is nonzero and `<= 3600` (`0xE10`). At `0x8004eee0` the code calls
  `Ply_GetKingDededeKOTime` (`0x8022f568`), bails on `== 0`, skips on `> 3600`
  (`bgt`), otherwise loads `li r4,47` (`0x2F`) and `bl 0x8004a054`
  (`ClearChecker_SetNewUnlock`).
- **`GOAL_CHECKLIST_LIST`**: `(sent_checks[mode] & goal_checks[mode]) == goal_checks[mode]`
  on both u64 words. `options.goal_checks[GMMODE_NUM][2]` is an AP-supplied
  per-mode bitmask of required clear_kinds — every set bit must be checked.
  `GOAL_CHECKLIST_LIST` lets the AP slot dictate exact required-checks lists,
  not just a count threshold.
- **`GOAL_MAX_STATS_CT`** (mode-independent): `ap_save->max_stats_ct_achieved`,
  a sticky save bit. Set by a per-rider GOBJ proc in `goal_max_stats_ct.c`
  when a human player's CT stats all hit the per-slot patch-cap target
  (`ap_save->options.city_trial_patch_cap_amount`, **not** the hard `PATCH_STAT_MAX`)
  in one trial round. This goal is detected outside the `sent_checks` flow, so
  `goal_max_stats_ct.c` calls `CheckDetection_EvaluateGoal()` after flipping the bit.

Victory fires only if at least one mode has a non-NONE goal AND every mode's
goal is satisfied. When victory fires, `ap_save->goal_complete = 1` is set,
mirrored to `ap_data->goal_complete`, and persisted to the memory
card. `CheckDetection_ResetAll()` clears `max_stats_ct_achieved` along with
`sent_checks` and `goal_complete`; `CheckDetection_DebugForceMarkAll()` sets
all three.

### Collect / Release semantics

When another player releases items destined for us, the items arrive via the
existing `incoming_item_id` mailbox path — `Grant()` sets `has_reward` on the
target checkbox if there's a local placement. **No effect on `sent_checks`** —
release does not represent a check completion on our side.

When another player collects items they own from our world, the AP server
marks those locations as checked from its authoritative view. The Python
client sees the new entries in `RoomUpdate.checked_locations` and writes them
into `client_backfill`. `ProcessBackfill()` consumes them on the next frame,
setting `sent_checks` bits, `is_unlocked` for visual consistency, `has_reward`
where applicable, and re-evaluating the goal. This means a passive collect can
trigger `goal_complete` without the player ever pressing a button — matching
standard AP semantics.

### Lifecycle (check detection)

```
OnBoot:
  CheckDetection_OnBoot()
    REPLACEFUNC ClearChecker_SetNewUnlock       → CheckDetection_SetNewUnlockReplacement
    REPLACEFUNC ClearChecker_SetNewUnlockSilent → CheckDetection_SetNewUnlockSilentReplacement  (Top Ride path)
    HOOKAPPLY 0x8017efc0  — AR 100-checklist meta store
    HOOKAPPLY 0x8017eff8  — TR 100-checklist meta store
    HOOKAPPLY 0x8017f030  — CT 100-checklist meta store
    HOOKAPPLY 0x8017f0ac  — CT Dragoon assembly meta store
    HOOKAPPLY 0x8017f120  — CT Hydra assembly meta store
    HOOKAPPLY 0x80180a64  — filler gate (replaces vanilla immediate rejects)
    HOOKAPPLY 0x80180dc4  — filler-apply: RecordCheck when player spends a filler

OnSaveLoaded:
  CheckDetection_OnSaveLoaded()
    Mirror ap_save->sent_checks → ap_data->sent_checks
    Mirror ap_save->goal_complete → ap_data->goal_complete
    EvaluateGoal()    — covers the case where new options or already-saved
                        sent_checks satisfy the goal as of this boot

OnFrameStart:
  CheckDetection_OnFrameStart()
    ProcessBackfill()    — consume client_backfill

On gameplay-driven SetNewUnlock(mode, kind):
  CheckDetection_SetNewUnlockReplacement(mode, kind)
    fresh = !is_new && !is_unlocked   (always evaluated, even when cache valid)
    if fresh:
        RecordCheck(mode, kind)
            SetSentCheck(mode, kind)              — save + ap_data mirror
            log [Check] line with reward type
            EvaluateGoal()
            Hoshi_WriteSave()
    if cache valid: return
    if fresh: SFX (one-frame cooldown)
    cd->clear[kind].is_new = 1

External save-bit goal triggers (e.g. goal_max_stats_ct.c):
  CheckDetection_EvaluateGoal()       — public re-eval entry point
  Hoshi_WriteSave()                   — persist whatever save bit was set
```

## Notes

**Cross-mode objective text:** When a cross-mode reward is placed at a checkbox, the objective text shown when hovering comes from the target mode's SIS (since it uses `clear_kind + 4` with `sis_id = 0`). This is correct — the objective belongs to the target mode's checklist.

## Reference

### Checklist Grid

- **12 columns × 10 rows = 120 cells**, hardcoded throughout 6+ functions as arithmetic constants.
- `grid_mapping[120]` maps clear_kind → visual grid position. Filled by `Checklist_InitGridMapping` (0x8004A2BC) using `HSD_Randi`.
- Pre-assigned positions exist for "meta" objectives (100-checkbox completion, Dragoon/Hydra assembly).
- Grid dimensions are deeply hardcoded and impractical to change.

### Checkbox Fillers

- Filler grants increment `checkbox_filler_num` (uncapped u8) and `checkbox_filler_list_len` (capped at 5).
- `Checklist_GrantFiller(mode)` provided as static inline in `game.h`.
- Filler placement handled by `Checklist_Think` states 5–9. State 8 validates target slot is empty.
- `checkbox_filler_num` lives in `GameClearData` (the game's native clear data), **not** in `APSave`. It is consumed when the player spends a filler, and `is_filler` cells (the spend record) have no mod-side reconstruction path — so the mod treats `GameClearData` as persisting across boots via the game's native save.

#### Checkbox filler grants (AP receipt is the sole authority)

A `REWARD_FILLER` arriving from AP grants exactly **one** usable filler token, in `ChecklistRewards_Grant`, to the **reward's own mode** (matching the `(<Mode>)` the textbox names) — independent of where the reward is placed (`shuffled_rewards`). Placement only drives the `has_reward` display badge.

- The grant fires only on a **real receipt** (`announce=1`): the AP item handler and the debug `GrantReward` API. The replay path (`RegrantAllReceivedRewards`, `announce=0`, run on every save-load and after `ApplyLocations`) must not re-grant, or `checkbox_filler_num` would inflate every boot.
- Both cell-COMPLETION grant paths are disabled so this is the single grant site: vanilla's reward-loop grant (REPLACEINSTRUCTION at `0x8017e00c`) and `ApplyCrossModeHasReward`. They would key off cell completion, not AP receipt; and because `Grant` writes `has_reward` on receipt, it pre-empts vanilla's "grant on `has_reward` 0→1 transition" anyway.
- Direct filler items (`AP_ITEM_CHECKBOX_FILLER_*`, used by the EnergyLink filler-buy and the debug filler-give) bypass `Grant` entirely — they call `Checklist_GrantFiller` directly in `ap_item_handler.c` — and are unaffected.

### Filler gate (AP goal protection)

Vanilla hardcodes a filler reject for 3 physical grid slots via immediate compares in `Checklist_Think` at `0x80180A74`–`0x80180A98`. Under the fixed vanilla `grid_mapping[]` those 3 slots cover the 5 meta auto-unlock cells (AR/TR/CT 100-checklist, CT Dragoon, CT Hydra); vanilla blocks them so the player can't cheese an auto-unlock by spending a filler.

Under reward shuffle that reasoning is wrong — any of those cells may hold a legitimate shuffled reward, and the player must be free to filler it. The mod installs a single `HOOKCONDITIONALCREATE` at `0x80180A64` (the vanilla `lbz r3, 20(r31)` mode-load) that:

- Replays vanilla's phys_slot computation (`row + col*12`, column-major) in the prologue, stashing the result in r18 (non-volatile) where downstream code at `0x80180AA4` expects it.
- Calls `FillerGate_IsRejected(mode, phys_slot)` — returns 1 to reject, 0 to accept.
- On accept: the clobbered `lbz r3, 20(r31)` auto-re-executes (restoring r3 = mode for the downstream `bl gmGetClearcheckerTypeP` at `0x80180A9C`), then branches past all three vanilla immediate rejects.
- On reject: branches to `0x80180C24`, vanilla's sole `playSoundFX_errorNoise` call site.

`FillerGate_IsRejected` reads `ap_save->options.goal[mode]` and protects only goal cells, translated from clear_kind to physical slot via `cd->grid_mapping[]`:

| `GoalKind` | Protected cells |
|------------|-----------------|
| `GOAL_HYDRA_AND_DRAGOON` | (CT only) CT clear_kind 0x77 ("In one match, complete both Dragoon and Hydra!"). On non-CT modes the gate returns 0 (nothing to protect). |
| `GOAL_BEAT_KING_DEDEDE`  | (CT only) CT clear_kind `KD_CLEAR_KIND` (0x2F). On non-CT modes the gate returns 0. |
| `GOAL_CHECKLIST_LIST` | every clear_kind whose bit is set in `options.goal_checks[mode]` (iterated via `__builtin_ctzll` over both u64 words). Per-mode — protects exactly the cells the AP slot listed as required. |
| `GOAL_100_CHECKLIST` / `GOAL_N_CHECKLIST` | none — count thresholds, filler'ing any cell still costs a filler token |
| `GOAL_MAX_STATS_CT` | none — the goal is set by a runtime save bit independent of any specific cell, so there's nothing to protect at the checklist level |
| `GOAL_NONE` | none |

Implemented in `check_detection.c` alongside the goal-evaluation logic.

### Auto-unlock Objectives

| Mode | ClearKind | Description |
|------|-----------|-------------|
| Air Ride | 0x18 (24) | Complete 100 checkboxes |
| Top Ride | 0x77 (119) | Complete 100 checkboxes |
| City Trial | 0x37 (55) | Complete 100 checkboxes |
| City Trial | 0x6D (109) | Unlock Dragoon Parts on the Checklist (all 3 parts received) |
| City Trial | 0x6E (110) | Unlock Hydra Parts on the Checklist (all 3 parts received) |
