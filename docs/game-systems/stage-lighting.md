# Stage Lighting

How light reaches the screen on a 3D stage, with City Trial as the worked case: the hardware
`LOBJ`s a stage loads, the KAR-specific AreaLight that shades riders and machines, why the city
itself barely responds to either, how to add a light, how projected textures reach every
surface, and how shadows work. The short version for a modder: the AreaLight changes how riders
and machines look, the stage's hardware lights reach only its few lit surfaces, and the city's
look changes only through fog, the EFB clear colour, the sky preset's fade overlay, its baked
vertex colours, a texture projected through the shadow-receiver path, or a hardware light once
its display lists are given normals.

## Hardware lights

`LObj_GX` (0x8042a22c) is the GX callback of any GObj carrying an `LOBJ` chain. It runs
`HSD_LObjSetCurrentAll` (0x803ff570), which empties the active list and the 9-entry hardware
slot table at 0x805899b0 (`stc_lobj_hw_slot_table`) and installs that GObj's chain alone,
walking `LOBJ.next`, then `HSD_LObjSetupInit` (0x803fe4b8), which loads each light into its GX
slot. Each light GObj therefore replaces the set rather than adding to it: lit geometry sees the
chain of the last `LObj_GX` drawn before it, in GX link order and then `gx_pri` within a link.
Riders and machines do not take the stage's set at all. `RiderLight_GX` (0x801901c0), the
callback of the rider light GObj on GX link 4, installs the stage's rider chain ahead of the
riders drawn there, and each colour-animated body's draw runs `ColAnim_SetupLights`
(0x8006a55c), which installs that body's own set plus its overlay light while one is live. Read
from game logic, the slot table holds whichever chain was drawn last in the previous frame -
usually not a stage light.

- A positional or directional light (`flags & 3` nonzero) takes slots 0-7 in insertion order.
- An ambient-only light (`flags & 3 == 0`) is pinned to slot 8, the dedicated ambient slot
  (`GX_LIGHT8`); the last one written wins.
- Specular companions attached to a light take further slots from the same counter.

There is no per-material light mask. `HSD_SetupChannelMode` (0x803f7d44) reads three masks that
`HSD_LObjSetupInit` rebuilds every frame - the OR of every active light's id with `LOBJ_DIFFUSE`,
`LOBJ_SPECULAR` and `LOBJ_ALPHA` (`HSD_LObjGetLightMaskDiffuse`, 0x803fdb14, and its siblings) -
so a light added to the active list reaches every lit material at once, with no channel patching.

A light's type is `LObjDesc.flags & 3` (`LOBJ_AMBIENT`, `LOBJ_INFINITE`, `LOBJ_POINT`,
`LOBJ_SPOT`), plus `LOBJ_DIFFUSE`, `LOBJ_SPECULAR`, `LOBJ_ALPHA` and `LOBJ_HIDDEN`. An INFINITE
light's position vector is its direction; its attenuation fields are ignored. `attnflags` bit 0
picks raw attenuation floats over the computed `ref_br` / `ref_dist` falloff.

## The stage's light chains

`GrData.lights` (+0x14) points at three NULL-terminated arrays of `LightGroup *`
(`{LObjDesc *desc, LightAnim *anim}`). `Light_StageInit` (0x800d5ed4), called from `grLoadStage`,
loads all three:

| Chain | Creator | GObj class / GX link | Use |
|---|---|---|---|
| +0x00 primary | `Light_CreateForStage` (0x800d5fd0) | 1 / 0 | stage lights; also sets `stc_main_light` |
| +0x04 rider | `Light_CreateAreaLightDefaults` (0x800d6188) | none | default values for the AreaLight; `RiderLight_Create` (0x801901ec) also draws this chain as its own light GObj (class 19, GX link 4) |
| +0x08 secondary | `Light_CreateForStageSecondary` (0x800d60d8) | 20 / 8 | more stage lights |

`stc_main_light` (`r13 + 0x5fc`) caches the primary chain's first INFINITE light. Writing its
`color` and `hw_color` re-tints everything lit by it, and nothing else writes them after load,
so one write holds. `Light_GX` (0x800d5fb0) and the secondary's callback are thunks to `LObj_GX`.

In `GrCity1.dat` each chain is an ambient light followed by an INFINITE one. The primary pair is
white plus a warm-white directional light from (-1000, 700, 1500); the secondary and the rider
chains carry identical pale-blue ambient and white directional lights from (-1000, 1000, 1500).
The primary chain draws on GX link 0 at `gx_pri` 1, so it is the set the map (link 1) is drawn
under; the secondary, on link 8, takes over only for what draws after it. Each fills slot 8 with
its ambient and slot 0 with its directional light. No other gameplay
code creates an HSD light: the lighthouse beam, the light tunnel, fireworks, item glows and
projectile flashes are all geometry, material animation and particles.

## Why the city barely reacts

`HSD_SetupChannelMode` sends a material through the lit colour channel only when its render mode
has `RENDER_DIFFUSE`. Otherwise the channel is disabled and colour comes straight from per-vertex
colours (`RENDER_VERTEX`) or the material register (`RENDER_CONSTANT`), modulated by texture, and
no light touches it.

In `GrCity1Model.dat` 4 of the map's 180 materials carry `RENDER_DIFFUSE`, about 0.1% of the
island's area, and only 6 of its 208 POBJs carry vertex normals. The rest are unlit: most POBJs
carry baked per-vertex colours (inline RGBA4 in the display lists), and that baked colour is
their appearance, while the sea - two thirds of the model's area, in triangles up to ~1,500
units long - uses the material colour. Forcing `RENDER_DIFFUSE` onto a surface without normals
only darkens it to the ambient term: `HSD_PObjDisp` (0x80407988) declares exactly the POBJ's own
attributes, so the lit channel sees a stale normal.

The lit channel's functions are fixed: every lit material uses `GX_DF_CLAMP` with `GX_AF_SPOT`,
from the channel template at 0x80502330. The normal-free mix of `GX_DF_NONE` with distance
attenuation exists only in the particle renderer (`psRenderParticles`, 0x80433f00). Even with
it, a lit channel computes vertex colour x clamp(ambient + lights), so a white ambient keeps the
baked look but leaves lights no headroom, and the result is per-vertex on large polygons. So a light in the stage's chain lights those few
surfaces and nothing of the city's baked look; riders and machines draw under their own sets. A city-wide change has to go through the paths the sky preset uses - fog
colour, the EFB clear colour and the lbfade overlay - or rewrite the baked colours in the POBJ
display lists, followed by `DCFlushRange` so the GPU sees them.

## Adding normals

The city's display lists can be given normals at runtime. A runtime POBJ copies its desc's
`verts` and `display` pointers, so repointing them at a rebuilt display list and a vertex
descriptor list with one more entry changes that round's instance alone. `HSD_PObjDisp` sets
each entry's format, so an entry of `GX_VA_NRM`, `GX_DIRECT`, `GX_NRM_XYZ`, `GX_S8` (6 fraction
bits) means three bytes per vertex. GX reads a vertex's attributes in a fixed order - matrix
indices, position, normal, colours, texture coordinates - so they go right after the position.

- **Winding.** Front faces wind clockwise on screen, so a triangle's outward normal is
  (v2 - v0) x (v1 - v0), with the first two corners of every odd strip triangle swapped. A
  strip that folds round a corner shares the corner's vertices between both faces, so it has to
  be split there for each face to keep its own normal.
- **Normal matrix.** `PObjSetupMtx` (0x804076f4) loads the normal matrix, the inverse transpose
  of the model-view, only for a joint with `JOBJ_LIGHTING` or a material with a reflection
  TObj. A joint whose POBJs gain normals needs the flag. The inverse transpose scales a normal
  by the inverse of the joint's world scale, which the stored normal has to undo.
- **Size.** `GrCity1Model.dat` holds about 25,700 triangles in 215 KB of display lists. The
  median triangle's longest edge is 28 units and 95% are under 143, so per-vertex lighting
  holds up away from the sea.

A lit channel still multiplies the baked colour by the light, capped at the baked look. A
second channel avoids that cap: `HSD_SetupChannelMode` (0x803f7d44) is the last call of
`HSD_SetupRenderModeWithCustomPE`, and for an unlit material it leaves COLOR0A0 alone and turns
COLOR1A1 off through the channel cache (`HSD_DisableChannel`, 0x803f8e10). A call after it
that sets COLOR1 through `HSD_SetupChannel` (an `HSD_Chan`) and `HSD_StateSetNumChans(2)` holds for
that material only, and a TEV stage ordered on `GX_COLOR1A1` reads the result as RASC.
Vanilla uses COLOR1 only for specular. GX lights sit in eye space, so a light's position goes
through each camera's `view_mtx` before `GXLoadLightObjImm`.

## Projected textures on every surface

The engine's projected-shadow path is the one route that reaches every material, lit or not,
per pixel. `HSD_MObjSetup` (0x803fac18) decides from the material's own `rendermode`: when it
has `RENDER_SHADOW` (0x04000000), the global shadow TObj list (`r13 + 0x1114`, 0x805de1f4,
linked through `TOBJ.next`) is appended to the material's TObj chain for that setup alone.
`HSD_MObjAddShadowTexture` (0x803faec4) and `HSD_MObjDeleteShadowTexture` (0x803faf28) edit the
list. `Map_GX` (0x800d81e4), the map GObj's callback, draws with rendermode 0, so each material's
loaded flags decide; `JObj_SetAllMOBJFlags` (0x80052fb8) ORs a flag over a whole tree, as
`lbShadow_SetReceiver` (0x8007b4c8) does. A rendermode passed to `JObj_DispAll` with
`RENDER_SHADOW` means the opposite - a caster pass that skips material setup.

- **Texgen.** A TObj with coord `TEX_COORD_SHADOW` (3) generates from vertex position through
  `PNMTX0`, post-multiplied by `TOBJ.mtx` (+0x74) concatenated with the current camera's inverse
  view (`TObjSetupMtx`, 0x803f5900). That runs at every material setup for every camera, so
  `mtx` is a world-to-texture projection a mod can rewrite each frame, split screen included.
  Clear `TEX_MTX_DIRTY` or the TObj class rebuilds `mtx` from its SRT fields. Envelope-skinned
  POBJs do not load `PNMTX0`, so their projection is wrong.
- **TEV.** Shadow TObjs are not compiled into a material's TEV program. `HSD_TObjSetupVolatileTev`
  (0x803f5e78) emits one stage per consecutive shadow TObj from the first with
  `TEX_LIGHTMAP_SHADOW` (0x100), each copied from the `HSD_TevDesc` template at 0x80501fdc: colour
  PREV x TEXC, alpha passed through, the TObj's colormap ignored. As shipped it can only darken;
  rewriting the template's selectors (`d` = CPREV gives PREV x (1 + TEX), `a` = TEXC with
  `b`, `c` zero and `d` = CPREV gives PREV + TEX) makes it brighten. For a stage per TObj,
  replace the loop's `HSD_SetupTevStage` call (0x803f5f50): the template arrives with the stage,
  coord and texmap filled in, each TObj's `id` is its texmap for this material, and further
  stages from `HSD_StateAssignTev` are counted, since `HSD_SetupRenderModeWithCustomPE` sets
  the stage total afterwards. The TEV compare ops then allow a per-pixel test, such as a
  pixel's height read off a ramp texture against a height stored in another. The alternative is a TObj
  flagged `TEX_LIGHTMAP_EXT` with `TEX_COLORMAP_ADD`, compiled in by `HSD_MObjCompileTev`
  (0x803faa80) while it is on the list; the compiled stage then holds its texmap for the
  material's lifetime.
- **Budget.** `HSD_TObjAssignResources` caps a material at 8 texmaps and silently drops the
  overflow. No City Trial material has more than one TObj of its own.
- **Owners.** Only the City Trial ending scene uses this path: `CityEnding_MinorLoad` (0x8004f3c4)
  builds two lbshadow casters (`fn_makeShadow`, 0x8007b284) and `CityEnding_PreRender`
  (0x8004f5b0) renders them through `fn_shadowRendering` (0x8007ade8). The list head and the
  template are shared with it. Several Air Ride stage models carry `RENDER_SHADOW` on some
  materials (61 of 140 in `GrCheck2Model.dat`, one decal in `GrCity1Model.dat`), dormant since
  no Air Ride code allocates a shadow.

A projection is 2D: it has no N.L and no occlusion, so a top-down one streaks down walls and
lights both faces of a bridge. Fog is applied after TEV, so projected light fogs with distance.

## The AreaLight

The AreaLight is a KAR-specific light object (`lbarealight.c`), not an `LOBJ` and never in the
hardware slot table. Every AreaLight is created by `AreaLight_Create` (0x80079428) and pushed onto
one registry list at `r13 + 0x538`. Its `kind` indexes a three-word handler table (walk, free,
lerp) at 0x8049ac60.

- **The stage's AreaLight** (kind 0) sits at `GrObj+0x718`. `AreaLight_StageInit` (0x800ef618)
  seeds it from `Light_GetAreaLightDefaults` (0x800d61e8) - the rider chain's first ambient and
  directional light - and from then on `Sky_Update` writes it every frame from the active sky
  preset. Its `is_visible` bit (0x80 of +0x38) follows the preset's `light_vis_flag` through
  `AreaLight_BroadcastVisFlag` (0x80079948).
- **Consumers** are the riders and machines. `AreaLight_RegistryWalk` (0x8007a2c0), called from
  `Rider_PostTransformThink` (0x8018f800) and from machine creation and pose updates, collects the
  nearest visible lights into a sorted per-object array (`AreaLight_InsertSorted`, 0x80079a60, up
  to nine). Nothing copies an AreaLight into an `LOBJ`, so a sky change re-shades characters while
  the stage's hardware lights keep their colours.
- **City Trial's zone lights.** The round also builds 128 kind-3 AreaLights, one per collision
  zone, carrying per-district colours. Kind 3's walk handler is NULL, so the registry walk skips
  them and they reach nothing on screen; writing them is not a way to tint a region.

To make riders pick up a colour of their own, add a kind-0 AreaLight (or give kind 3 a walk
handler at 0x8049ac84) so the registry walk collects it.

## Adding a light

`RuleMenu_CreateLObj` (0x8013aa60) shows the whole recipe:

```c
GOBJ *g = GObj_Create(38, 32, 0);
LOBJ *l = LObj_LoadDesc(&desc);
GObj_AddObject(g, HSD_OBJKIND_LOBJ, l);
GObj_AddGXLink(g, LObj_GX, 0, 0);
```

`HSD_LObjAddCurrent` is not needed: `LObj_GX` rebuilds the active list from the GObj's chain every
frame. That same rebuild means a light GObj of its own lights only what draws between it and the
next light GObj; to reach what a stage chain lights, the `LOBJ` has to sit in that chain.
`LObj_SetPosition` / `LObj_SetInterest` place it, each allocating a backing WObj on first use; to
follow a moving object, rewrite the WObj position every frame. The light is freed with the
scene, so a mod recreates it each round. Colour changes on a loaded `LOBJ` (`color` +0x10,
`hw_color` +0x14) persist, because `HSD_LObjSetCurrentAll` reassigns slots but never rewrites
them; that is also how a mod can silence one of the stage's own lights.

## Shadows

City Trial shadows ignore lighting entirely. Two shadow systems exist:

| System | Used by | What it is |
|---|---|---|
| SimpleShadow | City Trial: riders, machines, items, event actors | a textured blob laid flat on the ground straight below the entity |
| HSD projected shadow (`lbshadow.c`, `fn_makeShadow`, 0x8007b284) | the City Trial ending scene only | a silhouette projected from a scene light |

`SimpleShadow_CreateManager` (0x8027b294) builds one scene-wide manager GObj that draws on GX link
2 (`SimpleShadow_GX`, 0x8027ae50); each entity's shadow is a lightweight GObj holding a blob JObj
(`SimpleShadow_CreateGObj`, 0x8027b418). Placement casts a fixed ray straight down
(`Raycast_Ground`, 0x800d1ac4), and the blob shrinks with height and disappears past a maximum.
Each entity class gates its own blob: `Rider_UpdateSimpleShadowRender` (0x80195800),
`Machine_UpdateSimpleShadowRender` (0x801d108c), `Item_UpdateSimpleShadowRender` (0x80261a54),
and for event actors `EventActor_SetShadowActive` (0x802000ac) and `EventActor_ClearShadowActive`
(0x802000c0). A light-aware City Trial shadow would mean slanting that ray and quad, or bringing
the ending scene's projected path into the round.

## Key functions

| Function | Address | Role |
|---|---|---|
| `LObj_GX` | 0x8042a22c | per-frame slot assignment and GX load of a GObj's lights |
| `HSD_LObjSetCurrentAll` | 0x803ff570 | rebuild the active list and hardware slot table |
| `HSD_LObjSetupInit` | 0x803fe4b8 | load lights into GX and rebuild the light masks |
| `HSD_MObjSetup` | 0x803fac18 | per-material setup; appends the shadow TObj list to `RENDER_SHADOW` materials |
| `HSD_MObjAddShadowTexture` | 0x803faec4 | add a TObj to the shadow list |
| `HSD_TObjSetupVolatileTev` | 0x803f5e78 | emit the shadow TObjs' TEV stages from the template |
| `Light_StageInit` | 0x800d5ed4 | load the stage's three light chains |
| `AreaLight_Create` | 0x80079428 | create and register an AreaLight |
| `AreaLight_Lerp` | 0x800797a8 | lerp an AreaLight toward a preset's `AreaLightData` |
| `AreaLight_RegistryWalk` | 0x8007a2c0 | collect the nearest visible AreaLights for a rider or machine |
| `SimpleShadow_GX` | 0x8027ae50 | draw every enabled blob shadow |
