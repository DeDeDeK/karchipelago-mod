# Character Select Screens

The player select screens - where each player picks a machine, a color and, for CPUs, a level -
come in three independent implementations, one per mode, each a minor of that mode's major. They
share a shape: per-player input, L/R color cycling against an unlock check, and machine icons drawn
from an HSD archive by material animation. A mod that adds, removes or gates machines and colors
has to work with the tables and fixed-size layouts described here.

| Mode | Minor | `cb_Load` | Per-player data |
|---|---|---|---|
| Air Ride | 8 (`MNRKIND_AIRRIDEPLYSELECT`) | `CSS_airRide_ModeDispatch` (0x8002a1b0) | `airride_select_ply` (`GameData+0x108`, addressed from +0x10a) |
| City Trial | 10 (`MNRKIND_CITYPLYSELECT`) | `CitySelect_MinorLoad` (0x8003b2c0) | `city_select_ply` (`GameData+0x1d0`) |
| Top Ride | 9 (`MNRKIND_TOPRIDEPLYSELECT`) | `TopRide_LobbyInit` (0x8002dc9c) | `topride_select_ply` (`GameData+0x160`; its lobby data starts at +0x197) |

## Characters and Machines

A select screen offers `CharacterKind`s (20). Each maps to a machine through the `CharacterDesc`
table at 0x80495814 - 3 bytes per kind, `rider_kind`, `is_bike`, `machine_kind`, returned by
`Character_GetDesc` (0x8000b9dc). For a star (`is_bike` 0), `machine_kind` is the `VCKIND`; for a
bike it is relative, and the real kind is `VCKIND_WHEELNORMAL` (19) plus it. Six `VCKIND`s have no
character and are spawn or transformation only: FREE, STEER, WINGKIRBY, WHEELNORMAL, WHEELKIRBY
and WHEELVSDEDEDE.

`AirRide_CheckCharacterAvailable` (0x8002090c) decides what Air Ride offers: an Air Ride checklist
reward checked with `ClearChecker_CheckUnlocked` (0x80049e24), or with
`Checklist_CheckCachedUnlock_AirRide` (0x80007e34) during a LAN session.

| CharacterKind | Rider | `is_bike` | `machine_kind` | Machine | Air Ride |
|---|---|---|---|---|---|
| 0 `CKIND_COMPACT` | Kirby | 0 | 1 | `VCKIND_COMPACT` (1) | never |
| 1 `CKIND_WARP` | Kirby | 0 | 0 | `VCKIND_WARP` (0) | always |
| 2 `CKIND_TURBO` | Kirby | 0 | 12 | `VCKIND_TURBO` (12) | reward 0x1d |
| 3 `CKIND_FORMULA` | Kirby | 0 | 7 | `VCKIND_FORMULA` (7) | reward 0x19 |
| 4 `CKIND_SLICK` | Kirby | 0 | 6 | `VCKIND_SLICK` (6) | reward 0x18 |
| 5 `CKIND_SWERVE` | Kirby | 0 | 11 | `VCKIND_SWERVE` (11) | reward 0x15 |
| 6 `CKIND_WAGON` | Kirby | 0 | 9 | `VCKIND_WAGON` (9) | reward 0x14 |
| 7 `CKIND_BULK` | Kirby | 0 | 5 | `VCKIND_BULK` (5) | reward 0x16 |
| 8 `CKIND_SHADOW` | Kirby | 0 | 3 | `VCKIND_SHADOW` (3) | reward 0x1a |
| 9 `CKIND_WINGED` | Kirby | 0 | 2 | `VCKIND_WINGED` (2) | reward 0x13 |
| 10 `CKIND_JET` | Kirby | 0 | 13 | `VCKIND_JET` (13) | reward 0x1e |
| 11 `CKIND_ROCKET` | Kirby | 0 | 10 | `VCKIND_ROCKET` (10) | reward 0x1c |
| 12 `CKIND_WHEELIESCOOTER` | Kirby | 1 | 4 | `VCKIND_WHEELIESCOOTER` (23) | reward 0x1b |
| 13 `CKIND_WHEELIEBIKE` | Kirby | 1 | 2 | `VCKIND_WHEELIEBIKE` (21) | reward 0x17 |
| 14 `CKIND_REXWHEELIE` | Kirby | 1 | 3 | `VCKIND_REXWHEELIE` (22) | reward 0x1f |
| 15 `CKIND_DRAGOON` | Kirby | 0 | 8 | `VCKIND_DRAGOON` (8) | never |
| 16 `CKIND_HYDRA` | Kirby | 0 | 4 | `VCKIND_HYDRA` (4) | never |
| 17 `CKIND_FLIGHT` | Kirby | 0 | 14 | `VCKIND_FLIGHT` (14) | never |
| 18 `CKIND_DEDEDE` | King Dedede | 1 | 5 | `VCKIND_WHEELDEDEDE` (24) | reward 0x20 |
| 19 `CKIND_METAKNIGHT` | Meta Knight | 0 | 18 | `VCKIND_WINGMETAKNIGHT` (18) | reward 0x21 |

Two more byte tables order the icons: the Air Ride grid at 0x80495800 (two rows of 10, read by
`SelIcon_GetCKind` 0x8000b9bc as `row * 10 + column`) and a linear strip at 0x804957ec
(`SelIcon_GetCKindLinear` 0x8000b9a8). `Machine_GetCKind` (0x8000b9f4) maps a machine back to its
character, through a star table at 0x80495850 and a bike table at `r13 - 0x7fbc`. The strip, the
grid, `CharacterDesc` and the star table sit back to back with no slack, so a 21st character
cannot be appended in place: the tables have to move, with every `lis`/`addi` pair that loads
them.

## The Icon List

Each screen keeps the icons it offers in its own block of `GameData`, at the same shape off a base
of 0x10a (Air Ride) or 0x1d0 (City Trial): a count at +0x65 and one `CharacterKind` per icon from
+0x66. The list is built once per screen load - by `AirRide_PopulateSelectIcons` (0x80020a08) on
Air Ride, the tail of `CitySelect_CreateMachineIcons` (0x8002e3c4) on City Trial - and no engine
code reads past the count. Both builders pack the icons into two 10-byte stack rows, so neither
can carry an 11th column.

The bytes right after each 20-entry list are live. Air Ride keeps its two-row layout flag at +0x7a
(`GameData+0x184`) and its debug-grid flag at +0x7b (`+0x185`), then four 4-byte per-slot arrays;
City Trial keeps its debug-grid flag at +0x7a (`+0x24a`) and the same four arrays from +0x7b. The
arrays are controller-claim state for LAN play (each console's claimed pads, and each slot's
ordinal, console and pad), touched only behind `Net_IsSessionActive` (0x8007b650) once the screen
has initialized them. While a debug-grid flag is 1, that screen's color changer treats every color
as unlocked.

**Layout.** Both screens place their icons against 20 sibling anchor joints in a dedicated model -
`ScMenSelplyIpos_scene_models` in `MnSelplyAll.dat`, `ScMenSelplyIposCt_scene_models` in
`MnSelplyctAll.dat` - posed by an animation whose **frame is the icon count**.
`AirRideSelect_LayoutIcons` (0x80133f68) and `CitySelect_LayoutMachineIcons` (0x801355f4) set the
frame and read each anchor's world position into the ipos GObj's userdata
(`AirRideSelectIposData` / `CitySelectIposData`: anchors at +0x10, one `Vec3` per icon at +0x60).
At frame N the first `ceil(N/2)` anchors form the top row and the rest the bottom; unused anchors
park offscreen. Nine icons or fewer make one row; from 10 up the two rows interleave, half a column
apart, with spacing shrinking to keep both inside a fixed half-width `H` (30.62 on Air Ride, 25.20
on City Trial):

```
top = ceil(N/2)            spacing = 2H / max(top - 1, (N - top) - 0.5)
top row    x = -H + i * spacing
bottom row x = -H + spacing/2 + i * spacing
```

The anchors have no key past frame 20 and there is no 21st anchor, and the icon GObjs go into a
20-entry `sicon_gobj[]` array in `ScMenuCommon` (Air Ride +0x520, City Trial +0x80c) written by one
unguarded store, so more than 20 icons needs its own layout and storage.

**Cursor.** `AirRideSelect_Cursor1InputThink` (0x80023b68) and `CitySelect_Cursor1InputThink`
(0x800312fc) take a one-row path below 10 icons (left and right, wrapping) and from 10 up split
the list at `ceil(N/2)`, the same split as the anchors, adding up and down.

## Icon and Name Plate Art

The Air Ride icon model is `ScMenSelplySicon_scene_models` in `MnSelplyAll.dat` (City Trial's is
`ScMenSelplySiconCt_scene_models` in `MnSelplyctAll.dat`): a container root over one child with two
DObjs, a 64x64 CMPR image and a 32x32 RGB5A3 decoration. Its material animation bank 0 ends in a
`TexAnim` with 20 images and 20 palettes, one per `CharacterKind`, and **the animation frame is the
CharacterKind**: the icon creator sets the frame to the kind and applies it. Every slot has real
art, including Compact Star's, which vanilla Air Ride never offers.

The 80x48 name plate comes from a TexAnim bank shared by both select screens and the four results
screens, and its frame is not simply the kind:

```
frame = (ckind == CKIND_DEDEDE)     ? color + 20
      : (ckind == CKIND_METAKNIGHT) ? color + 30
      : ckind
```

The bank has keys at frames 0-17, 20 and 30, so every King Dedede color shows one plate and every
Meta Knight color another. Eight functions carry a copy of that arithmetic: the color and character
setters of each select screen (`AirRideSelect_SetSIcon2Color` 0x80151ab4,
`AirRideSelect_SetSIcon2Character` 0x80151b78, `CitySelect_SetSIcon2Color` 0x8015c574,
`CitySelect_SetSIcon2Character` 0x8015c638) and the big-icon creator of each results screen
(`MnResult_CreateSiconBig` 0x80167250, `MnResult2_CreateSiconBig` 0x8016aff4,
`MnResult4_CreateSiconBig` 0x8016e924, `MnResultCt_CreateSiconBig` 0x80177ae8).

## Colors

| Mode | Color changer | Merge point |
|---|---|---|
| Air Ride | `CSS_airRide_colorChanger` (0x80021654) | 0x8002176c |
| Top Ride | `CSS_topRide_colorChanger` (0x8002a400) | 0x8002a510 |
| City Trial | `CitySelect_ChangeColor` (0x8002f238) | 0x8002f350 |

L and R step a slot's color through 0-7 and test the candidate: colors 0-3 are always available,
4-7 are checklist rewards 15-18 of the mode, checked the same way as machines. A color another
human holds is skipped; one held by an inactive or CPU slot is swapped to that slot. Every branch of the test merges at one instruction per function, the
natural place to override availability.

## Air Ride (Minor 8)

`CSS_airRide_ModeDispatch` sets up the screen and starts the menu music; the per-frame think runs
`CSS_airRide_Main` (0x8002860c), which dispatches on `Gm_GetAirRideMode()`: Race runs
`CSS_airRide_RaceUpdate` (0x80028888), Time Attack and Free Run share `CSS_airRide_FreeTimeUpdate`
(0x80029bd8). Input is split by state across `CSS_airRide_inputGrabber` (0x80026f20),
`CSS_airRide_chooseVehicleInputGrabber` (0x80024d04), `CSS_airRide_inputGrabberReadyScreen`
(0x80026110) and `CSS_airRide_FreeRuInputGrabber` (0x80027fa4).

`CSS_airRide_InitSelectData` (0x80028754) clears the block and sets the init flag at
`GameData+0x10b`; an update that finds the flag set resets `color[]` to 0-3 and the per-slot
state, then clears it. Race and Free/Time each carry their own copy of that block, so color
validation has to cover both.

Fields worth knowing, by `GameData` offset: 0x113 rendering state, 0x12f the slot-to-player map,
0x137 `icon[]` (the grid index), 0x14f the per-slot state (0 active human, 2 CPU needing a pick,
3 inactive), 0x15b `color[]`, 0x16b the slot's machine kind, then the icon list at 0x16f and its
flags at 0x184 and 0x185. `icon[]` and `color[]` are separate: `color[]` is what L/R cycles and what
becomes the rider's color, and `icon[]` is never copied into it. The range 0x160-0x196 is Air Ride
state even though the header names it as the head of `topride_select_ply`.

**CPU machines.** `loadCPU` (0x80023600) and the ready-screen and setup paths at 0x80026534 and
0x8002988c pick a CPU's machine as `HSD_Randi(count)` into the available list. The list holds only
available machines, so gating the list gates the CPU picks too. `CSS_airRide_RaceUpdate` maps a
slot's machine back to its icon by scanning the list, falling back to the Warp Star.

## City Trial (Minor 10)

`CitySelect_MinorLoad` dispatches on `Gm_GetCityMode()` to `CitySelect_LoadCityTrial` (0x80038d6c),
`CitySelect_LoadStadium` (0x80039e20) or `CitySelect_LoadMachineSelect` (0x8003a904, Free Run).
`CitySelect_Think` (0x80037a90) is the main loop, with input in `CitySelect_InputUpdate`
(0x80032d34) and per-player `CitySelect_PlayerThink` (0x800348f8); `CitySelect_InitPlayerMachines`
(0x8002ddd8) makes the starting assignments. `city_select_ply` holds `player_state[4]` (+0x1f1: 0
inactive, 1 icon select, 2 handicap or CPU bar, 3 player element), `ply_pkind[4]` (+0x21d),
`ply_color[4]` (+0x221), `ply_cpu_level[4]` (+0x22d, 0-8 shown as 1-9), `ply_icon_ckind[4]`
(+0x231) and the icon list (+0x235 count, +0x236 kinds).

## Top Ride (Minor 9)

The Top Ride lobby is simpler: `TopRide_LobbyInit` loads it, `TopRide_LobbyThink` (0x8002dd34)
runs it, `TopRide_PreGameThink` (0x8002c06c) handles course and character selection, and
`TopRide_CSS_PanelThink` (0x8002b8a8) edits the bottom panel. Its data in `topride_select_ply`:
`color[4]` (+0x1ba), `panel_cpu_level[4]` (+0x1be), `panel_handicap[4]` (+0x1c2) and
`panel_machine[4]` (+0x1c6, the Control Type row: 0 Free Star, 1 Steer Star). The course is
`GameData.topride_selected_course` (+0x374).

## CPU Level and Handicap Bars

CPU level and handicap are drawn as a row of notch joints, not digits; the level shown is the count
of lit notches. City Trial and Air Ride show 9 (`cpu_level` 0-8), Top Ride 5 (`panel_cpu_level`
0-4, committed through `TopRide_SetCpuLevel` 0x8000be74).

- City Trial's bars go through `CSS_SetBarLevel` (0x80135694, arguments slot, bar kind and level; bar kind
  0 is handicap, 1 CPU level) into `CSS_SetBarLevel_Impl` (0x8015ca8c), which lights notches 0 to
  `level` and blanks the rest up to 8. The notch count is hardcoded to 9, so a level above 8 writes
  past the bar's joint array. Air Ride draws the same 1-9 bar through its own renderer.
- Top Ride's `TopRide_CSS_PanelThink` raises `panel_cpu_level` only while it is below 4 (0x8002bf8c)
  and draws `4 - level`; the 4 is baked into the clamp, the draw math and the notch count.

The notches are fixed art in `MnSelplyAll.dat`, `MnSelplyctAll.dat` and `MnSelplym2dAll.dat`.
Raising a maximum therefore takes the input clamp, the renderer bounds and new notch joints
together; a raised clamp alone is an out-of-bounds write. Harder CPUs do not need a wider bar: the
rider AI reads its difficulty from `CpuData+0x22`, which `Rider_CPUInit` (0x80262d6c) sets from the
level passed in (0-8; it asserts above 8), so a mod can remap the existing levels onto a steeper
curve instead.

## Title Screen Demo Machines

The title screen's attract rounds pick their own machines: `TitleScreen_MinorExit` calls
`TitleScreen_SetupDemoMachines` (0x8000dbe8), which calls `TitleScreen_SelectRandomMachine`
(0x8000daa0) to loop kinds 0-17, skip the four most recently used (kept at `GameData+0x1d`), let
the Warp Star through and gate the rest with `TitleScreen_CheckMachineUnlocked` (0x8000c364). Real
rounds never use this path; their CPUs pick through `loadCPU`.
