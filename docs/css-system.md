# Character Select Screen (CSS) System

## Overview

The CSS handles player, machine, and color selection before entering 3D gameplay. There are three independent CSS implementations — one per game mode — each running as minor scene callbacks within `MJRKIND_MENU` (major 3).

| Mode | Minor | Main Dispatch Function | Data Struct |
|------|-------|----------------------|-------------|
| Air Ride | 8 (`MNRKIND_AIRRIDEPLYSELECT`) | `CSS_airRide_ModeDispatch` (0x8002a1b0) | `airride_select_ply` (GameData+0x10a) |
| City Trial | 10 (`MNRKIND_CITYPLYSELECT`) | `CitySelect_MinorLoad` (0x8003b2c0) | `city_select_ply` (GameData+0x1d0) |
| Top Ride | — | `TopRide_PreGameThink` (0x8002c06c) | `topride_select_ply` (GameData+0x160) |

All three share common patterns: per-player input handling, color cycling via L/R buttons, and machine/character selection through HSD archive icon rendering.

The hoshi `OnPlayerSelectLoad` callback fires for both Air Ride (minor 8) and City Trial (minor 10). See `scene-system.md` § Hoshi Lifecycle Callbacks.

## Air Ride CSS (Minor 8)

### Architecture

The Air Ride CSS uses a **two-level dispatch** based on the selected Air Ride sub-mode:

```
CSS_airRide_ModeDispatch (0x8002a1b0)
  ├── airride_mode == RACE (0)  → CSS_airRide_RaceUpdate (0x80028888)
  └── airride_mode != RACE      → CSS_airRide_FreeTimeUpdate (0x80029bd8)
```

`Gm_GetAirRideMode()` (0x8003d5f0) returns `GameData[0x35d]`:

| Value | Mode | CSS Function |
|-------|------|-------------|
| 0 | `AIRRIDEMODE_RACE` | `CSS_airRide_RaceUpdate` (0x80028888, 0x1350 bytes) |
| 1 | `AIRRIDEMODE_TIME` | `CSS_airRide_FreeTimeUpdate` (0x80029bd8, 0x5d8 bytes) |
| 2 | `AIRRIDEMODE_FREE` | `CSS_airRide_FreeTimeUpdate` (0x80029bd8, 0x5d8 bytes) |

### Function Table

| Function | Address | Size | Purpose |
|----------|---------|------|---------|
| `CSS_airRide_ModeDispatch` | 0x8002a1b0 | 0x210 | Mode-based dispatch, calls CSS update + `loadMainMenuMusic` |
| `CSS_airRide_RaceUpdate` | 0x80028888 | 0x1350 | Race mode state machine (multi-player, init + per-frame) |
| `CSS_airRide_FreeTimeUpdate` | 0x80029bd8 | 0x5d8 | Free Run / Time Attack state machine (single-player) |
| `CSS_airRide_InitSelectData` | 0x80028754 | 0xe4 | Initializes select struct: colors to {0,1,2,3}, sets init trigger |
| `CSS_airRide_Main` | 0x8002860c | 0x148 | CSS entry point |
| `CSS_airRide_inputGrabber` | 0x80026f20 | 0x1084 | Main input handler (Race) |
| `CSS_airRide_chooseVehicleInputGrabber` | 0x80024d04 | 0xad8 | Vehicle selection input |
| `CSS_airRide_inputGrabberReadyScreen` | 0x80026110 | 0xe10 | Ready screen input (all confirmed) |
| `CSS_airRide_FreeRuInputGrabber` | 0x80027fa4 | 0x668 | Free Run input handler |
| `CSS_airRide_colorChanger` | 0x80021654 | 0x2e0 | L/R color cycling |
| `AirRide_PopulateSelectIcons` | 0x80020a08 | 0xc4c | Builds character icon grid |
| `AirRide_CheckCharacterAvailable` | 0x8002090c | 0xfc | Character availability check |
| `AirRide_CheckMachineUnlocked` | 0x8000c364 | 0x124 | Machine unlock check (CPU random) |
| `AirRide_SelectRandomMachine` | 0x8000daa0 | 0x148 | CPU random machine assignment |
| `loadCPU` | 0x80023600 | 0x2d4 | CPU player setup |
| `Gm_GetAirRideMode` | 0x8003d5f0 | 0x24 | Returns `airride_mode` |
| `Gm_GetAirRidePlayerSlot` | 0x8003d644 | 0x24 | Returns active player slot (Free Run / Time Attack) |

### Init Trigger

`GameData[0x10b]` is an init flag. When non-zero on entry to the CSS update function, it triggers the color init block which sets `color[0..3] = {0, 1, 2, 3}` and resets per-slot state. The flag is set to `0xFF` by `CSS_airRide_InitSelectData` and cleared by the CSS update functions after initialization.

Both `CSS_airRide_RaceUpdate` and `CSS_airRide_FreeTimeUpdate` have their own color init blocks. The mod hooks both convergence points to validate colors against the unlock mask. See `gate-colors.md` for details.

### Select Screen Grid

`AirRide_PopulateSelectIcons` (0x80020a08) has two display modes: a 2×10 grid layout (≥10 available characters) and a linear strip (<10 available).

2 rows × 10 columns stored at `0x80495800` (raw CKIND bytes verified against `scripts/mem1.raw`):

```
Row 0: DRAGOON FORMULA WINGED WARP    COMPACT FLIGHT SHADOW WAGON   SWERVE HYDRA
       (15)    (3)     (9)    (1)     (0)     (17)   (8)    (6)     (5)    (16)
Row 1: DEDEDE  JET     ROCKET TURBO   BULK    SLICK  WLBIKE WLSCOOT REXWHL METAKN
       (18)    (10)    (11)   (2)     (7)     (4)    (13)   (12)    (14)   (19)
```

Linear strip order (used when <10 available). The table lives at `0x804957ec`; `SelIcon_GetCKindLinear` (0x8000b9a8, a small function — `lis r4,0x8049; addi r3,r4,0x57ec; lbzx r3,r3,idx`) indexes into it. Raw CKIND bytes verified against `scripts/mem1.raw`:
```
DEDEDE DRAGOON JET ROCKET TURBO BULK FORMULA WINGED WARP COMPACT
FLIGHT SHADOW WAGON SWERVE SLICK WLBIKE WLSCOOT REXWHL HYDRA METAKN
```

(Note CKIND_SLICK=4 and CKIND_WAGON=6 per `menu.h` — Slick Star is the slot-4 entry, Wagon Star slot 6.)

`AirRide_PopulateSelectIcons` builds the available list at `gd[0x170]` (count at `gd[0x16f]`). Display mode flag at `gd[0x184]`: 1 = grid, 0 = linear.

#### Grid Mode Reordering

In grid mode, the function includes special handling for "special" characters (CKIND_DRAGOON=15, CKIND_HYDRA=16, CKIND_DEDEDE=18, CKIND_METAKNIGHT=19). It checks `(byte)(ckind - 0xf) < 2 || ckind == 0x12 || ckind == 0x13` and moves matching characters to the edges of each row for visual balance, then interleaves the two rows.

### CharacterKind ↔ MachineKind Mapping

CharacterKind (20 entries, in `menu.h`) maps to MachineKind (26 entries) via `CharacterDesc` (table at `0x80495814`, 3 bytes per entry). `Character_GetDesc` (0x8000b9dc) returns `ckind * 3 + 0x80495814`.

6 machines have no CharacterKind and are city-spawn-only: FREE, STEER, WINGKIRBY, WHEELNORMAL, WHEELKIRBY, WHEELVSDEDEDE.

#### Bike-Relative Indexing

For non-bikes (`is_bike=0`), `CharacterDesc.machine_kind` IS the MachineKind/VCKIND directly. For bikes (`is_bike=1`), `machine_kind` is a **bike-relative index** — the actual VCKIND is `VCKIND_WHEELNORMAL(19) + machine_kind`.

| CharacterKind | is_bike | desc.machine_kind | Actual VCKIND |
|---|---|---|---|
| CKIND_COMPACT (0) | 0 | 1 | VCKIND_COMPACT (1) |
| CKIND_WARP (1) | 0 | 0 | VCKIND_WARP (0) |
| CKIND_TURBO (2) | 0 | 12 | VCKIND_TURBO (12) |
| CKIND_FORMULA (3) | 0 | 7 | VCKIND_FORMULA (7) |
| CKIND_SLICK (4) | 0 | 6 | VCKIND_SLICK (6) |
| CKIND_SWERVE (5) | 0 | 11 | VCKIND_SWERVE (11) |
| CKIND_WAGON (6) | 0 | 9 | VCKIND_WAGON (9) |
| CKIND_BULK (7) | 0 | 5 | VCKIND_BULK (5) |
| CKIND_SHADOW (8) | 0 | 3 | VCKIND_SHADOW (3) |
| CKIND_WINGED (9) | 0 | 2 | VCKIND_WINGED (2) |
| CKIND_JET (10) | 0 | 13 | VCKIND_JET (13) |
| CKIND_ROCKET (11) | 0 | 10 | VCKIND_ROCKET (10) |
| CKIND_WHEELIESCOOTER (12) | 1 | 4 | VCKIND_WHEELIESCOOTER (23) |
| CKIND_WHEELIEBIKE (13) | 1 | 2 | VCKIND_WHEELIEBIKE (21) |
| CKIND_REXWHEELIE (14) | 1 | 3 | VCKIND_REXWHEELIE (22) |
| CKIND_DRAGOON (15) | 0 | 8 | VCKIND_DRAGOON (8) |
| CKIND_HYDRA (16) | 0 | 4 | VCKIND_HYDRA (4) |
| CKIND_FLIGHT (17) | 0 | 14 | VCKIND_FLIGHT (14) |
| CKIND_DEDEDE (18) | 1 | 5 | VCKIND_WHEELDEDEDE (24) |
| CKIND_METAKNIGHT (19) | 0 | 18 | VCKIND_WINGMETAKNIGHT (18) |

Use `CharacterDesc_GetMachineKind()` (inline in `menu.h`) to get the correct VCKIND from a CharacterDesc; `gate_machines.c` calls it.

### Vanilla Availability Logic

#### AirRide_CheckCharacterAvailable (0x8002090c)

Switch on CharacterKind:
- `case 0` (CKIND_COMPACT): **always returns 0** — Compact Star never appears in vanilla
- `case 1` (CKIND_WARP): **always returns 1** — Warp Star always available
- `case 15, 16, 17` (DRAGOON, HYDRA, FLIGHT): **always returns 0** — City Trial-only
- All others: map to an Air Ride checklist reward index, check via `ClearChecker_CheckUnlocked`

| CharacterKind | Reward Index |
|---|---|
| CKIND_TURBO (2) | 0x1d (29) |
| CKIND_FORMULA (3) | 0x19 (25) |
| CKIND_SLICK (4) | 0x18 (24) |
| CKIND_SWERVE (5) | 0x15 (21) |
| CKIND_WAGON (6) | 0x14 (20) |
| CKIND_BULK (7) | 0x16 (22) |
| CKIND_SHADOW (8) | 0x1a (26) |
| CKIND_WINGED (9) | 0x13 (19) |
| CKIND_JET (10) | 0x1e (30) |
| CKIND_ROCKET (11) | 0x1c (28) |
| CKIND_WHEELIESCOOTER (12) | 0x1b (27) |
| CKIND_WHEELIEBIKE (13) | 0x17 (23) |
| CKIND_REXWHEELIE (14) | 0x1f (31) |
| CKIND_DEDEDE (18) | 0x20 (32) |
| CKIND_METAKNIGHT (19) | 0x21 (33) |

#### AirRide_CheckMachineUnlocked (0x8000c364)

Used by `AirRide_SelectRandomMachine` for CPU machine assignment. Takes `machine_class` (is_bike) and `machine_id` (from `CharacterDesc.machine_kind`). Maps to the same Air Ride checklist reward indices. Returns 0 for unknown machine IDs.

#### AirRide_SelectRandomMachine (0x8000daa0)

Loops through CharacterKinds 0–17 (excludes CKIND_DEDEDE and CKIND_METAKNIGHT). Skips the 4 most recently assigned CKinds (history at `gd[0x1d]–gd[0x20]`). Warp Star (ckind 1) always bypasses the unlock check. All others call `AirRide_CheckMachineUnlocked`. Picks randomly with equal weight.

## Air Ride Select Data

Base: `GameData + 0x10a` (`airride_select_ply`)

### Per-Slot Fields

All per-slot fields have 4 entries (slots 0–3). Offsets are from GameData.

| Field | Offset | Per-slot stride | Purpose |
|-------|--------|----------------|---------|
| `p_kind[4]` | 0x133 | +1 | Player kind (human/CPU/none) |
| `icon[4]` | 0x137 | +1 | CSS machine icon display color |
| `color[4]` | 0x15b | +1 | **Actual in-game Kirby color** (L/R cycling target) |

Other relevant fields (offsets relative to base at 0x10a):

| Offset | Purpose |
|--------|---------|
| +0x00 (0x10a) | First byte of select struct |
| +0x01 (0x10b) | Init trigger flag |
| +0x03 (0x10d) | State flag (init vs active path) |
| +0x09 | Rendering state (0=inactive, 2=active) |
| +0x25 | Player identity mapping (slot → player) |
| +0x2d | Icon color (from machine-to-color lookup) |
| +0x31 | Secondary color field |
| +0x45 | CSS state (0=active human, 2=CPU needing random, 3=inactive) |
| +0x4d | Per-identity state |
| +0x51 | `color[]` — actual Kirby color |
| +0x55 | Field initialized to 8 |
| +0x59 | Field initialized to 8 |
| +0x5d | Field initialized to 2 |
| +0x61 | Machine kind |
| +0x65 | Number of available colors |
| +0x66 | Machine-to-color mapping table |

**Critical**: `icon[]` and `color[]` are separate fields. `color[]` determines the actual in-game Kirby color. `icon[]` controls only the CSS display. See `gate-colors.md` § The Two Color Fields.

## City Trial CSS (Minor 10)

### Architecture

The City Trial CSS is dispatched from `CitySelect_MinorLoad`, which checks `Gm_GetCityMode()` to determine the sub-mode:

| CityMode | Value | Sub-loader |
|----------|-------|-----------|
| `CITYMODE_TRIAL` | 0 | `CitySelect_LoadCityTrial` (0x80038d6c) |
| `CITYMODE_STADIUM` | 1 | `CitySelect_LoadStadium` (0x80039e20) |
| `CITYMODE_FREERUN` | 2 | `CitySelect_LoadMachineSelect` (0x8003a904) |

### Function Table

| Function | Address | Size | Purpose |
|----------|---------|------|---------|
| `CitySelect_MinorLoad` | 0x8003b2c0 | 0x1e8 | Minor load dispatcher |
| `CitySelect_MinorThink` | 0x8003b4c8 | 0x20 | Per-frame minor think |
| `CitySelect_Init` | 0x80135060 | 0x128 | CSS initialization |
| `CitySelect_Think` | 0x80037a90 | 0x11b0 | Main per-frame update loop |
| `CitySelect_CreatePlayers` | 0x801352b0 | 0x6c | Creates player GObjs |
| `CitySelect_InitPlayerMachines` | 0x8002ddd8 | 0x330 | Initial machine assignments |
| `CitySelect_CreateMachineIcons` | 0x8002e3c4 | 0xe74 | Machine icon grid |
| `CitySelect_InputUpdate` | 0x80032d34 | 0xe48 | Player input processing |
| `CitySelect_PlayerThink` | 0x800348f8 | 0xacc | Per-player think callback |
| `CitySelect_ChangeColor` | 0x8002f238 | 0x350 | L/R color cycling |
| `CitySelect_LoadCityTrial` | 0x80038d6c | 0x10b4 | City Trial game state loader |
| `CitySelect_LoadStadium` | 0x80039e20 | 0xae4 | Stadium mode loader |
| `CitySelect_LoadMachineSelect` | 0x8003a904 | 0x9bc | Machine selection (Free Run) |
| `CitySelect_GetColorAnimFrame` | 0x80009630 | 0x28 | Animation frame for color display |

## City Trial Select Data

Base: `GameData + 0x1d0` (`city_select_ply`)

| Field | Offset | Purpose |
|-------|--------|---------|
| `is_all_ready` | 0x1d5 | All players confirmed, checks for start |
| `ply_is_selecting_bitfield` | 0x1d6 | Flag per player currently selecting |
| `is_ready[4]` | 0x1d9 | Per-player ready flag |
| `player_state[4]` | 0x1f1 | 0=inactive, 1=icon select, 2=handicap/cpu, 3=player element |
| `ply_cursor[4]` | 0x1f5 | Player's cursor position |
| `ply_bar[4]` | 0x1f9 | Bar index hovered over |
| `icon[4]` | 0x1fd | Icon selection |
| `icon_saved[4]` | 0x201 | Preserved icon after player exits |
| `ply_pkind[4]` | 0x21d | Player kind |
| `ply_color[4]` | 0x221 | **Kirby color** |
| `ply_hmn_handicap[4]` | 0x225 | Human handicap setting |
| `ply_cpu_handicap[4]` | 0x229 | CPU handicap setting |
| `ply_cpu_level[4]` | 0x22d | CPU level |
| `ply_icon_ckind[4]` | 0x231 | CharacterKind of selected icon |
| `machine_select.num` | 0x235 | Number of selectable machines |
| `machine_select.c_kind_arr[20]` | 0x236 | Array of selectable CharacterKind indices |

## Top Ride CSS

Top Ride has a simpler select screen that handles both course and player selection.

### Function Table

| Function | Address | Size | Purpose |
|----------|---------|------|---------|
| `TopRide_PreGameThink` | 0x8002c06c | 0xa14 | Course/character selection |
| `TopRide_OnCourseSelect` | 0x8002cc30 | 0x3a8 | Course selection callback |
| `CSS_topRide_colorChanger` | 0x8002a400 | 0x230 | L/R color cycling |

### Top Ride Select Data

Base: `GameData + 0x160` (`topride_select_ply`)

| Field | Offset | Purpose |
|-------|--------|---------|
| `color[4]` | 0x1ba | Kirby color per player |

Course selection: `GameData.topride_selected_course` at offset 0x374.

## Common Patterns

### Input Grabbers

Each CSS has dedicated input grabber functions called per-player per-frame from the CSS Think callback. These handle:
- Controller port detection and player join/leave
- D-pad/stick navigation for machine selection
- A/B button confirm/cancel
- L/R button color cycling (delegated to colorChanger functions)

Air Ride has separate grabbers for different CSS states: main selection, vehicle choice, ready screen, and Free Run mode.

### Color Cycling

All three modes use dedicated `colorChanger` functions called from their input paths:

| Mode | Function | Address |
|------|----------|---------|
| Air Ride | `CSS_airRide_colorChanger` | 0x80021654 |
| Top Ride | `CSS_topRide_colorChanger` | 0x8002a400 |
| City Trial | `CitySelect_ChangeColor` | 0x8002f238 |

Each handles L/R button input to cycle through colors 0–7. Colors 0–3 go through a vanilla unlock check; colors 4–7 are always available in vanilla. The mod hooks all three to gate colors against the AP unlock mask. See `gate-colors.md` § All Hook Sites.

### Archive/Icon System

Select screen icons are rendered from HSD archive files using material animation:

| Archive | Purpose |
|---------|---------|
| `MnSelplyAll.dat` | Air Ride character icons |
| `MnSelplyctAll.dat` | City Trial character icons |
| `MnSelplym2dAll.dat` | Top Ride icons |
| `SisSelply.dat` | Air Ride system data |
| `SisSelplyCt.dat` | City Trial system data |

Archive data is loaded into `ScMenuSelect` (at `stc_menu_select`, 0x804962b0) which contains per-mode sub-structs with JOBJSet pointers and GObj arrays for all visual elements.

City Trial's archive (`MnSelplyctAll.dat`) likely has a Compact Star texture since Compact Star is the default City Trial machine. If the Air Ride archive lacks frame 0, the CT archive's texture could potentially be copied.

#### MnSelplyAll Archive Structure

The `MnSelplyAll.dat` archive (573 KB, 22 root symbols) is loaded from the game disc during menu initialization. Key symbols:

| Symbol | Offset | Purpose |
|--------|--------|---------|
| `ScMenSelplySicon_scene_models` | 0x040908 | Air Ride character icon model + material animation |
| `ScMenSelplySicon2_scene_models` | 0x078250 | Alternate icon model (possibly City Trial mode) |
| `ScMenSelply_scene_data` | 0x0002a4 | Scene layout/camera data |

The archive pointer is resolved during menu init via `AirRideSelect_Index` (0x801515f8):
```c
Archive_GetSymbols(archive, menu_data + 0x51c, "ScMenSelplySicon_scene_models", 0);
```

#### Scene Models Structure

Each `scene_models` root is an indirect pointer: root data → scene_models structure:

```
+0x00: JObjDesc*           — model root
+0x04: AnimJoint**          — skeletal animation banks (NULL for Sicon)
+0x08: MatAnimJoint**       — material animation banks (texture swapping)
+0x0C: ShapeAnimJoint**     — shape animation banks (NULL for Sicon)
```

For Sicon, the scene_models is at data offset 0x0408f8:
```
+0x00: 0x00035be8  → JObjDesc (model root)
+0x04: 0x00000000  → no skeletal animation
+0x08: 0x000408f0  → MatAnimJoint banks array
+0x0C: 0x00000000  → no shape animation
```

#### Icon Animation Pipeline

`_JObj_AddSetAnim` (0x80055a30) extracts animation from the scene_models:
```c
void _JObj_AddSetAnim(JObj *jobj, int bank, void *scene_models) {
    AnimJoint *anim = scene_models[1] ? scene_models[1][bank] : NULL;
    MatAnimJoint *mat = scene_models[2] ? scene_models[2][bank] : NULL;
    ShapeAnimJoint *shape = scene_models[3] ? scene_models[3][bank] : NULL;
    HSD_JObjAddAnimAll(jobj, anim, mat, shape);
}
```

The per-icon creation flow in `zz_80151644_` (0x80151644):
1. `MainMenu_CreateGObj(**scene_models_ptr)` — create GObj from JObjDesc
2. `GObj_AddProc(gobj, zz_801515d8_)` — add per-frame update callback
3. `MainMenu_AddAnim(ckind_float, ..., jobj, scene_models_ptr)` — apply material animation at frame = CharacterKind value

`MainMenu_AddAnim` → `JObj_AddSetAnim_SetFrameAndRate` (0x80138b10):
1. `HSD_JObjRemoveAnimAll()` — clear existing animation
2. `_JObj_AddSetAnim(jobj, 0, scene_models)` — load bank 0 animation (MatAnimJoint)
3. `HSD_JObjReqAnimAllFlags()` — request animation update
4. `JObj_SetAllAOBJRateByFlags(frame, ..., jobj, 0xffff)` — set frame to ckind value
5. `HSD_JObjAnimAll_(jobj)` — apply animation (selects texture for that frame)

#### JObj Model Tree (Sicon)

```
JObj@0x35be8 (root, container, no display)
  └─ JObj@0x35c28 (icon, has display objects)
       ├─ DObj 1: 64×64 CMPR texture (main icon image)
       └─ DObj 2: 32×32 RGB5A3 texture (decoration/frame)
```

The 64×64 CMPR texture is the character icon. The MatAnimJoint (bank 0, at data offset 0x36bd0) controls which texture is displayed by swapping the material's texture image based on the animation frame value (= CharacterKind).

#### MatAnimJoint Structure (Partially Traced)

```
MatAnimJoint root @0x36bd0: child=0x36bdc, next=NULL, matanim=NULL
  └─ MatAnimJoint child @0x36bdc: (corresponds to child JObj with display objects)
       └─ [contains MatAnim → TexAnim with ImageDesc array and keyframes — needs further tracing]
```

The TexAnim structure (within MatAnim) should contain:
- `AObjDesc` with keyframes mapping frame values to image indices
- `ImageDesc**` array of texture images (one per character)
- `n_images` count

**Investigation paused here.** Next step is to follow the MatAnimJoint child at 0x36bdc → MatAnim → TexAnim to enumerate the actual texture images and keyframe mapping. This will confirm whether frame 0 (CKIND_COMPACT) has a valid texture or is blank/missing.

## Mod Hook Points

The following mod systems intercept CSS behavior. Each is documented in its own file:

### Color Gating (`gate-colors.md`)

- L/R cycling hooks on all 3 colorChanger functions
- `color[]` init validation in both Air Ride CSS functions
- `icon[]` validation via machine-to-color lookup hook
- CPU random color replacement via `HSD_Randi` REPLACECALL hooks
- `ValidateCityTrialColors` via `OnPlayerSelectLoad` (CT minor only — CT has no init block to hook)

### Machine Gating (`gate-machines.md`)

- `AirRide_CheckCharacterAvailable` REPLACEFUNC — gates AR character availability
- `AirRide_CheckMachineUnlocked` REPLACEFUNC — gates CPU random machine assignment
- CT Free Run: hooks at `CitySelect_CreateMachineIcons` for counting (0x8002e5c0) and array-building (0x8002e738)
- CT Free Run select list filtering via `GateMachines_FilterSelectList` in `OnPlayerSelectLoad`

### Stage Gating (`gate-stages.md`)

- `AirRide_CheckCourseUnlocked` (0x8000c0e0) REPLACEFUNC — gates Air Ride stage availability on the map select screen. Implemented in `gate_airride_stages.c`. See `gate-stages.md`.

### Main-Menu Demo Rider (`main_menu.c`)

The title screen runs a "demo player" setup at `0x8000d300` that configures slot 0's idle rider via a series of `Ply_Set*` calls. Three `li r4, imm` operands choose what's ridden; each is `REPLACEINSTRUCTION`'d at boot to swap the default Kirby-on-Warp-Star for Dedede-on-Wagon:

| Address | Vanilla | Sets |
|---------|---------|------|
| 0x8000d340 | `li r4, 0` | `Ply_SetRiderKind(0, …)` (RDKIND) |
| 0x8000d34c | `li r4, 0` | `Ply_SetIsBike(0, …)` |
| 0x8000d358 | `li r4, 0` | `Ply_SetMachineKind(0, …)` (VCKIND) |

`Ply_SetMachineKind` stores a class-relative index: star-class (`is_bike = 0`) uses the `VCKIND_*` value directly, wheel-class is relative to `VCKIND_WHEELNORMAL`. **The demo init calls `MachineStateChange` with hardcoded star-only state ids (82/89)**, so a wheel-class machine crashes here — keep `is_bike = 0` and pick a star machine.
