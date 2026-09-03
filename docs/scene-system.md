# Scene System

Kirby Air Ride runs a two-level scene hierarchy: **major scenes** (`MajorKind`) are the top-level game modes (Title, Menu, Air Ride, City Trial, ...), and **minor scenes** (`MinorKind`) are the sub-screens within a major (settings, map select, player select, 3D gameplay, ...). The game loops over majors; within each major it loops over minors until a major exit is requested. Every minor has load / think / exit callbacks that drive its per-frame logic. Both enums, and the descriptor structs below, are declared in `externals/hoshi/include/scene.h`.

## Mode Within a Major

`Scene_GetCurrentMajor()` only says which top-level mode is active. Each gameplay major has a finer **mode** chosen from the menu, stored in `GameData` and read through a dedicated accessor. Mod gating code discriminates on the major first, then the mode.

| Major | Mode enum (`game.h`) | Values | Accessor (addr) | GameData field |
|-------|----------------------|--------|-----------------|----------------|
| `MJRKIND_AIR` | `AirRideMode` | `AIRRIDEMODE_RACE` (0), `_TIME` (1), `_FREE` (2) | `Gm_GetAirRideMode()` (0x8003d5f0) | `airride_mode` @ 0x35d |
| `MJRKIND_CITY` | `CityMode` | `CITYMODE_TRIAL` (0), `_STADIUM` (1), `_FREERUN` (2) | `Gm_GetCityMode()` (0x8003f6cc) | `mode` @ 0x399 |
| `MJRKIND_TOP` | `TopRideMode` | `TOPRIDEMODE_RACE` (0), `_TIME` (1), `_FREE` (2) | `TopRide_GetMode()` (0x8003ea9c) | `topride_mode` @ 0x381 |

Two stage-state predicates sit alongside the mode accessors and answer a different question - *where the player currently is*, not *what was picked in the menu*:

| Predicate | Address | True when |
|-----------|---------|-----------|
| `CityTrial_IsInStadium()` | 0x8000ad48 | The loaded stage is a stadium (`city_kind` 7-18). The stadium test mod code uses. |
| `CityTrial_IsInCity()` | 0x8000acb0 | The loaded stage is the open City Trial map. |
| `Gm_IsLegendaryAssembling()` | 0x8000c934 | A Dragoon/Hydra assembly cinematic is running (reads the cinematic GObj pointer at `GameData+0xa8c`). Not a mode or stadium check. |

## The Title Screen's Attract Demo

Idling on the title screen starts an attract demo, and that demo is a **real 3D round**: `TitleScreen_MinorExit` (0x8000e1d8, `gmautodemo.c`) sets the mode up exactly as a menu would and then enters minor 18 (`MNRKIND_3D`) - or minor 19 for Top Ride - **without leaving `MJRKIND_TITLE`**. Everything downstream runs: the stage loads, `CityItemSpawn_Init` builds the item registry, `On3DLoadStart` / `On3DLoadEnd` / `On3DExit` fire, items spawn and get collected.

Two fields in `TitleScreenData` (`GameData+0xc`) drive it:

| Field | Meaning |
|-------|---------|
| `autodemo_state` (0x15) | `TitleAutoDemoState`: 0 title screen, 1 attract round, 2 movie, 3 second movie of a pair. |
| `autodemo_slot` (0x17) | Rotating attract slot 0-3, advanced on every hand-back to the title screen. Slots 0 and 2 run Air Ride, slot 1 City Trial, slot 3 Top Ride. Read by `TitleScreen_GetAutoDemoKind()` (0x8000af94). |

The City Trial slot calls `Gm_SetCityMode(CITYMODE_TRIAL)` and `CityTrial_Init`, then sets all four `PlayerDesc.p_kind` to `PKIND_NONE` and three of them back to `PKIND_CPU` with randomly chosen characters (0x8000e108). So the demo has **no human player**, and `Gm_IsInCity()`, `Gm_GetCityMode()`, `Gm_GetCurrentStadiumKind()` and the rest answer exactly as they do for a round the player started - a mod gate written from mode and stage state alone lets the demo through.

`Gm_IsAutoDemo()` (`game.h`, `Scene_GetCurrentMajor() == MJRKIND_TITLE && autodemo_state == TITLEDEMO_ROUND`) is the discriminator. Anything that records progress has to consult it: per-player work is usually safe because the demo has only CPUs, but anything counted for the round regardless of who did it is not. Vanilla is immune for a structural reason rather than a check - its own unlock flow runs in `CityTrial_MinorExit`, the `MJRKIND_CITY` `cb_ExitMinor`, which the demo never reaches.

## Major Scenes

Each major is described by a `MajorSceneDesc` (12 bytes) holding its own id, the default next major, the first minor to enter, and two callbacks: `cb_Enter` (once on major entry) and `cb_ExitMinor` (after each minor exits). The vanilla table is `stc_major_scene_desc` at `0x80495058`.

### Major Scene Lifecycle

`Gm_Major` (0x800082d0, 0x5a4 bytes) runs the outer loop. At runtime hoshi redirects it into `Gm_MajorPatch` via a hook at 0x8000836c; the re-implemented loop is:

1. Call `Scene_InitHeaps()` once (hoshi addition, so the game can boot into a non-title scene).
2. Outer loop (infinite - one iteration per major):
   a. `major_cur = major_pending`
      b. Find the matching `MajorSceneDesc` in `major_scene_descs[]` (asserts if none)
   c. Set the default next major: `major_pending = next_major_id`
   d. `request_major_exit = 0`
   e. `Scene_SetNextMinor(initial_minor_id)`
   f. Call `cb_Enter()` if present
   g. Inner loop while `!request_major_exit`: call `Gm_Minor()`, then `cb_ExitMinor()` after that minor completes (skipped for the memcard-unplug scene and during reboot)

### Per-Major Callbacks

Only six majors populate the callback slots; records in `stc_major_scene_desc` are ordered by `major_id`.

| Major | `cb_Enter` | `cb_ExitMinor` |
|-------|-------------------|------------------------|
| `MJRKIND_TITLE` | `TitleScreen_MajorEnter` (0x8000da34) | `TitleScreen_MinorExit` (0x8000e1d8) |
| `MJRKIND_MENU` | `MainMenu_MajorEnter` (0x80015bb4) | `MainMenu_MinorExit` (0x80015be8) |
| `MJRKIND_TOP` | `TopRide_MajorEnter` (0x8003ed34) | `TopRide_MinorExit` (0x8003eed8) |
| `MJRKIND_CITY` | `CityTrial_MajorEnter` (0x8003fc5c) | `CityTrial_MinorExit` (0x8003fdd4) |
| `MJRKIND_CARD` | `CardPrompt_MajorEnter` (0x800476b8) | - |
| `MJRKIND_LAN` | `LAN_MajorEnter` (0x8004f654) | - |

`cb_ExitMinor` is the major's **minor-transition decider**: a jump table on the major's own scene-state byte in `GameData` that advances the sub-scene, (re)initializes data, runs the checklist new-unlock flow, or exits the major.

- `CityTrial_MinorExit` switches on `GameData+0x39b` (3 = player select, 4 = in game, 5 = properties graph, 6 = stadium splash, 7 = stadium, 8 = results). It calls `CityTrial_Init` (0x8003f988), snapshots per-player stats, runs `CityTrial_CheckForNewUnlocks` (0x8004db74), and in the stadium-results state calls `Stadium_RecordResults` (0x8004223c) to record the round's per-player result (keyed by stadium kind) into the clear-checker results block.
- `TopRide_MinorExit` switches on `GameData+0x384`, branching on `topride_mode` (`GameData+0x381`). It drives course-select -> race -> results and runs the Top Ride new-unlock flow.

## Minor Scenes

A minor's callbacks live in a `MinorSceneDesc` (0x24 bytes; vanilla table `stc_minor_scene_desc` at `0x80495154`): `cb_Load` once on entry, `cb_Exit` once on exit, five per-frame Think callbacks, and the heap kind copied into Preload.

Each major additionally owns a table of `MinorScene` entries mapping minor IDs to a heap kind, a `minor_prep` (initializes that minor's data) and a `minor_decide` (picks the next minor), plus the `minor_kind` index into the `MinorSceneDesc` array. Two `void *` data slots are shared between minors of the same major, which is how a select screen hands its choices to gameplay. The active major's table is selected at major entry and lives at `0x807e0580` at runtime - a heap-region address, not a stable symbol.

### Minor Scene Lifecycle

`Gm_Minor` (0x80008ad4, 0x324 bytes) handles one minor's lifecycle:

1. Look up the `MinorScene` entry for the current minor
2. Call `minor_prep()`
3. Call `cb_Load()`
4. Per-frame loop (`loop` @ 0x80006b58 -> `updateFunction` @ 0x800067a4) until `Scene_ExitMinor()` is signalled
5. On exit: call `cb_Exit()`, then `minor_decide()` to determine the next minor (or major)

### Per-Frame Callback Execution Order

`updateFunction` (0x800067a4, 0x3b4 bytes) copies the minor's five Think pointers into a per-frame callback table and `bctrl`s each non-null entry in strict order: `cb_ThinkPreGObjProc` (before GObj processing), `cb_ThinkPostGObjProc` (after GObj procs update), `cb_ThinkPostGObjProc2` (debug input in vanilla), `cb_ThinkPreRender`, `cb_ThinkPostRender`.

### Top Ride Uses Minor 19

Top Ride gameplay runs as **minor 19** (`MNRKIND_19`), *not* the shared minor 18 (`MNRKIND_3D`) that Air Ride and City Trial (including stadiums) use. It has its own 2D engine and never goes through the 3D-scene instantiation path, so:

- `On3DLoadStart` (0x80014448) and `On3DLoadEnd` (0x80014d3c) **do not fire** for Top Ride.
- `OnTopRideLoadEnd` (hook at 0x80008fac, inside minor 19's `cb_Load` `TopRide_SceneLoad` at 0x80008df8) is the Top Ride load notification. Any mod that needs to initialize per-round Top Ride state must use it.
- `On3DPause` / `On3DUnpause` / `On3DExit` are likewise 3D-only.

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

The transition contract: a **MinorThink** calls `Scene_ExitMinor()` to trigger the decide step; a **SceneDecide** calls either `Scene_SetNextMinor()` to enter another minor, or `Scene_SetNextMajor()` followed by `Scene_ExitMajor()` to enter another major.

### Teardown Reclaims Memory Without Running Destructors

`Gm_Minor()` calls `SceneChange_InitHeaps()` -> `Preload_ResetHeaps()` before the incoming minor's
`cb_Load`. This resets the scene heaps wholesale: every GObj and every heap-allocated struct from
the outgoing scene is reclaimed **without its destructor running**. A minor's `cb_Exit` only tears
down what it explicitly destroys - anything it leaves behind is reclaimed as memory, but its
teardown code never executes.

The practical consequence is that heap memory is safe while **non-heap resources leak**. Anything
an object registered in a static global array or fixed pool - audio tracks and sound generators in
`Audio3D`, entries in the 256-slot FGM instance pool, `AudioEmitterData` slots - survives the heap
reset because the destructor that would have released it never ran. A sound still playing is the
case that matters most, because it stays audible into the next scene.

Vanilla does very little about this. No `cb_Exit` in the table destroys a machine, and the only
scene-exit audio hygiene anywhere is `Stadium_ExitMinor` (`0x80014d5c`), which calls `BGM_Stop` then
`FGM_StopAll`, and minor 17's `cb_Exit` (`0x800462a4`), which calls `FGM_StopAll`. So a scene that
creates objects holding non-heap resources must release them in `cb_Exit` while the objects are
still alive - the destructor will not run.

## Static Data & Runtime Tables

| Pointer | Address | Contents |
|---------|---------|----------|
| `stc_major_scene_desc` | 0x80495058 | Vanilla major scene descriptor table |
| `stc_minor_scene_desc` | 0x80495154 | Vanilla minor scene descriptor table |
| `stc_scene_menu_common` | 0x80558788 | `ScMenuCommon` - shared menu/select screen state (`Gm_GetMenuData()` @ 0x801311e0) |
| `stc_menu_select` | 0x804962b0 | `ScMenuSelect` - select screen GObj/model data |
| Runtime minor table | 0x807e0580 | `MinorScene` entries for the active major (runtime, heap-region - not a stable symbol) |

## Hoshi Scene Extension

Hoshi extends the scene system in `externals/hoshi/src/more_scenes.c` so mods can install custom scenes:

1. **Table relocation** (`Scenes_CopyVanilla`): copies the vanilla `MajorSceneDesc` and `MinorSceneDesc` tables into larger statically allocated arrays (`major_scene_descs[MJRKIND_NUM*2]`, `minor_scene_descs[MNRKIND_NUM*2]`), then `memset`s the vanilla tables to `-1` to catch unpatched reads. `major_scene_num` / `minor_scene_num` track how many entries are live.
2. **Reference patching** (`Scenes_ApplyPatches`): patches the 7 vanilla code sites that load the minor scene table address, via ASM trampolines (`minor_scene_asm_1..7`), at `0x80008978`, `0x80008b1c`, `0x80008b90`, `0x80008c04`, `0x80008c70`, `0x80008cc8`, `0x80008d78`. The single major-table site at `0x80008374` is covered by the loop replacement below instead of a trampoline.
3. **Major loop replacement**: `CODEPATCH_HOOKCREATE` at `0x8000836c` (inside `Gm_Major`) redirects into `Gm_MajorPatch`, which re-implements the outer major loop over the relocated `major_scene_descs[]` and adds the initial `Scene_InitHeaps()` call.
4. **Install API**: `Scenes_InstallMajorScene()` / `Scenes_InstallMinorScene()` append a descriptor to the relocated array, assign the new ID, and bump the count. Exposed to mods through the hoshi func table as `Hoshi_InstallMajorScene` / `Hoshi_InstallMinorScene` (plus `Hoshi_GetMajorScenes` / `Hoshi_GetMinorScenes`, which mods also use to wrap a *vanilla* minor's callbacks by saving the existing pointer and writing their own).

## Hoshi Lifecycle Callbacks

Hoshi provides `ModDesc` callbacks (`externals/hoshi/include/hoshi/mod.h`) that fire at points in the scene lifecycle. Each is a `CODEPATCH_HOOKCREATE` in `hoshi.c`:

| Callback | Hook Address | When It Fires |
|----------|-------------|---------------|
| `OnBoot` | - (mod install) | Once at mod load, persistent heap available |
| `OnSaveInit` | - (save setup) | When save data is created for this mod |
| `OnSaveLoaded` | - (card read) | After save data loaded from memory card |
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

The heap is destroyed and recreated on every scene change: HSD objects (CObj, JObj, GObj) do not persist across one. Use `OnSceneChange` to recreate any persistent processes or objects. `OnBoot` is the exception - it runs with a persistent heap, so allocations made there survive the entire runtime; every other callback uses the scene-scoped heap.

Mods boot in alphabetical order, so an import of another mod's API resolves in `OnBoot` only if the exporter sorts earlier. `OnSaveLoaded` is the first point past every mod's `OnBoot`, which makes it the place a failed import genuinely means "that mod is not in this build".

## Scene Flow Per Mode

### Air Ride

```
Main Menu (minor 2)
  -> Air Ride Settings (minor 3) - mode select: Race / Time Attack / Free Run
    -> Map Select (minor 6) - stage select
      -> Player Select (minor 8) - CSS: character/color select
        -> 3D Gameplay (minor 18) - actual race
```

Major transition: `MJRKIND_MENU` -> `MJRKIND_AIR` when entering 3D.

### City Trial

```
Main Menu (minor 2)
  -> City Trial Settings (minor 5) - game settings
    -> Player Select (minor 10) - CSS: player/machine/color select
      -> 3D Gameplay (minor 18) - city trial round
        -> Stadium Splash (minor 17)
          -> Stadium (minor 18) - stadium minigame
            -> Results (minor 13)
```

Major transition: `MJRKIND_MENU` -> `MJRKIND_CITY` when entering 3D.

### Top Ride

```
Main Menu (minor 2)
  -> Top Ride course/player select
    -> Gameplay (minor 19) - top ride race
```

Major transition: `MJRKIND_MENU` -> `MJRKIND_TOP` when entering gameplay.

## Main Menu

The main menu runs as minor 2 within `MJRKIND_MENU` and handles mode selection and options. Its state is `MainMenuData` at `GameData+0x30` (`menu.h`): a `top_menu` selection, a `submenu_kind`, and a two-entry `cursor_val[]` indexed by `depth` (0 or 1), so entering a submenu preserves the outer cursor. The `MainMenuTopMenuKind` and `MainMenuSubmenuKind` enums are in `menu.h`.

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
