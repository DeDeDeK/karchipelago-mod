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

**City Trial** seeds `city_select_ply.ply_color[4]` (`GameData + 0x221`) with `{0,1,2,3}` at four sites, but none of them is a reliable hook point, so it is validated on CSS load instead. `CitySelect_InitSelectData` (0x80038c40) memsets the 0x8c-byte block and writes `ply_color[i] = i` at 0x80038cb0; it runs from `MainMenu_InitAllVariables`, `Gm_ResetCityTrialData`, `Gm_ResetAllData` and `CityTrial_MajorEnter` (0x8003fc5c, only when `GameData[0x399] != GameData[0x39a]`). The three CSS sub-loaders each carry their own copy - `CitySelect_LoadCityTrial` (0x80039930), `CitySelect_LoadStadium` (0x8003a278) and `CitySelect_LoadMachineSelect` (0x8003ad1c) - and all three are guarded by the same test: the block runs only when the incoming `x1d0` differs from the sub-mode being loaded, so re-entering the same CT sub-mode leaves the previous session's colors untouched.

`GateColors_ValidateCityTrialColors()` is therefore called from `main.c::OnPlayerSelectLoad` when the loaded minor is `MNRKIND_CITYPLYSELECT` (10, `scene.h`). hoshi's hook sits at 0x8003b48c in `CitySelect_MinorLoad`, after the sub-loader dispatch, so it clamps whatever the init block left behind. That is the sole CT validation point; there is no `OnMainMenuLoad` or scene-change color call, since AR and TR are covered entirely by their init hooks.

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

CPUs get a **random unlocked color** in every mode via `GateColors_RandomUnlockedColorExcept()`. It builds the unlocked set from the mask, drops the colors the other visible slots already show, and `HSD_Randi`-picks from what is left; if every unlocked color is taken it repeats one rather than failing, and an empty mask falls back to 0. `GateColors_RandomUnlockedColor()` is the no-exclusions wrapper. Without any of this a CPU would inherit the per-slot `{0,1,2,3}` default - validated to unlocked, but the same every race - and without the exclusion pass several CPUs would land on the same color whenever the unlocked set is small, which vanilla's per-slot seeding never did.

| Mode | Where | Kind field | Color field |
|------|-------|-----------|-------------|
| Air Ride | `GateColors_SetCpuAirRideColor`, hook at 0x800236a8 (r29 = `airride_select_ply` base + slot; color at +0x51). `loadCPU` runs from `CSS_airRide_chooseVehicleInputGrabber`, so this already fires on the select screen, and it walks the slots in order, leaving earlier picks visible to the exclusion pass | `airride_select_ply.slot_kind` (0x14f) | `airride_select_ply.color` (0x15b) |
| Top Ride | `GateColors_OnTopRideLobbyThink`, hook at 0x8002dd40 in `TopRide_LobbyThink`, plus `GateMachines_FixupTRInit` at both lobby init sites | `topride_select_ply.panel_pkind` (0x1b2) | `topride_select_ply.color` (0x1ba) |
| City Trial | `GateColors_OnCityTrialCpuAdded`, hook at 0x80033560 in `CitySelect_InputUpdate` (r25 = slot) | `city_select_ply.x215` (0x215) | `city_select_ply.ply_color` (0x221) |

Human color picks are never touched - each site fires only on the CPU branch or slot. CPU is kind 2 on all three screens; the value meaning "active human" is 0 on Air Ride and City Trial but 1 on Top Ride, which is what the exclusion pass keys on when deciding whether a panel is on screen.

### Where each screen is hooked

Air Ride has a CPU-aware commit point that already runs from an input grabber, and City Trial has one site that can turn a panel into a CPU at all: the `x215` 3 -> 2 branch in `CitySelect_InputUpdate`, guarded by `cmpwi r0,3` at 0x800334e8. The hook sits at 0x80033560 (`add r3, r23, r25`), between the kind store and the engine's own reload of `ply_color` two instructions later, so writing the color byte is the whole job - `CitySelect_GetColorAnimFrame` and `CitySelect_UpdatePlayer` at 0x80033570-0x80033580 repaint it. The clobbered instruction recomputes `r3` from `r23`/`r25`, both callee-saved, so the bare `bl` cannot disturb it.

Top Ride has no such site. `panel_pkind` reaches 2 from seven places across `TopRide_CSS_ReadyThink`, `TopRide_CSS_PanelThink` and `TopRide_PreGameThink`, and three of those step the kind (`+ 1` / `+ 0xff`) rather than storing a literal, so the value is not statically 2. Every one of them inlines the same repaint block instead of sharing a callee. So Top Ride compares `panel_pkind` against a mirror once per frame - but from a hook on `TopRide_LobbyThink` (0x8002dd34), minor 9's own think function, so it costs nothing on any other screen. A second hook on `TopRide_LobbyInit` (0x8002dc9c) clears the seed flag; the mirror is then filled on the lobby's first think, after the lobby's own setup has run.

Both Top Ride hooks land on `stw r31, 12(r1)`, past the `mflr`/`stw` LR save and touching only preserved registers.

### When a random pick happens

A CPU takes a fresh color **only on the frame its panel's kind becomes 2**. Cycling a CPU panel's color with L/R never triggers one, so a color the player sets by hand stands until the panel is toggled away from CPU and back. Both screens let the player do this: City Trial's input path reaches `CitySelect_ChangeColor` when `x215` is 0 for the player's own slot *or* 2 for any slot (0x80034980), and `CSS_topRide_colorChanger` takes the plain-store path for panel kinds 2 and 3 (0x8002a5bc).

City Trial gets this for free: the hook fires only on the 3 -> 2 transition, so a panel that is *already* CPU when the screen loads keeps the color it has, and a manual pick survives leaving and re-entering - `x215` and `ply_color` persist together, both reset by the same guarded init blocks, so they never disagree. Top Ride's mirror is seeded from the kinds the lobby opens with, for the same effect within a session. Top Ride has nothing to preserve across entries: `TopRide_InitSelectData` (0x8002cfd8) unconditionally reopens every panel as CPU with `color[i] = i` in its 0..3 loop at 0x8002d03c, and `GateMachines_FixupTRInit` at 0x8002d070 gives those panels their random colors right after.

Air Ride re-rolls rather than preserves, matching vanilla: `loadCPU` fills the CPU slots only when every other slot is inactive (the guard at 0x80023624-0x80023680) and re-randomizes each one's machine as well as its color, so a re-confirm regenerates the whole CPU set.

Assigning on the select screen rather than at each mode's commit point is what keeps the panel and the round in agreement. `CitySelect_InitPlayerMachines` (0x8002ddd8) has a single caller, `CitySelect_Think + 0xdf8` (0x80038888), on the start-button path two instructions before `MnLoading_CreateLeaveAnim` tears the scene down; it copies `ply_color[slot]` into `ply_desc[slot].color` at 0x8002decc. A color chosen there would be the one the round used but never the one the screen drew. `GateMachines_FinalizeCTMachine` still runs at that convergence point for the starting machine, and leaves the color alone.

### Repainting

Storing the color byte does not redraw anything. The engine's own color paths always pair the store with an update call. The City Trial hook lands inside one of those pairs and inherits it; the Top Ride mirror has to make the call itself:

| Screen | Call | Arguments |
|---|---|---|
| City Trial | `CitySelect_UpdatePlayer` (0x801354d4) | `(slot, ply_pkind[slot], frame)`, substituting anim kind 5 when `x1d0 == 2`, the slot's `x1d4` bit is clear and `ply_pkind` is 4. Only `GateColors_ValidateCityTrialColors` calls this directly - it runs after the screen has painted, so a clamped panel has to be redrawn |
| Top Ride | `TopRide_UpdatePanel` (0x80134a0c) | `(panel, panel_pkind[panel], frame)` |

`frame` is `CitySelect_GetColorAnimFrame(color)` (0x80009630) in both cases - one shared helper despite the City Trial name; `CSS_topRide_colorChanger` calls it too. Both update wrappers bail on a null menu-data pointer, so they are safe to call for a panel that has no visual. The pattern is lifted from `CitySelect_ChangeColor`'s swap branch (0x8002f434-0x8002f498), which recolors the *other* player - the same situation as recoloring a CPU nobody is cycling - and from `CSS_topRide_colorChanger` at 0x8002a5f4.
