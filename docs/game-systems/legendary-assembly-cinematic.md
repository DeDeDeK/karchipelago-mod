# Legendary Assembly Cinematic

The cutscene that plays when a City Trial player collects the third Dragoon or Hydra piece. The
world freezes and the HUD drops. A scripted camera swings in, the machine's parts fly together on
streaks of light, and the player comes out riding the assembled machine. Every machine-specific
decision in it is reachable at a `bl`, which is what lets a mod run the same cutscene for a
different machine with its own art.

It runs for 181 frames: a 28-frame lead-in, the 150-frame animation and a 3-frame tail.

## Entry

`LegendaryMachine_StartAssembly` (0x80283cf0) takes a `LegendaryAssemblyParams`: machine index
(0 = Dragoon, 1 = Hydra), player index, and the machine's world position, forward and up, copied
off `MachineData + 0x3e8` / `+0x418` / `+0x424`. Its one caller is the pickup arm of
`Machine_OnTouchItem` (0x801db34c), which first drops the machine's ColAnims with
`Machine_ResetColAnims` (0x801d633c) so none survive into the freeze. It cancels any running piece
hitstop, then calls `LegendaryMachine_CreateAssembly` (0x802838a0), which:

- creates the controller with `GObj_Create(0x21, 0x1f, 0)`. Its 0x54-byte state is pool-allocated
  and freed by `LegendaryMachine_FreeAssemblyState` (0x80283874);
- loads `VsDragoon.dat` or `VsHydra.dat` through `LegendaryMachine_LoadAssemblyArchive` (0x80283e18)
  and keeps its `vsData` public;
- sets the countdown to 28 and the phase to 1;
- freezes the world with `Gm_SetCinematicFreezeStage(0)` (`PAUSEKIND_EXPLODE`) and pauses both
  music slots (`BGM_PauseAll`, 0x8005e6c8);
- installs `LegendaryMachine_AssemblyThink` (0x802839b8) as the proc and publishes the GObj at
  `GameData + 0xa8c` (`Gm_SetLegendaryAssemblyGObj`, 0x8000c964).

`Gm_IsLegendaryAssembling` (0x8000c934) reads that pointer back. Its only consumers are two sites in
`Machine_OnTouchItem` (0x801db650, 0x801db738) that suppress piece pickups while a cinematic is up.
Nothing else stops a second cinematic, and a second one would tear down the first one's model GObjs.

`GameData + 0xa90` is a different object: the dramatic pause on a piece pickup.
`LegendaryPiece_CreateHitstop` (0x8028406c) builds it and `LegendaryPiece_HitstopThink`
(0x80284190) ticks it. It freezes with `PAUSEKIND_EXPLODE` for a countdown, then runs the world at a
scaled engine speed for a second interval. `LegendaryMachine_CancelPieceHitstop` (0x80283d70) kills
it before the cinematic starts, because the cinematic needs the freeze and the engine speed to
itself. Its accessors are `Gm_IsLegendaryHitstopActive` (0x8000c994), `Gm_GetLegendaryHitstopGObj`
(0x8000c9f4) and `Gm_SetLegendaryHitstopGObj` (0x8000c9c4).

## Phases

`LegendaryMachine_AssemblyThink` is a phase field plus a countdown, at `+0x50` / `+0x4c` of its
state.

**Phase 1**, after 28 frames, builds everything:

- Each of the two models gets its own GObj with a GX link on gx_link 26. The render callback,
  `LegendaryMachine_RenderAssemblyModel` (0x80283ed8), draws every pass but 3.
- `LegendaryMachine_BindAssemblyAnims` (0x80283f74) binds the joint animation with
  `JObj_AddFigaTreeAnim` (0x8006e2c0) and the material animation with `JObj_AddAnimAll`.
- Both models are placed with `JObj_SetFromBasis(1.0, jobj, forward, up, pos)` (0x80054414). Its
  matrix columns are `cross(forward, up)`, `forward`, `up` and `pos`, so the cinematic plays in
  world space at the pickup site, in machine-model units.
- `LegendaryMachine_GetAssemblyEndFrame` (0x80283f00) reads the end frame, 150, out of the glow
  model's FigaTree.
- `Gm_SetCinematicFreezeStage(1)` hides the HUD, and `PlyCam_CreateAnimCamera` opens the shot.
- `Ptcl_CreateCinematicRenderPass` (0x80233b04) adds a particle pass for gx_link 26.
- `Ply_EnterLegendaryAssembly` (0x8022d6b4) poses the rider, and `SFX_PlayFullVolume` plays 0x130008
  (Dragoon) or 0x130007 (Hydra).

**Phase 2** calls `LegendaryMachine_AdvanceAssemblyAnims` (0x80283f24) each frame until the end
frame. Then it destroys the camera (`PlyCam_DestroySlot`, 0x800b94c8), calls
`Ply_ExitLegendaryAssembly` (0x8022d71c), destroys both models and clears `GameData + 0xa8c`. It sets
a 3-frame countdown and moves to phase 3.

**Phase 3** closes out:

- `Gm_SetCinematicFreezeStage(2)` unfreezes and restores the HUD, and
  `Ptcl_DestroyCinematicRenderPass` (0x80233b30) drops the particle pass.
- `FGM_ResumeAll` (0x80061b08) and `BGM_ResumeAll` (0x8005e728) bring the audio back, and
  `BGM_PlayLegendaryTheme` (0x8006215c) starts `BGM_LEGENDARYAIRRIDEMACHINE` with a fade.
- `Sky_SetDragoonPreset` (0x800d5490, preset 13) or `Sky_SetHydraPreset` (0x800d54d8, preset 14)
  swaps the sky, and that swap stays after the cinematic.
- `Ply_OnLegendaryPieceCollect(ply, 4)` (0x8027a4e8) plays the completion fanfare; count 4 is the
  fanfare rung, not a fourth piece.
- The controller destroys itself, and `LegendaryMachine_FreeAssemblyArchive` (0x80283e88) frees the
  archive with `Archive_Free`. The preload table still hands that freed block back, so a second run
  of the same archive in one scene loads its joints out of freed memory. Vanilla never runs one
  legendary's cinematic twice in a round.

## The archive

`VsDragoon.dat` and `VsHydra.dat` each export one public, `vsDataDragoon` / `vsDataHydra`, of three
pointers:

```
vsData[0] -> { JOBJDesc*, FigaTree*, MatAnimJoint* }   glow model
vsData[1] -> { JOBJDesc*, FigaTree*, MatAnimJoint* }   parts model
vsData[2] -> one word holding the camera-animation descriptor pointer
```

The glow model is the light show: ribbon streaks, each a two-bone envelope mesh, plus billboarded
flash sprites, all drawn translucent with no Z update. The parts model is the machine broken into
pieces, each parented to its own pivot joint so the FigaTree can fly it in from its rest
translation to zero. It is dedicated cinematic geometry, not the `VcStar*.dat` model, but it uses
the same units. It is `vsData[0]`, the glow model, that anchors the camera and supplies the
150-frame length. Both FigaTrees run 150 frames, using rotate, translate and scale tracks, `PATH`,
`BRANCH` (12) for subtree visibility and `PTCL` (40).

`LegendaryMachine_PreloadAssemblyArchives` (0x80283d98), called from `Preload_AllCityFiles`
(0x80262ba4), warms both files when City Trial loads. It queues each with
`Preload_CreateEntry(5, name, 6, 6, 0, 1, 5, 0x20, 0)`; the ninth argument goes on the stack at
`8(r1)`. The preload is why the synchronous load at cinematic start does not hitch.

## The camera

`PlyCam_CreateAnimCamera` (0x800b9c74) is the shot, and nothing else in the game uses it. The
cinematic calls it as `(ply, root, desc, 16, 16)`, where `root` is the glow model's root joint:

- It claims the first free camera slot from 5 to 31; the per-player cameras own 0-4.
  `PlyCam_FindFreeSlot` (0x800b9370) is the same scan on its own.
- It builds a camera GObj with its own proc, set to camera kind 4, the scripted path follower.
- The caller only keeps the slot and hands it to `PlyCam_DestroySlot` at the end.
- The two 16s are frame counts. The view opens from a 1x1 viewport to full screen over the first,
  and returns to the previous camera's rects over the second. That open and close is the whole
  framing device; there is no fade.

The descriptor is `{HSD_CObjDesc*, HSD_CameraAnim**}`, the anim array NULL-terminated. An
`HSD_CameraAnim` is three pointers: an `HSD_AObjDesc`, then the eye's and the interest's WObj
animations. The eye is a `PATH` track over a spline and the interest three translate tracks, both
150 frames long. The
interest barely moves, so the shot is the eye's path. The `HSD_CObjDesc` carries fov 55, near 0.0175, far 5734.4.

The eye path is a spiral orbit, the same in both archives. Its `HSD_AObjDesc` points at a `JOBJDesc`
that carries the spline in place of a DObj (flag 0x4000, `JOBJ_SPLINE`, spline at `+0x10`). The
descriptor's rotation (`+0x14`), scale (`+0x20`) and translation (`+0x2c`) place the path. The spline lies flat in its own XY plane, so the joint's
`scale.x` and `scale.y` are the two horizontal axes of the orbit and `scale.z` does nothing.
Retargeting the shot to a machine of a different footprint is those two floats. The spline is a
type-2 (uniform cubic B-spline) `HSD_Spline`, evaluated by `splGetSplinePoint` (0x80414fc0). Its
arc-length tables, used by `splArcLengthGetParameter` (0x80415758), are all derivable from the
control points.

The descriptor references no joint. The only coupling to the model is the root passed in, and only
its world matrix (`JOBJ + 0x44`), sampled once at creation. Every eye and interest sample goes
through that matrix, so the same descriptor gives the same shot around any model placed at the
machine's basis. The whole descriptor block is self-contained in the archive, with no pointer
leaving it, and only `vsData[2]` points in, so it can be carried into another archive intact.

## The rider and the mount

The mount is data-driven. The cinematic only sets it up and cleans up after it.

`Ply_EnterLegendaryAssembly` clears that machine's three collected-piece bits in
`PlayerData + 0x908`. It hands the rider to `RiderGObj_EnterLegendaryAssembly` (0x8019248c), which is
Kirby-only. That runs the ability teardown and `RiderState_LegendaryAssemblyEnter` (0x801bda34),
which:

- drops the part-transform overrides and the material-color animations, so an invincibility flash
  or ability tint does not survive into the pose;
- saves the rider's p_link and gx_link neighbours (`RiderData + 0x93c` / `+0x940`) and relinks it
  from `GAMEPLINK_RIDER` (10) to p_link 32, and from gx_link 6 to 26;
- enters state 0x82 with substate `0x220 + machine_index`;
- stages the machine to mount: `respawn_is_bike` (`+0x944`) = 0 and `respawn_class_slot` (`+0x948`)
  = 8 for Dragoon or 4 for Hydra.

p_link 32 is what makes the pose work. `PAUSEKIND_EXPLODE`'s blacklist entry (0x80494f88,
`0x0743fffe`) freezes p_links 1-17, 22 and 24-26: the machine at 9, the rider's usual 10 and items
at 13. p_link 32 is outside it, so the rider keeps animating while everything else stands still.
gx_link 26 is in `World_CObj`'s (0x800b04a8) main pass, so it draws over the world; the cinematic's
models and particle pass share it. The player's machine is never touched. It stays frozen and
visible until the mount destroys it.

State 0x82 is inert except for its anim think, `RiderState_LegendaryAssemblyAnimThink`
(0x801bdb2c). When `RiderData + 0x808` is nonzero it clears it and calls
`Rider_RespawnFullRecreate(rd, respawn_is_bike, respawn_class_slot, 0, 0, 1, 0, 0)` (0x80193900).
`+0x808` is a motion-script variable written by `RiderScript_SetVariable` (0x8019b98c), so the
substate's own motion script picks the frame the machine swaps. Nothing in the mount names Dragoon or
Hydra beyond the two staged values.

`Ply_ExitLegendaryAssembly` -> `RiderGObj_ExitLegendaryAssembly` (0x801924f8) ->
`RiderState_LegendaryAssemblyExit` (0x801bdbac) restores both links from the saved neighbours. It
then settles the rider into a riding state with `Rider_ResolveQueuedAbility` (0x801a8454). The rider
GObj survives the recreate, which is why the saved neighbours are still valid.

## Reuse seams

Three `bl` sites carry every machine-specific decision:

| Site | Call | Replacing it buys |
|---|---|---|
| 0x80283914 | `LegendaryMachine_LoadAssemblyArchive` | A different `vsData`, of the same shape |
| 0x80283c98 | `LegendaryMachine_FreeAssemblyArchive` | Freeing it |
| 0x80283b70 | `Ply_EnterLegendaryAssembly` | Forward to vanilla, then overwrite `respawn_is_bike` / `respawn_class_slot` with any machine |

A fourth, the `bl LegendaryMachine_PreloadAssemblyArchives` at 0x80262be8, can warm a replacement
archive alongside the vanilla two. The two fanfare calls (0x80283b8c, 0x80283ba4) and the phase-3
sky preset are optional.

Run under a vanilla machine index, everything else plays untouched: the freeze, the HUD hide, the
rider pose, the camera, the timing, the audio bracket and the legendary theme. Index 1 gives
Hydra's pose, fanfare and sky, and the motion script that fires the swap. Nothing downstream reads
the index again, so the machine that arrives is entirely the staged pair.

A replacement archive must be on disc and preloaded before a run. The load goes through
`lbLoadArchive` (0x80059a20), which panics on a missing public. Because the run frees the archive
while the preload table keeps it, each archive can run once per scene; a mod that starts the
cinematic itself has to refuse a second run of the same archive until the next scene load.
