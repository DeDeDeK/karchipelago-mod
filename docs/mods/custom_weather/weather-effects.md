# Weather Effects

The visual and physical weather layers `custom_weather` draws over City Trial: wind, rain, snow,
hail, puddles, clouds, swaying trees, lightning, the moon and the stars. Each is driven by the
mod's per-frame weather runtime, configured by one nested def in the active preset's
`CustomPresetDef` (a 0 field takes the layer's default), and tunable from its own settings
submenu. The volcano and the tornado, which spawn and move game objects, are not covered here.

## Shared building blocks

**World-space geometry, per camera.** Every drawn layer is immediate-mode GX geometry built in
world coordinates and emitted from a GX callback on the world camera's XLU pass: depth-tested so
stage geometry hides what is behind it, never depth-writing, unculled. Each camera's pass emits it
again, so split-screen works with no per-camera state, and the cost scales with the number of
players.

**Camera-following boxes.** Rain and snow keep a fixed pool of random offsets in a 1000-unit cube.
All drops share one drift vector advanced each frame by the fall and wind velocity, and a drop's
position is `eye + wrap(offset + drift) - 500` per axis, so the field re-centres on each camera
every frame and can never be outrun. The eye comes from the view matrix as a rigid inverse
(`eye = -R^T * t`).

**The sky dome.** City Trial's backdrop is a depth-writing sphere at the world origin, which the mod
takes to be 2500 units in radius - its size at the default Backdrop Distance of 125%. A celestial
layer sits at `P = eye + direction * dist`, with `dist` the smallest of its desired distance, a
fraction of the eye-to-dome distance along that direction, and a fraction of the camera's far plane,
so it is never hidden behind the dome or clipped. Its size scales with `dist` about a reference of
1800 units, so it looks the same size wherever the clamp lands. Celestial layers draw fog-free
(`HSD_FogSet(NULL)`, then restore the live fog) so distance does not wash them to the fog colour.

**Round progress.** `grBoxGeneInfo.match_progress` (+0x2a4) runs 0 to 1 over a City Trial round
and freezes through pauses. The mod reads it as -1 during the pre-round intro
(`GRBOX_FLAG_MATCH_INTRO`) and when there is no round. Everything that pushes, slows or damages
waits for it to go non-negative: riders count as airborne while boarding, so a push during the
intro would shove them off the start line.

## Wind

One global horizontal vector that the other layers read: rain, snow and hail slant with it,
clouds drift with it, trees lean with it, airborne items are blown and gliding machines pushed.
The wind owns no GObj and installs no hook.

Speed and heading are each a smoothed random walk around the preset's base: a fresh target every
40 frames (speed) or 90 frames (heading), eased toward at 0.04 or 0.02 per frame. Heading
deviation is bounded by 75 degrees times `chaos`; speed pulses by `gustiness`. Defaults are 6
units/frame at heading 90 (0 = +Z, 90 = +X), gustiness 0.35, chaos 0.25.

Once the round timer runs, each frame adds 8% of the wind to the velocity of every item whose
`ItemData.is_airborne` is nonzero, and 1.2% times a glide factor to every machine whose
`is_airborne` is 1. The glide factor lerps from 0.40 to 1.0 by the machine's glide stat ratio, so
a Winged Star catches far more wind than a Wheelie Bike and a negative stat never pushes upwind.
`ITEM_X35A_GROUNDED` is not an airborne test: it latches the first time the ground raycast finds
anything below the item, which for a sky drop is its first frame.

## Rain

Translucent line streaks drawn in the stage, not as a screen overlay, so buildings hide the rain
behind them. The field is one `GX_LINES` batch of up to 1600 drops; a drop's streak is its
per-frame velocity (fall plus wind) times `streak`. `density` is the dominant cost and is drawn
once per camera. Defaults: pale blue-grey, 900 drops, 26 units/frame, line width 10, streak 1.5.
The menu scales intensity and fall speed and can turn the wind slant off.

## Snow

The same camera-following box (1000-flake pool), but each flake is a small camera-facing
`GX_TRIANGLEFAN` with an opaque centre and transparent rim, alpha-blended so it reads white rather
than glowing, and fogged with the world. Flakes fall slowly (default 3 units/frame) and each
flutters sideways along its own seeded direction by `flutter * sin(time * freq + phase)`, with
per-flake size varying +/-50%. Defaults: soft white, 600 flakes, radius 4, flutter 1.6.
Volcanic reuses the layer as grey ashfall.

## Hail

Hail rides on rain: it falls only while the rain layer is active. Each machine gets its own tight
cloud of real world-space stones - 20 at Normal, scaled by `amount`, at most 32 - spawning 220 units
above it and up to 120 units to either side, falling 32 units/frame plus the wind slant, and
recycled once 90 units below or 300 units away. The box follows the machine but the stones do not,
so speed cannot outrun the storm. A stone that enters a 20-unit sphere 10 units above the machine's
origin calls `Machine_GiveDamage(md, 1, ...)` and respawns, with a 10-frame per-machine cooldown
capping it to chip damage; a machine worn to nothing dies through the engine's own path.

Cover works: every 8 frames each machine casts a ray down from just above the stage's
out-of-bounds ceiling to 20 units above itself, and a machine with geometry overhead has its whole
cloud suppressed. Casting downward finds a roof by its walkable top face, whatever way its
triangles are wound. Only Hailstorm ships hail; the menu's amount forces it onto any rain preset.

## Puddles

Shallow translucent pools on the ground that drag machines driving through. Each pool slot runs
its own cycle - dormant, fade in (24 frames), hold (300-900), fade out (36), gap (120-480) - with
first appearances spread over the round's opening. On waking it picks a new spot: up to six
downward raycasts inside 72% of the out-of-bounds box, keeping only ground whose normal Y is at
least 0.85, then lays an ellipse (short axis 0.55-1.0 of the radius) flat on that surface, lifted 2
units along the normal against z-fighting.

Once the round timer runs, a grounded machine inside a pool has its horizontal velocity scaled by `1
- (1 - slow_factor) * strength * alpha` each frame, where `alpha` is the pool's current opacity,
capped at a 99% cut - a drag that fades with the pool and releases on exit. Only one pool applies
per frame. Defaults: 24 pools, radius 32, factor 0.90, at most 64. The menu can scale or remove the
slowdown, stop the pools roaming, or hide them while keeping the drag; with both slowdown and
display off the layer does no work at all.

## Clouds

A low deck of fly-through clouds. Each cloud is five overlapping flattened spheres (4 latitude
bands by 8 sectors, squashed to 0.55 height) - real geometry, not billboards - so a rider flying
into one has the view swallowed. One puff anchors the core; the others scatter around it and
shrink by `puff_var`, while `size_var` varies whole clouds. A per-vertex silhouette alpha
(`|normal . view|`, floored at 0.12) feathers each sphere's edge and hides most of the unsorted
overlap.

The deck sits at the preset's `height`, or 35% of the way up the out-of-bounds box, plus the menu
offset and a per-cloud spread. Clouds are fogged like everything else, so a deck above the fog
wall disappears. They drift at 30% of the wind speed, floored at 0.45 and capped at 3.5
units/frame (a calm preset drifts them along a fixed 40-degree heading). A cloud that reaches an
out-of-bounds wall wraps to the opposite one, fading over the last 260 units so the reshuffle is
hidden. Defaults: 12 clouds, puff radius 58 times a global 1.2, at most 30.

## Trees

The wind leans City Trial's forest trees (yakumono kind `YAKUKIND_TREE`). Once per round the mod
collects the stage's placed-instance records owned by a tree yakumono GObj and caches each
joint's authored rotation. Each frame it tips every intact trunk downwind by writing that joint's
`rot.X` / `rot.Z`: 0.018 rad per unit of wind speed, capped at 0.25 rad, modulated +/-22% by a
per-tree sine so the grove does not move in lockstep. The engine rebuilds each tree's world matrix
from its joint every frame, so the write is all it takes. Only the model moves; collision stays.
A tree whose collision is retired (`grScene_IsInstanceCollAll(record, 1)` false) has been knocked
down and is left alone. Trees have no per-preset config.

## Lightning

Random lulls (180-420 frames by default, the first 30 frames into the preset) punctuated by a
brief strobing flash. Each strike rolls its own length, peak brightness and strobe pattern.

Stage geometry ignores hardware lights, so the flash on the city comes from fog: during a strike the
mod lerps `HSD_Fog.color` and the EFB clear colour toward the flash colour and pulls `HSD_Fog.start`
in so near geometry lights up too. `Sky_Update` rewrites both every frame, so nothing needs
restoring. An overhead INFINITE light flashes alongside for riders and machines, which do read
hardware lights. Both lights are built the standard way (`GObj_Create`, `LObj_LoadDesc`,
`GObj_AddObject`, `GObj_AddGXLink(LObj_GX)`).

`lightning.bolt` adds a visible bolt: a jagged world-space polyline from y = 820 to y = -40 (13
segments plus a 4-segment fork from the upper third) at a random point inside the out-of-bounds
box, drawn additively as a wide glow pass and a thin white core, strobing with the flash. A POINT
light at its midpoint lights nearby riders. `augment` keeps the fog flash; `replace` draws only the
bolt. Until the stage is built there is no box to aim at, so an early strike flashes without a
bolt. Blood Rain, Storm and Tornado use `augment`.

## Moon

A disc on the sky dome that crosses the sky once per round, shows one of eight phases, and can
light the scene. Its sky direction comes from round progress `p` (held at 0 through the intro):

```
el  = arc_height * sin(p * pi)        // 0 at the ends, peak mid-round
az  = rise_bearing + 180 * p          // sets opposite where it rose
dir = (cos el * sin az, sin el, cos el * cos az)
```

Defaults are a 26-degree peak and a 95-degree (roughly east) rise. Below the horizon it and its
light are skipped. It is anchored 1800 units out, clamped to 82% of the way to the dome and 85% of
the far plane.

The lit part is 28 horizontal bands of six columns each. At band height `v` the disc's half-width
is `w = sqrt(r^2 - v^2)` and the terminator an ellipse of half-width `|k| * w`; a phase maps to
`k` and a lit side (Full 1, gibbous 0.5, quarter 0, crescent -0.5, New not drawn), with the lit
side relative to the camera. Alpha fades to 0 over the outer 15% of the radius, and 13 darker
crater patches are drawn only where they fit wholly inside the lit interior.

With `moon.light` set, the mod adds an INFINITE light pointed along the moon's direction each
frame, coloured `light_color` times brightness. To make it the dominant light it zeroes the stage's
secondary INFINITE light (the first INFINITE in hardware slots 0-7 that is neither the primary
stage light nor its own), caching the colour and restoring it when moonlight ends. The primary is
left to the runtime's terrain tint, so each light has one owner. Zeroing holds because the engine
reassigns slots every frame but never rewrites a light's colour.

## Stars

Faint dots fixed on the sky dome, drawn additively as soft glows so they brighten and never
darken the sky. Each star has a fixed direction seeded uniformly over the sky above 10 degrees
elevation, a size and a base brightness (0.35-1.0), and is clamped to 90% of the way to the dome
and of the far plane. Twinkle multiplies brightness by `1 + 0.7 * twinkle * sin(t * speed + phase)`
with per-star speeds of 0.05-0.14 rad/frame. The field is rescattered on each preset change;
defaults are 120 stars, at most 220.

Shooting stars ride along in the same callback: a pool of four, each launched when a random lull
expires (Rare 1200-3000 frames, Occasional 600-1500, Frequent 240-600; `StarDef.shoot` picks the
cadence, Default meaning Occasional). A meteor follows a great-circle arc from a start 25-75
degrees up along a downward-biased tangent, crossing 0.4-1.0 rad in 26-46 frames, drawn as an
8-segment fading `GX_LINESTRIP` trail and a glowing head.
