# Patch Type Gating

Each of the 9 City Trial stat patches can be individually locked behind an Archipelago unlock item; while a patch type is locked none of its ITKIND variants appear in any spawn pool. AP items 780-788 (`AP_PATCH_UNLOCK_BASE` + `PatchKind`) route through `ap_item_handler.c` to `GatePatches_UnlockPatch`, which sets the bit in `APSave.patch_unlocked_mask` and posts a textbox. The mask is exposed to other mods through `ArchipelagoAPI` as `AP_UNLOCK_PATCH`; when the slot option `patch_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` (`main.c`) pre-fills it with all 9 bits at connect and the gate never bites.

**File:** `mods/archipelago/src/gate_patches.c`.

## What Is Gated

The 9 `PatchKind` values (`item.h`). Each covers three ITKINDs at once - `ITKIND_<STAT>`, `ITKIND_<STAT>DOWN`, `ITKIND_<STAT>FAKE` - collapsed onto one `PatchKind` by `ItemKindToPatchKind()`. `PATCHKIND_HP` is the exception: `ITKIND_HP` has no Down or Fake variant. Grouping the variants keeps the AP pool at 9 items instead of 27, and "unlock offense stat items" is how a player thinks about it anyway.

`ITKIND_ALLUP` and the four `*MAX` items (`SPEEDMAX`, `CHARGEMAX`, `OFFENSEMAX`, `DEFENSEMAX`) are **not** mapped by `ItemKindToPatchKind` - it returns `-1` for them - so a locked stat still lets All-Up and Max-stat pickups through. Those carry their own bits in the individual-item gate.

## Game System

Every City Trial item drop is rolled out of one of two table families, and a patch ITKIND can sit in both.

`grBoxGeneObj` (`*stc_grBoxGeneObj`, r13+0x608) holds the box pools, each a parallel `it_kind[]` / `chance[]` array with a `num` count:

- `item_group_spawn[BOXKIND_NUM]` - one pool per box color, also used for sky and ground drops.
- `sameitem_*` - the pool the "All Same Item" event draws the single item from.
- `subsequent_*` - the pool a blue box uses when it drops more than one power-up in sequence.

`grBoxGeneInfo` (`*stc_grBoxGeneInfo`, r13+0x610) holds the event drop table at `item_desc->event_source_drop[]`: one entry per ITKIND with six independent weight columns, one per drop source (`chance_dyna`, `chance_tac`, `chance_meteor`, `chance_destructible`, `chance_chamber`, `chance_ufo`).

Both are populated at City Trial start by `CityItemSpawn_InitItemFallChances` (0x800eb374) and can be repopulated mid-match by `CityEvent_ModifyItemFallDesc` (0x800ed784) when an event changes the drop mix. Filtering has to run after each, or the repopulation undoes it.

## Implementation

This module installs **no hooks of its own**. hoshi allows one hook per address and three gate modules need the same two sites, so `item_spawn_filter.c` owns them and calls each module's filters in a fixed order: All-Up injection, then the box-pool filters (abilities, patches, items), then the event-drop filters in the same order, then the Max Stats drop-weight bias.

| Hook address | Hooked function (entry) | Clobbered instruction |
|-------------|-----------------|----------------------|
| `0x800eb558` | `CityItemSpawn_InitItemFallChances` (0x800eb374) | `lwz r0, 0x34(r1)` |
| `0x800ed7f0` | `CityEvent_ModifyItemFallDesc` (0x800ed784) | `lwz r0, 0x14(r1)` |

Both are function epilogues, so the hook can call C with no arguments. Stadium and Air Ride never run the `CityItemSpawn` init path at all, so `ItemSpawnFilter_On3DLoadEnd()` runs the same chain at scene load instead, guarded by `!Gm_IsInCity() && *stc_grBoxGeneObj`.

The two pool families are filtered differently, and the difference is load-bearing:

- **Box pools** (`FilterPatchItemsFromPool`): a locked entry is deleted by stable two-pointer forward compaction and `*pool_num` shrinks. The game samples these pools by random index, so the array length must actually shrink or the roll can land on a hole. Order is preserved - this is *not* a swap-with-last delete.
- **Event drop table** (`GatePatches_FilterEventDropTables`): entries cannot move, because callers index the table directly. All six `chance_*` columns of a locked entry are zeroed in place instead.

A newly received unlock takes effect at the next spawn-table population - the next round, or the next event reinit - because that is when the filters re-run over freshly loaded `.dat` data.
