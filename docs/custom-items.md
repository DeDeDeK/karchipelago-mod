# Custom Items

Framework for adding new City Trial item kinds from drop-in HSD archives. A
custom item ships as a self-contained `.dat` dropped into the FST `items/`
folder; the mod discovers it at boot and registers it as a new `ItemKind` that
spawns from the sky and boxes with author-specified weights.

Status: discovery, the descriptor contract, the exported API, the engine splice
(itData growth, kind-ceiling lift, behavior clamp, box/sky-pool weight injection,
event-source-drop injection), and the carve tool are all implemented. Custom
items spawn from every source - box breaks, sky falls, and event drops (Tac,
meteor, broken structures, secret chamber, UFO, Dyna Blade) - and the model may
be any item model carved from `Item.dat`, including the multi-texture, skinned
Hydra/Dragoon legendary pieces. A build with no custom item `.dat`s in `items/`
installs no hooks and leaves vanilla play untouched.

## The vanilla item system this builds on

All item structs and accessors are in `externals/hoshi/include/item.h`.

- **`ItemKind`** — 69 vanilla kinds (`ITKIND_NUM = 69`, indices 0–68): 3 boxes,
  the up/down stat patches, special patches, candy, 11 copy abilities, 12 foods,
  3 traps, Gordo, 6 legendary-machine pieces, 8 fake patches.
- **`itData`** (`item.h:302`) — the per-kind static asset record, `0x18`-byte
  stride, indexed positionally by `ItemKind`: `{ attr, unique_attr, model, anim,
  hurt, trigger }`. The array lives in `Item.dat` (public `itData`) and is grafted
  onto `itCommonDataAll` (`ItCommon.dat`, public `itCommonDataAll`) at
  `itCommonDataAll + 0x8` during load. Reached at runtime via
  `Item_GetItDataPtr(kind)` (`0x80250038`) = `itCommonDataAll.itData + kind*0x18`.
  There is no per-kind filename indirection — the model for kind N is simply the
  Nth entry's `model->j` pointer into the shared `Item.dat` archive.
- **`ItemCommonAttr`** (`item.h:256`) — per-kind scale/cull/land-offset/box-color,
  plus `effect_info` (`PatchEffectInfo`, the authoritative stat-grant list and
  BAD/GOOD/FAKE group).
- **Spawn pipeline** — periodic sky/box drops run
  `CityItemSpawn_Think` (`0x800eb108`) → `CityItemSpawn_GetRandomItemID`
  (`0x800eb7e4`, weighted) → `Item_Create` (`0x8024eef4`). Event/destructible
  drops run `City_SpawnMiscItems` (`0x80104db0`) → `CityItem_GetEventItem`
  (`0x80254114`) → `Item_Create`. Box breaks read a box's `forced_item` (+0x35c)
  or pick from the pool, then `Item_Create` per contained item.
- **Weight tables** — box/sky pools live in `grBoxGeneObj`
  (`item_group_spawn[BOXKIND_NUM]`, parallel `u8 it_kind[68]`/`chance[68]`/`num`);
  event drops live in `grBoxGeneInfo`'s `event_source_drop[]` (one row per kind,
  six `u16` chance columns: Dyna Blade / Tac / meteor / destructible / chamber /
  UFO). Populated at scene init by `CityItemSpawn_InitItemFallChances`
  (`0x800eb374`) and retuned per event by `CityEvent_ModifyItemFallDesc` — the
  same hook points the gating mods use.
- **Hard ceiling** — `Item_Create` asserts `kind < 69` at `0x8024efb4` (so
  vanilla kinds 0–68 pass). The weight arrays are sized `ITKIND_NUM-1` (68). The
  bound must be widened to admit kinds `>= ITKIND_NUM` (69+).

## Drop-in discovery

`items/` is scanned at boot by `CustomItems_Discover` (`item_discovery.c`) using
`FST_ForEachInFolder("items", ".dat", …)` in two passes (count, then index),
mirroring KAR Deluxe's custom-song loader. Each `.dat` becomes a
`CustomItemEntry`:

- `file_entrynum` — FST entry, re-openable across scenes.
- `id_hash` — FNV-1a over the full FST path. Stable identity independent of
  registry order, so per-item enable state (menu / AP gating) survives reboots
  and folder changes.
- `name` — the filename at discovery, superseded by the descriptor's own name
  once the archive is loaded.
- `enabled` / `assigned_kind` — per-item spawn gate, and the `ItemKind` assigned
  in the extended tables for the current round (`-1` until registered).

The registry is a fixed `CUSTOM_ITEM_MAX` (16) array — the practical ceiling
imposed by the 68-entry weight arrays, not an arbitrary limit.

## Descriptor contract

A custom-item `.dat` exports one HSD public symbol, `customItem`, whose address
is a `CustomItemDesc` (`include/custom_items_api.h`). It is a clone model: the new
kind inherits behavior (state class, trigger, hurt, animation) from a vanilla
`base_kind` and optionally overrides the visual `model` and stat-grant
`effect_info`, plus per-source spawn weights (`weight_box[3]`, `weight_event[6]`).
`magic` is `'CITM'` (`0x4349544D`); `version` gates forward compatibility (v2
adds `model_flag`, the model's itData render flag - `0x02000000` for flat panels,
`0x03/0x05/0x0b000000` for the legendary pieces - so skinned models render
correctly). `weight_free` is reserved: the sky/free-fall picker draws from the
union of the three box pools, so `weight_box` already governs sky drops too.

`CustomItems_LoadDescriptor` (`item_registry.c`) performs the load + validate:
`Archive_LoadFile` → `Archive_GetPublicAddress(arc, "customItem")` → magic/version
check. The archive and descriptor are valid only for the current scene
(`Archive_LoadFile` allocates from the per-scene heap, wiped on 3D scene exit), so
registration reloads per round.

## Registration / engine splice

`CustomItemRegistry_RegisterAll` (`item_registry.c`) runs once per City Trial
round via a hook on `CityItemSpawn_Init`'s epilogue (`0x800ec348`) — after
`CityItemSpawn_InitItemFallChances` has filled the spawn pools and the item data
is loaded, before the first `CityItemSpawn` tick. Custom kinds occupy indices
`[ITKIND_NUM, ITKIND_NUM + CUSTOM_ITEM_MAX)`.

1. **Grow `itData[]`** — the `ITKIND_NUM` (69) vanilla entries are snapshotted into a persistent
   `itData[ITKIND_NUM + CUSTOM_ITEM_MAX]` array (re-snapshotted each round because
   the vanilla array is re-allocated into per-scene memory), one cloned entry is
   appended per enabled custom item with `model`/`effect_info` overridden from the
   descriptor, and `itCommonDataAll->itData` is repointed at the grown array. The
   itData lookup in `CityItem_InitData` reads the raw kind from the `ItemDesc` arg,
   so a custom kind resolves to its own appended entry (custom model/effect). The
   overridden `model` points at a per-kind *synthesized* descriptor, not the raw
   `JOBJDesc`: `CityItem_Create`'s part setup (`zz_80252824_`) reads three
   "item-parts" counts at descriptor `+0x8/+0xc/+0x10` and asserts each `<= 11`
   ("item parts model num over!"). Vanilla model descriptors are full-width with
   those counts zero, so each custom kind gets a full-width, zero-filled
   `{ JOBJ *j; int flag; … }` (`stc_model_pair`) with only `j` and `flag` written —
   an 8-byte pair would let `+0x8` read into the next array element and trip the
   assert. `flag` carries the model's itData render flag (`model_flag`, v2+;
   `0x02000000` flat for v1 descriptors).
2. **Lift the ceiling** — `CityItem_Create`'s `cmpwi r4,69` bound at `0x8024efb4`
   is patched to `cmpwi r4, ITKIND_NUM + CUSTOM_ITEM_MAX` once at boot.
3. **Clamp behavior** — the state-handler table (`0x804b6088`, 69 entries) and the
   threshold category are indexed by `ItemData+0x1c` (the instance kind). A custom
   kind has no entry, so a hook at `0x8024eb44` (right after `CityItem_InitData`
   writes `ItemData+0x1c`) rewrites it to the descriptor's `base_kind`. The item
   therefore behaves and is categorized as its base kind while rendering/applying
   from its own `itData` entry.
4. **Inject box/sky weights** — each custom kind is appended in place to the box
   pools (`grBoxGeneObj.item_group_spawn[]`, which the sky picker scans as the
   union of all three colors and the box-break picker scans one color at a time)
   with the descriptor's `weight_box[]`. The 68-wide pools are sparsely filled, so
   a handful of kinds fit without growing them. The per-event re-bias
   (`CityEvent_ModifyItemFallDesc` → `CityItemSpawn_SetEventsItemFallChances`)
   rebuilds these pools, so `CustomItemRegistry_ReinjectPools` re-appends the
   custom kinds at that function's epilogue (`0x800ed7f0`) — the same seam the
   archipelago spawn filter hooks; hoshi chains the two.
5. **Inject event-source weights** — `event_source_drop[]`
   (`grBoxGeneInfo->item_desc`, stride `0x10`: `int it_kind` + six `u16` chance
   columns) is read straight from the table by `_CityItem_GetEventItem` on every
   pick, so the stage's rows are snapshotted into a persistent array, one row per
   custom kind (carrying its `weight_event[6]`) is appended, and the table pointer
   and `event_source_drop_num` are repointed/bumped. This covers Tac, meteor,
   broken structures, secret chamber, UFO, and Dyna Blade drops.

Remaining nuance: a custom `effect_info` that grants novel stats is wired (the
descriptor override repoints the cloned attribute record's `effect_info`), but the
clamp makes the pickup path resolve behavior through the `base_kind`, so pick a
`base_kind` in the intended effect family. The model is rendered/scaled from the
cloned `base_kind`'s attribute record (`scale_factor`, `cull_distance`), so a
legendary model on a flat-panel base renders at the base's scale (≈20% off the
piece's native size) — close enough; a per-model scale override is a future add.

## Authoring a custom item — the carve tool

`scripts/hsd/carve_custom_item.py` carves a model subtree out of `iso/files/Item.dat`
and packs it into a `customItem` `.dat`, reusing the backdrop walker/reloc
machinery (`scripts/hsd/walker.py`, `archive.py`). It emits the `CustomItemDesc`
at data offset 0 with synthetic relocations for the `name` and `model` pointer
fields, carries the source model's render flag into `model_flag`, and lists every
texture (ImageDesc) in the model. The walker is type-complete, so a model of any
complexity carves intact — including the multi-material, skinned legendary pieces
(Hydra/Dragoon, kinds 55–60). Example — a Hydra-modeled item that spawns from
boxes/sky and from broken structures, Tac, and UFOs:

```
uv run python scripts/hsd/carve_custom_item.py iso/files/Item.dat 55 \
    mods/custom_items/assets/items/MegaHydra.dat "Mega Hydra" \
    --base-kind 3 --group good \
    --weight-blue 40 --weight-green 40 --weight-red 40 \
    --ev-destructible 80 --ev-tac 40 --ev-ufo 40
```

`source_kind` (here `55`) is the model to carve; `--base-kind` is the behavior to
clone (default: the source kind). `--weight-*` set the box/sky weights; `--ev-*`
(`dyna`/`tac`/`meteor`/`destructible`/`chamber`/`ufo`) set the event-source drop
weights. `--texture PNG` re-encodes a custom texture (RGB5A3) into one ImageDesc;
on a multi-texture model add `--texture-index N` to choose which slot, and
`--texture-fit cover|contain` to center-crop or letterbox a mismatched aspect
instead of stretching it. Dropping the output in `assets/items/` stages it to the
FST `items/` folder, where it is discovered at boot.

## API

Exported via `Hoshi_ExportMod` for other mods (e.g. archipelago gating/granting
custom items). Items are addressed by `id_hash`:

```c
int  GetCount(void);
u32  GetIdHash(int index);
const char *GetName(int index);
int  IsEnabled(u32 id_hash);             // master toggle AND per-item gate
void SetEnabled(u32 id_hash, int enabled);
int  GetAssignedKind(u32 id_hash);       // ItemKind this round, or -1
void SetPickupHandler(CustomItemPickupFn h); // fired when a custom item is collected
```

`SetPickupHandler` registers a `void (*)(u32 id_hash, const char *name, int player)`
invoked from a hook on `Machine_OnTouchItem` (`0x801db34c`) whenever a custom item
is collected — the collected kind is recovered from `ItemData->itData` (which still
points into the grown array after the behavior clamp), and the collector's slot
comes from `Machine_GetRiderPly`. Because hoshi's hook trampoline does not preserve
registers across the C call, the hook's prologue/epilogue save and restore `r3`
(MachineData), `r4` (ItemData), and `LR` around it. This is how the **Miracle Fruit**
grants Hypernova:
the `hypernova` mod registers a handler that calls `HypernovaAPI.ActivatePlayer` for
the collector when the picked-up item's name matches.

## File layout

- `src/main.c` — `ModDesc`, settings menu (master enable; per-item toggles
  deferred since the set is runtime-discovered).
- `src/custom_items.c` — boot, registry storage, exported API.
- `src/item_discovery.c` — FST scan + path hashing.
- `src/item_registry.c` — descriptor load/validate, the per-round itData /
  box-pool / event-source-drop splice, the per-event pool re-inject, the
  kind-ceiling patch, the behavior-clamp hook, and the pickup hook
  (`Machine_OnTouchItem`) that fires the registered pickup handler.
- `scripts/hsd/carve_custom_item.py` — authoring tool: carve a model out of
  `Item.dat` into a `customItem` `.dat`.
- `include/custom_items_api.h` — public symbol, descriptor layout, API struct.
- `assets/items/` — drop-in folder; staged to the FST root `items/` at build time.
