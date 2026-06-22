# Particle system

The HSD **point-particle** pool is the lower of KAR's two visual-effect layers (the higher,
model-based layer is in `effects-system.md`). Point particles are camera-facing textured sprites
— machine exhaust, sparkle dust, spark trails, impact puffs — emitted in bulk by **generators**
and drawn by a dedicated render driver. The whole subsystem lives in one module, roughly
`0x80430000`–`0x80438000`.

It is a classic HSD design with three moving parts:

- a fixed **pool of 256 `Particle` objects** (148 bytes each),
- a dynamic pool of **generators** (`ptclGen`) that emit particles each frame from a static
  **`PtclDesc`** template,
- **32 render-group lists** (the bank head array at `0x8058cce8`) that a per-frame driver walks
  to draw every live particle.

Tick (physics/lifetime) and render are fully decoupled and run from separate walks.

> The inhale whirlwind is **not** a point particle — it is a model effect (see
> `effects-system.md`). The point-particle pool *is* the recolorable exhaust/sparkle layer
> described below: each particle carries its own start/end color, so forcing those fields recolors
> exhaust. The whirlwind has no point-particle component, so that lever does not touch it.

## The `Particle` struct (148 bytes, `0x94`)

The node size `148` is fixed by the pool allocator. The layout is used by the
allocator/descriptor-copy (`Ptcl_Alloc`, `0x8043294c`), the per-particle color
helper (`psDispParticles`, `0x80437168`), and the render driver + quad emitters (`0x80433f00`,
`0x80436460`, `0x80436774`).

| Offset | Type | Field | Notes |
|--------|------|-------|-------|
| 0x00 | `Particle*` | `next` | intrusive list link (free-list and render group) |
| 0x04 | `u32` | `flags` | see bit table below |
| 0x08 | `f32` | `fade_rate` | drives the `0x0c` lifetime init |
| 0x0a | `u8[2]` | tex sub-index | secondary texture index / `0xFF` sentinel |
| 0x0c | `f32` | `life` | remaining lifetime, counts down |
| 0x0e | `u16` | mat-color anim period | interp denominator for start→end mat color |
| 0x12 | `u8[4]` | **`rgba_start`** | **start material color (recolor lever)** |
| 0x14 | `u16` | `base_life` | from `PtclDesc+0x04` |
| 0x16 | `u16` | `flags2` | low nibble = emit shape |
| 0x18 | `u8[3]` | `col_rgb` | color template |
| 0x1e | `u16` | life variance | spawn aux |
| 0x20 | `PtclDesc*` | `desc` | **per-effect sub-descriptor pointer (stable scope key)** = `PtclDesc+0x3c` |
| 0x24 | `f32[3]` | `vel0` | initial velocity (spawn value) |
| 0x30 | `f32[3]` | `vel` | velocity x/y/z |
| 0x3c | `f32` | `grav_x` | gravity/drag X |
| 0x40 | `f32` | `grav_y` | gravity/drag Y |
| 0x44 | `f32` | `grav_z` | gravity/drag Z |
| 0x48 | `f32` | `spread_a` | spread/cone |
| 0x4c | `f32` | `size` | point/line size; also depth-cull key |
| 0x50 | `f32` | `scale_curve` | init `1.0` |
| 0x5c | `u16` | secondary color-anim period | |
| 0x60 | `f32[3]` | size-range / interp endpoints | |
| 0x6c | `u16` | end mat-color period | interp denominator |
| 0x70 | `u8[4]` | **`rgba_end`** | **end material color (recolor lever)** |
| 0x74 | `u8[4]` | amb color end | |
| 0x7a | `u16` | ambient anim period | |
| 0x82 | `u8[2]` | **`amb_start_rgb`** | ambient start |
| 0x86 | `u8[2]` | **`amb_end_rgb`** | ambient end |
| 0x88 | `f32` | z-scale | FIFO depth term |
| 0x8c | `ptclGen*` | `parent_gen` | back-pointer to the emitting generator |
| 0x90 | `u16`/ptr | geom kind / model-desc | selects billboard vs custom-geometry emitter |

The color/interp cluster (`0x0e`, `0x10`, `0x5c`, `0x6c`, `0x7a` and the `0x7c..0x88` bytes) has
overlapping byte/halfword offsets. The reliable color fields are start RGBA `+0x12`, end RGBA
`+0x70`, ambient `+0x82`/`+0x86`.

### `flags` bits (`Particle+0x04`)

| Bit | Meaning |
|-----|---------|
| `0x01` | material-color animation active |
| `0x08` | rotated / billboard-corner quad |
| `0x80` | **force white material** (ambient forced to 255) |
| `0x100` | fog enable |
| `0x800` | tick: already-disposed / external — skip |
| `0x10000` | blend-mode select bit |
| `0x100000` | use simple-quad emitter |
| `0x800000` | descriptor requests lifetime-from-alpha |
| bits 25/26 | animated texture matrix |
| `0x40000000` / `0x80000000` | quad-vs-geometry / geometry-vs-line path selectors |

## The render-group bank array (`0x8058cce8`)

The bank head array is **three parallel 32-entry word sub-arrays**, indexed by `group << 2`. It
is accessed only via split immediate (`lis 0x8059; addi -13080`), which is why a pointer search
finds no aligned reference to it.

| Sub-array | Address | Per-group contents |
|-----------|---------|--------------------|
| `+0x000` | `0x8058cce8` | owner/descriptor pointer + dirty flag |
| `+0x080` | `0x8058cd68` | **live-list head** |
| `+0x100` | `0x8058cde8` | **live-list tail** |

There are **32 groups** (the render driver bounds-checks `group < 32`); a bitmask argument
`1 << group` gates which groups draw. The group selector (`0x8043845c`) lazily re-sorts a group's
list (a radix split on `Particle+0x04` blend bits) when its owner word is marked dirty, then hands
the head/tail to the driver. Insertion into a group list is done by **`Ptcl_AllocNode`**
(`0x80438238`), called from `Ptcl_Spawn`: it pops a free node, computes `0x8058cce8 + (group<<2)`,
pushes the node at that group's head, and marks the dirty word at `0x8058cd68 + (group<<2)`. (The
helpers `0x80437ddc` / `0x80437e18` are unrelated — they attach a particle to a **parent/owner**
via `Particle+0x90`/`+0x54` with a refcount at `+0x3e`, not to a bank group.) A separate scratch
block at `0x8058cc08` holds the per-frame camera-facing billboard basis (not a list).

## Render path

The disp driver is **`psRenderParticles`** (`0x80433f00`) — the only function that walks
`0x8058cce8` and calls both the color helper and the quad emitters. Per frame it:

1. Builds a camera-facing billboard basis from the current CObj viewing matrix into scratch
   `0x8058cc08` (`HSD_CObjGetViewingMtx`, `PSMTXInverse`).
2. Loops the 32 groups (mask-gated), fetching each group's head/tail and walking the list via
   `Particle+0x00`.
3. Per particle, in order: channel-color setup (`GXSetChanMatColor` / `GXSetChanAmbColor`,
   `GXSetChanCtrl` with the light mask), blend mode from the `Particle+0x04` 2-bit selector
   (`GXSetBlendMode`), alpha compare, TEV setup, texture binding from two 3-level pointer tables
   (`*0x8059c508` primary, `*0x8059c408` CI) indexed by `Particle+0x08/0x09/0x0a`, then the color
   anim (`psDispParticles`), then geometry emission.
4. **Geometry**, branched on `Particle+0x04 & 0x40000000` then `+0x90`:
   - **Quad/point path** (`+0x04 & 0x40000000` set): `Ptcl_EmitStreak` (`0x80436460`) — a
     `GXBegin(GX_LINES, 2)` velocity-stretched textured **streak** (start `+0x2c` → end `+0x40`)
     with `GXSetLineWidth`, *not* a filled quad; or `Ptcl_EmitBillboardQuad` (`0x80436774`) when
     `gen+0x90 != 0` (builds an SRT and concatenates the camera-facing basis); else an inline
     `GX_POINTS` point-sprite.
   - **Trail path** (`+0x04 & 0x40000000` clear): `Ptcl_EmitTrailGen` (`0x80435c0c`, when
     `Particle+0x90 != 0` — basis from the generator's `+0x90` SRT) and `Ptcl_EmitTrailFree`
     (`0x80434d84`, velocity-aligned) both build a cross-section basis and tail-call the shared
     `Ptcl_EmitTrail` (`0x80435268`), which writes a `GXBegin(GX_QUADS, 4)` **ribbon strip** across
     the particle's stored history points — per-segment width from `Particle+0x4c`, depth term from
     `+0x88`, perspective-corrected to a constant screen width.

**`psDispParticles`** (`0x80437168`) is the per-particle **color** helper. It:

- skips if `flags & 0x01` is clear,
- lerps the ambient RGB from `+0x82`→`+0x86` over period `+0x5c`/`+0x7a` (forced to `255,255,255`
  when `flags & 0x80`),
- lerps the material RGB from `+0x12`→`+0x70` over period `+0x0e`/`+0x6c`,
- multiplies the material color by the active light color, and
- caches the result in SDA, only re-issuing `GXSetChanMatColor`/`GXSetChanAmbColor` when it changes.

This is the function that makes `Particle+0x12` / `+0x70` the **recolor lever**: overwrite a
particle's start/end RGBA (and clear `flags & 0x80`) and its rendered color changes. Billboard
orientation comes from the precomputed camera basis; texture from the `+0x08/0x09/0x0a` indices;
blend from the `+0x04` 2-bit field.

## Allocation and free list

Two separate free-list pools, both with 148-byte nodes but distinct heads:

**The particle pool** — 256 nodes, free-list head at `r13 + 4764` (`0x805de37c`). Initialized by
`Ptcl_PoolInit` (`0x80430298`), which builds and links the 256 nodes and sets the active-count
cache. The actual allocator is **`Ptcl_Alloc`**
(`0x8043294c`):

```c
Particle *Ptcl_Alloc(int type /*<32*/, int sub /*<64*/, int kind, u8 a, u8 b);
```

It bounds-checks `type`/`sub`/`kind`, fetches `desc = descTable[type][sub]` (table base
`0x8058c708`), pops the free-list head, increments the active count (`r13+4740`) and high-water
mark (`r13+4734`), splices the node into the active bank list, and **copies the template** from
the `PtclDesc` (see below).

| Property | Value |
|----------|-------|
| pool size | 256 |
| node size | 148 (`0x94`) |
| free-list head | `r13+4764` (`0x805de37c`) |
| active count | `r13+4740` (`0x805de364`) |
| high-water | `r13+4734` (`0x805de35e`) |

**The generator pool** — `PtclGen_PoolInit` (`0x80437bf0`) builds a dynamic free-list of `count`
nodes of caller-chosen size, head at `r13 + 4868`
(`0x805de3e4`). This pool holds the `ptclGen` emitter nodes (the producers), distinct from the
148-byte `Particle` pool (the products).

## Generators

A **`ptclGen`** is the emitter object: it carries world placement and orientation, references a
`PtclDesc` template, and spawns particles into a render group each frame.

### `PtclDesc` — the static emission template

`descTable[type][sub]` (base `0x8058c708`) holds one `PtclDesc` per particle kind. `Ptcl_Alloc`
copies it field-for-field into the new particle:

| `PtclDesc` offset | → `Particle` | Meaning |
|-------------------|--------------|---------|
| +0x00 (u16) | +0x16 | flags2 template |
| +0x02 (u16) | +0x1a | sub-kind / blend |
| +0x04 (u16) | +0x14 | base life |
| +0x06 (u16) | +0x1e | life variance |
| +0x08 (u32) | +0x04 | flags (bit 23 = lifetime-from-alpha) |
| +0x0c (f32) | +0x3c | gravity/drag X |
| +0x10 (f32) | +0x40 | gravity/drag Y |
| +0x14/0x18/0x1c | +0x30/0x34/0x38 | velocity x/y/z |
| +0x20 (f32) | +0x48 | spread/cone |
| +0x24 (f32) | +0x4c | size |
| +0x28 (f32) | +0x08 | fade/alpha rate (→ life init) |
| +0x2c (f32) | +0x44 | gravity/drag Z |
| +0x3c (base) | +0x20 | **sub-descriptor pointer** (color block + scope key) |

Start/end RGBA and ambient colors are **not** copied inline here — they live in the
sub-descriptor block at `PtclDesc+0x3c` (reached via `Particle+0x20`) and are read from the
particle by `psDispParticles` after spawn.

### `ptclGen` fields

| Offset | Type | Meaning |
|--------|------|---------|
| +0x04 | u32 | flags (billboard bits) |
| +0x08 | `f32[3]` | world position |
| +0x24/0x28 | f32 | rotation angles |
| +0x2c | `f32[3]` | up/orientation vector |
| +0x40 | `f32[3]` | secondary axis vector |
| +0x4c | f32 | scale |
| +0x58 | `ptclGenCallback*` | callback table |
| +0x5c | `void(*)(ptclGen*)` | direct destroy callback |
| +0x90 | `GeneratorAppSRT*` | transform node (see below) |

### `GeneratorAppSRT` — the transform node at `gen+0x90`

A small SRT node integrated once per frame: position `+0x08`, a velocity-add `+0x30` integrated
into the position each frame, a state byte `+0x3c`, a frame/dirty tag `+0x3d` (compared against
the global frame counter to integrate at most once per frame), and a 3×4 transform matrix at
`+0x40`.

### `ptclGenCallback`

Only slot 0 is dispatched in this build — `(*gen->callbacks)[0](particle)` is fired on spawn.
A global user hook at `r13 + 4776` (`0x805de388`) also fires for emit shapes > 8.

```c
typedef struct ptclGenCallback {
    void (*on_spawn)(Particle *p);   // +0x00 (only slot dispatched in this build)
} ptclGenCallback;
```

### Spawn pipeline

```
emit tick (0x804309e8, per parent particle)   // decrements life; switches on emit shape
  └─ Ptcl_Spawn (0x8042b054)
       ├─ p = pop free-list (r13+4764)
       ├─ p->parent_gen = gen
       ├─ copy fields from PtclDesc / args
       ├─ fire gen->callbacks[0](p)
       └─ link p into its render-group list
```

## Tick vs render are decoupled

Particles are advanced by their **own** walk, separate from the render driver and separate from
the JObj tree:

- **Tick / integrate** — `Ptcl_TickGenerators` (`0x80430198`) walks the 32 categories and, per
  generator, runs `Ptcl_TickOne` (`0x8042cce8`): decrement `life` (`+0x0c`), integrate position by
  velocity and gravity, advance size (`+0x4c`).
- **Render** — `psRenderParticles` (`0x80433f00`) + `psDispParticles` (`0x80437168`).

Both walks are entered from the Effect module's update thunks (`0x80233b74` / `0x80233ba0`), which
is the only structural tie between the point-particle pool and the model-effect manager.

## The `JOBJ_PTCL` flag (`1<<5 = 0x20`)

In this build the JObj display leaf (`0x8040f848`) reads
the JObj flags (`JObj+0x14`) and masks `0x4020` (`JOBJ_PTCL | JOBJ_SPLINE`). The DObj render runs
**only when neither bit is set** — when `JOBJ_PTCL` is set, the leaf simply **skips**. So
`HSD_JObjDisp` does **not** dispatch into the particle module on a ptcl JObj; the JObj walk only
renders ordinary geometry and skips ptcl/spline leaves. The particle subsystem is driven entirely
by its own tick/render walks above, not by the JObj tree.

### The JObj → generator bridge

The bridge from a model's ptcl JObj to the standalone particle walks is **not** in the display
path — it lives in the JObj **loader** and **animation-update** paths via three registered HSD hook
globals (installed at particle init): `Ptcl_OnJObjLoad` (`0x8023c850`, `r13` global `0x805de268`),
`Ptcl_OnJObjAnimCreate` (`0x8023c750`, `0x805de258`), `Ptcl_OnJObjAnimUpdate` (`0x8042b6a0`,
`0x805de260`).

- **Load:** when `JObjLoad` (`0x8040ade8`) builds a `JOBJ_PTCL` leaf it skips `HSD_DObjLoadDesc` and
  instead walks the `JOBJDesc+0x10` (`void *ptcl`) descriptor list, calling `(*0x805de268)(0,
  node[1]&0x3f, node[1]>>6)` (decoding `type = node[1]&0x3f`, `sub = node[1]>>6`) → **`Ptcl_CreateFromJObj`**
  (`0x80433098`): `Ptcl_Alloc(type,sub,kind)`, **bind the JObj into `gen+0x10`**, OR `gen+0x16 |= 0x700`,
  and append the generator to the effect master list `HSD_SListAppend(*0x805de370, gen)` — the list
  `Effect_UpdateAll` / `Ptcl_TickGenerators` walk.
- **Per frame:** `Effect_UpdateAll` (`0x804324ec`) walks `*0x805de370`; **`Ptcl_SyncGenToJObj`**
  (`0x8043070c`) re-derives each generator's orientation/position from its bound JObj's world matrix
  (`JObj+0x44`) when `gen+0x16 & 0x100`, so the standalone-rendered particles follow their owning
  model. JObj anim tracks `0x28` / `0x2a` (in `JObjUpdateFunc` `0x8040a000`) call the
  `0x805de258` / `0x805de260` hooks with the same `type`/`sub` decode, letting animation spawn/drive
  ptcl effects.

So a ptcl JObj's generators are created at load (or by an anim track), bound to the JObj, ticked by
`Ptcl_TickGenerators`, spawned via `Ptcl_Spawn` → bank-inserted by `Ptcl_AllocNode`, and drawn by
`psRenderParticles` — entirely outside `HSD_JObjDisp`.

## Recolor lever (exhaust / sparkles)

Because `psDispParticles` reads per-particle color, the generic exhaust/sparkle pool is
recolorable by forcing the color fields on live particles:

- `Particle+0x12` = start RGBA, `Particle+0x70` = end RGBA (interpolated over life).
- `Particle+0x82`/`+0x86` = ambient start/end.
- Clear `Particle+0x04 & 0x80` so the white-material override does not stomp the ambient.
- `Particle+0x20` is the per-effect sub-descriptor pointer — a stable scope key to recolor only
  one effect's particles rather than the whole pool.

This lever works for exhaust, but it has **no** reach on model effects like
the whirlwind, which carry no point-particle component (recolor those via `_HSD_TObjTev.constant`,
see `effects-system.md`).

## Symbol reference

| Address | Suggested name | Role |
|---------|----------------|------|
| `0x80433f00` | `psRenderParticles` | per-frame particle render driver |
| `0x80437168` | `psDispParticles` | per-particle color helper |
| `0x8043845c` | `Ptcl_GetGroupList` | fetch a render group's head/tail (lazy sort) |
| `0x80436460` | `Ptcl_EmitStreak` | `GX_LINES` velocity-stretched streak (not a filled quad) |
| `0x80436774` | `Ptcl_EmitBillboardQuad` | billboard quad/streak from the camera basis |
| `0x80435268` | `Ptcl_EmitTrail` | shared `GX_QUADS` ribbon/trail writer |
| `0x80435c0c` | `Ptcl_EmitTrailGen` | trail basis from `gen+0x90` SRT (`Particle+0x90 != 0`) |
| `0x80434d84` | `Ptcl_EmitTrailFree` | velocity-aligned trail basis (`Particle+0x90 == 0`) |
| `0x8043294c` | `Ptcl_Alloc` | allocate + template-copy a particle |
| `0x80438238` | `Ptcl_AllocNode` | pop free node + bank-insert at `0x8058cce8[group<<2]`, mark dirty |
| `0x80433098` | `Ptcl_CreateFromJObj` | `Ptcl_Alloc` + bind `gen+0x10`→JObj + list-append |
| `0x80430298` | `Ptcl_PoolInit` | build the 256-particle free-list |
| `0x80437bf0` | `PtclGen_PoolInit` | build the generator free-list |
| `0x80430198` | `Ptcl_TickGenerators` | per-frame generator walk |
| `0x8042cce8` | `Ptcl_TickOne` | integrate one particle (life/physics) |
| `0x8042b054` | `Ptcl_Spawn` | spawn one particle from a generator |
| `0x8043070c` | `Ptcl_SyncGenToJObj` | re-derive a generator's transform from its bound JObj |
| `0x8023c850` | `Ptcl_OnJObjLoad` | `0x805de268` hook, fired by `JObjLoad` for `JOBJ_PTCL` leaves |
| `0x8023c750` | `Ptcl_OnJObjAnimCreate` | `0x805de258` hook, JObj anim track `0x28` |
| `0x8042b6a0` | `Ptcl_OnJObjAnimUpdate` | `0x805de260` hook, JObj anim track `0x2a` |
| `0x8040f848` | `HSD_JObjDisp` (leaf) | renders geometry; skips `JOBJ_PTCL`/`JOBJ_SPLINE` |
| `0x8058cce8` | particle render-group bank array | 3× `u32[32]` (owner / head / tail) |
| `0x8058c708` | `PtclDesc` table base | `descTable[type][sub]` |
