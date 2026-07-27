# Scene System

Kirby Air Ride runs a two-level scene hierarchy: **major scenes** (`MajorKind`) are the top-level game modes (Title, Menu, Air Ride, City Trial, ...), and **minor scenes** (`MinorKind`) are the sub-screens within a major (settings, map select, player select, 3D gameplay, ...). The game loops over majors; within each major it loops over minors until a major exit is requested. Every minor has load / think / exit callbacks that drive its per-frame logic.

## Mode Within a Major

`Scene_GetCurrentMajor()` only says which top-level mode is active. Each gameplay major has a finer **mode** chosen from the menu, stored in `GameData` and read through a dedicated accessor. Mod gating code discriminates on the major first, then the mode.

| Major | Mode enum (`game.h`) | Values | Accessor (addr) | GameData field |
|-------|----------------------|--------|-----------------|----------------|
| `MJRKIND_AIR` | `AirRideMode` | `AIRRIDEMODE_RACE` (0), `_TIME` (1), `_FREE` (2) | `Gm_GetAirRideMode()` (0x8003d5f0) | `airride_mode` @ 0x35d |
| `MJRKIND_CITY` | `CityMode` | `CITYMODE_TRIAL` (0), `_STADIUM` (1), `_FREERUN` (2) | `Gm_GetCityMode()` (0x8003f6cc) | `mode` @ 0x399 |
| `MJRKIND_TOP` | `TopRideMode` | `TOPRIDEMODE_RACE` (0), `_TIME` (1), `_FREE` (2) | `TopRide_GetMode()` (0x8003ea9c) | `topride_mode` @ 0x381 |

Two stage-state predicates sit alongside the mode accessors and answer a different question — *where the player currently is*, not *what was picked in the menu*:

| Predicate | Address | True when |
|-----------|---------|-----------|
| `CityTrial_IsInStadium()` | 0x8000ad48 | The loaded stage is a stadium (`city_kind` 7–18). The stadium test mod code uses. |
| `Gm_IsInCity()` | 0x8000acb0 | The loaded stage is the open City Trial map. |
| `Gm_IsLegendaryAssembling()` | 0x8000c934 | A Dragoon/Hydra assembly cinematic is running (reads the cinematic GObj pointer at `GameData+0xa8c`). Not a mode or stadium check. |

## Major Scenes

### MajorKind Enum

| Value | Name | Purpose |
|-------|------|---------|
| 0 | `MJRKIND_0` | Boot/unspecified |
| 1 | `MJRKIND_MOVIE` | Movie playback |
| 2 | `MJRKIND_TITLE` | Title screen |
| 3 | `MJRKIND_MENU` | Main menu (all select screens run here) |
| 4 | `MJRKIND_AIR` | Air Ride 3D gameplay |
| 5 | `MJRKIND_TOP` | Top Ride gameplay (2D engine; uses minor 19, not the shared minor-18 3D path) |
| 6 | `MJRKIND_CITY` | City Trial 3D gameplay |
| 7 | `MJRKIND_DEBUG3D` | Air Ride debug scene |
| 8 | `MJRKIND_TOPDEBUG` | Top Ride debug menu |
| 10 | `MJRKIND_ENDING` | Credits |
| 16 | `MJRKIND_DEBUG` | Debug menu |
| 18 | `MJRKIND_CARD` | Memory card prompt |
| 19 | `MJRKIND_LAN` | LAN mode |

### MajorSceneDesc

Each major is described by a `MajorSceneDesc` (12 bytes); the vanilla table is at `0x80495058` (`stc_major_scene_desc`).

```c
struct MajorSceneDesc {
    MajorKind major_id : 8;         // 0x0 — this major's ID
    MajorKind next_major_id : 8;    // 0x1 — default next major (can be overridden)
    MinorKind initial_minor_id : 8; // 0x2 — first minor to enter
    void (*cb_Enter)();             // 0x4 — called once on major entry
    void (*cb_ExitMinor)();         // 0x8 — called after each minor exits
};
```

### Major Scene Lifecycle

`Gm_Major` (0x800082d0, 0x5c8 bytes) runs the outer loop. At runtime hoshi redirects it into `Gm_MajorPatch` via a hook at 0x8000836c; the re-implemented loop is:

1. Call `Scene_InitHeaps()` once (hoshi addition, so the game can boot into a non-title scene).
2. Outer loop (infinite — one iteration per major):
   a. `major_cur = major_pending`
   b. Find the matching `MajorSceneDesc` in `major_scene_descs[]` (asserts if none)
   c. Set the default next major: `major_pending = next_major_id`
   d. `request_major_exit = 0`
   e. `Scene_SetNextMinor(initial_minor_id)`
   f. Call `cb_Enter()` if present
   g. Inner loop while `!request_major_exit`: call `Gm_Minor()`, then `cb_ExitMinor()` after that minor completes (skipped for the memcard-unplug scene and during reboot)

### Per-Major Callback Functions

The `cb_Enter` (+0x4) and `cb_ExitMinor` (+0x8) slots of each `MajorSceneDesc` hold these functions (records in `stc_major_scene_desc` are ordered by `major_id`):

| Major | `cb_Enter` (+0x4) | `cb_ExitMinor` (+0x8) |
|-------|-------------------|------------------------|
| `MJRKIND_TITLE` | `TitleScreen_MajorEnter` (0x8000da34) | `TitleScreen_MinorExit` (0x8000e1d8) |
| `MJRKIND_MENU` | `MainMenu_MajorEnter` (0x80015bb4) | `MainMenu_MinorExit` (0x80015be8) |
| `MJRKIND_TOP` | `TopRide_MajorEnter` (0x8003ed34) | `TopRide_MinorExit` (0x8003eed8) |
| `MJRKIND_CITY` | `CityTrial_MajorEnter` (0x8003fc5c) | `CityTrial_MinorExit` (0x8003fdd4) |
| `MJRKIND_CARD` | `CardPrompt_MajorEnter` (0x800476b8) | — |
| `MJRKIND_LAN` | `LAN_MajorEnter` (0x8004f654) | — |

`cb_Enter` runs once on major entry to set up the major's data. `cb_ExitMinor` runs after each minor exits and is the major's **minor-transition decider**: a jump table on the major's scene-state field that advances the sub-scene, (re)initializes data, runs the checklist new-unlock flow, or exits the major. The state field is per-major:

- `CityTrial_MinorExit` switches on the City Trial scene field `GameData+0x39b` (3 = player select, 4 = in game, 5 = properties graph, 6 = stadium splash, 7 = stadium, 8 = results). It calls `CityTrial_Init` (0x8003f988), snapshots per-player stats, runs `CityTrial_CheckForNewUnlocks` (0x8004db74), and in the stadium-results state calls `Stadium_RecordResults` (0x8004223c) to record the round's per-player result (keyed by stadium kind) into the clear-checker results block.
- `TopRide_MinorExit` switches on the Top Ride scene field `GameData+0x385`, branching on `topride_mode` (`GameData+0x381`). It drives course-select → race → results and runs the Top Ride new-unlock flow.

## Minor Scenes

### MinorKind Enum

| Value | Name | Purpose |
|-------|------|---------|
| 0 | `MNRKIND_TITLESCREEN` | Title screen |
| 1 | `MNRKIND_OPENING` | Opening sequence |
| 2 | `MNRKIND_MAINMENU` | Main menu |
| 3 | `MNRKIND_AIRRIDESETINGS` | Air Ride settings (mode select) |
| 4 | `MNRKIND_4` | Unknown |
| 5 | `MNRKIND_CITYSETTINGS` | City Trial settings |
| 6 | `MNRKIND_AIRRIDEMAPSELECT` | Air Ride stage select |
| 7 | `MNRKIND_7` | Unknown |
| 8 | `MNRKIND_AIRRIDEPLYSELECT` | Air Ride CSS (player/color select) |
| 9 | `MNRKIND_9` | Unknown |
| 10 | `MNRKIND_CITYPLYSELECT` | City Trial CSS (player/machine select) |
| 11–12 | `MNRKIND_11`–`MNRKIND_12` | Unknown |
| 13 | `MNRKIND_CITYRESULT` | City Trial results |
| 14–16 | `MNRKIND_14`–`MNRKIND_16` | Unknown |
| 17 | `MNRKIND_STADIUMSPLASH` | Stadium splash screen |
| 18 | `MNRKIND_3D` | 3D gameplay (Air Ride + City Trial, incl. stadiums) |
| 19 | `MNRKIND_19` | Top Ride gameplay (own 2D engine; `cb_Load` = `TopRide_SceneLoad` @ 0x80008df8) |
| 20 | `MNRKIND_20` | Unknown |
| 21 | `MNRKIND_STADIUMSELECT` | Stadium select |
| 25 | `MNRKIND_MOVIE` | Movie |
| 28 | `MNRKIND_AIRRIDEENDING` | Ending movie played by the checklist's ending reward (`MvEnding.h4m`) |
| 29 | `MNRKIND_TOPRIDEENDING` | Ending movie (checklist ending reward) |
| 30 | `MNRKIND_CITYENDING` | Ending movie (checklist ending reward) |
| 32 | `MNRKIND_AIRRIDECHECKLIST` | Air Ride checklist |
| 33 | `MNRKIND_TOPRIDECHECKLIST` | Top Ride checklist |
| 34 | `MNRKIND_CITYCHECKLIST` | City Trial checklist |
| 35–37 | `MNRKIND_35`–`MNRKIND_37` | LAN menu (`gmLanMenu_InitializeGameState`) |
| 38 | `MNRKIND_CARD` | Memory card |
| 39 | `MNRKIND_DEBUGMENU` | Debug menu |
| 40 | `MNRKIND_40` | Unknown |

### Top Ride Uses Minor 19

Top Ride gameplay runs as **minor 19** (`MNRKIND_19`), *not* the shared minor 18 (`MNRKIND_3D`) that Air Ride and City Trial use. It has its own 2D engine and never goes through the 3D-scene instantiation path, so:

- `On3DLoadStart` (0x80014448) and `On3DLoadEnd` (0x80014d3c) **do not fire** for Top Ride.
- `OnTopRideLoadEnd` (hook at 0x80008fac, inside minor 19's `cb_Load` `TopRide_SceneLoad` at 0x80008df8) is the Top Ride load notification. Any mod that needs to initialize per-round Top Ride state must use it.
- `On3DPause` / `On3DUnpause` / `On3DExit` are likewise 3D-only.

### MinorSceneDesc

Each minor's callbacks are described by a `MinorSceneDesc` (0x24 bytes); the vanilla table is at `0x80495154` (`stc_minor_scene_desc`).

```c
struct MinorSceneDesc {
    s8 idx;                          // 0x00 — MinorKind index
    s8 x1;                          // 0x01
    u8 x2;                          // 0x02
    void (*cb_Load)();              // 0x04 — called once when minor loads
    void (*cb_Exit)(void *data);    // 0x08 — called once when minor exits
    void (*cb_ThinkPreGObjProc)();  // 0x0C — per-frame: before GObj procs
    void (*cb_ThinkPostGObjProc)(); // 0x10 — per-frame: after GObj procs
    void (*cb_ThinkPostGObjProc2)();// 0x14 — per-frame: after above (debug input)
    void (*cb_ThinkPreRender)();    // 0x18 — per-frame: before rendering
    void (*cb_ThinkPostRender)();   // 0x1C — per-frame: after rendering
    int preload_kind;               // 0x20 — heap kind copied to Preload
};
```

### Per-Frame Callback Execution Order

Each frame the 5 Think callbacks fire in strict order, dispatched from `updateFunction` (0x800067a4, 0x3b4 bytes). The minor's `MinorSceneDesc` think pointers (0x0c–0x1c) are copied into a per-frame callback table; `updateFunction` reads that table at offsets 0x0/0x4/0x8/0xc/0x10 and `bctrl`s each non-null entry. The addresses below are the load sites in `updateFunction` (the `lwz r12,N(r3)` immediately preceding each `bctrl`).

| Order | Callback (MinorSceneDesc field) | Load Site | Purpose |
|-------|----------|-------------|---------|
| 1 | `cb_ThinkPreGObjProc` (0x0c) | 0x800068bc | Before GObj processing |
| 2 | `cb_ThinkPostGObjProc` (0x10) | 0x80006a4c | After GObj procs updated |
| 3 | `cb_ThinkPostGObjProc2` (0x14) | 0x80006a64 | After above (debug input) |
| 4 | `cb_ThinkPreRender` (0x18) | 0x80006aa0 | Before rendering |
| 5 | `cb_ThinkPostRender` (0x1c) | 0x80006ac8 | After all rendering |

### MinorScene Table

Each major has a table of `MinorScene` entries mapping minor IDs to behavior:

```c
struct MinorScene {
    s8 minor_id;        // -1 = end of table
    u8 heap_kind;       // heap behavior
    void *minor_prep;   // initializes data for this minor
    void *minor_decide; // decides next minor (transition logic)
    u8 minor_kind;      // index into MinorSceneDesc array
    void *load_data;    // static data pointer (shared between minors)
    void *unload_data;  // static data pointer (shared between minors)
};
```

The active major's list is selected at major entry. At runtime the live table sits at `0x807e0580`, but that address is in the high heap region rather than static `.data` and is not a fixed symbol — informational only.

### Minor Scene Lifecycle

`Gm_Minor` (0x80008ad4, 0x324 bytes) handles one minor's lifecycle:

1. Look up the `MinorScene` entry for the current minor
2. Call `minor_prep()` — initializes data for this minor
3. Call `cb_Load()` (`MinorSceneDesc` + 0x04) — one-time load
4. Per-frame loop (`loop` @ 0x80006b58 → `updateFunction` @ 0x800067a4): run the 5 Think callbacks each frame until `Scene_ExitMinor()` is signalled
5. On exit: call `cb_Exit()` (`MinorSceneDesc` + 0x08), then `minor_decide()` to determine the next minor (or major)

## Scene Transition API

| Function | Address | Purpose |
|----------|---------|---------|
| `Scene_GetCurrentMajor()` | 0x8000aea8 | Returns current `MajorKind` |
| `Scene_GetCurrentMinor()` | 0x8000aecc | Returns current `MinorKind` |
| `Scene_SetNextMajor(id)` | 0x800082a0 | Queue next major (call from scene decide) |
| `Scene_ExitMajor()` | 0x80008220 | Trigger major exit (sets `request_major_exit`) |
| `Scene_SetNextMinor(id)` | 0x800088c8 | Queue next minor (call from scene decide) |
| `Scene_ExitMinor()` | 0x800064f0 | Trigger minor exit (call from think) |
| `Scene_SetDirection(dir)` | 0x8000a498 | Store button input for transitions (map name `Scene_StoreDirection`) |
| `Scene_GetDirection()` | 0x8000a474 | Retrieve stored direction |
| `Scene_InitHeaps()` | 0x8000891c | Initialize scene heaps (map name `SceneChange_InitHeaps`) |
| `Scene_GetMinorData()` | 0x80008874 | Get current minor's data pointer |
| `Scene_InitMinorData()` | 0x80008898 | Initialize minor data |

The transition contract (`scene.h`): a **MinorThink** calls `Scene_ExitMinor()` to trigger the decide step; a **SceneDecide** calls either `Scene_SetNextMinor()` to enter another minor, or `Scene_SetNextMajor()` followed by `Scene_ExitMajor()` to enter another major.

## Static Data & Runtime Tables

| Pointer | Address | Contents |
|---------|---------|----------|
| `stc_major_scene_desc` | 0x80495058 | Vanilla major scene descriptor table |
| `stc_minor_scene_desc` | 0x80495154 | Vanilla minor scene descriptor table |
| `stc_scene_menu_common` | 0x80558788 | `ScMenuCommon` — shared menu/select screen state (`Gm_GetMenuData()` @ 0x801311e0) |
| `stc_menu_select` | 0x804962b0 | `ScMenuSelect` — select screen GObj/model data |
| Runtime minor table | 0x807e0580 | `MinorScene` entries for the active major (runtime, heap-region — not a stable symbol) |

## Hoshi Scene Extension

Hoshi extends the scene system in `more_scenes.c` so mods can install custom scenes:

1. **Table relocation** (`Scenes_CopyVanilla`): copies the vanilla `MajorSceneDesc` and `MinorSceneDesc` tables into larger statically allocated arrays (`major_scene_descs[MJRKIND_NUM*2]`, `minor_scene_descs[MNRKIND_NUM*2]`), then `memset`s the vanilla tables to `-1` to catch unpatched reads. `major_scene_num` / `minor_scene_num` track how many entries are live.
2. **Reference patching** (`Scenes_ApplyPatches`): patches the 7 vanilla code sites that load the minor scene table address, via ASM trampolines (`minor_scene_asm_1..7`), at `0x80008978`, `0x80008b1c`, `0x80008b90`, `0x80008c04`, `0x80008c70`, `0x80008cc8`, `0x80008d78`. The single major-table site at `0x80008374` is covered by the loop replacement below instead of a trampoline.
3. **Major loop replacement**: `CODEPATCH_HOOKCREATE` at `0x8000836c` (inside `Gm_Major`, which starts at `0x800082d0`) redirects into `Gm_MajorPatch`, which re-implements the outer major loop over the relocated `major_scene_descs[]` and adds the initial `Scene_InitHeaps()` call.
4. **Install API**: `Scenes_InstallMajorScene()` / `Scenes_InstallMinorScene()` append a descriptor to the relocated array, assign the new ID (`major_scene_num` / `minor_scene_num`), and bump the count. Exposed to mods through the hoshi func table as `Hoshi_InstallMajorScene` / `Hoshi_InstallMinorScene` (plus `Hoshi_GetMajorScenes` / `Hoshi_GetMinorScenes`).

## Hoshi Lifecycle Callbacks

Hoshi provides `ModDesc` callbacks (`hoshi/mod.h`) that fire at points in the scene lifecycle. Each is a `CODEPATCH_HOOKCREATE` in `hoshi.c`:

| Callback | Hook Address | When It Fires |
|----------|-------------|---------------|
| `OnBoot` | — (mod install) | Once at mod load, persistent heap available |
| `OnSaveInit` | — (save setup) | When save data is created for this mod |
| `OnSaveLoaded` | — (card read) | After save data loaded from memory card |
| `OnSceneChange` | 0x8000678c | Every scene change (major or minor), after heap init |
| `OnMainMenuLoad` | 0x80018994 | Main menu minor loads (minor 2) |
| `OnPlayerSelectLoad` | 0x8003b48c, 0x8002a358 | City Trial CSS (minor 10) via `CitySelect_MinorLoad`, Air Ride CSS (minor 8) via `CSS_airRide_ModeDispatch` |
| `On3DLoadStart` | 0x80014448 | Before 3D scene instantiation |
| `On3DLoadEnd` | 0x80014d3c | After 3D scene fully instantiated (players, machines, map exist) |
| `On3DPause` | 0x80041160 | Game paused (receives pause player index) |
| `On3DUnpause` | 0x80113a30 | Game unpaused (receives pause player index) |
| `On3DExit` | 0x80015274 | Exiting 3D scene |
| `OnTopRideLoadEnd` | 0x80008fac | After Top Ride gameplay fully initialized (minor 19) |
| `OnFrameStart` | 0x80006844 | Every frame, first thing |
| `OnFrameEnd` | 0x80006a30 | Every frame, last thing before the frame index increments |

The heap is destroyed and recreated on every scene change: HSD objects (CObj, JObj, GObj) do not persist across one. Use `OnSceneChange` to recreate any persistent processes or objects. `OnBoot` is the exception — it runs with a persistent heap, so allocations made there survive the entire runtime; every other callback uses the scene-scoped heap.

## Scene Flow Per Mode

### Air Ride

```
Main Menu (minor 2)
  → Air Ride Settings (minor 3) — mode select: Race / Time Attack / Free Run
    → Map Select (minor 6) — stage select
      → Player Select (minor 8) — CSS: character/color select
        → 3D Gameplay (minor 18) — actual race
```

Major transition: `MJRKIND_MENU` → `MJRKIND_AIR` when entering 3D.

### City Trial

```
Main Menu (minor 2)
  → City Trial Settings (minor 5) — game settings
    → Player Select (minor 10) — CSS: player/machine/color select
      → 3D Gameplay (minor 18) — city trial round
        → Stadium Splash (minor 17)
          → Stadium (minor 18) — stadium minigame
            → Results (minor 13)
```

Major transition: `MJRKIND_MENU` → `MJRKIND_CITY` when entering 3D.

### Top Ride

```
Main Menu (minor 2)
  → Top Ride course/player select
    → Gameplay (minor 19) — top ride race
```

Major transition: `MJRKIND_MENU` → `MJRKIND_TOP` when entering gameplay.

The per-mode character/machine select screens between the settings minors and gameplay are documented in `css-system.md`.

## Main Menu

The main menu runs as minor 2 within `MJRKIND_MENU` and handles mode selection and options.

### MainMenuData (GameData + 0x30)

```c
struct MainMenuData {
    u8 input_lockout;                     // 0x30
    u8 x31;                               // 0x31
    u8 menu_name_tex_idx;                 // 0x32
    u8 is_in_submenu;                     // 0x33
    u8 top_menu;                          // 0x34
    u8 cursor_val[2];                     // 0x35
    u8 depth;                             // 0x37 — 0 or 1, indexes cursor_val
    MainMenuSubmenuKind submenu_kind : 8; // 0x38
    MajorKind major_kind : 8;             // 0x39
    // ... additional fields
};
```

### Menu Enums

Both enums are defined in `menu.h`.

**MainMenuTopMenuKind** (prefix `MAINMENU_TOPMENU_`, in order from 0): `AIRRIDE`, `TOPRIDE`, `CITY`, `OPTIONS`, `LAN`

**MainMenuSubmenuKind** (prefix `MAINMENU_SUBMENU_`, in order from 0): `AIRRIDE`, `TOPRIDE`, `CITY`, `OPTIONS`, `AIRRIDE_FREERUN`, `TOPRIDE_FREERUN`, `AIRRIDE_RECORDS`, `TOPRIDE_RECORDS`, `CITY_RECORDS`

### Key Functions

| Function | Address | Size | Purpose |
|----------|---------|------|---------|
| `MainMenu_Init` | 0x80131d44 | 0x108 | Menu initialization |
| `MainMenu_InitAllVariables` | 0x80007808 | 0x38 | Initializes all menu variables |
| `MainMenu_MinorEnter` | 0x80018548 | 0x47c | Minor scene entry |
| `MainMenu_MinorExit` | 0x80015be8 | 0x1cc | Minor scene exit |
| `MainMenu_MinorThink` | 0x800189e4 | 0x20 | Per-frame minor think |
| `MainMenu_SelectModeThink` | 0x80015e80 | 0x4b0 | Mode selection (AR/TR/CT) |
| `MainMenu_OptionsThink` | 0x80016330 | 0xa70 | Options menu handling |
| `MainMenu_InitCursor` | 0x800184d4 | 0x74 | Cursor setup |
| `loadMainMenuMusic` | 0x8000bba0 | 0x50 | Load/play menu music |
