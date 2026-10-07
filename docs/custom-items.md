# Custom Items

Framework for adding new City Trial item kinds from drop-in HSD archives. A
custom item ships as a self-contained `.dat` dropped into the FST `items/`
folder; the mod discovers it at boot and registers it as a new `ItemKind` that
spawns from the sky and boxes with author-specified weights.

Custom items spawn from box breaks, sky falls, and event drops (Tac, meteor, broken
structures, secret chamber, UFO, Dyna Blade). The two narrower pools in
`grBoxGeneObj` - `sameitem_*` (the "same item" event) and `subsequent_*`
(multi-patch blue boxes) - are left alone, so a custom kind never appears through
those. The stadiums that spawn items from boxes (Destruction Derby, VS King Dedede)
get custom kinds in their box pools too. The model may be any item model carved
from `Item.dat`, including the multi-texture, skinned Hydra/Dragoon legendary
pieces. A build with no custom item `.dat`s in `items/` installs no hooks and
leaves vanilla play untouched.

## Game System

Item structs and accessors are in `externals/hoshi/include/item.h`; the
`ItemKind` enum there is the authoritative list of the 69 vanilla kinds
(`ITKIND_NUM = 69`), of which the legendary machine pieces are 55-60. The spawn
tables (`grBoxGeneObj`, `grBoxGeneInfo`, `ItemEventSourceDrop`) and
`stc_it_common_data` are in `game.h`.

- **`itData`** - the per-kind static asset record, `0x18`-byte stride, indexed
  positionally by `ItemKind`: `{ attr, unique_attr, model, anim_data, hurt, trigger }`.
  The array lives in `Item.dat` (public `itData`) and is grafted onto
  `itCommonDataAll` (`ItCommon.dat`, public `itCommonDataAll`) at
  `itCommonDataAll + 0x8` during load: `Gm_LoadItCommon` (`0x8024feec`) loads
  `ItCommon.dat` into `stc_it_common_data` (`r13+0x7F0` = `0x805dd8d0`) and mirrors its
  `param` member into `stc_item_param`, then `Gm_LoadItem.dat` (`0x8024ff38`) loads
  `Item.dat` with its `itData` public written straight into
  `(*stc_it_common_data)->itData`, in every scene that loads items. Reached at runtime via
  `Item_GetItDataPtr(kind)` (`0x80250038`) = `itCommonDataAll.itData + kind*0x18`.
  There is no per-kind filename indirection - the model for kind N is simply the
  Nth entry's `model->j` pointer into the shared `Item.dat` archive.
- **`ItemCommonAttr`** - per-kind scale/cull/land-offset/box-color, plus
  `effect_info` (`PatchEffectInfo`, the authoritative stat-grant list and
  BAD/GOOD/FAKE group).
- **`unique_attr`** - per-kind tail data the kind's init copies into
  `ItemData.unique_attr`, a 0x38-byte buffer from `CityItem_AllocUniqueAttr`. 53 of
  the 69 kinds share a one-int template carried over from the box family's init,
  which nothing reads back; only the three box kinds fill a real layout
  (`ItemUniqueAttr.box`). A clone inherits its base kind's.
- **Spawn pipeline** - periodic sky/box drops run `CityItemSpawn_Think`
  (`0x800eb108`) -> `CityItemSpawn_GetRandomItemID` (`0x800eb7e4`, weighted) ->
  `CityItem_Create` (`0x8024eef4`). Event/destructible drops run
  `City_SpawnMiscItems` (`0x80104db0`) -> `CityItem_GetEventItem` (`0x80254114`)
  -> `CityItem_Create`. Box breaks read a box's `forced_item` (+0x35c) or pick
  from the pool, then `CityItem_Create` per contained item.
- **Weight tables** - box/sky pools live in `grBoxGeneObj`
  (`item_group_spawn[BOXKIND_NUM]`, parallel `u8 it_kind[68]`/`chance[68]`/`num`);
  event drops live in `grBoxGeneInfo`'s `event_source_drop[]` (one row per kind,
  six chance columns: Dyna Blade / Tac / meteor / destructible / chamber / UFO).
  `CityItemSpawn_Init` (`0x800ebf70`) fills the pools once per scene: from the
  stage's `item_desc` through `CityItemSpawn_InitItemFallChances` (`0x800eb374`) in
  City Trial, or from `pool_a`/`pool_b` through `CityItemSpawn_InitStadiumPoolsA/B`
  in a stadium, whose stage has no `item_desc` and so no `event_source_drop`.
  During a City Trial round the pools change twice per event. At event start
  `CityEvent_ModifyItemFallDesc` (`0x800ed784`) ->
  `CityItemSpawn_SetEventsItemFallChances` (`0x800eb568`) edits them in place,
  overwriting the chance of each of the event's kinds and appending the ones that
  are missing. At event end `CityEvent_RestoreItemFallDesc` (`0x800ed800`) calls
  `InitItemFallChances` again, which memsets `grBoxGeneObj` and rebuilds every pool
  from the stage table. Neither path writes `event_source_drop` or its count.
- **Hard ceiling** - `CityItem_Create` asserts `0 <= kind < 69` at `0x8024efb4`. The
  pool arrays are sized `ITKIND_NUM-1` (68). The bound must be widened to admit
  kinds `>= ITKIND_NUM`.

## Drop-In Discovery

`items/` is scanned once at boot by `CustomItems_Discover` (`item_discovery.c`)
with `FST_ForEachInFolder("items", ".dat", ...)`. Each file is loaded and its
descriptor validated - the `customItem` symbol, magic, version, a name, and a
`base_kind` in `[0, ITKIND_NUM)`. A file that fails is reported with the reason and
never registered, so a malformed `.dat` costs one message at boot and nothing per
round; the file cannot change while the game runs, so nothing checks it again. A
valid one becomes a `CustomItemEntry`:

- `file_entrynum` - FST entry, re-openable across scenes.
- `id_hash` - `hash_32_str` (CRC32) of the full FST path, never 0 since the API
  returns 0 for "not found". Stable identity independent of registry order.
- `name` - the descriptor's display name, the handle a consumer mod binds by. It
  must be known before any round registers anything, and for an item held
  disabled nothing ever registers it, which is why discovery reads the archive at
  all. For the whole of `OnBoot` hoshi redirects `HSD_MemAlloc` to a bump allocator
  and `Archive_LoadFile` to a matching persistent loader, so discovery uses the
  ordinary loader and brackets each read in an arena mark/release - a mark is the
  arena's next address and a release rewinds to it. Boot's high-water mark is
  therefore one `.dat`, not one per drop-in; holding them would cost the full file
  size of every item permanently off the ~10.2 MB HSD heap.
- `api_enabled` - the spawn gate, written by `SetEnabled` and defaulting open, so
  an item nobody gates spawns as soon as it is discovered. Closed keeps the item
  out of the round entirely. There is one gate per item shared by every consumer,
  so two consumers gating the same item is last-writer-wins.
- `assigned_kind` - the `ItemKind` assigned in the extended tables for the current
  scene (`-1` until registered).

Discovery also reports any weight past the engine's range (see the descriptor
contract) once, at boot.

The registry is a fixed `CUSTOM_ITEM_MAX` (16) array; files past it are counted and
reported in one line after the scan. Every registered kind with a box weight takes
a slot in each 68-wide box pool. City Trial's fullest pool (blue) holds 30 vanilla
kinds at init and at most 38 once the event kinds are appended, so 16 custom kinds
fit with room to spare; the append drops a kind silently once a pool is full.

## Descriptor Contract

A custom-item `.dat` exports one HSD public symbol, `customItem`, whose address
is a `CustomItemDesc` (`src/custom_items.h`, which carries the field layout - it
is the `.dat` file format, not the mod-to-mod API, so it does not live in the
public header). It is a clone model: the new kind inherits behavior (state class,
trigger, hurt, animation) from a vanilla `base_kind` and optionally overrides the
visual `model`, stat-grant `effect_info`, render `model_flag` (`0x02000000` for
flat panels, `0x03/0x05/0x0b000000` for the skinned legendary pieces) and `scale`,
plus per-source spawn weights (`weight_box[3]`, `weight_event[6]`). `magic` is
`'CITM'` (`0x4349544D`).

`version` is a layout stamp, not a compatibility ladder. The `.dat`s and the DOL
are separate Riivolution files and a player can end up with a stale `items/`
folder, so discovery rejects any descriptor whose version is not exactly
`CUSTOM_ITEM_DESC_VERSION` rather than reading it against the wrong offsets.
Every producer writes the current version, so changing the layout means bumping
the constant in `src/custom_items.h`, `scripts/hsd/carve_custom_item.py`,
`scripts/authoring/make_ap_star_pieces.py` and `scripts/authoring/make_ap_box.py`,
then regenerating every shipped `mods/*/assets/items/*.dat`.

The sky/free-fall picker draws from the union of the three box pools, so
`weight_box` governs sky drops too and there is no separate free-fall weight.
Weights are relative. The box pools store a chance as a `u8`, so `weight_box`
saturates at 255. `_CityItem_GetEventItem` (`0x800ebe44`) reads the event columns
sign-extended (`lhax`/`extsh`), so a value of 0x8000 or more would subtract from the
column's total; `weight_event` saturates at 0x7fff for that reason. The
BAD/GOOD/FAKE group is not a standalone field: it is read from the effect record
(`PatchEffectInfo.group`), so it follows `base_kind` (or the `effect_info`
override).

One flag is defined. `CUSTOM_ITEM_FLAG_NO_MAT_ANIM` says the supplied `model` is
not the base kind's, so the base kind's material animation must not be bound to
it. A material animation drives the diffuse/ambient/alpha tracks of the materials
it was authored against; pointed at a foreign model it repaints whatever material
sits in the same tree position. The legendary pieces are the sharp case - Hydra's
animates diffuse R/G/B over a 240-frame loop, which cycles a solid-colored
replacement model through colors that are not its own. Set the flag whenever the
model comes from somewhere other than `base_kind`; leave it clear when the model
was carved from the base kind itself.

`mat_anim` is the other answer to the same problem: a `MatAnimJointDesc*` bound
in place of the base kind's, in every anim slot. Where the flag drops the
animation, this replaces it, so a carve that rewrote the textures the base kind's
animation would have swapped can carry its own copy of that animation with the
image table repointed at the rewritten textures - which is how the AP Box keeps
the blue box's crack sequence on six recolored faces. The flag wins if both are
set. Leave it NULL to inherit.

`joint_anim` is the same argument for the other half of the animation record: an
`AnimJointDesc*` bound in place of the base kind's, in every anim slot. A joint
animation binds by tree position too, so the base kind's drives whatever joint of
a foreign model sits where its own animated joint did. Hydra's piece animation
squashes its second joint's X and Y between 1.0 and 0.7 on a 30-frame half period,
which on a replacement model with geometry at that position throbs the whole
thing. Leave it NULL to inherit; supply a tree mirroring the model's joints to
replace it. Looping is not the animation's to decide - `ItemGObj_BindStateAnim`
(`0x80251894`) loops every bound `AObj` when bit 30 of the inherited
`ItemAnimEntry` flags is set. The state script still comes from the base kind
either way; it drives the item's hurtbox and effect timing, so dropping it would
change behavior, not just looks.

Outside `OnBoot` the archive and descriptor are valid only for the current scene
(`Archive_LoadFile` allocates from the per-scene heap, wiped on 3D scene exit), so
registration reloads each enabled item's archive every round. It does not validate
again.

## Registration / Engine Splice

`CustomItemRegistry_RegisterAll` (`item_registry.c`) runs from a hook on
`CityItemSpawn_Init`'s epilogue (`0x800ec348`), once per scene that spawns items
from boxes - every City Trial round, and the Destruction Derby and VS King Dedede
stadiums. The pools are filled and the item data is loaded by then, and no spawn
tick has run. Custom kinds occupy indices `[ITKIND_NUM, ITKIND_NUM + CUSTOM_ITEM_MAX)`,
assigned in registry order to the items whose gate is open. A consumer that wants
an item in City Trial only closes its gate in other scenes.

1. **Grow `itData[]`** - the 69 vanilla entries are snapshotted into a persistent
   `itData[ITKIND_NUM + CUSTOM_ITEM_MAX]` array (re-snapshotted each scene because
   `Gm_LoadItem.dat` rewrites `itData` in every scene with items), one cloned entry is
   appended per enabled custom item with `model`/`effect_info` overridden from the
   descriptor, and `itCommonDataAll->itData` is repointed at the grown array. The
   itData lookup in `CityItem_InitData` reads the raw kind from the `ItemDesc` arg,
   so a custom kind resolves to its own appended entry. The overridden `model`
   points at a per-kind *synthesized* `ItemModelDesc`, not the raw `JOBJDesc`:
   `CityItem_Create`'s part setup (`Item_InitPartsModel`, `0x80252824`) reads three
   "item-parts" counts at descriptor `+0x8/+0xc/+0x10` and asserts each `<= 11`
   ("item parts model num over!"), so the synthesized descriptor is the full
   `{ j, flag, parts[3] }` with `parts[]` left zero and `flag` carrying the
   descriptor's `model_flag`. A kind that sets `NO_MAT_ANIM` or supplies a
   `mat_anim` / `joint_anim` also gets its own `anim_data`: the base kind's slots
   copied with `mat_anim` nulled or repointed and/or `joint_anim` repointed, which is
   what `CityItem_StateChange` (`0x8024f488`) hands to `ItemGObj_BindStateAnim`
   (`0x80251894`). Only `ITKIND_ALLUP` has two anim slots; every other kind's array
   holds one, so exactly as many slots as the base kind owns are copied.
2. **Lift the ceiling** - `CityItem_Create`'s `cmpwi r4,69` bound at `0x8024efb4`
   is patched to `ITKIND_NUM + CUSTOM_ITEM_MAX` once at boot.
3. **Clamp behavior** - the 69-entry state-handler table `stc_item_state_tbl`
   (`0x804b6088`) is indexed by `ItemData.kind`, so a custom kind would read past it.
   A hook at `0x8024eb44` (right after `CityItem_InitData` writes `ItemData.kind`)
   rewrites it to the descriptor's `base_kind`, so the item behaves and is
   categorized as its base kind while rendering/applying from its own `itData`
   entry. The 25-entry threshold-category table at `0x804b5f18` is scanned
   linearly by value rather than indexed, so it cannot overrun either way; the
   clamp is what gives a custom kind a real category instead of the `-1` a kind
   above 68 would fall through to. The hook's prologue and epilogue carry `r0` and
   `r6` across the call - `CityItem_InitData` loads both (the threshold scan's
   count and table pointer) before the patch site and reads them after it, and the
   trampoline's `bl` destroys both.
4. **Append box/sky weights** - each custom kind with a nonzero `weight_box` entry
   is appended to that color's pool (`grBoxGeneObj.item_group_spawn[]`, which the
   sky picker scans as the union of all three colors and the box-break picker scans
   one color at a time). Event start edits the pools in place, so the entries
   survive it. Event end rebuilds them from the stage table, so the call to
   `InitItemFallChances` in `CityEvent_RestoreItemFallDesc` (`0x800ed878`) is
   replaced with one that makes the call and appends the custom kinds again. Both
   appends follow a full rebuild, so a custom kind is never already in the pool;
   anything hooked on `InitItemFallChances`'s own exit (the archipelago spawn
   filter) sees the pools before the custom kinds go in, at round start and event
   end alike.
5. **Append event-source weights** - `event_source_drop[]`
   (`grBoxGeneInfo->item_desc`, stride `0x10`: `int it_kind` + six chance columns)
   is read straight from the table by `_CityItem_GetEventItem` on every pick, so the
   stage's rows are snapshotted into a persistent array, one row per custom kind
   with any event weight (carrying its `weight_event[6]`) is appended, and the table
   pointer and `event_source_drop_num` are repointed and bumped. Nothing on the event
   paths writes either back, so the repoint holds for the round. This covers Tac,
   meteor, broken structures, secret chamber, UFO, and Dyna Blade drops. The
   stadiums have no `item_desc`, so there a custom kind reaches only the box pools.

**Effect and scale overrides.** On pickup, `Machine_OnTouchItem` (`0x801db34c`)
applies stat grants generically from the instance's `effect_data`
(`ItemData+0x140`, copied from the kind's `attr->effect_info` by
`ItemGObj_CopyCommonAttr`) via `ItemGObj_GetEffectData` (`0x80252e90`), then reads
the instance kind (`ItemData.kind`) to drive category-specific reaction/SFX.
Because the descriptor's `effect_info` override repoints the cloned attribute
record's `effect_info`, a custom item can grant any combination of stat entries;
but the behavior clamp routes the category reaction through `base_kind`, so pick a
`base_kind` in the intended family (e.g. a stat patch). The clamp also means a
pickup counts as `base_kind` in the player's item-collect stats:
`Ply_IncrementItemCollectNum` (called at `0x801db928`) reads `ItemData.kind`. The
model is rendered at the cloned attribute record's `scale_factor`, so a model
carved onto a differently-scaled base kind (a legendary piece on a flat-panel base)
can render off its native size; the descriptor's `scale` multiplies `scale_factor`
on the clone to correct it. `attr` is cloned only when `effect_info` or `scale` is
overridden.

## Authoring a Custom Item

`scripts/hsd/carve_custom_item.py` carves a model subtree out of `iso/files/Item.dat`
and packs it into a `customItem` `.dat`, using the type-aware walker and relocation
machinery in `scripts/hsd/walker.py` and `archive.py`. It emits the `CustomItemDesc`
at data offset 0 with synthetic relocations for the `name` and `model` pointer
fields, carries the source model's render flag into `model_flag`, and lists every
texture (ImageDesc) in the model. The walker is type-complete, so a model of any
complexity carves intact - including the multi-material, skinned legendary pieces.

```
uv run python scripts/hsd/carve_custom_item.py iso/files/Item.dat 55 \
    mods/<owning_mod>/assets/items/MegaHydra.dat "Mega Hydra" \
    --base-kind 3 --scale 1.2 \
    --weight-blue 40 --weight-green 40 --weight-red 40 \
    --ev-destructible 80 --ev-tac 40 --ev-ufo 40
```

`source_kind` (here `55`) is the model to carve; `--base-kind` is the behavior to
clone (default: the source kind), and the item's group follows it. `--scale`
multiplies the render size when the carved model's native size differs from the
base kind's (default 1.0 = inherit). `--weight-*` set the box/sky weights (blue
defaults to 10, green and red to 0; 0-255); `--ev-*`
(`dyna`/`tac`/`meteor`/`destructible`/`chamber`/`ufo`) set the event-source drop
weights (default 0; 0-32767). `--texture PNG` re-encodes a custom texture (RGB5A3)
into one ImageDesc; on a multi-texture model add `--texture-index N` to choose
which slot, and `--texture-fit cover|contain` to center-crop or letterbox a
mismatched aspect instead of stretching it. `--no-effect` appends a zero-entry
`PatchEffectInfo` and points the descriptor at it, so the carve keeps the base kind's
look, state script and pickup SFX while granting nothing; `--effect-group` picks the
BAD/GOOD/FAKE group that record carries (GOOD by default).

A model `Item.dat` does not hold has to be generated instead of carved, and the
descriptor is the same either way - `scripts/authoring/make_ap_star_pieces.py` builds the
Archipelago Star's six spheres that way, emitting the `CustomItemDesc`, a generated
JOBJ tree and its own zero-entry `PatchEffectInfo` into one archive.

A third shape is a carve the script then rewrites: `scripts/authoring/make_ap_box.py`
carves `ITKIND_BOXBLUE` with its material animation, rewrites the textures, and ships
its own copy of the animation through `mat_anim`.

`mods/*/assets/` is copied to the disc root by the ordinary asset step, so any mod's
`assets/items/*.dat` lands at `items/` on disc with no packaging change, and this mod
discovers it there without knowing the mod exists. An item whose pickup does something
therefore ships with the mod that implements it - `mods/hypernova/assets/items/MiracleFruit.dat`
is the Miracle Fruit, `mods/ap_star/assets/items/ApSphere*.dat` the Archipelago Star's
six spheres, and `mods/archipelago/assets/items/ApPatch.dat` / `ApBox.dat` the Archipelago
location patches and the box that drops them. An item that needs no code at all is not a mod:
dropping its `.dat` into any built mod's `assets/items/` registers it, and it spawns and
behaves as its `base_kind`.
This mod ships no items of its own and has no `assets/`.

## API

`CustomItemsAPI` (`include/custom_items_api.h`) is exported via `Hoshi_ExportMod`
for other mods (e.g. archipelago gating/granting custom items), which import it
with `CUSTOM_ITEMS_MOD_NAME` and `CUSTOM_ITEMS_API_MAJOR`/`_MINOR`; `ModDesc`
carries the same version, which is what makes hoshi's import check able to reject
a stale consumer. Items are addressed by `id_hash`, not registry index, so a
consumer's binding survives a folder change. Beyond the enumeration accessors it
offers `SetEnabled` (the spawn gate), `GetAssignedKind` (this scene's `ItemKind`,
or -1), `AddPickupHandler`, and `GetItemKind`, which answers an item instance's real
`ItemKind` - the behavior clamp leaves a custom item's `ItemData.kind` holding its base
kind, so a consumer patching a game function that takes the item cannot tell the two
apart without it. `assigned_kind` is cleared for every item at
`On3DLoadStart`, so it answers -1 in any scene that has not registered.

A pickup handler is a `void (*)(u32 id_hash, int player)` invoked
once each time a custom item is collected. `Machine_OnTouchItem` returns early,
leaving the item in place to be touched again next frame, when the rider cannot
take the item's ability or power-up; it reaches its one call of
`ItemGObj_BeginPatchToss` (`0x801dba48`) only when the touch collects. That call is
replaced with one that dispatches the handlers and then makes the call. The
collected kind is recovered from `ItemData->itData`, which still points into the
grown array after the behavior clamp, and the collector's slot comes from
`Machine_GetRiderPly` (`0x801caa40`), which answers 5 for a riderless machine;
those pickups, which only a direct grant can produce, are dropped rather than
dispatched, so a handler always sees a real 0-4 slot. Up to four consumer mods may
subscribe (`CUSTOM_ITEM_PICKUP_HANDLERS_MAX`); each is invoked on every pickup.
This is how the **Miracle Fruit** grants Hypernova: the `hypernova` mod ships the
item's archive in its own `assets/items/`, resolves its id hash by the name
`Miracle Fruit`, and adds a handler that activates Hypernova for the collector when
that hash is picked up.

## File Layout

`src/main.c` is just the `ModDesc`. The mod has no settings menu and contributes no
entry to hoshi's: everything dropped into `items/` is discovered and enabled, and
the only spawn gate is `api_enabled`, owned by consumer mods.

`src/custom_items.c` holds boot, registry storage, the per-scene reset, the pickup
handler table and the exported API; `src/custom_items.h` the descriptor contract
and the internal declarations; `src/item_discovery.c` the FST scan, descriptor
validation and path hashing; `src/item_registry.c` the per-scene itData /
box-pool / event-source-drop splice, the event-end re-append, the kind-ceiling
patch, the behavior-clamp hook, and the pickup dispatch.
