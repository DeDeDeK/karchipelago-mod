# Kirby Color Gating

Kirby's 8 vanilla colors can be individually locked behind Archipelago unlock items. A locked color cannot be cycled to on any character select screen, is never handed to a CPU, and is replaced wherever a mode's init block seeds it.

## What Is Gated

The 8 `KirbyColor` values, one bit each in `color_unlocked_mask`. AP item ID = 880 + index.

| Bit | `KirbyColor` | AP item | `APItemId` |
|----:|--------------|--------:|------------|
| 0 | `KIRBYCOLOR_PINK` | 880 | `AP_COLOR_UNLOCK_PINK` |
| 1 | `KIRBYCOLOR_YELLOW` | 881 | `AP_COLOR_UNLOCK_YELLOW` |
| 2 | `KIRBYCOLOR_BLUE` | 882 | `AP_COLOR_UNLOCK_BLUE` |
| 3 | `KIRBYCOLOR_RED` | 883 | `AP_COLOR_UNLOCK_RED` |
| 4 | `KIRBYCOLOR_GREEN` | 884 | `AP_COLOR_UNLOCK_GREEN` |
| 5 | `KIRBYCOLOR_PURPLE` | 885 | `AP_COLOR_UNLOCK_PURPLE` |
| 6 | `KIRBYCOLOR_BROWN` | 886 | `AP_COLOR_UNLOCK_BROWN` |
| 7 | `KIRBYCOLOR_WHITE` | 887 | `AP_COLOR_UNLOCK_WHITE` |

Pink (color 0) is **not** hardcoded as always-unlocked — it is a normal AP unlock item like the others. The apworld is expected to ship Pink Kirby as a starting item, so the empty-mask soft fallback (`first_unlocked_color()` returning 0 when no bits are set) is unreachable in practice.

All three modes respect the mask: Air Ride (Race, Free Run, Time Attack), City Trial, and Top Ride (Start Game, Free Run, Time Attack). Coverage spans CSS color cycling (L/R), CPU random color assignment, the machine-to-color icon lookup, and per-mode `color[]` array initialization.

## Entry Points

**Files:** `mods/archipelago/src/gate_colors.c` / `gate_colors.h`

| Symbol | Kind | Where | Role |
|--------|------|-------|------|
| `GateColors_OnBoot()` | mod | gate_colors.c | Applies the nine hooks at boot (called from `main.c`). |
| `GateColors_FilterResult(int color_idx)` | mod (static) | gate_colors.c | Availability predicate at each L/R colorChanger convergence point. |
| `GateColors_ValidateAirRideColors(void)` | mod (static) | gate_colors.c | Rewrites `airride_select_ply.color[0..3]` after an AR CSS init block. |
| `GateColors_ValidateTopRideColors(void)` | mod (static) | gate_colors.c | Rewrites `topride_select_ply.color[0..3]` after a TR init block. |
| `GateColors_ValidateCityTrialColors(void)` | mod | gate_colors.c | Rewrites `city_select_ply.ply_color[0..3]`; called from `main.c::OnPlayerSelectLoad`. |
| `GateColors_RandomUnlockedColor(void)` | mod | gate_colors.c | Random unlocked color; also called by `gate_machines.c`. |
| `GateColors_SetCpuAirRideColor(u8 *slot_base)` | mod | gate_colors.c | Assigns the CPU color inside `loadCPU`'s per-slot loop. |
| `GateColors_UnlockColor(int color_idx, int announce)` | mod | gate_colors.c | Sets the unlock bit; optional textbox. Called from `ap_item_handler.c` and `checklist_rewards.c`. |
| `CSS_airRide_colorChanger` | game | 0x80021654 | Air Ride L/R color cycler. |
| `CSS_topRide_colorChanger` | game | 0x8002a400 | Top Ride L/R color cycler. |
| `CitySelect_ChangeColor` | game | 0x8002f238 | City Trial L/R color cycler. |
| `CSS_airRide_RaceUpdate` | game | 0x80028888 | AR Race CSS update; owns a `color[]` init block. |
| `CSS_airRide_FreeTimeUpdate` | game | 0x80029bd8 | AR Free Run / Time Attack CSS update; own `color[]` init block. |
| `TopRide_InitSelectData` | game | 0x8002cfd8 | TR general lobby data reset. |
| `TopRide_RaceInit` | game | 0x8002d0ec | TR Start Game / multiplayer race init. |
| `TopRide_SoloInit` | game | 0x8002d9e8 | TR Free Run / Time Attack init. |
| `loadCPU` | game | 0x80023600 | Air Ride CPU slot setup. |

## Game System

### `icon[]` vs `color[]`

The Air Ride select struct (`airride_select_ply` at `GameData + 0x108`, see `game.h`) carries two per-slot byte arrays that are easy to confuse. The AR CSS code addresses this struct through a base register holding `GameData + 0x10a`, so the in-code store offsets are smaller than the GameData-relative ones:

| Field | Offset from GameData | Offset from CSS base (`GameData + 0x10a`) | Purpose |
|-------|----------------------|-------------------------------------------|---------|
| `icon[4]`  | `0x137` | `+0x2d` (e.g. `stb r3,45(r28)` at `0x8002978c`) | Index into the available-machine list at `GameData + 0x170` (count at `+0x16f`) |
| `color[4]` | `0x15b` | `+0x51` (e.g. `stb r22,81(r25)`) | Actual in-game Kirby color, L/R cycling target |

Only `color[]` holds a `KirbyColor`. Despite the name, `icon[]` is **not** a color: `CSS_airRide_RaceUpdate` derives it by scanning the available-machine list for the slot's `machine_kind` (`+0x61`) and storing the matching **list position**, falling back to the entry whose value is 1 (Warp Star) and then to 0. Color gating must therefore never touch `icon[]` — a color-mask test applied to a list index is a type confusion that corrupts the CSS icon. Machine availability is gated separately by `gate_machines.c`.

### Air Ride init paths

`CSS_airRide_RaceUpdate` (0x80028888) initializes `color[0..3]` to `{0, 1, 2, 3}` inside a block guarded by `GameData[0x10b]` (read as `lbz r0,1(r31)` at `0x80029568`, then `beq 0x800295e8`). The convergence point at `0x800295e8` (`li r8, 0`) is reached on both branches (init executed or skipped), so a hook there always runs and can replace any locked entries. The same pattern applies to the alternate Free Run / Time Attack CSS at `0x80029e34` in `CSS_airRide_FreeTimeUpdate` (0x80029bd8).

### Top Ride init paths

The Top Ride `color[4]` lives at `topride_select_ply.color`, `GameData + 0x1ba` (`game.h`); the TR init functions reference it through a base register holding `GameData + 0x197` (the lobby-data start), so `color[]` is at `+0x23` from that base.

`TopRide_InitSelectData` (0x8002cfd8) does the general `color[0..3] = {0,1,2,3}` reset; its convergence point is `0x8002d06c` (`li r3, 0`).

`TopRide_LobbyInit` (0x8002dc9c) dispatches on `TopRide_GetMode()` (0x8003ea9c, which returns `GameData[0x381]`, read as `lbz r3,897(r3)`):

- mode `0` (Start Game / multiplayer Race) → `TopRide_RaceInit` (0x8002d0ec). Conditionally re-assigns `color[0..3] = {0,1,2,3}` — the block is skipped via `beq 0x8002d704` when the `init_flag` at `GameData[0x198]` (`lbz r0,1(r31)` at `0x8002d698`) matches. `0x8002d704` (`li r7, 0`) is the convergence point reached on both branches.
- mode `1` (Free Run) or `2` (Time Attack) → `TopRide_SoloInit` (0x8002d9e8). Same conditional pattern (`beq 0x8002db8c` at `0x8002db58` skips the `{0,1,2,3}` block); the single convergence point `0x8002db8c` (`li r28, 0`) covers both solo modes.

All three TR sites are load-bearing: `TopRide_InitSelectData` writes `color[]` first; both `TopRide_RaceInit` and `TopRide_SoloInit` then conditionally re-assign it after their init-flag check.

City Trial has no equivalent init block. Its `city_select_ply.ply_color[4]` (`GameData + 0x221`) persists across sessions, so it must be validated on every CSS load instead.

## Implementation

### Hooks

Nine `CODEPATCH_HOOKCREATE`s, all applied in `GateColors_OnBoot`.

| Address | Hook body | Purpose |
|---------|-----------|---------|
| `0x8002176c` | `GateColors_FilterResult` | Air Ride L/R color cycling (`CSS_airRide_colorChanger`); clobbered `extsb. r0, r3`, r23 = candidate color |
| `0x8002a510` | `GateColors_FilterResult` | Top Ride L/R color cycling (`CSS_topRide_colorChanger`); clobbered `extsb. r0, r0`, r23 = candidate, result returned in r0 |
| `0x8002f350` | `GateColors_FilterResult` | City Trial L/R color cycling (`CitySelect_ChangeColor`); clobbered `extsb. r0, r3`, r30 = candidate |
| `0x800295e8` | `GateColors_ValidateAirRideColors` | AR Race CSS `color[]` init convergence |
| `0x80029e34` | `GateColors_ValidateAirRideColors` | AR Free Run / Time Attack CSS `color[]` init convergence |
| `0x8002d06c` | `GateColors_ValidateTopRideColors` | TR general data reset convergence |
| `0x8002d704` | `GateColors_ValidateTopRideColors` | TR Race / Start Game re-assignment convergence |
| `0x8002db8c` | `GateColors_ValidateTopRideColors` | TR Solo (Free Run + Time Attack) re-assignment convergence |
| `0x800236a8` | `GateColors_SetCpuAirRideColor` | AR CPU-slot color (`stb r0, 69(r29)`, the CPU-slot kind write in `loadCPU`'s per-slot loop); epilogue restores `li r0, 2` for the re-executed store |

The three L/R hooks sit at each cycler's convergence point, where all vanilla paths (colors 0–3 hardcoded, 4–7 checklist) merge, so the mask overrides outright. Both per-mode TR hooks fire before the visual loop reads `color[]`, so corrected values are used for display.

### CPU random color

CPUs are given a **random unlocked color** in every mode via `GateColors_RandomUnlockedColor()` (collects the unlocked colors from `color_unlocked_mask`, `HSD_Randi`-picks one, falls back to 0). Without this, a CPU would inherit the per-slot `{0,1,2,3}` default — validated to unlocked, but the same every race and prone to collapsing several CPUs onto the first-unlocked color when their defaults are locked. The color is set at each mode's CPU-aware commit point, after that mode's `color[]` validator has run, so it is the final value:

| Mode | Where | Field |
|------|-------|-------|
| Air Ride | `GateColors_SetCpuAirRideColor`, hook at `0x800236a8` (r29 = `airride_select_ply` base + slot; color at +0x51) | `airride_select_ply.color` (`0x15b`) |
| Top Ride | `GateMachines_FixupTRInit` (RaceInit site, runs after `ValidateTopRideColors`), for panels with `panel_pkind == 2`; color at lobby +0x23 | `topride_select_ply.color` (`0x1ba`) |
| City Trial | `GateMachines_FinalizeCTMachine` (the `0x8002dea0` convergence hook), for slots with `x215 == 2` | `city_select_ply.ply_color` (`0x221`) |

Human color picks are never touched — each hook fires only on the CPU branch or slot.

The `HSD_Randi` calls at `0x800236b4` / `0x80026534` / `0x8002988c` in the AR CSS are **machine-list index** picks, not color picks (`machine[slot] = available_char_list[HSD_Randi(unlocked_count)]` over the `AirRide_CheckCharacterAvailable`-gated list), so they are `gate-machines.md`'s concern, not this file's.

### City Trial validation

`GateColors_ValidateCityTrialColors()` is called from `main.c::OnPlayerSelectLoad` when the loaded minor is `MNRKIND_CITYPLYSELECT` (10, `scene.h`). It validates `gd->city_select_ply.ply_color[4]` against the unlock mask. CT has no init block to hook, so this is the sole validation point for CT. There is no `OnMainMenuLoad` or `OnSceneChange` color call — AR and TR are covered exclusively by their CSS init hooks.

## Save Data

`u8 color_unlocked_mask` in `APSave` (`main.h`, accessed via the global `ap_save`) — bit N = color index N.

The mask is exposed through the cross-mod `ArchipelagoAPI` as `AP_UNLOCK_COLOR` (`archipelago_api.c` maps `Unlock_GetMask`/`Unlock_SetMask(AP_UNLOCK_COLOR)` to `color_unlocked_mask`). When the slot option `color_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` in `main.c` calls `Unlock_SetMask(AP_UNLOCK_COLOR, (1u << KIRBYCOLOR_NUM) - 1)` at connect, setting the mask to `0b11111111` so every color is available with no AP items required.

## AP Items

8 AP items, `AP_COLOR_UNLOCK_BASE` (880, `archipelago_api.h`) + `KirbyColor` index → IDs 880–887 (see the table above).

`ap_item_handler.c` routes IDs in `[880, 880 + KIRBYCOLOR_NUM)` to `GateColors_UnlockColor(id - AP_COLOR_UNLOCK_BASE, /*announce=*/1)`, which sets the bit, logs, and enqueues `"Unlocked Color: <name> Kirby"` via `tb_api->EnqueueColoredNoun` with `tb_api->KirbyColors[color_idx]`.

The vanilla checklist grants colors through the same entry point: `checklist_rewards.c` maps `REWARD_COLOR_GREEN`/`PURPLE`/`BROWN`/`WHITE` (colors 4–7 only) to `GateColors_UnlockColor(..., /*announce=*/0)`, so checklist rewards and AP item unlocks share one mask.
