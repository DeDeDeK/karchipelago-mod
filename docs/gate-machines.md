# Machine Gating

Every machine can be individually locked behind an Archipelago unlock item. One `machine_unlocked_mask` covers all three modes: City Trial field spawns and select grids, the Air Ride character select, and the Top Ride lobby's Free Star / Steer Star control types.

## What Is Gated

The 26 `MachineKind`/`VCKIND` values (`VCKIND_NUM` = 26, `machine.h`), one bit each in `machine_unlocked_mask`. AP item ID = 830 + VCKIND for the 25 in-range IDs 830–854; VCKIND 25 (`WHEELVSDEDEDE`) is outside the handler's range entirely.

Six VCKINDs have no `CharacterKind` and are city-spawn or transformation only: FREE, STEER, WINGKIRBY, WHEELNORMAL, WHEELKIRBY, WHEELVSDEDEDE. Character-to-machine resolution goes through `CharacterDesc_GetMachineKind` (`menu.h`), which un-does the bike-relative encoding of `CharacterDesc.machine_kind`; the full CharacterKind ↔ MachineKind table is in `css-system.md`.

**`CT_SPAWN_EXCLUDED_MASK`** force-zeros the non-field machines in the CT spawn pipeline regardless of unlock state: `VCKIND_FREE` / `VCKIND_STEER` (Top Ride-only forms), `VCKIND_WINGKIRBY` / `VCKIND_WHEELNORMAL` / `VCKIND_WHEELKIRBY` (Kirby transformation forms reached through the copy-ability item path, not a field machine spawn), and `VCKIND_WINGMETAKNIGHT` / `VCKIND_WHEELDEDEDE` / `VCKIND_WHEELVSDEDEDE` (the Meta Knight / King Dedede character forms). The two character forms matter because unlocking Meta Knight or King Dedede sets the corresponding bit; both have a 0 base spawn chance in vanilla, so without the exclusion the unlocked-but-zero-chance fallback (weight 10, below) would leak the Wing Meta Knight / Dedede Wheelie machine onto the City Trial field as a rideable star.

## Entry Points

**Files:** `mods/archipelago/src/gate_machines.c` / `gate_machines.h`

| Symbol | Kind | Where | Role |
|--------|------|-------|------|
| `GateMachines_OnBoot()` | mod | gate_machines.c | Applies all hooks (called from `main.c`). |
| `GateMachines_On3DLoadEnd(void)` | mod | gate_machines.c | Clears `legendary_assembled_mask` at scene load. |
| `GateMachines_SelectSpawn(MachineSpawnData*, float)` | mod | gate_machines.c | Full replacement for the CT field spawn selection. |
| `GateMachines_CountCTSelectAvailable()` | mod | gate_machines.c | Counting pass for the CT machine-select grid. |
| `GateMachines_BuildCTSelectArray(u8*, u8*)` | mod | gate_machines.c | Array-building pass for the CT machine-select grid. |
| `GateMachines_FinalizeCTMachine(int slot)` | mod | gate_machines.c | Commits the CT starting machine and CPU color per slot. |
| `GateMachines_NoteManualMachinePick(int slot)` | mod | gate_machines.c | Records a player-driven grid pick for `slot`. |
| `GateMachines_ResetStartingMachine(RiderData*)` | mod | gate_machines.c | Respawn machine selection. |
| `GateMachines_FixupTRInit(u8 *lobby_base)` | mod | gate_machines.c | Unlock-aware default for the four TR lobby panels. |
| `GateMachines_CycleTRMachine(u8*, u32)` | mod | gate_machines.c | Gated L/R cycler for the TR Control Type row. |
| `GateMachines_TRLobbyCanStart(void)` | mod | gate_machines.c | Blocks Start when no TR machine is unlocked. |
| `GateMachines_CheckAirRideCharacterAvailable(CharacterKind)` | mod | gate_machines.c | Replacement for `AirRide_CheckCharacterAvailable`. |
| `GateMachines_CheckTitleDemoMachineUnlocked(s8, s8)` | mod | gate_machines.c | Replacement for `TitleScreen_CheckMachineUnlocked`. |
| `GateMachines_ClearAirRideList(u8 *base)` | mod | gate_machines.c | Zeroes the AR CSS available-machine list each frame. |
| `GateMachines_GiveLegendaryMachine(int)` | mod | gate_machines.c | Delivery for the AP "give legendary" items (not a gate). |
| `GateMachines_UnlockMachine(MachineKind, int announce)` | mod | gate_machines.c | Sets the unlock bit and posts a textbox. Called from `ap_item_handler.c` and `checklist_rewards.c`. |
| `IsCKindUnlocked` / `GetFirstUnlockedCTMachine` / `RandomUnlockedKirbyCKind` / `IsTRMachineUnlocked` / `GetFirstUnlockedTRMachine` / `GetRandomUnlockedTRMachine` | mod (static) | gate_machines.c | Mask query helpers. |
| `CityMachineSpawn_DecideAndSpawn` | game | 0x801defac | Normal CT field machine spawn. |
| `cityTrialSpawnFormationStar` | game | 0x801df408 | Machine Formation event spawn. |
| `CitySelect_CreateMachineIcons` | game | 0x8002e3c4 | Builds the CT machine-select icon grid. |
| `CitySelect_InitPlayerMachines` | game | 0x8002ddd8 | Commits the per-slot CT starting machine. |
| `CitySelect_Cursor1InputThink` | game | 0x800312fc | CT grid cursor input + row-split threshold. |
| `Rider_ResetStartingMachine` | game | 0x80195288 | Respawn machine assignment. |
| `AirRide_CheckCharacterAvailable` | game | 0x8002090c | AR select screen availability (replaced). |
| `TitleScreen_CheckMachineUnlocked` | game | 0x8000c364 | Title-screen demo machine availability (replaced). |
| `AirRide_PopulateSelectIcons` | game | 0x80020a08 | Rebuilds the AR available-machine list every CSS frame. |
| `TopRide_CSS_PanelThink` / `TopRide_SoloPanelThink` | game | 0x8002b8a8 / 0x8002ca80 | TR panel think, race and solo. |
| `TopRide_PreGameThink` / `TopRide_OnCourseSelect` | game | 0x8002c06c / 0x8002cc30 | TR start-match bodies, race and solo. |
| `TopRide_InitSelectData` / `TopRide_RaceInit` / `TopRide_SoloInit` | game | 0x8002cfd8 / 0x8002d0ec / 0x8002d9e8 | TR lobby init paths. |
| `LegendaryMachine_StartAssembly` | game | 0x80283cf0 | Plays the legendary assembly cinematic. |

## City Trial — Field Spawning

### Game system

Machine spawning in City Trial is chance-based. The game reads a spawn table (`(*stc_vcDataCommon)->spawn_data->spawn_desc[]`; `stc_vcDataCommon` is `vcDataCommon**` at r13+0x758, the `spawn_data` sub-struct pointer sits at `vcDataCommon+0x20` and `spawn_desc` at `spawn_data+0x8`) indexed by match progress, with `float chance[VCKIND_NUM]` per entry. Some machines have 0 weight at certain match progress points (e.g. Hydra/Dragoon early in a match).

`CityMachineSpawn_DecideAndSpawn` (0x801defac) and `cityTrialSpawnFormationStar` (0x801df408) build a `u32` exclusion bitmask from `MachineSpawnData.prev_machine_kind[4]` (+0x50, recently spawned machines), then do a two-pass weighted random selection: pass 1 (r5) computes the total spawn chance sum, pass 2 (r29) performs the selection via `HSD_Randf`.

Hooking that exclusion bitmask to OR in locked machines fails for three reasons:

1. The game builds the mask **twice in separate registers** (r5 for totals, r29 for selection) — hooking one misses the other.
2. Hooking between the two passes clobbers **f1** (the `HSD_Randf` result), corrupting selection.
3. If the only unlocked machine has **0 base spawn weight** in the current table entry, or is excluded by the 4-slot `prev_machine` history, the selection loop falls through to machine_kind 26 (out of range) → crash trying to load a nonexistent `.dat`.

### Implementation

The whole selection is replaced instead, adapted from KAR Deluxe's `Machines_AdjustSpawnChance`. Two `CODEPATCH_HOOKCREATE`s skip from the start of mask building to just past the selection result:

| Function | Hook address | Skip target | Description |
|----------|-------------|-------------|-------------|
| `CityMachineSpawn_DecideAndSpawn` | 0x801df00c | 0x801df220 | Normal spawns |
| `cityTrialSpawnFormationStar` | 0x801df44c | 0x801df630 | Formation event spawns |

At both hook points `r30` = `MachineSpawnData*` (moved to r3) and `f1` = match_progress; the epilogue moves the result from `r3` into `r31`, which the vanilla code past the skip target uses for the history write and `CityMachineSpawn_Create`.

`GateMachines_SelectSpawn(MachineSpawnData *msd, float match_progress)`:

1. Walks `spawn_desc[]` to the entry for the current match progress.
2. Copies that entry's `chance[VCKIND_NUM]` into a local float array.
3. Zeros `CT_SPAWN_EXCLUDED_MASK` machines and locked machines; gives every remaining unlocked machine with 0 base chance a fallback weight of **10**.
4. Counts `spawnable_count` (chance > 0). If **0**, returns `GetFirstUnlockedCTMachine()` immediately with no weighted roll — that fallback also skips `CT_SPAWN_EXCLUDED_MASK`, so an unlock state holding only Free/Steer Star (or transform forms) never leaks a TR-only machine onto the field.
5. Shrinks the history exclusion: `history_size = (spawnable_count <= 4) ? spawnable_count - 1 : 4`, i.e. `min(spawnable_count - 1, 4)` — this prevents the only spawnable machine from being excluded by its own spawn history.
6. Zeros candidates found in `msd->prev_machine_kind[]` within that reduced history.
7. Rolls a weighted selection with `HSD_Randf()` and returns the machine kind (defaulting to `VCKIND_COMPACT`).

## City Trial — Machine Select Screens

### Game system

`CitySelect_CreateMachineIcons` (0x8002e3c4) builds the icon grid and branches on `Gm_GetCityMode()`:

- **Mode 1 (Stadium)**: vanilla switches on ckind inline. CKINDs 0–14 are hardcoded available; 15 (Dragoon), 16 (Hydra), 17 (Flight Warp Star), 18 (King Dedede), 19 (Meta Knight), and ≥20 are hardcoded unavailable. **No checklist or unlock checks.**
- **Mode 2 (Free Run)**: vanilla switches on ckind. 0–14 and 17 are hardcoded available; 15/16/18/19 each map to a per-character reward index (30, 34, 35, 36) and call `ClearChecker_CheckUnlocked` (0x80049e24). ≥20 is skipped.

Both modes have a **counting pass** (total available count → r27) and an **array-building pass** (populates a 2×10 local array of CharacterKinds for the icon grid). Mode 0 (Trial) doesn't populate `machine_select.c_kind_arr` at all — there is no on-screen machine grid.

### Implementation

Both passes are replaced in both modes:

| Mode | Pass | Hook address | Skip target | C function |
|------|------|-------------|-------------|------------|
| 1 (Stadium) | Counting | 0x8002e4d0 | 0x8002e670 | `GateMachines_CountCTSelectAvailable` |
| 1 (Stadium) | Array-building | 0x8002e67c | 0x8002f0b8 | `GateMachines_BuildCTSelectArray` |
| 2 (Free Run) | Counting | 0x8002e5c0 | 0x8002e670 | `GateMachines_CountCTSelectAvailable` |
| 2 (Free Run) | Array-building | 0x8002e738 | 0x8002f0b8 | `GateMachines_BuildCTSelectArray` |

`GateMachines_CountCTSelectAvailable()` iterates `ckind` over `CKIND_NUM` (20) and returns the count of `IsCKindUnlocked(ckind)`; the epilogue moves it into r27. `GateMachines_BuildCTSelectArray(char_arr, row_counts)` walks the 2×10 icon grid via `SelIcon_GetCKind(row, col)` (0x8000b9bc) and **packs** the unlocked ckinds into the two per-row stack arrays (`char_arr` = r29, 20 bytes, row0 at +0 / row1 at +10; `row_counts` = r28), left-aligning each row with no gaps.

`IsCKindUnlocked` resolves the ckind's `CharacterDesc` (`Character_GetDesc`, 0x8000b9dc) → VCKIND via `CharacterDesc_GetMachineKind` and tests `machine_unlocked_mask`.

The array-building hooks skip all the way to the **flat-copy at `0x8002f0b8`** (`stb r27, 101(r30)` → `machine_select.num`, then the layout thunk), bypassing the vanilla reorder/balance block in between. That reorder was written around vanilla's grid iteration (special characters at fixed col 0/9 positions); packed arrays violate that assumption and triggered a duplicate-icon bug when only DEDEDE/METAKNIGHT were unlocked. The flat-copy reads `row_counts` + `char_arr` directly, so no reorder is needed.

These hooks fire during `CitySelect_CreateMachineIcons` (before `OnPlayerSelectLoad`), so `machine_select.c_kind_arr` (`GameData+0x236`) is already filtered by the time the player select scene is up.

### Navigation off-by-one fix

The CT machine-select grid has **two independent row-layout authorities** that only agree for the counts vanilla actually produces:

- **Rendering** is archive-animation-driven, not code. The flat-copy at `0x8002f0b8` writes `machine_select.num` (= total unlocked count, `GameData+0x235`) and creates `num` icons at flat indices `0..num-1`. The icon positions come from the selection-box JObj animation applied at **frame = count** (setup `_CitySelect_LayoutMachineIcons` 0x8015bd14 via thunk `CitySelect_LayoutMachineIcons` 0x801355f4), read back per slot into `slot.pos` (+0x60); `CitySelect_GetIconPos` (0x8015badc) just returns `slot_array[index].pos`. The 2×10 layout keeps **up to 10 icons on a single line** and only wraps to two rows at **11** (at `num==10` every slot shares one Y; at 11 it is 6/5; at 12 it is 6/6).
- **Navigation** (`CitySelect_Cursor1InputThink`, 0x800312fc) reads `machine_select.num` and at `0x80031350` does `cmpwi r3, 9; ble` → `num<=9` single-row (LEFT/RIGHT only), `num>=10` two-row (up/down enabled, split at `ceil(num/2)`).

So at exactly `num==10` the renderer draws one line of 10 while the cursor splits it **5+5** and up/down jumps between the halves. Vanilla CT only ever produces counts **15–20** (Free Run: ckinds 0–14+17 always available, 15/16/18/19 checklist-gated; Stadium: 0–14 = 15), so the off-by-one was never exercised. AP machine gating can land on exactly 10 unlocked machines, exposing it.

Fix: `CODEPATCH_REPLACEINSTRUCTION(0x80031350, 0x2c03000a)` patches the threshold to `cmpwi r3, 10` so `num<=10` is single-row, matching the renderer. The same nav function serves Stadium and Free Run, so one patch covers both. Air Ride uses a separate code path — `AirRide_PopulateSelectIcons` switches linear ↔ grid at `count < 10`, the same `<10` boundary as its nav, so it is internally consistent and does **not** share this off-by-one.

## City Trial — Starting Machine and Respawn

### Starting machine

`CitySelect_InitPlayerMachines` (0x8002ddd8) commits the per-slot starting machine for **every** City Trial mode. Its two branches both write `ply_icon_ckind[slot]` (city_select_ply +0x61, `GameData+0x231`) and merge at the convergence point `0x8002dea0` (`lbz r3, 97(r28)` → `Character_GetDesc`):

- **Trial** (`city_select_ply.x1d0 == 0`): vanilla hardcodes Compact for every slot. The free-roam start has no machine grid — nobody, human or CPU, picks.
- **Stadium / Free Run** (`x1d0 != 0`): vanilla sets `ckind = machine_select.c_kind_arr[icon[slot]]` from the gated grid. The player can roam the single cursor onto any CPU panel and pick that CPU's machine (the icon-grid write at `0x800315ac` in `CitySelect_Cursor1InputThink`); a CPU the player never touches is seeded a random gated machine by the vanilla loaders (`icon[slot] = HSD_Randi(machine_select.num)`).

A single `CODEPATCH_HOOKCREATE` at `0x8002dea0` (prologue `mr 3, 26` → slot; skip target 0 re-executes the clobbered `lbz r3, 97(r28)`, reloading the updated ckind) runs `GateMachines_FinalizeCTMachine(slot)` for each slot. `r26` = slot index and `r28` = `city_select_ply + slot` are both callee-saved, so they survive the C call.

`x215[slot]` is `0` = human, `2` = CPU, anything else = inactive (left untouched). CPU slots also get `ply_color[slot] = GateColors_RandomUnlockedColor()` here, independent of the machine toggle.

The **Random Start Machine** menu toggle (`ap_menu_settings.ct_random_start_machine`, default **On**) is the single master and applies **identically to humans and CPUs** wherever neither makes an explicit grid pick:

- **Trial**: the toggle drives every active slot the same way. On → `RandomUnlockedKirbyCKind()`; Off → Compact when unlocked, else `RandomUnlockedKirbyCKind()`.
- **Stadium / Free Run**: humans actively pick on the grid, so a human's selection is always kept. **CPU** slots the player did not pick a machine for follow the toggle — On → a random entry from the gated `c_kind_arr[0..num-1]`; Off → the vanilla seed. A CPU the player **explicitly** picked a machine for keeps that pick regardless of the toggle.

**Manual-pick tracking.** Because the engine random-seeds CPU `icon[slot]`, a CPU never starts at a clean sentinel and there is no built-in "the player chose this" flag, so the mod tracks it. A second `CODEPATCH_HOOKCREATE` at `0x800315ac` (`stb r27, 45(r30)` → `icon[slot]`, prologue `mr 3, 29` → slot; that store fires only when the chosen grid index actually changes, so it is the sole player-driven pick site) runs `GateMachines_NoteManualMachinePick(slot)`, setting bit `slot` in the static `ct_machine_manual_pick_mask`. `GateMachines_FinalizeCTMachine` consumes-and-clears that bit per slot (cleared every match, including for inactive slots, so it never leaks forward) and skips the CPU re-roll when it is set. Without this, the re-roll would clobber a machine the player deliberately assigned to a CPU.

`RandomUnlockedKirbyCKind()` picks a random unlocked CharacterKind for the free-roam Trial start but **excludes `CKIND_DEDEDE` and `CKIND_METAKNIGHT`**: their riders rely on rider-specific 3D HUD assets that vanilla's HUD loader short-circuits in Base CT (`major==CITY && cityMode==TRIAL`), so selecting them there would NULL-deref `3DHud_CreateSpeedometerInner` during scene init. It falls back to `CKIND_COMPACT` when no eligible Kirby-rider machine is unlocked. The Stadium/Free Run CPU path draws straight from `c_kind_arr` instead (Dedede/Meta Knight are valid in stadium contexts).

### Respawn machine

When a player respawns mid-match, `Rider_ResetStartingMachine` (0x80195288) puts them back on a machine — vanilla hardcodes `VCKIND_COMPACT` via two `Ply_Set*` calls. A `CODEPATCH_HOOKCREATE` at `0x801952c8` (inside the function, `r31` = `RiderData*`) redirects to `GateMachines_ResetStartingMachine()` and skips to the epilogue at `0x801952e0`. The vanilla prologue gating runs unmodified before the hook point, so the replacement only consumes `rd`.

`GateMachines_ResetStartingMachine()` first tries `rd->starting_machine_idx` (the per-rider intended starting machine). If that VCKIND is locked, it falls back to the lowest-index unlocked **CT-spawnable** VCKIND via `GetFirstUnlockedCTMachine()` (which skips `CT_SPAWN_EXCLUDED_MASK`) — deterministic per-rider, distinct from the CSS default-pick logic above. It then writes the result through `Ply_SetMachineIsBike` / `Ply_SetMachineKind`, converting to the bike-relative index for VCKINDs ≥ `VCKIND_WHEELNORMAL`. Without this hook a player could respawn on a locked machine; without the exclusion-mask skip, a sparse unlock state could respawn them on a Top Ride-only Free/Steer Star.

## Top Ride — Lobby Machine Select

### Game system

The TR lobby panel has a three-row in-panel menu: Player/CPU on top, **Control Type** in the middle (Free Star = 0, Steer Star = 1), and Handicap on the bottom (5 bars). Machine selection cycles via analog stick L/R on the middle row. The cycle target is `GameData.topride_select_ply.panel_machine[panel]` (`GameData+0x1c6`, lobby offset `+0x2f`) and is shared between human-configured and CPU-configured panels.

**The race lobby and the solo Free Run / Time Attack lobby are two distinct code paths** — they do *not* share a cycler. `TopRide_LobbyThink` (0x8002dd34) dispatches on `topride_select_ply.init_flag` (`GameData+0x198`): 0 → `TopRide_PreGameThink` (0x8002c06c, multiplayer race), nonzero → `TopRide_OnCourseSelect` (0x8002cc30, solo Free Run / Time Attack). Each has its own per-frame panel-editing think with its own copy of the L/R cycler:

- **Race**: `TopRide_PreGameThink` → `TopRide_CSS_PanelThink` (0x8002b8a8); cycler block 0x8002be44..0x8002be94 (outer "any L/R?" guard at 0x8002be2c).
- **Solo**: `TopRide_OnCourseSelect` → `TopRide_SoloPanelThink` (0x8002ca80, for `ply_state != 1`); cycler block 0x8002cb88..0x8002cbec.

Both cyclers read/write `panel_machine` at lobby offset `0x2f` and test the same RIGHT (`0x80002`) / LEFT (`0x40001`) edge bits, so a single gate function serves both hook sites. The init paths split the same way: `TopRide_LobbyInit` (0x8002dc9c) dispatches on `TopRide_GetMode()` (0x8003ea9c) to `TopRide_RaceInit` (0x8002d0ec) or `TopRide_SoloInit` (0x8002d9e8), while `TopRide_InitSelectData` (0x8002cfd8) is a third, earlier init called from `MainMenu_InitAllVariables` / `Gm_ResetAllData` / scene transitions.

### Implementation

Seven hooks cover the TR lobby surface:

| Function | Hook address | Skip target | Description |
|----------|-------------|-------------|-------------|
| `TopRide_InitSelectData` | 0x8002d070 | 0x8002d074 | Post-init fixup (main-menu reset): vanilla writes `panel_machine = 0` (Free); when Free is locked, override to the first unlocked TR machine for all 4 panels |
| `TopRide_RaceInit` | 0x8002d748 | (fall-through) | Post-reset fixup (TR Main Game / multiplayer race): vanilla's conditional reset block at 0x8002d6c4..0x8002d700 overwrites `panel_machine = 0` again, undoing InitSelectData's fixup; this hook re-applies the unlock-aware default — **CPU panels (`panel_pkind == 2`, lobby +0x1b) get a *random* unlocked control type plus a random unlocked color, human panels get the first unlocked machine**. This is the only fixup site that runs after `panel_pkind` is filled, so the CPU-random branch only fires here |
| `TopRide_SoloInit` | 0x8002db90 | (fall-through) | Post-init fixup (Free Run / Time Attack): same as above for the solo flow, which hardcodes `panel_machine = 0` at 0x8002db70..0x8002db88 |
| `TopRide_CSS_PanelThink` | 0x8002be44 | 0x8002c054 / 0x8002be98 | L/R cycler gate (**race lobby**): replaces the entire `lbz`/`stb` cycle block + post-write compare; skips writes that would land on a locked machine. Conditional: 0 (no change → function end) or 1 (changed → SFX + UI update) |
| `TopRide_SoloPanelThink` | 0x8002cb98 | 0x8002cc18 / 0x8002cbf0 | L/R cycler gate (**solo Free Run / Time Attack**): solo counterpart reusing `GateMachines_CycleTRMachine`. Without it, solo had no unlock check on the Control Type row |
| `TopRide_PreGameThink` | 0x8002c52c | (fall-through) / 0x8002c878 | Start-match gate (race): blocks the menu confirm + commit-and-launch sequence when neither `VCKIND_FREE` nor `VCKIND_STEER` is in `machine_unlocked_mask`. Conditional |
| `TopRide_OnCourseSelect` | 0x8002cc80 | (fall-through) / 0x8002cddc | Start-match gate (solo): same condition, for the Free Run / Time Attack launch path |

Register and placement constraints behind those choices:

- **InitSelectData** hook lands at `li r0, 0x1` (the first instruction after the per-slot init loop) — `0x8002d06c` (the original convergence point) is already claimed by `gate_colors.c`'s `GateColors_ValidateTopRideColors`. The epilogue restores **`r3 = 0`** (clobbered by the C call but required by the three following `stb r3, {6,2,3}(r31)` lobby-flag clears) before the framework re-executes the clobbered `li r0, 1`. Without the `r3` restore, `active_pad_mask` / `x199` / `x19a` get written with garbage on first entry and the panel UI fails to render until the next scene entry.
- **RaceInit** hook (0x8002d748) deliberately skips past the `panel_pkind` CPU-fill loop at 0x8002d710..0x8002d744 rather than landing right after the `panel_machine` reset; landing inside that loop would clobber its caller-saved iterator `r7`. Hooking at the post-loop `bl gmGetGlobalP` is clean — the framework's auto-re-execution of the `bl` reloads `r3 = GameData*` for the following `addi r6, r3, 407`, so no epilogue is needed. Nothing between the reset block and the hook site reads `panel_machine[]`.
- **SoloInit** hook lands one instruction *after* `gate_colors.c`'s parallel solo color fixup at 0x8002db8c, so it fires after that hook's clobber-re-execution sets `r28 = 0`. The framework re-executes the clobbered `add r30, r31, r28`, leaving the per-slot loop's base register correct without an explicit epilogue.
- **Race cycler** hook lands at `lbz r4, 0x2f(r26)` (start of the cycle block, after the outer "any L/R input?" guard at 0x8002be2c). At entry `r26` = panel base, `r29` = direction-edge bits. `GateMachines_CycleTRMachine` reads the current value, computes the gated next value, writes it back if changed, and returns 1/0. On return-1 the function falls through to the change-SFX + icon-update block at 0x8002be98; on return-0 it skips to function exit at 0x8002c054, matching vanilla's `beq` at 0x8002be94.
- **Solo cycler** hook lands at 0x8002cb98 (`and. r0, r26, r0`, the RIGHT-bit test) — one instruction after the cycler computes `r29` = panel index and `r30` = lobby + panel (0x8002cb8c / 0x8002cb94), and after the outer 0xC0003 L/R guard at 0x8002cb80. At entry `r30` = panel base, `r26` = direction-edge bits. Both are callee-saved and set *before* the hook, so the downstream SFX + UI block at 0x8002cbf0 finds them intact after the C call and no epilogue is needed. On return-0 it exits to 0x8002cc18, matching vanilla's `beq` at 0x8002cbec.
- **Both start-match gates** sit at the first instruction of their respective start-match bodies (the `bl 0x80061658` menu-confirm SFX call). The preceding `andi.` against pad bit `0x1000` and `cmpwi ply_state, 1` (race) / `lbz is_all_ready` + `andi. 0x1000` (solo) already constrain the sites to "a Ready panel pressed Start"; the gate additionally requires some TR machine to be unlocked. When both Free and Steer are locked the L/R cycler keeps `panel_machine[slot]` at the locked default, so without these gates a player could still press Start and the commit loop would call `TopRide_SetMachineKind` with a locked VCKIND. With them, pressing Start is a no-op (no sound, no commit) until at least one TR machine is unlocked.
- **The race gate needs explicit register preservation** the solo gate does not. Its hook sits *inside* `TopRide_PreGameThink`'s 4-slot scan loop, and the block path returns to 0x8002c878, which loops back to 0x8002c4fc and recomputes `r3 = r4 + r5*68` (`r4` = slot-array base 0x8058b634, set once before the loop; `r5` = slot index). Both are caller-saved and live across the whole loop, but the hoshi codepatch trampoline saves no registers around its `bl`. Because `GateMachines_TRLobbyCanStart` calls the SFX + textbox helpers, those volatiles get clobbered and the loop continuation would fault on a garbage base — so the prologue stashes `r4`/`r5` on a scratch frame and the epilogue restores them on both paths. The solo gate (0x8002cc80, `TopRide_OnCourseSelect`) has no enclosing loop and needs no save.

## Air Ride — Select Screen

The vanilla select screen system, grid layout, icon animation pipeline, `CharacterDesc` table, and `MnSelplyAll` archive structure are documented in `css-system.md`; only the gating patches are covered here.

Two `CODEPATCH_REPLACEFUNC`s:

1. **`AirRide_CheckCharacterAvailable` (0x8002090c) → `GateMachines_CheckAirRideCharacterAvailable`** — gates the select screen icon grid. Takes a CharacterKind, returns 1/0. `CKIND_DRAGOON`, `CKIND_HYDRA`, and `CKIND_FLIGHT` always return 0 (City Trial-only); everything else resolves through `Character_GetDesc` + `CharacterDesc_GetMachineKind()` to the actual VCKIND and tests `machine_unlocked_mask`. Vanilla instead maps each ckind to a checklist reward index, makes only Warp Star available by default, and hardcodes Compact Star unavailable (`case 0: return 0`).
2. **`TitleScreen_CheckMachineUnlocked` (0x8000c364) → `GateMachines_CheckTitleDemoMachineUnlocked`** — gates the **title-screen attract-demo** machine pick (`TitleScreen_SelectRandomMachine` 0x8000daa0, reachable only via `TitleScreen_MinorExit` → `TitleScreen_SetupDemoMachines`). Takes `machine_class` (= `CharacterDesc.is_bike`) and `machine_id` (= `CharacterDesc.machine_kind`, a bike-relative index for bikes, not the absolute VCKIND), converts to the actual VCKIND (`VCKIND_WHEELNORMAL + machine_id` when `machine_class` is set), range-checks it, then tests the mask. This does **not** run for CPUs in real Air Ride races.

**Real in-game CPU machine pick.** The actual Air Ride CPU machine is chosen in `loadCPU` (0x80023600) and its sibling setup paths, which index a random entry out of the available-character list that `AirRide_PopulateSelectIcons` builds through the replaced `AirRide_CheckCharacterAvailable`. CPUs therefore already draw a random **unlocked** machine with no separate machine-pick hook. CPU color is gated on an independent path (`gate-colors.md`).

**Stale-list clear (`GateMachines_ClearAirRideList`).** The select struct caches its available-machine list at `airride_select_ply +0x66` (the 2×10 = 20-entry icon grid). `AirRide_PopulateSelectIcons` (0x80020a08) runs **every CSS frame** (called unconditionally at 0x8002896c in `CSS_airRide_RaceUpdate` and 0x80029c74 in `CSS_airRide_FreeTimeUpdate`), but it only (re)writes the first `count` entries and never clears the tail — only the once-per-entry `CSS_airRide_InitSelectData` memset zeroes the whole region. So when `machine_unlocked_mask` is narrowed mid-session (e.g. the debug menu locks machines while sitting in the CSS), `count` drops but stale entries from an earlier fill linger past the new count. Every slot's icon index (+0x2d) defaults to 0 and the CSS resolves the displayed **and committed** machine as `list[icon]`, so a stale `list[0]` drives the whole lobby — and the subsequent race — onto a vehicle that is no longer unlocked. Fix: a `CODEPATCH_HOOKCREATE` at 0x80020a88 (`lbz r0, 123(r31)`, just after `r31 = airride_select_ply` is set up and before the rebuild) zeroes `list[0..19]` each frame, so populate refills `[0..count-1]` and the tail stays 0 (→ `CKIND_COMPACT`); the epilogue restores `r4 = 0` for the following `stb r4,9(r1)`. Because populate runs per-frame, the lobby self-heals the next frame rather than needing a full CSS re-entry. `ply_icon_ckind +0x61` is **not** the rendered/committed field — `list[icon]` is — so clearing the list, not clamping +0x61, is the correct fix.

## Legendary Machine Delivery

`GateMachines_GiveLegendaryMachine` is **not** a gate — it is the delivery mechanism for the AP "give legendary" items `AP_ITEM_GIVE_DRAGOON` (→ `machine_index` 0) and `AP_ITEM_GIVE_HYDRA` (→ 1), dispatched from `ap_item_handler.c`. It hands a player the assembled legendary machine via the vanilla assembly cinematic, bypassing three-part field collection entirely, and does **not** consult `machine_unlocked_mask` — receiving the item is itself the grant.

`GateMachines_GiveLegendaryMachine(int machine_index)`:

1. Returns 0 if `!Gm_IsInCity()`.
2. Returns 0 if this machine's bit is already set in the static `legendary_assembled_mask` (bit 0 = Dragoon, bit 1 = Hydra), which `GateMachines_On3DLoadEnd()` clears at every scene load. The piece archives (`VsDragoon.dat` / `VsHydra.dat`) are freed when the cinematic finishes, so a second cinematic in the same scene loads a dangling joint and crashes in `HSD_JObjLoadJoint`.
3. Returns 0 if `Gm_IsLegendaryAssembling()` (0x8000c934) — a second concurrent cinematic (GObj at `GameData+0xA8C`) tears down the running one's piece GObjs and leaves a dangling jobj that crashes on the next update.
4. For each of the 5 players that is `PKIND_HMN` and has a machine `GOBJ`, reads its `MachineData` and fills a `LegendaryAssemblyParams { machine_index, ply, pos, up, forward }` (pos/up/forward copied from the rider's current `MachineData`), then calls `LegendaryMachine_StartAssembly(&params)` (0x80283cf0).
5. Returns 1 if assembly started for at least one human player (and records the bit), else 0.

The *availability* gating of legendary machines — suppressing the natural three-part assembly when Dragoon/Hydra pieces are locked — lives in `gate-items.md`, plus the CSS and spawn unlock checks above.

## Save Data

`u32 machine_unlocked_mask` in `APSave` (`main.h`, accessed via the global `ap_save`) — bit N = `MachineKind` N.

The mask is exposed through `ArchipelagoAPI` as `AP_UNLOCK_MACHINE`. When the slot option `machine_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` in `main.c` pre-fills the mask with `(1u << VCKIND_NUM) - 1` at connect.

## AP Items

AP item ID = `AP_MACHINE_UNLOCK_BASE` (830, `archipelago_api.h`) + `MachineKind`. The handler in `ap_item_handler.c` accepts IDs `830..854` — the bound is `< AP_MACHINE_UNLOCK_BASE + VCKIND_WHEELVSDEDEDE`, i.e. VCKINDs 0–24 — then calls `GateMachines_UnlockMachine(kind, /*announce=*/1)`. The mod accepts all 25 in-range IDs defensively even though the apworld generates fewer.

`GateMachines_UnlockMachine` sets the bit, logs, and (when announcing) enqueues `"Unlocked Machine: <MachineKind_Names[kind]>"` with `tb_api->MachineColor`. `VCKIND_WHEELDEDEDE` and `VCKIND_WINGMETAKNIGHT` instead announce `"Unlocked Character: King Dedede"` / `"Unlocked Character: Meta Knight"`, matching the checklist reward path. `checklist_rewards.c` also calls this entry point with `announce = 0` for the machine rewards.

**IDs the apworld does not generate** (`worlds/kirby_air_ride/KARItems.py`): 847 `WINGKIRBY`, 849 `WHEELNORMAL`, 850 `WHEELKIRBY` — transformation/ability-state forms with no readers anywhere, so granting them would be a genuine no-op — and 855 `WHEELVSDEDEDE`, which is out of the handler range entirely. The remaining 22 IDs are all generated as `progression`.

**845 FREE / 846 STEER are not no-ops.** They are the two Top Ride control-type machines (`VCKIND_FREE`, `VCKIND_STEER`); their bits are read by the Top Ride lobby gating in this file (`IsTRMachineUnlocked`, `GateMachines_TRLobbyCanStart`). They never spawn on the CT field (force-zeroed via `CT_SPAWN_EXCLUDED_MASK`) and no character rides them in the 3D modes, but setting either bit unlocks the TR lobby cyclers and the start-match gate. The apworld tags both `_TR` so they only land in Top Ride locations.

**`VCKIND_WHEELVSDEDEDE`** (= 25, would be ID 855) has no readers at all: no `CharacterDesc` references it, no CT spawn path includes it (`CT_SPAWN_EXCLUDED_MASK` pins it to 0 chance defensively), no CSS lists it, and the Vs. King Dedede stadium's availability check uses the stadium mask, not the machine mask. ID 855 falls through to the unknown-item path.

**The canonical Dedede unlock is 854 `WHEELDEDEDE`** (`VCKIND_WHEELDEDEDE` = 24), which is what `CharacterDesc[CKIND_DEDEDE]` resolves to via `CharacterDesc_GetMachineKind` (is_bike=1, machine_kind=5 → 19+5=24). `REWARD_KING_DEDEDE` in the checklist reward path also unlocks only this bit.

## Design Decisions

**Full selection replacement over exclusion mask:** the exclusion mask approach is attractive (minimal code change) but fundamentally broken — the game uses separate registers for the two passes, and hooking between them clobbers the random result. The KAR Deluxe full-replacement approach is more code but provably correct.

**Base chance of 10 for zero-weight unlocked machines:** without this, machines with 0 spawn weight in the current time window (e.g. Hydra early in a match) can never spawn even when unlocked. The fallback ensures every unlocked machine has some possibility of appearing, which matters for AP progression.

**History size reduction:** `min(spawnable_count - 1, 4)` prevents deadlock when few machines are spawnable. With only 2 spawnable machines and history size 4, both would be excluded.

**Shared mask across modes:** one `machine_unlocked_mask` covers City Trial, Air Ride, **and** Top Ride (Free/Steer). Unlocking a machine once makes it available everywhere. This keeps the AP item pool to one item per machine instead of one per mode, and matches player expectation.

## Known Limitations

**Legendary delivery is City Trial only.** The `Gm_IsInCity()` guard exists because the assembly cinematic loads legendary piece models and drives the CT sky/area-light setup, which only exist on the open City Trial map — running it in a stadium or in Air Ride / Top Ride dereferences a null jobj or hits the area-light assert. `AP_ITEM_GIVE_DRAGOON` / `AP_ITEM_GIVE_HYDRA` received elsewhere return 0, so the unprocessed-items list retries them until the player enters City Trial.

**Compact Star has no Air Ride select icon.** Vanilla `AirRide_CheckCharacterAvailable` hardcodes `case 0: return 0` — Compact Star was never meant to appear on the Air Ride select screen. The replacement allows it when unlocked, but the icon material animation in the `MnSelplyAll` archive uses the CharacterKind value as the animation frame and frame 0 likely has no valid texture, so the entry exists and is selectable/functional but renders no icon. Resolving it needs the frame-0 state along the MatAnimJoint child → MatAnim → TexAnim chain (structure documented in `css-system.md`); if blank, the options are patching the archive `.dat` or a runtime texture swap from the City Trial archive (`MnSelplyctAll.dat`).
