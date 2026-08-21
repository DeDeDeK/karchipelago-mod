# Item Type Gating

Each non-patch, non-copy City Trial item gets its own Archipelago unlock bit (30 total). AP items 790-819 (`AP_ITEM_UNLOCK_BASE` + `ItemUnlockKind`) route through `ap_item_handler.c` to `GateItems_UnlockItem`, which sets the bit in `APSave.item_unlocked_mask` (a `u32`, to fit all 30) and posts an `"Unlocked Item: <name>"` textbox in `tb_api->ItemColor`. A locked item is removed from every spawn pool and cannot appear in City Trial. The granularity is deliberate: a coarse group-based system (food, allup, maxmin, special, legendary) would be simpler but makes for far less interesting AP progression.

`ItemUnlockKind` (`archipelago_api.h`) is an Archipelago-only enum - **the bit index is not the ITKIND**. `ItemKindToUnlockBit()` maps ITKIND to bit and returns -1 for anything gated elsewhere; `ItemUnlockName()` is its inverse through the `itunlock_to_itkind[]` table and reuses hoshi's `ItemKind_Names[]` rather than carrying a parallel name table.

**File:** `mods/archipelago/src/gate_items.c`.

| Bit | `ItemUnlockKind` | ITKIND | Bit | `ItemUnlockKind` | ITKIND |
|----:|------------------|--------|----:|------------------|--------|
| 0 | `ITUNLOCK_ALLUP` | ALLUP | 15 | `ITUNLOCK_FOODOMELET` | FOODOMELET |
| 1 | `ITUNLOCK_SPEEDMAX` | SPEEDMAX | 16 | `ITUNLOCK_FOODHAMBURGER` | FOODHAMBURGER |
| 2 | `ITUNLOCK_SPEEDMIN` | SPEEDMIN | 17 | `ITUNLOCK_FOODSUSHI` | FOODSUSHI |
| 3 | `ITUNLOCK_OFFENSEMAX` | OFFENSEMAX | 18 | `ITUNLOCK_FOODHOTDOG` | FOODHOTDOG |
| 4 | `ITUNLOCK_DEFENSEMAX` | DEFENSEMAX | 19 | `ITUNLOCK_FOODAPPLE` | FOODAPPLE |
| 5 | `ITUNLOCK_CHARGEMAX` | CHARGEMAX | 20 | `ITUNLOCK_FIREWORKS` | FIREWORKS |
| 6 | `ITUNLOCK_CHARGENONE` | CHARGENONE | 21 | `ITUNLOCK_PANICSPIN` | PANICSPIN |
| 7 | `ITUNLOCK_CANDY` | CANDY | 22 | `ITUNLOCK_SENSORBOMB` | SENSORBOMB |
| 8 | `ITUNLOCK_FOODMAXIMTOMATO` | FOODMAXIMTOMATO | 23 | `ITUNLOCK_GORDO` | GORDO |
| 9 | `ITUNLOCK_FOODENERGYDRINK` | FOODENERGYDRINK | 24 | `ITUNLOCK_HYDRA1` | HYDRA1 |
| 10 | `ITUNLOCK_FOODICECREAM` | FOODICECREAM | 25 | `ITUNLOCK_HYDRA2` | HYDRA2 |
| 11 | `ITUNLOCK_FOODRICEBALL` | FOODRICEBALL | 26 | `ITUNLOCK_HYDRA3` | HYDRA3 |
| 12 | `ITUNLOCK_FOODCHICKEN` | FOODCHICKEN | 27 | `ITUNLOCK_DRAGOON1` | DRAGOON1 |
| 13 | `ITUNLOCK_FOODCURRY` | FOODCURRY | 28 | `ITUNLOCK_DRAGOON2` | DRAGOON2 |
| 14 | `ITUNLOCK_FOODRAMEN` | FOODRAMEN | 29 | `ITUNLOCK_DRAGOON3` | DRAGOON3 |

Stat patches, copy-ability panels and item boxes are **not** in this system - each has its own gating module, and their ITKINDs pass through this filter unfiltered.

## Spawn Tables

Two pool families are filtered, the same ones the patch and copy-ability gates use:

- **Box pools** (`grBoxGeneObj`, `*stc_grBoxGeneObj` at r13+0x608): the per-box-kind `item_group_spawn[BOXKIND_NUM]` arrays (also used for sky and ground drops), the `sameitem_*` pool, and the `subsequent_*` blue-box pool. Each is a parallel `it_kind[]` / `chance[]` array with a `num` count.
- **Event drop table** (`grBoxGeneInfo`, `*stc_grBoxGeneInfo` at r13+0x610): `item_desc->event_source_drop[]` (+0x18, count at +0x1c), one entry per ITKIND with six per-source weight columns - `chance_dyna`, `chance_tac`, `chance_meteor`, `chance_destructible` (yaku-break objects: star pole, event pillar, volcano walls, houses), `chance_chamber`, `chance_ufo`.

`FilterAllSpawnTables()` in `item_spawn_filter.c` runs the whole pipeline in order: `GateItems_EnsureAllUpInSpawnPools()` (inject, Max Stats goal only), then the ability/patch/item box-pool filters, then the ability/patch/item event-drop filters, then `GoalMaxStatsCT_ApplyDropBias()`. There is no cross-chaining between gate files - each filter touches only its own categories. It is `HOOKCREATE`d at two function-epilogue points:

| Hook address | Hooked function (entry) | Clobbered instruction |
|---------|-----------------|-----------------|
| 0x800eb558 | `CityItemSpawn_InitItemFallChances` (0x800eb374) | `lwz r0, 0x34(r1)` |
| 0x800ed7f0 | `CityEvent_ModifyItemFallDesc` (0x800ed784) | `lwz r0, 0x14(r1)` |

Stadium and Air Ride modes don't run the CT init path, so `ItemSpawnFilter_On3DLoadEnd()` calls `FilterAllSpawnTables()` at scene load as a fallback, guarded by `!Gm_IsInCity() && *stc_grBoxGeneObj`.

`GateItems_FilterSpawnTables()` compacts box pools with `FilterItemsFromPool()`, a stable two-pointer forward compaction that copies each surviving `it_kind`/`chance` pair down to the next write slot and rewrites `num`. Order is preserved - this is not a swap-with-last delete. `GateItems_FilterEventDropTables()` cannot compact, since the entry's index is its ITKIND, so it leaves the entry in place and zeroes all six `chance_*` columns.

## Legendary Piece Spawn Gating

Hydra and Dragoon parts are gated here as spawn items, separately from the assembled-machine gating in `gate_machines.c`: whether the *pieces* appear in boxes and whether the *assembled machine* is available are different questions, and the YAML can set either independently.

Pieces bypass both pool families entirely. `LegendaryPieces_Init` (0x800ecfac) populates `LegendaryPieceData.machine[i].item_kind[0..2]` with the three piece ITKINDs (`machine[0]` = Dragoon, `machine[1]` = Hydra; each entry is 0x38 bytes). At runtime `CityItemSpawn_SpawnLegendaryPiece` (0x800ed384) picks a machine with `req_spawn` set, reads `machine[i].item_kind[next_piece_index]`, and calls `LegendaryPiece_MarkAsSpawned` (0x80252f10), which writes the ITKIND straight into a target box's `forced_item` field (+0x35c) - the box then spawns that specific piece without consulting `grBoxGeneObj`. So the pool filter never sees `ITKIND_HYDRA*` / `ITKIND_DRAGOON*` in the box pools, and gating those bits through it alone does nothing. Their pool-filter arms are kept only as defensive identity mapping, and to cover `event_source_drop[]` if those ITKINDs ever appear there. Two dedicated patches do the real work.

**All three pieces locked disables the machine.** `GateItems_FilterLegendaryPieces()` is `HOOKCREATE`d at 0x800ec284, the instruction immediately after the `bl LegendaryPieces_Init` call site inside `CityItemSpawn_Init` (clobbered instruction `lwz r3, 1552(r13)`, re-executed by the hook). With all three Dragoon bits clear it sets `lpd->machine[0].is_enabled = 0`, and likewise for Hydra on `machine[1]`. `is_enabled` is bit 0x40 of the machine flags byte (+0x34); `CityItemSpawn_CheckToSpawnLegendaryPiece` (0x800ed2f0) tests it and early-outs when clear, so the machine is never promoted to `req_spawn` and `CityItemSpawn_SpawnLegendaryPiece` never runs for it.

**Partial locking skips individual pieces.** `GateItems_MarkAsSpawnedGated()` is `REPLACECALL`d over the two `bl LegendaryPiece_MarkAsSpawned` sites inside `CityItemSpawn_SpawnLegendaryPiece`: 0x800ed41c (Dragoon, `machine[0]`) and 0x800ed49c (Hydra, `machine[1]`). The wrapper checks the about-to-spawn ITKIND against the mask and, if the piece is locked, returns without calling through - so the spawner box's `forced_item` stays at its default (-1 = random pool roll) and the box still spawns at the legendary slot's progress threshold but holds a regular item. The caller advances `next_piece_index` and updates `x1c[idx]` / `x28[idx]` regardless, so the slot is consumed and the locked piece is not retried in the same round; once its unlock arrives, later rounds spawn it normally.

## All-Up Injection and Bias (Max Stats Insanity Goal)

`GateItems_EnsureAllUpInSpawnPools()` runs *before* the gate filters and is a no-op unless both `ap_save->options.goal[GMMODE_CITYTRIAL] == GOAL_MAX_STATS_CT` and the `ITUNLOCK_ALLUP` bit is set. When active it makes All-Up (`ITKIND_ALLUP`) reachable from every patch source: that goal needs 127 on every stat in about 7 minutes, which is only feasible with All-Up saturation. Broadcasting All-Up everywhere is deliberately not done in other modes - it would make individual-patch unlocks pointless and skew the drop economy.

`EnsureItemInPool()` appends `ITKIND_ALLUP` to a pool only if absent and there is room (`num < max_entries`); if All-Up is already present its vanilla weight is left alone.

| Target | Cap (`max_entries`) | Injected weight |
|--------|--------------------|-----------------|
| Each `item_group_spawn[box]` pool | `ITKIND_NUM - 1` | `ALLUP_BOX_POOL_CHANCE` = 8 |
| `sameitem_*` pool | `ITKIND_NUM - 1` | 8 |
| `subsequent_*` pool | 40 | 8 |
| `event_source_drop[ALLUP].chance_destructible` (if 0) | - | `ALLUP_CHANCE_DESTRUCTIBLE` = 16 |
| `event_source_drop[ALLUP].chance_dyna` (if 0) | - | `ALLUP_CHANCE_DYNA` = 4 |

Vanilla already places All-Up in the UFO / Tac / Meteor / Chamber columns, so only the destructible and Dyna Blade columns are topped up, and only when currently zero. Because injection runs before filtering and All-Up is unlocked by precondition, the box-pool filter never removes the injected entry.

Presence alone isn't enough - All-Up also has to dominate the rolls. `GoalMaxStatsCT_ApplyDropBias()` (in `goal_max_stats_ct.c`) runs last, after injection and the gate filters, and is a no-op unless the CT goal is `GOAL_MAX_STATS_CT`. It multiplies the weight of every +1 patch (`ITKIND_WEIGHT`/`ACCEL`/`TOPSPEED`/`TURN`/`CHARGE`/`GLIDE`/`OFFENSE`/`DEFENSE`/`HP`) and All-Up entry by `MAX_STATS_PATCH_BIAS` = 8, across all box pools (u8 chances, saturating at 255) and all six event-drop columns (u16, saturating at 65535); everything else keeps its vanilla weight. Vanilla patch/All-Up weights of 4-20 become 32-160, comfortably above the 1-10 typical of food/copy/trap entries, so patches dominate without fully suppressing other drops. The throughput target is roughly 127 All-Ups in 7 minutes (one All-Up bumps every stat by 1); at 60 fps with a 4-frame spawn floor the field caps around 15 items/sec, so modest dominance suffices. The constant is the single tuning knob.

## Ungated Pre-fill and the Goal Gates

The mask is exposed through `ArchipelagoAPI` as `AP_UNLOCK_ITEM`. When the slot option `item_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` (`main.c`) pre-fills it with `(1u << ITUNLOCK_NUM) - 1` at connect.

**The six legendary piece bits are the one exception to that pre-fill.** Assembling both machines is a City Trial goal, and with items ungated it is a feat the player can pull off in the first match with nothing from the item pool needed - the seed would be winnable before a single AP item arrived. When the AP world sets that goal it keeps `ITUNLOCK_HYDRA1-3` / `ITUNLOCK_DRAGOON1-3` in the pool despite the gate being off and sets `GOALGATE_LEGENDARY_PIECES` in the `goal_forced_gates` slot option; the pre-fill then clears exactly those six bits and hands over everything else in the category. Their unlock items arrive through the normal `GateItems_UnlockItem` path, which never consults the gate flag.

The six Archipelago Star spheres follow the same shape one category over. They are custom City Trial items rather than `ItemKind`s, so they get their own `u8 ap_star_piece_unlocked_mask` instead of bits of this one - `ITUNLOCK_NUM` has all but filled a `u32` - with their own `AP_UNLOCK_AP_STAR_PIECE` category and AP item IDs 820-825. They ride the same `item_gating_enabled` flag, and `GOALGATE_AP_STAR_PIECES` holds them out of the ungated pre-fill the way `GOALGATE_LEGENDARY_PIECES` does the vanilla pieces. That mask is owned by `gate_ap_star.c`, which pushes it into the `ap_star` mod's own sphere gate on every write; that mod is where a sphere is enabled or held out of the item registry.
