# Scene System

Kirby Air Ride runs a two-level scene hierarchy. **Major scenes** (`MajorKind`) are the top-level
modes - title, main menu, Air Ride, Top Ride, City Trial, memory card prompt - and **minor
scenes** (`MinorKind`) are the screens inside them: a rules screen, a select screen, the 3D round,
a results screen. The game loops over majors, and within each major over minors, until the major
asks to exit. Every minor brings its own load, exit and per-frame callbacks, so knowing which minor
is running - and which major decides what comes next - is the starting point for any mod that
reacts to where the player is.

## Descriptors

A major is a `MajorSceneDesc` (12 bytes): its own `major_id`, the default `next_major_id`, the
`initial_minor_id` to enter, and two callbacks - `cb_Enter`, run once when the major starts, and
`cb_ExitMinor`, run after each of its minors exits. The vanilla table is `stc_major_scene_desc`
(0x80495058).

A minor is a `MinorSceneDesc` (0x24 bytes): its `idx`, `cb_Load` (once on entry), `cb_Exit` (once
on exit), five per-frame callbacks, and the `preload_kind` copied into `Preload.kind`. The vanilla
table is `stc_minor_scene_desc` (0x80495154), 41 entries in `MinorKind` order. There is no
per-major minor list: a minor is found by scanning the whole table for its `idx`, whatever major
is running, and the choice of the next minor belongs to the major's `cb_ExitMinor`.

hoshi copies both tables into larger arrays at boot, fills the vanilla ones with -1 and repoints
every reader, so at runtime the vanilla addresses hold no descriptors.

## Lifecycle

`Gm_Major` (0x800082d0) runs the outer loop, one iteration per major:

1. `major_cur = major_pending`, and the matching `MajorSceneDesc` is looked up.
2. `major_pending` is set to the descriptor's `next_major_id`, `request_major_exit` is cleared,
   and `Scene_SetNextMinor(initial_minor_id)` queues the first minor.
3. `cb_Enter` runs, if present.
4. While `request_major_exit` is clear: `Gm_Minor` runs one minor, then `cb_ExitMinor` runs.
   It is skipped after the memory card prompt minor (38) when that minor ran inside another
   major, and during a reset.

`Gm_Minor` (0x80008ad4) runs one minor:

1. The queued minor (`GameData.minor_next`, +0x7d8) becomes current. If the memory card was
   pulled, minor 38 (`MNRKIND_CARD`) runs instead.
2. `Scene_InitHeaps` (0x8000891c) resets the scene heaps, then `SceneChangeTasks` (0x80006540)
   installs the minor's per-frame callbacks.
3. `cb_Load` runs.
4. `loop` (0x80006b58) calls `updateFunction` (0x800067a4) once per frame until
   `Scene_ExitMinor` is signalled. Each frame calls, when non-null, `cb_ThinkPreGObjProc`
   (before the GObj procs), `cb_ThinkPostGObjProc`, `cb_ThinkPostGObjProc2`, `cb_ThinkPreRender`
   and `cb_ThinkPostRender`.
5. `cb_Exit` runs, unless the console is resetting.

The transition contract: a minor's per-frame code calls `Scene_ExitMinor()`; the major's
`cb_ExitMinor` then either queues another minor with `Scene_SetNextMinor()`, or leaves the major
with `Scene_SetNextMajor()` followed by `Scene_ExitMajor()`.

| Function | Address | Purpose |
|---|---|---|
| `Scene_GetCurrentMajor` | 0x8000aea8 | Current `MajorKind` |
| `Scene_GetCurrentMinor` | 0x8000aecc | Current `MinorKind` |
| `Scene_SetNextMajor` | 0x800082a0 | Queue the next major (from `cb_ExitMinor`) |
| `Scene_ExitMajor` | 0x80008220 | Set `request_major_exit` |
| `Scene_SetNextMinor` | 0x800088c8 | Queue the next minor (from `cb_ExitMinor`) |
| `Scene_ExitMinor` | 0x800064f0 | End the current minor (from its per-frame code) |
| `Scene_SetDirection` / `Scene_GetDirection` | 0x8000a498 / 0x8000a474 | The button bits a screen exited with, read by `cb_ExitMinor` |
| `Scene_InitHeaps` | 0x8000891c | Reset the scene heaps |

## Teardown Runs No Destructors

`Scene_InitHeaps` calls `Preload_ResetHeaps` before the incoming minor's `cb_Load`. Every GObj and
every heap allocation of the outgoing minor is reclaimed wholesale, **without its destructor
running**. A minor's `cb_Exit` only tears down what it destroys explicitly.

Heap memory is therefore safe, but anything held outside the heap leaks: audio tracks and sound
generators in `Audio3D`, slots in the 256-entry FGM instance pool, `AudioEmitterData` slots. A
sound still playing is the case that matters, because it stays audible into the next scene.
Vanilla does little about it: minor 18's `cb_Exit` (`Stadium_ExitMinor`, 0x80014d5c) calls
`BGM_Stop` and `FGM_StopAll`, and minor 17's `cb_Exit` (0x800462a4) calls `FGM_StopAll`. A scene
that creates objects holding non-heap resources has to release them in `cb_Exit`, while the
objects are still alive.

## Majors and Their Callbacks

| Major | `cb_Enter` | `cb_ExitMinor` |
|---|---|---|
| `MJRKIND_TITLE` (2) | `TitleScreen_MajorEnter` (0x8000da34) | `TitleScreen_MinorExit` (0x8000e1d8) |
| `MJRKIND_MENU` (3) | `MainMenu_MajorEnter` (0x80015bb4) | `MainMenu_MinorExit` (0x80015be8) |
| `MJRKIND_AIR` (4) | 0x8003d9dc | `MinorExit_AirRideMachineSelect` (0x8003dcd8) |
| `MJRKIND_TOP` (5) | `TopRide_MajorEnter` (0x8003ed34) | `TopRide_MinorExit` (0x8003eed8) |
| `MJRKIND_CITY` (6) | `CityTrial_MajorEnter` (0x8003fc5c) | `CityTrial_MinorExit` (0x8003fdd4) |
| `MJRKIND_CARD` (18) | `CardPrompt_MajorEnter` (0x800476b8) | `CardPrompt_MinorExit` (0x800477ac) |
| `MJRKIND_LAN` (19) | `LAN_MajorEnter` (0x8004f654) | 0x8004f714 |

Each mode's `cb_ExitMinor` is a state machine on a scene-state byte in `GameData`: it reads the
exit direction, advances the state, and queues the next minor or leaves the major. The checklist
new-unlock flows run from these callbacks too. The three mode majors list their rules screen as
the initial minor, but their `cb_Enter` queues the first select screen in its place (or a
tutorial movie on a first visit).

## Flow Per Mode

The main menu (`MJRKIND_MENU`, minor 2) picks a mode by leaving its major: `MainMenu_MinorExit`
calls `Scene_SetNextMajor` with the mode's major. Every screen of a mode - select screens, rules,
the round and its results - then runs as a minor of that mode's major.

| Mode | State byte | Minors in order |
|---|---|---|
| Air Ride | `GameData+0x364` | course select (6, skipped when Course Selection is off) -> player select (8) -> round (18) |
| Top Ride | `GameData+0x384` | course select (7) -> player select (9) -> round (19) |
| City Trial | `GameData+0x39b` | player select (10) -> round (18) -> stat graph (16) -> stadium splash (17) -> stadium (18) -> results (13) |

The rules screens (minors 3, 4 and 5) are side trips: Z on a select screen (exit direction bit
0x10) opens the mode's rules screen, which returns to the select screen. City Trial's Stadium mode
starts at the stadium select (21). In `CityTrial_MinorExit` the state byte reads 3 player select,
4 in round, 5 stat graph, 6 stadium splash, 7 stadium, 8 results; the stadium state calls
`Stadium_RecordResults` (0x8004223c) to record each player's result for the stadium kind.

**Top Ride's round is minor 19 (`MNRKIND_TOPRIDE`), not the shared minor 18.** It runs a separate
2D engine and never goes through `SceneLoad_3D`, so code hooked into the 3D load, pause or exit
does not run in a Top Ride round.

## Mode and Location Within a Major

`Scene_GetCurrentMajor` only says which mode is active. Each mode has a finer setting picked from
its menu, kept in `GameData`:

| Major | Mode enum | Values | Accessor | Field |
|---|---|---|---|---|
| `MJRKIND_AIR` | `AirRideMode` | `AIRRIDEMODE_RACE` (0), `_TIME` (1), `_FREE` (2) | `Gm_GetAirRideMode` (0x8003d5f0) | `airride_mode` (+0x35d) |
| `MJRKIND_CITY` | `CityMode` | `CITYMODE_TRIAL` (0), `_STADIUM` (1), `_FREERUN` (2) | `Gm_GetCityMode` (0x8003f6cc) | `city.mode` (+0x399) |
| `MJRKIND_TOP` | `TopRideMode` | `TOPRIDEMODE_RACE` (0), `_TIME` (1), `_FREE` (2) | `TopRide_GetMode` (0x8003ea9c) | `topride_mode` (+0x381) |

Three predicates answer where the player is rather than what was picked:

| Predicate | Address | True when |
|---|---|---|
| `Gm_IsInCity` | 0x8000acb0 | The loaded stage is the open City Trial map |
| `CityTrial_IsInStadium` | 0x8000ad48 | The loaded stage is a stadium (`city_kind` 7-18) |
| `Gm_IsLegendaryAssembling` | 0x8000c934 | A Dragoon or Hydra assembly cinematic is running (the cinematic GObj at `GameData+0xa8c`) |

## The Title Screen's Attract Demo

Idling on the title screen starts an attract demo, and the demo is a **real round**:
`TitleScreen_MinorExit` sets the mode up as a menu would and enters minor 18 - or 19 for Top Ride -
**without leaving `MJRKIND_TITLE`**. Everything downstream runs: the stage loads, the City Trial
item registry is built, items spawn and get collected, and every 3D load and exit hook fires.

`TitleScreenData` (`GameData+0xc`) drives it:

| Field | Meaning |
|---|---|
| `autodemo_state` (+0x15) | `TitleAutoDemoState`: 0 title screen, 1 attract round, 2 movie, 3 second movie of a pair |
| `autodemo_slot` (+0x17) | Rotating slot 0-3, advanced on every return to the title: 0 and 2 Air Ride, 1 City Trial, 3 Top Ride. Read by `TitleScreen_GetAutoDemoKind` (0x8000af94) |

The City Trial slot calls `Gm_SetCityMode(CITYMODE_TRIAL)` and `CityTrial_Init`, sets all four
`PlayerDesc.p_kind` to `PKIND_NONE`, then three of them back to `PKIND_CPU` with random
characters. The demo has **no human player**, and `Gm_IsInCity`, `Gm_GetCityMode` and the stadium
accessors answer exactly as in a round the player started, so a gate written from mode and stage
alone lets the demo through.

`Gm_IsAutoDemo()` (an inline in hoshi: current major is `MJRKIND_TITLE` and `autodemo_state` is 1)
is the discriminator. Per-player work is usually safe because the demo has only CPUs; anything
counted for the round regardless of who did it is not. Vanilla avoids the problem structurally: its
unlock flow runs in `CityTrial_MinorExit`, the `MJRKIND_CITY` `cb_ExitMinor`, which the demo never
reaches.

## Main Menu

The main menu is minor 2 of `MJRKIND_MENU`. Its state is `MainMenuData` at `GameData+0x30`: the
`top_menu` selection, a `submenu_kind`, `is_in_submenu` (which panel's think runs: 0 mode select,
1 options list, 2 rumble, 3 sound test, 4 movies, 5 data delete), and a two-entry `cursor_val[]`
indexed by `depth`, so entering a submenu keeps the outer cursor.

| Function | Address | Purpose |
|---|---|---|
| `MainMenu_MinorEnter` | 0x80018548 | Minor 2's `cb_Load` |
| `MainMenu_MinorThink` | 0x800189e4 | Per-frame think |
| `MainMenu_SelectModeThink` | 0x80015e80 | Mode selection |
| `MainMenu_OptionsThink` | 0x80016330 | Options list |
| `MainMenu_Init` | 0x80131d44 | Menu archive and element setup |
| `MainMenu_InitAllVariables` | 0x80007808 | Resets every menu variable |
| `loadMainMenuMusic` | 0x8000bba0 | Starts the menu music |

`ScMenuCommon` (`stc_scene_menu_common`, 0x80558788, returned by `Gm_GetMenuData` 0x801311e0) is
the state every menu and select screen shares: camera, light, element slots and text.
`ScMenuSelect` (`stc_menu_select`, 0x804962b0) holds the select screens' model data.
