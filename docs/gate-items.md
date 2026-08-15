# Item Type Gating

Each non-patch, non-copy City Trial item gets its own Archipelago unlock bit (30 total). When locked, the item is removed from every spawn pool and cannot appear in City Trial. Individual item unlocks give finer-grained AP progression than a group-based system would.

## What Is Gated

30 items, one bit each in `item_unlocked_mask`. `ItemUnlockKind` (`archipelago_api.h`) is an Archipelago-only enum — the bit index is *not* the ITKIND. AP item ID = 790 + bit.

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

Stat patches, copy-ability panels, and item boxes are **not** in this system — each has its own gating module. `ItemKindToUnlockBit()` returns `-1` for them and they pass through unfiltered.

## Entry Points

**Files:** `mods/archipelago/src/gate_items.c` / `gate_items.h`

| Symbol | Kind | Where | Role |
|--------|------|-------|------|
| `GateItems_OnBoot()` | mod | gate_items.c | Installs the two legendary-piece patches (called from `main.c`). |
| `GateItems_FilterSpawnTables()` | mod | gate_items.c | Removes locked items from the three box pools. |
| `GateItems_FilterEventDropTables()` | mod | gate_items.c | Zeroes the six weight columns for locked items in `event_source_drop[]`. |
| `GateItems_EnsureAllUpInSpawnPools()` | mod | gate_items.c | Injects All-Up into every spawn source (Max Stats goal only). |
| `GateItems_FilterLegendaryPieces()` | mod (static) | gate_items.c | Disables a legendary machine whose three pieces are all locked. |
| `GateItems_MarkAsSpawnedGated()` | mod (static) | gate_items.c | Wrapper that skips spawning an individually locked piece. |
| `GateItems_UnlockItem(kind)` | mod | gate_items.c | Sets the unlock bit and posts a textbox. Called from `ap_item_handler.c`. |
| `ItemKindToUnlockBit(u8 it_kind)` | mod (static) | gate_items.c | ITKIND → `ItemUnlockKind` bit; `-1` if not gated here. |
| `FilterAllSpawnTables()` | mod (static) | item_spawn_filter.c | Owns the two spawn-table hooks and calls every gate module's filters. |
| `LegendaryPieces_Init` | game | 0x800ecfac | Fills `LegendaryPieceData.machine[i].item_kind[0..2]`. |
| `CityItemSpawn_CheckToSpawnLegendaryPiece` | game | 0x800ed2f0 | Tests `is_enabled`; promotes a machine to `req_spawn`. |
| `CityItemSpawn_SpawnLegendaryPiece` | game | 0x800ed384 | Picks the next piece and marks its carrier box. |
| `LegendaryPiece_MarkAsSpawned` | game | 0x80252f10 | Writes the piece ITKIND into a box's `forced_item` (+0x35c). |

## Game System

The same `grBoxGeneObj` / `grBoxGeneInfo` spawn-table system the patch gate uses. Two pool families are filtered:

- **Box pools** (`grBoxGeneObj`, `*stc_grBoxGeneObj`, r13+0x608): the per-box-kind `item_group_spawn[BOXKIND_NUM]` arrays (also used for sky and ground drops), the `sameitem_*` pool, and the `subsequent_*` blue-box pool. Each is a parallel `it_kind[]` / `chance[]` array with a `num` count.
- **Event drop table** (`grBoxGeneInfo`, `*stc_grBoxGeneInfo`, r13+0x610): `item_desc->event_source_drop[]` (+0x18, count at +0x1c), one entry per ITKIND with six per-source weight columns — `chance_dyna`, `chance_tac`, `chance_meteor`, `chance_destructible` (yaku-break objects: star pole / event pillar / volcano walls / houses), `chance_chamber`, `chance_ufo`. The full source-to-column map is in `event-source-drops.md`.

Legendary machine pieces bypass both families. `LegendaryPieces_Init` (0x800ecfac) populates `LegendaryPieceData.machine[i].item_kind[0..2]` with the three piece ITKINDs (`machine[0]` = Dragoon, `machine[1]` = Hydra; each `machine` entry is 0x38 bytes). At runtime `CityItemSpawn_SpawnLegendaryPiece` (0x800ed384) picks a machine that has `req_spawn` set, reads `machine[i].item_kind[next_piece_index]`, and calls `LegendaryPiece_MarkAsSpawned` (0x80252f10), which writes the ITKIND directly into a target box's `forced_item` field (+0x35c) — the box then spawns that specific piece without consulting `grBoxGeneObj`.

## Implementation

`FilterAllSpawnTables()` in `item_spawn_filter.c` runs the whole pipeline in order: `GateItems_EnsureAllUpInSpawnPools()` (inject, Max Stats only) → ability/patch/item box-pool filters → ability/patch/item event-drop filters → `GoalMaxStatsCT_ApplyDropBias()`. It is `HOOKCREATE`d at two function-epilogue points:

| Hook address | Hooked function (entry) | Clobbered instruction |
|---------|-----------------|-----------------|
| `0x800eb558` | `CityItemSpawn_InitItemFallChances` (0x800eb374) | `lwz r0, 0x34(r1)` |
| `0x800ed7f0` | `CityEvent_ModifyItemFallDesc` (0x800ed784) | `lwz r0, 0x14(r1)` |

No cross-chaining between gate files — each filter only touches its own categories. Stadium and Air Ride modes don't run the CT init path, so `ItemSpawnFilter_On3DLoadEnd()` calls `FilterAllSpawnTables()` at scene load as a fallback (guarded by `!Gm_IsInCity() && *stc_grBoxGeneObj`).

Box pools are filtered by `FilterItemsFromPool()`: a stable two-pointer (read/write) forward compaction that copies each surviving `it_kind`/`chance` pair down to the next write slot and rewrites `num`. Order is preserved — this is *not* a swap-with-last delete, and it matches `gate_patches.c`'s `FilterPatchItemsFromPool()` exactly. For `event_source_drop[]` entries (`GateItems_FilterEventDropTables()`), the entry is left in place and all six `chance_*` columns are zeroed instead.

`ItemUnlockName()` is the inverse of `ItemKindToUnlockBit()` via the `itunlock_to_itkind[]` table, and reuses hoshi's `ItemKind_Names[]` for display instead of a parallel name table.

### Legendary piece spawn gating

Because pieces are written straight into a box's `forced_item`, the pool filter never sees `ITKIND_HYDRA*` / `ITKIND_DRAGOON*` in the `grBoxGeneObj` pools, and gating those bits via the pool filter alone has no effect. The pool-filter arms for those ITKINDs are kept solely as defensive identity mapping (and to cover the `event_source_drop[]` table if those ITKINDs ever appear there). Two dedicated patches do the real work.

**All three pieces locked → disable the machine.** `GateItems_FilterLegendaryPieces()` is `HOOKCREATE`d at `0x800ec284` — the instruction immediately after the `bl LegendaryPieces_Init` call site inside `CityItemSpawn_Init` (clobbered instruction `lwz r3, 1552(r13)`, re-executed by the hook). When all three Dragoon bits are clear in `item_unlocked_mask`, it sets `lpd->machine[0].is_enabled = 0`; same for Hydra → `lpd->machine[1].is_enabled = 0`. `is_enabled` is bit `0x40` of the machine flags byte (+0x34). `CityItemSpawn_CheckToSpawnLegendaryPiece` (0x800ed2f0) tests that bit and early-outs when it is clear, so the machine never gets promoted to `req_spawn` and `CityItemSpawn_SpawnLegendaryPiece` (which is gated on `req_spawn`) never runs — no piece of that type ever spawns.

**Partial locking → skip individual pieces.** `GateItems_MarkAsSpawnedGated()` is `REPLACECALL`d into the two `bl LegendaryPiece_MarkAsSpawned` sites inside `CityItemSpawn_SpawnLegendaryPiece`:

| Address | Site |
|---------|------|
| `0x800ed41c` | Dragoon piece spawn (`machine[0]`) |
| `0x800ed49c` | Hydra piece spawn (`machine[1]`) |

The wrapper checks the about-to-spawn ITKIND against `item_unlocked_mask` via `ItemKindToUnlockBit`. If the piece is locked it returns without calling `LegendaryPiece_MarkAsSpawned`, so the spawner box's `forced_item` stays at its default (`-1` = random pool roll) — the box still spawns at the legendary slot's progress threshold but contains a regular item instead of the locked piece. The caller advances `next_piece_index` and updates `x1c[idx]` / `x28[idx]` regardless, so the slot is "consumed" and the locked piece is not retried in the same round. Once its AP unlock arrives, future rounds spawn it normally.

### All-Up injection (Max Stats Insanity goal)

`GateItems_EnsureAllUpInSpawnPools()` runs *before* the gate filters in `FilterAllSpawnTables()` and is a no-op unless **both**:

- `ap_save->options.goal[GMMODE_CITYTRIAL] == GOAL_MAX_STATS_CT`, and
- the `ITUNLOCK_ALLUP` bit is set in `item_unlocked_mask`.

When active it guarantees All-Up (`ITKIND_ALLUP`) is reachable from every patch source — the Max Stats goal needs 127 on every stat in ~7 minutes, which is only feasible with All-Up saturation. Broadcasting All-Up everywhere is *not* done in other modes (it would make individual-patch unlocks pointless and skew the drop economy).

`EnsureItemInPool()` appends `ITKIND_ALLUP` to a pool only if absent and there is room (`num < max_entries`); if All-Up is already present its vanilla weight is left untouched.

| Target | Cap (`max_entries`) | Injected weight |
|--------|--------------------|-----------------|
| Each `item_group_spawn[box]` pool | `ITKIND_NUM - 1` | `ALLUP_BOX_POOL_CHANCE` = 8 |
| `sameitem_*` pool | `ITKIND_NUM - 1` | 8 |
| `subsequent_*` pool | 40 | 8 |
| `event_source_drop[ALLUP].chance_destructible` (if 0) | — | `ALLUP_CHANCE_DESTRUCTIBLE` = 16 |
| `event_source_drop[ALLUP].chance_dyna` (if 0) | — | `ALLUP_CHANCE_DYNA` = 4 |

Vanilla already places All-Up in the UFO / Tac / Meteor / Chamber columns, so those are left alone — only the destructible and Dyna Blade columns are topped up, and only if currently zero. Because injection runs before filtering and All-Up is (by precondition) unlocked, the box-pool filter never removes the injected entry.

### Drop-weight bias (`GoalMaxStatsCT_ApplyDropBias`)

Ensuring All-Up is merely *present* isn't enough for the Max Stats goal — it also has to dominate the rolls. `GoalMaxStatsCT_ApplyDropBias()` (in `goal_max_stats_ct.c`) runs last in `FilterAllSpawnTables()`, after injection and the gate filters, and is a no-op unless the CT goal is `GOAL_MAX_STATS_CT`. It multiplies the spawn weight of every +1 patch and All-Up entry by **`MAX_STATS_PATCH_BIAS` = 8×**:

- All `item_group_spawn[box]` pools, plus the `sameitem_*` and `subsequent_*` pools (u8 chances, saturating at 255).
- All six `event_source_drop[]` chance columns — `chance_dyna`, `chance_tac`, `chance_meteor`, `chance_destructible`, `chance_chamber`, `chance_ufo` (u16 chances, saturating at 65535).

Only rows whose ITKIND is a +1 patch (`ITKIND_WEIGHT`/`ACCEL`/`TOPSPEED`/`TURN`/`CHARGE`/`GLIDE`/`OFFENSE`/`DEFENSE`/`HP`) or `ITKIND_ALLUP` are scaled; everything else keeps its vanilla weight.

**Why 8×.** Vanilla patch/All-Up weights of 4–20 become 32–160, comfortably above the typical 1–10 weights of food/copy/trap entries — patches dominate the pools without *fully* suppressing other drops. **Throughput target:** reaching 127 on every stat in ~7 minutes needs ~127 All-Ups collected (one All-Up bumps every stat by 1). At 60 fps with a 4-frame spawn floor the field caps around 15 items/sec, so even modest All-Up dominance puts that within reach. The constant is the single tuning knob — raise it if play-testing shows insufficient throughput.

## Save Data

`u32 item_unlocked_mask` in `APSave` (`main.h`, accessed via the global `ap_save`) — bit N = `ItemUnlockKind` N. The `u32` width accommodates all 30 bits.

The mask is exposed through `ArchipelagoAPI` as `AP_UNLOCK_ITEM`. When the slot option `item_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` in `main.c` pre-fills the mask with `(1u << ITUNLOCK_NUM) - 1` at connect.

**The six legendary piece bits are the one exception to that pre-fill.** Assembling both machines is a City Trial goal, and with items ungated it is a feat the player can pull off in the first match with nothing from the item pool needed - the seed would be winnable before a single AP item arrived. When the AP world sets that goal it keeps `ITUNLOCK_HYDRA1-3` / `ITUNLOCK_DRAGOON1-3` in the pool despite the gate being off and sets `GOALGATE_LEGENDARY_PIECES` in the `goal_forced_gates` slot option; the pre-fill then clears exactly those six bits and everything else in the category is still handed over at connect. The unlock items arrive through the normal `GateItems_UnlockItem` path, which never consults the gate flag.

## AP Items

30 AP items, `AP_ITEM_UNLOCK_BASE` (790, `archipelago_api.h`) + `ItemUnlockKind` index → IDs 790–819. `ap_item_handler.c` routes IDs in `[790, 790 + ITUNLOCK_NUM)` to `GateItems_UnlockItem(id - AP_ITEM_UNLOCK_BASE)`, which sets the bit, logs, and enqueues `"Unlocked Item: <name>"` via `tb_api->EnqueueColoredNoun` in `tb_api->ItemColor`.

## Design Decisions

**Individual over grouped:** A coarse group-based system (food, allup, maxmin, candy, special, gordo, legendary) is simpler but too coarse for interesting AP progression. Individual items mean each food type, each legendary piece, etc. is its own AP item — more items in the pool, more progression granularity, at the cost of a `u32` mask for the 30 bits.

**Legendary machine pieces as items:** Hydra and Dragoon parts (3 each) are gated here as spawn items, separate from the assembled-machine gating in `gate_machines.c`. This is intentional — gating whether the *pieces* appear in boxes is different from gating whether the *assembled machine* is available. Both can be used together or independently via YAML options.
