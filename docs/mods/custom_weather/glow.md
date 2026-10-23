# Machine Glow

The machine glow layer of `custom_weather`: a pool of light on the ground under every player's
machine, over a city darkened to the preset's ambient. It ships on the Moonlight preset and can be
forced onto any preset from its menu. Both the darkness and the pools land on the map's own
surfaces - roads, buildings, the sea - per pixel, including the baked-colour surfaces that no
hardware light reaches.

## Config

`GlowDef` holds `ambient` (packed RGB the unlit city is scaled to, 255 = as baked; 0 = as baked;
it applies with the pools off too, under the star light),
`color` (packed RGB glow colour; 0 = each player's colour, per controller slot), `radius` (pool
radius in world units, default 180) and `intensity` (default 1.0). Moonlight uses ambient
(90, 100, 140), a moonlit blue at about 35-55% brightness, under its own screen tint.

A pool falls off as (1 - d^2/r^2)^2 from the machine's XZ. A downward `Raycast_Ground` from the
machine finds the ground below; the pool fades and widens as the machine climbs, gone at 300
units up, and a machine with no ground below casts none.

## The light map

The layer owns one 128x128 RGBA8 texture spanning the out-of-bounds box, about 20 world units a
texel on City Trial. Every frame it fills the texture with the ambient and adds each machine's
pool, clamped at full brightness. A texel scales the surface it lands on by twice its value, so
half intensity leaves the baked colour as it is, lower darkens and higher brightens to at most
double. Two buffers alternate, so the CPU never writes the one the GPU may still be reading, and
the edge texels stay ambient so clamped sampling past the box is ambient too.

The texture's alpha is never sampled - the shadow stage passes the previous alpha through - so
every fill writes a running count there. Dolphin's default texture cache hashes only a sparse
sample of a texture's bytes and reuses its decode while those match; without the count, a pool
moving between sampled texels would leave the stale light map on screen.

The texture reaches the map through the engine's projected-shadow path:

- **Receivers.** Once per round every material of the terrain tree (`GrObj.joint_table[0]`) gets
  `RENDER_SHADOW`, which makes `HSD_MObjSetup` append the global shadow TObj list to that material's
  chain. City Trial's breakable props live in the stage model by joint index, so the tree covers
  them too. Materials whose POBJs are envelope-skinned are skipped: the projection follows only
  `PNMTX0`, not their per-vertex matrices. So is the star pole's star joint, which keeps its own
  look as the star light's source.
- **Projection.** The layer's TObj has coord `TEX_COORD_SHADOW` with `TEX_LIGHTMAP_SHADOW`; its
  `mtx` maps world (x, z) to (s, t) over the box, and the engine concatenates the current camera's
  inverse view onto it, so the same map lines up in every view.
- **Stages.** `HSD_TObjSetupVolatileTev` (0x803f5e78) emits one stage per shadow TObj by filling
  a shared template's stage, coord and texmap and calling `HSD_SetupTevStage`. The layer
  replaces that call (0x803f5f50) at boot. Outside the map's draw it passes the template
  through; inside, it matches the TObj by texmap (HSD assigns each TObj's `id` per material
  before the loop) and loads its own stage: for the light map alone, PREV x texel x 2. The star
  light's TObjs get their own stages and one extra stage from `HSD_StateAssignTev`, which
  `HSD_SetupRenderModeWithCustomPE` counts when it sets the stage total. That function's last
  call, to `HSD_SetupChannelMode` at 0x803f82a4, is replaced too: under the star's GX light a
  receiver, unless it is specular, also gets COLOR1 lit by the star alone.
- **Scope.** The map GObj's GX callback is replaced with a wrapper around `Map_GX` (0x800d81e4)
  that puts the TObjs on the shadow list - the star light's last, so theirs lead it, since each
  add prepends - loads the star's GX light for the camera being drawn, and invalidates the
  texture cache, then takes the TObjs off once `Map_GX` returns.
  Only the map's draw sees the light map - riders, machines, items and the ending scene's own
  shadows are untouched - and with the layer off the flags are inert.

## Limits

The projection is straight down with no occlusion or surface angle: a pool runs down walls in
vertical streaks and lights the top and underside of a bridge alike. Fog is applied after the
light map, so pools fog out with distance like the surfaces under them. Riders, machines, items,
enemies and event actors draw outside the map's callback, so they keep the preset's own lighting.
