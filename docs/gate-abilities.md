# Copy Ability Gating

Each of Kirby's 11 copy abilities can be individually locked behind an Archipelago unlock item. AP items 760-770 (`AP_ABILITY_UNLOCK_BASE` + `CopyKind`) route through `ap_item_handler.c` to `GateAbilities_UnlockAbility`, which sets the bit in `APSave.ability_unlocked_mask` and posts a textbox. A locked ability cannot be obtained from *any* source - copy panels, the copy chance wheel, or enemy inhale - and its item panels and themed enemies are filtered out of the spawn tables so the locked state is invisible rather than teasing.

**Files:** `mods/archipelago/src/gate_abilities.c` (gating hooks + enemy-spawn filtering); `ability_item.c` (`Ability_GiveItem` AP grant path, `Ability_ItKindToCopyKind` ITKIND to CopyKind mapping).

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

The mask is a `u16` in `APSave` (`main.h`, via the global `ap_save`), exposed through `ArchipelagoAPI` as `AP_UNLOCK_ABILITY`. When the slot option `ability_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` (`main.c`) pre-fills it with `(1 << COPYKIND_NUM) - 1` at connect.

## Acquisition Paths

Copy abilities can be obtained four ways, all of which need gating.

**Item pickup and enemy touch.** `Machine_OnTouchItem` (0x801db34c, case 0x1a) calls `Rider_CheckAndGiveAbility` (0x80192650), which checks `rd->kind == RDKIND_KIRBY` and then calls the master grant `Rider_GiveAbility` (0x801a81a4). `Rider_CheckAndGiveAbility` is the single entry point for every non-wheel grant.

**Copy chance wheel (inhale an enemy).** A roulette wheel spins and lands on a random ability. `randomAbility_mainLoop` (0x801a5fb8) runs it per frame through `rd->cb_copy_input` (+0x930); `randomAbility_checkIfaPress` (0x801ae7c8) and `randomAbility_aPress` (0x801ae7f4) stop it on A, `randomAbility_autoSelect` (0x801ae890) does the same on timer expiry, and `randomAbility_getItemID` (0x801aea30) reads the landed slot as `rd->x9b0[rd->x99c]`. Both stop paths commit through `randomAbility_giveAbility` (0x801a61d4), whose vanilla body is `Rider_AbilityRemoveModel` -> `Rider_AbilityClearQueued` -> `Rider_RecordCopyAbility` (0x8022ee00) -> `stc_ability_init_table[kind](rd)` (the 11 per-CopyKind init function pointers at 0x804af4f0, e.g. `ability_Fire` at 0x801af474). `randomAbility_queuedGive` (0x801aec60) is the timer-based give for a queued ability. `Rider_MarkCopyAbilityObtained` (0x8022f150) is called by `giveAbility`'s *callers* (0x801ae874 in `aPress`, 0x801ae910 in `autoSelect`), not by `giveAbility` itself - which matters for the hook below.

**Ground copy panels (collision attribute 0xF).** Floor polygons tagged with ground collision attribute 0xF spin the wheel when a machine drives over them. `Machine_ProcessEnvColl` (0x801e5108) detects the attribute via `zz_80246f40_` (0x80246f40), checks the panel differs from last frame's stored ID, and calls `Rider_GiveRandomAbility` (0x80191fb8) -> `Rider_StartRandomCopyWheel` (0x801ae4ec), which rolls `HSD_Randi(0xB)`, looks the ability up in the table at 0x804af690 (`stc_copy_wheel_normal`, `rider.h` - 11 ints, an identity mapping to CopyKind; a weighted 29-entry melee alternative sits at 0x804af6bc as `stc_copy_wheel_melee`), and calls `Rider_StartCopyWheel` (0x801ae550), landing in `randomAbility_giveAbility`. `zz_80246f40_` resolves the attribute out of the 3D stage collision data hanging off `stc_grobj` (0x805dd6cc), which only `grLoadStage` (0x800ce318) populates, so this path exists in City Trial and Air Ride only.

**Copy panels from boxes and event drops.** Copy panels are regular items in the `grBoxGeneObj` spawn table system (at `*(0x805dd0e0 + 0x608)`), whose pools are `item_group_spawn[BOXKIND_NUM]` (per box type, each with parallel `it_kind`/`chance` arrays and a `num`), `sameitem_*` ("All Same Item" event) and `subsequent_*` (blue box multi-item). The event drop table is `grBoxGeneInfo->item_desc->event_source_drop` (+0x18, count at +0x1c), per-item entries with one chance field per drop source: dyna, tac, meteor, destructible, chamber, ufo.

## Acquisition Hooks

**`CODEPATCH_REPLACEFUNC(Rider_CheckAndGiveAbility, GateAbilities_CheckAndGiveAbility)`** gates item and enemy pickups: it checks `rd->kind == RDKIND_KIRBY`, then the mask bit, before calling `Rider_GiveAbility`. It also sends a TrapLink (`TRAPLINK_KIND_SLEEP`) when a non-CPU player **successfully** receives COPYSLEEP - gated on `Rider_GiveAbility`'s nonzero return, since the grant can fail when the rider is in an unable state, which would otherwise emit a phantom trap.

`Rider_GiveAbility` itself is deliberately **not** replaced. `Ability_GiveItem` (the AP grant path) calls it directly, so an AP-granted ability - including one bought with EnergyLink - bypasses the gate and applies whether or not its unlock item has arrived. That path also reaches the rider without touching any per-kind item data (it only indexes `stc_ability_init_table`), which is why `APItems_HandleItem` runs the copy-ability branch above its Free Run / stadium gate: the grants land in every 3D mode, at the cost of no pickup visual.

**`CODEPATCH_REPLACEFUNC(randomAbility_giveAbility, GateAbilities_RandomGiveAbility)`** gates the wheel. If it lands on a locked ability, `RandomUnlockedAbility()` substitutes a random unlocked one, so inhaling an enemy stays worthwhile as soon as anything is unlocked.

With nothing unlocked there is no substitute, and the replacement must still resolve the rider's state: it calls `Rider_AbilityRemoveModel` -> `Rider_AbilityClearQueued` -> `Rider_ResolveQueuedAbility` (0x801a8454) before returning 0. Both callers reach this function from an action-state with no other exit - the post-swallow state entered at 0x801b9a54 runs `randomAbility_queuedGive` every frame until the grant transitions the rider out, and the wheel commit has already torn down the wheel model and cleared `cb_copy_input` by the time it calls the grant. Returning without a transition leaves the rider stuck for the rest of the match with no inhale and no quick spin. `Rider_ResolveQueuedAbility` is the engine's own "nothing to give" step (the tail of `Rider_StartCopyWheel` and the exit of the inhale START state): it grants a pending queued ability if there is one, else runs the IASA fallback chain and settles on `AS_StarWait` (0x801ab1a0, `RiderStateChange` 0x21).

On the success path the replacement reproduces the vanilla sequence and additionally calls `Rider_MarkCopyAbilityObtained` itself, with the possibly-substituted kind. `GateAbilities_OnBoot` therefore NOPs the callers' own calls with `CODEPATCH_REPLACEINSTRUCTION(addr, 0x60000000)` at 0x801ae874 and 0x801ae910, so the obtained-abilities bitmask tracks the ability actually given rather than the one the wheel showed.

## Spawn Table Filtering

`item_spawn_filter.c` owns the two spawn-table hook points; `FilterAllSpawnTables()` calls each gate file's filters in a fixed order:

1. `GateItems_EnsureAllUpInSpawnPools()` - injects All-Up (active only under the Max Stats Insanity CT goal).
2. Box pools (`grBoxGeneObj`): `GateAbilities_FilterSpawnTables()` -> `GatePatches_FilterSpawnTables()` -> `GateItems_FilterSpawnTables()`.
3. Event drop pools (`grBoxGeneInfo`): `GateAbilities_FilterEventDropTables()` -> `GatePatches_FilterEventDropTables()` -> `GateItems_FilterEventDropTables()`.
4. `GoalMaxStatsCT_ApplyDropBias()` - biases +1 patch / All-Up weights (Max Stats Insanity goal only).

The two `GateAbilities_*` filters always run first within their group. Box pools are compacted (`FilterCopyItemsFromPool`, a stable two-pointer); event-drop entries stay in place with all six chance columns zeroed.

| Hook address | Function (entry) | Clobbered instruction | When |
|-------------|-------------|----------------------|------|
| 0x800eb558 | `CityItemSpawn_InitItemFallChances` (0x800eb374) | `lwz r0, 0x34(r1)` | After initial population |
| 0x800ed7f0 | `CityEvent_ModifyItemFallDesc` (0x800ed784) | `lwz r0, 0x14(r1)` | After event reinit |

Both are function-epilogue hooks, so calling C with no arguments is safe. `ItemSpawnFilter_On3DLoadEnd()` is the fallback for non-CT modes where these hooks don't fire.

## Enemy Spawn Filtering

Enemies themed around locked abilities never spawn: `GateAbilities_On3DLoadEnd()` zeroes their weights in the stage's enemy spawn data, which is reloaded from disc on every stage load so editing in place is safe. Zeroing weights beats rejecting at spawn time - substituting `enemy_id = -1` makes the spawner repeatedly select a locked enemy, get rejected and cycle through the respawn delay, which visibly thins enemy density.

The filter reads `*stc_enemy_spawn_data` (`EnemySpawnData` in `enemy.h`, pointer at r13+0x630 = 0x805dd710) and early-exits when it or its `config` pointer is NULL. That covers every mode without stage-based enemy spawning - the City Trial city map, Top Ride, and every stadium but Kirby Melee - so no explicit mode check is needed. `config->mode` then selects one of three layouts:

| Mode | Context | IDs offset | Weights offset | Max slots |
|------|---------|-----------|---------------|-----------|
| 1 | Air Ride courses | +0x1E | +0x26 | 4 |
| 2 | `STKIND_MELEE1` (Kirby Melee 1) | Two-stage selection | Two-stage selection | - |
| 3 | `STKIND_MELEE2` (Kirby Melee 2) | +0x06 | +0x10 | 5 |

**Modes 1 and 3** (`FilterMode1Or3`) walk each spawn entry's id/weight pairs (weight -1 terminates) and zero the weight of any normal enemy whose ability is locked. Meta-enemy IDs (0x50-0x5E) instead index `data->secondary_table`, a sub-table array indexed by `enemy_id - 0x50`; the sub-table is filtered the same way, and if no entry with positive weight survives, the meta-enemy's own primary weight is zeroed too. Each sub-table is filtered only once, tracked in `meta_valid[]`.

**Mode 2** (`FilterMode2`) covers Kirby Melee 1, whose `Enemy_SpawnerDecideMode2` (0x800f0efc) selects in two stages: stage 1 picks a meta-enemy category out of the `secondary_table[0]` sub-table, stage 2 picks an individual enemy from that category's weight column in the spawn entries (`enemy_id` at +0x06, weight columns at +0x08). Two details of the vanilla picker drive the filter:

- The **column index is the category's meta id minus 0x50**, not its position in the sub-table (`addi r0,r3,-80` at 0x800f0fdc, then `slwi r31,r0,1` at 0x800f1020 as the byte offset into the columns). Vanilla `GrPasture1` lists ids 0x50-0x59 in order so the two coincide there, but the filter derives the column from the id.
- The sub-table's second short per pair is an **ascending threshold**, not a weight: stage 1 walks the pairs and takes the first whose value exceeds `total * (1 - EnemyMgr.time_progress)`. A threshold of 0 is therefore never selected, which is what makes zeroing it a valid way to retire a category.

So the filter zeroes the weight column of every category for entries whose enemy has a locked ability, then zeroes the threshold of any category left with no positive weight anywhere in its column.

`EnemySpawnEntry.mode2.weight_columns` is declared with the full width the entry has room for (20 shorts, +0x08..+0x2F) rather than the stage's category count. A shorter declaration lets the compiler assume every index is 0 and fold the zeroing loop down to a single store, leaving all columns but the first live and locked-ability enemies spawning.

**Enemy ID to CopyKind.** `enemy_slot_copykind[24]` is a per-tier-slot table (`ACTORID_ENEMIES_PER_TIER` = 0x18). T0/T1/T2 share one slot mapping because the ability is tied to the archive, not the tier flags - T1 Heat Phan-Phan is visually distinct but uses Phan-Phan's Fire archive. `EnemyIDToCopyKind(enemy_id)` mods into the slot table for IDs in `[ACTORID_TIER0_START, ACTORID_SPECIAL_START)` (0x00-0x47) and special-cases `ACTORID_SP_SWORD_KNIGHT` (0x49) as SWORD. All other special IDs (TAC, Dyna Blade, Meteor, etc.) are NONE.

## Mode Coverage

The acquisition hooks are not mode-specific - they gate acquisition everywhere `RiderData` exists, so `GateAbilities_CheckAndGiveAbility` also covers Air Ride (callers: `Machine_OnTouchItem` and the debug menu) and `GateAbilities_RandomGiveAbility` covers Air Ride's static-stage copy wheels.

Top Ride has no copy abilities at all. Its scene creates neither `MachineData` nor `RiderData`, so `Rider_GiveAbility`, `Rider_GiveRandomAbility` and `randomAbility_giveAbility` are unreachable there, and it loads no 3D stage collision, so the attribute 0xF panels do not exist either. Every acquisition hook is a no-op in Top Ride.

The one place the mask still matters in Top Ride is the four ability-themed Top Ride **items** - Freeze Fan (TRITEM 9), Fire (11), Bomb (13), Walky (16). `GateTopRideItems_ApplyMask` in `gate_topride_items.c` treats the matching copy ability unlock (`COPYKIND_FREEZE`/`_FIRE`/`_BOMB`/`_MIC`) as an alternative key to the item's own bit in `topride_item_unlocked_mask`: either one enables it. The ability key counts only while `options.ability_gating_enabled` is set - otherwise the all-1s ungated mask would free all four items and strand their own AP unlocks.
