# Machine Gating

Every machine can be individually locked behind an Archipelago unlock item. AP item `AP_MACHINE_UNLOCK_BASE` (830) + `MachineKind` routes through `ap_item_handler.c` to `GateMachines_UnlockMachine(kind, announce)`, which sets a bit in `APSave.machine_unlocked_mask` and posts a textbox. One mask covers all three modes: City Trial field spawns and select grids, the Air Ride character select, and the Top Ride lobby's Free Star / Steer Star control types, so unlocking a machine once makes it available everywhere and the AP pool holds one item per machine rather than one per mode.

**File:** `mods/archipelago/src/gate_machines.c`.

## The Kind Space

The 26 `MachineKind`/`VCKIND` values (`VCKIND_NUM` = 26, `machine.h`) plus whatever kinds `custom_machines` registers, one bit each. Six VCKINDs have no `CharacterKind` and are city-spawn or transformation only: FREE, STEER, WINGKIRBY, WHEELNORMAL, WHEELKIRBY, WHEELVSDEDEDE. Character-to-machine resolution goes through `CharacterDesc_GetMachineKind` (`menu.h`), which un-does the bike-relative encoding of `CharacterDesc.machine_kind` (bikes store a 0-6 slot against VCKINDs 19-25).

`custom_machines` is a required dependency. It owns the widened kind space and the engine seams widening breaks - both character select screens' icon packing, the City Trial field spawn roll, and the legendary assembly cutscene - and this mod reimplements none of them: it hands the registry a filter for each and lets it call back. Registered machines take `MachineKind`s past 26 and are gated the same way.

Every ceiling in `gate_machines.c` is the runtime `MachineKind_Num()` / `CharacterKind_Num()` rather than `VCKIND_NUM` / `CKIND_NUM`, and every `(is_bike, class slot)` to `MachineKind` conversion goes through `MachineKind_Resolve()`, because a custom machine is the only kind whose class slot and `MachineKind` differ. Those are thin bindings in `main.h` over inlines the registry publishes in `custom_machines_api.h`, which is also where their vanilla fallbacks live.

A build that leaves `custom_machines` out still boots. `AP_ResolveCustomMachines` (`main.c`) reports the missing import once at `OnSaveLoaded` - the first point past every mod's `OnBoot`, and so the first point a failed import means anything - and machine gating is off for that session rather than half-applied: the select screens offer the engine's own roster, the field spawns the vanilla table, and the ceilings fall back to 26 and 20.

**`CT_SPAWN_EXCLUDED_MASK`** force-zeros the non-field machines in the CT spawn pipeline regardless of unlock state: `VCKIND_FREE` / `VCKIND_STEER` (Top Ride-only forms), `VCKIND_WINGKIRBY` / `VCKIND_WHEELNORMAL` / `VCKIND_WHEELKIRBY` (Kirby transformation forms reached through the copy-ability item path, not a field spawn), and `VCKIND_WINGMETAKNIGHT` / `VCKIND_WHEELDEDEDE` / `VCKIND_WHEELVSDEDEDE` (the Meta Knight / King Dedede character forms). The two character forms matter because unlocking Meta Knight or King Dedede sets the corresponding bit; both have a 0 base spawn chance in vanilla, so without the exclusion the unlocked-but-zero-chance fallback below would leak the Wing Meta Knight / Dedede Wheelie machine onto the field as a rideable star.

## City Trial - Field Spawning

Machine spawning in City Trial is chance-based. The game reads a spawn table (`(*stc_vcDataCommon)->spawn_data->spawn_desc[]`; `stc_vcDataCommon` is `vcDataCommon**` at r13+0x758, the `spawn_data` sub-struct pointer sits at `vcDataCommon+0x20` and `spawn_desc` at `spawn_data+0x8`) indexed by match progress, with `float chance[VCKIND_NUM]` per entry.

The table lives in `VcCommon.dat` (public `vcDataCommon`, data offset 0x2b1c) and has exactly **three** entries, thresholds `0.0` / `0.5` / `1.1`, selected by `while (match_progress > spawn_desc[i].match_progress) i++`. Only **14** of the 26 VCKINDs carry a nonzero weight, and the same 14 in all three entries - the weights only shuffle between windows:

| Machine | 0.0 | 0.5 | 1.1 |
|---------|----:|----:|----:|
| WARP | 10 | 10 | 10 |
| WINGED | 8 | 8 | 8 |
| SHADOW | 8 | 8 | 8 |
| BULK | 7 | 9 | 9 |
| SLICK | 6 | 10 | 10 |
| FORMULA | 8 | 8 | 10 |
| WAGON | 10 | 6 | 6 |
| ROCKET | 8 | 8 | 8 |
| SWERVE | 8 | 8 | 8 |
| TURBO | 8 | 8 | 8 |
| JET | 6 | 10 | 10 |
| WHEELIEBIKE | 8 | 8 | 8 |
| REXWHEELIE | 6 | 10 | 10 |
| WHEELIESCOOTER | 10 | 6 | 6 |
| **total** | **111** | **117** | **119** |

The four non-spawning machines outside `CT_SPAWN_EXCLUDED_MASK` are `VCKIND_COMPACT` (the starting machine), `VCKIND_FLIGHT`, `VCKIND_HYDRA` and `VCKIND_DRAGOON` - all 0 in every window, and so the only four vanilla kinds that ever reach the mod's zero-chance fallback. The two legendaries are assembly-only in vanilla and never appear whole on the field; under the fallback their machine unlock puts one there, independently of whether their piece items have arrived.

`CityMachineSpawn_DecideAndSpawn` (0x801defac) and `cityTrialSpawnFormationStar` (0x801df408) build a `u32` exclusion bitmask from `MachineSpawnData.prev_machine_kind[4]` (+0x50, recently spawned machines), then do a two-pass weighted random selection: pass 1 (r5) computes the total spawn chance sum, pass 2 (r29) performs the selection via `HSD_Randf`. **Hooking that exclusion bitmask to OR in locked machines does not work**: the mask is built twice in separate registers so hooking one misses the other, hooking between the two passes clobbers f1 (the `HSD_Randf` result), and if the only unlocked machine has 0 base weight in the current window or is excluded by the 4-slot history, the selection loop falls through to machine_kind 26 and crashes loading a nonexistent `.dat`.

`custom_machines` therefore replaces the whole selection - the engine's loop is `VCKIND_NUM` wide and has no index a registered machine could be picked at - and asks a weight filter per `MachineKind`, registered as `cm_api->SetSpawnWeightFilter(GateMachines_SpawnWeight)`. `GateMachines_SpawnWeight(kind, default_weight)` is handed `VcCommon.dat`'s own chance for a vanilla kind in the window being rolled, and the descriptor's `spawn_weight` for a registered one. It returns:

- **0** for anything in `CT_SPAWN_EXCLUDED_MASK`, and for anything the mask does not hold.
- **`default_weight` unchanged** for a registered machine, which brings its own weight and takes no fallback - a descriptor asking for 0 keeps it off the field however the mask reads - and for any vanilla kind the table already weighs.
- **`ZeroChanceSpawnWeight(kind)`** for an unlocked vanilla kind the table gives 0.

The fallback exists so an unlocked machine can always appear, which matters for AP progression, but it is a token weight rather than a competitive one and is tiered by how much the machine warps a match. Vanilla per-machine weights run 6-10 against a table total of 111-119, so a flat fallback in that band would make each of these a joint-highest entry at roughly 8% of spawns:

| Machine | Weight | Share | Rationale |
|---------|-------:|------:|-----------|
| Compact | 5 | ~4% | Ordinary all-rounder; only absent from the table because it is the starting machine |
| Flight Warp Star | 2 | ~1.7% | Strong flyer, but recoverable to fight against |
| Hydra / Dragoon | 1 | ~0.8% each | The two strongest machines in the game |
| Archipelago Star | 1 | ~0.8% | From its descriptor, not the table; priced with the legendaries it is assembled alongside |

Together they take ~7.5% of spawns, leaving the vanilla 14 with ~92%. For the legendaries, field-piece assembly stays the primary route and a whole-machine spawn is a rare bonus.

Everything downstream of the filter is the registry's: seeding the array, shrinking the spawn-history exclusion to `min(spawnable - 1, 4)` so the only candidate cannot be excluded by its own history (with 2 spawnable machines and a history of 4, both would be), and rolling the weighted pick with `HSD_Randf`. If the filter zeroes every kind, the roll is skipped and the registry takes the first kind the filter permits at an even weight of 1.0.

### Free Run's separate placer

City Trial's Free Run mode does not use the weighted spawner. `CityMachineSpawn_Think` (0x801df840) branches on `Gm_GetCityMode()` and, in Free Run, calls `CityMachineSpawn_SpawnFreeRunMachine` (0x801dee58) -> `CityMachineSpawn_PickFreeRunKind` (0x801de41c), which fills the city with up to 23 machines by drawing uniformly from the kinds not yet placed. It tracks those in `MachineSpawnData.freerun_placed[VCKIND_NUM]` (+0xac) and takes every kind unconditionally - except 4 (Hydra) and 8 (Dragoon), which it puts through `CityTrial_CheckLegendaryMachineUnlocked` (0x8000c508), a **checklist** reward query that AP never writes.

A `CODEPATCH_REPLACECALL` at 0x801de528 answers that call from `GateMachines_CheckFreeRunLegendaryUnlocked` instead, so an AP-owned legendary appears in Free Run and a locked one does not. The other kinds are left as vanilla: Free Run's sandbox roster is not gated, and the picker's 0-25 loop cannot reach a custom `MachineKind` at all, so the Archipelago Star never appears there.

## City Trial - Machine Select Screens

`CitySelect_CreateMachineIcons` (0x8002e3c4) builds the icon grid and branches on `Gm_GetCityMode()`:

- **Mode 1 (Stadium)**: vanilla switches on ckind inline. CKINDs 0-14 are hardcoded available; 15 (Dragoon), 16 (Hydra), 17 (Flight Warp Star), 18 (King Dedede), 19 (Meta Knight), and >=20 are hardcoded unavailable, with no checklist or unlock check at all.
- **Mode 2 (Free Run)**: 0-14 and 17 are hardcoded available; 15/16/18/19 each map to a per-character reward index (30, 34, 35, 36) and call `ClearChecker_CheckUnlocked` (0x80049e24). >=20 is skipped.

Both modes have a counting pass (total available count -> r27) and an array-building pass (populating a 2x10 local array of CharacterKinds). Mode 0 (Trial) doesn't populate `machine_select.c_kind_arr` at all - there is no on-screen machine grid.

`custom_machines` owns the packing of both character select screens, because it widens the character grid they pack from and vanilla's loops are a hard 10 columns into two 10-byte stack rows, leaving an 11th column nowhere to land. It replaces `AirRide_PopulateSelectIcons` and takes over City Trial's counting and array-building passes (hooks at 0x8002e4d0, 0x8002e5c0 and 0x8002f0b8, with the two array-building passes branched straight to the tail), then asks an availability predicate per `CharacterKind`, registered as `cm_api->SetAvailabilityFilter(GateMachines_FilterSelectCharacter)`.

`GateMachines_FilterSelectCharacter(ckind, default_available)` discards `default_available` and returns `IsCKindUnlocked(ckind)`, which resolves the ckind's `CharacterDesc` (`Character_GetDesc`, 0x8000b9dc) to a VCKIND and tests the mask. Discarding the engine's answer is what makes an owned machine selectable in every mode whose select screen offers it, including the four specials Stadium hardcodes out and the appended characters a drop-in machine brings.

The filter is registered in `AP_ResolveCustomMachines`, not `GateMachines_OnBoot` - mods boot alphabetically and the registry boots after us, so a `Hoshi_ImportMod` during our own boot always returns NULL. Registration is idempotent and re-tried per call, and the first call happens at `OnSaveLoaded`, well before any select screen builds a list. The screens ask per rebuild rather than caching, so a filter set from any scene takes effect on the next one. The packing runs during `CitySelect_CreateMachineIcons` (before `OnPlayerSelectLoad`), so `machine_select.c_kind_arr` (`GameData+0x236`) is already filtered by the time the player select scene is up.

### Navigation off-by-one fix

The CT machine-select grid has **two independent row-layout authorities** that only agree for the counts vanilla actually produces:

- **Rendering** is archive-animation-driven, not code. The flat-copy at 0x8002f0b8 writes `machine_select.num` (= total unlocked count, `GameData+0x235`) and creates `num` icons at flat indices `0..num-1`. Icon positions come from the selection-box JObj animation applied at **frame = count** (setup `_CitySelect_LayoutMachineIcons` 0x8015bd14 via thunk `CitySelect_LayoutMachineIcons` 0x801355f4), read back per slot into `slot.pos` (+0x60); `CitySelect_GetIconPos` (0x8015badc) just returns `slot_array[index].pos`. The 2x10 layout keeps **up to 10 icons on a single line** and only wraps to two rows at **11** (at `num==10` every slot shares one Y; at 11 it is 6/5; at 12 it is 6/6).
- **Navigation** (`CitySelect_Cursor1InputThink`, 0x800312fc) reads `machine_select.num` and at 0x80031350 does `cmpwi r3, 9; ble` -> `num<=9` single-row (LEFT/RIGHT only), `num>=10` two-row (up/down enabled, split at `ceil(num/2)`).

So at exactly `num==10` the renderer draws one line of 10 while the cursor splits it 5+5 and up/down jumps between the halves. Vanilla CT only ever produces counts 15-20 (Free Run: ckinds 0-14+17 always available, 15/16/18/19 checklist-gated; Stadium: 0-14 = 15), so the off-by-one was never exercised. AP machine gating can land on exactly 10 unlocked machines, exposing it.

The threshold is patched to `cmpwi r3, 10` (`0x2c03000a`) so `num<=10` is single-row, matching the renderer. The same nav function serves Stadium and Free Run, so one patch covers both. It is applied by `custom_machines` alongside the packing, since any filtered roster can reach a count of 10 and not only a gated one. Air Ride uses a separate code path: `AirRide_PopulateSelectIcons` switches linear/grid at `count < 10`, the same boundary as its nav, so it is internally consistent and does **not** share this off-by-one.

## City Trial - Starting Machine and Respawn

`CitySelect_InitPlayerMachines` (0x8002ddd8) commits the per-slot starting machine for **every** City Trial mode. Its two branches both write `ply_icon_ckind[slot]` (city_select_ply +0x61, `GameData+0x231`) and merge at the convergence point 0x8002dea0 (`lbz r3, 97(r28)` -> `Character_GetDesc`):

- **Trial** (`city_select_ply.x1d0 == 0`): vanilla hardcodes Compact for every slot. The free-roam start has no machine grid - nobody, human or CPU, picks.
- **Stadium / Free Run** (`x1d0 != 0`): vanilla sets `ckind = machine_select.c_kind_arr[icon[slot]]` from the gated grid. The player can roam the single cursor onto any CPU panel and pick that CPU's machine (the icon-grid write at 0x800315ac in `CitySelect_Cursor1InputThink`); a CPU the player never touches is seeded a random gated machine by the vanilla loaders (`icon[slot] = HSD_Randi(machine_select.num)`).

A single `CODEPATCH_HOOKCREATE` at 0x8002dea0 (prologue `mr 3, 26` -> slot; skip target 0 re-executes the clobbered `lbz`, reloading the updated ckind) runs `GateMachines_FinalizeCTMachine(slot)` for each slot. `r26` = slot index and `r28` = `city_select_ply + slot` are both callee-saved, so they survive the C call. `x215[slot]` is `0` = human, `2` = CPU, anything else = inactive (left untouched). CPU slots also get `ply_color[slot] = GateColors_RandomUnlockedColor()` here, independent of the machine toggle.

The **Random Start Machine** menu toggle (`ap_menu_settings.ct_random_start_machine`, default On) is the single master and applies identically to humans and CPUs wherever neither makes an explicit grid pick:

- **Trial**: the toggle drives every active slot the same way. On -> `RandomUnlockedKirbyCKind()`; Off -> Compact when unlocked, else `RandomUnlockedKirbyCKind()`.
- **Stadium / Free Run**: humans actively pick on the grid, so a human's selection is always kept. CPU slots the player did not pick a machine for follow the toggle - On -> a random entry from the gated `c_kind_arr[0..num-1]`, Off -> the vanilla seed. A CPU the player explicitly picked a machine for keeps that pick regardless of the toggle.

**Manual-pick tracking.** Because the engine random-seeds CPU `icon[slot]`, a CPU never starts at a clean sentinel and there is no built-in "the player chose this" flag, so the mod tracks it. A second `CODEPATCH_HOOKCREATE` at 0x800315ac (`stb r27, 45(r30)` -> `icon[slot]`, prologue `mr 3, 29` -> slot; that store fires only when the chosen grid index actually changes, so it is the sole player-driven pick site) runs `GateMachines_NoteManualMachinePick(slot)`, setting bit `slot` in the static `ct_machine_manual_pick_mask`. `GateMachines_FinalizeCTMachine` consumes-and-clears that bit per slot (cleared every match, including for inactive slots, so it never leaks forward) and skips the CPU re-roll when it is set. Without this, the re-roll would clobber a machine the player deliberately assigned to a CPU.

`RandomUnlockedKirbyCKind()` picks a random unlocked CharacterKind for the free-roam Trial start but **excludes `CKIND_DEDEDE` and `CKIND_METAKNIGHT`**: their riders rely on rider-specific 3D HUD assets that vanilla's HUD loader short-circuits in Base CT (`major==CITY && cityMode==TRIAL`), so selecting them there would NULL-deref `3DHud_CreateSpeedometerInner` during scene init. It falls back to `CKIND_COMPACT` when no eligible Kirby-rider machine is unlocked. The Stadium/Free Run CPU path draws straight from `c_kind_arr` instead (Dedede/Meta Knight are valid in stadium contexts).

### Respawn machine

When a player respawns mid-match, `Rider_ResetStartingMachine` (0x80195288) puts them back on a machine - vanilla hardcodes `VCKIND_COMPACT` via two `Ply_Set*` calls. A `CODEPATCH_HOOKCREATE` at 0x801952c8 (inside the function, `r31` = `RiderData*`) redirects to `GateMachines_ResetStartingMachine()` and skips to the epilogue at 0x801952e0. The vanilla prologue gating runs unmodified before the hook point, so the replacement only consumes `rd`.

`GateMachines_ResetStartingMachine()` first tries `rd->starting_machine_idx` (the per-rider intended starting machine). If that VCKIND is locked, it falls back to the lowest-index unlocked **CT-spawnable** VCKIND via `GetFirstUnlockedCTMachine()` (which skips `CT_SPAWN_EXCLUDED_MASK`) - deterministic per-rider, distinct from the CSS default-pick logic above. It then writes the result through `Ply_SetMachineIsBike` / `Ply_SetMachineKind`, which the engine addresses class-relatively, so the VCKIND is converted with `MachineKind_ClassIndexOf()`. That conversion has to be the custom-aware one: bikes are VCKINDs 19-25 against class slots 0-6, and a registered custom machine sits at 26 and up, so a bare `kind >= VCKIND_WHEELNORMAL` test claims it as a bike and hands the bike class a slot past its seven. A custom machine is a star, and its class slot is the appended star slot the registry gave it. Without this hook a player could respawn on a locked machine; without the exclusion-mask skip, a sparse unlock state could respawn them on a Top Ride-only Free/Steer Star.

## Top Ride - Lobby Machine Select

The TR lobby panel has a three-row in-panel menu: Player/CPU on top, **Control Type** in the middle (Free Star = 0, Steer Star = 1), and Handicap on the bottom. Machine selection cycles via analog stick L/R on the middle row. The cycle target is `GameData.topride_select_ply.panel_machine[panel]` (`GameData+0x1c6`, lobby offset +0x2f) and is shared between human-configured and CPU-configured panels.

**The race lobby and the solo Free Run / Time Attack lobby are two distinct code paths** - they do *not* share a cycler. `TopRide_LobbyThink` (0x8002dd34) dispatches on `topride_select_ply.init_flag` (`GameData+0x198`): 0 -> `TopRide_PreGameThink` (0x8002c06c, multiplayer race), nonzero -> `TopRide_OnCourseSelect` (0x8002cc30, solo). Each has its own per-frame panel-editing think with its own copy of the L/R cycler: race is `TopRide_CSS_PanelThink` (0x8002b8a8), solo is `TopRide_SoloPanelThink` (0x8002ca80, for `ply_state != 1`). Both cyclers read/write `panel_machine` at lobby offset 0x2f and test the same RIGHT (0x80002) / LEFT (0x40001) edge bits, so one `GateMachines_CycleTRMachine` serves both hook sites. The init paths split the same way: `TopRide_LobbyInit` (0x8002dc9c) dispatches on `TopRide_GetMode()` (0x8003ea9c) to `TopRide_RaceInit` (0x8002d0ec) or `TopRide_SoloInit` (0x8002d9e8), while `TopRide_InitSelectData` (0x8002cfd8) is a third, earlier init called from `MainMenu_InitAllVariables` / `Gm_ResetAllData` / scene transitions.

Seven hooks cover the surface:

| Hook address | Function | Role |
|-------------|----------|------|
| 0x8002d070 | `TopRide_InitSelectData` | Post-init fixup (main-menu reset): vanilla writes `panel_machine = 0` (Free); when Free is locked, override to the first unlocked TR machine for all 4 panels |
| 0x8002d748 | `TopRide_RaceInit` | Post-reset fixup: vanilla's conditional reset block at 0x8002d6c4..0x8002d700 overwrites `panel_machine = 0` again, undoing InitSelectData's fixup. This is the only fixup site that runs after `panel_pkind` (lobby +0x1b) is filled, so it is the only one where CPU panels (`panel_pkind == 2`) can take a *random* unlocked control type plus a random unlocked color; human panels get the first unlocked machine |
| 0x8002db90 | `TopRide_SoloInit` | Same fixup for the solo flow, which hardcodes `panel_machine = 0` at 0x8002db70..0x8002db88 |
| 0x8002be44 | `TopRide_CSS_PanelThink` | Race L/R cycler gate: replaces the cycle block + post-write compare through 0x8002be94. Conditional - 0 (no change) skips to function end 0x8002c054, 1 falls through to the SFX + UI update at 0x8002be98 |
| 0x8002cb98 | `TopRide_SoloPanelThink` | Solo L/R cycler gate, same function; 0 -> 0x8002cc18, 1 -> 0x8002cbf0. Without it, solo had no unlock check on the Control Type row |
| 0x8002c52c | `TopRide_PreGameThink` | Start-match gate (race): `GateMachines_TRLobbyCanStart` blocks the confirm + commit-and-launch sequence when neither `VCKIND_FREE` nor `VCKIND_STEER` is unlocked; blocked -> 0x8002c878 |
| 0x8002cc80 | `TopRide_OnCourseSelect` | Start-match gate (solo), same condition; blocked -> 0x8002cddc |

Register and placement constraints behind those choices:

- **InitSelectData** lands at `li r0, 0x1` (the first instruction after the per-slot init loop) because 0x8002d06c, the natural convergence point, is already claimed by `gate_colors.c`'s `GateColors_ValidateTopRideColors`. The epilogue restores `r3 = 0` (clobbered by the C call but required by the three following `stb r3, {6,2,3}(r31)` lobby-flag clears) before the framework re-executes the clobbered `li r0, 1`. Without the restore, `active_pad_mask` / `x199` / `x19a` get garbage on first entry and the panel UI fails to render until the next scene entry.
- **RaceInit** deliberately lands past the `panel_pkind` CPU-fill loop at 0x8002d710..0x8002d744 rather than right after the `panel_machine` reset; landing inside that loop would clobber its caller-saved iterator `r7`. Hooking at the post-loop `bl gmGetGlobalP` is clean - the framework's auto-re-execution of the `bl` reloads `r3 = GameData*` for the following `addi r6, r3, 407`, so no epilogue is needed. Nothing between the reset block and the hook site reads `panel_machine[]`.
- **SoloInit** lands one instruction after `gate_colors.c`'s parallel solo color fixup at 0x8002db8c, so it fires after that hook's clobber-re-execution sets `r28 = 0`. The re-executed `add r30, r31, r28` leaves the per-slot loop's base register correct without an explicit epilogue.
- **Race cycler** lands at `lbz r4, 0x2f(r26)`, after the outer "any L/R input?" guard at 0x8002be2c; at entry `r26` = panel base and `r29` = direction-edge bits.
- **Solo cycler** lands at 0x8002cb98 (`and. r0, r26, r0`, the RIGHT-bit test) - one instruction after the cycler computes `r29` = panel index and `r30` = lobby + panel, and after the outer 0xC0003 L/R guard at 0x8002cb80. `r30` and `r26` are callee-saved and set before the hook, so the downstream SFX + UI block finds them intact and no epilogue is needed.
- **Both start-match gates** sit at the first instruction of their start-match bodies (the `bl 0x80061658` menu-confirm SFX call). The preceding `andi.` against pad bit 0x1000 and `cmpwi ply_state, 1` (race) / `lbz is_all_ready` + `andi. 0x1000` (solo) already constrain the sites to "a Ready panel pressed Start"; the gate additionally requires some TR machine to be unlocked. When both Free and Steer are locked the cycler keeps `panel_machine[slot]` at the locked default, so without these gates Start would still commit a locked VCKIND through `TopRide_SetMachineKind`. With them, Start is a no-op (error buzzer and a textbox, no commit).
- **The race gate needs explicit register preservation** the solo gate does not. Its hook sits inside `TopRide_PreGameThink`'s 4-slot scan loop, and the block path returns to 0x8002c878, which loops back to 0x8002c4fc and recomputes `r3 = r4 + r5*68` (`r4` = slot-array base 0x8058b634, set once before the loop; `r5` = slot index). Both are caller-saved and live across the whole loop, but the hoshi codepatch trampoline saves no registers around its `bl`, and `GateMachines_TRLobbyCanStart` calls the SFX + textbox helpers, so the prologue stashes `r4`/`r5` on a scratch frame and the epilogue restores them on both paths. The solo gate has no enclosing loop and needs no save.

## Air Ride - Select Screen

Two `CODEPATCH_REPLACEFUNC`s at `GateMachines_OnBoot`, plus the packing `custom_machines` owns:

1. **`AirRide_CheckCharacterAvailable` (0x8002090c) -> `GateMachines_CheckAirRideCharacterAvailable`** gates the select screen icon grid. Takes a CharacterKind, returns 1/0, and defers to the same `IsCKindUnlocked` rule the City Trial passes use. Vanilla instead maps each ckind to a checklist reward index, makes only Warp Star available by default, and hardcodes four ckinds unavailable whatever the save holds - its jump table at 0x80496e10 sends 0 (Compact Star), 15 (Dragoon), 16 (Hydra) and 17 (Flight Warp Star) to a bare `li r3, 0` with no reward query. Dropping those four exclusions is what makes an owned City Trial machine rideable in Air Ride. Nothing else has to move: the icon grid at 0x80495800 places Dragoon and Hydra at row 0 columns 0 and 9, and the `MnSelplyAll` icon TexAnim carries 20 distinct 64x64 CMPR images, one per ckind. Vanilla's reorder block that treated ckinds 15/16/18/19 as row-end specials does not run at all, because the replacement below packs left-aligned with no reorder.
2. **`AirRide_PopulateSelectIcons` (0x80020a08) -> `custom_machines`' packer.** Not this mod's patch, but the reason it is anyone's. The replacement counts available characters, fills the list in one-row-strip order below 10 icons and grid order at 10 or more, writes the count to select base +0x65 and the row-split flag to +0x7a, and calls `AirRideSelect_LayoutIcons` / `AirRideSelect_CreateSIcon`. **Vanilla's own packing hangs the console under a mask-driven roster.** At 10 icons or more it packs the 2x10 grid into two rows and then rebalances them, and the rebalance loop at 0x80021110-0x80021320 only converges when the rows differ by at most three. Its two halves are not mirrored: the `row0 < row1` half moves an entry and then moves a second the same way (0x8002104c, 0x800210b0), closing the gap by two per pass, while the `row0 >= row1` half moves one entry across (0x80021260) and then re-reads the counts and moves it straight back (0x800212bc), making no progress. The rows oscillate forever, the CPU never leaves the function, and the packed rows are shredded into one repeated ckind by the shift that runs each pass. Vanilla can never reach a gap that wide, because `AirRide_CheckCharacterAvailable` returns 0 unconditionally for ckinds 0, 15, 16 and 17 and all four sit in grid row 0, capping it at 6 of 10. Handing those four back on the mask raises row 0's ceiling to 10, so any unlock set with 10+ Air Ride characters and row 0 at least four ahead of row 1 reaches the defect - 7/3 is the smallest such split. Packing directly and never calling the rebalance is what avoids it; the rebalance only reordered icons for looks.
3. **`TitleScreen_CheckMachineUnlocked` (0x8000c364) -> `GateMachines_CheckTitleDemoMachineUnlocked`** gates the **title-screen attract-demo** machine pick (`TitleScreen_SelectRandomMachine` 0x8000daa0, reachable only via `TitleScreen_MinorExit` -> `TitleScreen_SetupDemoMachines`). Takes `machine_class` (= `CharacterDesc.is_bike`) and `machine_id` (= `CharacterDesc.machine_kind`, a class-relative slot rather than the absolute VCKIND), resolves the VCKIND, range-checks it, then tests the mask. This does **not** run for CPUs in real Air Ride races.

**Real in-game CPU machine pick.** The actual Air Ride CPU machine is chosen in `loadCPU` (0x80023600) and its sibling setup paths, which index a random entry out of the available-character list `AirRide_PopulateSelectIcons` builds through the replaced `AirRide_CheckCharacterAvailable`. CPUs therefore already draw a random unlocked machine with no separate machine-pick hook. CPU color is gated on its own path in `gate_colors.c`.

**Stale-list clear.** The select struct caches its available-machine list at `airride_select_ply +0x66`. `AirRide_PopulateSelectIcons` runs **every CSS frame** (called unconditionally at 0x8002896c in `CSS_airRide_RaceUpdate` and 0x80029c74 in `CSS_airRide_FreeTimeUpdate`), and vanilla only (re)writes the first `count` entries, never clearing the tail - only the once-per-entry `CSS_airRide_InitSelectData` memset zeroes the whole region. So when the mask is narrowed mid-session (e.g. the debug menu locks machines while sitting in the CSS), `count` drops but stale entries from an earlier fill linger past the new count. Every slot's icon index (+0x2d) defaults to 0 and the CSS resolves the displayed **and committed** machine as `list[icon]`, so a stale `list[0]` drives the whole lobby - and the subsequent race - onto a vehicle that is no longer unlocked. `ply_icon_ckind +0x61` is not the rendered/committed field, so clearing the list rather than clamping +0x61 is the correct fix. The replacement zeroes the whole list before every rebuild, so the tail stays 0 (-> `CKIND_COMPACT`) and the lobby self-heals on the next frame.

## Legendary Machine Delivery

`GateMachines_GiveLegendaryMachine` is **not** a gate - it is the delivery mechanism for the AP "give legendary" items `AP_ITEM_GIVE_DRAGOON` (-> `machine_index` 0) and `AP_ITEM_GIVE_HYDRA` (-> 1), dispatched from `ap_item_handler.c`. It hands a player the assembled legendary via the assembly cutscene, bypassing three-part field collection, and does **not** consult `machine_unlocked_mask` - receiving the item is itself the grant.

`custom_machines` owns that cutscene (`LegendaryMachine_StartAssembly`, 0x80283cf0). It stands in at the three `bl`s the engine takes its machine-specific decisions at, so a registered machine can run the whole shot with its own archives, and `StartAssembly(kind, ply)` accepts `VCKIND_DRAGOON` and `VCKIND_HYDRA` as well and falls through to the engine's own. Every condition on a run lives there rather than here:

- City Trial only. The cutscene stages its models on the open CT map and drives that scene's sky and area lights, so a stadium or an Air Ride race dereferences a null jobj or trips the area-light assert.
- A Kirby rider. `Rider_EnterLegendaryAssembly` (0x8019248c) is Kirby-only and the mount rides on the state it enters, so Meta Knight or King Dedede would get the whole shot and no machine.
- One run at a time. `GameData+0xA8C` holds a single controller GObj and a second run tears down the first one's piece GObjs, leaving a dangling jobj that crashes on the next update.
- Each vanilla legendary at most once per scene. The engine frees `VsDragoon.dat` / `VsHydra.dat` when a run ends, so a second run in the same scene loads a joint out of the freed archive and crashes in `HSD_JObjLoadJoint`. A registered machine's archives are reloaded per run and carry no such limit.

So `GateMachines_GiveLegendaryMachine(machine_index)` is the loop and nothing else: it maps the index to `VCKIND_DRAGOON` / `VCKIND_HYDRA` and calls `cm_api->StartAssembly(kind, i)` for each `PKIND_HMN` player until one takes, returning 1 (consume the item) on the first success and 0 (keep queued, retry) if none can. Because the engine holds a single cutscene, it lands on the first human it can rather than every one.

## AP Items and Save Data

`u32 machine_unlocked_mask` in `APSave` (`main.h`, accessed via the global `ap_save`) - bit N = `MachineKind` N. It is exposed through `ArchipelagoAPI` as `AP_UNLOCK_MACHINE`, and when the slot option `machine_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` (`main.c`) pre-fills it at connect with every gateable kind.

AP item ID = `AP_MACHINE_UNLOCK_BASE` (830, `archipelago_api.h`) + `MachineKind`. The handler accepts the block 830..859 minus 855, then calls `GateMachines_UnlockMachine(kind, /*announce=*/1)`. All in-range IDs are accepted defensively even though the apworld generates fewer. `checklist_rewards.c` calls the same entry point with `announce = 0` for the machine rewards.

Two ceilings apply, and `AP_ResolveCustomMachines` reports each overflow once:

- **`AP_MACHINE_GATE_NUM` (32)** is the mask's width. `MachineKind_IsUnlocked` returns 1 for anything past it, so kinds beyond bit 31 are permanently available rather than shifted out of range, and `GateMachines_UnlockMachine` does not persist them.
- **`AP_MACHINE_UNLOCK_NUM` (30)** is the width of the ID block, computed as `AP_BOX_UNLOCK_BASE - AP_MACHINE_UNLOCK_BASE`. The handler's upper bound is `MachineUnlock_KindNum()` (`main.h`), which clamps the registry's kind ceiling to it so a build with more registered machines stops at the block edge instead of consuming another category's IDs - without the clamp the fifth registered machine would swallow ID 860, `AP_BOX_UNLOCK_BLUE`. Vanilla fills 830-855, so registered kinds get 856-859, four of them.

`GateMachines_UnlockMachine` sets the bit, logs, and (when announcing) enqueues `"Unlocked Machine: <name>"` with `tb_api->MachineColor`, resolving the name through `GateMachines_GetName` so a registered machine gets the registry's. `VCKIND_WHEELDEDEDE` and `VCKIND_WINGMETAKNIGHT` instead announce `"Unlocked Character: King Dedede"` / `"Unlocked Character: Meta Knight"`, matching the checklist reward path.

**IDs the apworld does not generate** (`worlds/kirby_air_ride/KARItems.py`): 847 `WINGKIRBY`, 849 `WHEELNORMAL`, 850 `WHEELKIRBY` - transformation/ability-state forms with no readers anywhere, so granting them would be a genuine no-op - and 855 `WHEELVSDEDEDE`, which is out of the handler range entirely. The remaining 22 IDs are all generated as `progression`.

**845 FREE / 846 STEER are not no-ops.** They are the two Top Ride control-type machines; their bits are read by `IsTRMachineUnlocked` and `GateMachines_TRLobbyCanStart`. They never spawn on the CT field (force-zeroed via `CT_SPAWN_EXCLUDED_MASK`) and no character rides them in the 3D modes, but setting either bit unlocks the TR lobby cyclers and the start-match gate. The apworld tags both `_TR` so they only land in Top Ride locations.

**`VCKIND_WHEELVSDEDEDE`** (= 25, would be ID 855) has no readers at all: no `CharacterDesc` references it, no CT spawn path includes it, no CSS lists it, and the Vs. King Dedede stadium's availability check uses the stadium mask, not the machine mask. ID 855 falls through to the unknown-item path. **The canonical Dedede unlock is 854 `WHEELDEDEDE`** (`VCKIND_WHEELDEDEDE` = 24), which is what `CharacterDesc[CKIND_DEDEDE]` resolves to (is_bike=1, machine_kind=5 -> 19+5=24); `REWARD_KING_DEDEDE` in the checklist reward path also unlocks only this bit.

## Known Limitations

**Legendary delivery is City Trial only, and one player per run.** `AP_ITEM_GIVE_DRAGOON` / `AP_ITEM_GIVE_HYDRA` received elsewhere return 0, so the unprocessed-items list retries them until the player enters City Trial; in a local multiplayer session the machine goes to the first human the cutscene will accept, not all of them.

**Nothing gates the Air Ride start.** The Top Ride lobby has `GateMachines_TRLobbyCanStart` on both start-match sites, but Air Ride has no equivalent, so a save with zero Air Ride machines unlocked reaches an empty select screen and can still start a race on whatever the committed icon index resolves to.
