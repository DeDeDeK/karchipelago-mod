# Machine Gating

## Overview

26 MachineKind/VCKIND values (`VCKIND_NUM` = 26, see `machine.h`), one bit per machine in `machine_unlocked_mask` (a `u32` in `APSave`). The AP world generates an unlock for all 25 in-range machines (IDs 830–854); only VCKIND 25 (WHEELVSDEDEDE) is excluded (see [AP Items](#ap-items)). Unlocking a machine makes it available across **City Trial, Air Ride, and Top Ride** (Free Star / Steer Star) — the mask is shared (`gate_machines.c` comment at `IsTRMachineUnlocked`).

## City Trial — Field Spawning

### Game System

Machine spawning in City Trial is chance-based. The game reads a spawn table (`(*stc_vcDataCommon)->spawn_data->spawn_desc[]`; `stc_vcDataCommon` is `vcDataCommon**` at r13+0x758, the `spawn_data` sub-struct sits at `vcDataCommon+0x20`) indexed by match progress, with `float chance[VCKIND_NUM]` per entry. Some machines have 0 weight at certain match progress points (e.g., Hydra/Dragoon early in a match).

The vanilla functions `CityMachineSpawn_DecideAndSpawn` (0x801defac) and `cityTrialSpawnFormationStar` (0x801df408) build a `u32` exclusion bitmask from `MachineSpawnData.prev_machine_kind[4]` (recently spawned machines), then do a two-pass weighted random selection:
- **Pass 1** (r5): computes total spawn chance sum
- **Pass 2** (r29): performs the actual selection via `HSD_Randf`

### Why Exclusion Mask Hooking Doesn't Work

Hooking the exclusion bitmask to OR in locked machine bits fails for three reasons:

1. The game builds the mask **twice in separate registers** (r5 for totals, r29 for selection) — hooking one misses the other
2. Hooking between the two passes clobbers **f1** (the `HSD_Randf` result), corrupting selection
3. If the only unlocked machine has **0 base spawn weight** in the current table entry, or is excluded by the 4-slot `prev_machine` history, the selection loop falls through to machine_kind 26 (out of range) → crash trying to load nonexistent .dat file

### Implemented Approach — Full Spawn Selection Replacement

Adapted from [KAR Deluxe](https://github.com/UnclePunch/KAR-Deluxe/blob/main/mods/city_settings/src/patches/machines.c) `Machines_AdjustSpawnChance`. `CODEPATCH_HOOKCREATE` replaces the entire selection logic, skipping from the start of mask building to after the selection result:

| Function | Hook Address | Skip Target | Description |
|----------|-------------|-------------|-------------|
| `CityMachineSpawn_DecideAndSpawn` | 0x801df00c | 0x801df220 | Normal spawns |
| `cityTrialSpawnFormationStar` | 0x801df44c | 0x801df630 | Formation event spawns |

At both hook points: `r30` = `MachineSpawnData*`, `f1` = match_progress.
Epilogue: result in `r3` → `r31` (machine_kind for subsequent code).

`GateMachines_SelectSpawn(MachineSpawnData *msd, float match_progress)`:
1. Finds spawn table entry for current match progress
2. Makes local copy of spawn chances
3. Zeros locked machines and CT-excluded machines (Top Ride forms, transformation kinds — see `CT_SPAWN_EXCLUDED_MASK`); gives remaining unlocked machines with 0 base chance a fallback weight of 10
4. Counts `spawnable_count` (machines with chance > 0); if **0**, returns `GetFirstUnlockedCTMachine()` immediately (no weighted roll) — this fallback also skips `CT_SPAWN_EXCLUDED_MASK`, so a unlock state with only Free/Steer Star (or transform forms) never leaks a TR-only machine onto the field
5. Reduces history exclusion size: `history_size = (spawnable_count <= 4) ? spawnable_count - 1 : 4` (i.e. `min(spawnable_count - 1, 4)`) — prevents the only spawnable machine from being excluded by its own spawn history
6. Removes recently spawned machines from candidates (using reduced history, scanning `msd->prev_machine_kind[]`)
7. Weighted random selection from remaining candidates via `HSD_Randf()`
8. Returns machine_kind; vanilla code at the skip target handles history buffer writes

## City Trial — Stadium & Free Run Select Screens

`CitySelect_CreateMachineIcons` (0x8002e3c4) builds the icon grid for the machine select screen. It branches on `Gm_GetCityMode()`:

- **Mode 1 (Stadium)**: Vanilla switches on ckind inline. CKINDs 0–14 are hardcoded available; 15 (Dragoon), 16 (Hydra), 17 (Flight Warp Star), 18 (King Dedede), 19 (Meta Knight), and ≥20 are hardcoded unavailable. **No checklist or unlock checks.**
- **Mode 2 (Free Run)**: Vanilla switches on ckind. 0–14 and 17 (Flight Warp Star) are hardcoded available; 15/16/18/19 each map to a per-character reward index (30, 34, 35, 36) and call `ClearChecker_CheckUnlocked` (0x80049e24). ≥20 is skipped.

Both modes have a **counting pass** (determines total available count → r27) and an **array-building pass** (populates a 2×10 local array of CharacterKinds for the icon grid). Both are replaced:

- `GateMachines_CountCTSelectAvailable()` iterates `ckind` over `CKIND_NUM` (20) and returns the count of `IsCKindUnlocked(ckind)`. Result lands in `r27` (total count).
- `GateMachines_BuildCTSelectArray(char_arr, row_counts)` walks the 2×10 icon grid via `SelIcon_GetCKind(row, col)` and **packs** the unlocked ckinds into the two per-row stack arrays (`char_arr` = local_41 at r29, 20 bytes: row0 at +0 / row1 at +10; `row_counts` = local_48 at r28). Packing left-aligns each row, leaving no gaps.

`IsCKindUnlocked` resolves the ckind's `CharacterDesc` → VCKIND via `CharacterDesc_GetMachineKind` and tests `machine_unlocked_mask`.

### Hook Addresses

| Mode | Pass | Hook Address | Skip Target | C Function |
|------|------|-------------|-------------|------------|
| 1 (Stadium) | Counting | 0x8002e4d0 | 0x8002e670 | `GateMachines_CountCTSelectAvailable` |
| 1 (Stadium) | Array-building | 0x8002e67c | 0x8002f0b8 | `GateMachines_BuildCTSelectArray` |
| 2 (Free Run) | Counting | 0x8002e5c0 | 0x8002e670 | `GateMachines_CountCTSelectAvailable` |
| 2 (Free Run) | Array-building | 0x8002e738 | 0x8002f0b8 | `GateMachines_BuildCTSelectArray` |

The array-building hooks skip all the way to the **flat-copy at `0x8002f0b8`** (`stb r27, 101(r30)` → `machine_select.num`, then the layout thunk), bypassing the vanilla reorder/balance block in between. That reorder was written around vanilla's grid iteration (special characters at fixed col 0/9 positions); our packed arrays violate that assumption and triggered a duplicate-icon bug when only DEDEDE/METAKNIGHT were unlocked. The flat-copy reads `row_counts` + `char_arr` directly, so no reorder is needed.

These hooks fire during `CitySelect_CreateMachineIcons` (before `OnPlayerSelectLoad`), so by the time the player select scene is up, `machine_select.c_kind_arr` is already filtered. Mode 0 (Trial) doesn't populate `machine_select.c_kind_arr` at all — the starting machine is set by the `0x8002dea0` convergence hook (see below) and there's no on-screen machine grid to filter.

### Navigation off-by-one fix (`CODEPATCH_REPLACEINSTRUCTION 0x80031350`)

The CT machine-select grid has **two independent row-layout authorities** that only agree for the counts vanilla actually produces:

- **Rendering** is archive-animation-driven, not code. The flat-copy at `0x8002f0b8` writes `machine_select.num` (= total unlocked count, `GameData+0x235`) and creates `num` icons at flat indices `0..num-1`. The icon positions come from the selection-box JObj animation applied at **frame = count** (setup `_CitySelect_LayoutMachineIcons` (0x8015bd14) via thunk `CitySelect_LayoutMachineIcons` (0x801355f4)), read back per slot into `slot.pos` (`+0x60`); `CitySelect_GetIconPos` (0x8015badc) just returns `slot_array[index].pos`. The 2×10 layout keeps **up to 10 icons on a single line** and only wraps to two rows at **11** (at `num==10` every slot shares one Y; at 11 it is 6/5; at 12 it is 6/6).
- **Navigation** (`CitySelect_Cursor1InputThink`, 0x800312fc) reads `machine_select.num` and at `0x80031350` does `cmpwi r3, 9; ble` → `num<=9` single-row (LEFT/RIGHT only), `num>=10` two-row (up/down enabled, split at `ceil(num/2)`).

So at exactly `num==10` the renderer draws one line of 10 while the cursor splits it **5+5** and up/down jumps between the halves — the reported "single line that scrolls to the middle, then up/down to the other half" bug. Vanilla CT only ever produces counts **15–20** (Free Run: ckinds 0–14+17 always available, 15/16/18/19 checklist-gated; Stadium: 0–14 = 15), so the off-by-one was never exercised. AP machine gating can land on exactly 10 unlocked machines, exposing it (intermittent — depends purely on how many machines are unlocked on entry).

Fix: `CODEPATCH_REPLACEINSTRUCTION(0x80031350, 0x2c03000a)` patches the threshold to `cmpwi r3, 10` so `num<=10` is single-row, matching the renderer. The same nav function serves Stadium and Free Run, so one patch covers both. (Air Ride uses a separate code path — `AirRide_PopulateSelectIcons` switches linear↔grid at `count < 10`, the same `<10` boundary as its nav, so it is internally consistent and does **not** share this off-by-one.)

## City Trial — Starting Machine & Respawn

### Starting Machine (`CitySelect_InitPlayerMachines`, entry 0x8002ddd8)

`CitySelect_InitPlayerMachines` commits the per-slot starting machine for **every** City Trial mode. Its two branches both write `ply_icon_ckind[slot]` (`+0x61`) and merge at the convergence point `0x8002dea0` (`lbz r3, 97(r28)` → `Character_GetDesc`):

- **Trial** (`city_select_ply.x1d0 == 0`): vanilla hardcodes Compact for every slot. The free-roam start has no machine grid — nobody (human or CPU) picks.
- **Stadium / Free Run** (`x1d0 != 0`): vanilla sets `ckind = machine_select.c_kind_arr[icon[slot]]` from the gated grid. No CT select function calls `HSD_Randi`, so a CPU's `icon[slot]` stays at its default (~0) — without intervention every CPU collapses to the first unlocked machine.

A single `CODEPATCH_HOOKCREATE` at `0x8002dea0` (prologue `mr 3, 26` → slot; skip target 0 re-executes the clobbered `lbz r3, 97(r28)`, reloading the updated ckind) runs `GateMachines_FinalizeCTMachine(slot)` for each active slot. `r26` = slot index and `r28` = `city_select_ply + slot` are both callee-saved, so they survive the C call.

The **Random Start Machine** menu toggle (`ap_menu_settings.ct_random_start_machine`, default **On**) is the single master and applies **identically to humans and CPUs** wherever neither makes an explicit grid pick:

- `x215[slot]`: `0` = human, `2` = CPU, else inactive (left untouched).
- **Trial**: toggle drives every active slot the same way. On → `RandomUnlockedKirbyCKind()`; Off → Compact when unlocked, else `RandomUnlockedKirbyCKind()`.
- **Stadium / Free Run**: humans actively pick on the grid, so a human's selection is always kept. Only auto-assigned **CPU** slots follow the toggle — On → a random entry from the gated `c_kind_arr[0..num-1]`; Off → the vanilla first-unlocked default.

`RandomUnlockedKirbyCKind()` picks a random unlocked CharacterKind for the free-roam Trial start but **excludes `CKIND_DEDEDE` and `CKIND_METAKNIGHT`**: their riders rely on rider-specific 3D HUD assets that vanilla's HUD loader short-circuits in Base CT (`major==CITY && cityMode==TRIAL`), so selecting them there would NULL-deref `3DHud_CreateSpeedometerInner` during scene init. It falls back to `CKIND_COMPACT` when no eligible Kirby-rider machine is unlocked. The Stadium/Free Run CPU path draws straight from `c_kind_arr` instead (Dedede/Meta Knight are valid in stadium contexts).

### Respawn Machine (`Rider_ResetStartingMachine`, entry 0x80195288)

When a player respawns mid-match, `Rider_ResetStartingMachine` (entry `0x80195288`) resets them to their starting machine. A `CODEPATCH_HOOKCREATE` at `0x801952c8` (inside the function, with `r31` = `RiderData*`) redirects this to `GateMachines_ResetStartingMachine()` and skips to the epilogue at `0x801952e0`. The vanilla prologue gating runs unmodified before the hook point, so the replacement only consumes `rd`.

`GateMachines_ResetStartingMachine()` first tries `rd->starting_machine_idx` (the per-rider intended starting machine). If that VCKIND is locked, it falls back to the lowest-index unlocked **CT-spawnable** VCKIND via `GetFirstUnlockedCTMachine()` (which skips `CT_SPAWN_EXCLUDED_MASK`) — deterministic per-rider, distinct from the CSS default-pick logic above. Without this hook, a player could respawn on a locked machine if the vanilla starting machine was locked; without the exclusion-mask skip, a sparse unlock state could respawn them on a Top Ride-only Free/Steer Star.

## Top Ride — Lobby Machine Select

The TR lobby panel has a three-row in-panel menu: Player/CPU on top, **Control Type** in the middle (Free Star = 0, Steer Star = 1), and Handicap on the bottom (5 bars). Machine selection cycles via analog stick L/R on the middle row. The cycle target is `GameData.topride_select_ply.panel_machine[panel]` and is shared between human-configured and CPU-configured panels.

**The race lobby and the solo Free Run / Time Attack lobby are two distinct code paths** — they do *not* share a cycler. `TopRide_LobbyThink` (dispatch on `puVar[0x198]`) routes to `TopRide_PreGameThink` for the multiplayer race (`TopRide_GetMode() == 0`) and to `TopRide_OnCourseSelect` for solo Free Run / Time Attack. Each has its own per-frame panel-editing think with its own copy of the L/R cycler:

- **Race**: `TopRide_PreGameThink` → `TopRide_CSS_PanelThink` (0x8002b8a8); cycler block 0x8002be44..0x8002be94 (outer "any L/R?" guard at 0x8002be2c).
- **Solo**: `TopRide_OnCourseSelect` → `TopRide_SoloPanelThink` (0x8002ca80, for `ply_state != 1`); cycler block 0x8002cb88..0x8002cbec.

Both cyclers read/write `panel_machine` at lobby offset `0x2f` and test the same RIGHT (`0x80002`) / LEFT (`0x40001`) edge bits, so a single gate function (`GateMachines_CycleTRMachine`) serves both hook sites. The init and start-match paths likewise split: `TopRide_RaceInit` / `TopRide_PreGameThink` (race) vs. `TopRide_SoloInit` / `TopRide_OnCourseSelect` (solo). `TopRide_InitSelectData` is a third init path called from `MainMenu_InitAllVariables` / `Gm_ResetAllData` / scene transitions — earlier than the two lobby-specific inits. `TopRide_LobbyInit` dispatches between `TopRide_RaceInit` and `TopRide_SoloInit` based on `TopRide_GetMode()`.

Seven hooks cover the TR lobby surface:

| Function | Hook Address | Skip Target | Description |
|----------|-------------|-------------|-------------|
| `TopRide_InitSelectData` | 0x8002d070 | 0x8002d074 | Post-init fixup (main-menu reset): vanilla writes panel_machine = 0 (Free); when Free is locked, override to the first unlocked TR machine for all 4 panels |
| `TopRide_RaceInit` | 0x8002d748 | (fall-through) | Post-reset fixup (TR Main Game / multiplayer race): vanilla's conditional reset block at 0x8002d6c4..0x8002d700 overwrites panel_machine = 0 again, undoing InitSelectData's fixup; this hook re-applies the unlock-aware default — **CPU panels (`panel_pkind == 2`) get a *random* unlocked control type, human panels get the first unlocked machine**. This is the only fixup site that runs after `panel_pkind` is filled, so the CPU-random branch only fires here |
| `TopRide_SoloInit` | 0x8002db90 | (fall-through) | Post-init fixup (Free Run / Time Attack): same as above for the solo flow, which uses a separate init function that hardcodes panel_machine = 0 at 0x8002db70..0x8002db88 |
| `TopRide_CSS_PanelThink` | 0x8002be44 | 0x8002c054 / 0x8002be98 | L/R cycler gate (**race lobby**): replaces the entire `lbz`/`stb` cycle block + post-write compare; skips writes that would land on a locked machine. Conditional: returns 0 (no change → function end) or 1 (changed → SFX + UI update) |
| `TopRide_SoloPanelThink` | 0x8002cb98 | 0x8002cc18 / 0x8002cbf0 | L/R cycler gate (**solo Free Run / Time Attack**): solo counterpart to the race cycler above, reusing `GateMachines_CycleTRMachine`. Without it, solo had no unlock check on the Control Type row. Conditional: returns 0 (no change → function end 0x8002cc18) or 1 (changed → SFX + UI update 0x8002cbf0) |
| `TopRide_PreGameThink` | 0x8002c52c | (fall-through) / 0x8002c878 | Start-match gate (race): blocks the menu confirm + commit-and-launch sequence when neither `VCKIND_FREE` nor `VCKIND_STEER` is in `machine_unlocked_mask`. Conditional |
| `TopRide_OnCourseSelect` | 0x8002cc80 | (fall-through) / 0x8002cddc | Start-match gate (solo): same condition as above, for the Free Run / Time Attack launch path |

The InitSelectData hook lands at `li r0, 0x1` (the first instruction after the per-slot init loop) — `0x8002d06c` (the original convergence point) is already claimed by `gate_colors.c`'s `GateColors_ValidateTopRideColors`. The epilogue restores **`r3 = 0`** (clobbered by the C call but required by the three following `stb r3, {6,2,3}(r31)` lobby-flag clears) before the hook framework re-executes the clobbered `li r0, 1`. Without the `r3` restore, the lobby's `active_pad_mask` / `x199` / `x19a` get written with garbage on first entry and the panel UI fails to render until the next scene entry.

The RaceInit hook (`0x8002d748`) deliberately skips past the panel_pkind CPU-fill loop at `0x8002d710..0x8002d744` rather than landing immediately after the `panel_machine` reset at `0x8002d6c4..0x8002d700`. Landing inside that loop would clobber its caller-saved iterator `r7`. Hooking at the post-loop `bl gmGetGlobalP` is clean: the framework's auto-re-execution of the `bl` naturally reloads `r3 = GameData*` for the subsequent `addi r6, r3, 407`, so no epilogue is needed. Nothing between the reset block and the hook site reads `panel_machine[]`, so the fixup window is intact.

The SoloInit hook lands one instruction *after* `gate_colors.c`'s parallel solo color fixup at 0x8002db8c, so it fires after that hook's clobber-re-execution sets `r28 = 0`. The hook framework re-executes the clobbered `add r30, r31, r28`, leaving the per-slot loop's base register correct without an explicit epilogue.

The **race** cycler hook lands at `lbz r4, 0x2f(r26)` (the start of the cycle block, after the outer "any L/R input?" guard at 0x8002be2c). At entry: `r26` = panel base, `r29` = direction-edge bits (`0x80002` = RIGHT, `0x40001` = LEFT). `GateMachines_CycleTRMachine` reads the current value, computes the gated next value, writes it back if changed, and returns 1/0. On return-1 the function falls through to the change-SFX + icon-update block at 0x8002be98; on return-0 it skips straight to function exit at 0x8002c054, matching vanilla's `beq` behavior at 0x8002be94.

The **solo** cycler hook lands at 0x8002cb98 (`and. r0, r26, r0`, the RIGHT-bit test) — one instruction after the cycler computes `r29` = panel index and `r30` = lobby + panel (0x8002cb8c / 0x8002cb94), and after the outer 0xC0003 L/R guard at 0x8002cb80. At entry: `r30` = panel base, `r26` = direction-edge bits (same `0x80002` / `0x40001` values as the race path). Because `r29`/`r30` are callee-saved and set *before* the hook, the downstream change-path SFX + UI block at 0x8002cbf0 (which reads `r30` and `r29`) finds them intact after the C call, so no epilogue is needed. On return-1 it falls through to 0x8002cbf0; on return-0 it exits to 0x8002cc18, matching vanilla's `beq` at 0x8002cbec. This hook is required because the solo lobby never routes through `TopRide_CSS_PanelThink`, so the race cycler hook does not fire there — without it, the Free Run / Time Attack menus have no machine gating.

Both start-match gates sit at the first instruction of their respective start-match bodies (the `bl 0x80061658` menu-confirm SFX call). The preceding `andi.` against pad bit `0x1000` and `cmpwi ply_state, 1` (race) / `lbz is_all_ready` + `andi. 0x1000` (solo) already constrain the sites to "a Ready panel pressed Start"; we additionally require some TR machine to be unlocked. When both Free and Steer are locked the L/R cycler keeps `panel_machine[slot]` at the locked default, so without these gates a player could still press Start and the commit loop would call `TopRide_SetMachineKind` with a locked VCKIND. With the gates, pressing Start is a no-op (no sound, no commit) until at least one TR machine is unlocked.

The **race** gate (`0x8002c52c`) needs explicit register preservation that the solo gate does not. Its hook sits *inside* `TopRide_PreGameThink`'s 4-slot scan loop, and the block path returns to `0x8002c878`, which loops back to `0x8002c4fc` and recomputes `r3 = r4 + r5*68` (`r4` = slot-array base `0x8058b634`, set once before the loop; `r5` = slot index). Both are caller-saved and live across the whole loop, but the hoshi codepatch trampoline saves no registers around its `bl` to the C function. Because `GateMachines_TRLobbyCanStart` calls the SFX + textbox helpers, those volatiles get clobbered and the loop continuation would fault on a garbage base. The hook's prologue stashes `r4`/`r5` on a scratch frame and the epilogue restores them on both paths, so the loop is register-clean regardless of what the C function touches. The solo gate (`0x8002cc80`, `TopRide_OnCourseSelect`) has no enclosing loop and needs no save (empty prologue/epilogue).

The CT field spawn pipeline still force-zeros `VCKIND_FREE` and `VCKIND_STEER` via `CT_SPAWN_EXCLUDED_MASK` — these are TR-only forms and do not spawn on the City Trial field regardless of unlock state.

## Air Ride Mode — Select Screen

For full details on the vanilla select screen system, grid layout, icon animation pipeline, CharacterDesc table, and MnSelplyAll archive structure, see **[css-system.md](css-system.md)** (§ Air Ride CSS).

### Hooks

Two `CODEPATCH_REPLACEFUNC`:

1. **`AirRide_CheckCharacterAvailable` → `GateMachines_CheckAirRideCharacterAvailable`**
   - Gates the select screen icon grid. Takes CharacterKind, returns 1/0.
   - CKIND_DRAGOON, CKIND_HYDRA, CKIND_FLIGHT always return 0 (City Trial-only).
   - Others: uses `CharacterDesc_GetMachineKind()` to resolve the actual VCKIND (accounting for bike-relative indexing — see [css-system.md](css-system.md#bike-relative-indexing)), then checks `machine_unlocked_mask`.

2. **`TitleScreen_CheckMachineUnlocked` → `GateMachines_CheckTitleDemoMachineUnlocked`**
   - Gates the **title-screen attract-demo** machine pick (`TitleScreen_SelectRandomMachine`, reachable only via `TitleScreen_MinorExit` → `TitleScreen_SetupDemoMachines`). This does **not** run for CPUs in real Air Ride races.
   - Takes `machine_class` (is_bike) and `machine_id` (bike-relative index from `CharacterDesc.machine_kind`, NOT the absolute VCKIND for bikes).
   - Converts to actual VCKIND (`VCKIND_WHEELNORMAL + machine_id` if bike), then checks `machine_unlocked_mask`.

In vanilla, both functions map to checklist reward indices and check `ClearChecker_CheckUnlocked`. Only Warp Star (CKIND_WARP) is available by default. Compact Star (CKIND_COMPACT) is hardcoded unavailable (`case 0: return 0`).

**Real in-game CPU machine pick:** the actual Air Ride CPU machine is chosen in `loadCPU` (0x80023600) and its sibling setup paths, which do `machine[slot] = available_char_list[HSD_Randi(unlocked_count)]` over the list gated by the `AirRide_CheckCharacterAvailable` replacement (hook #1 above). So CPUs already draw a random **unlocked** machine in real races purely from that character-list gating — no separate machine-pick hook is needed. CPU color is gated separately via `color[]` validation, on a path independent of these `HSD_Randi` call sites; see [gate-colors.md](gate-colors.md).

**Stale-list clear (`GateMachines_ClearAirRideList`):** the select struct caches its available-machine list at `airride_select_ply +0x66` (the 2x10 = 20-entry icon grid). `AirRide_PopulateSelectIcons` runs **every CSS frame** (called unconditionally at 0x8002896c in `CSS_airRide_RaceUpdate` and 0x80029c74 in `CSS_airRide_FreeTimeUpdate`), but it only (re)writes the first `count` entries and never clears the tail — only the once-per-entry `CSS_airRide_InitSelectData` memset zeroes the whole region. So when `machine_unlocked_mask` is narrowed mid-session (e.g. the debug menu locks machines while you sit in the CSS), `count` drops but stale entries from an earlier fill linger past the new count. Every slot's icon index (`+0x2d`) defaults to 0 and the CSS resolves the displayed **and committed** machine as `list[icon]`, so a stale `list[0]` drives the whole lobby — and the subsequent race — onto a vehicle that is no longer unlocked (symptom: lock everything, re-open AR CSS, the entire lobby is on Winged Star). Fix: a `CODEPATCH_HOOKCREATE` at 0x80020a88 (just after `r31 = airride_select_ply` is set up, before the rebuild) zeroes `list[0..19]` each frame, so populate refills `[0..count-1]` and the tail stays 0 (→ `CKIND_COMPACT`). Because populate runs per-frame, the lobby self-heals the next frame rather than needing a full CSS re-entry. (Note: `ply_icon_ckind +0x61` is **not** the rendered/committed field — `list[icon]` is — so clearing the list, not clamping `+0x61`, is the correct fix.)

### Compact Star Icon Issue

Vanilla `AirRide_CheckCharacterAvailable` hardcodes `case 0: return 0` — Compact Star was **never** intended to appear on the Air Ride select screen. Our replacement allows it when unlocked, but the icon material animation in the `MnSelplyAll` archive may have no valid texture at frame 0 for CKIND_COMPACT. Result: the select entry exists (blank box is selectable and functional) but no icon renders. See [css-system.md](css-system.md#mnselplyall-archive-structure) for the archive/animation structure.

## Legendary Machine Delivery (`GateMachines_GiveLegendaryMachine`)

This function is **not** a gate — it is the delivery mechanism for the AP "give legendary" items `AP_ITEM_GIVE_DRAGOON` (→ `machine_index` 0) and `AP_ITEM_GIVE_HYDRA` (→ `machine_index` 1), dispatched from `ap_item_handler.c`. It directly hands a player the assembled legendary machine via the vanilla assembly cinematic, bypassing the three-part field collection entirely.

`GateMachines_GiveLegendaryMachine(int machine_index)`:
1. Returns 0 immediately if `Scene_GetCurrentMajor() != MJRKIND_CITY`.
2. For each of the 5 players that is `PKIND_HMN` and has a machine `GOBJ`, reads its `MachineData` and fills a `LegendaryAssemblyParams { machine_index, ply, pos, up, forward }` (pos/up/forward copied from the rider's current `MachineData`).
3. Calls `LegendaryMachine_StartAssembly(&params)` (0x80283cf0) to play the cinematic and grant the assembled machine.
4. Returns 1 if assembly started for at least one human player, else 0.

It does **not** consult `machine_unlocked_mask` — receiving the give-item is itself the grant. (The *availability* gating of legendary machines — suppressing the natural three-part assembly when Dragoon/Hydra are locked — lives in `gate_items.c` via legendary-piece spawn filtering, plus the CSS/spawn unlock checks above; see [gate-items.md](gate-items.md).)

### Limitation: City Trial only

The `MJRKIND_CITY` guard exists because calling the assembly cinematic on Air Ride courses crashes the game — legendary machines have no Air Ride course support (no select-screen entry, no spawn pipeline, and the cinematic assumes the City Trial machine/rider context). `AP_ITEM_GIVE_DRAGOON` / `AP_ITEM_GIVE_HYDRA` items received outside City Trial return 0, so the AP unprocessed-items list retries them until the player enters City Trial.

## Save Data

`u32 machine_unlocked_mask` in `APSave` — bit N = MachineKind N.

## AP Items

AP item ID = `AP_MACHINE_UNLOCK_BASE (830) + MachineKind`. The handler in `ap_item_handler.c` accepts IDs `830..854` (the bound is `< AP_MACHINE_UNLOCK_BASE + VCKIND_WHEELVSDEDEDE`, i.e. VCKINDs 0–24), then calls `GateMachines_UnlockMachine(kind, announce=1)`. The AP world generates an unlock for **all 25 in-range IDs (830–854)** as `progression`; only 855 (WHEELVSDEDEDE) is excluded.

`VCKIND_WHEELVSDEDEDE` (= 25, would be ID 855) is **not part of the AP item range** at all. It is the Vs. King Dedede stadium's CPU machine — no `CharacterDesc` references it, no CT spawn path includes it (`CT_SPAWN_EXCLUDED_MASK` keeps it pinned to 0 chance defensively), no CSS lists it, and the stadium availability check uses the stadium mask not the machine mask, so its bit has no readers. The handler upper bound stops at `VCKIND_WHEELVSDEDEDE`, so ID 855 falls through to the unknown-item path.

**Five IDs behave specially in-game (845 FREE, 846 STEER, 847 WINGKIRBY, 849 WHEELNORMAL, 850 WHEELKIRBY)** — all are generated by the world, but they split into two cases for *this* file:

- **845 FREE / 846 STEER** are the two Top Ride control-type machines (`VCKIND_FREE`, `VCKIND_STEER`). Their bits **are read** by the Top Ride lobby gating in `gate_machines.c` (`IsTRMachineUnlocked`, `GateMachines_TRLobbyCanStart`). They do not spawn on the CT field (force-zeroed via `CT_SPAWN_EXCLUDED_MASK`) and no character rides them in the 3D modes, but setting either bit unlocks the TR lobby cyclers and the start-match gate. So if the AP world receives one of these IDs it is **not** a no-op.
- **847 WINGKIRBY / 849 WHEELNORMAL / 850 WHEELKIRBY** are transformation/debug forms with no readers anywhere — granting them is a genuine no-op.

The canonical Dedede unlock is **854 WHEELDEDEDE** (`VCKIND_WHEELDEDEDE` = 24), which is what `CharacterDesc[CKIND_DEDEDE]` resolves to via `CharacterDesc_GetMachineKind` (is_bike=1, machine_kind=5 → 19+5=24). `REWARD_KING_DEDEDE` in the checklist reward path also unlocks only this bit.

The mod still accepts all 25 in-range IDs defensively (no crash if they arrive).

See `docs/client-game-protocol.md` for the full machine-unlock breakdown. The AP world generates FREE (845) and STEER (846) as progression items (`worlds/kirby_air_ride/KARItems.py`), so a received Free/Steer unlock opens the Top Ride lobby. One caveat: those items are tagged `source_modes=_AR_CT` (Air Ride + City Trial), not `_TR`, so their reachability in Top-Ride-only seeds is an open question (see `client-game-protocol.md`).

## Remaining Work

### TODO: Compact Star Air Ride Icon

The `MnSelplyAll` archive's MatAnimJoint uses the CharacterKind value as the animation frame to select the icon texture. Frame 0 (CKIND_COMPACT) likely has no valid texture since vanilla never displays Compact Star on this screen. See [css-system.md](css-system.md#matanimjoint-structure-partially-traced) for the TexAnim keyframe / ImageDesc array layout. What's needed: the frame 0 state along the MatAnimJoint child → MatAnim → TexAnim chain. If blank, options include patching the archive `.dat` or runtime texture swap from the City Trial archive (`MnSelplyctAll.dat`).

## Design Decisions

**Full selection replacement over exclusion mask:** The exclusion mask approach is attractive (minimal code change) but fundamentally broken — the game uses separate registers for the two passes, and hooking between them clobbers the random result. The KAR Deluxe full-replacement approach is more code but provably correct.

**Base chance of 10 for zero-weight unlocked machines:** Without this, machines that have 0 spawn weight in the current time window (e.g., Hydra early in a match) can never spawn even when unlocked. The base chance ensures all unlocked machines have some possibility of appearing, which is important for AP progression.

**History size reduction:** `min(spawnable_count - 1, 4)` prevents deadlock when few machines are spawnable. With only 2 spawnable machines and history size 4, both would be excluded. The reduction ensures at least one machine is always selectable.

**Shared mask across modes:** One `machine_unlocked_mask` covers City Trial, Air Ride, **and** Top Ride (Free/Steer). Unlocking a machine once makes it available everywhere. This keeps the AP item pool to one item per machine instead of a separate item per mode, and matches player expectation. (The actually-generated pool is 20 IDs — five VCKINDs are spawn/transform placeholders the world never generates; see AP Items.)
