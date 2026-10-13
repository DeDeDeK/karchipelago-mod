# Title Screen

The archipelago mod rebrands the title screen. It keeps the vanilla "KIRBY" text and blue swoosh
and replaces the "AIR RIDE" subtitle with a KARchipelago subtitle and a cluster of six Kirbys, puts
the Archipelago Star under the idle demo rider, and stamps the release version in the corner. The
rest of the title - background, menu, PRESS START - is vanilla. The work lives in the mod's title
subsystem, installed from `OnBoot`.

## Hooks

| Site | What runs |
|---|---|
| 0x8000d2b4, in `SceneLoad_TitleScreen` (0x8000d26c, the title minor's `cb_Load`) | Picks the demo machine, then loads `MnTitleKarchi.dat` into the title's scene heap with `Gm_LoadGameFile` |
| 0x8017b5d8, in `TitleScreen_CreateForegroundElements` (0x8017b4c0), after both title element GObjs exist | Swaps the logo |
| The title minor's `cb_ThinkPreGObjProc` (`TitleScreen_MinorThink`, 0x8000d698) | Wrapped: the audio fix and the version stamp, then the vanilla think |
| The title minor's `cb_Exit` (`SceneExit_TitleScreen`, 0x8000d5c8) | Wrapped: the cleanup below, then the vanilla exit |

The two wrappers go through `Hoshi_GetMinorScenes()[MNRKIND_TITLESCREEN]`, saving each vanilla
pointer and calling it from the wrapper. A code-patch hook would not work on `SceneExit_TitleScreen`:
its site would be the function's first instruction, where the hook's `bl` clobbers LR before the
prologue saves it, and the function would return into the hook forever.

## The Logo

The title foreground scene is `Gm_GetMenuData()->ScMenTitleFg_gobj`. The vanilla "AIR RIDE"
subtitle - its text and blue box - is its joint 14 in `GObj_GetJObjIndex`'s depth-first order, and
`JObj_SetFlagsAll(..., JOBJ_HIDDEN)` hides that whole subtree. Everything else in the foreground
stays.

The replacement is the `karchiTitleFg_scene_models` public of `MnTitleKarchi.dat`, a `JOBJSet`
array shaped like the vanilla `Sc*_scene_models`: an XLU root with one textured quad per piece (the
subtitle and the Kirbys). It is instantiated with `MenuElement_Create` (0x801388a8), which
registers `MainMenu_ElementGX` (0x80138ae0) as the render callback. That callback dereferences the
element's userdata and draws only when its visible bit (+0x8, 0x80) is set, so
`MenuElement_AddData(element, 99)` (0x80138a00) must follow; a static model needs no per-frame proc.

The pieces are RGBA8 textures - the logo's gradients band badly in RGB5A3 - on unlit, translucent
materials with material alpha 1.0 and `AM_MODULATE` on the texture, so each texture's own alpha
cuts the art out of its quad; change either and the quads draw as opaque rectangles. The archive is
generated from source PNGs by an authoring script, with the pieces' layout taken from a reference
image of the full logo. Animating the pieces would take an animated model and a
`TitleScreenForeground_Proc` (0x8017b424) plus `JObj_AddSetAnim` after `MenuElement_Create`, or a
per-frame material change from a think proc.

## The Demo Machine

The title's idle demo rider is configured in `SceneLoad_TitleScreen` by `li r4` operands passed to
`Ply_Set*`: the rider kind at 0x8000d340, `is_bike` at 0x8000d34c and the class-relative machine
slot at 0x8000d358, all 0 in vanilla. The load hook rewrites two of them with
`CODEPATCH_REPLACEINSTRUCTION` on every title entry, ahead of those instructions:

- If the Archipelago Star resolves to a machine kind and that kind is star-class, the demo becomes
  Kirby on it - the machine the goal awards, on show before it is earned.
- Otherwise it is King Dedede on the Wagon Star.

`is_bike` stays 0: the demo setup uses star-only machine state ids, and a wheel-class machine
crashes it. The choice is made per title entry rather than at boot because the Archipelago Star's
machine kind only resolves after every mod has booted.

## Demo Machine Audio

Every star requests the same two looping sounds at spawn, an engine loop and a surface loop. What
differs per kind is the engine loop's idle volume floor in its `MachineAudioParams` record
(`engine_idle_floor`, from `VcCommon.dat`, through `stc_machineAudioParams` indexed by `is_bike`
and class slot). `Machine_UpdateEngineLoop` (0x801dcb18) sets the loop's volume to
`speed term * engine_volume_coef + engine_idle_floor`, clamped to 1.0, and a volume near zero never
becomes a voice. The Warp Star's floor is 0.0, which is why the vanilla title is silent; only
Bulk, Wagon, Turbo, Jet and Formula have a nonzero floor, and theirs clamp to full volume while
parked. The surface loop's floor is too low to hear for every star.

Nothing in the title scene stops a playing loop: `SceneExit_TitleScreen` destroys only the movie
GObjs, and the scene teardown reclaims `MachineData` without running `Machine_Destroy`
(0x801c6b44). A loud demo machine would keep humming under the main menu and add another copy on
every title visit. The two wrappers handle it:

- **Think** sets the demo kind's `engine_idle_floor` to 0.0, saving the vanilla value once. The
  arithmetic then matches the Warp Star's, and a kind already at 0.0 passes through unchanged. It
  runs from the think rather than the load hook because the audio record is loaded later in the
  title's `cb_Load`; a frame's delay is harmless, since the engine loop starts at volume 0.0 and
  ramps up, and the record is re-read every frame.
- **Exit** restores the floor - the record is shared game data, read by the same kind in City
  Trial - then, for every machine on the `GAMEPLINK_MACHINE` list, stops the surface and engine
  loops with `FGM_Stop` (0x8005e7d8) and calls `Machine_FreeAudioEmitter` (0x801dc618). The loops
  hold FGM instances even at volume 0, and the audio tracks and emitter live in static `Audio3D`
  slots the heap reset does not reclaim; vanilla leaks all of them on every title visit. Stopping
  the loops first leaves the emitter with no voice, so `AudioEmitter_Free` completes at once. The
  machines are found through the GObj list because the title demo machine is never registered in
  `PlayerData`: `Ply_GetMachineGObj(0)` returns NULL throughout the title.

`MachineData.xc39` bit 0 (`MACHINE_HITREACT_HOLD`) also silences both loops, but it latches the
whole presentation - it freezes the model animation, hides part of the model and stops the
machine's effects, exhaust included - so it is not an audio mute.

## Version Stamp

The release version is drawn in the bottom-right corner as `v<version>` on a translucent black
panel. The string is `KARCHIPELAGO_VERSION`, a hand-maintained literal matching the release tag,
also printed once at boot. It is separate from `ModDesc.version`, which carries the mod's exported
API version that `Hoshi_ImportMod` checks.

The stamp is a `Text` on hoshi's 640x480 screen canvas (`Hoshi_CreateScreenText`), created by the
think wrapper and destroyed by the exit wrapper: a `Text` left alone is not reliably reclaimed by
the scene teardown and goes on drawing over the next scene. The think creates it only while the
title foreground GObj exists, because the boot cinematic runs in the same minor without it, and
destroys it if the foreground goes away. It also waits for hoshi's canvas, which is rebuilt on
every scene change, since `Text_CreateText` faults on an empty canvas list.

`color` and `viewport_color` are set before `Text_AddSubtext`, which bakes them into the subtext.
The subtext is placed one pad in from the left and measured with `Text_GetWidthAndHeight`, which
leaves out the subtext's own offset, so `aspect` is the measured box plus a pad on each side; the
scaled `aspect` sizes the panel, and `trans` places its top-left from the canvas's bottom-right
corner minus a margin.
