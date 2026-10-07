# Max Stats in One Run (GOAL_MAX_STATS_CT)

A City Trial goal: in a single City Trial Trial run, one human rider holds the slot's patch-cap
ceiling worth of patches on all nine stats at once. The AP world offers it as the City Trial
goal "Max Stats in One Run" (`GOAL_MAX_STATS_CT`, 5). No checklist square backs it - the mod
latches `APSave.max_stats_ct_achieved` and the goal evaluator reads that flag.

While the goal is set the mod also adds All Up to every City Trial spawn table that lacks it,
since one All Up advances all nine stats at once. Every other spawn weight stays vanilla.

Implementation: `mods/archipelago/src/goal_max_stats_ct.c` / `.h`, driven from `main.c`
(`On3DLoadStart`, `On3DLoadEnd`) and from the spawn-table hooks in `item_spawn_filter.c`.

## The check

`GoalMaxStatsCT_On3DLoadEnd` arms the check by attaching `GoalMaxStatsCT_PerFrame` to every
human rider GObj through `AP_AttachHumanRiderProcs` (proc priority `RDPRI_HITCOLL + 1`). It does
so only when all of these hold:

- the round is not the title screen's attract demo (`Gm_IsAutoDemo`);
- the City Trial row's goal (`options.goal[GMMODE_CITYTRIAL]`) is `GOAL_MAX_STATS_CT`;
- the round is a Trial on the city map - `Gm_IsInCity()`, which is true on the CT main map only,
  and `Gm_GetCityMode() == CITYMODE_TRIAL` - so Free Run, Stadium mode and a Trial's closing
  stadium never arm it;
- `max_stats_ct_achieved` is not already set.

Because the check is armed at round load, the goal in force when the round loads is the one
that counts for that round. Before the slot options arrive every row's goal reads 0,
`GOAL_100_CHECKLIST`, so nothing arms on a save that has never connected.

Each frame the proc reads the rider's nine stats (`RiderData.stats.values`) and tests each with
`PatchCap_IsStatAt(values, kind, PatchCap_GetMax())`. The target is the slot's ceiling,
`city_trial_patch_cap_max` (18-30 in the AP world), not the cap currently in force. Counts are
patches above each stat's start value - 0 for HP, -2 for the other eight - and the target is
capped at the raw ceiling `Patch_GetMaxValue` enforces, `max - 2`. Every stat therefore needs
raw `max - 2`: `max` patches for the eight, and `max - 2` for HP, which the game stops there.

The first frame all nine pass, the proc sets `max_stats_ct_achieved`, logs
`[GoalMaxStatsCT] Player N reached T patches on all 9 stats, goal latched`, and calls
`APGoal_Evaluate`, which latches the City Trial row in `goal_latched` and republishes
`goal_satisfied_mask` / `goal_complete` for the client. From then on every armed proc returns at
once, and later rounds do not arm at all. Nothing writes the card on the spot - the flag rides in
the save block to the game's next save point - but the client reads `goal_complete` from `APData`,
so victory reaches the server without waiting for it.

The cap shapes what is reachable. Stats cannot grow past the cap in force (`PatchCap_GetCap`),
so on a progressive slot (`min < max`) the goal only becomes possible once every Patch Cap
Increase has arrived. The AP world's logic for the goal requires all of them and, when both
patches and items are gated, either every patch-kind unlock or the All Up unlock.

The debug mod's Force-Mark All sets the flag; Clear All sent_checks (`APGoal_Reset`) and Reset
Progression clear it.

## Spawn tables

`FilterAllSpawnTables` in `item_spawn_filter.c` runs the whole spawn-table pass, hooked at the
end of `CityItemSpawn_InitItemFallChances` (`0x800eb558` in `0x800eb374`) and at the end of
`CityEvent_ModifyItemFallDesc` (`0x800ed7f4` in `0x800ed784`), and from `On3DLoadEnd` as a
fallback in the modes that never run the City Trial init. Its order is fixed:

1. `GoalMaxStatsCT_EnsureAllUpInPools` - inject All Up;
2. the gate filters - remove locked kinds from the box pools, zero locked rows of the event-drop
   table.

Injecting first sends the new entries through the same lock filters as everything else.

### All Up injection

`GoalMaxStatsCT_EnsureAllUpInPools` does nothing unless the City Trial goal is
`GOAL_MAX_STATS_CT` and All Up is unlocked (`GateItems_IsItemLocked(ITKIND_ALLUP)` is 0). It
checks only those two, so it acts on every pass of the filter, not only in Trial rounds.

| Target | Added when | Weight |
|---|---|---|
| Each of the three `item_group_spawn[box]` pools (box contents and sky drops) | All Up absent and the pool has room | `ALLUP_BOX_POOL_CHANCE`, 8 |
| The `sameitem` pool (Same Item event) | same | 8 |
| The `subsequent` pool | same | 8 |
| `event_source_drop`'s All Up row, `chance_destructible` | column is 0 | `ALLUP_CHANCE_DESTRUCTIBLE`, 16 |
| `event_source_drop`'s All Up row, `chance_dyna` | column is 0 | `ALLUP_CHANCE_DYNA`, 4 |

`EnsureItemInPool` appends `ITKIND_ALLUP` only if it is missing and the pool is under its
capacity (`ITKIND_NUM - 1` entries for the box and Same Item pools, 40 for `subsequent`); an All
Up already present keeps its own weight. In the event table the destructible and Dyna Blade
columns are the two sources the vanilla All Up row leaves at zero, so those are the two topped
up. Every write fills a gap, so a repeat pass changes nothing.

## Logging

`[GoalMaxStatsCT]` prints one line per armed round with the number of riders and the target,
and one line when the goal latches. Nothing per frame.
