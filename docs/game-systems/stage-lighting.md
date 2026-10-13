# Stage Lighting

How light reaches the screen on a 3D stage, with City Trial as the worked case: the hardware
`LOBJ`s a stage loads, the KAR-specific AreaLight that shades riders and machines, why the city
itself barely responds to either, how to add a light, and how shadows work. The short version for
a modder: hardware lights and the AreaLight change how riders, machines and items look; the city's
look changes only through fog, the EFB clear colour, the sky preset's fade overlay, or its baked
vertex colours.

## Hardware lights

`LObj_GX` (0x8042a22c) is the GX callback of any GObj carrying an `LOBJ` chain. It runs
`HSD_LObjSetCurrentAll` (0x803ff570), which rebuilds the active list every frame from the chain
attached to the GObj and assigns hardware slots into the 9-entry table at 0x805899b0
(`stc_lobj_hw_slot_table`), then `HSD_LObjSetupInit` (0x803fe4b8), which loads each light into its
GX slot.

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
| +0x04 rider | `Light_CreateAreaLightDefaults` (0x800d6188) | none | default values for the AreaLight; the rider system renders this chain as its own light GObj |
| +0x08 secondary | `Light_CreateForStageSecondary` (0x800d60d8) | 20 / 8 | more stage lights |

`stc_main_light` (`r13 + 0x5fc`) caches the primary chain's first INFINITE light. Writing its
`color` and `hw_color` re-tints everything lit by it, and nothing else writes them after load,
so one write holds. `Light_GX` (0x800d5fb0) and the secondary's callback are thunks to `LObj_GX`.

In `GrCity1.dat` each chain is an ambient light followed by an INFINITE one. The primary pair is
white plus a warm-white directional light from (-1000, 700, 1500); the secondary and the rider
chains carry identical pale-blue ambient and white directional lights from (-1000, 1000, 1500).
A City Trial round therefore runs four hardware lights - both ambients on slot 8, the primary
directional on slot 0 and the secondary on slot 1 - leaving several slots free. No other gameplay
code creates an HSD light: the lighthouse beam, the light tunnel, fireworks, item glows and
projectile flashes are all geometry, material animation and particles.

## Why the city barely reacts

`HSD_SetupChannelMode` sends a material through the lit colour channel only when its render mode
has `RENDER_DIFFUSE`. Otherwise the channel is disabled and colour comes straight from per-vertex
colours (`RENDER_VERTEX`) or the material register (`RENDER_CONSTANT`), modulated by texture, and
no light touches it.

In `GrCity1Model.dat` only a handful of terrain materials carry `RENDER_DIFFUSE`, and only those
few polygon objects carry vertex normals; most of the city's POBJs carry baked per-vertex colours
instead, and that baked colour is its appearance. Forcing `RENDER_DIFFUSE` onto a surface without
normals only darkens it to the ambient term. So a new light lights riders, machines, items and
those few surfaces. A city-wide change has to go through the paths the sky preset uses - fog
colour, the EFB clear colour and the lbfade overlay - or rewrite the baked colours in the POBJ
display lists, followed by `DCFlushRange` so the GPU sees them.

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
frame. `LObj_SetPosition` / `LObj_SetInterest` place it, each allocating a backing WObj on first
use; to follow a moving object, rewrite the WObj position every frame. The light is freed with the
scene, so a mod recreates it each round. Colour changes on a loaded `LOBJ` (`color` +0x10,
`hw_color` +0x14) persist, because `HSD_LObjSetCurrentAll` reassigns slots but never rewrites
them; that is also how a mod can silence one of the stage's own lights.

## Shadows

City Trial shadows ignore lighting entirely. Two shadow systems exist:

| System | Used by | What it is |
|---|---|---|
| SimpleShadow | City Trial: riders, machines, items, event actors | a textured blob laid flat on the ground straight below the entity |
| HSD projected shadow (`lbshadow.c`, `fn_makeShadow`, 0x8007b284) | Top Ride only | a silhouette projected from a scene light |

`SimpleShadow_CreateManager` (0x8027b294) builds one scene-wide manager GObj that draws on GX link
2 (`SimpleShadow_GX`, 0x8027ae50); each entity's shadow is a lightweight GObj holding a blob JObj
(`SimpleShadow_CreateGObj`, 0x8027b418). Placement casts a fixed ray straight down
(`Raycast_Ground`, 0x800d1ac4), and the blob shrinks with height and disappears past a maximum.
Each entity class gates its own blob: `Rider_UpdateSimpleShadowRender` (0x80195800),
`Machine_UpdateSimpleShadowRender` (0x801d108c), `Item_UpdateSimpleShadowRender` (0x80261a54),
and for event actors `EventActor_SetShadowActive` (0x802000ac) and `EventActor_ClearShadowActive`
(0x802000c0). A light-aware City Trial shadow would mean slanting that ray and quad, or porting
the Top Ride projected path.

## Key functions

| Function | Address | Role |
|---|---|---|
| `LObj_GX` | 0x8042a22c | per-frame slot assignment and GX load of a GObj's lights |
| `HSD_LObjSetCurrentAll` | 0x803ff570 | rebuild the active list and hardware slot table |
| `HSD_LObjSetupInit` | 0x803fe4b8 | load lights into GX and rebuild the light masks |
| `Light_StageInit` | 0x800d5ed4 | load the stage's three light chains |
| `AreaLight_Create` | 0x80079428 | create and register an AreaLight |
| `AreaLight_Lerp` | 0x800797a8 | lerp an AreaLight toward a preset's `AreaLightData` |
| `AreaLight_RegistryWalk` | 0x8007a2c0 | collect the nearest visible AreaLights for a rider or machine |
| `SimpleShadow_GX` | 0x8027ae50 | draw every enabled blob shadow |
