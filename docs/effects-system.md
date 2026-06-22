# Effects system

Kirby Air Ride has **two** distinct visual-effect subsystems that are easy to conflate
because they share the same data files and ID space:

- **Model effects** (this doc) — a standalone `GOBJ` that owns an HSD `JOBJ` model tree: a
  small, self-animating model spawned at a joint. The inhale whirlwind, hit sparks, charge
  flashes, the suction cone, and most "shaped" VFX are model effects. Managed by the **Effect
  module** (code roughly `0x80232xxx`–`0x80241xxx`) and spawned by `Effect_SpawnSync`.
- **Point particles** (see `particle-system.md`) — the HSD point-particle pool: camera-facing
  textured sprites (machine exhaust, sparkle dust) emitted by generators.

A single effect ID can spawn either kind, or both. The two subsystems are wired together at
the data level (an effect bank carries both model templates and particle generators) but render
through completely separate paths. **This doc covers the model-effect side and the effect-ID /
bank machinery shared by both. The point-particle pool has its own doc.**

The model-effect recolor recipe (see *Recoloring a model effect*): the rendered color lives in
each part's `_HSD_TObjTev` (`constant` blended with `tev0`), plain `GXColor` value fields rewritten
in place each frame — used by the hypernova mod for the inhale whirlwind.

## Effect IDs

Effect IDs are **decimal-packed**, not bitwise:

```
id    = group * 10000 + entry
group = id / 10000        // valid model-descriptor groups: 24 .. 36
entry = id % 10000
```

The `/10000` is the standard signed-magic divide (`0x68DC8BAD`, `srawi 12`) seen at the top of
`Effect_GetModelData` (`0x80235190`). Known IDs:

| ID (hex) | decimal | group | entry | effect |
|----------|---------|-------|-------|--------|
| `0x3a982` | 240002 | 24 | 2 | inhale suction cone / whirlwind (`Rider_StartInhale`) |
| `0x3a9ad` | 240045 | 24 | 45 | (spawn-body case) |
| `0x57e47` | 360007 | 36 | 7 | (spawn-body case) |
| `0x5a59f` | 370079 | 37 | 79 | event-actor effect kind 1 (`EventActor_SpawnEffect`) |
| `0x6a592` | 435090 | 43 | 90 | machine hit effect (`Machine_SpawnHitEffect` fixed secondary) |

Note that **two different lookup tables** are keyed off an ID, and they answer different questions:

1. **ID → per-kind descriptor** — `Effect_GetModelData` (`0x80235190`). Decodes the decimal ID,
   bounds-checks `group ∈ [24,37)`, reads the per-group descriptor table pointer at
   `*(gEffectMgr + 0x24 + group*4)`, then indexes `table[entry*8]` (8-byte stride) to get the
   per-kind `EffectModelDesc*`. This is what model-effect creation uses. Among the resident banks
   only **group 24** is populated, from the `EfCommon.dat` `efModelData` symbol (installed by
   `Effect_InstallModelData`, `0x8023515c`); a per-stage bank would have to ship its own
   model-descriptor symbol to fill groups 25..36.
2. **ID → group index** — `Effect_GetUnkFromEfGroup` (`0x80234cf0`, better named
   `Effect_GetGroupFromId`). Returns an `s16` group index from an 8-byte-stride LUT at
   `*(gEffectMgr + 0x24C)`; valid ID range `1..517`. Early-out returns 1 if the busy flag at
   `gEffectMgr + 0x254` is set.

Groups 37+ (e.g. the event-actor IDs `0x5a59f..`) are **not** in the `[24,37)` descriptor table;
they are handled by an explicit special-case branch in the spawn body.

## Spawning: `Effect_SpawnSync`

`Effect_SpawnSync` (`0x80236c40`) is the universal entry point. It is **one** large function
running to ~`0x8023b7e0`; the second map symbol `Effect_SpawnSync @ 0x8023af88` is **spurious**
(it labels a point in the middle of the function's per-ID `switch`). Treat `0x80236c40` as the
only entry.

```c
typedef struct { u32 lo, hi; } EffectHandle;   // returned in {r3, r4}

EffectHandle Effect_SpawnSync(HSD_GObj *parent,  // owner GObj (rider / event actor / machine)
                              int       id,       // group*10000 + entry
                              void     *owner,    // owner-data ptr, passed to the per-ID helper
                              int       joint,    // joint index / anchor-mode selector
                              ...);               // position vec / matrix ptrs; optional f1.. floats
```

Behaviour:

- **Global suppress gate.** If `*(u32*)0x805DD8B8` (`r13 + 2008`) is nonzero, the call returns
  `{0,0}` immediately — effects are globally suppressed during pause / non-gameplay scenes.
- **Create gate.** `Effect_CheckToCreate` (`0x802410d4`) returns 0/1 from a scene/mode byte plus
  two `r13` globals; 0 → return `{0,0}`.
- **Anchor resolve.** The `joint` argument selects an anchor mode (0, 1, 100–112, 200–220). A
  resolver (`0x80240284`) fills a local 52-byte placement descriptor (position default 0, scale
  fields) from the parent's JObj/model so the effect attaches at the right bone.
- **Per-ID construction.** A giant hand-rolled `switch (id)` builds the specific effect (model
  vs particle, sub-models, scale, color). Each case calls into the spawn helper that ultimately
  reaches `EffectModel_CreateGObj` for model effects.
- **Return.** A 64-bit **fire-and-forget handle** `{Effect+0x10, Effect+0x14}` (a packed
  generation counter + pool index), or `{0,0}` on failure. A single call can fan out into up to
  **5** sub-effects; only the primary handle is returned. Most callers (including the inhale)
  **discard the handle** — nothing points back at the spawned effect.

Three call sites pin the signature:

| Caller | parent | id | joint | notes |
|--------|--------|----|----|-------|
| `Rider_StartInhale` (`0x801ad348`) | rider GObj | `0x3a982` | 218 (mouth) | handle discarded |
| `EventActor_SpawnEffect` (`0x8020d390`) | actor GObj | `0x5a59f..a1` | 510 | stores handle at actor +0xA70/+0xA74 |
| `Machine_SpawnHitEffect` (`0x8018dc2c`) | machine GObj | arg + `0x6a592` | 215 | spawns two effects |

A float-vararg convention (`f1..f8` for x/y/z/scale) exists but is **dead for all known callers**
— they pass geometry through the pointer args and clear the FP-arg condition bit.

## Model effects (the `EffectModel` object)

A model effect is a `GOBJ` carrying an HSD `JOBJ` model. It is built by **`EffectModel_CreateGObj`**
(`0x8023ccb4`):

1. Allocate an `Effect` state struct from a `gEffectMgr` object pool.
2. `GObj_Create(entity_class = 25, p_link = 16, p_priority = 0)`.
3. `GObj_AddUserData(gobj, data_kind = 25, dtor = 0x80233ddc, userdata = Effect)`.
4. `Effect_Init(effect, kind, gobj)` — wire the back-pointers and zero the state.
5. `model = Effect_GetModelData(kind)` (`0x80235190`); if the kind is the default `0x5a5b9` the
   model comes from a fixed record instead. Assert/`OSReport "not found effect model data(kind %d)"`
   if missing.
6. `jobj = HSD_JObjLoadJoint(model->jointdesc)` — instantiate the JObj tree from the descriptor's
   joint template (`model + 0x00`).
7. `GObj_AddObject(gobj, obj_kind = 3 /*JOBJ*/, jobj)` → `GObj+0x28` = JObj root.
8. Register the Effect in its per-group active list; store the list node at `Effect+0x08`.
9. Install the render callback: `GObj+0x1C (gx_cb) = 0x8023dfe0`.

The resulting GObj layout (offsets per `obj.h`):

| Offset | Field | Value |
|--------|-------|-------|
| 0x02 | p_link | 16 |
| 0x06 | obj_kind | 3 (`HSD_OBJKIND_JOBJ`) |
| 0x07 | data_kind | 25 |
| 0x1C | gx_cb | `0x8023dfe0` |
| 0x28 | hsd_object | **JObj root** of the model tree |
| 0x2C | userdata | **`Effect` state struct** |
| 0x30 | destructor | `0x80233ddc` |

So `GObj+0x28` is the model and `GObj+0x2C` is the effect state; the `Effect` in turn points back
to the JObj-list node at `Effect+0x08`.

**Rendering.** The gx_cb `0x8023dfe0` is a thin wrapper around `GObj_RenderJObj` (`0x8042a258`),
which reads `gobj->hsd_object` (`GObj+0x28`), maps the GX pass to an HSD render mode
(`HSD_GetRenderPass`), and recurses the whole tree with `HSD_JObjDispAll` (`0x8040a7b8`). OPA vs
XLU pass is chosen by the JObj root flags (`JOBJ_ROOT_OPA`/`JOBJ_ROOT_XLU`); the whirlwind's
translucent spiral renders in the XLU pass. The function installs **no** GX link or think proc
of its own — both come from the entity-class-25 defaults plus the manually-installed gx_cb.

**Destruction.** The destructor `0x80233ddc` (GObj+0x30) frees an optional aux heap block at
`Effect+0x90` (`HSD_Free`) and returns the `Effect` slot to its pool (`HSD_ObjFree`). The JObj
tree itself is owned by the GObj and torn down by `GObj_Destroy`.

**Lifetime / animation.** There is **no per-GObj think callback**. All live effects are advanced
by the global updater `Effect_UpdateAll` (`0x804324ec`), entered each frame via two thunks:

- `Ptcl_Think` (`0x80233b74`) — category mask `0`.
- `Ptcl_Think2` (`0x80233ba0`) — category mask `0xFFFD0000`.

The two masks select the two object pools the manager allocates from. The updater ticks each
effect's JObj material/texture animation (`HSD_JObjAnimAll` — this is what scrolls the spiral)
and its lifetime counter. **Position-follow-joint is done externally by the spawner**, not by the
effect's own think — the inhale code rewrites the whirlwind's world matrix to track the mouth
bone every frame.

## The `EffectModelDesc` (per-kind model descriptor)

`Effect_GetModelData` returns one of these (`0x14` bytes), an array element of the group-24
`efModelData` table. `EffectModel_CreateGObj` reads only `+0x00`; the anim-set arrays are consumed
downstream by JObj animation.

| Offset | Field | Notes |
|--------|-------|-------|
| 0x00 | `JOBJDesc *jointdesc` | model joint template (→ `HSD_JObjLoadJoint`) |
| 0x04 | `void **anim_set0` | NULL-terminated anim-tree pointer array, or NULL |
| 0x08 | `void **anim_set1` | second anim-tree array, or NULL |
| 0x0C | `void *anim_set2` | shape-anim set (NULL in `EfCommon`) |
| 0x10 | list head (`next`/`prev`) | self-referencing empty-list sentinel |

The records are not a packed array (each sits at the end of its own joint/anim data block); the
packed structure is the 8-byte-stride pointer table at `efModelData`. The inhale whirlwind is
`efModelData[2]` (group 24 / entry 2). The default-kind id `0x5a5b9` uses a fixed record instead of
the table. (The exact HSD element type of the `+0x04`/`+0x08` anim-set arrays — `AnimJoint` vs
`MatAnimJoint` — is not labelled definitively.)

## The `Effect` state struct

There are two related runtime structs. The **GObj-userdata effect state** (written by
`Effect_Init`, `0x80233e24`):

| Offset | Field | Notes |
|--------|-------|-------|
| 0x00 | `gobj` | owning GObj |
| 0x04 | `kind` | effect kind/ID |
| 0x08 | `list_node` | per-group list node (set during registration) |
| 0x0C | `life` | lifetime/frame counter, init `-1` (unset/infinite) |
| 0x18 | flags byte | state bits; bit set when scene mode in `[7,11)` |
| 0x28–0x30 | `Vec3` | position/velocity offset, init `{0,0,0}` |
| 0x34 | `f32` | scale/rate, init `1.0` |
| 0x90 | `aux` | optional heap block freed by the destructor |

The **spawn list-node** (allocated by `0x8023475c`, the SpawnSync path) carries the returned
handle and owner pointers: `+0x00` list link, `+0x10/+0x14` handle, `+0x18` owner GObj, `+0x1C`
attach context, `+0x28` kind/state (init 5), `+0x5C` descriptor pointer.

## Manager and registry globals

Two distinct globals back the effect system:

**`gEffectMgr` @ `0x8055D7A0`** — the effect-instance manager (built by `Effect_InitObjAllocs`,
`0x802332c4`). It is an absolute/SDA global, so declare it as a `static` pointer cast in a header
rather than putting it in `link.ld`.

| Offset | Contents |
|--------|----------|
| +0x00 | HSD object pool #1 (capacity 256) |
| +0x24 | per-group `EffectModelDesc*` table array, indexed `[group]` (only group 24 populated by the resident `EfCommon` bank) |
| +0x2C | HSD object pool #2 (capacity 512) — effect slots when sub-effect flag clear |
| +0x58 | HSD object pool #3 (capacity 64) — effect slots when sub-effect flag set |
| +0xBC | handle generation counter (`+1` per spawn) |
| +0xC0.. | per-kind active-list heads (32 buckets) |
| +0x1C0.. | per-kind active-effect counts |
| +0x24C | pointer to the flat 8-byte-stride ID→group LUT |
| +0x254 | busy/fallback flag (checked by `Effect_GetUnkFromEfGroup`) |

**`efGlobal` registry @ `0x8058C208`** — the bank-install registry, written when an effect `.dat`
loads. Parallel `u32[64]` arrays indexed by group:

| Array base | Per-group contents |
|------------|--------------------|
| `0x8058C208` | parsed `_ptcl` generator-bank pointer |
| `0x8058C308` | bank **base effect-ID** (`*_ref` — `group*10000`, not a count) |
| `0x8058C408` | effect-ID manifest pointer (`_ref + 4`) |
| `0x8058C508` | secondary table (`_form + 4`, matched to `_ref` by the `*_form == *_ref` assert) or 0 |
| `0x8058C608` / `0x8058C708` | generator-template count / array (parsed from `_ptcl`, version-branched) |

`psInitDataBanks` (`0x8042a734`) populates these arrays from a loaded archive's symbols (the map's
`0x8042abe8` is a sibling install function sharing its panic strings). **The `efGlobal` registry and
the `gEffectMgr +0x24` descriptor table are independent stores from unrelated install paths**:
`psInitDataBanks` never touches `gEffectMgr`/`efModelData`, and `efGlobal` is consumed only by the
point-particle path (`Ptcl_Alloc`, `Ptcl_TickOne`, `psRenderParticles`). Model-effect descriptor
lookup (`Effect_GetModelData`) reads only `gEffectMgr +0x24` (← `efModelData`).

## Bank loading and data files

Effect banks are HSD archives whose symbols follow a **four-symbol-per-group** convention:
`<name>_ptcl` (generator-template table), `<name>_texg` (texture group), `<name>_ref`
(`{u32 base_id; u32 ids[]}` — the bank's decimal effect-ID range key plus the manifest of effect
IDs it provides; the first word is the base id `group*10000`, **not** a count), `<name>_form`
(secondary model/form table, matched to `_ref` by a `*_form == *_ref` assertion).

- **`Effect_LoadEfCommon` (`0x80235524`) / `Effect_PreloadEfCommon` (`0x802354d8`)** — loads
  `EfCommon.dat` (descriptor at `0x804B51CC`) and registers groups 1, 2, 5 (`common1_*`,
  `common2_*`, …) plus a dynamic group 24. `EfCommon` is the always-resident common bank.
- **`Ptcl_LoadEfPtclVehicle` (`0x80235394`) / `Ptcl_PreloadEfPtclVehicle` (`0x80235348`)** —
  loads `EfPtclVehicle.dat` (`vehicle_*`), the per-vehicle particle set.

**Preload integration.** Effect files flow through the preload system with `file_kind = 3`
(`PRELOADFILEKIND_EFFECT`) and `flags = 0x04` (`PRELOADFLAG_EFFECT`); `EfCommon`/`A2EfCom` also
carry `0x40` (`PRELOADFLAG_PERSIST`) and stay resident. When a preloaded effect file is first
accessed, the `0x80018110` path Archive-inits it and triggers the group-symbol resolution +
install. (The `EfMnData`/`EfCoData` names in `preload.h` are m-ex/Melee carryover — in KAR the
persistent common file is `EfCommon.dat`.)

**Effect file inventory** (`iso/files/`):

- Common / persistent: `EfCommon.dat` (`common1_*`, `common2_*`, `yakumono_*`, `efModelData`),
  `A2EfCom.dat` (Top Ride / 2D, `a2dcommon_*`).
- Per-domain particle banks (`EfPtcl*`, per-stage): `EfPtclCity/Desert/Heat/Ice/Plants/Valley/
  Stadium/Menu/Check/Machine/Vehicle`.
- Map / misc: `EfMapCity`, `EfMapPlants`, `EfEnemy`, `EfDebug`, `EfEnding`, `EfEnding2d`,
  `A2EfBg00..0A` (Top Ride backdrops).

## Recoloring a model effect (the whirlwind)

A model effect's color is **not** in the HSD material color registers — writing
`MObj->mat` (`MObj+0x0c`) ambient/diffuse/specular has no visible effect. The color comes from a
compiled **TEV color expression**, and the literal RGBA lives one level deeper than the
expression tree:

- `MObj` carries two related pointers: `texp` (`MObj+0x1C`, the **source** `HSD_TExp` expression
  tree) and `tevdesc` (`MObj+0x18`, the **compiled** TEV stage list). `MObjSetupTev`
  (`0x803faba0`) asserts `tevdesc != 0` and calls `HSD_TExpSetupTev` (`0x80424624`) **every render
  frame**, which walks the tree, materializes the constant colors, and emits them via
  `GXSetTevColor` / `GXSetTevKColor`.
- The constant-color node (`HSD_TExpCnst`, type 4, 20 bytes) **does not store the RGBA**. It holds
  a *pointer* at node `+0x08` to the color source, and a live list-link at node `+0x04`. The
  builder `MakeColorGenTExp` (`0x803f5f98`) sets that pointer to fields of the TObj's
  `_HSD_TObjTev` struct (`TObj+0xA8`): `&tev->constant` (+0x10), `&tev->tev0` (+0x14),
  `&tev->tev1` (+0x18).

**So the color is owned by `_HSD_TObjTev`, already defined in `obj.h`:** `constant` is a plain
`GXColor` at `+0x10`, with `tev0`/`tev1` at `+0x14`/`+0x18`. Rewrite those bytes and the next
frame's `MObjSetupTev` re-reads them through the node pointer.

```c
// For each part of a model effect's JObj tree (recurse child + sibling, not just the root):
//   jobj -> dobj -> mobj -> tobj (TOBJ list) -> tev (TObj+0xA8, _HSD_TObjTev*)
// rewrite the RGB of tev->constant and tev->tev0, preserving each register's alpha.
for (DObj *d = jobj->dobj; d; d = d->next)
    for (TObj *t = d->mobj->tobj; t; t = t->next)
        if (t->tev) {                          // any color-gen TObj
            tint_rgb(&t->tev->constant, r, g, b);
            tint_rgb(&t->tev->tev0,     r, g, b);
        }
```

The `_HSD_TObjTev.color_a..d` / `alpha_a..d` bytes select which register feeds each combiner input:
`0x00–0x0F` are the GX combiner inputs verbatim (`0x08`=texture color, `0x0C`=ONE, `0x0F`=ZERO),
while `0x80`=`constant`, `0x85`=`tev0`, `0x87`=`tev1` (with `0x8N+1..3` selecting `.g/.b/.a`); the
alpha selectors mirror this at base `0x40` (`0x43`=`constant.a`).

Why this is safe: the only things that crash are writes into the
**TExp node tree** — node `+0x04` (`next`) is the list link that `HSD_TExpSetReg`/anim walk, and
node `+0x08` (`val`) is a heap pointer, not color bytes; clobbering either corrupts the walk.
`_HSD_TObjTev.constant` is a value field that the texture-animation system never touches, so
writing it cannot corrupt anim state.

**The whirlwind's actual combiner** (all four materials of the inhale model, group 24 / entry 2,
read from `EfCommon.dat`): `out = ZERO + lerp(tev0, constant, texC)` — `color_a=0x85` (tev0),
`color_b=0x80` (constant), `color_c=0x08` (texture color = the lerp weight), `color_d=0x0F` (zero).
The visible tint is therefore a texture-weighted blend of **two** registers: `constant`
(≈ near-white cyan) where the spiral texture is bright, `tev0` (≈ muted blue-grey) where it is dark.
**`tev1` is unused** (no selector references it; its value is zero). To recolor uniformly, set the
RGB of **both `constant` and `tev0`** — setting one alone leaves a two-tone artifact. The alpha
equation routes through `constant.a` (`alpha_b=0x43`), which feeds the XLU opacity, so **preserve
each register's alpha** and change only RGB.

Practical notes:

- Each visible sub-part (the whirlwind has an outer body and a central cone) is its **own** MObj
  with its **own** `tev`, and the parts can be separate child joints — traverse the whole JObj tree
  (child + sibling), not just the root's DObj list.
- A one-shot write persists (the registers are the durable color owners), but writing every frame
  is harmless and overrides any effect-internal reset.
- The color/alpha TExp trees are gated by the top bits of `tev->flags` (the inhale model's
  `flags = 0xC000007F`, both built). A plain NULL-check on `tev` is a sufficient write guard, since
  writing an unreferenced register (e.g. `tev1` here) is a harmless no-op.

## Boot init

`Effect_Init` (`0x80233908`) runs at boot: `Effect_InitObjAllocs` builds `gEffectMgr` (three
object pools, the per-group descriptor array, the ID→group LUT), then allocates the particle and
generator pools, registers the effect update/draw/cleanup hooks, and loads `EfCommon` + the
vehicle particle bank.

## Symbol reference

| Address | Name | Role |
|---------|------|------|
| `0x80236c40` | `Effect_SpawnSync` | universal effect spawn (decimal ID; `0x8023af88` is a spurious mid-function label) |
| `0x8023ccb4` | `EffectModel_CreateGObj` | build a model-effect GObj + JObj |
| `0x80235190` | `Effect_GetModelData` | ID → per-kind `EffectModelDesc*` |
| `0x80234cf0` | `Effect_GetGroupFromId` | ID → group index (LUT at `gEffectMgr+0x24C`) |
| `0x802410d4` | `Effect_CheckToCreate` | scene/mode spawn gate |
| `0x8023dfe0` | `EffectModel_Render` | gx_cb; wraps `GObj_RenderJObj` |
| `0x8042a258` | `GObj_RenderJObj` | reads `GObj+0x28`, → `HSD_JObjDispAll` |
| `0x80233ddc` | `EffectModel_Destructor` | GObj+0x30 dtor |
| `0x804324ec` | `Effect_UpdateAll` | per-frame effect/anim/lifetime updater |
| `0x80233b74` / `0x80233ba0` | `Ptcl_Think` / `Ptcl_Think2` | updater thunks (pool masks 0 / 0xFFFD0000) |
| `0x80233908` | `Effect_Init` | boot init |
| `0x802332c4` | `Effect_InitObjAllocs` | build `gEffectMgr` |
| `0x80235524` / `0x802354d8` | `Effect_LoadEfCommon` / `Effect_PreloadEfCommon` | load `EfCommon.dat` |
| `0x80235394` / `0x80235348` | `Ptcl_LoadEfPtclVehicle` / `Ptcl_PreloadEfPtclVehicle` | load `EfPtclVehicle.dat` |
| `0x8042a734` | `psInitDataBanks` | populate `efGlobal` registry from a bank's `_ptcl`/`_ref`/`_form` symbols |
| `0x8042a874` | `psRelocDataBanks` | file-offset → pointer fixup pass for a loaded bank |
| `0x8023515c` | `Effect_InstallModelData` | write `gEffectMgr+0x24+group*4` from `efModelData` (group 24) |
| `0x80236144` | `Effect_ResolveModelData` | resolve `efModelData` symbol → `Effect_InstallModelData` |
| `0x803faba0` | `MObjSetupTev` | per-frame TEV setup; asserts `MObj->tevdesc` |
| `0x80424624` | `HSD_TExpSetupTev` | walk the compiled TEV list |
| `0x80424128` | `HSD_TExpSetReg` | materialize TExp constants → GX registers |
| `0x803f5f98` | `MakeColorGenTExp` | build the color-gen TExp tree from a TObj's `tev` |
| `0x804221e0` | `HSD_TExpCnst` | allocate a constant TExp node (20 bytes) |
| `0x80422120` | `HSD_TExpTev` | allocate a TEV-op TExp node (108 bytes) |
| `0x8055D7A0` | `gEffectMgr` | effect-instance manager (SDA global) |
| `0x8058C208` | `efGlobal` | bank-install registry (4× `u32[64]`) |
