# Star Light

The star light layer of `custom_weather`: one point light at the star on City Trial's star
pole, shadowed by the terrain, over a city darkened by the glow layer's ambient. It ships on the
Beacon preset and can be forced onto any preset from its menu. Like the machine glow it lands on
the map's own surfaces, baked-colour ones included, and buildings, hills and roofs cast shadows
away from the star onto the ground, walls and each other.

The light itself comes one of two ways, picked by the Star Shading option:

- **Normals** (the default): a GX hardware point light at the star, on normals the layer adds
  to the map's display lists. Each surface takes the light by its own angle to the star and
  its true distance, per vertex.
- **Light Map**: the light baked into the light map from above, per column, with the column's
  top surface standing in for every surface in it.

The shadows come from the same baked heights either way, and Star Shadows turns them off.

## Config

`StarLightDef` holds `color` (packed RGB; 0 = gold), `range` (world units the light reaches,
default 1800) and `intensity` (default 1.0). The darkness is not part of it: the map is scaled
by the preset's `GlowDef.ambient`, which holds even with the glow's pools off. Beacon uses
ambient (16, 16, 26), about 6% of the baked colour, and no pools.

With Normals the light at a vertex is `color x N.L x intensity / (1 + (d / d0)^2)`, `d0` a
3.5th of `range`, clamped at full colour: bright under the star, fading with no hard edge.
With Light Map it is `color x intensity x (1 - d^2/range^2)^2`, `d` the 3D distance from the
star, times a facing term: 0.35 for a surface turned away from the star, rising to 1 for one
facing it squarely.

## The star

The star pole is yakumono kind 29, created by `GrYakuStarPole_Create` (0x801043c8). Its one
collision record (`YakumonoData+0x130`) is the star itself: a static, unanimated joint whose
record `aabb_center` is the light's position, about (142, 145, -187) on City Trial. The pole
under it is plain terrain. One threshold hit busts the star for the rest of the round - its
collision is retired, the joint is set `JOBJ_HIDDEN`, and the GObj destroys itself once the
break effects end (`GrYakuBreakCoral_BrokenThink`, 0x80103db4). With Bust Puts It Out on (the
default) the light fades out when the joint goes hidden. The star joint is left out of the
light map's receivers, so it keeps its own baked look and reads as the light source.

## The GX light

Once per round, before the bake, every rigid POBJ of the terrain tree but the star's gets a
normal per vertex (`MapNormals_Add`). Its display list is rebuilt with a direct S8 normal after
each position: the area-weighted normal of the triangles sharing that position within 45
degrees of the vertex's own. Strips are split where they fold more than 45 degrees, so the two
faces of a building corner keep their own normals; a split starting on an odd triangle leads
with a repeated vertex, so the winding holds. Each normal is pre-scaled by its joint's world
scale, which the normal matrix divides back out, and each joint with normals gets
`JOBJ_LIGHTING` so the normal matrix is loaded for it. City Trial's model takes about 330 KB;
the rebuilt lists and descriptors are the round's, and the scratch is freed. If memory runs
out, the round falls back to Light Map.

The light is `GX_LIGHT7`, loaded per camera before the map draw with the star moved into eye
space; no HSD light uses that slot on City Trial. It reaches the map through COLOR1 alone: black
ambient, white material, `GX_DF_CLAMP` and `GX_AF_SPOT` with the angle terms off. COLOR0 keeps
the material's own colour.

## The bake

The layer bakes once per round, spread over the round's first frames, into a 128x128 grid over
the out-of-bounds box, the same grid as the glow's light map:

1. **Heights.** A `Raycast_Ground` straight down each column finds its top surface and that
   triangle's normal. Casts pass through moving triangles (the gondolas) and ground type 30, the
   invisible barrier ring.
2. **Shadows.** Each column's lit height is the lowest height the star reaches there: the
   column's own surface, or the steepest line from the star over every column between them,
   whichever is higher. The march steps one texel at a time from the star, so the whole pass
   is a heightfield horizon test, not a raycast per column.
3. **Compose.** One RGBA8 texture: RGB the light at each column's surface, alpha its lit
   height quantized to 256 steps over the range from the lowest surface to the star (or the
   highest surface), lowered by 2 steps against self-shadowing. Each texel takes the lower lit
   height of itself and the column one step toward the star, so a wall facing the star takes the
   ground in front of it rather than the roof behind.

A 256x4 I8 ramp (texel x holds x) turns a pixel's world height into the same 256 steps.

## The shadow test

Both textures are `TEX_COORD_SHADOW` TObjs on the glow layer's shadow list: the light map's
`mtx` maps world (x, z) to (s, t), the ramp's maps world y to s over the baked range. The glow
layer's replacement for `HSD_SetupTevStage` in the shadow loop gives the map draw four stages
after the material's own, with the material's colour in PREV:

| Stage | Texture | Computes |
|---|---|---|
| star | light map | C2 = COLOR1 (Normals) or texel RGB (Light Map) x fade konst, A2 = lit height (1 with shadows off) |
| ramp | height ramp | A2 = (pixel height > A2) ? 1 : 0, `GX_TEV_COMP_A8_GT` |
| glow | glow light map | C2 = glow texel + C2 x A2 |
| apply | none | PREV = PREV x C2 x 2 |

With shadows off the ramp stage is left out. So a pixel gets the star's light only above its column's lit height, and the glow's ambient
and pools everywhere. The fade selects among the TEV's fixed konst fractions (1/8 to 8/8), one
step every 3 frames, so it needs no konst colour register.

## Limits

- **Per vertex.** Under Normals, light and angle are interpolated across each triangle, so a
  long road triangle near the star takes its corners' light rather than the bright spot under
  the star. The four materials that already have hardware lighting keep their stage lights in
  COLOR0; a specular one would read its specular colour as the star's.

- **One height per column.** The test holds where a column's lit points are everything above
  one height. Ground under a bridge or overhang the star could see past is shadowed, and a
  thin wall with no floor on top casts nothing.
- **Resolution.** At about 20 world units a texel, shadow edges follow the bilinear
  interpolation of the lit heights, and anything under a texel wide casts nothing. Walls
  facing away from the star can take part of the light where a small roof's texel blends in.
- **Static.** The bake is once per round: a busted house keeps its shadow, and machines,
  riders, items and trees cast none. Riders, machines and items draw outside the map's
  callback, under the preset's own lighting.
- **Dolphin.** Its default texture cache hashes only a sparse sample of a texture's bytes, so
  each compose flips red's low bit in every texel; a recompose in the same buffer then never
  matches the last decode.
