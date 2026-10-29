# Max Stats in One Run

A City Trial goal (`GOAL_MAX_STATS_CT`, apworld choice `max_stats_in_one_run`): in one City
Trial run, a single human rider holds the slot's patch cap ceiling on all nine stats at once.
Unlike the other goals it has no checklist cell behind it. The mod sets a sticky save flag,
`APSave.max_stats_ct_achieved`, and the goal evaluator reads that flag for the City Trial row.
While the goal is set the mod also makes All Up appear in every City Trial spawn source, since
one All Up raises all nine stats.

## The check

`On3DLoadEnd` attaches a per-frame proc to every human rider (`AP_AttachHumanRiderProcs`, at
`RDPRI_HITCOLL + 1`) when all of these hold:

- the City Trial row's goal is `GOAL_MAX_STATS_CT`;
- the round is a Trial on the city map - `Gm_IsInCity()` and `CITYMODE_TRIAL` - so Free Run,
  Stadium mode and a Trial's closing stadium never count;
- the round is not the title screen's attract demo;
- the flag is not already set.

The goal in force when the round loads is the one that counts for that round. Before a save has
its slot options every goal reads 0, `GOAL_100_CHECKLIST`, so nothing arms.

Each frame the proc tests the rider's nine stats (`RiderData.stats.values`) with
`PatchCap_IsStatAt` against `PatchCap_GetMax()`, the slot's `city_trial_patch_cap_max` (18-30
in the apworld). Stats count patches from where they start - -2 for the eight that spawn there,
0 for HP - and the target is capped at the raw ceiling `Patch_GetMaxValue` (0x8000aaf0)
enforces, `max - 2`. So the eight need `max` patches and HP `max - 2`, the most it can hold. The
first frame all nine pass, the proc sets the flag, logs the player and target, and runs
`APGoal_Evaluate`, which latches the City Trial row and republishes `goal_complete` for the
client. Later rounds do not arm at all. The flag reaches the card with the game's next save, but
the client reads `goal_complete` from `APData`, so victory is reported without waiting for it.

The target is the ceiling, not the cap in force, and stats cannot grow past the cap in force -
so on a progressive slot (min below max) the goal opens only once every Patch Cap Increase has
arrived. The apworld's logic for the goal requires all of them and, when patches and items are
both gated, every patch type or the All Up unlock.

## All Up in the spawn tables

`GoalMaxStatsCT_EnsureAllUpInPools` runs at the head of the City Trial spawn-table filter pass -
at the end of `CityItemSpawn_InitItemFallChances` (0x800eb374), at the end of
`CityEvent_ModifyItemFallDesc` (0x800ed784), and at scene load where those never run. It acts
only while the City Trial goal is this one and All Up itself is unlocked:

| Target | Added when | Weight |
|---|---|---|
| Each box color's pool (box contents and sky drops) | All Up absent and the pool has room | 8 |
| The Same Item event's pool | same | 8 |
| The follow-up pool a multi-item box draws from | same | 8 |
| The event drop table's All Up row, destructible-object column | the column is 0 | 16 |
| The same row's Dyna Blade column | the column is 0 | 4 |

An All Up already present keeps its vanilla weight. On the city's own table the All Up row
reads 0 / 2 / 2 / 1 / 1 / 10 (Dyna Blade, Tac, meteor, destructible, Secret Chamber, UFO), so
only the Dyna Blade column is filled; the destructible column already holds 1 and is left alone.
Every write fills a gap, so a repeat pass changes nothing, and because the injection runs before
the lock filters, the entries it adds go through them like any other.

## Debug

archipelago_debug's Force-Mark All sets the flag; Clear All sent_checks and Reset Progression
clear it. Setting the goal from its Goals page takes effect from the next round load, since the
proc is armed there.
