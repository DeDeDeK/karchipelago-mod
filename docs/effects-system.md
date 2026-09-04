# Effects System

The Effect module is the effect-ID and spawn-API layer for Kirby Air Ride's visual effects: a
decimal-packed ID resolves to a per-kind descriptor, and `Effect_SpawnSync` turns it into a live
**model effect** - a standalone `GOBJ` owning a small self-animating HSD `JOBJ` model tree (inhale
whirlwind, hit sparks, charge flashes, the suction cone). Code lives roughly in `0x80232xxx`-`0x80241xxx`.

An effect ID can also fan out into HSD **point particles** - camera-facing textured sprites emitted
by generators (machine exhaust, sparkle dust). Those share the same `.dat` files and ID space but are
a separate pool with its own tick and render walks. Model effects and point particles never share a
render path, and a recolor applied to one has no reach on the other.

## Effect IDs

Effect IDs are decimal-packed, not bitwise: `id = group * 10000 + entry`, so `group = id / 10000`
and `entry = id % 10000`. The `/10000` is the standard signed-magic divide (`0x68DC8BAD`,
`srawi 12`) at the top of `Effect_GetModelData` (`0x80235190`). Known IDs:

| ID (hex) | Decimal | Group | Entry | Effect |
|----------|---------|-------|-------|--------|
| `0x3a982` | 240002 | 24 | 2 | inhale suction cone / whirlwind (`Rider_StartInhale`) |
| `0x3a9ad` | 240045 | 24 | 45 | (spawn-body case) |
| `0x57e47` | 360007 | 36 | 7 | (spawn-body case) |
| `0x5a592` | 370066 | 37 | 66 | machine hit effect, `Machine_SpawnHitEffect`'s fixed fallback |
| `0x5a59f` | 370079 | 37 | 79 | event-actor effect kind 1 (`EventActor_SpawnEffect`) |
| `0x5a5a0` | 370080 | 37 | 80 | event-actor effect kind 2 |
| `0x5a5a1` | 370081 | 37 | 81 | event-actor effect kind 3 |
| `0x5a5b9` | 370105 | 37 | 105 | default kind; resolves to a fixed record, not the descriptor table |

Two different lookup tables are keyed off an ID and answer different questions:

1. **ID to per-kind descriptor** - `Effect_GetModelData` (`0x80235190`). Decodes the decimal ID,
   bounds-checks `group` in `[24,37)`, reads the per-group descriptor table pointer at
   `*(gEffectMgr + 0x24 + group*4)`, then indexes `table[entry*8]` (8-byte stride) for the per-kind
   `EffectModelDesc *`. This is what model-effect creation uses. Among the resident banks only
   **group 24** is populated, from the `EfCommon.dat` `efModelData` symbol (installed by
   `Effect_InstallModelData`, `0x8023515c`); a per-stage bank would have to ship its own
   model-descriptor symbol to fill groups 25 to 36.
2. **ID to group index** - `Effect_GetUnkFromEfGroup` (`0x80234cf0`; the name in the map, though it
   maps ID to group rather than the reverse). Returns an `s16` group index from an 8-byte-stride LUT
   at `*(gEffectMgr + 0x24C)`; valid ID range 1 to 517. Early-outs returning 1 if the busy flag at
   `gEffectMgr + 0x254` is set.

Groups 37 and up (the event-actor and machine-hit IDs at `0x5a592`-`0x5a5b9`) are **not** in the
`[24,37)` descriptor table; an explicit special-case branch in the spawn body handles them.

## Spawning

`Effect_SpawnSync` (`0x80236c40`) is the universal entry point, declared in
`externals/hoshi/include/effect.h`. It is one `0x4ba4`-byte function running to `0x8023b7e4`; the
map's `Effect_SpawnSync_mid` at `0x8023af88` is a label inside its per-ID `switch`, not a second
entry point. It returns a 64-bit handle in `{r3, r4}`.

The third argument is the **EfGroup**, not an owner pointer, and the fourth is an **anchor mode**,
not a joint index. Both are load-bearing: `efgroup == -1` trips
`__assert("efrequest.c", 75, "group!=EfGroup_None")` after an `OSReport("efgroup is none!! kind=%d")`,
and the anchor mode decides how many varargs the placement resolver consumes and what they mean.

What the call does, in order:

- **NULL parent is legal.** The owner-player index preseeds to `5` ("none") at `0x80236ccc` and the
  whole owner block (`0x80236d5c`-`0x80236d98`) is skipped, so an effect can spawn with no owning
  object at all. The parent is only stored into the spawn node at `node+0x08`.
- **Global suppress gate.** If `*(u32*)0x805DD8B8` (`r13 + 2008`) is nonzero the call returns
  `{0,0}` immediately; effects are globally suppressed during pause and non-gameplay scenes.
- **Create gate.** `Effect_CheckToCreate` (`0x802410d4`) is a split-screen dedup (`Gm_GetPlyViewNum`
  plus two ID globals at `0x805D7328` / `0x805D732C`) that returns 1 in 1P. A 0 returns `{0,0}`.
- **Anchor resolve.** The resolver at `0x80240284(mode, va_list*, desc, ply)` zero-fills a 52-byte
  placement descriptor and `va_arg`s mode-specific arguments into it. It fills the descriptor
  **purely from the varargs and never reads `parent`**.
- **Per-ID construction.** A hand-rolled `switch (id)` builds the specific effect (model vs particle,
  sub-models, scale, color). IDs at or above `0x3A980` branch to a per-ID switch at `0x8023731c`.
  Model cases end up in `EffectModel_CreateGObj`.
- **Return.** A fire-and-forget handle `{node+0x10, node+0x14}`, or `{0,0}` on failure. A single call
  can fan out into up to five sub-effects and only the primary handle comes back. Most callers,
  including the inhale, discard it, so nothing points back at the spawned effect.

### Anchor modes

| Mode | Varargs | Descriptor slot | Meaning |
|------|---------|-----------------|---------|
| 0 | none | - | no placement |
| **1** | **1** | `desc[0]` | a `void (*)(void *node)` post-spawn callback invoked with the spawn node once the effect exists. Also skips the joint-attach path entirely |
| 100-112 | varies | `desc[4]` | `JOBJ *` follow target |
| 200-220 | varies | `desc[5]` (plus `desc[6]` / `desc[7]` for 218) | `JOBJ *` follow target; 218 is the rider mouth anchor |

Mode 1 is the **world-anchored spawn path**. No variant takes a raw `Vec3 *` world position: you
spawn with mode 1, receive the spawn node in the callback, take the model root from `node+0x5c` (the
effect GObj), and write its SRT yourself. The epilogue at `0x8023b794` is what calls the callback,
loading `desc[0]`, null-checking it and `bctrl`ing with the node.

Because mode 1 takes the `bne 0x8023b758` branch at `0x802378dc`, `EffectModel_AttachToJObj`
(`0x8023d9b0`) never runs, so `Effect+0x1e` (anchor flags) stays 0, `Effect+0x20` (follow joint)
stays NULL, **no follow proc is installed**, and the one-shot init at `0x8023e684` never runs. That
last part means a mode-1 effect never arms its own animation looping: the spawner must call
`JObj_SetAllAOBJLoopByFlags(root, 0xffff)` and set `Effect.life = 1`, or the animation stalls on the
intro's final frame. It must also call `JObj_AnimAll` on the root every frame, since mode 1 installs
no proc to advance it.

Four call sites pin the signature:

| Caller | parent | id | efgroup | mode | Notes |
|--------|--------|----|---------|------|-------|
| `Rider_StartInhale` (`0x801ad2c4`) | rider GObj | `0x3a982` | `RiderData.efgroup` | 218 (mouth) | call site `0x801ad374`, args `(rd->gobj, 0x3a982, rd->efgroup, 218, hatJObj, hatJObj, ply)`; handle discarded |
| `EventActor_SpawnEffect` (`0x8020d30c`) | actor GObj | `0x5a59f`-`0x5a5a1` by kind arg | - | 510 | call site `0x8020d3ac`; stores the handle at actor `+0xA70`/`+0xA74` |
| `Machine_SpawnHitEffect` (`0x8018dba0`) | machine GObj | caller arg, else fixed `0x5a592` | - | 215 | two mutually exclusive call sites (`0x8018dc44` arg-id, `0x8018dc6c` fallback), not two effects |
| `TornadoSpawnModel` (`mods/custom_weather/src/tornado.c`) | NULL | `0x3a982` | borrowed from a live rider | **1** | the world-anchored case: a detached, scaled-up whirlwind driven as a tornado funnel |

A float-vararg convention (`f1`-`f8` for x/y/z/scale) exists in the resolver but is dead for all
known callers; they pass geometry through the pointer args and clear the FP-arg condition bit.

Because the handle is discarded, mod code that wants to reach a live effect must find it by walking
the `p_link` 16 GObj list, filtering on `entity_class == 25` and `Effect->kind == <id>`. Both
`mods/hypernova/src/hypernova.c` (`RecolorWhirlwinds`) and `tornado.c` (`TornadoModelAlive`) do
exactly that.

### Never destroy a spawned effect by hand

The spawn node outlives the caller's interest in it and keeps pointing at the effect GObj at
`node+0x5c`. The per-node kill at `0x80234a8c` - reached from the efgroup kill `0x80236358`, which
sweeps all 32 node buckets and matches `node+0x1c` against the group - calls `GObj_Destroy(node+0x5c)`
unconditionally. So an effect that mod code already destroyed is destroyed a *second* time whenever
its group is next retired.

The second destroy pushes a still-live GObj onto the free list. Nothing faults at that moment; the
next `GObj_Create` hands the same GObj out again and the failure surfaces far away, as

```
assertion "gobj->user_data_kind == HSD_GOBJ_USER_DATA_NONE" failed in gobjuserdata.c on line 40
  GObj_AddUserData <- UnkGOBJ_Create (0x800e7108) <- ... <- Machine_HitThink
```

in whichever unrelated system happened to allocate next. To retire a mode-1 effect, **hide the model
tree** with `JObj_SetFlagsAll(root, JOBJ_HIDDEN)` and leave the lifetime to the engine, re-validating
the GObj against the `p_link` 16 bucket before each use in case a group kill already took it.

### The whirlwind model's local axes

`0x3a982`'s joint tree (`efModelData[2]` in `EfCommon.dat`) is authored **along its local +Z**: the
narrow mouth end sits at the origin and the swirl flares out to a radius of `8.07` at `z = 9.95`
(the cross section pinches to `r ~ 1.3` around `z = 3`, with a wide `r ~ 7.4` ring at `z ~ 1`).
Local X/Y are the cross-section plane. A detached spawn that wants it standing upright has to map
local +Z onto world +Y itself; mode 1 applies no orientation of its own, so the model renders lying
flat.

Driving the root's **world matrix** (`JOBJ+0x44`, with `JOBJ_USER_DEFINED_MTX` set) rather than its
SRT is the practical way to do that: it sidesteps the euler-order question, allows independent axial
and radial scale, and overrides whatever the effect's own animation writes to the root joint.

## The EffectModel object

A model effect is a `GOBJ` carrying an HSD `JOBJ` model, built by `EffectModel_CreateGObj`
(`0x8023ccb4`):

1. Allocate an `Effect` state struct from a `gEffectMgr` object pool.
2. `GObj_Create(entity_class = 25, p_link = 16, p_priority = 0)`.
3. `GObj_AddUserData(gobj, data_kind = 25, dtor = 0x80233ddc, userdata = Effect)`.
4. The instance init at `0x80233e24`, `Effect_Init(effect, kind, gobj)`, wires the back-pointers and
   zeroes the state.
5. `model = Effect_GetModelData(kind)`; if the kind is the default `0x5a5b9` the model comes from a
   fixed record instead. Asserts with `OSReport "not found effect model data(kind %d)"` when missing.
6. `jobj = HSD_JObjLoadJoint(model->jointdesc)` instantiates the JObj tree from the descriptor's
   joint template.
7. `GObj_AddObject(gobj, obj_kind = 3 /*JOBJ*/, jobj)`, which lands the root at `GObj+0x28`.
8. Register the Effect in its per-group active list, storing the list node at `Effect+0x08`.
9. Install the render callback: `GObj+0x1C` (`gx_cb`) = `0x8023dfe0`.

So for a live model effect, `GObj+0x28` is the model tree and `GObj+0x2C` is the `Effect` state,
which points back at its list node.

**Rendering.** The gx_cb at `0x8023dfe0` is a thin wrapper around `GObj_RenderJObj` (`0x8042a258`),
which reads `gobj->hsd_object`, maps the GX pass to an HSD render mode with `HSD_GetRenderPass`, and
recurses the whole tree through `HSD_JObjDispAll` (`0x8040a7b8`). OPA versus XLU pass is chosen by
the JObj root flags (`JOBJ_ROOT_OPA` / `JOBJ_ROOT_XLU`); the whirlwind's translucent spiral renders
in the XLU pass. `EffectModel_CreateGObj` installs no GX link or think proc of its own - both come
from the entity-class-25 defaults plus the manually installed gx_cb.

**Destruction.** The destructor at `0x80233ddc` frees the optional aux heap block at `Effect+0x90`
(`HSD_Free`) and returns the `Effect` slot to its pool (`HSD_ObjFree`). The JObj tree is owned by the
GObj and torn down by `GObj_Destroy`.

**Procs.** `Effect_SpawnSync`'s per-ID case installs up to two procs, both at **priority 11**, and
only when the anchor mode attaches to a joint: the per-frame joint re-anchor at `0x8023ce1c`
(installed by `EffectModel_AttachToJObj`, `0x8023d9b0`, if `mode & 7`) and the anim loop-start watcher
at `0x8023e6bc` (installed from `0x8023e570`).

The generator update pass at `0x804324ec` is often mistaken for an effect updater. It is not: it
walks the generator list at `*0x805de370`, calls `Ptcl_SyncGenToJObj`, and belongs entirely to the
point-particle path reached through `Ptcl_Think` (`0x80233b74`) and `Ptcl_Think2` (`0x80233ba0`). It
never touches model effects.

**Position-follow-joint is done by the effect module itself**, not by the spawner. The proc at
`0x8023ce1c` reads the anchor flags at `Effect+0x1e` and the target joint at `Effect+0x20`, then
rewrites the model root's **SRT** (not its world matrix) from the target's world matrix each frame:

```c
if (flags & 1) root->trans = translate(tgt->mtx) [+ Effect+0x28 offset if flags & 0x100];
if (flags & 2) root->rot   = rotation(tgt->mtx);
if (flags & 4) root->scale = scale(tgt->mtx) * Effect.scale;   // else scale = Effect.scale
```

The inhale attaches with flags `7`, so all three are driven. Mod code has three ways past it, best
first: spawn with anchor mode 1 so the proc is never installed; drive `Effect.scale` (`+0x34`) and
`Effect+0x28` instead of the JObj if the follow is wanted; or, on an already-spawned engine effect,
zero the whole `Effect+0x1e` u16. Note that the third option still lets the `else` branch stomp scale
from `Effect+0x34`, so set that too.

## Effect state

`EffectModelDesc` and `Effect` are both declared in `externals/hoshi/include/effect.h`. Two things
about them are worth stating outright.

`EffectModelDesc` records are **not a packed array** - each sits at the end of its own joint and anim
data block. What is packed is the 8-byte-stride pointer table at `efModelData`, which is what
`Effect_GetModelData` indexes. The inhale whirlwind is `efModelData[2]` (group 24, entry 2). The
default-kind ID `0x5a5b9` bypasses the table for a fixed record. The exact HSD element type of the
descriptor's two anim-set arrays (`AnimJoint` versus `MatAnimJoint`) is not definitively labelled.

`Effect.life` (`+0x0c`) is **not a countdown**. It is a tri-state anim-loop flag: `-1` at init means
untouched, `0` means the one-shot intro is playing, `1` means looping. Nothing decrements it and
nothing destroys an effect from it. Its only writer is `Effect_SetAnimLoop` (`0x8023ff80`), and the
loop watcher at `0x8023e6bc` flips `0` to `1` once the intro anim ends, enabling AOBJ looping.
Writing a nonzero value before the intro finishes is what freezes the animation: the watcher then
never arms looping.

The **spawn list-node** is a separate allocation (`0x8023475c`) that carries the returned handle and
the back-pointers: `+0x00` list link, `+0x08` parent GObj (stored at `0x8023b76c`), `+0x10`/`+0x14`
handle, `+0x18` kind, `+0x1C` efgroup (what the group-kill at `0x80236358` matches on), `+0x28`
state (init 5), `+0x5C` the effect GObj. `+0x5c` is the field an anchor-mode-1 post-spawn callback
dereferences to reach the model, exposed as `EFFECT_NODE_GOBJ` in `effect.h`.

## Manager and registry globals

Two distinct globals back the effect system, both SDA/absolute addresses declared as pointer casts in
`effect.h` rather than in `link.ld`.

**`gEffectMgr` at `0x8055D7A0`** is the effect-instance manager, built by `Effect_InitObjAllocs`
(`0x802332c4`):

| Offset | Contents |
|--------|----------|
| `+0x00` | HSD object pool #1 (capacity 256) |
| `+0x24` | per-group `EffectModelDesc *` table array, indexed by group (only group 24 populated by the resident `EfCommon` bank) |
| `+0x2C` | HSD object pool #2 (capacity 512) - effect slots when the sub-effect flag is clear |
| `+0x58` | HSD object pool #3 (capacity 64) - effect slots when the sub-effect flag is set |
| `+0xBC` | handle generation counter, `+1` per spawn |
| `+0xC0` | per-kind active-list heads, 32 buckets |
| `+0x1C0` | per-kind active-effect counts |
| `+0x24C` | pointer to the flat 8-byte-stride ID-to-group LUT |
| `+0x254` | busy/fallback flag checked by `Effect_GetUnkFromEfGroup` |

**`efGlobal` at `0x8058C208`** is the bank-install registry, written when an effect `.dat` loads.
Parallel `u32[64]` arrays indexed by group:

| Array base | Per-group contents |
|------------|--------------------|
| `0x8058C208` | parsed `_ptcl` generator-bank pointer |
| `0x8058C308` | bank base effect ID (`*_ref`, which is `group*10000`, not a count) |
| `0x8058C408` | effect-ID manifest pointer (`_ref + 4`) |
| `0x8058C508` | secondary table (`_form + 4`, matched to `_ref` by the `*_form == *_ref` assert) or 0 |
| `0x8058C608` / `0x8058C708` | generator-template count / array, parsed from `_ptcl`, version-branched |

`psInitDataBanks` (`0x8042a734`) populates these from a loaded archive's symbols, with a sibling
installer at `0x8042abe8` sharing its panic strings. **The two stores are independent.**
`psInitDataBanks` never touches `gEffectMgr` or `efModelData`, `efGlobal` is consumed only by the
point-particle path, and model-effect descriptor lookup reads only `gEffectMgr+0x24`.

## Bank loading and data files

Effect banks are HSD archives whose symbols follow a four-symbol-per-group convention:
`<name>_ptcl` (generator-template table), `<name>_texg` (texture group), `<name>_ref`
(the `EffectBankRef` in `effect.h`: base ID `group*10000` followed by the manifest of IDs the bank
provides), and `<name>_form` (secondary model/form table, matched to `_ref` by a `*_form == *_ref`
assertion).

- `Effect_LoadEfCommon` (`0x80235524`) and `Effect_PreloadEfCommon` (`0x802354d8`) load
  `EfCommon.dat` (descriptor at `0x804B51CC`) and register groups 1, 2, 5 (`common1_*`, `common2_*`)
  plus a dynamic group 24. `EfCommon` is the always-resident common bank.
- `Ptcl_LoadEfPtclVehicle` (`0x80235394`) and `Ptcl_PreloadEfPtclVehicle` (`0x80235348`) load
  `EfPtclVehicle.dat` (`vehicle_*`), the per-vehicle particle set.

Effect files flow through the preload system with `file_kind = 3` (`PRELOADFILEKIND_EFFECT`) and
`flags = 0x04` (`PRELOADFLAG_EFFECT`); `EfCommon` and `A2EfCom` also carry `0x40`
(`PRELOADFLAG_PERSIST`) and stay resident. On first access of a preloaded effect file, the
`0x80018110` path archive-inits it and triggers group-symbol resolution plus install. The
`EfMnData` / `EfCoData` names in `preload.h` are m-ex/Melee carryover; in KAR the persistent common
file is `EfCommon.dat`.

Effect files in `iso/files/`:

- Common / persistent: `EfCommon.dat` (`common1_*`, `common2_*`, `yakumono_*`, `efModelData`) and
  `A2EfCom.dat` (Top Ride / 2D, `a2dcommon_*`).
- Per-domain particle banks, one per stage domain: `EfPtclCity`, `EfPtclDesert`, `EfPtclHeat`,
  `EfPtclIce`, `EfPtclPlants`, `EfPtclValley`, `EfPtclStadium`, `EfPtclMenu`, `EfPtclCheck`,
  `EfPtclMachine`, `EfPtclVehicle`.
- Map and misc: `EfMapCity`, `EfMapPlants`, `EfEnemy`, `EfDebug`, `EfEnding`, `EfEnding2d`,
  `A2EfBg00`-`A2EfBg0A` (Top Ride backdrops).

## Recoloring a model effect

A model effect's color is **not** in the HSD material color registers - writing `MObj->mat`
(`MObj+0x0c`) ambient / diffuse / specular has no visible effect. The color comes from a compiled
TEV color expression, and the literal RGBA lives one level deeper than the expression tree:

- `MObj` carries two related pointers: `texp` (`MObj+0x1C`, the **source** `HSD_TExp` expression
  tree) and `tevdesc` (`MObj+0x18`, the **compiled** TEV stage list). `MObjSetupTev` (`0x803faba0`)
  asserts `tevdesc != 0` and calls `HSD_TExpSetupTev` (`0x80424624`) **every render frame**, which
  walks the tree, materializes the constant colors, and emits them via `GXSetTevColor` /
  `GXSetTevKColor`.
- The constant-color node (`HSD_TExpCnst`, type 4, 20 bytes) does not store the RGBA. It holds a
  *pointer* at node `+0x08` to the color source and a live list link at node `+0x04`. The builder
  `MakeColorGenTExp` (`0x803f5f98`) points that at fields of the TObj's `_HSD_TObjTev` struct
  (`TObj+0xA8`): `&tev->constant` (`+0x10`), `&tev->tev0` (`+0x14`), `&tev->tev1` (`+0x18`).

**So the color is owned by `_HSD_TObjTev`, defined in `externals/hoshi/include/obj.h`.** `constant`
is a plain `GXColor`, with `tev0` and `tev1` beside it. Rewrite those bytes and the next frame's
`MObjSetupTev` re-reads them through the node pointer.

```c
// For each part of a model effect's JObj tree (recurse child + sibling, not just the root):
//   jobj -> dobj -> mobj -> tobj (TOBJ list) -> tev (TObj+0xA8, _HSD_TObjTev*)
for (DObj *d = jobj->dobj; d; d = d->next)
    for (TObj *t = d->mobj->tobj; t; t = t->next)
        if (t->tev) {                          // any color-gen TObj
            tint_rgb(&t->tev->constant, r, g, b);
            tint_rgb(&t->tev->tev0,     r, g, b);
        }
```

`RecolorEffectTree` in `mods/hypernova/src/hypernova.c` is the working implementation.

### Combiner input selector encoding

The `_HSD_TObjTev.color_a`-`color_d` and `alpha_a`-`alpha_d` bytes select which register feeds each
combiner input:

| Value | Meaning |
|-------|---------|
| `0x00`-`0x0F` | GX combiner inputs verbatim (`0x08` = texture color, `0x0C` = ONE, `0x0F` = ZERO) |
| `0x80` | `constant` (RGB); `0x81`/`0x82`/`0x83` select `.r`/`.g`/`.b`, `0x84` selects `.a` |
| `0x85` | `tev0` (RGB); `0x86` selects `tev0.a` |
| `0x87` | `tev1` (RGB); `0x88` selects `tev1.a` |
| `0x40` | alpha selectors mirror the above at base `0x40`: `0x43` = `constant.a`, `0x44` = `tev0.a`, `0x45` = `tev1.a` |

Writing these registers is safe because the only things that crash are writes into the **TExp node
tree**: node `+0x04` (`next`) is the list link `HSD_TExpSetReg` and the anim walk follow, and node
`+0x08` (`val`) is a heap pointer, not color bytes; clobbering either corrupts the walk.
`_HSD_TObjTev.constant` is a value field the texture-animation system never touches.

### The whirlwind's combiner

All four materials of the inhale model (group 24, entry 2, read from `EfCommon.dat`) compute
`out = ZERO + lerp(tev0, constant, texC)`: `color_a = 0x85` (tev0), `color_b = 0x80` (constant),
`color_c = 0x08` (texture color, the lerp weight), `color_d = 0x0F` (zero). The visible tint is
therefore a texture-weighted blend of **two** registers: `constant` (near-white cyan) where the
spiral texture is bright, `tev0` (muted blue-grey) where it is dark. `tev1` is unused - no selector
references it and its value is zero. To recolor uniformly, set the RGB of **both** `constant` and
`tev0`; setting one alone leaves a two-tone artifact. Preserve each register's alpha and change only
RGB.

Opacity is not in these registers and is not readily drivable at all on this model. The tev's alpha
selectors are `alpha_a..d = 0x07 0x07 0x07 0x04`, meaning `ZERO/ZERO/ZERO/GX_CA_TEXA`: the stage
takes the texture's alpha and never references `RASA`, which is where the material and vertex alpha
folded in by `MObj.rendermode`'s `RENDER_ALPHA_BOTH` (`3 << 13`) would arrive. So the obvious knob -
`MObj.mat->alpha`, what `HSD_MObjSetAlpha` (`0x803fad80`) writes and what the vanilla item-box spawn
fade (`Box_ApplyAlpha`, `0x80257da0`, which ORs `RENDER_XLU` into `rendermode` first) uses to fade an
ordinary model - has no path to the pixel here.

Fading a whirlwind therefore means either scaling its geometry or rewriting the alpha selectors (for
instance `a = ZERO, b = TEXA, c = 0x43` for `constant.a`) and recompiling the TExp tree, since the
selectors are compiled at load and the raw bytes have no effect on their own. Writing `mat->alpha` is
still safe and per-instance - `MObjLoad` (`0x803f9f04`) allocates an `HSD_Material` per MObj and
memcpys the desc's `0x14` bytes into it, so it can never touch another copy of the same model - it
just may not be visible. Write it *after* the frame's `JObj_AnimAll`, which would otherwise restore
the authored value.

Practical notes:

- Each visible sub-part (the whirlwind has an outer body and a central cone) is its own MObj with its
  own `tev`, and the parts can be separate child joints. Traverse the whole JObj tree, child and
  sibling, not just the root's DObj list.
- A one-shot write persists because the registers are the durable color owners, but writing every
  frame is harmless and overrides any effect-internal reset.
- The color and alpha TExp trees are gated by the top bits of `tev->flags` (the inhale model's
  `flags = 0xC000007F`, both built). A plain NULL check on `tev` is a sufficient write guard, since
  writing an unreferenced register such as `tev1` here is a harmless no-op.

## Boot init

`Effect_Init` (`0x80233908`) runs at boot: `Effect_InitObjAllocs` builds `gEffectMgr` (three object
pools, the per-group descriptor array, the ID-to-group LUT), then it allocates the particle and
generator pools, registers the effect update / draw / cleanup hooks, and loads `EfCommon` plus the
vehicle particle bank.

## Key functions

Names in parentheses are descriptive labels for addresses the symbol map leaves unnamed.

| Address | Name | Role |
|---------|------|------|
| `0x80233908` | `Effect_Init` | boot init |
| `0x802332c4` | `Effect_InitObjAllocs` | build `gEffectMgr` |
| `0x80236c40` | `Effect_SpawnSync` | universal effect spawn (decimal ID) |
| `0x8023af88` | `Effect_SpawnSync_mid` | mid-function label inside `Effect_SpawnSync`, not an entry point |
| `0x80240284` | (anchor resolver) | fills the 52-byte placement descriptor from the varargs |
| `0x802410d4` | `Effect_CheckToCreate` | scene/mode spawn gate |
| `0x8023475c` | (spawn list-node alloc) | allocates the handle/owner node |
| `0x80234a8c` | (per-node kill) | `GObj_Destroy(node+0x5c)`; reached from the efgroup kill |
| `0x80236358` | (efgroup kill) | sweeps the 32 node buckets, matching `node+0x1c` |
| `0x8023ccb4` | `EffectModel_CreateGObj` | build a model-effect GObj + JObj |
| `0x80233e24` | (instance init) | writes the `Effect` state struct |
| `0x80233ddc` | (destructor) | `GObj+0x30` dtor; frees `Effect+0x90` and the pool slot |
| `0x8023dfe0` | (model gx_cb) | wraps `GObj_RenderJObj` |
| `0x8023d9b0` | `EffectModel_AttachToJObj` | sets `Effect+0x1e`/`+0x20` and installs the follow proc |
| `0x8023ce1c` | (follow-joint proc) | priority 11: re-anchors the model root's SRT to the target joint |
| `0x8023e6bc` | (anim loop watcher) | priority 11: arms AOBJ looping once the intro anim ends |
| `0x8023ff80` | `Effect_SetAnimLoop` | the only writer of `Effect.life` |
| `0x80235190` | `Effect_GetModelData` | ID to per-kind `EffectModelDesc *` |
| `0x80234cf0` | `Effect_GetUnkFromEfGroup` | ID to group index via the LUT at `gEffectMgr+0x24C` |
| `0x8023515c` | `Effect_InstallModelData` | write `gEffectMgr+0x24+group*4` from `efModelData` |
| `0x80236144` | `Effect_ResolveModelData` | resolve the `efModelData` symbol for the installer |
| `0x80235524` / `0x802354d8` | `Effect_LoadEfCommon` / `Effect_PreloadEfCommon` | load `EfCommon.dat` |
| `0x80235394` / `0x80235348` | `Ptcl_LoadEfPtclVehicle` / `Ptcl_PreloadEfPtclVehicle` | load `EfPtclVehicle.dat` |
| `0x8042a734` | `psInitDataBanks` | populate `efGlobal` from a bank's `_ptcl`/`_ref`/`_form` symbols |
| `0x8042a874` | `psRelocDataBanks` | file-offset to pointer fixup for a loaded bank |
| `0x8042abe8` | (sibling bank installer) | shares `psInitDataBanks`' panic strings |
| `0x80233b74` / `0x80233ba0` | `Ptcl_Think` / `Ptcl_Think2` | point-particle updater thunks (pool masks 0 / `0xFFFD0000`) |
| `0x804324ec` | (generator update pass) | walks `*0x805de370` driving `Ptcl_SyncGenToJObj`; not a model-effect updater |
| `0x8042a258` | `GObj_RenderJObj` | reads `GObj+0x28`, feeds `HSD_JObjDispAll` |
| `0x8040a7b8` | `HSD_JObjDispAll` | recursive JObj tree render |
| `0x803faba0` | `MObjSetupTev` | per-frame TEV setup; asserts `MObj->tevdesc` |
| `0x80424624` | `HSD_TExpSetupTev` | walk the compiled TEV list |
| `0x80424128` | `HSD_TExpSetReg` | materialize TExp constants into GX registers |
| `0x803f5f98` | `MakeColorGenTExp` | build the color-gen TExp tree from a TObj's `tev` |
| `0x804221e0` / `0x80422120` | `HSD_TExpCnst` / `HSD_TExpTev` | allocate constant (20 B) / TEV-op (108 B) TExp nodes |
| `0x801ad2c4` | `Rider_StartInhale` | spawns the whirlwind from its call site at `0x801ad374` |
| `0x8020d30c` | `EventActor_SpawnEffect` | spawns actor effects by kind, storing the handle at actor `+0xA70`/`+0xA74` |
| `0x8018dba0` | `Machine_SpawnHitEffect` | hit effect: caller-supplied ID, else the fixed `0x5a592` fallback |
| `0x8055D7A0` | `gEffectMgr` | effect-instance manager (SDA global) |
| `0x8058C208` | `efGlobal` | bank-install registry (4x `u32[64]`) |
| `0x804B51CC` | `EfCommon.dat` descriptor | file descriptor used by `Effect_LoadEfCommon` |
