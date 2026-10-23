# Particles

HSD point particles are the bulk sprite layer: machine exhaust, sparkle dust, spark trails, item
box bursts. A **generator** emits **particles** each frame from a descriptor in an effect bank,
and the particles are ticked and drawn by their own walks, outside the JObj display path. Model
effects (a GObj with a JObj tree, such as the inhale whirlwind) are a separate system: they share
the banks and ID space but never the render path, so nothing written to the particle pool reaches
them. This doc covers the pools, the bank tables and how to extend them, the descriptor program,
and the levers for recoloring particles.

## Generators and particles

Two node pools back the system.

- **Generators** are 0x94-byte nodes. `Ptcl_Alloc(link_no, bank, id)` (0x8043294c) takes one
  from the free list at 0x805de37c (32 built at boot by `Ptcl_PoolInit`, 0x80430298, more
  allocated when it runs dry), copies the fixed fields of descriptor
  `stc_ps_generator_desc[bank][id]` into it, and links it into the generator list at 0x805de370.
  It returns NULL when `link_no >= 32`, `bank >= 64`, `id >= stc_ps_generator_count[bank]`, or the
  descriptor slot is NULL. The node keeps the descriptor's program pointer for life.
- **Particles** are 0xb0-byte nodes from a 256-node free list at 0x805de3e8, built at boot.
  `Ptcl_Spawn` (0x8042b054) creates one from a generator's fields and the caller's arguments, and
  `Ptcl_AllocNode` (0x80438238) pushes it onto render group `link_no`'s list.

There are 32 render groups, held in three parallel word arrays: the list head at 0x8058cce8, a
dirty flag at 0x8058cd68 that `Ptcl_AllocNode` sets, and a cached pointer at 0x8058cde8. When a
group is dirty, the selector at 0x8043845c regroups its list by the particles' flag bits before
handing it to the renderer. The arrays are reached only through a split `lis`/`addi` pair, so a
pointer search finds no reference to them.

Both walks are entered from the Effect module's thunks `Ptcl_Think` (0x80233b74) and
`Ptcl_CinematicThink` (0x80233ba0):

- **Tick.** `Ptcl_TickGenerators` (0x80430198), despite the name, walks the 32 group lists and runs
  `Ptcl_TickOne` (0x8042cce8) on each particle: integrate velocity and gravity, age it, and step
  its descriptor program.
- **Render.** `psRenderParticles` (0x80433f00) builds a camera-facing basis from the current
  CObj, then walks the groups enabled by its mask. Per particle it sets the channel colors
  (`psDispParticles`, 0x80437168), blend mode, alpha compare, TEV and texture, then emits one of:
  a point sprite, a billboard quad (0x80436774), a velocity-stretched `GX_LINES` streak
  (`Ptcl_EmitStreak`, 0x80436460), or a `GX_QUADS` ribbon across the particle's stored history
  points (`Ptcl_EmitTrail`, 0x80435268, through `Ptcl_EmitTrailGen` or `Ptcl_EmitTrailFree`).

## Banks and descriptors

Particle data ships in `Ef*.dat` archives, one per area or system. A bank exports `<name>_ptcl`
(a `ParticleGroup` of generator templates), `<name>_texg` (a `TexGBank` of sprite textures),
`<name>_ref` (a base effect ID and the IDs the bank provides) and `<name>_form`.
`Effect_InstallBankGroup` (0x8023603c) resolves them and hands them to `Effect_InitBankGroup`
(0x802350a0), which rejects a `_ptcl` version below 0x43 and calls `psInitDataBanks` (0x8042a734).
`Effect_InstallBankGroupReloc` (0x802360b8) runs `psRelocDataBanks` (0x8042a874) first.

`psInitDataBanks` sets the two tables every reader uses: `stc_ps_generator_desc[bank]`
(0x8058c708) points at the bank's descriptor offsets, and `stc_ps_generator_count[bank]`
(0x8058c608) bounds the id. For a version 0x40-0x43 bank it biases the descriptor pointer back by
the bank's base id (`ParticleGroup+0x04`) and stores `base + count` as the count, so both tables
are indexed by the whole effect id: the yakumono bank (bank 5) holds the item box bursts at ids
50000-50005. The bank index is a literal at each install site; `Ptcl_LoadEfPtclVehicle`
(0x80235394) installs `EfPtclVehicle.dat`, 52 generators, as bank 0. Both tables are rebuilt on
every scene load that installs banks, so re-resolve a descriptor instead of caching it.

A descriptor (`PtclDesc`) names a **texgraphic**, not an image: one `TexGDesc` is a flipbook of
same-format, same-size images that the program steps through with the image index at
`Particle+0x0a`. Two fields shape a trail: `spread` (+0x24) is the emission cone half-angle in
radians, ramped toward `x30` as the particle runs; `rate` (+0x28) seeds the emission accumulator.
Everything from +0x3c on is the program.

**Growing a bank.** Nothing but `psInitDataBanks` writes the two tables (the teardowns at
0x8042ad44 and 0x8042af18 clear them), and both readers - `Ptcl_Alloc` and the spawn helper at
0x8042b350 - bound an id by the count alone. So a mod can copy a bank's pointers into its own
array, append descriptors held anywhere, point `stc_ps_generator_desc[bank]` at the array and
raise the count. A descriptor holds no pointers (loops store offsets, textures and child
generators are indexes), so its bytes work from anywhere, except that `psRelocDataBanks` rewrites
the `0x0E000000` bits of `flags` to `0x08000000` in every descriptor it relocates; an appended one
needs the same rewrite. The table must grow before anything emits, since a generator copies its
program pointer at creation. For bank 0 the tail of `Ptcl_LoadEfPtclVehicle` at 0x802354bc, where
its install paths meet, is that point.

## The descriptor program

`Ptcl_TickOne` steps the program once per frame from the frame the particle spawns. The wait
counter at `Particle+0x1a` counts down and the next opcode runs when it reaches zero, so a program
is a short timeline. The opcode constants are hoshi's `PTCL_OP_*`.

| Opcode | Operands | Effect |
|---|---|---|
| `< 0x80` | one more byte when `0x20` is set | wait `b & 0x1f` frames, or a 13-bit count across both bytes |
| `0x40 \| wait` | 1 byte | that wait, then set the image index (`+0x0a`) |
| `0xbc` | 2 bytes | roll a random image index between them |
| `0xc0 \| channels` | a duration, then one byte per set channel | ramp the primary color (`+0x12`) to it |
| `0xd0 \| channels` | same | ramp the secondary color (`+0x16`) |
| `0xff` | - | end |

The low nibble of a color opcode says which of R/G/B/A follow, so `0xcf` and `0xdf` carry all
four; a duration of 0 applies the color at once. Durations go through `Ptcl_ProgReadVarU16`
(0x8042bc10: one byte, or two with the top bit set) and floats through `Ptcl_ProgReadF32`
(0x8042bbd8: four unaligned bytes). Opcodes `0x80`-`0x98` are the vector group, one float per set
low bit; the rest dispatch through a 128-entry table at 0x80504fe8, indexed by `opcode - 0x80`
with `0xc?` and `0xd?` folded down.

## Color

`psDispParticles` sets a particle's channel colors, and only for a particle with bit
`0x80000000` of its flags (`+0x04`) set:

- Material color lerps from `+0x12` (start RGBA) to `+0x70` (end) as the remaining count at
  `+0x6c` runs down over the period at `+0x0e`. Flag `0x80` forces it to white.
- Ambient is a grey level at `+0x82` with alpha at `+0x83`, lerped toward `+0x86`/`+0x87` over
  `+0x7a` / `+0x5c`, then multiplied by the material color.
- The result is cached so `GXSetChanMatColor` is only reissued when it changes.

That gives three recolor levers, which look different in motion:

- **Live particles.** Write `+0x12`, `+0x70` and the ambient bytes on particles already in flight,
  and clear flag `0x80`. The whole effect turns at once, at the cost of walking the 32 group lists
  every frame, and the program overrides it on any frame one of its color opcodes fires. The
  program pointer at `+0x20` is a stable key for touching one generator's particles only.
- **The descriptor.** Overwrite a color operand in the loaded program. Only particles born after
  that read it, so the change travels along a trail as a gradient, with no walk and nothing to
  fight it. It is bank-wide: every emitter of that id takes it.
- **A descriptor copy.** Since a generator keeps the program pointer it was created with,
  pointing a table slot at a recolored copy for the length of one spawn recolors that emitter
  alone.

## JObj-bound generators

`HSD_JObjDisp` (0x8040f848) skips any JObj with `JOBJ_PTCL` or `JOBJ_SPLINE` set, so the display
walk never draws particles. The link from a model to its particles is in the loader and the
animation update instead, through three hook globals installed at init:

- `JObjLoad` (0x8040add4) builds no DObj for a `JOBJ_PTCL` leaf. It walks the descriptor's
  particle list and calls `Ptcl_OnJObjLoad` (0x8023c850, hook 0x805de268) with `type = word & 0x3f`
  and `sub = word >> 6`, reaching `Ptcl_CreateFromJObj` (0x80433098): `Ptcl_Alloc`, bind the JObj
  into the generator, and append it to the generator list.
- Every frame the generator update pass at 0x804324ec runs `Ptcl_SyncGenToJObj` (0x8043070c),
  which re-derives each bound generator's position and orientation from its JObj's world matrix.
- JObj animation tracks `0x28` and `0x2a` (dispatched inside `JObjUpdateFunc`, 0x8040985c) call
  `Ptcl_OnJObjAnimCreate` (0x8023c750) and `Ptcl_OnJObjAnimUpdate` (0x8042b6a0), so an animation
  can spawn and drive generators.

## Machine exhaust

A machine names its trail by bank-0 generator id, read off its animation bank on every spawn with
nothing translating it. A star's `vcAnimationStar` carries two `unk_particle` slots (+0x30), two
`moving_particle` slots for cruising (+0x38), three `boosting_particle` slots (+0x40), and up to
three emit joints (`particle_bone`, +0x4c). A bike's `vcAnimationWheel` carries one
`cruise_particle` (+0x20), emitted on a timer by `Machine_Wheel_Think` (0x801f5390), one
`boost_particle` (+0x24), emitted at all four bones by the class's boost proc (0x801f516c), and
four bones from +0x28. Each slot is an id or -1. Of the 52 vehicle generators only 3 and 8 are
named by no machine.

Cruise and boost generators are authored as a pair and do not swap. The cruise one emits for as
long as the machine moves, so it stays tight and short-lived; the boost one is a wide, long-lived
flare. A boost generator in a cruise slot gives a permanent cone: the Hydra's boost `13` is pi/6
wide where its cruise `12` is 5 degrees.

## Key functions

| Function | Address | Role |
|---|---|---|
| `Ptcl_Alloc` | 0x8043294c | create a generator from `stc_ps_generator_desc[bank][id]` |
| `Ptcl_Spawn` | 0x8042b054 | create one particle from a generator |
| `Ptcl_TickOne` | 0x8042cce8 | integrate one particle and step its program |
| `psRenderParticles` | 0x80433f00 | per-frame render driver |
| `psDispParticles` | 0x80437168 | per-particle channel colors |
| `psInitDataBanks` | 0x8042a734 | fill the per-bank count and descriptor tables |
| `Ptcl_LoadEfPtclVehicle` | 0x80235394 | install `EfPtclVehicle.dat` as bank 0 |
| `Ptcl_SyncGenToJObj` | 0x8043070c | move a JObj-bound generator with its JObj |
