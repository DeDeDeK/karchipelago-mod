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

Two vanilla code sites are hooked:

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
