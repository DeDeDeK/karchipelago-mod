# Kirby Color Gating

Kirby's 8 vanilla colors can be individually locked behind Archipelago unlock items. AP items 880-887 (`AP_COLOR_UNLOCK_BASE` + `KirbyColor`) route through `ap_item_handler.c` to `GateColors_UnlockColor(idx, /*announce=*/1)`, which sets a bit in `APSave.color_unlocked_mask` (a `u8`) and enqueues `"Unlocked Color: <name> Kirby"` with `tb_api->KirbyColors[idx]`. A locked color cannot be cycled to on any character select screen, is never handed to a CPU, and is replaced wherever a mode's init block seeds it.

**File:** `mods/archipelago/src/gate_colors.c`.

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

Pink (color 0) is **not** hardcoded as always-unlocked - it is a normal AP unlock item like the others. The apworld is expected to ship Pink Kirby as a starting item, so the empty-mask soft fallback (`first_unlocked_color()` returning 0 when no bits are set) is unreachable in practice.

The vanilla checklist grants colors through the same entry point: `checklist_rewards.c` maps `REWARD_COLOR_GREEN`/`PURPLE`/`BROWN`/`WHITE` (colors 4-7 only) to `GateColors_UnlockColor(..., /*announce=*/0)`, so checklist rewards and AP unlocks share one mask.

The mask is exposed through `ArchipelagoAPI` as `AP_UNLOCK_COLOR`. When the slot option `color_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` (`main.c`) sets it to all-1s at connect.

All three modes are covered - Air Ride (Race, Free Run, Time Attack), City Trial, and Top Ride (Start Game, Free Run, Time Attack) - across CSS color cycling, CPU color assignment, and per-mode `color[]` array initialization.

## `icon[]` vs `color[]`

The Air Ride select struct (`airride_select_ply` at `GameData + 0x108`, `game.h`) carries two per-slot byte arrays that are easy to confuse. The AR CSS code addresses the struct through a base register holding `GameData + 0x10a`, so in-code store offsets are smaller than the GameData-relative ones:

| Field | Offset from GameData | Offset from CSS base (`GameData + 0x10a`) | Purpose |
|-------|----------------------|-------------------------------------------|---------|
| `icon[4]`  | 0x137 | +0x2d (e.g. `stb r3,45(r28)` at 0x8002978c) | Index into the available-machine list at `GameData + 0x170` (count at +0x16f) |
| `color[4]` | 0x15b | +0x51 (e.g. `stb r22,81(r25)`) | Actual in-game Kirby color, L/R cycling target |

Only `color[]` holds a `KirbyColor`. Despite the name, `icon[]` is **not** a color: `CSS_airRide_RaceUpdate` derives it by scanning the available-machine list for the slot's `machine_kind` (+0x61) and storing the matching **list position**, falling back to the entry whose value is 1 (Warp Star) and then to 0. Color gating must never touch `icon[]` - a color-mask test applied to a list index is a type confusion that corrupts the CSS icon. Machine availability is a separate concern, handled in `gate_machines.c`.

For the same reason, the `HSD_Randi` calls at 0x800236b4 / 0x80026534 / 0x8002988c in the AR CSS are left alone here: they pick a machine-list index (`machine[slot] = available_char_list[HSD_Randi(unlocked_count)]` over the `AirRide_CheckCharacterAvailable`-gated list), not a color.

## Init Paths and Convergence Points

Each mode seeds `color[0..3]` with `{0, 1, 2, 3}` inside a conditional block, so the gate hooks the point where both branches merge - the corrected values are then in place before anything reads `color[]` for display.

**Air Ride.** `CSS_airRide_RaceUpdate` (0x80028888) runs its init block guarded by `GameData[0x10b]` (`lbz r0,1(r31)` at 0x80029568, then `beq 0x800295e8`); 0x800295e8 (`li r8, 0`) is reached whether the block ran or not. `CSS_airRide_FreeTimeUpdate` (0x80029bd8) has the same pattern with its convergence at 0x80029e34.

**Top Ride.** The TR `color[4]` lives at `topride_select_ply.color`, `GameData + 0x1ba`; the init functions reference it through a base register holding `GameData + 0x197` (the lobby-data start), so `color[]` is at +0x23 from that base. Three sites all write it and are all load-bearing:

- `TopRide_InitSelectData` (0x8002cfd8) does the general reset; convergence at 0x8002d06c (`li r3, 0`).
- `TopRide_LobbyInit` (0x8002dc9c) then dispatches on `TopRide_GetMode()` (0x8003ea9c, returning `GameData[0x381]`). Mode 0 (Start Game / multiplayer race) goes to `TopRide_RaceInit` (0x8002d0ec), which re-assigns `{0,1,2,3}` unless the `init_flag` at `GameData[0x198]` (`lbz r0,1(r31)` at 0x8002d698) makes it `beq 0x8002d704`; 0x8002d704 (`li r7, 0`) is the convergence.
- Mode 1 (Free Run) or 2 (Time Attack) goes to `TopRide_SoloInit` (0x8002d9e8), same conditional pattern (`beq 0x8002db8c` at 0x8002db58), single convergence at 0x8002db8c (`li r28, 0`) covering both solo modes.

**City Trial** has no init block at all. Its `city_select_ply.ply_color[4]` (`GameData + 0x221`) persists across sessions, so it is validated on CSS load instead: `GateColors_ValidateCityTrialColors()` is called from `main.c::OnPlayerSelectLoad` when the loaded minor is `MNRKIND_CITYPLYSELECT` (10, `scene.h`). That is the sole CT validation point; there is no `OnMainMenuLoad` or scene-change color call, since AR and TR are covered entirely by their init hooks.

## Hooks

Nine `CODEPATCH_HOOKCREATE`s, all applied in `GateColors_OnBoot`.

| Address | Hook body | Purpose |
|---------|-----------|---------|
| 0x8002176c | `GateColors_FilterResult` | Air Ride L/R color cycling (`CSS_airRide_colorChanger`, 0x80021654); clobbered `extsb. r0, r3`, r23 = candidate color |
| 0x8002a510 | `GateColors_FilterResult` | Top Ride L/R color cycling (`CSS_topRide_colorChanger`, 0x8002a400); clobbered `extsb. r0, r0`, r23 = candidate, result returned in r0 |
| 0x8002f350 | `GateColors_FilterResult` | City Trial L/R color cycling (`CitySelect_ChangeColor`, 0x8002f238); clobbered `extsb. r0, r3`, r30 = candidate |
| 0x800295e8 | `GateColors_ValidateAirRideColors` | AR Race CSS `color[]` init convergence |
| 0x80029e34 | `GateColors_ValidateAirRideColors` | AR Free Run / Time Attack CSS `color[]` init convergence |
| 0x8002d06c | `GateColors_ValidateTopRideColors` | TR general data reset convergence |
| 0x8002d704 | `GateColors_ValidateTopRideColors` | TR Race / Start Game re-assignment convergence |
| 0x8002db8c | `GateColors_ValidateTopRideColors` | TR Solo (Free Run + Time Attack) re-assignment convergence |
| 0x800236a8 | `GateColors_SetCpuAirRideColor` | AR CPU-slot color (`stb r0, 69(r29)`, the CPU-slot kind write in `loadCPU`'s per-slot loop, 0x80023600); epilogue restores `li r0, 2` for the re-executed store |

The three L/R hooks sit at each cycler's convergence point, where all vanilla paths (colors 0-3 hardcoded, 4-7 checklist) merge, so the mask overrides outright.

## CPU Colors

CPUs get a **random unlocked color** in every mode via `GateColors_RandomUnlockedColor()` (collects the unlocked colors from the mask, `HSD_Randi`-picks one, falls back to 0). Without it a CPU would inherit the per-slot `{0,1,2,3}` default - validated to unlocked, but the same every race, and prone to collapsing several CPUs onto the first-unlocked color when their defaults are locked. The assignment happens at each mode's CPU-aware commit point, after that mode's `color[]` validator has run, so it is the final value:

| Mode | Where | Field |
|------|-------|-------|
| Air Ride | `GateColors_SetCpuAirRideColor`, hook at 0x800236a8 (r29 = `airride_select_ply` base + slot; color at +0x51) | `airride_select_ply.color` (0x15b) |
| Top Ride | `GateMachines_FixupTRInit` (RaceInit site, runs after `ValidateTopRideColors`), for panels with `panel_pkind == 2`; color at lobby +0x23 | `topride_select_ply.color` (0x1ba) |
| City Trial | `GateMachines_FinalizeCTMachine` (the 0x8002dea0 convergence hook), for slots with `x215 == 2` | `city_select_ply.ply_color` (0x221) |

Human color picks are never touched - each hook fires only on the CPU branch or slot.
