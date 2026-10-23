# HUD

Every element of the round HUD is a JObj model tree hanging off a GObj on a HUD GX link, drawn by
an orthographic camera. Digits, gauges and labels are frame-indexed material animations, so
"setting a value" means setting an animation frame. A mod can reuse the game's own HUD models -
pull a model out of an `IfAll*` archive, load it onto a GObj on a HUD link, and drive its child
joints each frame - or give its elements a camera of their own.

## Element GObjs

| Field | Value |
|---|---|
| `entity_class` | 27, hardcoded by every creator |
| `p_link` | Per-player elements use `GAMEPLINK_HUD` (26); the pause stat chart uses `GAMEPLINK_PAUSEHUD` (27), which is what keeps it running while paused |
| `gx_link` / `gx_pri` | Usually `GAMEGX_HUD` (21) at priority 1; the indicators use `GAMEGX_HUDORTHO` (18) |
| `hsd_object` | The JObj tree |
| `userdata` | An optional `HUDElementData`, attached by `HUD_AddElementData`, not by the creator |
| proc | Per-frame update at priority 20 |
| `gx_cb` | A visibility-gated wrapper, or plain `JObj_GX` |

`HUD_CreateMiscElement(jobjdesc, p_link, gx_link, gx_pri)` (0x801147dc) is the engine's creator:
`GObj_Create(27, p_link, 0)`, `JObj_LoadJoint` (0x8040afe8), `GObj_AddObject`, then
`GObj_AddGXLink` with the visibility-gated `3DHud_RenderIfVisible` (0x8011500c), and
`JObj_SetAllMOBJFlags(jobj, 0x28000000)` (0x80052fb8). `HUD_CreateMiscElement2` (0x8011487c) and
`HUD_CreateMiscElement3` (0x80114904) are variants with other fixed parameters.
`HUD_CreatePlyElement(view, jobjdesc)` (0x80114ba4) creates on p_link 26 with `HUD_GXLink` at GX
link 21, priority 1, and with more than one viewport up moves the root to that viewport's region.

### HUDElementData

`HUD_AddElementData(gobj, kind, ply, view)` (0x80114e24) allocates a zeroed 0xE4-byte
`HUDElementData`, attaches it as userdata (destructor 0x801151e8), stores `kind` at +0x4, packs
`ply` and `view` into the byte at +0x8, and sets **`is_visible`** (bit 0x02 of that byte).
`HUD_SetVisible` (0x80114eec) and `HUD_SetInvisible` (0x80114f04) toggle it afterwards. Both
gated callbacks skip drawing while it is 0, so an element created by `HUD_CreateMiscElement` draws
nothing until it has element data or is made visible. `HUDKind` is read only by element-specific
update procs, so a custom element may pass any kind it does not collide with.

## Rendering

Rendering has two levels. **Camera GObjs** each own a GX link of 64 or above, and their
`cobj_links` bitmask selects which links (0-63) they draw. **Renderable GObjs** sit on one of those
links with a `gx_cb`. For each camera, `CObjThink_Common` (0x8042a29c) sets up the projection and
calls `CObj_RenderGXLinks` (0x8042a0b4) with passes 0-2, which walks every object on each selected
link and calls its `gx_cb`, one pass at a time across all links. `HSD_GetRenderPass` (0x8042a0a0)
maps each pass to the DObj bucket it draws (hoshi's `GXRenderPass`): 0 opaque, 1 TEXEDGE
(`RENDER_XLU` materials that still update Z), 2 XLU (those that also set `RENDER_NO_ZUPDATE`). Only the camera's `cobj_links` matters:
`GObj_Create` zeroes it on every object and nothing reads it on a non-camera.

| Callback | Address | Behavior |
|---|---|---|
| `JObj_GX` | 0x8042a258 | Draws the JObj tree unconditionally |
| `3DHud_RenderIfVisible` | 0x8011500c | Checks `is_visible`, then `JObj_GX` (installed by `HUD_CreateMiscElement`) |
| `HUD_GXLink` | 0x80114f1c | Checks `is_visible`, scissors to the player's viewport, `JObj_GX`, restores the scissor (installed by `HUD_CreatePlyElement`) |

| GX link | Enum | Use |
|---|---|---|
| 18 | `GAMEGX_HUDORTHO` | Player numbers, icons, indicators |
| 19 | `GAMEGX_HUDMAP` | Minimap |
| 20 | `GAMEGX_HUDMAPDOTS` | Minimap dots |
| 21 | `GAMEGX_HUD` | Timer, stat bars, other elements |

All four have cameras during a City Trial round. Link 18 suits a custom element drawn with plain
`JObj_GX`, since the game's own elements there use their own callbacks; everything on link 21 comes
through `HUD_CreateMiscElement`, so a custom element there either gets element data or uses
`JObj_GX` directly.

**Coordinates.** Every HUD ortho camera uses top 0, bottom -480, left 0, right 640: X runs 0 to 640
left to right, Y runs 0 to **-480** top to bottom, Z is 0. `CObj_SetOrtho(cobj, top, bottom, left,
right)` (0x80402f08) sets a camera to match.

**Hidden flags.** `JObj_LoadJoint` copies `JOBJ_HIDDEN` (0x10) from the descriptor, and some HUD
models ship hidden because the game shows them later; loading with `is_hidden = 0` does not clear
it, so clear it with `JObj_ClearFlagsAll` after loading. `HUD_UpdateElement`'s animation pass can
also clear `JOBJ_HIDDEN` on joints it touches, so a part that must stay hidden is re-hidden after
every update. After writing a joint's `trans` or `scale`, `JObj_SetMtxDirtySub` (0x8040d92c) makes
the world matrix recompute.

## Archives

HUD models come from `IfAll*.dat` archives, named by family: IfAll1 the common round HUD,
IfAll2 the open City Trial map, IfAll3 the stadiums; `c` marks the common file and 1, 2 or 4
the per-viewport-count file. `IfAll1c` is preloaded and always resident
(`Gm_GetIfAllCityArchive`, 0x80112050). `3D_LoadHUDFile` (0x80112cac) loads the rest per round: the
IfAll1 file for the view count (`Gm_GetIfAllScreenArchive`, 0x80112058; the `s` variants in the
open City Trial map), the IfAll2 pair in the open map, and the IfAll3 pair in a stadium.

To list a loaded archive's publics at runtime, walk `arch->header.nb_public`, reading each name at
`arch->symbols + arch->public_info[i].symbol` and its data at `arch->data +
arch->public_info[i].offset`.

## Reusable Models

**The stat gauge.** `ScInfPausegaugect_scene_models` (`IfAll1c`) is the City Trial pause chart's
bar: depth-first joint 1 is the fill bar, 4 and 5 two digits (4 sits on the right, 5 on the left),
6 a minus sign, all direct children of the root. `HUD_UpdateElement(jobj, value)` (0x8011503c)
sets a joint's animation frame and runs it, and the digit images are frame-indexed, so frame N
shows N; the digit `TexAnim` has 11 images, 0-9 and a spare. A lone digit on joint 5 reads as
centered.

`CityHUD_CreateStatChart(ply, view)` (0x80128bb8) builds the real chart: a background by view count
(`HUD_CreateMiscElement` on p_link 27, GX link 21, priority 1, kind `HUDKIND_CITYSTATBG`) whose
child joints 1-9 give the nine stat slot positions, stored in its element data from +0x14.
`CityHUD_CreateStatBar(ply, view, stat_kind)` (0x80129154) then loads one gauge per stat with the
same parameters (kind `HUDKIND_CITYSTATBAR`), caches the four part joints, and moves the gauge to its
slot. The update proc reads `Ply_GetCityStatNum(ply, stat_kind, 0)`, adds 2 for stats 0-7 - which
is why a fresh stat reads 2 - fills the bar only for positive values, and shows the sign only at -10
or below. The chart and bars are stored in `Game3dData.cityui_statchart_gobj[ply]` (+0x63c) and
`cityui_statbar_gobj[ply][stat_kind]` (+0x650). Loading the gauge on its own does not conflict
with the real chart as long as `CityHUD_CreateStatChart` is not called for it.

**The timer.** `ScInfTime_scene_models` (1-2 views, cached at `Game3dData+0xb4`) and
`ScInfTime4_scene_models` (3-4 views, +0xb8). `3DHud_CreateTimer` (0x80119218) picks one by view
count and adds `3DHud_TimerUpdate` (0x80118f50) at priority 20; it uses p_link 27 in Air Ride Free
Run and 26 elsewhere. Joints 1-6 are six frame-indexed digits from least significant (frames' ones)
to most (minutes' tens), so the model doubles as a six-digit readout, though it may carry separator
geometry between digit pairs.

**The player number.** `ScInfPlynum_scene_models` (`Game3dData+0xc0`, `3DHud_CreatePlyNum`
0x8011fd20) is one model whose animation frame selects "P1", "P2" and so on - a label, not digits.

**The legendary pieces.** `3DHud_IndexLegendaryPiecesSymbols` (0x8012b39c) caches the IfAll2
piece models in `Game3dData`: the anchor row (+0xdec), the set-complete effect (+0xdf0), three Hydra
(+0xdf4) and three Dragoon (+0xe00) icons, and the live element per viewport (+0xe0c). The anchor is
a root plus six empty joints in a row, 1-3 for Hydra and 4-6 for Dragoon, and an icon fills the
first free anchor of its half in collection order - the display says how many pieces of a set are
held, not which.

## A Custom Element

```c
HSD_Archive **arch = Gm_GetIfAllCityArchive();
JOBJSet **sets = Archive_GetPublicAddress(*arch, "ScInfPausegaugect_scene_models");

// JObj_LoadSet_SetPri draws with plain JObj_GX: no visibility gate
GOBJ *gauge = JObj_LoadSet_SetPri(0, sets[0], 0, 0.0f, GAMEPLINK_HUD, GAMEGX_HUD, 1, NULL, 0);
JOBJ *root = gauge->hsd_object;
JObj_ClearFlagsAll(root, JOBJ_HIDDEN);   // the model ships hidden
root->trans = (Vec3){ x, y, 0 };
JObj_SetMtxDirtySub(root);

JOBJ *bar_j = GObj_GetJObjIndex(gauge, 1);   // depth-first joint index
```

`JObj_LoadSet_SetPri` is hoshi's helper; the engine's own creator works too, as long as element data
or `HUD_SetVisible` sets `is_visible`. Several instances of a model load independently, and
`GObj_Destroy` is enough to remove one.

To stay independent of the game's camera management, an element can own an ortho camera on a
free GX link:

```c
#define CUSTOM_GX_LINK 23

GOBJ *cam = GOBJ_EZCreator(0, GAMEPLINK_SYS, 0, 0, 0, HSD_OBJKIND_COBJ, stc_text_cobjdesc,
                           0, 0, CObjThink_Common, 0, 5);
cam->cobj_links = 1ULL << CUSTOM_GX_LINK;
CObj_SetOrtho(cam->hsd_object, 0.0f, -480.0f, 0.0f, 640.0f);

GOBJ *hud = JObj_LoadSet_SetPri(0, sets[0], 0, 0.0f, GAMEPLINK_HUD, CUSTOM_GX_LINK, 1, NULL, 0);
```

`stc_text_cobjdesc` (0x805096a0) is the game's 640x480 ortho camera descriptor, the one
`Text_CreateCanvas` loads. Destroying the element and the camera is the whole teardown.

## Constraints

- Models draw with the colors baked into their materials; there is no per-element color override,
  so per-player tinting means work at the material level.
- The game's own stat bars sit at positions known only through the chart background's nine slots,
  so an element placed by absolute coordinates can overlap them.
