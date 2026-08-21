# Particle System

The HSD point-particle pool draws camera-facing textured sprites in bulk - machine exhaust, sparkle
dust, spark trails, impact puffs - emitted by **generators** and drawn by a dedicated render driver
that never touches the JObj display walk. The whole subsystem lives in one module, roughly
`0x80430000`-`0x80438000`.

It is a classic HSD design with three moving parts:

- a fixed pool of **256 `Particle` objects**, 148 bytes each,
- a dynamic pool of **generators** (`ptclGen`) that emit particles each frame from a static
  `PtclDesc` template,
- **32 render-group lists** (the bank head array at `0x8058cce8`) that a per-frame driver walks to
  draw every live particle.

Tick (physics and lifetime) and render are fully decoupled and run from separate walks. Both are
entered from the Effect module's update thunks (`0x80233b74` / `0x80233ba0`), which is the only
structural tie between this pool and the model-effect manager. A given visual can be built from point
particles, from a standalone model-effect GObj (its own `GOBJ` carrying a JObj tree), or from both;
the two render through completely separate paths, so the recolor levers below reach only assets that
actually carry a point-particle component. The inhale whirlwind, for instance, is a pure model effect
with no particles, and no write into this pool changes its color.

## The Particle node (148 bytes, 0x94)

The node size is fixed by the pool allocator. No struct is declared for it in
`externals/hoshi/include/particle.h`, because a particle and a generator alias the same 148 bytes
differently. The layout below is used by `Ptcl_Alloc` (`0x8043294c`), the per-particle color helper
`psDispParticles` (`0x80437168`), and the render driver plus quad emitters (`0x80433f00`,
`0x80436460`, `0x80436774`).

| Offset | Type | Field | Notes |
|--------|------|-------|-------|
| 0x00 | `Particle*` | `next` | intrusive list link (free-list and render group) |
| 0x04 | `u32` | `flags` | see the bit table below |
| 0x08 | `u8` | bank | which installed bank the texture comes from |
| 0x09 | `u8` | texgraphic | index into that bank's texgraphic array |
| 0x0a | `u8` | image | index into that texgraphic's image array |
| 0x0b | `u8` | TLUT | palette index for a CI format, `0xFF` for none |
| 0x0c | `f32` / `u16` | emit accumulator on a generator, ramp countdown on a particle | seeded from `PtclDesc.rate` |
| 0x0e | `u16` | mat-color anim period | interp denominator for start-to-end mat color |
| 0x12 | `u8[4]` | **`rgba_start`** | **start material color (recolor lever)** |
| 0x14 | `u16` | `base_life` | |
| 0x16 | `u8[4]` | secondary RGBA | the `0xd0` opcode's ramp source |
| 0x1a | `u16` | wait | frames left before the next opcode runs |
| 0x1e | `u16` | life variance | spawn aux |
| 0x20 | `u8*` | program | the descriptor's bytecode; also a stable scope key |
| 0x24 | `f32[3]` | `vel0` | initial velocity (spawn value) |
| 0x30 | `f32[3]` | `vel` | velocity x/y/z |
| 0x3c / 0x40 / 0x44 | `f32` | `grav_x` / `grav_y` / `grav_z` | gravity and drag |
| 0x48 | `f32` | paired with `+0x64` | from `PtclDesc+0x20` |
| 0x4c | `f32` | `spread` | emission cone half-angle in radians; `Ptcl_TickOne` ramps it toward `+0x60` |
| 0x50 | `f32` | `scale_curve` | init `1.0` |
| 0x5c | `u16` | secondary color-anim period | |
| 0x60 | `f32[2]` | ramp target for `+0x4c`, plus one emit-shape parameter | |
| 0x6c | `u16` | end mat-color period | interp denominator |
| 0x70 | `u8[4]` | **`rgba_end`** | **end material color (recolor lever)** |
| 0x74 | `u8[4]` | amb color end | |
| 0x7a | `u16` | ambient anim period | |
| 0x82 | `u8[2]` | `amb_start_rgb` | ambient start |
| 0x86 | `u8[2]` | `amb_end_rgb` | ambient end |
| 0x88 | `f32` | z-scale | FIFO depth term |
| 0x8c | `ptclGen*` | `parent_gen` | back-pointer to the emitting generator |
| 0x90 | `u16` / ptr | geom kind / model-desc | selects billboard vs custom-geometry emitter |

The color and interp cluster (`0x0e`, `0x10`, `0x5c`, `0x6c`, `0x7a` and the `0x7c`-`0x88` bytes) has
overlapping byte/halfword offsets. The reliable color fields are start RGBA `+0x12`, end RGBA `+0x70`,
and ambient `+0x82`/`+0x86`.

**Generators alias several of these offsets**, because they come out of the same 148-byte pool.
`Ptcl_Alloc` writes a generator's emit shape at `+0x16`, its bank at `+0x18`, its sub-index at `+0x19`
and its texgraphic index at `+0x1a`, where a spawned particle instead carries bank and texgraphic at
`+0x08`/`+0x09` (`Ptcl_Spawn` takes both as arguments from the emit tick). `Ptcl_TickOne` reads a
program counter as a `u16` at `+0x24`, over what is `vel0` on a spawned particle. Read a field against
the kind of node it belongs to.

### flags bits (Particle+0x04)

| Bit | Meaning |
|-----|---------|
| `0x01` | material-color animation active |
| `0x08` | rotated / billboard-corner quad |
| `0x80` | force white material (ambient forced to 255) |
| `0x100` | fog enable |
| `0x800` | tick: already-disposed / external, skip |
| `0x10000` | blend-mode select bit |
| `0x100000` | use simple-quad emitter |
| `0x800000` | descriptor requests lifetime-from-alpha |
| bits 25/26 | animated texture matrix |
| `0x40000000` / `0x80000000` | quad-vs-geometry / geometry-vs-line path selectors |

## The render-group bank array

The bank head array at `0x8058cce8` is **three parallel 32-entry word sub-arrays**, indexed by
`group << 2`. It is accessed only via split immediate (`lis 0x8059; addi -13080`), which is why a
pointer search finds no aligned reference to it.

| Sub-array | Address | Per-group contents |
|-----------|---------|--------------------|
| `+0x000` | `0x8058cce8` | owner/descriptor pointer + dirty flag |
| `+0x080` | `0x8058cd68` | live-list head |
| `+0x100` | `0x8058cde8` | live-list tail |

The render driver bounds-checks `group < 32`, and a bitmask argument `1 << group` gates which groups
draw. The group selector at `0x8043845c` lazily re-sorts a group's list (a radix split on the
`Particle+0x04` blend bits) when its owner word is marked dirty, then hands the head and tail to the
driver. Insertion is done by `Ptcl_AllocNode` (`0x80438238`) from `Ptcl_Spawn`: it pops a free node,
computes `0x8058cce8 + (group << 2)`, pushes the node at that group's head, and marks the dirty word
at `0x8058cd68 + (group << 2)`.

The helpers at `0x80437ddc` and `0x80437e18` are unrelated - they attach a particle to a parent/owner
via `Particle+0x90`/`+0x54` with a refcount at `+0x3e`, not to a bank group. A separate scratch block
at `0x8058cc08` holds the per-frame camera-facing billboard basis, not a list.

## Render path

`psRenderParticles` (`0x80433f00`) is the disp driver and the only function that walks `0x8058cce8`
and calls both the color helper and the quad emitters. Per frame it:

1. Builds a camera-facing billboard basis from the current CObj viewing matrix into scratch
   `0x8058cc08` (`HSD_CObjGetViewingMtx`, `PSMTXInverse`).
2. Loops the 32 groups, mask-gated, fetching each group's head and tail and walking the list via
   `Particle+0x00`.
3. Per particle, in order: channel-color setup (`GXSetChanMatColor` / `GXSetChanAmbColor`,
   `GXSetChanCtrl` with the light mask), blend mode from the 2-bit selector in `Particle+0x04`
   (`GXSetBlendMode`), alpha compare, TEV setup, texture binding from two 3-level pointer tables
   (`*0x8059c508` primary, `*0x8059c408` CI) indexed by `Particle+0x08`/`0x09`/`0x0a`, then the color
   anim (`psDispParticles`), then geometry emission.
4. Geometry, branched on `Particle+0x04 & 0x40000000` and then `+0x90`:
   - **Quad/point path** (bit set): `Ptcl_EmitStreak` (`0x80436460`) emits a `GXBegin(GX_LINES, 2)`
     velocity-stretched textured **streak** (start `+0x2c` to end `+0x40`) with `GXSetLineWidth`, not
     a filled quad; or the billboard-quad emitter at `0x80436774` when `gen+0x90 != 0`, which builds
     an SRT and concatenates the camera basis; else an inline `GX_POINTS` point sprite.
   - **Trail path** (bit clear): `Ptcl_EmitTrailGen` (`0x80435c0c`, when `Particle+0x90 != 0`, basis
     from the generator's `+0x90` SRT) and `Ptcl_EmitTrailFree` (`0x80434d84`, velocity-aligned) both
     build a cross-section basis and tail-call the shared `Ptcl_EmitTrail` (`0x80435268`), which
     writes a `GXBegin(GX_QUADS, 4)` **ribbon strip** across the particle's stored history points -
     per-segment width from `Particle+0x4c`, depth term from `+0x88`, perspective-corrected to a
     constant screen width.

`psDispParticles` (`0x80437168`) is the per-particle color helper. It skips if `flags & 0x01` is
clear, lerps the ambient RGB from `+0x82` to `+0x86` over period `+0x5c`/`+0x7a` (forced to
`255,255,255` when `flags & 0x80`), lerps the material RGB from `+0x12` to `+0x70` over period
`+0x0e`/`+0x6c`, multiplies the material color by the active light color, and caches the result in
SDA so it only re-issues `GXSetChanMatColor` / `GXSetChanAmbColor` when it changes.

That is what makes `Particle+0x12` and `+0x70` the recolor lever. Billboard orientation comes from
the precomputed camera basis, texture from the `+0x08`/`0x09`/`0x0a` indices, and blend from the
`+0x04` 2-bit field, so none of those interfere.

## Allocation and free lists

Two separate free-list pools, both with 148-byte nodes but distinct heads.

**The particle pool** is 256 nodes with its free-list head at `r13 + 4764` (`0x805de37c`), built by
`Ptcl_PoolInit` (`0x80430298`). The allocator is `Ptcl_Alloc(type, sub, kind, a, b)` (`0x8043294c`):
it bounds-checks `type` (< 32), `sub` (< 64) and `kind`, fetches `desc = psGeneratorDesc[bank][id]`
with `id` checked against `psGeneratorCount[bank]`, pops the free-list head, increments the active
count (`r13+4740`, `0x805de364`) and high-water mark (`r13+4734`, `0x805de35e`), splices the node into
the active bank list, and copies the template from the `PtclDesc`.

**The generator pool** is built by `PtclGen_PoolInit` (`0x80437bf0`) as a dynamic free-list of
caller-sized nodes with its head at `r13 + 4868` (`0x805de3e4`). It holds the `ptclGen` emitter nodes
(the producers), distinct from the 148-byte `Particle` pool (the products).

## Banks

Generators and their sprites ship in `Ef*.dat` archives, one per area or system: `EfPtclVehicle.dat`
for machines, `EfPtclCity.dat`, `EfPtclStadium.dat` and so on. A bank exports four publics under its
own prefix - `<name>_ptcl` (a `ParticleGroup` of generator templates), `<name>_texg` (a `TexGBank` of
sprite textures), `<name>_ref` (a base effect ID and an ID manifest), and `<name>_form`. The first
three structs are declared in `externals/hoshi/include/particle.h`.

`Effect_InstallBankGroup` (`0x8023603c`) resolves those symbols and hands them to
`Effect_InitBankGroup` (`0x802350a0`), which rejects a group whose `<name>_ptcl` version word is below
`0x43` with `Warning: old particle data version`, then calls `psInitDataBanks` (`0x8042a734`) to fill
the count and descriptor tables. `Effect_InstallBankGroupReloc` (`0x802360b8`) is the same path with a
`psRelocDataBanks` pass first.

**The bank index is a literal at the install site**, so which archive lands where is fixed:
`Ptcl_LoadEfPtclVehicle` (`0x80235394`) installs `EfPtclVehicle.dat` as bank 0. That archive holds 52
generators and six texgraphics between them, 28 sprites in total.

A generator names a **texgraphic**, not an image: `PtclDesc.texgraphic` indexes the bank's `TexGBank`
and the particle's own `Particle+0x0a` indexes the images inside it. So one texgraphic is a flipbook a
program can step through. The vehicle bank's group 5 is a round puff followed by six smoke frames, and
the generators that use it walk the index with `0x40`/wait pairs. Every image in a texgraphic shares
one GX format and one size. For a CI format, the `image_count` entries after the terminated image
array are the TLUTs, selected by `Particle+0x0b` (`0xFF` for none).

## Where a machine's exhaust comes from

A star machine's archive names its trail in the `vcAnimationStar` struct reached through
`vcData.anim` (`vcData+0x18`, declared in `externals/hoshi/include/machine.h`): two slots at `+0x38`
for cruising, three at `+0x40` for boosting, two more at `+0x30`, and up to three joints at `+0x4c` to
emit from. Each slot is a bank-0 generator ID or `-1`, so `Machine_StoreVcDataPtr` (`0x801c4f98`)
never has to translate anything: the Slick Star's `20` and `51` are `psGeneratorDesc[0][20]` and
`[51]` directly. The bike class stores its own pair and bones from `+0x20` of its animation bank
instead.

Of the 52 vehicle generators only `3` and `8` go unreferenced by any machine. Several that look spare
belong to the bikes.

Those two spare slots are the way out of the fact that a generator is bank data. `psGeneratorDesc[bank]`
is an array of pointers, so a slot can be pointed at a descriptor somewhere else entirely: copy a
generator into memory of your own, store the copy's address in a slot nothing reads, and a machine
naming that slot emits particles identical to the original out of a descriptor no one else touches.
The swap has to land before anything emits, because `Ptcl_Alloc` reads the descriptor's program
pointer into the generator node once at creation and the node keeps it for life. The tail of
`Ptcl_LoadEfPtclVehicle` at `0x802354bc`, where both of its install paths meet, is the point where the
table exists and nothing has emitted yet.

A machine's cruise and boost generators are authored as a pair and are not interchangeable. The cruise
one emits for as long as the machine is moving, so it stays tight and short-lived; the boost one is a
wide, long-lived flare meant to run for the length of a boost. Putting a boost generator in a cruise
slot gives a permanent cone: the Hydra's `13` is `pi/6` wide where its cruise `12` is 5 degrees, and
the Dragoon's cruise `44` is 4.

## Generators

A `ptclGen` is the emitter object. It carries world placement and orientation, references a `PtclDesc`
template, and spawns particles into a render group each frame.

| Offset | Type | Meaning |
|--------|------|---------|
| +0x04 | u32 | flags (billboard bits) |
| +0x08 | `f32[3]` | world position |
| +0x24 / +0x28 | f32 | rotation angles |
| +0x2c | `f32[3]` | up/orientation vector |
| +0x40 | `f32[3]` | secondary axis vector |
| +0x4c | f32 | scale |
| +0x58 | `ptclGenCallback*` | callback table |
| +0x5c | `void(*)(ptclGen*)` | direct destroy callback |
| +0x90 | `GeneratorAppSRT*` | transform node |

The transform node at `+0x90` is a small SRT integrated once per frame: position `+0x08`, a
velocity-add `+0x30` folded into the position each frame, a state byte `+0x3c`, a frame/dirty tag
`+0x3d` compared against the global frame counter so it integrates at most once per frame, and a 3x4
transform matrix at `+0x40`.

Only slot 0 of the callback table is dispatched in this build: `(*gen->callbacks)[0](particle)` fires
on spawn. A global user hook at `r13 + 4776` (`0x805de388`) also fires for emit shapes above 8.

The spawn chain is: the emit tick at `0x804309e8` runs per parent particle, decrementing life and
switching on the emit shape, and calls `Ptcl_Spawn` (`0x8042b054`), which pops the free-list head,
sets `parent_gen`, copies fields from the `PtclDesc` and the caller's arguments, fires the spawn
callback, and links the particle into its render-group list.

### PtclDesc and the fields it seeds

`psGeneratorDesc[bank][id]` holds one `PtclDesc` per generator a bank installed; `EfPtclVehicle.dat`
is bank 0 with 52 of them. Both `psGeneratorDesc` (`0x8058c708`) and `psGeneratorCount`
(`0x8058c608`) are rebuilt on every scene load that installs banks, so a descriptor must be
re-resolved rather than cached. The struct itself is declared in `particle.h`; what the header does
not say is where each field lands in the 148-byte node:

| `PtclDesc` field | To `Particle` |
|------------------|---------------|
| `flags2` (+0x00) | +0x16 |
| `texgraphic` (+0x02) | +0x1a |
| `x04` | +0x14 (base life) |
| `x06` | +0x1e (life variance) |
| `flags` (+0x08) | +0x04 |
| `grav[0]` / `grav[1]` / `grav_z` | +0x3c / +0x40 / +0x44 |
| `vel[3]` (+0x14) | +0x30 / +0x34 / +0x38 |
| `x20` | +0x48 |
| `spread` (+0x24) | +0x4c |
| `rate` (+0x28) | +0x0c |
| `x30` / `x34` | +0x60 / +0x64 |
| `program` (+0x3c, its base address) | +0x20 |

Two of those carry the shape of the trail. **`spread` is the cone half-angle**: every value in the
vehicle bank is a whole number of degrees converted to radians, from `0` for a generator that emits
along one line up to `pi/2`, and it is the field that separates a tight streak from a wide spray.
`Ptcl_TickOne` then ramps it toward `x30` over the countdown at `Particle+0x0c`, so a burst can open
out or close in as it runs. **`rate` is the emission rate**: `Ptcl_Alloc` seeds a fractional
accumulator at `Particle+0x0c` from it, either against a pair of SDA constants or from `HSD_Randf`,
depending on bit `0x100` of `flags`. Both are the fields to read when a trail emits too much or too
wide.

Start/end RGBA and ambient colors are **not** copied inline. They are set by the program at
`PtclDesc+0x3c`, whose pointer lands at `Particle+0x20`, and read back by `psDispParticles`.

### The descriptor program

Everything from `PtclDesc+0x3c` on is bytecode, stepped by `Ptcl_TickOne` (`0x8042cce8`) once per
frame per particle from the frame it spawns. `Particle+0x1a` counts down the current wait and the next
opcode runs when it reaches zero, so a program is a short timeline rather than a field list. The
opcode constants are the `PTCL_OP_*` defines in `particle.h`; their operand encodings are:

| Opcode | Operands | Effect |
|--------|----------|--------|
| `< 0x80` | one more byte when `0x20` is set | wait `b & 0x1f` frames, or a 13-bit count spanning both bytes |
| `0x40 \| wait` | 1 byte | that wait, then set the sprite image index (`Particle+0x0a`) |
| `0xbc` | 2 bytes | roll a random sprite image index between them |
| `0xc0 \| channels` | a varint, then one byte per set channel | ramp the primary color (`Particle+0x12`) toward it over that many frames |
| `0xd0 \| channels` | same | the same for the secondary color (`Particle+0x16`) |
| `0xff` | - | end |

The low nibble of the two color opcodes says which of R/G/B/A the operands carry, so `0xcf` and `0xdf`
are the four-channel forms, and a duration of 0 applies the target at once. Operands come through two
readers that also advance the program pointer: `Ptcl_ProgReadVarU16` (`0x8042bc10`) takes one byte, or
two when its top bit is set, giving a 15-bit count; `Ptcl_ProgReadF32` (`0x8042bbd8`) takes four
unaligned bytes as a big-endian float through an SDA scratch at `0x805de368`.

Opcodes `0x80`-`0x98` are the vector group, taking one float per set low bit. A 128-entry jump table
at `0x80504fe8`, indexed by `opcode - 0x80` after folding `0xc?` and `0xd?` down to `0xc0`/`0xd0`,
holds the rest.

## Tick versus render

Particles are advanced by their own walk, separate from the render driver and separate from the JObj
tree. `Ptcl_TickGenerators` (`0x80430198`) walks the 32 categories and, per generator, runs
`Ptcl_TickOne` (`0x8042cce8`): decrement life (`+0x0c`), integrate position by velocity and gravity,
advance size (`+0x4c`). Rendering is `psRenderParticles` plus `psDispParticles`, on its own pass.

## The JOBJ_PTCL flag (1 << 5 = 0x20)

The JObj display leaf `HSD_JObjDisp` (`0x8040f848`) reads the JObj flags (`JObj+0x14`) and masks
`0x4020` (`JOBJ_PTCL | JOBJ_SPLINE`). The DObj render runs **only when neither bit is set**; when
`JOBJ_PTCL` is set the leaf simply skips. So `HSD_JObjDisp` does *not* dispatch into the particle
module on a ptcl JObj. The JObj walk renders ordinary geometry and skips ptcl and spline leaves, and
the particle subsystem is driven entirely by its own tick and render walks.

### The JObj-to-generator bridge

The bridge from a model's ptcl JObj to the standalone particle walks lives in the JObj **loader** and
**animation-update** paths, not the display path, via three registered HSD hook globals installed at
particle init: `Ptcl_OnJObjLoad` (`0x8023c850`, hook global `0x805de268`), `Ptcl_OnJObjAnimCreate`
(`0x8023c750`, `0x805de258`), and `Ptcl_OnJObjAnimUpdate` (`0x8042b6a0`, `0x805de260`).

- **At load**, when `JObjLoad` (`0x8040add4`) builds a `JOBJ_PTCL` leaf it skips `HSD_DObjLoadDesc`
  and instead walks the `JOBJDesc+0x10` (`void *ptcl`) descriptor list, calling
  `(*0x805de268)(0, node[1] & 0x3f, node[1] >> 6)` - decoding `type = node[1] & 0x3f`,
  `sub = node[1] >> 6` - into `Ptcl_CreateFromJObj` (`0x80433098`). That runs
  `Ptcl_Alloc(type, sub, kind)`, binds the JObj into `gen+0x10`, ORs `gen+0x16 |= 0x700`, and appends
  the generator to the effect master list with `HSD_SListAppend(*0x805de370, gen)`.
- **Per frame**, the generator update pass at `0x804324ec` walks `*0x805de370` and calls
  `Ptcl_SyncGenToJObj` (`0x8043070c`), which re-derives each generator's orientation and position from
  its bound JObj's world matrix (`JObj+0x44`) when `gen+0x16 & 0x100`, so the standalone-rendered
  particles follow their owning model. JObj anim tracks `0x28` and `0x2a` (branches at `0x8040a000`
  inside `JObjUpdateFunc`, `0x8040985c`) call the `0x805de258` / `0x805de260` hooks with the same
  `type`/`sub` decode, letting animation spawn and drive ptcl effects.

So a ptcl JObj's generators are created at load (or by an anim track), bound to the JObj, ticked by
`Ptcl_TickGenerators`, spawned via `Ptcl_Spawn`, bank-inserted by `Ptcl_AllocNode`, and drawn by
`psRenderParticles` - entirely outside `HSD_JObjDisp`.

## Recolor levers

There are two, and they look different in motion.

**The live particles.** Because `psDispParticles` reads per-particle color, forcing the color fields
on particles already in flight turns the whole effect at once: write `Particle+0x12` (start RGBA) and
`+0x70` (end RGBA), plus `+0x82`/`+0x86` for ambient start and end, and clear `Particle+0x04 & 0x80`
so the white-material override does not stomp the ambient. `Particle+0x20`, the program pointer, is a
stable scope key for recoloring only the particles from one generator rather than the whole pool. The
cost is a walk of the 32 render-group lists each frame, and the descriptor program fights it on any
frame one of its own color opcodes fires.

**The descriptor.** Writing over a color operand in a loaded `PtclDesc` program instead colors the
particles born from that frame on and leaves the ones in flight alone, which comes out as a gradient
travelling along a trail. It is a three-byte store per frame with no walk, and nothing fights it
because it *is* what the program reads. In exchange it is bank-wide: every emitter using that
generator ID takes the same color, and the tables are rebuilt on every scene load, so the descriptor
has to be re-resolved rather than cached.

Either lever reaches only assets that actually emit point particles. Anything built as a standalone
model-effect GObj carries no `Particle` node at all, so no write into this pool affects it; recoloring
those means rewriting their material TEV color registers instead.

## Key functions

Names in parentheses are descriptive labels for addresses the symbol map leaves unnamed.

| Address | Name | Role |
|---------|------|------|
| `0x80433f00` | `psRenderParticles` | per-frame particle render driver |
| `0x80437168` | `psDispParticles` | per-particle color helper |
| `0x8043845c` | (render-group selector) | fetch a render group's head/tail, lazy re-sort |
| `0x80436460` | `Ptcl_EmitStreak` | `GX_LINES` velocity-stretched streak, not a filled quad |
| `0x80436774` | (billboard-quad emitter) | quad from the camera basis and an SRT |
| `0x80435268` | `Ptcl_EmitTrail` | shared `GX_QUADS` ribbon/trail writer |
| `0x80435c0c` | `Ptcl_EmitTrailGen` | trail basis from the `gen+0x90` SRT (`Particle+0x90 != 0`) |
| `0x80434d84` | `Ptcl_EmitTrailFree` | velocity-aligned trail basis (`Particle+0x90 == 0`) |
| `0x8043294c` | `Ptcl_Alloc` | allocate and template-copy a particle |
| `0x80438238` | `Ptcl_AllocNode` | pop a free node, bank-insert at `0x8058cce8[group << 2]`, mark dirty |
| `0x80430298` | `Ptcl_PoolInit` | build the 256-particle free-list |
| `0x80437bf0` | `PtclGen_PoolInit` | build the generator free-list |
| `0x80430198` | `Ptcl_TickGenerators` | per-frame generator walk |
| `0x8042cce8` | `Ptcl_TickOne` | integrate one particle and step its program |
| `0x8042b054` | `Ptcl_Spawn` | spawn one particle from a generator |
| `0x804309e8` | (emit tick) | per-parent-particle emit switch feeding `Ptcl_Spawn` |
| `0x8042bc10` | `Ptcl_ProgReadVarU16` | program operand: one byte, or two for a 15-bit count |
| `0x8042bbd8` | `Ptcl_ProgReadF32` | program operand: four unaligned bytes as a float |
| `0x80433098` | `Ptcl_CreateFromJObj` | `Ptcl_Alloc` + bind `gen+0x10` to the JObj + list append |
| `0x8043070c` | `Ptcl_SyncGenToJObj` | re-derive a generator's transform from its bound JObj |
| `0x8023c850` | `Ptcl_OnJObjLoad` | `0x805de268` hook, fired by `JObjLoad` for `JOBJ_PTCL` leaves |
| `0x8023c750` | `Ptcl_OnJObjAnimCreate` | `0x805de258` hook, JObj anim track `0x28` |
| `0x8042b6a0` | `Ptcl_OnJObjAnimUpdate` | `0x805de260` hook, JObj anim track `0x2a` |
| `0x8040add4` | `JObjLoad` | creates generators for `JOBJ_PTCL` leaves via the `0x805de268` hook |
| `0x8040985c` | `JObjUpdateFunc` | anim-track dispatch; the ptcl branches sit at `0x8040a000` |
| `0x8040f848` | `HSD_JObjDisp` | geometry leaf; skips `JOBJ_PTCL` / `JOBJ_SPLINE` |
| `0x80233b74` / `0x80233ba0` | `Ptcl_Think` / `Ptcl_Think2` | Effect-module thunks that enter both walks |
| `0x804324ec` | (generator update pass) | walks `*0x805de370` driving `Ptcl_SyncGenToJObj` |
| `0x8042a734` | `psInitDataBanks` | fill the per-bank count and descriptor tables |
| `0x8042a874` | `psRelocDataBanks` | relocate a bank's internal offsets before install |
| `0x802350a0` | `Effect_InitBankGroup` | version-gate a bank group and install it |
| `0x8023603c` | `Effect_InstallBankGroup` | resolve a bank's `_ptcl`/`_texg`/`_ref` symbols |
| `0x802360b8` | `Effect_InstallBankGroupReloc` | same, with a `psRelocDataBanks` pass first |
| `0x80235394` | `Ptcl_LoadEfPtclVehicle` | load `EfPtclVehicle.dat` and install it as bank 0 |
| `0x801c4f98` | `Machine_StoreVcDataPtr` | binds a machine's `vcData`, including its generator IDs |
| `0x8058cce8` | particle render-group bank array | 3x `u32[32]`: owner / head / tail |
| `0x8058c708` | `psGeneratorDesc` | `descTable[bank][id]` |
| `0x8058c608` | `psGeneratorCount` | per bank, bounds for `id` |
| `0x80504fe8` | descriptor-program jump table | 128 entries, indexed by `opcode - 0x80` |
