# Custom Items

A registry that adds new City Trial item kinds from drop-in archives. Each custom item is one
`.dat` in the disc's `items/` folder, which any mod's staged assets can fill; at boot the mod finds
them all, and in every scene that spawns items it appends each one to the engine's item tables as
a new `ItemKind` that falls from the sky, comes out of boxes and drops from events with weights its
archive chooses. A custom item clones a vanilla kind's behavior and overrides its look and what it
grants. The mod ships no items of its own, has no settings page, and with no archive found installs
no hooks at all.

## The engine side

Everything the mod widens is City Trial's item machinery:

- **`itData`** - the per-kind asset record, `{attr, unique_attr, model, anim_data, hurt,
  trigger}` at a 0x18 stride, indexed by `ItemKind`. The array is `Item.dat`'s `itData` public.
  `Gm_LoadItCommon` (0x8024feec) loads `ItCommon.dat` into `stc_it_common_data`, and the `Item.dat`
  loader at 0x8024ff38 writes the `itData` public straight into
  `(*stc_it_common_data)->itData` (`itCommonDataAll+0x8`), in every scene that loads items.
  `Item_GetItDataPtr` (0x80250038) indexes it, so the model for a kind is just that entry's model
  pointer into the shared archive.
- **`ItemCommonAttr`** (`itData.attr`) - scale, cull and box color, and `effect_info`, a
  `PatchEffectInfo` that is the item's whole stat-grant list and its BAD / GOOD / FAKE group.
- **The spawn paths.** Sky drops and box contents run `CityItemSpawn_Think` (0x800eb108) ->
  `CityItemSpawn_GetRandomItemID` (0x800eb7e4) -> `CityItem_Create` (0x8024eef4); a box with a
  `forced_item` spawns that instead. Event and destructible drops run `City_SpawnMiscItems`
  (0x80104db0) -> `CityItem_GetEventItem` (0x80254114), whose picker `_CityItem_GetEventItem`
  (0x800ebe44) rolls over the event table.
- **The pools.** `grBoxGeneObj.item_group_spawn[]` holds one pool per box color - parallel `u8`
  `it_kind[68]` and `chance[68]` arrays and a count. A box draws from its own color's pool and the
  sky from all three at once. Event drops read `grBoxGeneInfo.item_desc->event_source_drop[]`, one
  row per kind with six chance columns: Dyna Blade, Tac, meteor, destructible, secret chamber and
  UFO. `CityItemSpawn_Init` (0x800ebf70) fills the pools once per scene - from the stage's
  `item_desc` through `CityItemSpawn_InitItemFallChances` (0x800eb374) in City Trial, from the
  stadium pools in Destruction Derby and VS King Dedede, whose stage has no `item_desc` and so no
  event table. In a round each event changes the pools twice: at its start
  `CityEvent_ModifyItemFallDesc` (0x800ed784) edits them in place, and at its end
  `CityEvent_RestoreItemFallDesc` (0x800ed800) calls `InitItemFallChances` again, which clears and
  rebuilds every pool from the stage table. Neither touches the event table.
- **The ceiling.** `CityItem_Create` asserts `kind < 69` (`cmpwi r4,69` at 0x8024efb4), and
  `stc_item_state_tbl` (0x804b6088), the per-kind state descriptors, has exactly `ITKIND_NUM`
  entries indexed by `ItemData.kind`.

The narrower `sameitem_*` pool (the "same item" event) and the `subsequent_*` pool (multi-patch
blue boxes) are left alone, so a custom kind never comes out of either.

## Discovery

At `OnBoot` the mod walks the FST `items/` folder for `.dat`s. Each is loaded and its descriptor
checked: the `customItem` public, the `CITM` magic, the exact current version, a name, and a
`base_kind` below `ITKIND_NUM`. A file that fails is reported with its reason once and never
registered; the disc cannot change while the game runs, so nothing checks it again. A weight past
the engine's range is reported once here and clamped at registration. Files past the registry's
`CUSTOM_ITEM_MAX` are counted and reported in one line.

A valid file becomes a registry entry holding its FST entry number, its **id hash** - the CRC32
(`hash_32_str`) of its full FST path, never 0, which is the item's identity for consumers whatever
order the folder scans in - its display name, its spawn gate and the `ItemKind` it was given this
scene.

The archive is read at boot only for the name, which consumers bind by and which must be known
before any scene registers anything - a gated item never registers at all. For the whole of
`OnBoot` hoshi redirects `HSD_MemAlloc` and `Archive_LoadFile` to a persistent bump arena, so each
read is bracketed by `HSD_ArenaMark` / `HSD_ArenaRelease` and boot's high-water mark is one
archive rather than the sum of them. Outside `OnBoot` an archive lives on the scene heap, so
registration reloads each enabled item's archive every scene, without validating it again.

## The descriptor

A custom item `.dat` exports one public, `customItem`, a `CustomItemDesc`. The struct is the file
format, private to the mod, not part of its API.

| Field | Meaning |
|---|---|
| `magic`, `version` | `'CITM'` and `CUSTOM_ITEM_DESC_VERSION`; any other version is rejected rather than read at the wrong offsets |
| `name` | Display name, the handle consumers find the item by |
| `base_kind` | The vanilla `ItemKind` whose behavior - state class, hurt, trigger, state script, animation - the item clones |
| `flags` | `CUSTOM_ITEM_FLAG_NO_MAT_ANIM` |
| `model`, `model_flag` | Replacement model and its render flag; NULL inherits the base kind's |
| `effect_info` | Replacement `PatchEffectInfo`, so the stat grants and group; NULL inherits |
| `scale` | Multiplier on the base kind's render scale; 0 or 1.0 inherits |
| `weight_box[3]` | Blue, green, red box-pool weights, which also govern sky drops; saturate at 255 |
| `weight_event[6]` | Event-table weights per column; saturate at 0x7fff |
| `joint_anim`, `mat_anim` | Replacement joint and material animations, bound in every anim slot; NULL inherits |

Weights are relative, and 0 keeps an item out of that pool. The box pools store a chance as a
`u8`, hence 255; `_CityItem_GetEventItem` reads the event columns sign-extended, so a weight of
0x8000 or more would subtract from the column's total, hence 0x7fff.

The animation overrides exist because animations bind by tree position. Pointed at a foreign
model, the base kind's material animation repaints whatever material sits where its own did - a
Hydra piece's cycles diffuse color over a 240-frame loop, which turns a solid-colored replacement
into a rainbow - and its joint animation drives whatever joint sits where its animated one did.
`NO_MAT_ANIM` drops the material animation; it should be set whenever the model did not come from
the base kind, and wins over `mat_anim`. `mat_anim` replaces it instead, which is how a carve that
rewrote the base kind's textures keeps that animation pointed at the new images. `joint_anim`
replaces the joint animation. Looping is not the animation's to decide: `ItemGObj_BindStateAnim`
(0x80251894) loops every bound `AObj` when the inherited anim entry's flags say so. The state
script always comes from the base kind, since it drives the item's hurtbox and effect timing.

## Registering each scene

A hook on `CityItemSpawn_Init`'s epilogue (0x800ec348) registers the items in every scene that
fills box pools - every City Trial round and the two box stadiums - after the pools and item data
are loaded and before any spawn tick. Custom kinds take `ITKIND_NUM` upward, in registry order,
for the items whose gate is open.

1. **Grow `itData`.** The vanilla entries are copied into a static array with room for every
   custom kind - again each scene, since the loader rewrites `itData` every time - and one entry is
   appended per item, cloned from its base kind with the overrides applied, and
   `(*stc_it_common_data)->itData` is repointed at it. A replacement model goes in through a
   synthesized `ItemModelDesc` `{j, flag, parts[3]}` with zero part counts, because
   `Item_InitPartsModel` (0x80252824) reads three part counts from the descriptor and asserts each
   is at most 11. An animation override gets its own copy of the base kind's anim entries - two for
   `ITKIND_ALLUP`, one for every other kind. `attr` is cloned only when `effect_info` or `scale` is
   overridden.
2. **Lift the ceiling.** `cmpwi r4,69` at 0x8024efb4 is patched once at boot to the custom
   ceiling.
3. **Clamp the behavior.** A hook at 0x8024eb44, in `CityItem_InitData` (0x8024eaf4) right after
   it writes `ItemData.kind`, rewrites a custom kind to its base kind. The item then indexes
   `stc_item_state_tbl` and gets its threshold category as its base kind, while still rendering
   and granting from its own `itData` entry, which `CityItem_InitData` looked up by the raw kind.
   `CityItem_InitData` loads `r0` and `r6` before the site and reads them after, so the hook saves
   both across its call.
4. **Append the box pools.** Each item with a box weight is appended to that color's pool. Event
   start edits the pools in place, so the entries survive it; event end rebuilds them, so the call
   to `InitItemFallChances` inside `CityEvent_RestoreItemFallDesc` (at 0x800ed878) is replaced with
   one that makes the call and appends again. Both appends follow a full rebuild, so a kind is
   never already present, and a hook on `InitItemFallChances`'s own exit sees the pools before the
   custom kinds go in. A pool already holding 68 entries drops the kind silently; City Trial fills
   its fullest pool to at most 38, so the registry fits.
5. **Append the event table.** The picker reads `event_source_drop` straight from the table on
   every pick, so the stage's rows are copied into a static array, one row per item with any event
   weight is appended, and the table pointer and `event_source_drop_num` are repointed. Nothing on
   the event paths writes either back. A stadium has no table, so there a custom kind reaches only
   the box pools.

## Pickup

`Machine_OnTouchItem` (0x801db34c) applies stat grants from the instance's effect record, copied
from `attr->effect_info`, and dispatches its pickup reaction and sound on `ItemData.kind`. An
`effect_info` override can therefore grant any stat combination, but the reaction follows the base
kind, so `base_kind` should come from the intended family. The clamp also means a pickup counts as
the base kind in `Ply_IncrementItemCollectNum` (0x8022fbcc).

`Machine_OnTouchItem` returns early, leaving the item to be touched again next frame, when the
rider cannot take the item's ability or power-up; its one call to `ItemGObj_BeginPatchToss`
(0x80256254), at 0x801dba48, is reached only when the touch collects. That call is replaced with
one that fires the pickup handlers and then makes the call. The collected kind is recovered from
`ItemData.itData`, which still points into the grown array after the clamp, and the collector from
`Machine_GetRiderPly` (0x801caa40), which answers 5 for a riderless machine; such a pickup, which
only a direct grant produces, fires no handler.

## The API

`CustomItemsAPI` is exported at `OnBoot` and imported with
`Hoshi_ImportMod(CUSTOM_ITEMS_MOD_NAME, CUSTOM_ITEMS_API_MAJOR, CUSTOM_ITEMS_API_MINOR)`. Items are
addressed by id hash, not by registry index.

| Call | Purpose |
|---|---|
| `GetCount`, `GetIdHash(i)`, `GetName(i)` | Enumerate the discovered items; a consumer binds by name and keeps the hash |
| `SetEnabled(hash, on)` | The item's spawn gate |
| `GetAssignedKind(hash)` | This scene's `ItemKind`, or -1 |
| `AddPickupHandler(fn)` | `void fn(u32 id_hash, int player)` on every custom pickup, `player` 0-4 |
| `GetItemKind(item)` | An instance's real `ItemKind`, custom kinds included |

The gate defaults open, so an item no consumer touches spawns as soon as it is discovered. There is
one gate per item, shared by every consumer, so two consumers gating the same item is last writer
wins. A closed item is skipped at registration: it gets no `ItemKind` and nothing can spawn it.
Gates are read at `CityItemSpawn_Init`, so a consumer sets them by `On3DLoadStart`, and one that
wants an item in City Trial only closes it in other scenes. Assigned kinds are cleared at every
`On3DLoadStart`, so `GetAssignedKind` answers -1 until the scene registers. `GetItemKind` exists
because the clamp leaves `ItemData.kind` holding the base kind; a consumer patching a game function
that takes an item cannot otherwise tell a custom item from its base. Up to
`CUSTOM_ITEM_PICKUP_HANDLERS_MAX` handlers may subscribe, and each sees every pickup and filters by
hash.

## Making an item

The descriptor is the same however the archive is made. Most items are a model carved out of
`Item.dat` - any of its models carves intact, the skinned, multi-material legendary pieces
included - with the descriptor, a retextured image or a zero-entry `PatchEffectInfo` (an item that
grants nothing) emitted alongside; a model `Item.dat` does not hold is generated instead. An item
whose pickup does something ships in the mod that implements it, which binds a pickup handler by
the item's name; an item that only behaves as its base kind needs no code, and dropping its `.dat`
into any built mod's staged `items/` folder registers it.
