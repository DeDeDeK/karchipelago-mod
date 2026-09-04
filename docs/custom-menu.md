# Custom Main Menu

The archipelago mod's `main_menu` subsystem (`mods/archipelago/src/main_menu.c`, booted from
`main.c`'s `OnBoot` via `MainMenu_OnBoot`) turns the vanilla "KIRBY AIR RIDE" title logo into
the KARchipelago logo. It keeps the vanilla "KIRBY" text and blue swoosh and replaces only the
"AIR RIDE" subtitle with two pieces loaded from a mod asset - the "AIRRIDE / ARCHIPELAGO"
subtitle and a six-Kirby cluster - drawn in the title foreground scene. The rest of the title
screen (background, Kirby, menu options) is untouched.

## Demo Machine

`MainMenu_SelectDemoMachine` rewrites the three `li r4` operands of the idle demo-player setup
inside `SceneLoad_TitleScreen` - RiderKind at `0x8000d340`, IsBike at `0x8000d34c`, class slot at
`0x8000d358`. It asks `GateApStar_MachineKind()` for the Archipelago Star; if that resolves to a
star-class kind the demo becomes Kirby riding it, otherwise it falls back to Dedede on the Wagon
Star. Showing the Archipelago Star on the title screen is the point: it is the machine the goal
awards, on display before it is earned.

The demo ride must stay star-class (`is_bike = 0`) - the demo init uses hardcoded star-only state
ids and a wheel-class machine crashes there. The selection is re-run from the title load hook on
every title entry, not just at boot, because the machine registry only resolves the AP Star kind
after every mod has booted.

## Hooks

Two vanilla code sites are hooked (the title minor's `cb_Exit` and `cb_ThinkPreGObjProc` are also
wrapped through the scene descriptor, covered under Demo Machine Audio):

- **Title file load (`0x8000d2b4`)** - `MainMenu_OnTitleLoad` re-runs the demo-machine selection,
  then calls `Gm_LoadGameFile(&menu_archive, "MnTitleKarchi")`, pulling `MnTitleKarchi.dat` from
  the disc overlay into the title-screen heap. `Gm_LoadGameFile` appends the `.dat` extension.
- **Title scene create (`0x8017b5d8`)** - `MainMenu_OnTitleCreate`:
  1. Hides the vanilla "AIR RIDE" subtitle (its text and its blue background box), which is
     depth-first joint index `14` of the title foreground scene
     (`Gm_GetMenuData()->ScMenTitleFg_gobj`), via
     `JObj_SetFlagsAll(GObj_GetJObjIndex(fg, 14), JOBJ_HIDDEN)` (recursive, so it covers the
     whole subtree). Everything else stays: the "KIRBY" text, the blue swoosh, the letterbox
     bars, the copyright line, and PRESS START. This index is the runtime `GObj_GetJObjIndex`
     order, not the static desc-tree order.
  2. Fetches the custom model's public with
     `Archive_GetPublicAddress(menu_archive, "karchiTitleFg_scene_models")`, instantiates it
     with `MenuElement_Create(set[0]->jobj)`, then calls `MenuElement_AddData(element, 99)`.

`MenuElement_Create` (`0x801388a8`) allocates a menu gobj, instantiates the JOBJDesc, and
registers its own GX render link. That render callback, `MainMenu_ElementGX` (`0x80138ae0`),
dereferences the element's `userdata` (GOBJ+0x2c) and only draws when its `is_visible` flag
(userdata+0x8, bit 0x80) is set - both of which `MenuElement_AddData` (`0x80138a00`) provides. So
the static pieces need no per-frame proc, but `AddData` must be called or the render callback
faults on a null userdata. The public is a `JOBJSet` array (`set[0]->jobj` is the root JOBJDesc),
matching the vanilla `Sc*_scene_models` layout.

## Demo Machine Audio

Every star machine requests the same two looping SFX at spawn - a constant engine loop and a
surface loop - so no demo machine is doing anything the Warp Star does not. What differs is the
idle volume floor in the per-kind audio record, and that is the whole reason the swap needs any
audio handling at all.

Those records are `MachineAudioParams` (`externals/hoshi/include/machine.h`), authored in
`iso/files/VcCommon.dat`. `vcLoadCommon` (`0x801c6d0c`) caches the two-entry lookup into
`r13+0x764` (`stc_machineAudioParams`), indexed first by `MachineData.is_bike` and then by the
class-relative machine kind.

`Machine_UpdateEngineLoop` (`0x801dcb18`) computes the engine loop's volume as
`md->x870 * engine_volume_coef + engine_idle_floor`, clamped to `1.0`, and `Machine_PlaySFX`
(`0x801dd17c`) scales that by 255. A volume near zero never becomes a voice at all, which is why
a vanilla title screen shows every `sounds[].sfx_id` at `-1` and zero live FGM instances despite
the Warp Star requesting both loops: the Warp Star's floor is `0.0`. Only five of the nineteen
star kinds have a nonzero floor - Bulk (40), Wagon (20), Turbo (18), Jet (12), Formula (10) - and
those clamp straight to full volume while parked. The surface loop's floor is 0.03 for every star
kind, below the audible threshold, so only the engine loop is ever a problem at idle.

Nothing in the title scene stops a loop once it is playing. `SceneExit_TitleScreen`
(`0x8000d5c8`) destroys only the movie GObjs, and the heap reset reclaims `MachineData` without
running `Machine_Destroy` (`0x801c6b44`), so an audible loop would carry on under the main menu
and gain another copy on each title visit.

Two wrappers on the title minor's descriptor handle this:

- `cb_ThinkPreGObjProc` -> `MainMenu_TitleThink` zeroes `engine_idle_floor` on the demo kind's
  record, saving the vanilla value once. With the floor at 0 the volume arithmetic is identical
  to the Warp Star's and the loop stays inaudible for the same reason vanilla's does. Nothing
  about the demo machine is special-cased; it simply stops being one of the loud kinds, and a
  kind whose floor is already 0.0 passes through unchanged.
- `cb_Exit` -> `MainMenu_TitleExit` restores the floor, then walks the machine GObj list stopping
  the surface and engine loops with `FGM_Stop` (`0x8005e7d8`) and calling
  `Machine_FreeAudioEmitter` (`0x801dc618`). The loops still hold FGM instances even at volume 0,
  and the five audio tracks and the `AudioEmitter` live in static `Audio3D` slots the heap reset
  does not reclaim. Vanilla leaks all of these on every title visit; this is the last point at
  which the machine is alive enough to return them. Stopping the loops first also means the
  emitter holds no voice, so `AudioEmitter_Free` (`0x8005e08c`) completes instead of deferring to
  state 1.

The patch is applied from the think rather than the load hook because `vcLoadCommon` runs at
`0x8000d304`, partway through the title `cb_Load` and after the `0x8000d2b4` hook site - the
record is not resident yet at load time, whereas a machine existing at all proves it is. Applying
it a frame late is harmless: `Machine_UpdateEngineLoop` re-reads the record every frame, and the
engine loop is only ever created at volume 0.0 (by that function, not by
`Machine_PlaySpawnSound` (`0x801dccec`), which starts only the surface loop) and slew-ramped up
from there, so no audible frame can slip through. Restoring on exit matters because the record is
shared game data - the same kind in City Trial reads the same fields.

`MachineData.xc39` bit 0 (`MACHINE_HITREACT_HOLD`) also silences both loops and looks like a
tidier lever, but it is a presentation-wide latch rather than an audio mute: it additionally
freezes the model animation, hides a `DObj` subgroup in `Machine_GX`, and suppresses the
machine's persistent and periodic effects, which costs the demo machine its exhaust particles.

`MainMenu_TitleExit` reaches the machines through `(*stc_gobj_lookup)[GAMEPLINK_MACHINE]` followed
via `GOBJ.next`. `PlayerData` is not a usable route: the title demo machine is never registered in
it, so `Ply_GetMachineGObj(0)` returns null for the whole title scene even though the machine
exists and holds audio tracks.

The wraps are installed through `Hoshi_GetMinorScenes()[MNRKIND_TITLESCREEN]` at boot, saving each
vanilla pointer and calling it from the wrapper. Both slots are populated in vanilla
(`cb_Exit` = `SceneExit_TitleScreen`, `cb_ThinkPreGObjProc` = `TitleScreen_MinorThink`
`0x8000d698`), so the calls are unconditional.
This is deliberately **not** a `CODEPATCH_HOOKCREATE` on `SceneExit_TitleScreen`: that macro calls
out with `bl`, which clobbers LR, and `0x8000d5c8` is the function's first instruction - the
`mflr r0` two instructions later would save the trampoline address as the return address and `blr`
into it, hard-freezing the console. Patching the descriptor is a plain C call with no register
hazard, and hoshi relocates the minor table, so the entry is live.

## Asset Pipeline

`scripts/authoring/make_menu_logo.py` authors `mods/archipelago/assets/MnTitleKarchi.dat` from two
piece PNGs in `art/` (`ap-banner.png`, `ap-kirbs.png`). Source art lives there rather than
beside the archive because `assets/` is the disc staging folder - everything in it ships:

    uv run --with pillow python scripts/authoring/make_menu_logo.py

Each piece is cropped to its opaque content box and encoded as an RGBA8 texture (full 8-bit
color - the logo's smooth gradients band badly under RGB5A3). The pieces are wrapped in one
renderable model: a root JOBJDesc with one child quad per piece.

    karchiTitleFg_scene_models (NULL-terminated JOBJSet array)
      JOBJSet -> root JOBJDesc (ROOT_XLU)
        child JOBJDesc (XLU)   [one per piece, chained via next]
          DObjDesc
            MObjDesc  render 0x60002011 (unlit CONSTANT, TEX0, XLU, alpha from
                      material * texture)
              MaterialDesc  white, alpha 1.0 (texture renders untinted; its own
                            alpha cuts the art out of the transparent background)
              TObjDesc  TEXMAP0, COORD_UV, CM_MODULATE / AM_MODULATE -> ImageDesc
            POBJDesc  CULLFRONT, VtxDescList (POS index8 f32, TEX0 index8 f32),
                      4-vertex TRIANGLESTRIP over private vertex arrays

Keeping the vanilla "KIRBY" + swoosh (already resident) and shipping only the two small
subtitle/Kirby textures keeps the title-heap footprint low (~220 KB total).

Material alpha must stay 1.0 and the TObj must keep `AM_MODULATE`, or the quads render as
opaque rectangles instead of cut-outs; if a quad renders backfacing (invisible), flip
`POBJ_FLAGS_CULLFRONT` to cull back instead.

## Placement

The pieces' positions come straight from `art/karchipelago-logo.png` (the full combined logo):
each piece is composited into that image at native size, and edge template-matching finds its
exact pixel box within it. Those pixel boxes are baked into `PIECES` in `make_menu_logo.py`. One
transform maps the combined logo into the title foreground scene (an XY plane at Z=0, +Y up,
camera looking down -Z):

- The combined logo's opaque content box (`LOGO_CONTENT_BOX`) maps to a world rect centered at
  `WORLD_CENTER` with width `WORLD_CONTENT_W`.
- Each piece's world center and width follow from its pixel box, so the relative layout
  (subtitle to Kirbys) stays locked to the png.

`WORLD_CENTER` and `WORLD_CONTENT_W` are the two global tuning knobs - overall position and
scale - and the only things to adjust to reposition or resize the whole assembly.
`WORLD_CONTENT_W` is sized so the logo matches the vanilla Kirby's on-screen scale;
`WORLD_CENTER` is offset down-left of the vanilla logo center so the subtitle tucks under the
Kirby text and the Kirbys sit lower-left. Per-texture resolution is set by each piece's `tex_w`
(height derives from the cropped aspect; both rounded to a multiple of 4, a GX requirement).

## Animation

The pieces are static. Adding motion takes one of two forms. A rigged animation authored in
HSDRaw/Blender needs the C side to add proc `TitleScreenForeground_Proc` (`0x8017b424`) and a
`JObj_AddSetAnim` call after `MenuElement_Create`. Simple effects - cycling the Archipelago
icon through the logo colors, say - are a per-frame material-color or texture swap driven from a
think proc, the same shape as the moon/stars weather effects.
