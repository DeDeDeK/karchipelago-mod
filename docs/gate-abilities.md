# Copy Ability Gating

## Overview

11 CopyKinds (FIRE, WHEEL, SLEEP, SWORD, BOMB, PLASMA, NEEDLE, MIC, FREEZE, TORNADO, BIRD) can be individually locked. When locked, the ability cannot be obtained from *any* source — copy panels, copy chance wheels, or enemy inhale. The ability item panels are also filtered from spawn tables, and ability-themed enemies are filtered from enemy spawn tables across all modes (Air Ride, City Trial stadiums).

## Game Systems — Three Acquisition Paths

Copy abilities can be obtained three ways, all of which need gating:

### 1. Copy Panels from Boxes/Event Drops

Copy ability panels are regular items in the `grBoxGeneObj` spawn table system and the `event_source_drop` table. The item-to-ability mapping (ITKIND → CopyKind), implemented by `Ability_ItKindToCopyKind` in `ability_item.c`:

- `ITKIND_COPYFIRE` → FIRE, `ITKIND_COPYTIRE` → WHEEL, `ITKIND_COPYSLEEP` → SLEEP
- `ITKIND_COPYSWORD` → SWORD, `ITKIND_COPYBOMB` → BOMB, `ITKIND_COPYPLASMA` → PLASMA
- `ITKIND_COPYSPIKE` → NEEDLE, `ITKIND_COPYMIC` → MIC, `ITKIND_COPYICE` → FREEZE
- `ITKIND_COPYTORNADO` → TORNADO, `ITKIND_COPYBIRD` → BIRD

Box spawn pools (`grBoxGeneObj` at `*(0x805dd0e0 + 0x608)`):
- `item_group_spawn[BOXKIND_NUM]` — per-box-type pools. Each has `it_kind[ITKIND_NUM-1]` (68), `chance[ITKIND_NUM-1]` (68), `num`.
- `sameitem_it_kind/chance/num` — "All Same Item" event pool.
- `subsequent_it_kind/chance/num` — blue box multi-item pool.

Event drop table (`grBoxGeneInfo->item_desc->event_source_drop`):
- Per-item entries with one chance field per drop source: `chance_dyna`, `chance_tac`, `chance_meteor`, `chance_destructible`, `chance_chamber`, `chance_ufo`. `GateAbilities_FilterEventDropTables()` zeroes all six for any entry whose `it_kind` maps to a locked ability. (See `docs/event-source-drops.md` for the field-to-source map.)

### 2. Copy Chance Wheel (Inhale Enemy)

When Kirby inhales an enemy, a roulette wheel spins and lands on a random ability. The wheel system:

| Function | Address | Purpose |
|----------|---------|---------|
| `randomAbility_mainLoop` | 0x801a5fb8 | Per-frame wheel callback via `rd->cb_copy_input` (0x930) |
| `randomAbility_checkIfaPress` | 0x801ae7c8 | Checks A press to stop wheel |
| `randomAbility_aPress` | 0x801ae7f4 | Stops wheel, calls giveAbility |
| `randomAbility_autoSelect` | 0x801ae890 | Auto-selects on timer expiry (same logic as aPress) |
| `randomAbility_getItemID` | 0x801aea30 | Reads wheel position: `rd->x9b0[rd->x99c]` |
| `randomAbility_giveAbility` | 0x801a61d4 | Gives ability: `Rider_AbilityRemoveModel` → `Rider_AbilityClearQueued` → `Rider_RecordCopyAbility` → `stc_ability_init_table[kind](rd)`. (Replacement inserts `Rider_MarkCopyAbilityObtained` before the init call.) |
| `randomAbility_removeWheelModel` | 0x801a66d0 | Removes wheel 3D model |
| `randomAbility_queuedGive` | 0x801aec60 | Timer-based give for queued abilities |

Ability init function pointer table `stc_ability_init_table` at `0x804af4f0` (declared in `rider.h`): 11 entries, one per CopyKind, each pointing to the ability init function (e.g., `ability_Fire` at `0x801af474`). The wheel replacement invokes `stc_ability_init_table[kind](rd)` directly.

Tracking functions (must be called by any replacement):
- `Rider_RecordCopyAbility(ply, kind)` (0x8022ee00) — records ability history, checks sequences for checklist achievements
- `Rider_MarkCopyAbilityObtained(ply, kind)` (0x8022f150) — sets bit in per-player obtained-abilities bitmask (called by callers of giveAbility, not by giveAbility itself)

### 3. Item Pickup / Enemy Touch

When a machine touches a copy panel item: `Machine_OnTouchItem` (0x801db34c, case `0x1a`) → `Rider_CheckAndGiveAbility` (0x80192650) → `Rider_GiveAbility` (0x801a81a4).

`Rider_CheckAndGiveAbility` checks `rd->kind == RDKIND_KIRBY` then calls `Rider_GiveAbility`. This is the single entry point for all non-wheel copy ability grants (items and enemies).

## Implementation

**Files:** `gate_abilities.c` / `gate_abilities.h` (gating hooks + enemy-spawn filtering); `ability_item.c` / `ability_item.h` (`Ability_GiveItem` AP grant path, `Ability_ItKindToCopyKind` ITKIND→CopyKind mapping).

### Hooks

**1. `CODEPATCH_REPLACEFUNC(Rider_CheckAndGiveAbility, GateAbilities_CheckAndGiveAbility)`**

Gates item/enemy copy ability pickups. Checks `rd->kind == RDKIND_KIRBY`, then the `ability_unlocked_mask` bit, before calling `Rider_GiveAbility`. Also sends a TrapLink (`TRAPLINK_KIND_SLEEP`) when a non-CPU player **successfully** receives COPYSLEEP (gated on `Rider_GiveAbility`'s non-zero return, since it can fail when the rider is in an unable state — avoids phantom traps). Since `Rider_GiveAbility` itself is NOT replaced, AP-granted abilities (via `Ability_GiveItem` → `Rider_GiveAbility` direct) bypass the gate automatically.

**2. `CODEPATCH_REPLACEFUNC(randomAbility_giveAbility, GateAbilities_RandomGiveAbility)`**

Gates the copy chance wheel. If the wheel lands on a locked ability, picks a random unlocked one instead via `RandomUnlockedAbility()`. If no abilities are unlocked at all, gives nothing. The replacement also NOPs the `Rider_MarkCopyAbilityObtained` calls in `randomAbility_aPress` (0x801ae874) and `randomAbility_autoSelect` (0x801ae910), calling it internally with the possibly-substituted kind so the obtained-abilities bitmask tracks correctly.

**3. Spawn Table Filter Chain (owned by `item_spawn_filter.c`)**

`FilterAllSpawnTables()` in `item_spawn_filter.c` owns the two hook points and calls each gate file's filters in this order:

1. `GateItems_EnsureAllUpInSpawnPools()` — injects All-Up (active only under the Max Stats Insanity CT goal).
2. Box spawn pools (`grBoxGeneObj`): `GateAbilities_FilterSpawnTables()` → `GatePatches_FilterSpawnTables()` → `GateItems_FilterSpawnTables()`
3. Event drop pools (`grBoxGeneInfo`): `GateAbilities_FilterEventDropTables()` → `GatePatches_FilterEventDropTables()` → `GateItems_FilterEventDropTables()`
4. `GoalMaxStatsCT_ApplyDropBias()` — biases +1 patch / All-Up weights (Max Stats Insanity goal only).

The two `GateAbilities_*` filters always run first within their respective groups.

| Hook Address | Function End | Clobbered Instruction | When |
|-------------|-------------|----------------------|------|
| `0x800eb558` | `CityItemSpawn_InitItemFallChances` (0x800eb374) | `lwz r0, 0x34(r1)` | After initial population |
| `0x800ed7f0` | `CityEvent_ModifyItemFallDesc` (0x800ed784) | `lwz r0, 0x14(r1)` | After event reinit |

Both are function epilogue hooks — safe to call C with no arguments. A stadium fallback in `On3DLoadEnd` handles non-CT modes where these hooks don't fire.

### Enemy Spawn Filtering

Enemies themed around locked copy abilities are prevented from spawning by zeroing their weights in the spawn data. This is done in `GateAbilities_On3DLoadEnd()`, which reads `*stc_enemy_spawn_data` and dispatches by mode.

**Gate condition:** early-exit when `*stc_enemy_spawn_data == NULL` **or** its `config` pointer is NULL (`if (!data || !data->config) return;`). The spawn-data pointer is NULL in any mode without stage-based enemy spawning: City Trial city map, Top Ride, and stadiums other than Kirby Melee (Air Glider, Destruction Derby, Single Race, etc.). No explicit mode check needed; the NULL check covers all enemy-less cases.

**Spawn data structure** (`EnemySpawnData` in `enemy.h`; accessed via `stc_enemy_spawn_data` at r13 + 0x630 = `0x805dd710`):

```c
typedef struct EnemySpawnData {
    short spawn_count;          // 0x00, number of entries in spawn_entries
    short pad02;                // 0x02
    char *spawn_entries;        // 0x04, primary spawn table (stride 0x38)
    int x08;                    // 0x08
    int **secondary_table;      // 0x0C, meta-enemy sub-table array (may be NULL)
    EnemySpawnConfig *config;   // 0x10, mode at config->mode (+0x28)
} EnemySpawnData;
```

**Three modes:**

| Mode | Context | IDs Offset | Weights Offset | Max Slots |
|------|---------|-----------|---------------|-----------|
| 1 | Air Ride courses | +0x1E | +0x26 | 4 |
| 2 | `STKIND_MELEE1` (Kirby Melee 1) | Two-stage selection | Two-stage selection | — |
| 3 | `STKIND_MELEE2` (Kirby Melee 2) | +0x06 | +0x10 | 5 |

**Mode 1 / Mode 3 filtering** (`FilterMode1Or3`):

For each spawn entry's enemy ID/weight pairs:
1. Normal enemies: zero weight if their copy ability is locked (via `EnemyIDToCopyKind`).
2. Meta-enemy IDs (0x50–0x5E): filter the secondary sub-table by zeroing weights for locked-ability enemies. If no entries with positive weight remain, zero the meta-enemy's primary weight too.
3. Each meta-enemy sub-table is filtered only once (tracked via `meta_valid[]` array).

**Mode 2 filtering** (`FilterMode2`):

Mode 2 uses a two-stage selection system (Kirby Melee 1):
1. **Stage 1**: picks a meta-enemy category from `secondary_table[0]` sub-table (weighted random).
2. **Stage 2**: picks an individual enemy from that category's weight column in the spawn entries.

Entry layout: `enemy_id` at +0x06, weight columns at +0x08 (one short per category).

Filtering:
1. Zero all weight columns for entries whose `enemy_id` has a locked copy ability.
2. For each category in `secondary_table[0]`, check if any entries still have positive weight in that column. If not, zero the category's weight in the sub-table to prevent empty selections.

**Enemy ID → CopyKind mapping:**

`enemy_slot_copykind[24]` is a per-tier-slot table (T0/T1/T2 share the same slot mapping because the copy ability is tied to the archive, not the tier flags — e.g., T1 Heat Phan-Phan is visually distinct but uses Phan-Phan's Fire archive). `EnemyIDToCopyKind(enemy_id)` mods into the slot table for IDs 0–71 and special-cases `ACTORID_SP_SWORD_KNIGHT` (0x49) → SWORD. All other special IDs (TAC, Dyna Blade, Meteor, etc.) are NONE.

### Air Ride Mode

The ability acquisition hooks are NOT mode-specific — they gate acquisition everywhere:

- `GateAbilities_CheckAndGiveAbility` works in Air Ride (callers: `Machine_OnTouchItem` and debug menu)
- `GateAbilities_RandomGiveAbility` works in Air Ride (copy chance wheels are static stage objects)

### Top Ride

- **Copy ability roulette** (ground attr 0xF panels): gated by `GateAbilities_RandomGiveAbility`, same as other modes.
- **Ability-themed items** (Freeze, Fire, Needle, Bomb, Mike): gated by `ability_unlocked_mask` in `GateTopRideItems_ApplyMask`. These bypass the TR item unlock mask — no separate AP items. See `gate_topride_items.c` and `topride-item-system.md`.

## Save Data

`u16 ability_unlocked_mask` in the `APSave` struct (`main.h`) — bit N = CopyKind N.

## AP Items

11 items, `AP_ABILITY_UNLOCK_BASE` (760) + CopyKind index. IDs 760–770.

## Design Decisions

**Filter chain ownership:** `item_spawn_filter.c` owns the two spawn table hook points and `FilterAllSpawnTables()` dispatches to each gate file. This makes execution order explicit: All-Up injection → (per pool) abilities → patches → items → drop-weight bias.

**Enemy spawn weight zeroing over spawn-time rejection:** Substituting `enemy_id=-1` at spawn time causes low enemy density because the spawner repeatedly selects locked enemies, gets rejected, and cycles through respawn delays. Weight zeroing preserves density because the spawner never selects locked enemies.

**AP ability bypass:** `Ability_GiveItem` calls `Rider_GiveAbility` directly rather than through the hooked `Rider_CheckAndGiveAbility`. AP-granted abilities are never blocked by the gate.
