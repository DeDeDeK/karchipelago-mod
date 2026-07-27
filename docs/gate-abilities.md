# Copy Ability Gating

Each of Kirby's 11 copy abilities can be individually locked behind an Archipelago unlock item. A locked ability cannot be obtained from *any* source — copy panels, the copy chance wheel, or enemy inhale — and its item panels and themed enemies are filtered out of the spawn tables so the locked state is invisible rather than teasing.

This doc covers the gating only. The engine's grant / per-frame tick / teardown lifecycle for a held ability is in `copy-ability-system.md`.

## What Is Gated

The 11 `CopyKind`s (`rider.h`), one bit each in `ability_unlocked_mask`. Each has a copy panel ITKIND (mapped by `Ability_ItKindToCopyKind` in `ability_item.c`) and, for most, one or more themed enemies.

| Bit | `CopyKind` | Panel ITKIND | AP item |
|----:|------------|--------------|--------:|
| 0 | `COPYKIND_FIRE` | `ITKIND_COPYFIRE` | 760 |
| 1 | `COPYKIND_WHEEL` | `ITKIND_COPYTIRE` | 761 |
| 2 | `COPYKIND_SLEEP` | `ITKIND_COPYSLEEP` | 762 |
| 3 | `COPYKIND_SWORD` | `ITKIND_COPYSWORD` | 763 |
| 4 | `COPYKIND_BOMB` | `ITKIND_COPYBOMB` | 764 |
| 5 | `COPYKIND_PLASMA` | `ITKIND_COPYPLASMA` | 765 |
| 6 | `COPYKIND_NEEDLE` | `ITKIND_COPYSPIKE` | 766 |
| 7 | `COPYKIND_MIC` | `ITKIND_COPYMIC` | 767 |
| 8 | `COPYKIND_FREEZE` | `ITKIND_COPYICE` | 768 |
| 9 | `COPYKIND_TORNADO` | `ITKIND_COPYTORNADO` | 769 |
| 10 | `COPYKIND_BIRD` | `ITKIND_COPYBIRD` | 770 |

## Entry Points

**Files:** `mods/archipelago/src/gate_abilities.c` / `gate_abilities.h` (gating hooks + enemy-spawn filtering); `ability_item.c` / `ability_item.h` (`Ability_GiveItem` AP grant path, `Ability_ItKindToCopyKind` ITKIND → CopyKind mapping).

| Symbol | Kind | Where | Role |
|--------|------|-------|------|
| `GateAbilities_OnBoot()` | mod | gate_abilities.c | Installs the two REPLACEFUNCs and two NOPs (called from `main.c`). |
| `GateAbilities_CheckAndGiveAbility(GOBJ*, int kind)` | mod | gate_abilities.c | Replacement for `Rider_CheckAndGiveAbility`; gates item/enemy grants, sends the Sleep TrapLink. |
| `GateAbilities_RandomGiveAbility(RiderData*, int kind)` | mod | gate_abilities.c | Replacement for `randomAbility_giveAbility`; substitutes a random unlocked ability. |
| `GateAbilities_FilterSpawnTables()` | mod | gate_abilities.c | Removes locked copy panels from the three `grBoxGeneObj` box pools. |
| `GateAbilities_FilterEventDropTables()` | mod | gate_abilities.c | Zeroes all six chance columns of locked copy panels in `event_source_drop[]`. |
| `GateAbilities_On3DLoadEnd()` | mod | gate_abilities.c | Zeroes enemy spawn weights for locked-ability enemies. |
| `GateAbilities_UnlockAbility(CopyKind)` | mod | gate_abilities.c | Sets the unlock bit and posts a textbox. Called from `ap_item_handler.c`. |
| `EnemyIDToCopyKind(int enemy_id)` | mod (static) | gate_abilities.c | Enemy actor ID → themed `CopyKind`. |
| `RandomUnlockedAbility()` | mod (static) | gate_abilities.c | Random unlocked `CopyKind`, or `-1` if none. |
| `Rider_CheckAndGiveAbility` | game | 0x80192650 | Single entry point for non-wheel grants (replaced). |
| `Rider_GiveAbility` | game | 0x801a81a4 | Master grant; **not** replaced, so AP grants bypass the gate. |
| `randomAbility_giveAbility` | game | 0x801a61d4 | Copy-wheel commit (replaced). |
| `stc_ability_init_table` | game | 0x804af4f0 | 11 per-`CopyKind` init function pointers (e.g. `ability_Fire` at `0x801af474`); the replacement calls it directly. |
| `Rider_RecordCopyAbility(ply, kind)` | game | 0x8022ee00 | Records ability history, checks checklist sequences. |
| `Rider_MarkCopyAbilityObtained(ply, kind)` | game | 0x8022f150 | Sets the bit in the per-player obtained-abilities mask. |

## Game System

Copy abilities can be obtained four ways, all of which need gating.

### Copy panels from boxes and event drops

Copy ability panels are regular items in the `grBoxGeneObj` spawn table system and the `event_source_drop` table.

Box spawn pools (`grBoxGeneObj` at `*(0x805dd0e0 + 0x608)`):

- `item_group_spawn[BOXKIND_NUM]` — per-box-type pools. Each has `it_kind[ITKIND_NUM-1]` (68), `chance[ITKIND_NUM-1]` (68), `num`.
- `sameitem_it_kind/chance/num` — "All Same Item" event pool.
- `subsequent_it_kind/chance/num` — blue box multi-item pool.

Event drop table (`grBoxGeneInfo->item_desc->event_source_drop`, +0x18 with count at +0x1c): per-item entries with one chance field per drop source — `chance_dyna`, `chance_tac`, `chance_meteor`, `chance_destructible`, `chance_chamber`, `chance_ufo`.

### Copy chance wheel (inhale an enemy)

When Kirby inhales an enemy, a roulette wheel spins and lands on a random ability.

| Function | Address | Purpose |
|----------|---------|---------|
| `randomAbility_mainLoop` | 0x801a5fb8 | Per-frame wheel callback via `rd->cb_copy_input` (0x930) |
| `randomAbility_checkIfaPress` | 0x801ae7c8 | Checks A press to stop wheel |
| `randomAbility_aPress` | 0x801ae7f4 | Stops wheel, calls giveAbility |
| `randomAbility_autoSelect` | 0x801ae890 | Auto-selects on timer expiry (same logic as aPress) |
| `randomAbility_getItemID` | 0x801aea30 | Reads wheel position: `rd->x9b0[rd->x99c]` |
| `randomAbility_giveAbility` | 0x801a61d4 | Commits the result: `Rider_AbilityRemoveModel` → `Rider_AbilityClearQueued` → `Rider_RecordCopyAbility` → `stc_ability_init_table[kind](rd)` |
| `randomAbility_removeWheelModel` | 0x801a66d0 | Removes wheel 3D model |
| `randomAbility_queuedGive` | 0x801aec60 | Timer-based give for queued abilities |

`Rider_MarkCopyAbilityObtained` is called by `randomAbility_giveAbility`'s *callers* (`randomAbility_aPress` at 0x801ae874, `randomAbility_autoSelect` at 0x801ae910), not by `giveAbility` itself.

### Item pickup / enemy touch

When a machine touches a copy panel item: `Machine_OnTouchItem` (0x801db34c, case `0x1a`) → `Rider_CheckAndGiveAbility` (0x80192650) → `Rider_GiveAbility` (0x801a81a4). `Rider_CheckAndGiveAbility` checks `rd->kind == RDKIND_KIRBY` then calls `Rider_GiveAbility`. This is the single entry point for all non-wheel copy ability grants (items and enemies).

### Ground copy panels (collision attribute 0xF)

Floor polygons tagged with ground collision attribute 0xF spin the copy wheel when a machine drives over them:

```
Machine_ProcessEnvColl (0x801e5108)
  └─ detects attribute 0xF via zz_80246f40_ (0x80246f40)
  └─ checks it's a new panel (different from last frame's stored ID)
  └─ calls Rider_GiveRandomAbility (0x80191fb8)
       └─ calls Rider_StartRandomCopyWheel (0x801ae4ec)
            └─ HSD_Randi(0xB) picks ability index 0–10
            └─ looks up ability from the table at 0x804af690
            └─ calls Rider_StartCopyWheel (0x801ae550), which lands in randomAbility_giveAbility
```

The table at `0x804af690` is 11 ints, `{0 … 10}` — an identity mapping to CopyKind (`stc_copy_wheel_normal`, `rider.h`). A weighted 29-entry alternative for melee mode sits at `0x804af6bc` (`stc_copy_wheel_melee`).

`zz_80246f40_` resolves the attribute out of the 3D stage collision data hanging off `stc_grobj` (0x805dd6cc), which only `grLoadStage` (0x800ce318) populates, so this path exists in City Trial and Air Ride only.

## Implementation

### Hooks

**1. `CODEPATCH_REPLACEFUNC(Rider_CheckAndGiveAbility, GateAbilities_CheckAndGiveAbility)`**

Gates item/enemy copy ability pickups. Checks `rd->kind == RDKIND_KIRBY`, then the `ability_unlocked_mask` bit, before calling `Rider_GiveAbility`. Also sends a TrapLink (`TRAPLINK_KIND_SLEEP`) when a non-CPU player **successfully** receives COPYSLEEP (gated on `Rider_GiveAbility`'s non-zero return, since it can fail when the rider is in an unable state — avoids phantom traps). Since `Rider_GiveAbility` itself is NOT replaced, AP-granted abilities (via `Ability_GiveItem` → `Rider_GiveAbility` direct) bypass the gate automatically.

**2. `CODEPATCH_REPLACEFUNC(randomAbility_giveAbility, GateAbilities_RandomGiveAbility)`**

Gates the copy chance wheel. If the wheel lands on a locked ability, `RandomUnlockedAbility()` picks a random unlocked one instead; if no abilities are unlocked at all, nothing is given. The replacement reproduces the vanilla sequence (`Rider_AbilityRemoveModel` → `Rider_AbilityClearQueued` → `Rider_RecordCopyAbility` → `stc_ability_init_table[kind](rd)`) and additionally calls `Rider_MarkCopyAbilityObtained` itself with the possibly-substituted kind. `GateAbilities_OnBoot` therefore NOPs the callers' own calls with `CODEPATCH_REPLACEINSTRUCTION(addr, 0x60000000)` at `0x801ae874` (`randomAbility_aPress`) and `0x801ae910` (`randomAbility_autoSelect`), so the obtained-abilities bitmask tracks the ability actually given.

**3. Spawn table filter chain (owned by `item_spawn_filter.c`)**

`FilterAllSpawnTables()` in `item_spawn_filter.c` owns the two hook points and calls each gate file's filters in this order:

1. `GateItems_EnsureAllUpInSpawnPools()` — injects All-Up (active only under the Max Stats Insanity CT goal).
2. Box spawn pools (`grBoxGeneObj`): `GateAbilities_FilterSpawnTables()` → `GatePatches_FilterSpawnTables()` → `GateItems_FilterSpawnTables()`
3. Event drop pools (`grBoxGeneInfo`): `GateAbilities_FilterEventDropTables()` → `GatePatches_FilterEventDropTables()` → `GateItems_FilterEventDropTables()`
4. `GoalMaxStatsCT_ApplyDropBias()` — biases +1 patch / All-Up weights (Max Stats Insanity goal only).

The two `GateAbilities_*` filters always run first within their respective groups. Box pools are compacted (`FilterCopyItemsFromPool`, stable two-pointer); event-drop entries stay in place with all six chance columns zeroed.

| Hook address | Function (entry) | Clobbered instruction | When |
|-------------|-------------|----------------------|------|
| `0x800eb558` | `CityItemSpawn_InitItemFallChances` (0x800eb374) | `lwz r0, 0x34(r1)` | After initial population |
| `0x800ed7f0` | `CityEvent_ModifyItemFallDesc` (0x800ed784) | `lwz r0, 0x14(r1)` | After event reinit |

Both are function epilogue hooks — safe to call C with no arguments. `ItemSpawnFilter_On3DLoadEnd()` is the fallback for non-CT modes where these hooks don't fire.

### Enemy spawn filtering

Enemies themed around locked copy abilities are prevented from spawning by zeroing their weights in the spawn data. This is done in `GateAbilities_On3DLoadEnd()`, which reads `*stc_enemy_spawn_data` and dispatches by mode. The `.dat` data is modified in place; it is reloaded from disc on every stage load.

**Gate condition:** early-exit when `*stc_enemy_spawn_data == NULL` **or** its `config` pointer is NULL (`if (!data || !data->config) return;`). The spawn-data pointer is NULL in any mode without stage-based enemy spawning: City Trial city map, Top Ride, and stadiums other than Kirby Melee (Air Glider, Destruction Derby, Single Race, etc.). No explicit mode check is needed; the NULL check covers all enemy-less cases.

**Spawn data structure** (`EnemySpawnData` in `enemy.h`; accessed via `stc_enemy_spawn_data` at r13 + 0x630 = `0x805dd710`):

| Offset | Field | Meaning |
|-------:|-------|---------|
| 0x00 | `short spawn_count` | Number of entries in `spawn_entries` |
| 0x04 | `EnemySpawnEntry *spawn_entries` | Primary spawn table, stride 0x38 |
| 0x08 | `int x08` | — |
| 0x0C | `int **secondary_table` | Meta-enemy sub-table array, indexed by `enemy_id - 0x50`; may be NULL |
| 0x10 | `EnemySpawnConfig *config` | Mode at `config->mode` (+0x28) |

**Three modes:**

| Mode | Context | IDs offset | Weights offset | Max slots |
|------|---------|-----------|---------------|-----------|
| 1 | Air Ride courses | +0x1E | +0x26 | 4 |
| 2 | `STKIND_MELEE1` (Kirby Melee 1) | Two-stage selection | Two-stage selection | — |
| 3 | `STKIND_MELEE2` (Kirby Melee 2) | +0x06 | +0x10 | 5 |

**Mode 1 / mode 3 filtering** (`FilterMode1Or3`), per spawn entry's id/weight pairs (weight `-1` terminates):

1. Normal enemies: zero the weight if their copy ability is locked (via `EnemyIDToCopyKind`).
2. Meta-enemy IDs (0x50–0x5E): filter the secondary sub-table by zeroing weights for locked-ability enemies. If no entry with positive weight remains, zero the meta-enemy's primary weight too.
3. Each meta-enemy sub-table is filtered only once (tracked via the `meta_valid[]` array).

**Mode 2 filtering** (`FilterMode2`) — Kirby Melee 1 uses a two-stage selection: stage 1 picks a meta-enemy category from the `secondary_table[0]` sub-table (weighted random), stage 2 picks an individual enemy from that category's weight column in the spawn entries. Entry layout: `enemy_id` at +0x06, weight columns at +0x08 (one short per category). Filtering:

1. Zero all weight columns for entries whose `enemy_id` has a locked copy ability.
2. For each category in `secondary_table[0]`, check whether any entry still has positive weight in that column. If not, zero the category's weight in the sub-table to prevent empty selections.

**Enemy ID → CopyKind mapping:** `enemy_slot_copykind[24]` is a per-tier-slot table (`ACTORID_ENEMIES_PER_TIER` = 0x18). T0/T1/T2 share the same slot mapping because the copy ability is tied to the archive, not the tier flags — e.g. T1 Heat Phan-Phan is visually distinct but uses Phan-Phan's Fire archive. `EnemyIDToCopyKind(enemy_id)` mods into the slot table for IDs in `[ACTORID_TIER0_START, ACTORID_SPECIAL_START)` (0x00–0x47) and special-cases `ACTORID_SP_SWORD_KNIGHT` (0x49) → SWORD. All other special IDs (TAC, Dyna Blade, Meteor, etc.) are NONE.

### Mode coverage

The acquisition hooks are not mode-specific — they gate acquisition everywhere `RiderData` exists. `GateAbilities_CheckAndGiveAbility` covers Air Ride (callers: `Machine_OnTouchItem` and the debug menu) and `GateAbilities_RandomGiveAbility` covers Air Ride copy chance wheels (static stage objects).

Top Ride has no copy abilities. Its scene creates neither `MachineData` nor `RiderData`, so `Rider_GiveAbility`, `Rider_GiveRandomAbility` and `randomAbility_giveAbility` are all unreachable there, and it loads no 3D stage collision, so the attribute 0xF copy panels do not exist either. Every acquisition hook above is a no-op in Top Ride.

The one place `ability_unlocked_mask` still matters in Top Ride is the four ability-themed Top Ride **items** — Freeze Fan (TRITEM 9), Fire (11), Bomb (13), Walky (16). `GateTopRideItems_ApplyMask` in `gate_topride_items.c` treats the matching copy ability unlock (`COPYKIND_FREEZE`/`_FIRE`/`_BOMB`/`_MIC`) as an alternative key to the item's own bit in `topride_item_unlocked_mask`: either one enables it. The ability key only counts while `options.ability_gating_enabled` is set — otherwise the all-1s ungated mask would free all four items and strand their own AP unlocks.

## Save Data

`u16 ability_unlocked_mask` in `APSave` (`main.h`, accessed via the global `ap_save`) — bit N = `CopyKind` N.

The mask is exposed through `ArchipelagoAPI` as `AP_UNLOCK_ABILITY`. When the slot option `ability_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` in `main.c` pre-fills the mask with `(1 << COPYKIND_NUM) - 1` at connect.

## AP Items

11 AP items, `AP_ABILITY_UNLOCK_BASE` (760, `archipelago_api.h`) + `CopyKind` index → IDs 760–770. `ap_item_handler.c` routes IDs in `[760, 760 + COPYKIND_NUM)` to `GateAbilities_UnlockAbility(id - AP_ABILITY_UNLOCK_BASE)`, which sets the bit, logs, and enqueues `"Unlock Copy Ability: <name>"` via `tb_api->EnqueueColoredNoun` with `tb_api->AbilityColors[kind]`.

## Design Decisions

**Filter chain ownership:** `item_spawn_filter.c` owns the two spawn table hook points and `FilterAllSpawnTables()` dispatches to each gate file. This makes execution order explicit: All-Up injection → (per pool) abilities → patches → items → drop-weight bias.

**Enemy spawn weight zeroing over spawn-time rejection:** Substituting `enemy_id = -1` at spawn time causes low enemy density because the spawner repeatedly selects locked enemies, gets rejected, and cycles through respawn delays. Weight zeroing preserves density because the spawner never selects locked enemies.

**AP ability bypass:** `Ability_GiveItem` calls `Rider_GiveAbility` directly rather than through the hooked `Rider_CheckAndGiveAbility`. AP-granted abilities are never blocked by the gate.

**Wheel substitution over wheel suppression:** A locked wheel result becomes a random unlocked ability rather than nothing, so inhaling an enemy stays worthwhile as soon as any ability is unlocked.
