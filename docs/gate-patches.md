# Patch Type Gating

Each of the 9 City Trial stat patches can be individually locked behind an Archipelago unlock item. When a patch type is locked, none of its ITKIND variants appear in any spawn pool.

## What Is Gated

The 9 `PatchKind` values (`item.h`), one bit each in `patch_unlocked_mask`. The Up, Down, and Fake ITKIND variants of a stat gate together under one unlock — unlocking OFFENSE enables `ITKIND_OFFENSE`, `ITKIND_OFFENSEDOWN`, and `ITKIND_OFFENSEFAKE` at once. HP is the exception: it has only `ITKIND_HP`, no Down or Fake variant.

| Bit | `PatchKind` | ITKIND variants | AP item |
|----:|-------------|-----------------|--------:|
| 0 | `PATCHKIND_WEIGHT` | `WEIGHT` / `WEIGHTDOWN` / `WEIGHTFAKE` | 780 |
| 1 | `PATCHKIND_ACCEL` | `ACCEL` / `ACCELDOWN` / `ACCELFAKE` | 781 |
| 2 | `PATCHKIND_TOPSPEED` | `TOPSPEED` / `TOPSPEEDDOWN` / `TOPSPEEDFAKE` | 782 |
| 3 | `PATCHKIND_TURN` | `TURN` / `TURNDOWN` / `TURNFAKE` | 783 |
| 4 | `PATCHKIND_CHARGE` | `CHARGE` / `CHARGEDOWN` / `CHARGEFAKE` | 784 |
| 5 | `PATCHKIND_GLIDE` | `GLIDE` / `GLIDEDOWN` / `GLIDEFAKE` | 785 |
| 6 | `PATCHKIND_OFFENSE` | `OFFENSE` / `OFFENSEDOWN` / `OFFENSEFAKE` | 786 |
| 7 | `PATCHKIND_DEFENSE` | `DEFENSE` / `DEFENSEDOWN` / `DEFENSEFAKE` | 787 |
| 8 | `PATCHKIND_HP` | `HP` | 788 |

**Not gated:** `ITKIND_ALLUP` and the `*MAX` items (`ITKIND_SPEEDMAX`, `ITKIND_CHARGEMAX`, `ITKIND_OFFENSEMAX`, `ITKIND_DEFENSEMAX`) are not mapped by `ItemKindToPatchKind` (it returns `-1` for them), so they are never removed — a locked stat still lets All-Up and Max-stat pickups through. Those have their own bits in the individual-item gate.

## Entry Points

**Files:** `mods/archipelago/src/gate_patches.c` / `gate_patches.h`

| Symbol | Kind | Where | Role |
|--------|------|-------|------|
| `GatePatches_FilterSpawnTables()` | mod | gate_patches.c | Filters the three `grBoxGeneObj` box pools (`item_group_spawn[]`, `sameitem_*`, `subsequent_*`). |
| `GatePatches_FilterEventDropTables()` | mod | gate_patches.c | Zeroes locked entries in `grBoxGeneInfo->item_desc->event_source_drop[]`. |
| `GatePatches_UnlockPatch(PatchKind kind)` | mod | gate_patches.c | Sets the unlock bit, logs, and posts a textbox notification. Called from `ap_item_handler.c`. |
| `ItemKindToPatchKind(u8 it_kind)` | mod (static) | gate_patches.c | Collapses up / down / fake ITKINDs onto one `PatchKind`; `-1` for non-patch items. |
| `FilterPatchItemsFromPool(...)` | mod (static) | gate_patches.c | Stable two-pointer compaction of one box pool. |
| `FilterAllSpawnTables()` | mod (static) | item_spawn_filter.c | Owns the two spawn-table hook points and calls every gate module's filters. |
| `CityItemSpawn_InitItemFallChances` | game | 0x800eb374 | Populates the spawn tables at City Trial start. |
| `CityEvent_ModifyItemFallDesc` | game | 0x800ed784 | Reinitialises them mid-match on an event. |

This module installs **no hooks of its own** — hoshi allows only one hook per address, so `item_spawn_filter.c` owns both sites.

## Game System

All City Trial item spawning flows through the `grBoxGeneObj` spawn table system. The tables live at `*(0x805dd0e0 + 0x608)` and contain three pools that must all be filtered:

- **`item_group_spawn[BOXKIND_NUM]`** — per-box-type item pools (blue/green/red). Each entry has `it_kind[ITKIND_NUM - 1]`, `chance[ITKIND_NUM - 1]`, and `num` (count of active entries). When a box spawns, the game picks from the corresponding pool via weighted random.
- **`sameitem_it_kind/chance/num`** — used by the "All Same Item" City Trial event. When active, all boxes drop the same item selected from this pool.
- **`subsequent_it_kind/chance/num`** — used when a blue box drops more than one patch power-up in sequence.

The event-drop table is separate: `grBoxGeneInfo->item_desc->event_source_drop[]` (`grBoxGeneInfo` at `*(0x805dd0e0 + 0x610)`, `item_desc` at +0xc, `event_source_drop` at +0x18, count at +0x1c). One entry per ITKIND with six per-source weight columns — `chance_dyna`, `chance_tac`, `chance_meteor`, `chance_destructible`, `chance_chamber`, `chance_ufo`.

The tables are populated once at City Trial start (`CityItemSpawn_InitItemFallChances`, 0x800eb374) and can be reinitialised mid-match by events (`CityEvent_ModifyItemFallDesc`, 0x800ed784). Filtering must happen after both.

## Implementation

`FilterAllSpawnTables()` in `item_spawn_filter.c` is installed at two function-epilogue hooks (safe to call C with no arguments):

| Hook address | Function (entry) | Clobbered instruction | When it runs |
|-------------|-----------------|----------------------|-------------|
| `0x800eb558` | `CityItemSpawn_InitItemFallChances` (0x800eb374) | `lwz r0, 0x34(r1)` | After initial spawn-table population |
| `0x800ed7f0` | `CityEvent_ModifyItemFallDesc` (0x800ed784) | `lwz r0, 0x14(r1)` | After event-triggered reinit |

For stadium / Air Ride modes the `CityItemSpawn` init path never runs, so `ItemSpawnFilter_On3DLoadEnd()` calls `FilterAllSpawnTables()` directly instead (guarded by `!Gm_IsInCity() && *stc_grBoxGeneObj`).

Inside `FilterAllSpawnTables()` the order is: `GateItems_EnsureAllUpInSpawnPools()` → box-pool filters (`GateAbilities_` → `GatePatches_` → `GateItems_`) → event-drop filters (same order) → `GoalMaxStatsCT_ApplyDropBias()`.

### Filtering logic

`ItemKindToPatchKind(u8 it_kind)` maps an ITKIND to its `PatchKind`. For each entry whose `PatchKind` bit is clear in `ap_save->patch_unlocked_mask`:

- **Box pools** (`FilterPatchItemsFromPool`): the entry is removed by stable two-pointer forward compaction and `*pool_num` is shrunk. The game samples these pools by random index, so the array length must actually shrink. Order is preserved — this is *not* a swap-with-last delete.
- **Event-drop pool** (`GatePatches_FilterEventDropTables`): cannot be compacted (callers iterate the table by index), so all six `chance_*` columns of the entry are set to `0` instead.

## Save Data

`u16 patch_unlocked_mask` in `APSave` (`main.h`, accessed via the global `ap_save`) — bit N = `PatchKind` N.

The mask is exposed through `ArchipelagoAPI` as `AP_UNLOCK_PATCH`. When the slot option `patch_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` in `main.c` pre-fills the mask with `(1 << PATCHKIND_NUM) - 1` at connect.

## AP Items

9 AP items, `AP_PATCH_UNLOCK_BASE` (780, `archipelago_api.h`) + `PatchKind` index → IDs 780–788.

`ap_item_handler.c` routes IDs in `[780, 780 + PATCHKIND_NUM)` to `GatePatches_UnlockPatch(id - AP_PATCH_UNLOCK_BASE)`, which:

1. Sets `ap_save->patch_unlocked_mask |= (1 << kind)`.
2. Logs `[GatePatches] Patch %d (%s) unlocked (mask = %s)` using `PatchKind_Names[kind]` and `MaskBits(mask, 16)`.
3. Enqueues a textbox: `EnqueueColoredNoun("Unlocked Patch: ", PatchKind_Names[kind], tb_api->PatchColors[kind], NULL)`.

The new mask takes effect at the next spawn-table population (next round or event reinit), when `FilterAllSpawnTables()` re-runs the filters.

## Design Decisions

**Variant grouping:** Up, Down, and Fake variants gate together under one `PatchKind` rather than individually. This keeps the AP item count manageable (9 instead of 27) and is intuitive — "unlock offense stat items" means all offense-related patches. `ItemKindToPatchKind()` handles the many-to-one relationship.

**Filter chain architecture:** All spawn-table filtering (abilities, patches, items) shares the same two hook points. `item_spawn_filter.c`'s `FilterAllSpawnTables()` owns the hooks and calls each gate module's filters independently, avoiding conflicts at shared hook addresses. Box-type gating is unrelated to this chain — `gate_boxes.c` handles it with a `REPLACEFUNC` on `GrBoxGeneratorDetermine`.
