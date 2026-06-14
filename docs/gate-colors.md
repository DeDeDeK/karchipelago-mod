# Kirby Color Gating

## Overview

Kirby has 8 vanilla colors (0–7). Each can be individually locked behind AP unlock items.

Pink (color 0) is **not** hardcoded as always-unlocked — it's a normal AP unlock item like the others. The AP world is expected to ship a Pink Kirby unlock as a starting item, so the empty-mask soft fallback (`first_unlocked_color()` returning 0 when no bits are set) is unreachable in practice.

## Coverage

All three modes respect the unlock mask:

- **Air Ride** (Race, Free Run, Time Attack)
- **City Trial**
- **Top Ride** (Start Game, Free Run, Time Attack)

Coverage spans CSS color cycling (L/R), CPU random color assignment, the machine-to-color icon lookup, and per-mode color array initialization. CT has no init block, so persisted selections are validated on every CSS load.

## Hook Inventory

### HOOKCREATE

| Address | Function | Purpose |
|---------|----------|---------|
| `0x8002176c` | `GateColors_FilterResult` | Air Ride L/R color cycling (`CSS_airRide_colorChanger`) |
| `0x8002a510` | `GateColors_FilterResult` | Top Ride L/R color cycling (`CSS_topRide_colorChanger`) |
| `0x8002f350` | `GateColors_FilterResult` | City Trial L/R color cycling (`CitySelect_ChangeColor`) |
| `0x8002978c` | `GateColors_ValidateColor` | Machine-to-color icon lookup (cosmetic — keeps the CSS icon in sync with the actual unlocked color) |
| `0x800295e8` | `GateColors_ValidateAirRideColors` | AR Race CSS `color[]` init (`CSS_airRide_RaceUpdate`, `0x80028888`) |
| `0x80029e34` | `GateColors_ValidateAirRideColors` | AR Free Run / Time Attack CSS `color[]` init (`CSS_airRide_FreeTimeUpdate`, `0x80029bd8`) |
| `0x8002d06c` | `GateColors_ValidateTopRideColors` | TR `color[]` general data reset (`TopRide_InitSelectData`, `0x8002cfd8`) |
| `0x8002d704` | `GateColors_ValidateTopRideColors` | TR `color[]` Race / Start Game conditional re-assignment (`TopRide_RaceInit`, `0x8002d0ec`) |
| `0x8002db8c` | `GateColors_ValidateTopRideColors` | TR `color[]` Solo (Free Run + Time Attack) conditional re-assignment (`TopRide_SoloInit`, `0x8002d9e8`) |

The three TR hooks are all load-bearing: `TopRide_InitSelectData` writes `color[]` first; both `TopRide_RaceInit` and `TopRide_SoloInit` then conditionally re-assign `color[]` after their init-flag check, so each mode's post-init point also needs validation.

> Note: the symbols above are the current `GKYE01.map` names. The `gate_colors.c` source comments still refer to several of these by their old map placeholders — `zz_80028888_` (= `CSS_airRide_RaceUpdate`), `zz_80029bd8_` (= `CSS_airRide_FreeTimeUpdate`), `zz_8002cfd8_`/`zz_8002d0ec_`/`zz_8002d9e8_`/`zz_8002dc9c_` (the four Top Ride functions). They are the same functions.

### CPU random color

CPUs are given a **random unlocked color** in every mode via `GateColors_RandomUnlockedColor()` (collects the unlocked colors from `color_unlocked_mask`, `HSD_Randi`-picks one, falls back to the first unlocked / Pink). Without this, a CPU would inherit the per-slot `{0,1,2,3}` color default (validated to unlocked, but the same every race and prone to collapsing several CPUs onto the first-unlocked color when their defaults are locked). The color is set at each mode's CPU-aware commit point, after that mode's `color[]` validator has run, so it is the final value:

| Mode | Where | Field |
|------|-------|-------|
| Air Ride | `GateColors_SetCpuAirRideColor`, HOOKCREATE at `0x800236a8` (the CPU-slot `ply_kind = 2` write inside `loadCPU`'s per-slot loop; r29 = `airride_select_ply` base + slot, color at +0x51) | `airride_select_ply.color` (`0x15b`) |
| Top Ride | `GateMachines_FixupTRInit` (RaceInit, runs after `ValidateTopRideColors`), for panels with `panel_pkind == 2`; color at lobby +0x23 | `topride_select_ply.color` (`0x1ba`) |
| City Trial | `GateMachines_FinalizeCTMachine` (the `0x8002dea0` convergence hook), for slots with `x215 == 2` | `city_select_ply.ply_color` (`0x221`) |

Human color picks are never touched — each hook fires only on the CPU branch/slot. (Note: the `HSD_Randi` calls at `0x800236b4`/`0x80026534`/`0x8002988c` in the AR CSS are **machine-list index** picks, not color picks — `machine[slot] = available_char_list[HSD_Randi(unlocked_count)]` over the `AirRide_CheckCharacterAvailable`-gated list. See [gate-machines.md](gate-machines.md).)

### Other call sites

`GateColors_ValidateCityTrialColors()` is called from `main.c::OnPlayerSelectLoad` when the loaded minor is `MNRKIND_CITYPLYSELECT` (10, `scene.h`). It validates `gd->city_select_ply.ply_color[4]` (`GameData + 0x221`) against the unlock mask. CT has no init block to hook (unlike AR's `CSS_airRide_RaceUpdate` / `CSS_airRide_FreeTimeUpdate` and TR's three init paths), so the `OnPlayerSelectLoad` call is the sole validation point for CT.

There is no `OnMainMenuLoad` or `OnSceneChange` color call. AR and TR are covered exclusively by their CSS init hooks above.

## The icon[] vs color[] dichotomy

The Air Ride select struct (`airride_select_ply` at `GameData + 0x108`, see `game.h`) carries two distinct color fields per slot. The AR CSS code addresses this struct through a base register holding `GameData + 0x10a`, so the in-code store offsets are smaller than the GameData-relative ones:

| Field | Offset from GameData | Offset from CSS base (`GameData + 0x10a`) | Purpose |
|-------|----------------------|-------------------------------------------|---------|
| `icon[4]`  | `0x137` | `+0x2d` (e.g. `stb r3,45(r28)` at `0x8002978c`) | CSS machine icon display color (cosmetic) |
| `color[4]` | `0x15b` | `+0x51` (e.g. `stb r22,81(r25)`) | Actual in-game Kirby color, L/R cycling target |

`color[]` is what determines the in-game color and what L/R cycles. `icon[]` is purely the CSS icon rendering color and is never copied to `color[]`. The icon-validate hook at `0x8002978c` is therefore cosmetic — it keeps the CSS icon visually consistent with the unlock state. It is intentionally retained for UX cleanliness; without it, the CSS would briefly render a locked-color machine icon for slots picked up from the machine-to-color lookup table.

`CSS_airRide_RaceUpdate` (`0x80028888`) initializes `color[0..3]` to `{0, 1, 2, 3}` inside a block guarded by `GameData[0x10b]` (read as `lbz r0,1(r31)` at `0x80029568`, then `beq 0x800295e8`). The convergence point at `0x800295e8` (`li r8, 0`) is reached on both branches (init executed or skipped), so the AR validate hook there always runs and replaces any locked entries. The same pattern applies to the alternate Free Run / Time Attack CSS at `0x80029e34` in `CSS_airRide_FreeTimeUpdate` (`0x80029bd8`).

## Top Ride init paths

The Top Ride `color[4]` lives at `topride_select_ply.color`, `GameData + 0x1ba` (`game.h`); the TR init functions reference it through a base register holding `GameData + 0x197` (the lobby-data start), so `color[]` is at `+0x23` from that base.

`TopRide_InitSelectData` at `0x8002cfd8` does the general `color[0..3] = {0,1,2,3}` reset, hooked at the convergence point `0x8002d06c` (`li r3, 0`).

`TopRide_LobbyInit` (`0x8002dc9c`) dispatches on `TopRide_GetMode()` (which returns `GameData[0x381]`, read as `lbz r3,897(r3)`):
- mode `0` (Start Game / multiplayer Race) → `TopRide_RaceInit` (`0x8002d0ec`). Conditionally re-assigns `color[0..3] = {0,1,2,3}` — the block is skipped via `beq 0x8002d704` when the `init_flag` at `GameData[0x198]` (`lbz r0,1(r31)` at `0x8002d698`) matches. Hook at `0x8002d704` (`li r7, 0`) is the convergence point reached on both branches.
- mode `1` (Free Run) or `2` (Time Attack) → `TopRide_SoloInit` (`0x8002d9e8`). Same conditional pattern (`beq 0x8002db8c` at `0x8002db58` skips the `{0,1,2,3}` block); the single hook at `0x8002db8c` (`li r28, 0`) covers both solo modes.

Both per-mode hooks fire before the visual loop reads `color[]`, so corrected values are used for display.

## Save Data

`u8 color_unlocked_mask` in `APSave` (`main.h`, accessed via the global `ap_save`) — bit `N` = color index `N`.

The mask is also exposed through the cross-mod `ArchipelagoAPI` as `AP_UNLOCK_COLOR` (`archipelago_api.c` maps `Unlock_GetMask`/`Unlock_SetMask(AP_UNLOCK_COLOR)` to `color_unlocked_mask`). When the slot option `color_gating_enabled` is false, `main.c::OnSaveInit` calls `Unlock_SetMask(AP_UNLOCK_COLOR, (1u << KIRBYCOLOR_NUM) - 1)`, setting the mask to `0b11111111` so every color is available with no AP items required.

## AP Items

8 items, one per color, IDs 880–887:

```
AP_COLOR_UNLOCK_BASE   = 880
AP_COLOR_UNLOCK_PINK   = 880   // KIRBYCOLOR_PINK
AP_COLOR_UNLOCK_YELLOW = 881   // KIRBYCOLOR_YELLOW
AP_COLOR_UNLOCK_BLUE   = 882   // KIRBYCOLOR_BLUE
AP_COLOR_UNLOCK_RED    = 883   // KIRBYCOLOR_RED
AP_COLOR_UNLOCK_GREEN  = 884   // KIRBYCOLOR_GREEN
AP_COLOR_UNLOCK_PURPLE = 885   // KIRBYCOLOR_PURPLE
AP_COLOR_UNLOCK_BROWN  = 886   // KIRBYCOLOR_BROWN
AP_COLOR_UNLOCK_WHITE  = 887   // KIRBYCOLOR_WHITE
```

Dispatched by `ap_item_handler.c` (`ap_item_id >= AP_COLOR_UNLOCK_BASE && < AP_COLOR_UNLOCK_BASE + KIRBYCOLOR_NUM`), which calls `GateColors_UnlockColor(ap_item_id - AP_COLOR_UNLOCK_BASE, /*announce=*/1)`. The vanilla checklist also grants colors via the same `GateColors_UnlockColor` entry point — `checklist_rewards.c` maps `REWARD_COLOR_GREEN`/`PURPLE`/`BROWN`/`WHITE` (colors 4–7 only) to `GateColors_UnlockColor(..., /*announce=*/0)` — so checklist rewards and AP item unlocks share the same mask.

## Mod ↔ AP Client Protocol

None. Color unlocks come in via the standard `APData.incoming_item_id` mailbox; the mod owns the mask and never publishes color state to `APData`.
