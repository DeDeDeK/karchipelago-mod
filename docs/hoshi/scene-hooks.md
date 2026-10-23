# Scene Hooks

hoshi gives every mod a set of lifecycle callbacks in its `ModDesc` - boot, save, scene change,
3D load and exit, pause, per frame - each fired from a hook hoshi patches into the game's scene
code. It also relocates the game's scene tables so a mod can install its own scenes or wrap a
vanilla minor's callbacks. Picking the right callback, and knowing exactly where in the game's
flow it runs, is most of what decides whether mod code runs in the right scenes.

## Lifecycle Callbacks

Each callback is a `ModDesc` function pointer; hoshi calls it for every installed mod that sets
it, in install order. The hook sites sit inside vanilla functions:

| Callback | Hook site | Runs |
|---|---|---|
| `OnBoot` | - | Once, when the mod is installed at boot |
| `OnSaveInit` | `Gm_InitData` (0x80007630) | Every boot, after the mod's save block is filled with defaults and before the card is read; only for a mod with save data |
| `OnSaveLoaded` | the memory card prompt (0x80047834, 0x80047720, 0x80047718) | Once per boot, after the card file is read or created, or when no card is present; for every mod |
| `OnSceneChange` | `SceneChangeTasks` (0x8000678c) | Every minor change, after the scene heaps are reset and before the minor's `cb_Load` |
| `OnMainMenuLoad` | `MainMenu_MinorEnter` (0x80018994) | The main menu minor loads |
| `OnPlayerSelectLoad` | `CSS_airRide_ModeDispatch` (0x8002a358), `CitySelect_MinorLoad` (0x8003b48c) | The Air Ride or City Trial player select loads; not Top Ride's |
| `On3DLoadStart` | `SceneLoad_3D` (0x80014448) | Before the 3D round is built |
| `On3DLoadEnd` | `SceneLoad_3D` (0x80014d3c) | After players, machines and the stage exist |
| `On3DPauseStart` | `pauseMain` (0x80041160) | A 3D round pauses, before the pause HUD is built; gets the pausing player |
| `On3DPauseEnd` | `pauseMain` (0x80041174) | After the pause HUD, camera zoom and rumble stop; gets the pausing player |
| `On3DUnpause` | `Gm_HidePauseHUD` (0x80113a30) | The round unpauses; gets the pausing player |
| `On3DExit` | `Stadium_ExitMinor` (0x80015274) | At the end of minor 18's `cb_Exit` |
| `OnTopRideLoadEnd` | `TopRide_SceneLoad` (0x80008fac) | After a Top Ride round (minor 19) is initialized |
| `OnFrameStart` | `updateFunction` (0x80006844) | Every tick, in every scene, before any game logic |
| `OnFrameEnd` | `updateFunction` (0x80006a30) | Every tick that ran the GObj procs, before the engine frame counter increments |
| `OnShowHUD` | `Gm_ShowHUD` (0x801126d8) | The game shows the round HUD |
| `OnHideHUD` | `Gm_HideHUD` (0x80112968) | The game hides the round HUD |

Notes on the callbacks:

- **3D versus Top Ride.** The `On3D*` callbacks fire only for minor 18, the Air Ride and City
  Trial round (stadiums and the title screen's attract demo included). A Top Ride round is minor
  19 with its own engine, so per-round Top Ride state is set up in `OnTopRideLoadEnd`, and none of
  the 3D pause or exit callbacks run there.
- **Heap scope.** `OnBoot` runs while hoshi swaps `HSD_MemAlloc` for a persistent allocator, so
  what it allocates lives for the whole session. Every other callback allocates from the scene
  heap, which the next minor change reclaims without running any destructor. Objects that must
  span scenes are recreated in `OnSceneChange`.
- **HUD state.** hoshi keeps `g_hud_is_hidden` (0x8048b5d9) in step with the show and hide
  callbacks, and clears it at `On3DLoadEnd`.
- **Imports.** `OnSaveLoaded` is the first callback that runs after every mod's `OnBoot`, so it is
  where a mod imports another mod's API.
- **Frame hooks.** `OnFrameStart` runs through every pause; `OnFrameEnd` stops under the debug
  pause and on ticks the engine-speed setting drops. Neither knows the scene, so a handler tests
  where it is.

## Custom Scenes

At boot hoshi copies the vanilla `MajorSceneDesc` and `MinorSceneDesc` tables into arrays twice
their size, fills the vanilla tables with -1 so a stray read is obvious, and repoints every reader:
the seven sites in `Scene_InitHeaps` and `Gm_Minor` that load the minor table go through
trampolines, and a hook at 0x8000836c replaces the major loop in `Gm_Major` with an equivalent
loop over the copy. The replacement loop also calls `Scene_InitHeaps` once before the first major,
so the game can boot into a scene other than the title.

| Function | Use |
|---|---|
| `Hoshi_InstallMajorScene(desc)` | Appends a `MajorSceneDesc`, assigns it the next free `major_id` and returns it |
| `Hoshi_InstallMinorScene(desc)` | Appends a `MinorSceneDesc`, assigns it the next free `idx` and returns it |
| `Hoshi_GetMajorScenes()` / `Hoshi_GetMinorScenes()` | The relocated arrays |

The copy keeps the vanilla order, so `Hoshi_GetMinorScenes()[kind]` is the descriptor of vanilla
minor `kind`.

## Wrapping a Vanilla Minor

To run code around a vanilla minor's callback, save the descriptor's pointer, write a wrapper into
the slot, and call the saved pointer from the wrapper. It is a plain C call, so it has none of the
register hazards of a code patch.

A `CODEPATCH_HOOKCREATE` hook is the wrong tool for a function's first instruction. The hook
replaces one instruction with a branch to an injection that calls the mod function with a bare
`bl`, clobbering LR and the volatile registers with no save. At a function's entry LR still holds
the caller's return address: the prologue's `mflr` then saves the injection's address instead, and
the function's `blr` returns into the injection and runs the function again, forever. A hook is
safe only where the clobbered registers are dead, such as right before another `bl`.
