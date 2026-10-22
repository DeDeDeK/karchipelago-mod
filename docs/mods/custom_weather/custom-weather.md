# Custom Weather

`custom_weather` changes what City Trial looks and feels like round to round. It replaces the
game's sky roll with a pool of vanilla and custom sky presets, appends its custom presets to the
stage's preset table so the engine treats them as its own, layers world-space weather on top -
rain, snow, hail, wind, puddles, clouds, lightning, moon, stars, an erupting volcano, a tornado,
earthquakes, tsunamis, a glow under every machine on a darkened city, a shadow-casting light at
the star pole's star - swaps the backdrop for another stage's, and can stop City Trial events from changing the sky.
Everything is configured from one settings menu. This doc covers the architecture: the preset
extension, the hooks, the per-frame runtime, the presets and the settings.

## The engine it builds on

City Trial's look comes from a table of 0x48-byte `SkyPresetEntry` records reached through
`GrData.sky_block->preset_header->{preset_array, preset_count}`. `Sky_Init` (0x8010f114) picks the
round's preset (a weighted roll over `{0, 10, 11, 12}` for City Trial, preset 0 for Free Run),
stores it with `Sky_SetPresetIndex` and applies it with `Sky_LoadPreset`. Each frame `Sky_Update`
(0x800dc640) lerps toward the target preset and writes `HSD_Fog.color/start/end`, the global EFB
clear colour, the sky tint colour and the AreaLight that shades riders and machines. It never
writes `HSD_Fog.type`, `HSD_Fog.scale` or any `LOBJ` colour, so a mod owns those with one write.

City Trial's terrain is almost entirely unlit - its colour is baked into vertex colours - so no
hardware light changes the look of the city. City-wide change has to go through fog, the EFB
clear colour, or the lbfade overlay that `Sky_BeginTransition` drives from a preset's
`fade_color`. The stage's hardware lights reach only a few lit surfaces, and riders and machines
draw under light sets of their own, shaded by the AreaLight.

## Preset extension

On every City Trial and Free Run `Sky_Init` the mod copies the stage's vanilla presets into a
static array, appends its own, and repoints `{preset_array, preset_count}` at the longer array.
Since that pair is all `Sky_GetPresetCount` and `Sky_BeginTransition` read, vanilla code - event
transitions included - indexes the custom presets like any other. `GrCity1.dat` can stay resident
into the next round with the extended array still in place, so the copy runs only from the
stage's own array.

Each custom entry is a copy of a vanilla `base_preset` (inheriting its AreaLight flags and
attenuation) with the fog colour and distances, sky colour, AreaLight colour, specular colour and
direction, and `light_vis_flag` overridden. `transition_frames` is forced to 1 so the change
snaps. `fade_color` carries the preset's `screen_tint`, and that is load-bearing:
`Sky_BeginTransition` fires the lbfade overlay with the target's `fade_color` on every
transition, and the overlay holds whatever it was last driven to. If the custom entries left it
0, the first event that swapped the sky would end with the restore fading the round's tint away
for good. With it filled in, the restore reproduces the tint by itself.

The custom indices are fixed by `WeatherKind`, which puts them right after a vanilla count of 17
(`WEATHER_VANILLA_NUM`), and the copy takes exactly 17 entries. `GrCity1.dat` ships 19: entries 17
and 18 are the Meteor and Lighthouse event skies. Those two are dropped, Blood Rain and Storm take
their indices, and those two events transition to the custom presets' skies instead. A stage
with fewer than 17 presets gets Day and no custom weather, with one log line.

## Hooks

| Site | In | What it does |
|---|---|---|
| 0x8010f1a4 | `Sky_Init`, the City Trial roll | extends the table, rolls among the enabled presets with `Gm_Roll` (Day if none is enabled), `Sky_SetPresetIndex` |
| 0x8010f224 | `Sky_Init`, City Trial Free Run | the same, in place of the fixed preset 0 |
| 0x800ce648 | `Gr_Think` (0x800ce618), right after its `bl Sky_Update` | runs the per-frame weather runtime with the GrObj (`r31`) |
| 0x800dcc18, 0x800dce84 | `CreateStageModel_3D` (0x800dcbf0) | backdrop selection and backdrop scale |
| `Sky_TransitionGlobal`, `Sky_RestoreGlobal` | replaced at boot | the event sky toggle |
| `On3DLoadEnd` | every 3D round | resets the runtime and every layer |
| `On3DExit` | every 3D round | puts the stage's backdrop slot back |

The runtime hook sits after `Sky_Update` so the mod's writes land on top of the frame's sky
writes instead of under them. `Gr_Think` is the stage GObj's priority-1 proc on p_link 7, so the
runtime freezes with the match pause and the hitstops, and its velocity writes land before
machine and item physics integrate them at priority 4. Both `Sky_Init` hooks cover only
StageKinds 9 and 52, so the Destruction Derby 4 and 5 stadiums, which also run on the City Trial
ground, keep their fixed skies. The runtime and the backdrop hooks test the ground
(`gr_kind == GR_CITY1`) instead, so they do run in those two stadiums.

## Per-frame runtime

The runtime runs every frame on the City Trial ground. `On3DLoadEnd` resets it before the round's
first frame, dropping cached handles and per-round state: the previous round's GObjs and lights
went with the scene heap, so nothing is written through them.

**A preset change.** The runtime watches `SkyState.current_preset_index`, which only
`Sky_SetPresetIndex` writes. It therefore never sees an event's temporary sky, and the layers stay
on through events. It does see the legendary-machine skies: assembling the Dragoon or Hydra stores
vanilla preset 13 or 14 as the round's new base, which turns the custom preset's layers and tints
off for the rest of the round (layers forced On in the menu keep running, on the schedule they
already had). On a change it looks up the preset's `CustomPresetDef` (NULL for a vanilla preset)
and applies:

| Layer | Mechanism |
|---|---|
| Terrain lights | writes `color` / `hw_color` of the stage's primary light chain: `*stc_main_light` and the ambient light ahead of it, found in the chain of the light GObj `Light_CreateForStage` (0x800d5fd0) builds |
| Fog | writes `HSD_Fog.type` from `fog_type` (a `GXFogType`; 0 keeps the stage's linear fog) and `HSD_Fog.scale` from the Fog Distance setting, vanilla presets included |
| Screen tint | `Sky_BeginFade(grobj, &screen_tint, 30)` once, since the round's first preset arrives through `Sky_LoadPreset`, which fires no overlay |
| Effect layers | each layer latches its config from the def |

A vanilla preset gets the lights' loaded colours back and no layers. Nothing else writes the
fog's type or scale or those light colours, so each write holds until the next preset change.

**Every frame** it ticks each effect layer, wind before every layer that reads it.

Every layer follows one shape, as `Rain_SetActive`, `Rain_Tick` and `Rain_Reset` show: the first
latches the preset's config, with each 0 numeric field taking the layer's default; the second
advances the layer and lazily creates its render GObj; the third drops per-round state. Most layers
switch off when the def is NULL or not enabled. Hail, moon, stars, volcano, tornado, earthquake,
tsunami, machine glow and star light latch their resolved config either way, because their menu's On value can force them onto a preset that ships
them dormant. Anything a layer seeds from the game RNG is seeded per round, so a round draws the
same numbers every time it is played.

The drawn layers share one setup. Each is a proc-less GObj (class 201, p_link 36, past every
engine bucket) drawing in world space from a GX callback on the world camera's GX link 0, in
pass 1 (`GXPASS_TEXEDGE`, after every link's opaque pass and before its no-Z-update translucent
one), opening with hoshi's `GX_BeginXlu`: per-vertex colour, alpha or additive blend,
depth-tested but not depth-writing so stage geometry hides what is behind it, no culling, and
the camera's view matrix as the position matrix. Because geometry is built in world coordinates
and re-emitted per camera, every split-screen viewport draws it correctly with no per-camera
state. Billboards take the camera's axes and eye from the same matrix (`COBJ_GetViewAxes`,
`COBJ_GetViewEye`).

## Presets

Custom presets occupy indices 17-30 of the extended table.

| # | Preset | Base | Layers |
|---|---|---|---|
| 17 | Blood Rain | Red Vignette (15) | rain, lightning with bolts |
| 18 | Storm | Dark Vignette (5) | rain, lightning with bolts, wind, clouds, plasma volcano |
| 19 | Rain | Gray Sky (13) | rain, wind, puddles |
| 20 | Hailstorm | Gray Sky (13) | rain, hail, wind, clouds |
| 21 | Snowstorm | Dense Fog (9) | snow, wind, clouds; exp2 fog |
| 22 | Moonlight | Midnight (1) | moon with moonlight, stars with frequent shooting stars, machine glow |
| 23 | Cotton Candy | Pink Sky (8) | clouds |
| 24 | Toxic | Dark Vignette (5) | light rain, wind, puddles |
| 25 | Bubblegum | Pink Sky (8) | clouds |
| 26 | Volcanic | Dark Vignette (5) | fire volcano, ash (the snow layer), wind, clouds |
| 27 | Tornado | Gray Sky (13) | tornado, rain, lightning with bolts, strong wind, clouds |
| 28 | Earthquake | Dusky Clouds (4) | earthquakes, dust (the snow layer), light wind |
| 29 | Tsunami | Gray Sky (13) | tsunamis, rain, wind, puddles, clouds |
| 30 | Beacon | Midnight (1) | star light over a near-black city (the glow's ambient, no pools) |

A preset is a `CustomPresetDef`. Its static fields are `base_preset`, the fog colour and
distances, `sky_color`, the four terrain light colours (`terrain_diffuse` / `terrain_specular` for
`*stc_main_light`, `terrain_ambient` / `terrain_ambient_specular` for the chain's ambient),
`char_diffuse` / `char_specular` / `char_dir` / `char_dir_lit` (the AreaLight), `screen_tint` and
`fog_type`; then one nested def per layer. Colours are packed RGBA8888. The fog, sky and AreaLight
fields are written into the preset entry as they are, 0 included; a 0 terrain light field keeps
that light's loaded colour. In a nested def `enabled = 0` turns the layer off and a 0 numeric
field takes the layer's default. Alpha is meaningful: `sky_color`'s alpha is the sky tint's
opacity (0 leaves the vanilla skybox showing) and `screen_tint`'s is the overlay strength.

## Settings

The mod's menu, "City Trial Sky", has a Weather Presets pool, a Backdrops pool, one submenu per
layer, and the Event Sky Changes toggle.

- **Pools.** Weather Presets carries Fog Distance (Preset = 1.0x, then 50%, 75%, 125%, 150% or
  200% of the fog wall's distance, applied to every City Trial preset), Enable All / Disable
  All, and an Enabled/Disabled toggle per preset. The roll is `Gm_Roll` (0x800db2b8) over the
  toggles as 0/1 weights, so it picks uniformly among the enabled presets. Every change to the
  pool logs the whole pool as a mask, since it decides which gameplay layers can roll.
- **Layer knobs augment the presets.** A knob with a per-preset field behind it leads with
  **Preset**, which passes the preset's value through: a scale resolves to 1.0x, so its other
  values are only the ones that differ, and a choice such as moon phase resolves to the preset's
  own. Its other values override whichever preset rolls. The **On** value of Moon, Stars,
  Volcano, Tornado, Earthquake, Tsunami, Machine Glow and Star Light forces that layer onto every preset. Lightning Bolts
  reads Preset / Off / On, and On only adds bolts to a preset that already has lightning.
- **Knobs with nothing per-preset behind them** carry no Preset value. A switch - wind slant,
  which things the wind pushes, puddle roaming and visibility, tree sway, tornado screen shake,
  earthquake controller rumble, whether busting the star puts its light out, star shadows - is
  a plain Off / On; a scale - tree sway strength, the shooting stars' size, speed and
  brightness - defaults to Normal. Star Shading picks Normals (the default) or Light Map.
- Gameplay-affecting options log their new value through `on_change`. All settings persist
  through hoshi's keyed menu save, and `ModDesc.affects_gameplay` is set so hoshi saves and
  restores them around a replay.

**Event Sky Changes.** City Trial events reach the sky only through `Sky_TransitionGlobal`
(event start) and `Sky_RestoreGlobal` (event end). The mod replaces both at boot: with the option
On (the default) they behave as vanilla. With it Off the start returns at once, so the round's
weather holds through every event, and the restore does nothing unless the sky's target is no
longer the stored preset. That happens when a legendary machine is finished mid-event:
`Sky_SetDragoonPreset` (0x800d5490) and `Sky_SetHydraPreset` store their preset but leave the
transition to the event's restore. Off also neuters the Dense Fog event, whose whole visual is
its sky preset; the other events have actors of their own.
