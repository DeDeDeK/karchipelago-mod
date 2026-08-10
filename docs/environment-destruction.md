# Environment Destruction

A City Trial mod that takes real chunks out of the environment. Quick spins,
projectiles, machine rams and charged dashes drive a convex solid into whatever
wall they ran into; the surface it cuts is re-cut into new triangles, the hole it
leaves is lined with new geometry, and the map collision is re-cut the same way
so riders can drive into the hole and along its inside. Everything is generated
live from the loaded stage - there are no pre-authored break states - and it
resets when the stage reloads.

The mod lives in `mods/environment_destruction/` and targets City Trial only: CT
is the free-roam city with actual buildings to knock holes in.

Build: `make package INCLUDE_MODS=environment_destruction` (add to the comma list
to build alongside other mods).

## The chunk

One impact is one convex carve volume, built by `EnvVolume_Build` in
`src/carve.c`: an n-sided solid driven into the surface along its axis, bounded
by `n_sides` side planes, a front cap just outside the surface, and - when the
shape is capped - a back cap at depth. Planes are stored with outward normals, so
a point is inside when it is on the negative side of all of them.

`Chunk Shape` picks between three:

| Shape | Sides | Back | Character |
|---|---|---|---|
| **Square bore** (default) | 4 | flat cap | constant square cross-section |
| **Square pyramid** | 4 | apex | tapers to a point |
| **Tri pyramid** | 3 | apex | tapers to a point, least regular |

The difference is not cosmetic. A **bore** holds the width it opened all the way
down, so a rider drives its full length and comes out of it going the way they
went in; where its back face is buried in material it is flat and square to the
axis, and a rider bottoming out against it drives the next chunk straight on. A
**pyramid** narrows to a point a rider cannot follow, and the sloped face they
wedge against has an off-axis normal, so the next chunk goes off at an angle and
successive ones fan out. That makes pyramids the shape for organic, wandering
cavities and the bore the shape for tunnelling.

A bore has a second advantage: its sides run parallel to the axis, so pushing the
front cap further out does not widen the mouth. `ENV_MOUTH_CLEARANCE` is 4 world
units, enough that a contact reported well off the surface still leaves the
surface inside the volume. A pyramid flares over that distance, so it gets the
shorter `ENV_TAPER_CLEARANCE` (2 units) - otherwise a surface carved in front of
the impact point loses a wider disc than the wall behind it and grows walls that
stand out of the building instead of running into it. Both are clamped to half
the mouth width so a small chunk does not get a cap out in front of itself.

The impact does not choose the direction; the wall does. The damage source hands
over the contact point and the wall's normal there, and the volume is driven
along the reverse of that normal. So a spin next to a building bites into the
building, and a spin in open ground does nothing at all.

`Chunk Size` sets the mouth half-width (6 / 12 / 20 / 32 / 50 world units) and
`Chunk Depth` sets the depth as a multiple of it (0.8x / 1.6x / 3.0x), scaled
further by the impact's force.

Those sizes are calibrated against the map. City Trial's collision carries about
14000 triangles, of which roughly 5900 are carveable walls, and their bounding
boxes span 13 units at the 25th percentile, 22 at the median and 96 at the 90th.
So `Medium` is about two median wall triangles wide and `Huge` is wider than 90%
of them - at that size a chunk stops cutting geometry and starts swallowing it,
which is what makes big settings read as a whole face vanishing rather than as a
hole. Triangle size does not set the size of the hole: the clip takes the exact
volume out of whatever triangle it lands on, however large.

The sizes are large next to the things that make chunks: a machine's collision
sphere is about 0.76 world units, so even `Small` opens a mouth several machine
widths across. Where a chunk is taken has nothing to do with that scale - it is
decided by a contact probe out of the machine's own position, a few units long.

### The basis

The mouth ring is built on a basis seeded from world up, so for a wall carve one
side face lies flat across the floor of the hole and one across its ceiling. That
matters twice over: the rider drives on the flat one, and both classify as ground
rather than as wall, which keeps a later chunk from cutting the floor out from
under them.

### Clipping

Both the visual mesh and the collision mesh go through the same routine,
`EnvVolume_CarveTri`. A triangle is peeled one plane at a time: the part outside
plane *i* (and inside planes 0..*i*-1) is a surviving piece, and whatever is
still inside after every plane is the removed chunk. The pieces are convex, the
union of them is exactly `triangle \ volume`, and the removed polygon is exactly
`triangle ∩ volume`.

Attributes ride along. Every clip vertex interpolates position, normal, texture
coordinates and colour from the source edge, so the surviving surface carries the
same texture, lit the same way, with no seam against the untouched part of the
wall.

The cheap reject in front of it, `EnvVolume_TriOutside`, counts a triangle
**lying on** a bounding plane as outside it, to `ENV_PLANE_EPS`. Such a triangle
encloses none of the volume, and the ones that actually lie there are the lining
an earlier chunk left behind: without the tolerance they read as cuttable, and
the carve retires each one and stands an identical copy on top of the next patch,
so a wall worked over repeatedly ends up with stacks of coplanar faces fighting
each other.

### The lining

The surface of the hole is built from the **volume**, once per chunk, by
`EnvVolume_Lining` - not from the triangles the chunk cut. A tube of quads runs
from the mouth ring down to the ring where the tube ends, plus a fan across that
end when the chunk is capped; on a pyramid the back ring collapses onto the apex,
so each quad degenerates into the single triangle from its mouth edge to the
point.

Every lining face lies exactly in one of the clip planes, which is also where the
cut rim of every surface the chunk went through lies. The two therefore meet with
nothing between them to see through, however coarsely the wall happened to be
tessellated and whichever way its triangles faced. The tube starts
`ENV_LINING_LIP` (1 unit) in front of the impact plane rather than on it, which
covers the case where the real surface stands slightly proud of where the contact
was reported.

A face's normal is the **inverse** of the plane it lies in. A plane normal points
out of the volume, and a rider inside the cavity has to be pushed the other way,
back into the material.

**A bore's tube stops at `ENV_LINING_DEPTH` (6 units) past the impact surface**,
at the ring `tail_t`, and its back cap - when it has one - is placed at that same
ring rather than at the volume's own back face. The cut still runs the full depth;
only the lining is bounded.

The reason is that the depth setting is a multiple of the mouth half-width and
runs to 3x, so a Medium chunk driven at `Through` is 36 units deep and a rammed
one twice that, against City Trial walls that are single planes with 3 to 13
units of thickness where they have any at all. Lining that whole length builds
the inside of something that is not there: an open box standing out in the
street, or - when the far end happened to land inside the building across the
road and the chunk read as buried - a closed one. Six units reads as the wall the
hole went through, which is all the tube was ever standing in for.

`tail_t` is stored as a fraction of the mouth-to-back span, not a distance, so it
survives the transform into a joint's local space where a world unit is not one
unit.

Pyramids are not bounded. A pyramid closes on itself at its apex, so whatever of
it lies past the wall is only ever seen as a dent from the outside; truncating it
would open the point and give the cavity a hole to see through.

Generated geometry is drawn with **neither cull bit set**. A lining is seen from
inside the cavity, which is the back of the face bounding it, and a wall thin
enough to be punched clean through leaves remnants that get looked at from
behind. Culling either side shows those as see-through holes.

Lining vertex normals are **unit length and point out of the cavity**. Lighting
takes a normal at face value, and a raw cross product is twice the face's area:
at chunk scale that drives the diffuse term past its clamp either way, so a face
would come out blown white or solid black depending only on which way its winding
happened to run.

`Crater Fill` picks how they are shaded:

- **Stretch texture** (default) - the lining goes into the same patch as the
  surviving surface and shares its material. `EnvVolume_Lining` reports, per
  vertex, the distance along two perpendicular in-plane axes of the face it
  belongs to; the emitter scales those by the texel density of the wall that was
  cut - texture units per world unit, measured off the longest edge of the
  removed polygon - so the wall's own texture runs into the hole at its own
  scale. Each face has its own frame, so the texture runs true across each one
  and only breaks at the creases between them.
- **Flat colour** - the lining goes into a second patch with a cloned, untextured
  material (`RENDER_TEXTURES` and `RENDER_VERTEX` cleared, `RENDER_CONSTANT` set)
  pointing at a grey `HSD_Material`, so exposed interiors read as fresh broken
  material.

### Breaking through

The back cap is what a rider bottoms out against, and what the next chunk is then
driven off. It only belongs there while the bore is still inside material:
capping one that came out the far side would stand a wall nothing draws in open
air at the end of the tunnel.

`EnvColl_BoreExits` decides. It walks the segment from the mouth to the back
face with the map's own raycast, restarting just past each hit, and classifies
every surface crossing by whether its normal looks back toward the mouth (an
entry) or the way the bore travels (an exit). The bore is still in material only
when the last crossing was an entry.

Two crossings do not count. A surface the mod generated is skipped outright: it
stands in a hole the mod already took out, so reading it as material lets the
back face of one chunk pass for the wall the next one is buried in. And crossing
nothing at all is a break-through, not a burial. City Trial is built from single
planes, not solids - a third of its walls have no collision of any kind within
sixty units behind them, and only a quarter have an opposing face within ten - so
a bore that meets no stage surface between its mouth and its back face has come
out of the wall it went into on the first bite. Treating that case as "still
inside" is what stands a capped chunk in mid-air.

The practical result in City Trial is that a back cap is rare: it appears when a
bore ends inside a *second* wall behind the first, and the common hole is an open
tube through one plane. A wall thicker than the bore is tunnelled in two bites
rather than one - the near face, then the far face struck from inside, which
`EnvColl_RayHit` already handles by flipping the normal to the way the probe was
travelling.

Both carves are told the same answer, so the visual and collision linings agree.
Pyramids never get a back face - they end in a point.

### Chunks out of open space

A generated surface is a real wall as far as every damage source is concerned -
`Carveable` accepts the mod's own triangles on purpose, so that a hole can be
widened or driven deeper. That leaves a way for the mod to feed on itself: a
rider who bores out the far side of a building goes on striking the back of the
last chunk and standing a new one in front of it, laying a line of free-standing
walls across open ground for as long as they keep driving.

`EnvColl_HasStageMaterial` closes it. Before anything is cut, the volume is
clipped against the collision array exactly as the carve itself clips it, and the
chunk is refused unless some triangle that is not a **lining face** loses a piece
to it. The impact still books its spot in the recent-chunk table, so a source
leaning on a generated wall is not re-testing the whole array every frame.

The distinction between the mod's two kinds of triangle is the whole of it, and
getting it wrong breaks the feature in one direction or the other:

| | what it is | material? |
|---|---|---|
| **remnant** | the surviving part of a stage triangle, in its plane, with its `kind`/`flags`/`state` | **yes** - it *is* the wall |
| **lining** | a face the carve invented to cover a cut rim, standing where material was removed | no |

A wall cut once is made of remnants from then on - its original triangles are
retired and their surviving parts are pool triangles - so a test that asks
"did the mod make this?" answers yes for the entire wall after the first bite,
and every chunk after that is refused. A building would be carveable exactly
once. The test has to ask "is this a hole's lining?" instead. `EnvPool_Alloc`
takes the flag from `PendTri.lining` and `EnvPool_IsLining` reads it back; the
break-through walk classifies crossings by the same rule, since a remnant lies in
the plane of the wall it came out of and crossing it is crossing that wall.

This makes collision-backed geometry the requirement: a decorative mesh with no
collision behind it is not carveable, which is the right trade for a feature
about driving through things.

Tunnelling is then whatever falls out of that. Nothing aims a chunk at the last
one or remembers a tunnel; each bite removes the material in front of a rider and
leaves remnants where the wall still stands, so the next bite has something to
cut and the run of them goes where the rider went. In a stage built from planes a
tunnel through a building is a hole in the near wall, the room, and a hole in the
far wall - the far one struck from inside, which `EnvColl_RayHit` handles by
flipping the normal to the way the probe was travelling.

## Visual geometry

`grModelCity1` builds a JObj tree (317 JOBJ / 180 DOBJ / 208 POBJ, ~20,000
triangles). The tree is `JOBJ -> DOBJ -> POBJ`; a POBJ owns a vertex-descriptor
list and a display list, which is a raw GX primitive stream of
`[u8 opcode][u16 count][count x per-vertex record]` zero-padded to 32 bytes. The
opcode's high bits are the primitive and its low 3 bits the vertex format. City
Trial's terrain uses `POS = INDEX16/F32` and `TEX0 = INDEX16/F32` against one
shared 12,312-vertex pool, and some POBJs add a `GX_DIRECT` colour.

The whole blob is writable MEM1 (`Archive_Init` `0x8041e224` relocates it in
place) and the draw path re-reads it every frame
(`HSD_JObjDispAll 0x8040a7b8` -> `HSD_JObjDispDObj 0x8040f2e8` ->
`HSD_DObjDisp 0x803f4998` -> `HSD_PObjDisp 0x80407988` ->
`GXCallDisplayList 0x803d0384`), so editing the bytes edits the drawn frame.

### Finding the meshes a chunk reaches

`EnvGeom_BuildBounds` measures a world-space box for every stage JOBJ and POBJ
once, on the first gameplay frame, and `src/geometry.c` rejects both levels with
one sphere-versus-box test per carve. Without it every chunk decodes all ~20,000
terrain triangles to find the handful it cuts, which is most of a frame's budget
spent on meshes nowhere near the impact.

Bounds are read one vertex record at a time rather than one triangle at a time -
the primitive structure does not matter to a box - and the table is sorted by
pointer so lookups binary-search. It is built on a gameplay frame rather than at
stage load because it reads the joints' live matrices.

Generated geometry has no box of its own and is always tested, which is correct:
it is by definition at the impact. What its joint gets instead is a box grown by
the reach of every volume carved on it, so the joint stays reachable without its
new POBJs having to be measured.

### Retiring the source triangles

The walker converts the volume into each joint's local space once and tests every
triangle of the meshes that survived the box reject. Positions alone are read for
the reject test; only a triangle that survives it pays for its other attributes.

- **`GX_TRIANGLES`** - a carved triangle has its three position slots pointed at
  the first vertex. It becomes zero-area, the rasterizer drops it, and no other
  triangle in the batch shares the record.
- **Strips and quads** - neighbouring triangles share vertex records, so one
  cannot be retired alone. The run of triangles the volume reaches is re-issued
  into the patch mesh (carved ones as their clipped remnants, untouched ones in
  between copied over verbatim) and the primitive is **split in place** around
  it: the original header's count is shortened to the untouched prefix, and a
  fresh 3-byte header for the untouched suffix is stamped into the tail of a
  vertex record the carve is removing anyway. A strip's suffix always starts on
  an even vertex, because a strip restarts with even parity and a misaligned
  start would flip the winding of every triangle in it.
- **Fans** share their first vertex with every triangle they have, so they cannot
  be split; a touched fan is re-issued whole and its bytes zeroed.

Splitting is what keeps the arena in bounds. A re-issued triangle costs
`GX_DIRECT` records - roughly twenty times the bytes the indexed source used - so
copying an untouched strip that merely passes through a building is what spends
the arena, not the hole itself.

The walker **skips** zero bytes rather than stopping at them. A retired primitive
and the gap left by a split both sit in the middle of a display list, and
treating either as the end of the stream would make everything behind it
permanently uncarvable.

The edited display list is `DCFlushRange`'d before the frame ends;
`GXCallDisplayList` does not flush the CPU cache itself.

### The patch mesh

New triangles cannot go into the source POBJ - its display list has no slack and
its vertices are indices into a fixed pool - so `src/patch_mesh.c` builds a real
HSD mesh of its own, and hangs it off the **same joint** the geometry came from.
That means the generated vertices are in that joint's local space and inherit its
matrix for free, with no injected joint and no matrix ownership question.

Per carved DOBJ, `EnvPatch_Get` appends a new DOBJ at the tail of the joint's
DOBJ chain (the tail, so anything that pairs the chain with a parallel list still
lines up with the original run) carrying:

- **A cloned material.** `HSD_MObjAnim` (`0x803f9ebc`) steps `mobj->aobj` and
  then unconditionally walks `mobj->tobj`, and `JObj_AnimAll` reaches every DOBJ
  on a joint - so a patch that shared the stage's MObj pointer would run that
  material's texture animation twice a frame. The MObj (0x20 bytes) and its TObj
  chain are copied with their `aobj` nulled, sharing the image, TLUT and PE
  pointers. An animated source texture therefore keeps its own pace and the patch
  holds the frame it was cut at.
- **Render bucket bits copied from the source DOBJ** (`flags` bits 1/2/3), so the
  patch draws in the same pass as the surface it came out of.
- **POBJs with an all-`GX_DIRECT` vertex format** mirroring the source's
  attribute set (`POS`, plus `NRM`/`CLR0`/`TEX0` when the source has them), in
  GX's canonical record order. `HSD_PObjDisp` excludes `GX_DIRECT` entries from
  its `GXSetArray` loop but still runs `GXSetVtxDesc`/`GXSetVtxAttrFmt` on them,
  always into `GX_VTXFMT0`, so a fully-direct POBJ needs no reachable vertex
  array at all. Cull flags are cleared; the shape-anim and envelope bits stay
  clear too so it takes the plain rigid path.

Each POBJ holds one growing `GX_TRIANGLES` primitive in a 2 KB display-list
chunk. Appending rewrites the count and bumps `n_display`; when a chunk fills, a
fresh POBJ is chained onto the DOBJ rather than moving a buffer the GP might be
reading. Everything - patch records, HSD objects, cloned materials and display
lists - comes out of one 512 KB arena reserved by `HSD_MemAlloc` in `OnBoot`
(allocations made there persist for the whole runtime; anywhere else they last
only the current scene). A stage load rewinds it wholesale. Chunks are kept small
because a patch that ends up holding three triangles still costs a whole one.

### Carving generated geometry

Geometry an earlier chunk generated is carved again by later ones, **in place**:
its remnants go back into the same patch. That is what lets a tunnel be driven
deeper than once without the mod growing a chain of patches, one per bite.

The hazard is a walker reading back its own output - re-cutting fresh geometry,
and misparsing vertex records as primitive headers once it runs past the end the
display list had on entry. `EnvPatch_WalkLimit` reports where the chunk being
appended to stood before the carve started, and the walker stops there. Only the
tail chunk of a patch can receive triangles; a closed one already carries its
final `n_display` and reads as NOPs past its data. New chunks chained on during a
carve are not walked either, because the POBJ and DOBJ loops both stop at the
chain end as it stood on entry.

Remnants below `ENV_GEOM_SLIVER_AREA` (0.25 square world units) are dropped. A
spot carved over and over would otherwise accumulate fragments without bound, and
each one still costs a whole triangle's worth of arena while covering no pixels.
The collision side has the same rule at a much larger threshold, because a gap
there is one a machine drives through rather than one nobody sees.

### Never destroying more than is replaced

Retiring a source triangle and then failing to emit its replacement deletes
geometry outright, and for a strip that means the whole primitive disappears -
which is what a wall losing a building-sized piece looks like. So destruction
always trails emission:

- `EnvPatch_Mark` snapshots every patch's write position before a source
  primitive is touched, and `EnvPatch_Rollback` rewinds them if any triangle
  failed to land. Rollback re-zeroes the tail of the chunk past the restored
  write position, because `n_display` rounds up to a whole 32-byte line and the
  command processor reads the whole line. Only then is the source rewritten.
- A re-issued run is priced first (`EnvPatch_CanFit`), so a long one is skipped
  outright rather than half-copied and unwound.
- The reject test only rules out triangles wholly outside a single plane, so a
  primitive can pass it and still lose nothing to the clip. If the emit pass
  carves nothing, the re-issue is rolled back and the original bytes are left
  alone - otherwise the arena would be spent on an identical copy.

The lining is emitted after the whole tree has been walked, onto the joint of the
first mesh the chunk actually cut, and is not priced against the arena. If the
arena is exhausted at that point the hole is cut but unlined.

## Collision

The map collision is `GrObj.coll` (`GrObj+0x54`), one `GrCollVtx` pool and one
`GrCollTri` array shared by baked terrain and every placed prop, with each prop
owning a contiguous slice through its `GrCollRecord`. Vertices are baked to world
space at load and triangles hold raw `Vec3 *` into the pool, so no transform is
needed to read them and replacement triangles can point anywhere writable.

### Where the mod's triangles come from

The arrays are not the mod's to replace. `grColl_Alloc` (0x800d6dcc) makes
**nine separate `HSD_MemAlloc` calls**, one per array, each sized exactly
`count * stride` out of the capacity mirror `GrObj.coll_max` (`GrObj+0x0C`), and
`grColl_Free` (0x800d7060) releases them the same way, field by field. Pointing
`coll.tri` at a mod-owned buffer therefore leaks the stage's original block - a
megabyte a stage, which exhausts the HSD heap on the second City Trial - and
hands a foreign pointer to `OSFreeToHeap` at teardown.

So the mod does not allocate. It hooks `grColl_Alloc` at **0x800d6f74**, the
instruction after the zone-count assert and before the first allocation, where
`r29` is `&GrObj.coll_max`, and adds its own room to `tri_num`, `vtx_num` and
`moving_record_num`. Every size is read back out of `coll_max` below that point,
so the game allocates the extra, owns it, and frees it. The fill pass leaves
whatever it does not use as slack at the tail of each array; the mod takes the
**bottom** of that slack and raises `coll.tri_num` / `coll.vtx_num` over it. The
stage's own headroom ends up above the pool, still there for a prop attaching
mid-match, which bump-allocates upward from `coll.tri_num`.

Being *inside* the live count is not optional. `PointCollision_EnsureIDValid`
(0x800d1838) rejects any triangle id outside `[0, coll.tri_num)`, and around
twenty-five ground, landing and shadow consumers run every id through it -
`zz_8019a660_` (rider ground snap), `Machine_ShadowThink`, `CityItem_FindGroundBelow`,
`EventActor_GroundSnap` among them. A pool sitting above the count would still
stop a rider as a wall, because the wall path does not validate, but a crater
floor would never register as a surface to stand on. Nothing else in the system
range-checks: `grGetGroundTypeFromTriangleID`, `PointCollision_GetNormalByID`,
`grColl_GetTriVtx` and the contact inserter all compute `coll.tri + id * 0x40`
on whatever they are handed, and ids are cached at full 32-bit width, so a
stray id reads out of bounds rather than being caught.

Reaching those triangles is the other half. Every query runs **two passes**: a
brute-force walk of `GrCollParam.moving_record` - each record AABB-tested, and
only on overlap is its triangle slice scanned - and then a walk of the KD-tree
baked into the stage archive (`GrObj+0x700`, from `GrData+0x48`). The tree can
only ever emit the 16-bit indices it was built with, so an appended triangle is
invisible to it. The moving pass is not: it re-reads `moving_record` and
`moving_record_num` fresh every time. Both paths a rider needs run it - the
wall/floor pushback sweep `mpColl_SweptSphereMapColl` (0x802454f8, reached from
`mpColl_UpdateCollision`) unconditionally, and `Raycast_Do` (0x800d9958)
unconditionally. `grColl_SweptSphereQuery` gates its moving pass on an argument,
and `mpColl_SphereSceneObjColl` passes 0 there because prop regions have their
own list.

The per-frame rebake that re-transforms moving geometry from its joint would
overwrite hand-written vertices, but it does not read that array. It reads the
terrain *window* at `GrObj+0x9C` (and each prop's own at `yaku_data+0x1C`). The
windows share the same allocation as `GrObj.coll` but their counts are separate
words - so **a record past `coll_terrain.moving_record_num` is queried by
everything and rebaked by nothing.** That is what makes mod-owned collision
possible.

`src/coll_pool.c` appends 32 records to the query list, one per group, each with
its own box, so a carve's output lands in one tight box that every unrelated
query rejects on three float compares. A synthetic record needs `world` and
`prev_inv` set to identity (the swept-sphere query brings the collider through
both), `desc_kind != 3` and `yaku_gobj` NULL to stay out of the prop break
dispatch, and its triangles need `flags` 0 so they miss both the rough sweep and
the moving classification.

If the reservation does not happen - the hook gates on City Trial - `Install`
finds only the stage's own slack, says so, and **no carving happens at all**.
Cutting a hole with no budget to line it is worse than not cutting one.

### Classification

`GrCollTri.kind` bits 0..2 are the baked surface category - 1 `GrCFK_Under`
(ground), 2 `GrCFK_Wall`, 4 `GrCFK_Top` (ceiling). Every query ANDs its own mask
against them in `grColl_SweptSphereVsTri` and `grColl_RayVsTri` before it looks
at anything else, and `mpColl_UpdateCollision` files its contacts into the
under / wall / top lists by the same bits. It is not a runtime normal test, so a
surface built at runtime has to set them to match its own facing or the consumers
it belongs to will never look at it.

- **A remnant** lies in the plane of the triangle it came out of and inherits its
  classification whole - `kind`, `flags` and `state` alike - so a replacement is
  indistinguishable from the wall it stands in for.
- **A lining face** does not. It is a surface the carve invented, and the wall it
  borrows its ground type from faces a different way, so its category bits are
  derived from its own normal by `EnvCategoryForNormal`, at the same threshold
  the carve uses to decide what counts as floor. Leaving it the wall's category
  is what would make the floor of the hole a wall - a surface every ground query
  filters out, so nothing would ever stand on it.

Only the bits that claim membership of another array are dropped in both cases:
the rough-prism entries and the moving rebake each hold their own list, and a
triangle in neither must not advertise itself as being in one. A live City Trial
triangle reads `state` 0x60 and most often `flags` 0x00008000, and nothing in the
stage carries `GRCOLL_STATE_COLLIDABLE` without `GRCOLL_STATE_SURFACE_PARAM`
beside it - building a replacement out of zeroed fields produces a triangle
unlike any the engine made.

The normal matters as much. `grColl_SweptSphereVsTri` uses `GrCollTri.normal`
directly as the plane normal, so it has to be unit length; lining normals come
off the volume's own planes and are already normalized.

### The mod's own triangles are carveable

A carve retires the stage triangles it went through and stands its own in their
place, so the surface at a carved spot is made of pool triangles from then on.
They are candidates like any other. Sparing them would mean a second chunk taken
at the same wall finds nothing left to remove - the hole could never be widened
or driven deeper, and a source leaning on one would go on hunting for a stage
triangle that is no longer there. The floor of a bore is the exception, and only
incidentally: it classifies as ground, and the carve never cuts ground.

What they do not get is a restore slot. A stage cut is banked so its group can
hand the wall back when the ring recycles it; one of the mod's own is reclaimed
wholesale with its own group, and setting the live bit on a slot that has since
been zeroed would leave the narrowphase reading NULL vertex pointers. Cutting one
therefore costs nothing against `ENV_POOL_CUTS`, which is what keeps a chunk
taken out of an old crater from spending its whole cut budget before it reaches
the stage wall behind.

### Groups are a ring

Slots are handed out as a ring, one group per carve. Reaching the end wraps to
the front and recycles the oldest groups in the way. Recycling **restores** the
stage triangles that group's carve retired, so an old crater goes back to solid
wall rather than becoming a hole with nothing standing in it. That caps how much
of the map can be open at once and never leaves collision missing; a wall you can
see through but not drive through is the acceptable direction, the reverse is
not. `ENV_POOL_CUTS` is the per-group cap on retired triangles, and it is a hard
limit on a carve because every cut must stay restorable.

Slots freed by cutting are *not* reused in place. The baked KD-tree only ever
offers a slot where its original triangle stood, so a replacement written there
is reachable from nowhere else and silently does nothing.

### Slots are the budget

Cutting one triangle frees one slot. Its surviving parts want most of a dozen.
Retiring it anyway is what turns a doorway-sized hole into a wall a rider drives
straight through, so **a triangle is cut only when every remnant it owes has a
slot to live in**, and a wall that cannot be afforded keeps its collision and
becomes a chunk the rider can see but not drive into. That is the safe failure;
the reverse is not.

Two things make the budget work out more often than that sounds:

- **A cut is priced by piece size, not piece count.** Refusing a cut because one
  rim sliver has nowhere to go would mean coarse walls never carve at all, so a
  remnant below `ENV_COLL_SLIVER_AREA` may be dropped. The threshold is an
  absolute area and not a fraction of the triangle, because a building face is a
  single enormous triangle and a tenth of one is a gap a machine drives straight
  through.
- **Small triangles are free.** A triangle the chunk swallows outright owes no
  remnants at all, so it costs nothing but a restore slot. On City Trial's walls
  a mid-sized chunk swallows several of these for every large one it straddles.
  They are taken first, before anything else is priced.

The lining's own faces are not optional, so their slots come off the budget
before any cut is priced against what is left, and the remnant placement pass is
floored at that reserve. Pricing alone would not hold it: remnants too small to
have been *required* are queued all the same, and without a floor they would fill
the group and leave the hole with no inside.

A candidate whose remnants cannot all be queued is dropped rather than priced
short. An uncounted remnant would make the cut look cheaper than it is, which is
how a wall loses its collision without getting it back.

`EnvColl_CarveVolume` runs in that order: gather and price every candidate
(`GatherCandidates`), decide which are affordable nearest-first
(`ChooseCandidates`), retire only those, then place remnants and then the lining.
Placement writes vertices into the pool's own slice of the stage vertex array
with `prev == pos`, since nothing rebakes them, recomputes the AABB, and folds it
into the group's record box.

Lining faces are ranked so the floor of the hole survives a short placement ahead
of its walls and its back face: a bore missing its sides is still drivable, one
missing its floor is a pit.

`Debug Log` reports `coll cut N skip M placed P pool F` per impact: `cut` counts
stage and pool triangles alike, `skip` is walls left solid for want of slots, and
`pool` is how many of the mod's triangles are unspent. A `skip` that stays high is
the per-carve budget binding (`ENV_POOL_PER_GROUP` or `ENV_POOL_CUTS`), not the
pool running dry.

## Floor protection

The impact sits on a rider standing on the ground, so both carve paths skip
near-horizontal surfaces and only cut near-vertical ones. A surface counts as
floor when its normal is within ~60 degrees of vertical (`|ny|/|n| >= cos 60`,
`EnvIsHorizontalSurface`, threshold `ENV_FLOOR_COS2`); the test is
magnitude-independent so an unnormalized normal still classifies.

- **Collision** reads the triangle's own outward normal.
- **Visual** computes the world-space geometric normal from the triangle's three
  vertices and tests `|ny|`, sparing floors and flat tops alike so the visual hole
  stays matched to the spared collision.

Raise the threshold toward 1.0 to protect only near-flat ground and cut steeper
faces; lower it to also protect slopes and ramps. It is a normal-based heuristic,
so flat building roofs and ledges are spared too.

## Damage sources

Sources are polled every frame from the frame-end hook (`EnvDamage_Poll` in
`damage_sources.c`), not patched into the collision path. A source carves only
what it demonstrably ran into: the trigger is a **contact probe**, a call into the
engine's own map raycast rather than a distance test against the triangle array.

The strongest evidence is the pushback record. `mpColl_UpdateCollision`
(`0x802485e0`) resolves a body against the map in up to ten substeps and files
each contact into the `mpCollInfo` at `CollData+0x44`, split into under / wall /
top by `GrCollTri.kind` bits 0..2. `mpCollInfo.wall_rec_num` non-zero means the
body was stopped by a wall this frame, and `wall_recs[i]->wall` names the triangle
and the world point it was stopped at. `Machine_GetWallContactNum` (`0x801cde84`)
is the game's own reader for the machine; riders have no wall equivalent, but the
same fields are there off `RiderData.coll_data (+0x670)`.

All of the records are offered, not just the first: a machine wedged into a
crater is stopped by its floor as much as by its side, and only one of those is
worth cutting. `EnvColl_WallAt` takes each triangle id, range checks it - nothing
on the engine's contact path does - and rejects a floor or an already-retired
triangle, orienting the normal back toward the body. A lining face is not
rejected; it is the surface of a hole the source already made, and cutting from
one is what takes that hole deeper.

The record is only filled while the pushback runs, so a source that made contact
between polls has nothing to report. The **contact probe** covers those frames:

`EnvColl_RayHit(from, dir, len, hit_out, nrm_out)` in `collision.c` wraps
`Raycast_Any` (`0x800d1a54` -> `Raycast_Do` `0x800d9958`), which returns the
nearest triangle id along the segment or `-1`.

**Which raycast wrapper is not a detail.** Four byte-identical wrappers sit over
`Raycast_Do`, differing only in the kind mask they pass, and that mask decides
which surfaces exist as far as the caller is concerned: `Raycast_Ground`
(0x800d1ac4) passes 1 and cannot return a wall at all, `Raycast_Wall`
(0x800d1b34) passes 2 and cannot see the floor in front of it. The probes use
`Raycast_Any` (mask 7) because they are looking for walls *and* because anything
the mod may not cut has to stop the probe rather than be seen through - carving
past geometry that is staying put would open a hole with a wall still standing in
front of it. A floor, a rough-prism triangle or a moving platform therefore
blocks the probe outright.

Two further properties of that path are what the trigger rests on:

- `grColl_RayVsTri` (`0x800d95dc`) gates on the same `GrCollTri.state` bits the
  swept-sphere query does - bit 6 set, bit 7 clear - so a triangle an earlier
  carve retired is never returned. A probe fired into an existing hole sees
  straight through it to whatever now stands at its surface.
- `Raycast_Do` runs the moving-record pass as well as the KD-tree, so the mod's
  own pool triangles do get hit - and are exactly what a probe fired inside an
  existing crater ought to find.

A wall struck from behind (cutting a building's far face from inside) has its
normal flipped so the chunk is driven the way the probe was travelling.

The contact point and wall normal the probe returns are handed straight to
`EnvDestruct_ApplyImpact(Vec3 *hit, Vec3 *nrm, float radius, float force)`, which
builds the volume and carves both meshes. `force` scales the depth, so a boosted
ram punches deeper than a spin.

| Source | Toggle | Signal | Contact | Force |
|---|---|---|---|---|
| Quick spin | Spin | `RiderData.state_idx (+0x1c)` == `0x2c` (Kirby, Dedede) or `0x2d` (Meta Knight, by `RiderData.kind +0x04`) | the machine's wall record, else four horizontal rays (+-X, +-Z) of `ENV_SPIN_REACH` | 1.0 |
| Machine ram | Machine Ram | approach speed >= `ENV_RAM_FRACTION` x `top_speed_current (+0x398)` | the machine's wall record, else one ray along its approach, length speed + `ENV_TRAVEL_MARGIN` | speed / top speed, capped at 2.0 |
| Charge / dash | Charge / Dash | `MachineData.charge_value (+0x78c) >= ENV_CHARGE_MIN` | the machine's wall record, else one ray along `MachineData.forward (+0x418)`, length `ENV_SPIN_REACH` | 1.0 + charge |
| Projectiles / items | Projectiles | any live entity on the `GAMEPLINK_PROJECTILE` / `GAMEPLINK_ITEM` GObj lists | one ray along its own velocity (`+0x94` / `+0xc4`), length speed + `ENV_TRAVEL_MARGIN` | 1.0 |

A quick spin bounces the machine off the wall rather than holding it there, which
is why the spin takes either signal: the record catches the frame the bounce is
resolved on, the probes catch the approach.

**A ram is judged on its approach speed, not the speed it has left.** The poll
runs at frame end, after `mpColl_UpdateCollision` has resolved the frame, so a
machine driven head-on into a building reads as stopped on exactly the frame its
contact record names the wall. The source keeps each machine's world displacement
(`MachineData.world_velocity +0x354`, `pos - prev_pos`, so collisions and slope
drag are already folded in) from the previous poll and gates on whichever of the
two is larger, then fires its travel ray along that one - a ray along what is
left of a stopped machine's motion points nowhere near what it hit.

A quick spin is performed from the machine, so the machine is what strikes the
wall and the rider only carries the state. A spin swings in every direction at
once, which is why it probes on axes rather than along a heading.

Riders and machines are scanned for the 5 player slots, human only; projectiles
and items for all owners.

Probe lengths are measured against the collision spheres the game gives these
entities: a machine's `CollData.radius` is about 0.76 world units, it settles
about 0.4 above the ground it rests on, and its ground top speed is near 1.1 units
a frame. So a few units of reach is contact.

Three limiters keep this from running away:

- **A per-frame carve budget** (`ENV_CARVES_PER_FRAME`), spent on chunks actually
  taken rather than on attempts - the probes are cheap map queries, the carve
  behind them is not. The player scan starts at a rotating offset so a rider
  holding a spin against a wall cannot starve everyone else.
- **A per-source cooldown** (`ENV_SOURCE_COOLDOWN` frames) after a source lands a
  chunk. The wall it cut is retired and no longer carveable, so this is not what
  stops it drilling one spot; it is what keeps a rider held against a building
  from spending the chunk pool and the mesh arena as fast as the carver can run.
- **A recent-chunk list** in `environment_destruction.c`: a carve is refused when
  its surface point is within `ENV_RECENT_SPAN` of one made in the last
  `ENV_RECENT_HOLD` frames. This only has to stop two sources landing on the same
  spot in the same instant. It stays short and tight - anything wider than a few
  units would forbid the second bite of a tunnel as well.

**Tunables**: `ENV_SPIN_REACH`, `ENV_TRAVEL_MARGIN`, `ENV_RAM_FRACTION`,
`ENV_CHARGE_MIN`, `ENV_SOURCE_COOLDOWN`, `ENV_CARVES_PER_FRAME` in
`damage_sources.c`; `ENV_RECENT_*` in `environment_destruction.c`. The tornado
copy ability enters its own action state and is not matched.

The probes reach a few units and a chunk is tens across, so a source standing at
the mouth of the hole it just made has nothing in range - every surface left is
the far side of the chunk. Taking the next bite means driving into the hole until
the machine is stopped against one of its faces, which is what the contact record
then names. In a bore that face is the flat back, and its normal is the drive
axis, so the tunnel goes on straight.

## Files

| File | Role |
|---|---|
| `src/main.c` | `ModDesc mod_desc` + settings menu (Enabled, Chunk Size, Chunk Depth, Chunk Shape, Crater Fill, Damage Sources, D-Pad self test, Debug Log) |
| `src/environment_destruction.c` | CT gate, shape table, `EnvDestruct_ApplyImpact`, break-through gate, recent-chunk limiter, lifecycle, D-Pad self test |
| `src/carve.c` / `.h` | the carve volume, world-to-joint-local transform, convex clipping, lining generation |
| `src/geometry.c` | stage bounds cache, tree walk, display-list parse, primitive splitting, source-triangle retirement, remnant and lining emission |
| `src/patch_mesh.c` / `.h` | runtime HSD DOBJ/POBJ builder, material cloning, display-list arena |
| `src/collision.c` | collision clip, cut budget, replacement triangle writeback, wall search, contact probe, break-through walk, stage-material test |
| `src/coll_pool.c` / `.h` | the mod's collision triangles: the `grColl_Alloc` reservation hook, the group ring, and the query-only broadphase records |
| `src/damage_sources.c` | per-frame contact probes for the spin / projectile / ram / charge sources, carve budget and per-source cooldown |

The self-test (`D Pad self test` on, D-Pad Up in CT gameplay) carves at whatever
wall is nearest player 1 with the selected size, depth and shape - a controllable
trigger for validating the pipeline independently of the damage sources, and the
only caller left of the brute-force `EnvColl_FindSurface` scan.

## Live verification plan

Validate in Dolphin in this order, with `Debug Log` on (it reports the chunk
position, its shape and whether it capped, the triangles generated, the collision
triangles placed, and arena use):

1. **A chunk appears.** Drive at a building wall with the self-test on and press
   D-Pad Up. Expect a hole with a lined interior, no flicker on the surrounding
   wall, and a non-zero triangle count.
2. **The surface stays whole.** The wall around the hole should keep its texture
   with no cracks or missing triangles - that is the clip's remnants landing and
   the primitive split leaving the untouched ends where they were.
3. **The floor is ground.** Drive into a `Square bore`. The machine should sit on
   the bore's floor with normal ground handling, not skid along it as if it were
   a wall - that is `EnvCategoryForNormal` giving lining faces the right category
   bits.
4. **Fill modes.** Toggle `Crater Fill`; the stretched-texture and flat-grey
   interiors should both light like the wall around them, and the stretched one
   should show the wall's texture at roughly its own scale rather than smeared.
5. **Collision matches.** The rider should enter, ride the inside faces, and be
   stopped by the wall around it - a wall that has gone pass-through well beyond
   the hole means source triangles are being retired without replacement. The
   log's `coll cut / skip / placed` quantifies it.
6. **Tunnelling stops at the far side.** With `Square bore` and `Chunk Depth` at
   `Through`, drive through a building and out the other side. Chunks must stop
   the moment there is no building left: the log should turn over to
   `No stage material ... skipped` and no geometry may appear in open ground.
   Most chunks should read `through`, not `capped`.
7. **Shapes.** Switch to the pyramids and confirm the cavity wanders instead, and
   that neither ever reports `capped`.
8. **Budgets.** Watch arena use in the log across a full match, and the
   `[EnvColl] Replacement pool full` warning. The bounds cache should report its
   mesh count once per stage.

Watch for: torn frames (edit timing against the draw), holes larger or smaller
than expected (`env_size_table`, `env_depth_table`), and materials that look
wrong in flat-colour mode (the `RENDER_CONSTANT` rewrite is the least certain
part).

## Open questions and future work

- **Old craters go solid.** Recycling a group restores the collision its carve
  retired but not the geometry, so after 32 carves the oldest crater is a hole
  you can still see and no longer drive into. For a mod about tunnelling that is
  the wrong way round; it wants either a pool big enough that the ring rarely
  wraps (`ENV_POOL_TRIS` / `ENV_POOL_GROUPS`, at 64 bytes a triangle plus 3 x 24
  bytes of vertices out of stage heap) or recycling by distance from the nearest
  player rather than by age.
- **Collision candidate search** is a linear scan of all ~14000 triangles per
  carve, AABB-rejected, and the stage-material test in front of it is a second
  pass over the same array. That is cheap next to what the geometry walk used to
  cost, but a coarse grid built at stage load would make both free.
- **Only the surface at the mouth gets lined.** The tube is one run from the lip
  to `tail_t`, so it lines the wall the chunk was driven into and nothing else. A
  deep chunk that reaches a second wall further along its axis cuts that wall too
  and leaves the hole in it bare, which is a hole you can see through in a
  building nobody touched. It wants the lining to become a set of axial segments
  of the same tube - one starting at each surface the volume actually cut, all
  of them lying in the same side planes, so they never overlap laterally - rather
  than a single run from the mouth. Until then, keeping `Chunk Depth` short
  enough that a chunk stays inside one building avoids it.
- **Depth is scaled off the mouth width, not off the world.** `env_depth_table`
  is `{0.8, 1.6, 3.0}` times the half-width and is then multiplied by an impact
  force that reaches 2.0, so a Medium chunk ranges from 10 to 72 units deep
  against walls measured at 3 to 13 units thick. The setting is really choosing
  how many buildings the cut passes through. Absolute depths in world units would
  say what they mean.
- **Damage-source refinement** - the charge source still gates on
  `charge_value` and probes along `MachineData.forward (+0x418)` rather than on
  anything the machine demonstrably struck, so it fires on a charged machine
  merely pointed at a wall.
- **Prop integration** - for triangles owned by a breakable prop's record, a
  partial carve leaves the prop's yakumono record thinking it is intact. A big
  enough hit could instead drive the vanilla break tail (`collideWithObject`
  `0x800f5004`), which retires collision, hides the mesh and spawns debris.
- **Debris** - chunks are removed, not thrown; spawning short-lived pieces from
  the removed polygons would sell the impact.
- **Restore** - carving is permanent until the stage reloads. A mid-match restore
  would need each edited display list snapshotted before its first edit.
