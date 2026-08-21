# Legendary Assembly Cinematic

The short cutscene that plays when a player collects the third Dragoon or Hydra piece:
the world freezes, the HUD drops, a scripted camera swings in, the machine's parts fly
together on trails of light, and the player comes out riding the assembled machine. The
whole thing is roughly 1 KB of code around `0x80283800`-`0x80284300` plus two data
archives, and every machine-specific decision in it is reachable at a `bl`.

It runs for 181 frames: a 28-frame lead-in, a 150-frame animation, and a 3-frame tail.

## Entry

`LegendaryMachine_StartAssembly` (`0x80283cf0`) takes a `LegendaryAssemblyParams`:
machine index (0 = Dragoon, 1 = Hydra), player index, and the machine's world position,
forward and up, copied straight off `MachineData + 0x3e8` / `+ 0x418` / `+ 0x424`. Its one
caller, the pickup arm of `Machine_OnTouchItem`, drops the machine's collision animations
with `Machine_ResetColAnims` (`0x801d633c`) first so none survive into the freeze. It cancels any running piece-pickup hitstop, then tail-calls
`LegendaryMachine_CreateAssembly` (`0x802838a0`), which does the real setup:

- `GObj_Create(0x21, 0x1f, 0)` for the controller, with state allocated from the
  `0x8055f760` pool and freed by `LegendaryMachine_FreeAssemblyState` (`0x80283874`);
- `LegendaryMachine_LoadAssemblyArchive` (`0x80283e18`) loads `VsDragoon.dat` or
  `VsHydra.dat` and returns the archive's `vsData` public;
- the params are copied into the state, timer set to 28 and phase to 1;
- `Gm_SetCinematicFreezeStage(0)` sets `PAUSEKIND_EXPLODE`, freezing the world;
- `BGM_PauseAll` (`0x8005e6c8`) pauses both music slots;
- `LegendaryMachine_AssemblyThink` (`0x802839b8`) is installed as the proc;
- `Gm_SetLegendaryAssemblyGObj` (`0x8000c964`) publishes the GObj at `GameData+0xa8c`.

`Gm_IsLegendaryAssembling` (`0x8000c934`) reads that pointer back. Its only consumers in
the whole game are two sites in `Machine_OnTouchItem` (`0x801db650`, `0x801db738`), which
suppress piece pickups while a cinematic is up. Nothing else in the engine branches on it,
so a second concurrent cinematic is not blocked by the engine - it would tear down the
running one's model GObjs and leave a dangling joint.

`GameData+0xa90` is a **different** object and easy to confuse with it: the dramatic pause
on a piece pickup, built by `LegendaryPiece_CreateHitstop` (`0x8028406c`), ticked by
`LegendaryPiece_HitstopThink` (`0x80284190`) and torn down by
`LegendaryPiece_DestroyHitstop` (`0x80284140`). It freezes with the same `PAUSEKIND_EXPLODE`
for a countdown, then runs the world at a scaled engine speed for a second interval.
`LegendaryMachine_CancelPieceHitstop` (`0x80283d70`) kills it before the cinematic starts,
because the cinematic needs the freeze flag and the engine speed to itself. Its accessors
are `Gm_IsLegendaryHitstopActive` (`0x8000c994`), `Gm_GetLegendaryHitstopGObj`
(`0x8000c9f4`) and `Gm_SetLegendaryHitstopGObj` (`0x8000c9c4`).

## The Four Phases

`LegendaryMachine_AssemblyThink` is a phase field plus a countdown. Its 0x54-byte state
block holds the two model GObjs, the camera slot, the `vsData` pointer, the animation
cursor (current frame from -1.0, step 1.0, end frame), the machine's `(pos, up, forward)`
and index, and the countdown/phase pair at `+0x4c`/`+0x50`.

**Phase 1**, after 28 frames, builds everything. Each model gets a
`GObj_Create(0x21, 0x20, 0)` with `GObj_AddGXLink(gobj, LegendaryMachine_RenderAssemblyModel,
0x1a, 1)`; the render callback (`0x80283ed8`) draws through `GObj_RenderJObj` for every pass
but 3. The joint tree comes from `HSD_JObjLoadJoint`, and
`LegendaryMachine_BindAssemblyAnims` (`0x80283f74`) binds the animations: a DObj relink pass
(`0x8005523c`), then `JObj_AddFigaTreeAnim` (`0x8006e2c0`) for the joint animation and
`HSD_JObjAddAnimAll(jobj, NULL, matanim, NULL)` for the material animation, then
`HSD_JObjReqAnimAllByFlags(jobj, 0.0f, 21)` and `JObj_SetAllAOBJRateByFlags(1.0f, jobj, 0xffff)`.

Both models are then placed with `gmLanMenu_Scale3DObject(1.0f, jobj, forward, up, pos)`
(`0x80054414`), which stamps an orientation matrix built from the machine's own basis: its
columns are `cross(forward, up)`, `forward`, `up`, `pos`, so the argument named `forward`
becomes the model's +Y and the one named `up` becomes its +Z. The cinematic
therefore plays **in world space at the pickup site**, in machine-model units, not in any
camera-local rig.

The rest of phase 1: `LegendaryMachine_GetAssemblyEndFrame` (`0x80283f00`) reads 150.0 out
of the glow model's FigaTree, `Gm_SetCinematicFreezeStage(1)` hides the HUD,
`PlyCam_CreateAnimCamera` opens the shot, `Ptcl_CreateCinematicRenderPass` (`0x80233b04`)
stands up the particle render pass for gx_link 26, `Ply_EnterLegendaryAssembly`
(`0x8022d6b4`) poses the rider, and `SFX_PlayFullVolume` plays `0x130008` for Dragoon or
`0x130007` for Hydra.

**Phase 2** calls `LegendaryMachine_AdvanceAssemblyAnims` (`0x80283f24`) once per frame -
`HSD_JObjAnimAll` on both models plus `frame += step` - until the frame counter reaches the
end frame. Then it destroys the camera with `PlyCam_DestroySlot`, calls
`Ply_ExitLegendaryAssembly` (`0x8022d71c`), destroys both model GObjs, clears
`GameData+0xa8c`, sets the countdown to 3 and moves to phase 3.

**Phase 3**, three frames later, closes out: `Gm_SetCinematicFreezeStage(2)` unfreezes and
restores the HUD, `Ptcl_DestroyCinematicRenderPass` (`0x80233b30`) drops the render pass,
`FGM_ResumeAllKinds` (`0x80061b08`) and `BGM_ResumeAll` (`0x8005e728`) bring audio back, and
`BGM_PlayLegendaryTheme` (`0x8006215c`) ends both BGM slots and starts
`BGM_LEGENDARYAIRRIDEMACHINE` with a fade. `Sky_SetDragoonPreset` (`0x800d5490`, preset 13) or
`Sky_SetHydraPreset` (`0x800d54d8`, preset 14) swaps the sky, and that swap **stays up after
the cinematic ends**. `Ply_OnLegendaryPieceCollect(ply, 4)` plays the completion sound pair -
the count-4 rung of a three-piece ladder is the fanfare, not a fourth piece. Finally the
controller GObj destroys itself and `LegendaryMachine_FreeAssemblyArchive` (`0x80283e88`)
frees the archive.

Because the archive is freed here, a second cinematic in the same scene reloads it. Code that
runs two assemblies back to back must let the first finish, or `HSD_JObjLoadJoint` walks a
dangling joint.

## The Archive

`VsDragoon.dat` (179 KB) and `VsHydra.dat` (116 KB) each hold exactly one public,
`vsDataDragoon` / `vsDataHydra`, which is three pointers:

```
vsData[0] -> { JOBJDesc*, FigaTree*, MatAnimJoint* }   glow model
vsData[1] -> { JOBJDesc*, FigaTree*, MatAnimJoint* }   parts model
vsData[2] -> one word holding a camera-animation descriptor pointer
```

The two models are built in the order `vsData[1]` then `vsData[0]`, and it is
**`vsData[0]`** that both anchors the camera and supplies the 150-frame length.

**The glow model** is 23 KB and structurally identical between the two archives (107 nodes
each; only joint transforms and one vertex blob differ). It is the light show: three ribbon
streaks, each a two-bone envelope mesh on a 32x128 `IA8` texture, plus billboarded flash
sprites on 64x64 `IA8` and one untextured vertex-colored aura mesh. Everything in it renders
`XLU` with `NO_ZUPDATE`. Hydra's version is the regular one - three identical four-node
groups (`BRANCH` to reveal the group, `ROTX/Y/Z` to aim it, `TRAX/Y/Z` plus `SCAX` to stretch
the ribbon) followed by five shared flash nodes. Dragoon's carries the same node count in a
less uniform arrangement.

**The parts model** is the machine broken into its pieces, each piece parented to its own
pivot joint so the FigaTree can fly it in. Hydra's three pieces rest at roughly
`(-2.7, 15.3, 23.8)`, `(-19.4, 6.4, -16.6)` and `(19.2, 6.4, -10.8)` - those descriptor
translations are the fly-in start positions, and the animation drives them to zero. It is
dedicated cinematic geometry, not the `VcStar*.dat` model, but it is authored in the same
units: Hydra's assembled parts measure about the same radius as `VcStarHydra`'s main model
at 7.5.

Both FigaTrees run 150 frames. `JObj_AddFigaTreeAnim` (`0x8006e2c0`) walks the joint tree in
preorder against the tree's per-node track-count table and builds one `AObj` per node and one
`FObj` per track. Track types in use are `ROTX/Y/Z` (1-3), `TRAX/Y/Z` (5-7), `SCAX/Y/Z`
(8-10), `PATH` (4), `BRANCH` (12) for subtree visibility, and `PTCL` (40).

## The Camera

`PlyCam_CreateAnimCamera` (`0x800b9c74`) is the shot, and it is used by nothing else in the
game. The cinematic calls it as `(ply, glow_model_jobj, desc, 16, 16)`. It claims the first free slot in 5-31 out of `cam_gobjs` (the per-player cameras own 0-4),
builds a camera GObj that registers its own proc, and switches it to **camera kind 4**, the
scripted path-follower whose entry sits in the kind table at `0x804a1f0c`. No per-frame call
from the caller is needed; the only obligations are to keep the returned slot index and hand
it to `PlyCam_DestroySlot` (`0x800b94c8`) at the end. `PlyCam_FindFreeSlot` (`0x800b9370`) is
the same scan standalone.

The two `16`s are both frame counts and mean different things: the new view opens from a
degenerate 1x1 viewport and scissor to fullscreen over the first, and lerps back to the
previous camera's rects over the second when the animation ends. That open and close is the
cinematic's whole framing device - there is no fade and no backdrop geometry.

The descriptor is 8 bytes, `{HSD_CObjDesc*, HSD_CameraAnim**}`, with the anim array
`NULL`-terminated. An `HSD_CameraAnim` is `{AObjDesc*, WObjAnim *eye, WObjAnim *interest}`:
the eye is a `PATH` track over a nine-point `HSD_Spline` and the interest is three translate
tracks, both over 150 frames. The interest barely moves - `(0, 1.05, 0)` to `(0, 1.31, 0)`,
the machine's own center - so the whole shot is the eye's path. The `HSD_CObjDesc` carries
`fov 55.0`, `near 0.0175`, `far 5734.4` and `aspect 1.17999`; its viewport and scissor are
overwritten by the open transition and never used.

**The eye path is a spiral orbit, and both archives hold the same one.** The eye's
`AObjDesc` points its fourth slot at a JOBJ carrying the spline in the `SPLINE`-flag arm of
its `+0x10` union, so the joint's own rotation, scale and translation place the path:

```
+0x04 flags 0x4000 (SPLINE)     +0x20 scale
+0x10 HSD_Spline*               +0x2c translation
+0x14 rotation
```

Hydra's spline is a flat spiral in its own XY plane, swung horizontal by the joint's
`(pi/2, pi/2, 0)` rotation, scaled `(1.41, 1.57, 1.41)` and lifted to `y 6.37`; Dragoon's is
the same path baked into world coordinates and shrunk by a uniform `0.948`. Nine knots carry
the eye a full turn around the machine while the radius falls from 14.1 to 8.4 at Hydra's
size. Because the spline's own Z is zero, `scale.x` and `scale.y` are the two horizontal axes
of the orbit and `scale.z` has nothing to act on - retargeting the shot to a machine of a
different footprint is those two floats and nothing else.

An `HSD_Spline` is `{u8 type, u8 pad, u16 nb_points, f32 tension, Vec3 *values,
f32 total_length, f32 *knots, f32 (*speed)[5]}`, with `tension` read only by type 3. Type 2,
the one both cameras use, is a uniform cubic B-spline: `values` holds `nb_points + 2` control
points, one phantom at each end, and `splGetSplinePoint` (`0x80414fc0`) reads only those.
The other three fields are the arc-length reparametrization
`splArcLengthGetParameter` (`0x80415758`) bisects with:
`total_length` is the true arc length, `knots[i]` is the cumulative arc-length fraction at
knot `i`, and each `speed[i]` is the five coefficients of `|P'(t)|^2` over segment `i`, in
descending order. All three are derivable from `values`, so an authored spline computes
them rather than storing anything the evaluator cannot rebuild.

**The descriptor holds no reference to any joint.** The only coupling to the model is the
`JOBJ*` passed as argument 2, and only its world matrix at `JOBJ+0x44`, sampled once at
creation and baked in. Every eye and interest sample is transformed through that matrix, so
the path is authored in the anchor's local space. Since the anchor is placed at the machine's
`(pos, forward, up)` at scale 1.0, the same descriptor gives the same shot around any model
placed the same way, whatever its joint count.

The whole descriptor block is self-contained inside the archive - `0xa8e4..0xabbc` in
`VsDragoon.dat`, `0xa938..0xac08` in `VsHydra.dat`, both with `nb_extern = 0` and no pointer
leaving the range. Only `vsData[2]` points into it from outside.

`LegendaryMachine_PreloadAssemblyArchives` (`0x80283d98`), called from
`Preload_AllCityFiles` (`0x80262be8`), warms both files through
`Preload_CreateEntry(5, name, 6, 6, 0, 1, 5, 0x20, 0)` when scene 9 (City Trial) loads,
which is why the synchronous load at cinematic start does not hitch. That call takes nine
arguments, the ninth on the stack at `8(r1)`; it is forwarded to
`Preload_CreateEntryByEntrynum` along with the other eight.

## The Rider and the Mount

The mount is not a call the cinematic makes. It is data-driven, and the cinematic only sets
it up and cleans up after it.

`Ply_EnterLegendaryAssembly` (`0x8022d6b4`) clears that machine's three collected-piece bits
in `PlayerData+0x908` and hands the rider GObj to `Rider_EnterLegendaryAssembly`
(`0x8019248c`), which is Kirby-only. That fires the ability-teardown callbacks and calls
`RiderState_LegendaryAssemblyEnter` (`0x801bda34`), which:

- zeroes the per-part transform override block and drops all material-color animations, so
  an invincibility flash or ability tint does not survive into the pose;
- saves the rider GObj's p_link neighbour in `RiderData+0x93c` and relinks it from
  `GAMEPLINK_RIDER` (10) to **p_link 32**;
- saves the gx_link neighbour in `RiderData+0x940` and relinks from gx_link 6 to **26**;
- calls `RiderStateChange(rd, 0x82, 0x220 + machine_index, 0)`;
- stages `RiderData+0x944 = 0` (is_bike) and `RiderData+0x948 = 8` for Dragoon or `4` for
  Hydra (the class slot, which for a star equals its `MachineKind`);
- clears `RiderData+0x808` and re-sets two presentation bits that `RiderStateChange` wipes.

**p_link 32 is what makes the pose work.** The per-`PauseKind` p_link freeze mask table at
`0x80494f68` has `PAUSEKIND_EXPLODE`'s entry at `0x0000000007 43fffe`, which freezes p_links
1-17, 22 and 24-26 - the machine at 9, the rider's usual 10, items at 13. p_link 32 is
outside it, so the rider keeps ticking while everything else is stopped. gx_link 26 hides
nothing: `World_CObj` (`0x800b04a8`) includes bit 26 in its main-pass `cobj_links` mask, so
the bucket simply draws after links 0-15 and overlays the world. The cinematic's two models
and the particle pass share that bucket. The player's machine GObj is never touched - it
stays frozen and visible until `Rider_RespawnFullRecreate` destroys it.

State `0x82` is inert. Its Kirby table entry at `0x804af0c8` has bare `blr`s for iasa,
physics and collision; the only live slot is the anim-think,
`RiderState_LegendaryAssemblyAnimThink` (`0x801bdb2c`), whose whole body is: when
`RiderData+0x808` is non-zero, clear it and call
`Rider_RespawnFullRecreate(rd, rd->x944, (u8)rd->x948, 0, 0, 1, 0, 0)`.

`RiderData+0x808` is one of four motion-script variables written by the script opcode handler
`RiderScript_SetVariable` (`0x8019b98c`). So the substate's own motion script decides the
frame at which the machine swaps, and the swap itself goes through
`Rider_RespawnFullRecreate` (`0x80193900`) on the `(is_bike, class slot)` pair Enter staged.
There is no Dragoon/Hydra token anywhere in the mount - the two literals are the only
machine-specific thing about it, which is what makes the path reusable for any machine.

`Ply_ExitLegendaryAssembly` (`0x8022d71c`) and `Rider_ExitLegendaryAssembly` (`0x801924f8`)
call `RiderState_LegendaryAssemblyExit` (`0x801bdbac`), which restores both links from the
saved neighbours and calls `Rider_ResolveQueuedAbility` (`0x801a8454`) to settle the rider
into a normal riding state. The rider GObj survives the recreate, which is why the saved
neighbours are still valid at that point.

## Reuse Seams

Three `bl` sites carry every machine-specific decision, which is enough to drive the whole
cinematic with a different archive and a different destination machine:

| Address | Call | What replacing it buys |
|---|---|---|
| `0x80283914` | `LegendaryMachine_LoadAssemblyArchive` | supply a different `vsData` |
| `0x80283c98` | `LegendaryMachine_FreeAssemblyArchive` | free it |
| `0x80283b70` | `Ply_EnterLegendaryAssembly` | forward to vanilla, then overwrite `RiderData+0x944` / `+0x948` with any `(is_bike, class slot)` |

A fourth, `0x80262be8` in `Preload_AllCityFiles`, warms a replacement archive alongside the
two vanilla ones. Two more are optional: the SFX calls at `0x80283b8c` and `0x80283ba4`, and
the sky preset picked in phase 3.

`mods/custom_machines/src/machine_cinematic.c` takes the first three with
`CODEPATCH_REPLACECALL` and dispatches on a latch naming whichever registered machine started
the run; with the latch clear the vanilla pair plays untouched. `machine_preload.c` owns the
fourth.

Called with `machine_index = 1` behind a caller-side flag, everything else runs untouched -
the freeze, the HUD hide, the rider pose, the camera, the 150-frame timing, the audio
bracket and the legendary theme. The substate is then Hydra's `0x221`, which supplies Kirby's
Hydra riding pose and the script that fires the swap; nothing downstream reads the substate
again, so the machine that arrives is entirely the `+0x944` / `+0x948` pair.
