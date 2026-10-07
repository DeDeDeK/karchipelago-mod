# Item Type Gating

Each non-patch, non-copy City Trial item gets its own Archipelago unlock bit (30 total). AP items 790-819 (`AP_ITEM_UNLOCK_BASE` + `ItemUnlockKind`) route through `ap_item_handler.c` to `GateItems_UnlockItem`, which sets the bit in `APSave.item_unlocked_mask` (a `u32`, to fit all 30) and announces `"Unlocked Item: <name>"` in `tb_api->ItemColor` through `APAnnounce_Grant` (shown only with Messages -> Local -> Items on, default Off). A locked item is removed from every spawn pool and cannot appear in City Trial. The granularity is deliberate: a coarse group-based system (food, allup, maxmin, special, legendary) would be simpler but makes for far less interesting AP progression.

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

`FilterAllSpawnTables()` in `item_spawn_filter.c` runs the whole pipeline in order: `GoalMaxStatsCT_EnsureAllUpInPools()` (inject, Max Stats goal only), then one compaction pass over the box pools and one zeroing pass over the event-drop table. Each gate file contributes only a predicate - `GateAbilities_IsItemLocked`, `GatePatches_IsItemLocked`, `GateItems_IsItemLocked` - and reports only on kinds in its own category, so a kind no gate covers is never locked. It is `HOOKCREATE`d at two function-epilogue points:

| Hook address | Hooked function (entry) | Clobbered instruction |
|---------|-----------------|-----------------|
| 0x800eb558 | `CityItemSpawn_InitItemFallChances` (0x800eb374) | `lwz r0, 0x34(r1)` |
| 0x800ed7f4 | `CityEvent_ModifyItemFallDesc` (0x800ed784) | `mtlr r0` (r0 holds the caller's LR, so the hook saves and restores it around the call) |

Stadium and Air Ride modes don't run the CT init path, so `ItemSpawnFilter_On3DLoadEnd()` calls `FilterAllSpawnTables()` at scene load as a fallback, guarded by `!Gm_IsInCity() && *stc_grBoxGeneObj`.

The box-pool pass is a stable two-pointer forward compaction: each surviving `it_kind`/`chance` pair is copied down to the next write slot and `num` rewritten. Order is preserved - this is not a swap-with-last delete. The event-drop table cannot compact, since the entry's index is its ITKIND, so a locked row stays in place with all six `chance_*` columns zeroed.

## Hardcoded Event All Ups

Two event sources throw an All Up without rolling any table, so the pool filter cannot reach them. `gate_items.c` gates each at its call site. A locked All Up there gives way to one roll of the source's own event-drop column, which the filter has already zeroed for locked kinds. A roll of -1 spawns nothing.

- **The UFO.** Each of its five state thinks (`CityUFO_State0Think`, `CityUFO_State1Think`, `CityUFO_State2Think`, `CityUFO_State3Think`, `CityUFO_State4Think`) drops a ring whose slot 0 is `li r29, 20` (`ITKIND_ALLUP`) and whose other slots call `CityItem_GetEventItem(EVDROP_UFO)`. At each of 0x8010b268, 0x8010b958, 0x8010c0cc, 0x8010c7a4 and 0x8010ce44 the `li` becomes a `bl GateItems_UfoRingLeadItem`, and the `b` after it is shortened by 4 to land on the `mr r29, r3` the pool roll returns through. No volatile register is live there, since the other arm makes a call at the same point.
- **Dyna Blade.** `DynaBlade_ThrowItems` (0x8021db44) throws a single `ITKIND_ALLUP` once enough damage lands, instead of that throw's usual `EVDROP_DYNA` rolls. `GateItems_DynaBladeThrowReward` is `REPLACECALL`ed over that `bl CityItem_Throw` at 0x8021ddf4.

The rest of the event sources already roll gated tables: Tac, the meteor, the Secret Chamber, pillars and the other breakables use their `event_source_drop` columns. Same Item, Bounce and Fake Powerups draw from the box pools. The Machine Formation's machines come through custom_machines' gated spawn roll.

## Legendary Piece Spawn Gating

Hydra and Dragoon parts are gated here as spawn items, separately from the assembled-machine gating in `gate_machines.c`: whether the *pieces* appear in boxes and whether the *assembled machine* is available are different questions, and the YAML can set either independently.

Pieces bypass both pool families entirely. `LegendaryPieces_Init` (0x800ecfac) populates `LegendaryPieceData.machine[i].item_kind[0..2]` with the three piece ITKINDs (`machine[0]` = Dragoon, `machine[1]` = Hydra; each entry is 0x38 bytes). At runtime `CityItemSpawn_SpawnLegendaryPiece` (0x800ed384) picks a machine with `req_spawn` set, reads `machine[i].item_kind[next_piece_index]`, and calls `LegendaryPiece_MarkAsSpawned` (0x80252f10), which writes the ITKIND straight into a target box's `forced_item` field (+0x35c) - the box then spawns that specific piece without consulting `grBoxGeneObj`. So the pool filter never sees `ITKIND_HYDRA*` / `ITKIND_DRAGOON*` in the box pools, and gating those bits through it alone does nothing. Their pool-filter arms are kept only as defensive identity mapping, and to cover `event_source_drop[]` if those ITKINDs ever appear there. Two dedicated patches do the real work.

**All three pieces locked disables the machine.** `GateItems_FilterLegendaryPieces()` is `HOOKCREATE`d at 0x800ec284, the instruction immediately after the `bl LegendaryPieces_Init` call site inside `CityItemSpawn_Init` (clobbered instruction `lwz r3, 1552(r13)`, re-executed by the hook). With all three Dragoon bits clear it sets `lpd->machine[0].is_enabled = 0`, and likewise for Hydra on `machine[1]`. `is_enabled` is bit 0x40 of the machine flags byte (+0x34); `CityItemSpawn_CheckToSpawnLegendaryPiece` (0x800ed2f0) tests it and early-outs when clear, so the machine is never promoted to `req_spawn` and `CityItemSpawn_SpawnLegendaryPiece` never runs for it.

**A locked Red Box disables both machines.** The carrier is a red box that hardcodes its color at 0x800eb218 instead of going through `GrBoxGeneratorDetermine`, so box gating cannot reach it - without this a player who has not received the Red Box unlock still gets Dragoon and Hydra part deliveries. The same hook clears `is_enabled` on both machines when `GateBoxes_IsUnlocked(BOXKIND_RED)` is 0, ahead of the per-machine piece checks. It has to be done here rather than at the spawn seam: `CityItemSpawn_UpdateAndCheckToSpawn` returns category 2 for as long as a piece is pending, so suppressing the spawn instead of the arming would stall the item spawner for the rest of the round. Assembling a legendary machine in City Trial therefore takes its three piece unlocks *and* the Red Box unlock.

**Partial locking skips individual pieces.** `GateItems_MarkAsSpawnedGated()` is `REPLACECALL`d over the two `bl LegendaryPiece_MarkAsSpawned` sites inside `CityItemSpawn_SpawnLegendaryPiece`: 0x800ed41c (Dragoon, `machine[0]`) and 0x800ed49c (Hydra, `machine[1]`). The wrapper checks the about-to-spawn ITKIND against the mask and, if the piece is locked, returns without calling through - so the spawner box's `forced_item` stays at its default (-1 = random pool roll) and the box still spawns at the legendary slot's progress threshold but holds a regular item. The caller advances `next_piece_index` and updates `x1c[idx]` / `x28[idx]` regardless, so the slot is consumed and the locked piece is not retried in the same round; once its unlock arrives, later rounds spawn it normally.

## All-Up Injection (Max Stats Goal)

The injection belongs to the City Trial goal `GOAL_MAX_STATS_CT` ("Max Stats in One Run") and lives in `goal_max_stats_ct.c`. The goal latches when one human brings all nine stats to `PatchCap_GetMax()` patches above their start (-2 for the eight non-HP stats, 0 for HP) in a single run - the `city_trial_patch_cap_max` slot option, 18-30 in the apworld. An All-Up advances all nine stats at once, so the goal leans on it; doing the same in other modes would make individual-patch unlocks pointless and skew the drop economy.

`GoalMaxStatsCT_EnsureAllUpInPools()` runs *before* the gate filters and is a no-op unless both `ap_save->options.goal[GMMODE_CITYTRIAL] == GOAL_MAX_STATS_CT` and All-Up is unlocked (`GateItems_IsItemLocked(ITKIND_ALLUP)` is 0). When active it makes All-Up (`ITKIND_ALLUP`) reachable from every patch source.

`EnsureItemInPool()` appends `ITKIND_ALLUP` to a pool only if absent and there is room (`num < max_entries`); if All-Up is already present its vanilla weight is left alone.

| Target | Cap (`max_entries`) | Injected weight |
|--------|--------------------|-----------------|
| Each `item_group_spawn[box]` pool | `ITKIND_NUM - 1` | `ALLUP_BOX_POOL_CHANCE` = 8 |
| `sameitem_*` pool | `ITKIND_NUM - 1` | 8 |
| `subsequent_*` pool | 40 | 8 |
| `event_source_drop[ALLUP].chance_destructible` (if 0) | - | `ALLUP_CHANCE_DESTRUCTIBLE` = 16 |
| `event_source_drop[ALLUP].chance_dyna` (if 0) | - | `ALLUP_CHANCE_DYNA` = 4 |

Vanilla already places All-Up in the UFO / Tac / Meteor / Chamber columns, so only the destructible and Dyna Blade columns are topped up, and only when currently zero. Because injection runs before filtering and All-Up is unlocked by precondition, the box-pool filter never removes the injected entry.

## Ungated Pre-fill and the Goal Gates

The mask is exposed through `ArchipelagoAPI` as `AP_UNLOCK_ITEM`. When the slot option `item_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` (`ap_options.c`) pre-fills it through `APUnlock_SetMask` with `(1u << ITUNLOCK_NUM) - 1`, less any bits the goal forces, when the first slot options arrive.

**The six legendary piece bits are the one exception to that pre-fill.** Assembling both machines is a City Trial goal, and with items ungated it is a feat the player can pull off in the first match with nothing from the item pool needed - the seed would be winnable before a single AP item arrived. When the AP world sets that goal it keeps `ITUNLOCK_HYDRA1-3` / `ITUNLOCK_DRAGOON1-3` in the pool despite the gate being off and sets `GOALGATE_LEGENDARY_PIECES` in the `goal_forced_gates` slot option; the pre-fill then leaves exactly those six bits clear and hands over everything else in the category. Their unlock items arrive through the normal `GateItems_UnlockItem` path, which never consults the gate flag.

The six Archipelago Star spheres follow the same shape one category over. They are custom City Trial items rather than `ItemKind`s, so they get their own `u8 ap_star_piece_unlocked_mask` instead of bits of this one - `ITUNLOCK_NUM` has all but filled a `u32` - with their own `AP_UNLOCK_AP_STAR_PIECE` category and AP item IDs 820-825. They ride the same `item_gating_enabled` flag, and `GOALGATE_AP_STAR_PIECES` holds them out of the ungated pre-fill the way `GOALGATE_LEGENDARY_PIECES` does the vanilla pieces. That mask is owned by `gate_ap_star.c`, which pushes it into the `ap_star` mod's own sphere gate on every write; that mod is where a sphere is enabled or held out of the item registry. The spheres are delivered by the same red carrier box, so the push sends 0 while Red is locked - the sphere equivalent of clearing `is_enabled`, since a sphere held out of the registry takes no delivery step.
