# Patch Type Gating

## Overview

Each PatchKind (9 total: WEIGHT, ACCEL, TOPSPEED, TURN, CHARGE, GLIDE, OFFENSE, DEFENSE, HP — see `PatchKind` in `item.h`) can be individually locked. The Up, Down, and Fake ITKIND variants of each stat are gated together under one unlock — so unlocking OFFENSE enables `ITKIND_OFFENSE`, `ITKIND_OFFENSEDOWN`, and `ITKIND_OFFENSEFAKE` at once. HP is the exception: it has only `ITKIND_HP` (no Down/Fake variant). When a patch type is locked, none of its item variants appear in any spawn pool.

## Game System

All City Trial item spawning flows through the `grBoxGeneObj` spawn table system. The spawn tables live at `*(0x805dd0e0 + 0x608)` and contain three pools that must all be filtered:

- **`item_group_spawn[BOXKIND_NUM]`** — per-box-type item pools (blue/green/red). Each entry has `it_kind[ITKIND_NUM - 1]`, `chance[ITKIND_NUM - 1]`, and `num` (count of active entries). When a box spawns, the game picks from the corresponding pool via weighted random.
- **`sameitem_it_kind/chance/num`** — used by the "All Same Item" City Trial event. When active, all boxes drop the same item selected from this pool.
- **`subsequent_it_kind/chance/num`** — used when a blue box drops more than one patch power-up in sequence.

The tables are populated once at City Trial start (`CityItemSpawn_InitItemFallChances`) and can be reinitialised mid-match by events (`CityEvent_ModifyItemFallDesc`). Filtering must happen after both.

## Implementation

**Files:** `gate_patches.c` / `gate_patches.h`

This module installs **no hooks of its own** — the spawn-table hooks are owned by `item_spawn_filter.c` (hoshi allows only one hook per address). It exposes three functions:

| Function | Role |
|----------|------|
| `GatePatches_FilterSpawnTables()` | Filters the three `grBoxGeneObj` box pools (`item_group_spawn[]`, `sameitem_*`, `subsequent_*`). |
| `GatePatches_FilterEventDropTables()` | Zeroes locked entries in the `grBoxGeneInfo->item_desc->event_source_drop[]` event-drop table. |
| `GatePatches_UnlockPatch(PatchKind kind)` | Unlock entry point (see [AP Items](#ap-items)). |

### Hook points (in `item_spawn_filter.c`)

The static `FilterAllSpawnTables()` is invoked from two function-epilogue hooks (safe to call C with no arguments). It runs the box-pool filters then the event-drop filters for abilities, patches, and items:

| Hook Address | Function (entry) | Clobbered Instruction | When It Runs |
|-------------|-----------------|----------------------|-------------|
| `0x800eb558` | `CityItemSpawn_InitItemFallChances` (`0x800eb374`) | `lwz r0, 0x34(r1)` | After initial spawn-table population |
| `0x800ed7f0` | `CityEvent_ModifyItemFallDesc` (`0x800ed784`) | `lwz r0, 0x14(r1)` | After event-triggered reinit |

For stadium / Air Ride modes the `CityItemSpawn` init path never runs, so `ItemSpawnFilter_On3DLoadEnd()` calls `FilterAllSpawnTables()` directly instead (guarded by `!Gm_IsInCity()`).

### Filtering logic

`ItemKindToPatchKind(u8 it_kind)` maps an ITKIND to its `PatchKind`, collapsing up / down / fake variants onto one kind (returns `-1` for non-patch items). For each patch entry whose `PatchKind` bit is clear in `ap_save->patch_unlocked_mask`:

- **Box pools** (`FilterPatchItemsFromPool`): the entry is removed via stable two-pointer in-place compaction and `*pool_num` shrunk. The game samples these pools by random index, so the array length must actually shrink.
- **Event-drop pool** (`GatePatches_FilterEventDropTables`): cannot be compacted (callers iterate the table by index), so the entry's six chance columns — `chance_dyna`, `chance_tac`, `chance_meteor`, `chance_destructible`, `chance_chamber`, `chance_ufo` — are all set to `0`. See `docs/event-source-drops.md` for the source-to-column mapping.

**Not gated:** `ITKIND_ALLUP` and the `*MAX` items (`ITKIND_SPEEDMAX`, `ITKIND_CHARGEMAX`, `ITKIND_OFFENSEMAX`, `ITKIND_DEFENSEMAX`) are not mapped by `ItemKindToPatchKind` (they return `-1`), so they are never removed — a locked stat still lets All-Up and Max-stat pickups through.

## Save Data

`u16 patch_unlocked_mask` in `KARSave` — bit N = PatchKind N.

## AP Items

9 AP items, `AP_PATCH_UNLOCK_BASE` (780) + PatchKind index. IDs 780–788 (see `archipelago_api.h`).

`ap_item_handler.c` routes IDs in `[780, 780 + PATCHKIND_NUM)` to `GatePatches_UnlockPatch(id - AP_PATCH_UNLOCK_BASE)`, which:

1. Sets `ap_save->patch_unlocked_mask |= (1 << kind)`.
2. Logs `[GatePatches] Patch %d (%s) unlocked (mask = %s)` using `PatchKind_Names[kind]` and `MaskBits(mask, 16)`.
3. Enqueues a textbox: `EnqueueColoredNoun("Unlocked Patch: ", PatchKind_Names[kind], tb_api->PatchColors[kind], NULL)`.

The new mask takes effect at the next spawn-table population (next round / event reinit), when `FilterAllSpawnTables()` re-runs the filters.

## Design Decisions

**Variant grouping:** Up, Down, and Fake variants gate together under one PatchKind rather than individually. This keeps the AP item count manageable (9 instead of 27) and is intuitive — "unlock offense stat items" means all offense-related patches. The `ItemKindToPatchKind()` mapping handles the many-to-one relationship.

**Filter chain architecture:** All spawn-table filtering (abilities, patches, items) shares the same two hook points. `item_spawn_filter.c`'s `FilterAllSpawnTables()` owns the hooks and calls each gate module's filters independently, avoiding conflicts at shared hook addresses. The order is explicit and predictable — box-pool filters first (`GateAbilities_FilterSpawnTables` → `GatePatches_FilterSpawnTables` → `GateItems_FilterSpawnTables`), then the event-drop filters in the same order. (Around them, `GateItems_EnsureAllUpInSpawnPools()` runs first and `GoalMaxStatsCT_ApplyDropBias()` last; both are Max-Stats-goal concerns, not patch gating.) Box-type gating is unrelated to this chain — `gate_boxes.c` handles it via a `REPLACEFUNC` on `GrBoxGeneratorDetermine`.
