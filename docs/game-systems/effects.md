# Effects

The Effect module turns an effect ID into something on screen. Most effects a modder touches are
**model effects**: a standalone GObj carrying a small self-animating JObj tree - the inhale
whirlwind, hit sparks, charge flashes. An effect ID can also fan out into point particles, which
share the same banks and ID space but live in a separate pool with their own tick and render; a
change made to one never reaches the other. This doc covers the model-effect side - IDs, the
spawn call, the GObj it builds, how to drive an effect from mod code, and how to recolor one - and
the ColAnim colour overlay that flashes and tints rider and machine bodies.

## Effect IDs

IDs are decimal-packed: `id = group * 10000 + entry`. `Effect_GetModelData` (0x80235190) splits an
ID with a divide by 10000, accepts groups 24 to 36, and indexes the per-group descriptor table at
`stc_effect_mgr + 0x24 + group * 4` with an 8-byte stride to reach the kind's `EffectModelDesc`.
Among the resident banks only group 24 is populated, from `EfCommon.dat`'s `efModelData` public
(installed by `Effect_InstallModelData`, 0x8023515c). Group 37 IDs (event-actor and machine-hit
effects) are not in that table; the spawn body special-cases them.

| ID | Group / entry | Effect |
|---|---|---|
| `0x3a982` (240002) | 24 / 2 | inhale whirlwind, spawned by `Rider_StartInhale` (0x801ad2c4) |
| `0x5a592` (370066) | 37 / 66 | machine hit, `Machine_SpawnHitEffect`'s fallback when the caller gives none |
| `0x5a59f`-`0x5a5a1` (370079-370081) | 37 / 79-81 | event-actor effect kinds 1-3 (`EventActor_SpawnEffect`, 0x8020d30c) |
| `0x5a5b9` (370105) | 37 / 105 | default kind; built from a fixed record, not the descriptor table |

## Spawning

```c
u32 Effect_SpawnSync(GOBJ *parent, int id, int efgroup, int anchor_mode, ...); // 0x80236c40
```

- **`parent`** may be NULL; an effect needs no owning object.
- **`efgroup`** is an EfGroup bucket, not an owner. -1 trips `__assert("efrequest.c", ...,
  "group!=EfGroup_None")`. Borrowing a live rider's `RiderData.efgroup` or `efgroup2` works.
- **`anchor_mode`** decides how many varargs follow and what they mean.
- The call returns `{0,0}` without spawning while effects are globally suppressed (the word at
  0x805dd8b8 is nonzero) or when `Effect_CheckSplitScreenAllowed` (0x802410d4) rejects it.
- The return is a 64-bit handle in `{r3, r4}`; hoshi declares only the low word, enough to test
  for failure. One call can fan out into several sub-effects and returns only the primary handle.
  Most callers discard it, so nothing points back at the effect.

| Mode | Varargs | Meaning |
|---|---|---|
| 0 | none | no placement |
| 1 | one `void (*)(void *node)` | world-anchored: the callback receives the spawn node once the effect exists |
| 100-112 | varies | follow a `JOBJ *` |
| 200-220 | varies | follow a `JOBJ *`; 218 is the rider mouth anchor `Rider_StartInhale` uses |
| 510 | varies | what `EventActor_SpawnEffect` passes |

No mode takes a raw world position. To place an effect in the world, spawn with mode 1 and take
the effect GObj from the node in the callback (`node + 0x5c`, `EFFECT_NODE_GOBJ`).

## The model-effect GObj

`EffectModel_CreateGObj` (0x8023ccb4) builds every model effect:

1. Takes an `Effect` state struct from a pool in `stc_effect_mgr` (0x8055d7a0).
2. `GObj_Create(entity_class 25, p_link 16, priority 0)`, with the `Effect` as userdata and the
   destructor at 0x80233ddc.
3. Loads the descriptor's joint template with `JObj_LoadJoint` and attaches it, so `GObj+0x28` is
   the model root and `GObj+0x2c` the `Effect`.
4. Installs the render callback 0x8023dfe0, a wrapper around `JObj_GX` (0x8042a258). The root's
   `JOBJ_ROOT_OPA` / `JOBJ_ROOT_XLU` flags pick the pass; the whirlwind draws in XLU.

A joint-anchored spawn then adds up to two procs at priority 11: the follow proc at 0x8023ce1c
and the animation loop watcher at 0x8023e6bc. The follow proc reads the anchor flags at
`Effect+0x1e` and the target joint at `Effect+0x20`, and every frame rewrites the root's SRT from
the target's world matrix:

```c
if (flags & 1) root->trans = translation(target) [+ Effect+0x28 if flags & 0x100];
if (flags & 2) root->rot   = rotation(target);
if (flags & 4) root->scale = scale(target) * Effect.scale;  // else root->scale = Effect.scale
```

The inhale attaches with flags 7. To move an engine-spawned effect yourself, drive `Effect.scale`
(+0x34) and `Effect+0x28` rather than the JObj, or zero the `Effect+0x1e` halfword - and then still
set `Effect.scale`, which the `else` branch keeps writing.

`Effect.life` (+0x0c) is the loop state: -1 at init, 0 while the one-shot intro plays, 1 once
looping is armed. The watcher waits for the intro animation to finish on a 0, then sets 1 through
0x8023ff80 and calls `JObj_SetAllAOBJLoopByFlags(root, 0xffff)`. Writing a nonzero value before the
intro ends freezes the animation, since the watcher never fires.

A spawned effect is tracked by a separate spawn node (allocated at 0x8023475c) holding the
handle, the kind at +0x18, the efgroup at +0x1c and the effect GObj at +0x5c. Effects live in the
p_link 16 GObj list with entity class 25 and `Effect->kind` set, which is how mod code finds one
it did not keep a handle to.

## Driving an effect from mod code

**Never destroy a spawned effect by hand.** Retiring an efgroup (0x80236358) sweeps every spawn
node in that group and calls `GObj_Destroy(node + 0x5c)` unconditionally. An effect a mod already
destroyed is destroyed a second time, which pushes a live GObj back onto the free list. Nothing
faults then; the next `GObj_Create` hands the same GObj out twice and an unrelated system later
asserts, typically as `gobj->user_data_kind == HSD_GOBJ_USER_DATA_NONE` in `GObj_AddUserData`. To
end an effect, hide its tree with `JObj_SetFlagsAll(root, JOBJ_HIDDEN)` and leave its lifetime to
the engine. Since a group kill can still take it, re-validate a held effect GObj against the
p_link 16 list before each use.

**Mode 1 installs nothing.** It skips the joint-attach path, so no follow proc, no loop watcher,
and no one-shot init run. The model root is the caller's to place, and the caller must arm the
loop itself (`JObj_SetAllAOBJLoopByFlags(root, 0xffff)`) and call `JObj_AnimAll(root)` every frame,
or the animation stalls on the intro's last frame. Mode 1 applies no orientation either. The
whirlwind's tree, for instance, is authored along its local +Z - narrow end at the origin, flaring
to radius 8.07 at z = 9.95 - so a raw mode-1 spawn lies flat. Writing the root's world matrix
(`JOBJ+0x44` with `JOBJ_USER_DEFINED_MTX` set) is the practical way to stand it up: it avoids euler
order, allows independent axial and radial scale, and overrides whatever the effect's own
animation writes to the root.

**Material alpha is per instance.** `MObjLoad` (0x803f9f04) allocates an `HSD_Material` per MObj and
copies the descriptor's 0x14 bytes into it, so writing `MObj.mat` of one effect never touches
another copy of the same model. Write it after the frame's `JObj_AnimAll`, which would otherwise
restore the authored value.

## Recoloring a model effect

A model effect's color is not in the material color registers; writing `MObj.mat` ambient,
diffuse or specular does nothing visible. It comes from a TEV color expression. `MObjSetupTev`
(0x803faba0) calls `HSD_TExpSetupTev` (0x80424624) every render, which walks the material's
compiled TExp tree and emits its constant colors with `GXSetTevColor` / `GXSetTevKColor`. The
constant nodes (`HSD_TExpCnst`, built by `MakeColorGenTExp`, 0x803f5f98) hold not the color but a
pointer into the TObj's `_HSD_TObjTev` block (`TObj+0xa8`): `constant` (+0x10), `tev0` (+0x14) and
`tev1` (+0x18), each a plain `GXColor`.

So the color is owned by `_HSD_TObjTev`. Rewrite those bytes and the next render picks them up:

```c
// Recurse child and sibling: each visible part is its own joint and MObj.
for (DObj *d = jobj->dobj; d; d = d->next)
    for (TObj *t = d->mobj->tobj; t; t = t->next)
        if (t->tev)
        {
            set_rgb(&t->tev->constant, rgb);
            set_rgb(&t->tev->tev0, rgb);
        }
```

Only the register values are safe to write. The TExp nodes themselves hold a list link (+0x04)
and a heap pointer (+0x08), and clobbering either corrupts the walk. A one-shot write persists,
since nothing else rewrites the registers; writing every frame is harmless.

The `color_a`-`color_d` and `alpha_a`-`alpha_d` bytes of `_HSD_TObjTev` pick each combiner input:

| Value | Input |
|---|---|
| `0x00`-`0x0f` | GX combiner inputs verbatim (`0x08` texture color, `0x0c` one, `0x0f` zero) |
| `0x80` | `constant` RGB; `0x81`-`0x83` its R/G/B, `0x84` its alpha |
| `0x85` / `0x86` | `tev0` RGB / alpha |
| `0x87` / `0x88` | `tev1` RGB / alpha |
| `0x43` / `0x44` / `0x45` | alpha selectors for `constant.a` / `tev0.a` / `tev1.a` |

**The whirlwind.** All four materials of `0x3a982` compute `lerp(tev0, constant, texture color)`:
`constant` (near-white cyan) where the spiral texture is bright, `tev0` (blue-grey) where it is
dark. A uniform recolor sets the RGB of both, keeping each alpha; setting one leaves a two-tone
model. Its alpha stage is `ZERO/ZERO/ZERO/TEXA`, so it takes only the texture's alpha and never
the rasterized alpha that `MObj.mat->alpha` feeds. Fading a whirlwind therefore means shrinking its
geometry, or rewriting the alpha selectors and recompiling the TExp tree; the raw selector bytes
are compiled at load and have no effect on their own.

## Body colour overlays (ColAnim)

The flash a rider shows when it turns invincible or intangible is not an effect object but a
colour overlay on its body. `RiderData.col_anim` (+0x5c) is a `ColAnimState`: three
`ColAnimSlot`s followed by the render state they resolve into. Slot 0 is the body tint every
`Rider_ApplyColAnim` (0x8019bfb4) request lands in, slot 1 the additive glow aura that copy-ability
code drives, and slot 2 goes unused by rider code. Machines carry the same overlay, played by
`Machine_ApplyColAnim` (0x801d5ca0) from the ColAnim table in `VcCommon.dat`
(`vcDataCommon.colanim`).

- `ColAnim_Apply` (0x8006a3f0) latches a `ColAnimDesc` - a command script and a priority - from a
  table into a slot, and refuses (returning 0) while the slot holds one of higher priority. In the
  rider table index 2 is the intangibility flash and index 3 the invincibility flash.
- Each frame the slot's script writes its `color` and `light_color`. Setting `cmd_data` to NULL
  freezes the slot on whatever it last wrote.
- `ColAnim_Resolve` (0x8006ae7c) copies the highest-priority active slot's tint, light and blend
  ratio into the render state, the tint only while that slot's `flags` has `COLANIM_FLAG_TINT`.
- `ColAnim_SetupTev` (0x8006aaa4) uploads the resolved colour to a free TEV colour register, which
  blends it over whatever texture is showing. It does nothing when the tint flag is clear and the
  ratio is `COLANIM_RATIO_NONE`.
- `ColAnim_Reset` (0x8006a250) clears a slot.

A mod that wants a continuous body tint owns a slot: apply an anim once (flooring the slot's `pri`
first so the apply is not refused), NULL its `cmd_data`, then each frame write the colour, keep
`pri` at `COLANIM_PRI_MAX` so no pickup flash wins the resolver, and keep the tint flag set.

Kirby's own body colour is not a colour register at all. `RiderKirby_SetMaterialColor`
(0x80198d1c) stages a baked palette index per model part, and
`RiderKirby_SetMaterialColorAndUpdate` (0x80198d3c) also seeks each part's TObj animation to that
variant, swapping in a baked texture. A recolour through it can only snap between the baked
variants; anything continuous goes through the ColAnim overlay.

## Banks and data files

Effect banks are HSD archives exporting four publics per group: `<name>_ptcl` (point-particle
generator templates), `<name>_texg` (sprite textures), `<name>_ref` (`EffectBankRef`: the base ID
`group * 10000` followed by the IDs the bank provides) and `<name>_form`, which must match `_ref`.
`psInitDataBanks` (0x8042a734) installs them into per-bank tables that only the particle path
reads; model-effect lookup reads only `stc_effect_mgr + 0x24`.

`Effect_Init` (0x80233908) runs at boot: it builds `stc_effect_mgr` (`Effect_InitObjAllocs`,
0x802332c4), allocates the particle pools, and loads `EfCommon.dat` (`Effect_LoadEfCommon`,
0x80235524) and `EfPtclVehicle.dat` (`Ptcl_LoadEfPtclVehicle`, 0x80235394). Effect files go
through the preload system as `PRELOADFILEKIND_EFFECT`; `EfCommon` and `A2EfCom` also carry
`PRELOADFLAG_PERSIST` and stay resident.

| Files | Contents |
|---|---|
| `EfCommon.dat` | always resident: `common1_*`, `common2_*`, `yakumono_*`, `efModelData` |
| `A2EfCom.dat` | Top Ride common bank |
| `EfPtclCity`, `EfPtclDesert`, `EfPtclHeat`, `EfPtclIce`, `EfPtclPlants`, `EfPtclValley`, `EfPtclStadium`, `EfPtclMenu`, `EfPtclCheck`, `EfPtclMachine`, `EfPtclVehicle` | per-domain particle banks |
| `EfMapCity`, `EfMapPlants`, `EfEnemy`, `EfDebug`, `EfEnding`, `EfEnding2d`, `A2EfBg00`-`A2EfBg0A` | map, enemy, ending and Top Ride backdrop banks |

## Key functions

| Function | Address | Role |
|---|---|---|
| `Effect_SpawnSync` | 0x80236c40 | spawn an effect by ID |
| `Effect_GetModelData` | 0x80235190 | ID to `EffectModelDesc` |
| `EffectModel_CreateGObj` | 0x8023ccb4 | build a model-effect GObj and its JObj tree |
| `Effect_CheckSplitScreenAllowed` | 0x802410d4 | spawn gate; always passes in 1P |
| (follow proc) | 0x8023ce1c | re-anchors the root SRT to the target joint each frame |
| (loop watcher) | 0x8023e6bc | arms AOBJ looping once the intro ends |
| (efgroup kill) | 0x80236358 | destroys every effect in a group |
| `MObjSetupTev` | 0x803faba0 | per-render TEV setup that re-reads the color registers |
| `ColAnim_Resolve` | 0x8006ae7c | pick the body overlay colour a rider or machine draws with |
