# Custom Main Menu

The archipelago mod's `main_menu` subsystem (`mods/archipelago/src/main_menu.c`, booted from
`main.c`'s `OnBoot` via `MainMenu_OnBoot`) turns the vanilla "KIRBY AIR RIDE" title logo into
the KARchipelago logo. It keeps the vanilla "KIRBY" text and blue swoosh and replaces only the
"AIR RIDE" subtitle with two pieces loaded from a mod asset - the "AIRRIDE / ARCHIPELAGO"
subtitle and a six-Kirby cluster - drawn in the title foreground scene. The rest of the title
screen (background, Kirby, menu options) is untouched. `main_menu.c` also swaps the idle title
demo player - Kirby on Warp Star to Dedede on Wagon - by rewriting the three `li r4` operands
(RiderKind, IsBike, MachineKind) of the demo setup at `0x8000d340`/`0x8000d34c`/`0x8000d358`.
The demo ride must stay star-class (`is_bike = 0`): the demo init uses hardcoded star-only
state ids and a wheel-class machine crashes there.

## Hooks

Two vanilla code sites are hooked (the title minor's `cb_Exit` and `cb_ThinkPreGObjProc` are also
wrapped through the scene descriptor, covered under Demo Machine Audio):

- **Title file load (`0x8000d2b4`)** - `MainMenu_OnTitleLoad` calls
  `Gm_LoadGameFile(&menu_archive, "MnTitleKarchi")`, pulling `MnTitleKarchi.dat` from the disc
  overlay into the title-screen heap. `Gm_LoadGameFile` appends the `.dat` extension.
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
registers its own GX render link. That render callback (`0x80138ae0`) dereferences the
element's `userdata` (GOBJ+0x2c) and only draws when its `is_visible` flag (userdata+0x8, bit
0x80) is set - both of which `MenuElement_AddData` (`0x80138a00`) provides. So the static
pieces need no per-frame proc, but `AddData` must be called or the render callback faults on a
null userdata. The public is a `JOBJSet` array (`set[0]->jobj` is the root JOBJDesc), matching
the vanilla `Sc*_scene_models` layout.

## Demo Machine Audio

Every star machine requests the same two looping SFX at spawn - a constant engine loop and a
surface loop - so the Wagon Star is not doing anything the Warp Star does not. What differs is
the idle volume floor in the per-kind audio record, and that is the whole reason the swap needs
any audio handling at all.

Those records come from `iso/files/VcCommon.dat`. `vcLoadCommon` (`0x801c6d0c`) stores
`vcDataCommon+0x10` into `r13+0x764` (`0x805dd844`), a two-entry pointer array indexed by
`MachineData.is_bike`; each entry points at `0x94`-byte records indexed by `MachineData.kind`.
Field `+0x00` is the engine loop id, `+0x1c` the surface loop id, `+0x5c` and `+0x60` the engine
volume coefficient and idle floor.

`Machine_UpdateEngineLoop` (`0x801dcb18`) computes the engine loop's volume as
`md->x870 * rec[0x5c] + rec[0x60]`, clamped to `1.0`, and `Machine_PlaySFX` (`0x801dd17c`) scales
that by 255. A volume near zero never
becomes a voice at all, which is why a vanilla title screen shows every `sounds[].sfx_id` at `-1`
and zero live FGM instances despite the Warp Star requesting both loops:

| | engine id | coef `+0x5c` | idle floor `+0x60` | at idle |
|---|---|---|---|---|
| `VCKIND_WARP` | `0x30017` | 0.1 | 0.0 | volume 0, no voice created |
| `VCKIND_WAGON` | `0x3001c` | 0.1 | 20.0 | clamps to full volume |

Five of the nineteen star kinds have a nonzero floor - `BULK` (40), `WAGON` (20), `TURBO` (18),
`JET` (12), `FORMULA` (10). The surface loop's floor (`+0x48`) is 0.03 for every star kind, below
the threshold, so only the engine loop is ever audible at idle. Picking any of the other fourteen
star kinds for the demo ride would remove the need for the two wrappers below entirely; the
handling below exists to keep the Wagon Star specifically.

Nothing in the title scene stops a loop once it is playing. `SceneExit_TitleScreen`
(`0x8000d5c8`) destroys only the movie GObjs, and the heap reset reclaims `MachineData` without
running `Machine_Destroy` (`0x801c6b44`), so an audible loop would carry on under the main menu
and gain another copy on each title visit.

Two wrappers on the title minor's descriptor handle this:

- `cb_ThinkPreGObjProc` -> `MainMenu_TitleThink` zeroes `engine_idle_floor` on the Wagon Star's
  record, saving the vanilla value once. The Wagon and the Warp Star share an
  `engine_volume_coef` of 0.1, so with the floor at 0 the Wagon's volume arithmetic is identical
  to the Warp Star's and the loop stays inaudible for the same reason vanilla's does. Nothing
  about the demo machine is special-cased; it simply stops being one of the five loud kinds.
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
`Machine_PlaySpawnSound`, which starts only the surface loop) and slew-ramped up from there, so
no audible frame can slip through. Restoring on exit matters because the record is shared game
data - a Wagon Star in City Trial reads the same fields.

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
hazard, and hoshi relocates the minor table (`Gm_Minor`'s seven lookup sites are patched to
`minor_scene_descs`), so the entry is live.

## Asset Pipeline

`scripts/hsd/make_menu_logo.py` authors `mods/archipelago/assets/MnTitleKarchi.dat` from the
two piece PNGs in the same directory (`AirRide_Archipelago.png`, `Archipelago_Kirbs-05.png`):

    uv run --with pillow python scripts/hsd/make_menu_logo.py

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

The pieces' positions come straight from `karchipelago-logo.png` (the full combined logo): each
piece is composited into that image at native size, and edge template-matching finds its exact
pixel box within it. Those pixel boxes are baked into `PIECES` in `make_menu_logo.py`. One
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

## Planned Animation

The static pieces are milestone one. Later per-piece animation can use the split logo assets in
`~/kirby-assets/` (`Kirby.png`, `Swoosh.png`, `AirRide.png`, `Archipelago.png`, `ap-icon.png`).
Two paths:

- **Rigged animation** authored in HSDRaw/Blender (like KAR Deluxe's title asset, whose custom
  model is one skeleton-root joint carrying a chain of ~30 textured quads with a set animation).
  The C side gains the animation driver: after `MenuElement_Create`, add proc `0x8017b424` and
  `JObj_AddSetAnim`.
- **Code-driven effects** for simple motion - e.g. the Archipelago icon flashing each logo color
  in a circular pattern is a per-frame material-color or texture-swap cycle driven from a think
  proc, the same shape as the moon/stars weather effects.
