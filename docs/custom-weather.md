# Custom Weather

`mods/custom_weather/` replaces vanilla sky selection in City Trial, appends its own sky
presets, and layers world-space weather effects on top of the engine's lighting system.

## The engine system it builds on

Stage lighting is driven by a per-stage table of `SkyPresetEntry` records (0x48 bytes
each) reached through `GrObj.gr_data->sky_block->preset_header`, whose first two fields
are `{SkyPresetEntry *preset_array, int preset_count}`. City Trial ships 17 of them in
`GrCity1.dat`. `Sky_Init` (0x8010f114) dispatches on `stGetCurrentStageKind` and, for
stage kind 9, picks one at random from `{0, 10, 11, 12}`; stage kind 52 (City Trial Free
Run) hardcodes preset 0. The initial pick goes through `Sky_LoadPreset`, not
`Sky_BeginTransition`, so the lbfade overlay never arms on stage entry.

Each frame `Sky_Update` (0x800dc640) lerps the live preset toward its target and writes
`HSD_Fog.color/start/end`, the global EFB clear color (`*stc_global_fog_color`), the
sky ambient color read back by `Sky_DrawTintQuad`, and the AreaLight at `GrObj+0x718`.
Anything a mod wants to hold across frames has to be written *after* `Sky_Update` runs,
or be a field it never touches - `HSD_Fog.type`, `HSD_Fog.scale`, and LOBJ colors.

Two more engine facts the mod leans on throughout:

- **City Trial geometry is mostly unlit.** Only about 5 of ~180 terrain MObjs carry
  `RENDER_DIFFUSE`, and only 6 of 208 terrain POBJs have vertex normals, so no hardware
  light can change the look of the buildings or roads. A new LOBJ lights riders,
  machines and items. City-wide visual change has to go through the baked paths: fog
  color, the EFB clear, or the lbfade overlay.
- **The City Trial `GrObj` is reused across exit and re-entry**, so `grobj !=
  last_grobj` never fires on the second round. `GrObj.fade_slot_id` (+0x714) *is* fresh
  every entry, because each `ScreenFade_Alloc` increments a global counter, which makes
  it the freshness signal for per-round state.

Putting a positioned light into the scene takes four calls, which is what the lightning
and moon modules do: `GObj_Create(38, 32, 0)`, `LObj_LoadDesc(&desc)`,
`GObj_AddObject(g, HSD_OBJKIND_LOBJ, l)`, `GObj_AddGXLink(g, LObj_GX, 0, 0)`.
`HSD_LObjAddCurrent` is not needed - `LObj_GX -> HSD_LObjSetCurrentAll` rebuilds the
active list every frame from the chain attached by `GObj_AddObject`.

## Preset-array extension

`CustomWeather_ExtendPresetArray` (`custom_weather.c`) copies the 17 vanilla
`SkyPresetEntry` records out of the stage file into a static
`extended_presets[WEATHER_TOTAL]` buffer, appends the custom presets, then repoints
`sky_block->preset_header->{preset_array, preset_count}` at the longer array. Because that
pair is exactly what `Sky_GetPresetCount` and `Sky_BeginTransition` read, vanilla code
(event transitions, the debug selector) can index custom presets after the repoint. The
function is idempotent and runs on every stage load.

`WeatherKind` pins the custom indices at `WEATHER_VANILLA_NUM`, so the copy length is
fixed at 17 rather than taken from `preset_count`; a stage shipping a different count
would put every enum value on the wrong preset, and the mismatch is reported once.

Each custom entry is cloned from a vanilla `base_preset` (inheriting its `AreaLightData`
flags/attn/header), then overrides fog color/start/end, `sky_ambient_color`, the AreaLight
color/hw_color/direction, and `light_vis_flag` (from `char_dir_lit`). `transition_frames`
is forced to 1, so the mod snaps rather than using the vanilla fade.

**`fade_color` carries `screen_tint`**, and that is load-bearing rather than decorative.
`Sky_BeginTransition` (0x800dc354) calls `Sky_BeginFade(grobj, &entry->fade_color,
entry->transition_frames)` on *every* transition, and lbfade slot 3 holds its target
indefinitely rather than restoring. If the custom entries left `fade_color` at 0, the
first City Trial event that swapped the sky would drive the overlay to the event preset's
tint and the event-end restore would then fade it to zero - permanently, since
`Sky_BeginTransition` writes only `SkyState.target_preset` and never
`current_preset_index`, so the runtime would never notice a preset change and re-fire its
own fade. Writing the field means the restore reproduces the round's tint by itself.

The runtime still calls `Sky_BeginFade` once when the preset activates, because the
initial preset arrives through `Sky_LoadPreset`, which does not fire the overlay at all.

## Hooks

| Site | Where | Replaces |
|------|-------|----------|
| 0x8010f1a4 | `Sky_Init` (0x8010f114), stage kind 9 random block | `Gm_Roll` over `{0,10,11,12}` -> `Sky_SetPresetIndex` |
| 0x8010f224 | `Sky_Init`, stage kind 52 (City Trial Free Run) | hardcoded preset 0 |
| 0x800ce648 | `Gr_Think` (0x800ce618), right after `bl Sky_Update` | nothing - appends `CustomWeatherRuntime_Tick(grobj)` |
| 0x800dcc18, 0x800dce84 | `CreateStageModel_3D` (0x800dcbf0) | backdrop selection and dome scale |
| `Sky_TransitionGlobal`, `Sky_RestoreGlobal` | `CODEPATCH_REPLACEFUNC` from `event_sky.c` | event-driven sky swaps |

Both `Sky_Init` hooks land on `CustomWeather_OverrideSky`, which extends the array then
picks uniformly among the **enabled** presets (falling back to Day if none are) and calls
`Sky_SetPresetIndex`.

The per-frame hook is placed *after* `Sky_Update` on purpose: the mod's writes have to layer
on top of the per-frame sky writes rather than be clobbered by them. `r31` holds `grobj`
across the `bl` (it is `Sky_Update`'s own argument), and the trampoline re-runs the
clobbered `lwz r0,4(r31)`.

## Per-frame runtime

`CustomWeatherRuntime_Tick` (`custom_weather_runtime.c`) early-returns on any non-`GR_CITY1`
ground. On a preset change it applies the optional static layers of the active
`CustomPresetDef`; the global fog scale and the effect ticks run every frame.

| Layer | Mechanism | Code |
|-------|-----------|------|
| Terrain re-tint | writes `(*stc_main_light)->color`/`hw_color` - the primary chain's INFINITE LOBJ, which sky presets never touch | `ApplyTerrainTint` |
| Ambient (slot-8) re-tint | writes the slot-8 ambient LOBJ resolved from `stc_lobj_hw_slot_table[8]`; the HW table lags think by a frame, so it retries while either ambient field is set | `ApplyAmbientTint` |
| Fog curve | writes `HSD_Fog.type` from `fog_curve`; `Sky_Update` never lerps type, so one write per preset holds | `ApplyFogCurve` |
| Screen overlay | `Sky_BeginFade(grobj, &screen_tint, 30)` once per preset activation, gated on `grobj->fade_slot_id != 0`; event transitions reproduce it from the entry's `fade_color` | `CustomWeatherRuntime_Tick` |
| Global fog distance | every frame: `HSD_Fog.scale = CustomWeather_GetFogScale()`, the menu-driven multiplier on the fog far wall; covers vanilla presets too | `CustomWeatherRuntime_Tick` |

Effect ticks then run in a fixed order - lightning, wind, rain, snow, hail, puddles, trees,
clouds, moon, stars, volcano, tornado - with wind before every layer that reads its vector.
`event_sky.c` is a sibling module that installs its own boot hook rather than running from
this tick, and `Tornado_OnFrameEnd` runs from the mod's `ModDesc.OnFrameEnd` instead.

Every effect module follows the same shape. `X_SetActive(def)` latches the preset's config
on a preset change, with each 0 numeric field resolving to the module's default. `X_Tick()`
advances the layer and lazily creates its render GObj. `X_Reset()` drops per-stage state -
cached handles and per-round counters - and is called from `ResetPerStage` on the *first
tick of a new City Trial entry*, detected by a changed `grobj` or `fade_slot_id`, not at
teardown. That ordering is why the modules only drop handles and never write through them:
by the time reset runs, the engine has already freed every GObj and stage LOBJ from the
previous round.

Most layers turn off when `def` is NULL or `enabled == 0`. The five whose menu knob can
force them onto a preset that ships them dormant - hail, moon, stars, volcano, tornado -
latch their resolved config either way, so a forced-On value has something to run with.

Modules are independent of the static sky/fog/light fields and almost independent of each
other. The two couplings are deliberate: hail only falls while `Rain_IsActive()`, and rain,
snow, hail, clouds and trees all read `Wind_GetVector()` each frame.

Shared plumbing is in `weather_fx.c`: the round-progress accessor, the stage-node and
live-fog lookups, a ground raycast, the OOB-box query, the schedule seeder, the
enabled-pool picker, the random-range helpers, and the GX layer setup.
`WeatherGX_BeginXlu(cam, additive, line_width)` sets flat per-vertex color, alpha or
additive blend, `GXSetZMode(GX_ENABLE, GX_LEQUAL, GX_DISABLE)` (depth-tested but not
depth-writing, so stage geometry occludes the layer), `GX_CULL_NONE`, and loads the active
camera's view matrix as the position matrix so world coordinates work from every
split-screen viewport. `WeatherGX_EnsureLayer` creates the layer GObj and latches one
pool-exhausted warning across all eight layers. All layers draw on the world camera's
gx_link 0, XLU sub-pass (`pass == 1`), gx_pri 0.

## WeatherKind presets

`WEATHER_VANILLA_NUM = 17`, `WEATHER_CUSTOM_NUM = 11`, `WEATHER_TOTAL = 28`
(`custom_weather.h`). Custom presets occupy indices 17-27:

| Idx | WeatherKind | Name | base_preset | Effect layers |
|-----|-------------|------|-------------|---------------|
| 17 | `WEATHER_BLOOD_RAIN`   | Blood Rain   | Red Vignette (15)  | rain + lightning |
| 18 | `WEATHER_STORM`        | Storm        | Dark Vignette (5)  | lightning + rain + wind + clouds + volcano (plasma) |
| 19 | `WEATHER_RAIN`         | Rain         | Gray Sky (13)      | rain + wind + puddles |
| 20 | `WEATHER_HAILSTORM`    | Hailstorm    | Gray Sky (13)      | rain + hail + wind + clouds |
| 21 | `WEATHER_SNOWSTORM`    | Snowstorm    | Dense Fog (9)      | snow + wind + clouds, exp2 fog curve |
| 22 | `WEATHER_MOONLIGHT`    | Moonlight    | Midnight (1)       | moon + stars (frequent meteors) |
| 23 | `WEATHER_COTTON_CANDY` | Cotton Candy | Pink Sky (8)       | clouds |
| 24 | `WEATHER_TOXIC`        | Toxic        | Dark Vignette (5)  | light rain + wind + puddles |
| 25 | `WEATHER_BUBBLEGUM`    | Bubblegum    | Pink Sky (8)       | clouds |
| 26 | `WEATHER_VOLCANIC`     | Volcanic     | Dark Vignette (5)  | volcano (fire) + snow (ashfall) + wind + clouds |
| 27 | `WEATHER_TORNADO`      | Tornado      | Gray Sky (13)      | tornado + rain + lightning + strong wind + low clouds; green supercell sky |

## CustomPresetDef

Declared in `custom_weather.h` and grouped by on-screen effect, not by engine mechanism. The
static fields are `base_preset`, `fog_color`/`fog_start`/`fog_end`, `sky_color`,
`terrain_diffuse`/`terrain_specular`, `char_diffuse`/`char_specular`/`char_dir`/
`char_dir_lit`, `char_ambient`/`char_ambient_specular`, `fog_curve` and `screen_tint`; the
layer configs are `rain`, `hail`, `snow`, `lightning`, `wind`, `puddles`, `clouds`, `moon`,
`stars`, `volcano` and `tornado`.

Two conventions run through the whole struct: `0` on an optional static field means "inherit
from `base_preset`", and inside a nested def `enabled = 0` disables that layer while a 0
numeric field takes the owning module's default. Alpha in a color field is meaningful -
`sky_color`'s A is skybox opacity (0 leaves the vanilla skybox visible) and `screen_tint`'s A
is overlay strength.

The global **Fog Distance** menu setting is not one of these fields: it scales
`HSD_Fog.scale` for every CT preset, vanilla and custom, via `CustomWeather_GetFogScale`.

## Lightning (`lightning.c`)

Long random lulls punctuated by a brief, bright flash. Because KAR stage geometry does not
read hardware light colors, the terrain flash comes from punching `HSD_Fog.color` and
`*stc_global_fog_color` toward the flash color on a decaying strobe envelope, plus pulling
`fog.start` in so the brightness reaches near geometry rather than only the distance band. A
spare INFINITE LOBJ flashes alongside to catch characters and machines, which *do* read
hardware lights. None of this needs restoring - `Sky_Update` rewrites fog and the EFB clear
every frame, so the preset values return on their own when a strike ends.

`LightningDef` carries `flash_color`, `flash_frames`, `min_lull`/`max_lull` and `bolt`.
Defaults are a near-white flash, an 18-frame envelope and 180-420 frame lulls, with the first
strike 30 frames into the preset. Each strike additionally rolls its own length scale, peak
intensity and strobe on/off/floor values so no two flashes read alike.

The module owns two lights, both built with the custom-light recipe above: an INFINITE flash
light overhead and a POINT light (computed ref-brightness attenuation) parked at the bolt
midpoint.

**Visible bolts** are opt-in via `lightning.bolt` (`LightningBoltMode`: off / augment /
replace). A bolt is a jagged top-to-ground polyline of depth-tested GX line segments on the
world XLU pass - occluded by stage geometry exactly like the rain - lit by the midpoint POINT
light. Geometry is regenerated per strike in **world space**, so every split-screen camera
draws the same bolt from its own pass: a 13-segment main channel from y=820 to y=-40 with
per-step horizontal jitter, plus one 4-segment fork off the upper third, anchored at a
uniform random XZ inside the stage OOB box. It draws in two passes - a wide dim glow in the
flash color and a thin white-hot core - both strobing on the shared flash envelope.
`augment` keeps the screen flash, `replace` draws only the bolt. Blood Rain, Storm and
Tornado all set `augment`.

The strike anchor needs the stage's OOB box, so before the scene is built a strike fires
the flash with no bolt geometry rather than falling back to a guessed position.

## Rain (`rain.c`)

A field of falling translucent line segments drawn *in the stage*, not as a screen overlay:
immediate-mode GX geometry on the world camera's pass, depth-tested, so buildings and terrain
occlude the drops behind them. The whole field is one
`GXBegin(GX_LINES, ..., density*2)` batch after `WeatherGX_BeginXlu`.

**Camera-following toroidal box.** The drops are a fixed pool (cap `RAIN_MAX_DROPS` = 1600)
of random offsets in a cube of edge `RAIN_BOX` (1000) that re-centers on the camera every
frame, so you can never outrun the rain. All drops share one `drift` vector advanced per
frame by the resolved velocity (dominant `-Y` fall plus the wind's X/Z); a drop's world
position is `eye + wrap(offset + drift) - RAIN_BOX/2` per axis, where the single-subtract
`wrap` recycles anything that leaves the box. The camera eye is derived from the view matrix
as a rigid inverse (`eye = -R^T * t`), so one pool serves every split-screen viewport with no
per-camera state. Rain, snow, hail and the celestial layers all use this same eye derivation.

Slant is not a `RainDef` field: `rain.c` reads the global wind vector every frame, so a
preset's `WindDef` drives both the rain's slant and the rest of the weather. Defaults are
pale blue-gray, 900 drops, 26 units/frame fall, line width 10, streak 1.5.

**Performance.** `rain.density` (clamped to the pool cap) is the number of GX line primitives
and the dominant cost lever; the batch is re-emitted once per camera, so 3-4P split-screen
multiplies it. If this immediate-mode path ever proves too expensive, the HSD point-particle
pool is the natural fallback - `psRenderParticles` (0x80433f00) already draws
velocity-stretched `GX_LINES` streaks (`Ptcl_EmitStreak`, 0x80436460) and `GX_POINTS` in
world space from decoupled tick/render walks. The trade-off is that the pool copies a static
`PtclDesc` template from the fixed ROM descriptor table (`descTable[type][sub]`, base
0x8058c708), with no clean runtime descriptor registration, so it is far less tunable from
mod code.

## Snow (`snow.c`)

Same camera-following box model as the rain (`SNOW_MAX` 1000 pool, `SNOW_BOX` 1000 edge,
shared wrapped drift), but each flake draws as a small camera-facing glow instead of a line
streak, and falls far slower (default 3 units/frame against rain's 26). Riders pass through
it. Only the first `density` flakes are drawn.

**Flutter** is the one thing snow adds on top of the shared drift: each flake owns a phase, an
angular speed and a unit horizontal sway direction, all seeded once, and gets a sideways
offset of `flutter * sin(time * freq + phase)` along that direction added to its X/Z in the GX
callback. A shared clock advances once per `Snow_Tick`. Flutter = None freezes the sway to a
straight fall.

A flake is a `GX_TRIANGLEFAN` with an opaque center and 6 transparent rim vertices,
billboarded from rows 0/1 of the world->view rotation, drawn with straight alpha blend
(`additive = 0`) so flakes read white over the world rather than glowing. Fog is left on, so
distant flakes tint toward the world fog. Per-flake size varies +/-0.5 about the resolved
base, seeded once. Defaults: soft white, 600 flakes, fall 3.0, flutter 1.6, radius 4.0.

Snowstorm authors it as snow; Volcanic reuses the same layer as soot-grey ashfall. The
**Snow** menu's Intensity and Fall Speed are latched by `Snow_SetActive` (so they apply on
the next preset change or CT re-entry) while Flutter and Wind Slant are read live.

## Wind (`wind.c`)

A single global horizontal vector several systems read each frame so they all blow the same
way: rain, snow and hail slant to it, clouds drift with it, trees lean to it, airborne City
Trial items are nudged (`Wind_ApplyToItems`), and gliding machines are pushed
(`Wind_ApplyToMachines`, airborne only, scaled by the machine's glide stat). The module owns
no GObj and installs no hook; it exposes `Wind_GetVector` for everyone else.

The vector is not static. Speed pulses (gustiness) and heading wanders (chaos), each a
smoothed random walk around the preset's base: a fresh target is rolled every period (40
frames for gust, 90 for heading) and eased toward each frame (0.04 / 0.02), with heading
deviation bounded by 75 degrees x chaos. `WindDef` defaults are 6.0 units/frame at heading 90
(0 = +Z, 90 = +X), gustiness 0.35, chaos 0.25. Coupling constants: 0.08 of the wind added to
an airborne item's velocity per frame; 0.012 at full glide for machines, with a 0.40 floor on
the glide-stat scale.

**The two physics pushes are held until the round timer starts** (`Weather_RoundProgress()` is
negative through the intro). Riders are still boarding then and read as airborne, so a push
would shove them off the start line, and the opening item drop would be blown across the city
before play begins. Precipitation slant, cloud drift and tree lean run throughout, so the
weather still looks alive during the countdown.

An item counts as airborne when `ItemData.is_airborne != 0` - the engine writes 0 at every
land transition and 1 (or -1, meaning airborne with the ground raycast suppressed) whenever it
leaves a surface. `ITEM_X35A_GROUNDED` is **not** usable for this: it latches the first time
the envcoll raycast finds ground beneath the item, which on a sky drop happens on its first
frame hundreds of units up, and it is never cleared afterwards.

## Hail (`hail.c`)

A damaging layer that rides on the rain (stone count is gated on `Rain_IsActive()`). While a
rain preset is active and hail is on, each machine carries a tight box of real world-space
hailstones falling under gravity plus the wind slant. Unlike a raindrop - camera-relative,
with no persistent world position - a hailstone is a true world point, so the hit is honest:
entering a machine's body sphere (radius 20, lifted 10 off the machine origin) deals
`Machine_GiveDamage(md, 1, mg)` and respawns the stone at the top of its box. A 10-frame
per-machine cooldown caps it to chip damage; a fully eroded machine dies through the engine's
own death path.

Hail holds off until the round timer starts, like the wind pushes and the puddle drag, so
nobody is chipped while riders are still boarding.

The stones fall in true world space (they do not slide with a moving machine) but the box that
re-seeds them follows the machine, so speed cannot outrun the storm. **Cover** can: a machine
with stage geometry overhead has its whole cloud suppressed. Shelter is found by casting a ray
*down* from the top of the playable volume (`stage_node->oob_max.Y` + 50) to just above the
machine - a down-cast detects a roof by its walkable top face, so it works regardless of
triangle sidedness. The probe is throttled to every 8 frames and cached per machine.

Box geometry is a 120-unit XZ half-extent, spawning 220 above the machine, recycling 90 below
or 300 horizontally away, falling 32/frame. Stones draw as short thick icy GX lines, depth
tested like the rain. `HailDef` is `enabled` plus `amount`, a density multiplier over
`HAIL_BASE_STONES` (20) capped at `HAIL_MAX_STONES` (32), where 1.0 is Normal. Only Hailstorm
authors it.

## Puddles (`puddle.c`)

A roaming field of shallow pools lying on the City Trial ground that drag machines driving
through them. Each pool slot runs an independent lifecycle - dormant, fade in (24 frames),
hold (300-900), fade out (36), gap (120-480) - with first appearances staggered over the
round's opening (up to 240 frames).

Each time a slot wakes it re-rolls a spot: up to 6 attempts raycasting straight down inside
`PUDDLE_PLAY_FRACTION` (0.72) of the OOB box, keeping only hits whose normal Y is at least
0.85 so pools never climb walls, then laying a translucent ellipse (short axis 0.55-1.0 of the
radius) flush in the surface plane via a tangent basis from the ground normal, lifted 2.0
along the normal to beat z-fighting. Discs are `GX_TRIANGLEFAN`s (22 rim segments, rim alpha
half the center alpha) on the world XLU pass.

Every frame, any grounded live machine whose XZ falls inside a pool ellipse has its horizontal
velocity damped by `1 - (1 - slow_factor) * menu_scale * pool_alpha`, capped at 0.99 - a
self-correcting drag that recovers on exit and bites less while a pool is forming or drying.
One pool's drag applies per frame even where pools overlap. Like the wind pushes, the drag
holds off until the round timer starts, so nobody is slowed on the start line.

With **Slowdown** off *and* **Show Puddles** off the layer neither draws nor drags, so the
whole lifecycle - including its ground raycasts - is skipped rather than run invisibly.

`PuddleDef` defaults are a bright reflective pool, 24 pools, radius 32, factor 0.90 (10%
velocity damp per frame), with the slot count capped at `PUDDLE_MAX` (64). The menu's
**Roaming** off pins the hold timer so the field stays put, and **Show Puddles** can hide the
discs while keeping the drag.

## Clouds (`clouds.c`)

A low deck of soft clouds drifting over the map. Each cloud is a cluster of 5 overlapping
flattened translucent spheroids - real world-space geometry (coarse UV spheres, 4 latitude
bands x 8 sectors, vertical squash 0.55), not billboards - so riders fly straight through a
cloud and vision inside one is heavily obscured. Puff 0 anchors the cluster core at full
radius; the rest scatter within 1.6 / 0.45 of the radius horizontally / vertically and shrink
by up to 0.85 (floored at 0.15) according to `puff_var`. `puff_var` is the puff-to-puff
spread within a cluster, distinct from `size_var` which spreads whole cloud sizes, so a cloud
reads as lumpy rather than as a stack of equal blobs.

The deck sits at either the preset's absolute `height` or 0.35 of the way up the OOB box, plus
the menu height offset, with a per-cloud spread. Clouds inherit the world fog, so a deck placed
above a preset's `fog_end` fogs out - keep it below the fog wall.

Drift is 0.30 of the wind's magnitude, floored at 0.45/frame and capped at 3.5; a calm preset
still drifts them slowly along a fixed 40-degree heading. A cloud reaching an OOB wall wraps
to the opposite wall, where a horizontal-clearance edge fade (260 units) holds it invisible so
re-rolling its shape and height is hidden, then it ghosts back in as it drifts inward.

Each spheroid is one `GX_TRIANGLESTRIP` per latitude band of flat-color, alpha-blended,
depth-tested-but-not-depth-writing geometry with `GX_CULL_NONE`, so it still fills the view
from the inside. A per-vertex silhouette alpha (`|normal . camera-forward|`, floored at 0.12)
feathers each spheroid toward a soft edge and hides most of the unsorted-translucency noise.
No texture asset. Defaults are 12 clouds, base puff radius 58 x a global 1.2 scale, size_var
0.35, puff_var 0.6, height_var 90, capped at `CLOUD_MAX` (30).

## Moon (`moon.c`)

A distant fog-free disc fixed on the City Trial sky dome that crosses the sky over the round,
shows craters and one of the eight canonical lunar phases, and can cast a directional
moonlight that makes it the scene's dominant light.

**Placement - camera-anchored, dome-clamped.** Each frame the moon sits at
`P = eye + skydir * dist`. Anchoring to the eye gives a consistent apparent elevation and no
parallax swim. `dist` is the minimum of three limits: `MOON_MAX_DIST` (1800, the desired
anchor distance); `MOON_DOME_FRAC` (0.82) x the eye-to-dome distance along `skydir`; and
`MOON_FAR_FRAC` (0.85) x the camera far plane, so it is never frustum-clipped. The City Trial
backdrop is a depth-writing sphere at the world origin of modelled radius `MOON_DOME_R`
(2500); a moon beyond it would be occluded and pop out as the camera nears the edge, so the
ray-march `t_dome = -e.d + sqrt((e.d)^2 + R^2 - |e|^2)` and the 0.82 fraction keep it inside
from every camera position. The disc radius scales with the final `dist` about `MOON_REF_DIST`
(1800), so apparent size is constant even when clamped. Terrain nearer than `dist` occludes
the moon through the normal depth test; the fog-free far sky and the dome (always farther) do
not. The disc is a camera-facing billboard from rows 0/1 of `COBJ.view_mtx`.

**Motion - synced to the match clock.** The moon crosses the sky once over the round, tied to
the City Trial match timer rather than a private counter. `grBoxGeneInfo` (`*stc_grBoxGeneInfo`,
`*(r13+0x610)`) exposes a pre-normalized `float match_progress` at +0x2a4 that the game
advances 0.0 -> 1.0 over the round and freezes during pause / match end;
`GRBOX_FLAG_MATCH_INTRO` marks the pre-round intro. The shared `Weather_RoundProgress()`
returns -1 there, which the moon clamps to 0 so it holds at its rise point. From progress `p`:

```
el  = arc_height_deg * sin(p * PI)      // 0 at the ends, peak at mid-round
az  = rise_bearing_deg + 180 * p        // rises at rise_bearing, sets opposite
dir = (cos el * sin az, sin el, cos el * cos az)
```

`arc_height` is the peak elevation in degrees (default 26, a low horizon-hugging arc);
`rise_bearing` defaults to 95, roughly east. When `dir.Y <= 0` the moon and its light are
skipped.

**Phase geometry.** The lit region is drawn as 28 horizontal scanline bands. For a band at
billboard height `v` the disc half-width is `w = sqrt(r^2 - v^2)`; the terminator is a
per-scanline ellipse of horizontal half-width `|k|*r`, `k` in `[-1, 1]`. Lit on camera-right
means `u` in `[-k*w, +w]`, lit on camera-left means `u` in `[-w, +k*w]` - the far edge is
always the disc rim, the near edge the terminator. `PhaseParams` maps each `MoonPhase` to its
`(k, side)`: Full `+1.0`, Waxing Crescent `-0.5` right, First Quarter `0.0` right, Waxing
Gibbous `+0.5` right, Waning Gibbous `+0.5` left, Last Quarter `0.0` left, Waning Crescent
`-0.5` left, New `-1.0` (not drawn). The lit side is camera-relative, matching a
player-selected phase rather than tracking a sun position. Each band is split into 6 columns
and emitted as a `GX_TRIANGLESTRIP`; empty scanlines (deep crescents) collapse to zero width
and are skipped.

**Soft rim and craters.** Each disc vertex's alpha is scaled by `MoonRimFade` - full inside
0.85 of the radius, falling linearly to 0 at the rim. Splitting each band into columns keeps
the fade localized to the rim and crescent tips instead of gradient-washing the lit face; the
terminator stays crisp because its interior vertices sit well inside the fade radius. Craters
are 13 darker translucent `GX_TRIANGLEFAN` patches seeded once over the inner disc
(`rad = sqrt(rand) x 0.62`) so they stay off the soft rim, each drawn only when `CraterFits`
confirms the whole circle is inside the opaque interior and on the lit side of the terminator -
so a crater never overhangs into the dark or off the disc edge.

**Fog-free draw.** At ~1800 units the disc is far past the fog wall (`fog_end` ~420-665), so
the GX callback brackets the draw with `HSD_FogSet(NULL)` ... `HSD_FogSet(live_fog)`, restoring
the live fog from `(*stc_grobj)->sky_gobj->hsd_object` so later geometry stays fogged. Depth is
kept (not forced to `GX_ALWAYS`) so terrain occludes the moon; visibility against the sky comes
from the dome clamp, not from disabling depth.

**Moonlight and distant-light suppression.** With `moon.light` set, the module creates one
`LOBJ_INFINITE | LOBJ_DIFFUSE | LOBJ_SPECULAR` light, points it along the moon's sky direction
each frame the moon is up, and colors it from `light_color x Brightness`. It lights riders,
machines, items and the few DIFFUSE terrain materials - the bulk of the stage is unlit, so a
moonlit preset's darkness comes from its fog / ambient / screen-tint, not from the light.

To make the moon dominant it also removes the leftover distant stage light. City Trial has two
distant INFINITE lights; the primary (`*stc_main_light`) is already owned by the runtime's
terrain tint, so the moon module owns only the **secondary**:
`Moon_SuppressStageLight` walks `stc_lobj_hw_slot_table[0..7]` for the INFINITE light that is
neither the primary nor its own, caches its color, and zeroes it. Zeroing is durable because
`HSD_LObjSetCurrentAll` rebuilds slot assignment each frame but never rewrites `LOBJ.color`.
Splitting ownership this way keeps exactly one owner per light.
`Moon_RestoreStageLight` puts the cached color back when moonlight turns off or the preset
changes; on reset the handle is dropped without restoring, since the LOBJ went with the
scene. The slot table lags the think hook by a frame, so the secondary resolves lazily with
retry.

A **New** moon is not drawn, so it casts no moonlight and suppresses nothing either.

## Stars (`stars.c`)

A field of faint camera-anchored dots over the City Trial sky dome, drawn additively as soft
glows with per-star size and brightness variance, each shimmering on its own phase. Like the
moon, a star is a celestial billboard fixed on a world sky direction and clamped inside the
backdrop dome: each star owns a fixed unit direction seeded once uniformly over the sky cap
above 10 degrees elevation, and `dist` is the minimum of `STAR_MAX_DIST` (6000, always clamped
smaller), `STAR_DOME_FRAC` (0.9) x the eye-to-dome distance, and 0.9 x the camera far plane.
Each dot's world radius scales with its final `dist` about 1800, so apparent size is constant.
Panning sweeps across a world-fixed field with no parallax swim.

A star is a `GX_TRIANGLEFAN` with a bright center and 6 transparent rim vertices,
camera-facing, drawn after `WeatherGX_BeginXlu(cam, additive=1, 0)`. Additive blend means dots
glow, never darken the sky, and draw order against the other translucent layers is irrelevant.
The draw is bracketed with `HSD_FogSet(NULL)` / restore so distant dots are not washed to the
fog color.

**Twinkle.** `Star_Tick` advances a shared clock by 1 each frame. Each star has a random phase
and angular speed (0.05-0.14 rad/frame, roughly 0.75-2 s per cycle), so the field shimmers out
of sync; per frame its brightness is multiplied by `1 + tw * 0.7 * sin(time * speed + phase)`,
with the additive blend clamping the overshoot. Twinkle = None freezes the field.

**Field composition.** The field is scattered once per preset activation by `Star_Arm`, with no
stage dependency, so Density and Size Variance apply on the next preset change or CT re-entry
while Twinkle, Luminosity and Color are read live. Count is `density x menu factor` clamped to
`STAR_MAX` (220). `SeedStar` rolls a sky-cap direction (uniform via `z` in
`[sin(min_elev), 1]`), a size, a base brightness in 0.35-1.0 so some dots are dim, and a
twinkle phase/speed. A dot's additive alpha is `color.a x luminosity x star.bright x twinkle`,
clamped to 255 and skipped below 1. Defaults: 120 stars, twinkle 0.5, luminosity 1.0, size 5.5,
size_var 0.5.

**Shooting stars.** Meteors ride along with the starfield - same GX callback, same additive
fog-free draw - gated on the star feature being active and the effective cadence not being Off.
A pool of 4 holds concurrent meteors; `Shoot_Tick` (called from `Star_Tick`, so aging happens
once per frame rather than once per viewport) ages live ones and launches a new one when a
random lull timer expires, re-seeding it from the effective cadence (Rare 1200-3000, Occasional
600-1500, Frequent 240-600 frames). `StarDef.shoot` (`ShootFreq`: Default/Off/Rare/Occasional/
Frequent, Default = Occasional) is latched by `Star_SetActive`; the menu's **Preset** value
honors it, any other value forces a level.

A meteor is a great-circle arc: a start direction `d0` high in the sky (25-75 degrees
elevation) and a unit tangent `t` biased downward, `head(p) = d0*cos(arc*p) + t*sin(arc*p)` for
`p = age/life`, where `arc` (0.4-1.0 rad) is how far it crosses and `life` (26-46 frames) x the
Speed menu factor sets the pace. It draws as an 8-segment `GX_LINESTRIP` trail spanning 0.15 of
the arc behind the head, per-vertex alpha fading to transparent at the tail, plus a
`GX_TRIANGLEFAN` head glow, both additive with a 4-frame fade-in / 12-frame fade-out envelope.
The pool clears and the timer re-seeds on every preset change and CT teardown.

## Wind-bent trees (`tree.c`)

The global wind can lean the City Trial forest trees. Each forest tree (yakumono `desc_id` 34,
53 instances in CT) renders from its own `JOBJ_SKELETON` joint whose world matrix is rebuilt
from the joint SRT every frame by `JObj_SetupMtxSub`, so a small tilt written into that
joint's Euler rotation each frame is honored automatically - no user matrix, no dirty flag, no
vertex work. Only the visual model is touched; collision is never moved.

The tree joints are enumerated once per stage. `Tree_Enumerate` walks the stage's placed-instance
pool (`Gr_GetCollRecords`) and keeps records whose `yaku_gobj` owner is one of the tree-family
yakumono GObjs, gathered by walking the `GAMEPLINK_YAKUMONO` GObj list for `desc_id` 34. The
owner slot is matched by pointer only and never dereferenced (it is meaningless for non-break
instances), and each kept joint's authored base rotation is cached so the lean is always
relative to it.

`Tree_Tick` reads the wind vector, derives a lean angle (0.018 rad per world-unit of wind speed,
capped at 0.25 rad / ~14 degrees), and tips every intact trunk toward the downwind heading by
writing its joint `rot.X` / `rot.Z`; a calm wind leaves the trees at their base rotation. A
per-tree sinusoidal gust (0.22 amplitude, 0.09 rad/frame, 0.7 rad phase step between adjacent
trees) breaks the grove out of lockstep so it reads as wind through foliage rather than one
rigid block pivoting. A knocked-down tree is skipped via the `grScene_IsInstanceCollAll(record, 1)`
gate - its collision is retired and the break tail owns the joint from then on.

Trees carry no per-preset config; they are a global menu effect gated on the wind.

## Volcano (`volcano.c`)

The City Trial volcano erupts a set number of times over the round, each eruption throwing
volleys of themed copy-ability projectiles out of the crater on ballistic arcs. The projectiles
are real `GAMEPLINK_PROJECTILE` actors with live hitboxes, so an eruption genuinely threatens
riders, machines and boxes.

**Scheduling against the round.** `Volcano_Tick` reads `grBoxGeneInfo.match_progress` (0 -> 1
over the match, -1 during the intro), so "3 eruptions per game" means exactly 3. `SeedSchedule`
divides progress into `n` equal slices and rolls one start point inside each
(`(i + 0.15 + 0.70*rand) / n`), so eruptions spread out but never land on the same beat twice.
Changing the count mid-round re-plans from the current progress and skips entries already behind
it, so no eruption replays. An eruption holds for `duration` frames and fires `burst`
projectiles every `interval` frames.

**Launch geometry.** The crater mouth is `(-366.19, 114.97, -575.42)`, surveyed at the rim -
left-and-back of map center in the `+/-1300` X/Z play box. Shots start there jittered by 18 in
X/Z, each along a random upward cone ray: azimuth uniform over the circle, tilt rolled in
0.4-1.0 of `VOLC_MAX_TILT` (70 degrees) x the preset's `spread`. Speed is 5.5 x `power` x
`1 +/- 0.3`. Every shot also rolls a size uniformly over 0.5-3.5 into `desc.velocity_scale`
which, despite the name, is a size scale driving the model and the hitbox together - so a big
one is genuinely more dangerous. Size and speed roll independently, so a boulder is no slower
than a pebble.

Ballistic range is roughly `speed^2 / gravity` ~= 1380 units, which from that mouth reaches the
-X and -Z map edges and blankets a wide radius around the volcano without covering the far
corners. Speed and `VOLC_GRAVITY` are tuned as a pair - gravity scales with the square of the
speed, so changing how fast an arc plays out leaves where shots land alone. Flight is around 8
seconds at the steepest tilt, hence the generous `VOLC_LIFETIME` (1680) backstop. `desc.up` is
built as the unit vector perpendicular to the launch ray in the same vertical plane rather than
world up, because `Projectile_Create` crosses forward with up to build the orientation basis and
a near-vertical launch would make those parallel.

**Ownerless projectiles.** Volcano shots end up with `owner_gobj` NULL, which
`HitColl_CheckIfSamePlayer` reads as "never the same player" - so the volcano is excluded from
nobody and threatens everyone, with no damage misattributed. That constrains the usable kinds:
plain sword stars and plasma C/D dereference the owner inside `Projectile_Create` and also home,
and all three auras re-snap to the owner's hand bone every frame. The themes therefore draw from
plasma A/B, the two spread shots, bomb, sensor bomb, the charged sword star, and the Fire
ability's bullet. Bomb and sensor bomb are transitioned to their thrown state in the same call as
the spawn, before their hand-snapping state-0 slot can run.

Fire bullet is the one kind that cannot be created with a null owner - its `init` and
`post_init` read rider fields through it - so `LaunchOne` lends it the first live rider from
`stc_playerdata` for the duration of `Projectile_Create` and clears `proj->owner_gobj`
immediately after; nothing in its per-frame slots reads the owner again. It also seeds the fire
bullet's charge scratch, because the borrowed rider is never holding a charged Fire ability and
its `init` would otherwise cache a zero there - which the kind later turns into a zero-radius
hitbox and a zero-scale model on impact. `kind_scratch` word 0 (the hitbox-radius multiplier) gets 1.0,
vanilla's full-charge ceiling, and word 1 gets the shot's rolled size since the kind assigns
it to `cur_scale` on impact. Every launch guards on `proj_kind_data[kind] != NULL`, since
that table is empty until the first rider is created and `Projectile_Create` does not check
it.

**Arcs.** Nothing in the projectile pipeline applies gravity, and prio 0 zeroes the accel vector
every frame, so every shot gets `VolcanoGravity` installed on `proj->user_hook_0` - invoked at
the tail of prio 0, right after the zeroing and before prio 4 integrates. It goes on every kind,
not just the plasma and sword-star ones whose pre-physics slot is all `blr`: bomb, firecracker
and sensor bomb only ever *add* the stage air current to accel in their flying state, so a
hook-written value survives on them and all themes arc identically. Lifetime is overwritten on
every shot because the per-kind defaults are unusable here - plasma A/B expire in 6-9 frames, and
bomb and sensor bomb never expire at all. The Fire theme fires the Fire ability's bullet rather
than the firecracker for the same reason: the firecracker carries its own fuse in kind scratch
that bursts it mid-flight no matter what `lifetime` says, while fire bullet's state-0 `fn0` is a
stub and it flies until it hits something.

Authored on **Volcanic** (fire, 4 eruptions) and **Storm** (plasma, 2). `LaunchOne` borrows
the first live rider from `stc_playerdata` only for the fire-bullet kind, through the shared
`Weather_FindDonorRider()`.

## Tornado (`tornado.c`)

A funnel that wanders City Trial on a random path, drawing loose items, breakable props and
parked machines into an orbit around its core, dragging at riders who stray inside, and shaking
the camera of anyone nearby. Appearances are spread across the round by the match timer, the
same way volcano eruptions are.

**Two entry points, deliberately.** `Tornado_Tick` runs from the weather runtime (inside the
stage think, GObj proc priority 1) and decides *where the funnel is*: schedule, touchdown,
wander, model placement and target claiming. `Tornado_OnFrameEnd` runs from the mod's
`ModDesc.OnFrameEnd` hook and decides *what the funnel does to the world*: the orbit's position
writes, the rider push and the camera shake. The split is forced - item physics
(`CityItem_PhysicsThink`, priority 4), the item ground snap (priority 5),
`Machine_PhysicsThink` (priority 4) and `PlyCam_Think` (priority 13) all run after the
priority-1 weather tick, so a position written there is recomputed before render. `OnFrameEnd`
is the only place those overrides survive.

**Path.** The funnel roams a **disc** centered on the OOB box, its radius the shorter half-extent
x `TORN_PLAY_FRACTION` (0.68) - 884 units on City Trial's `+/-1300` box. Motion is
waypoint-driven: a waypoint is drawn uniformly over the disc's *area* (`r * sqrt(u)`, so the
middle of the city is as likely as the rim), the heading blends toward it by `TORN_STEER` each
frame, a small random rotation jitters the track, and a fresh waypoint is drawn on arrival. The
heading is carried as a **unit direction vector** rotated by `cosf`/`sinf`, never an angle plus
`atan2f` - the freestanding libm here has no `atan2f`. Nothing in the path reads the wind.

Waypoints are what keep the funnel off the rim. A heading walk that only turns when it *would*
leave the box grazes the boundary and then curves straight back out, so the funnel spends the
whole tornado sliding around the perimeter; steering toward interior waypoints makes it cross
the city instead. The radial clamp at the end of the wander only ever catches jitter nudging it
slightly past the rim. Base height follows the ground via `Raycast_Ground` each frame, falling
back to the previous height where the cast finds nothing.

**The funnel model is the Kirby inhale whirlwind (`Effect 0x3a982`), spawned detached.**
`Effect_SpawnSync` is called with a NULL parent and **anchor mode 1**, which takes a single
vararg: a post-spawn callback handed the spawn node. That mode skips the joint-attach path
entirely, so no follow proc is installed and the model root belongs to the mod. The `efgroup`
argument asserts on -1 and is borrowed from a live rider (`RiderData+0x440`) - the group the
inhale itself spawns into.

The model is authored **along its local +Z**: narrow mouth at the origin, flaring to radius 8.07
at `z = 9.95`. Mode 1 applies no orientation, so a raw spawn renders lying flat.
`TornadoPlaceModel` therefore writes the root's **world matrix** (`JObj+0x44`) with
`JOBJ_USER_DEFINED_MTX` set rather than its SRT: the matrix maps local +Z onto world +Y, scales
length to the funnel height and cross-section to `TORN_MODEL_WIDTH` of the capture reach
independently, and rolls the whole thing about its axis in the same sense as the orbit. Writing
the matrix keeps euler order out of it and overrides whatever the effect's own animation does to
the root joint.

Two more consequences of mode 1. The effect's one-shot init never runs, so looping is armed by
hand (`JObj_SetAllAOBJLoopByFlags(root, ALL_ANIM)`) and the tick calls `JObj_AnimAll(root)`
itself, because no proc advances a detached effect's animation. And the effect **must not be
destroyed by hand**: the spawn node still points at the GObj, and the engine's per-node kill
destroys it again when the group is retired - a double free that corrupts the GObj free list and
asserts out of an unrelated `GObj_AddUserData` seconds later. A lifting tornado hides the model
tree (`JObj_SetFlagsAll(root, JOBJ_HIDDEN)`) and the same model is reused for the rest of the
stage, re-validated against the p_link-16 bucket each frame and respawned if a group kill took
it.

**The funnel forms and ropes out** over `TORN_FADE_FRAMES` (120) at each end of its life instead
of popping. The ramp does two things. It **narrows the column** - the matrix's radial scale is
multiplied by the ramp (floored at `TORN_FADE_MIN_WIDTH` so the matrix never goes degenerate)
while the height stays put, so the funnel thins to a thread rather than blinking out. And it
**scales each material's authored opacity**: `MObjLoad` (0x803f9f04) allocates an `HSD_Material`
per MObj and memcpys the desc into it, so writing `MObj.mat->alpha` dims this funnel alone and
never a player's inhale whirlwind sharing the same model.

The width ramp is the load-bearing half. Material alpha reaches the screen only through the
rasterized alpha, and this model's TEV alpha stage is `alpha_a..d = ZERO/ZERO/ZERO/GX_CA_TEXA` -
it takes the texture's alpha and discards `RASA`, so the material write may never be visible.
Both are applied because the opacity write costs nothing when it is inert. Authored alphas are
captured with the rest of the one-time model setup, which is deferred to the first frame the root
JObj is reachable rather than done in the spawn callback - a root not yet attached there would
silently leave both the swirl unanimated and the fade with nothing to scale. The scaled values
are written *after* the frame's `JObj_AnimAll`, the only thing that would put them back. A
tornado shorter than two ramps never reaches full width.

**Orbit.** A claimed target's polar position is re-derived from its live world position every
frame (so anything nudged by another system self-corrects rather than drifting out of the swirl),
rotated about the axis, pulled inward by a fraction of the distance to its ring, and lifted.
Angular speed comes from a constant *tangential* speed (`TORN_TANGENT / r`, capped at
`TORN_SPIN_MAX`) so the outer edge does not whip round faster than the core.

Because that is a pure function of position, every target at the same radius would lap at the
same rate and converge on the same ring - one rigid spiral. So each claim also draws a fixed
**orbit variation**: a multiplier on angular speed (and on its cap, or everything resynchronizes
near the core), the ring radius it settles into as a multiple of the core radius, a multiplier on
climb rate, and a slow radial breathing with a random phase. The draw happens once at claim time
and is stored with the claim; re-rolling per frame would read as jitter rather than as different
debris.

| Target | Mechanism |
|---|---|
| Items | Claimed by `ItemData` pointer (boxes skipped via `item_category == 0`), position overridden, `vel` zeroed, `is_airborne = -1` and the grounded bit cleared so the ground snap stops fighting it. Re-validated against the item bucket each frame, so a collected item self-heals out of the claim set. |
| Yakumono | Claimed per **scene-instance record**, not per GObj (the break families are one GObj to N props). Collision is retired at claim time so nobody runs into an airborne prop, `JOBJ_USER_DEFINED_MTX` is set so the skeleton families stop rebuilding from their authored SRT, and the record's world matrix translation is orbited. After `TORN_YAKU_HOLD` (110) frames the prop is destroyed through its own family `coll_func`. |
| Unridden machines | Claimed by `MachineData` pointer (`rider_gobj == NULL`), position overridden with `accel` and `velocity` zeroed - `Machine_PhysicsThink` integrates both into `pos` every frame. |
| Ridden machines | **Pushed, never possessed.** An inward + tangential + upward acceleration is *added* to `MachineData.velocity` with linear falloff to nothing at the reach. Velocity is a persistent accumulator the movement controllers only read-modify-write and drag down, so an impulse decays naturally and a fast machine can drive or boost its way back out. |

Nothing takes damage - the tornado is purely kinetic.

**The prop break is synthesized**: a zeroed `CollData` with `radius` cranked to `1.0e9` (force
is `radius * impactSpeed^2`, so any prop's HP falls in one hit) and `pos_delta` set to
`-normalize(region normal) * 100`. The delta *must* point into the surface, because
`grScene_GetImpactSpeed` negates the projection onto the outward normal and clamps a
non-positive result to zero. The record's collision is re-armed just before the call so the
family tail's "still collidable?" guard passes, the target region's refine bit is cleared for the
duration so the geometry-refined path cannot rewrite the synthetic delta, and `coll.g` is left
NULL so the break is credited to nobody. On a successful break the weak families (coral, trees,
rocks) get `JOBJ_USER_DEFINED_MTX` cleared, since they never hide their dragged mesh inline and
would otherwise leave a whole tree frozen in mid-air. Weak-family debris still spawns at the
prop's baked ground spot rather than at altitude - that anchor node is per-family and is not
relocated here.

**Camera shake drives the engine's own per-view shake record** rather than patching the camera
solve. `CamData.eye_pos` / `interest_pos` are *not* in the render path (the param that reaches
the COBJ is `CamData.x14`, snapshotted into a stack local inside `PlyCam_Think`), so writing them
does nothing. The record hangs off `PlayerCamData.shake` (+0x74) as a typed `CamShakeRec`, and
`PlyCam_Think` applies it after building the camera basis, gated on `gate` being positive:

```
eye += right * eye_right * scale_right  +  up * eye_up * scale_up
```

Both scale fields initialise to 1.0, so writing `eye_right`/`eye_up` in world units and
raising `gate` is the whole mechanism - no code patch, and therefore no conflict with
`custom_events`, which already replaces the `CObj_SetEyePosition` call at 0x800b3900. The
pointer is sanity-checked as an aligned MEM1 address before anything is written through it,
distance is measured from the view's own aim point, and the gate is sticky so it is
explicitly lowered when the funnel lifts.

`TornadoDef` is `enabled`, `count` (touchdowns per round), `duration` (frames), `size` (funnel
scale, which also scales core radius, reach and height), `strength` (spin/pull/drag scalar) and
`speed` (wander speed). Authored on the **Tornado** preset with `count = 3`.

Two guards keep the funnel from degenerating. The resolved core radius is floored, because
`TornadoOrbit` divides by it when a target sits dead on the axis. And a spawn that never
adopts - the callback rejects the node - is retried on a cooldown rather than once per
frame, so a failing spawn cannot flood the effect group.

## Event sky suppression (`event_sky.c`)

A standalone toggle - not a per-preset layer and not driven from the runtime tick - that stops
City Trial siren events (Meteor, Fog, Dyna Blade, ...) from swapping the sky to their themed
preset and back mid-round, which is jarring when a custom preset is already running.

The engine routes every event sky change through two tiny global wrappers, so the module replaces
both at boot with `CODEPATCH_REPLACEFUNC`: `Sky_TransitionGlobal` (event start ->
`Sky_BeginTransition`) and `Sky_RestoreGlobal` (event end -> `Sky_ApplyStoredIndex`). The
**Event Sky Changes** menu toggle gates them: `On` (default) reimplements each wrapper
faithfully, `Off` early-outs both so the round's weather holds through every event. Suppressing
the sky change neuters the **Dense Fog** event specifically, whose visual is delivered entirely
by its sky preset; every other siren event has its own actors. Installed from `EventSky_OnBoot`,
called from `main.c`'s `OnBoot`. The only other caller of `Sky_TransitionGlobal` is the inert
debug preset-cycler.

## Settings menu

`main.c` registers the mod settings menu ("City Trial Sky") with 14 entries: **Weather Presets**
(`custom_weather.c`), **Backdrops** (`custom_backdrops.c`), then one submenu per effect module -
Rain (with a Hail sub-value from `hail.c`), Snow, Wind, Lightning, Puddles, Trees, Clouds, Moon,
Stars, Volcano, Tornado - and the single **Event Sky Changes** option. Each submenu lives in its
own module's source file and is named `<module>_menu`.

The menus augment the per-preset configs rather than replacing them. A knob that has a
per-preset field behind it leads with **Preset** (index 0), the pass-through value: a scaling
knob resolves it to 1.0x so the preset's authored value shows through unchanged, and a
categorical knob (Phase, Color, Arc, Projectiles) honors the preset's own field. The remaining
values are explicit overrides applied to whichever preset the round rolls, and the **On** value
on Moon / Stars / Volcano / Tornado forces the layer onto every preset. Lightning's
**Lightning Bolts** knob is the one that reads Auto / Off / Force: Force only upgrades bolt
geometry on a preset that already has lightning, it does not add lightning to one that has
none.

A knob with **no** per-preset field behind it is a plain Off / On instead, defaulted to the
behavior the module used to call "Preset": Wind Slant (rain and snow), Randomize Direction,
Affect Machines, Affect Items, Roaming, Show Puddles, Bend in Wind, Screen Shake and Event Sky
Changes. Every option that changes gameplay rather than appearance logs its new value through
an `on_change` callback.

Two entries are pool selectors rather than layer overrides. **Weather Presets** carries the
**Fog Distance** value (Preset = 1.0x, then 50-200% overrides of the global `HSD_Fog.scale`
multiplier), an Enable-All / Disable-All pair, and one plain Enabled/Disabled toggle per preset,
backing the `weather_enabled[WEATHER_TOTAL]` array that `CustomWeather_OverrideSky` filters its
random pick against. **Backdrops** carries the parallel Backdrop Distance scale plus a
per-backdrop enable set. Both pools share `Weather_PickEnabled`, which picks uniformly among
the enabled entries and reports "none enabled" so each caller can choose its own fallback.

All settings persist via hoshi's keyed menu-save, and `ModDesc.affects_gameplay` is set so
hoshi backs them up and restores them around a replay.
