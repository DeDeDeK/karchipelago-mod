# Stat Patches: Cap and Permanent Patches

Two Archipelago mechanics change how City Trial's stat patches work. The **patch cap** replaces
the game's fixed per-stat ceiling with one the slot sets, optionally starting low and raised by
Patch Cap Increase items. **Permanent patches** are received +1 stat boosts that the mod
re-applies at the start of every round. Both count in patches, so both are described here,
along with how they meet.

## Counting in patches

A machine's nine stats live in `MachineData.stats.values` as floats. Eight of them start a round
at -2 and HP at 0, and every +1 patch adds 1. Vanilla clamps each stat to the raw value
`Patch_GetMaxValue` (0x8000aaf0) returns - 16 from `gmGameParams.patch_max`, so 18 patches for
the eight and 16 for HP - and sign-extends it from a byte (`extsb`), which makes 127 the largest
value that works (`PATCH_STAT_MAX`).

The mod measures everything in patches from each stat's start (`PatchCap_GetStatStart`: -2, or
0 for HP), so the cap means the same number of patches on all nine stats.

## The patch cap

Two slot options set it:

| Option | apworld range | Meaning |
|---|---|---|
| `city_trial_patch_cap_min` | 1-18 | The cap a save starts at |
| `city_trial_patch_cap_max` | 18-30 | The ceiling, and the Max Stats goal's target |

The cap in force is `min + patch_cap_count`, up to `max`; each Patch Cap Increase (item 5)
raises `APSave.patch_cap_count` by one. The apworld ships `max - min` of them, so `min == max`
is a flat cap with none. 18/18, the default, plays like vanilla. Until the save takes its slot
options both the cap and the ceiling read 18, and the max is clamped to 127; nothing is
precomputed, so every clamp reads the options live.

`PatchCap_OnBoot` replaces three functions:

| Replaced | Replacement does |
|---|---|
| `Patch_GetMaxValue` (0x8000aaf0) | Returns `max - 2` (at least 1) |
| `Machine_GivePatch` (0x801cacf4) | Clamps a positive delta to the room left under the cap, applies it, refreshes the machine |
| `Machine_GiveAllUp` (0x801cad40) | The same per stat, then credits the player's All Up count |

**`Patch_GetMaxValue` returns the ceiling, not the current cap.** It is both the raw clamp of
every stat and the denominator `Machine_GetStatRatio` (0x801caa8c) uses to normalize a stat for
the HUD bars and for the machine's stat-to-attribute interpolation. `max - 2` keeps a non-HP stat
at the slot ceiling at ratio 1, exactly as a vanilla stat at 18 patches, while the cap grows from
`min` underneath it - so a patch moves the curve the same amount from the first item to the
last, and a ceiling above 18 makes each patch worth proportionally less. HP, starting at 0,
meets that raw clamp after `max - 2` patches, two short of the others, as in vanilla.

**The delta clamp** gives a positive delta at most `start + cap - current`. Negative deltas -
stat downs, the drop-patches trap - pass through. The give then runs the vanilla
`Machine_ApplyStatClamped` (0x801e094c), whose second clamp to `Patch_GetMaxValue` is what stops
HP. Both replacements finish like vanilla: `Machine_UpdateAppearance`, then
`Machine_AdjustAttributes` unless the machine sets `suppress_attr_recalc` (Wing Kirby and Wheel
Kirby, whose attributes are fixed). The All Up credit uses the uncapped count, as vanilla does,
so a capped All Up still counts as picked up.

Every consumer of the stat ceiling - the stat clamps, the HUD (`PlyView_HudThink`), both ratio
functions and the appearance update - goes through `Patch_GetMaxValue`, which is why replacing
it is complete. The clamps apply in every mode despite the `city_trial_` names; nothing else in
Air Ride raises stats.

A Patch Cap Increase applies in any scene and announces "Patch cap increased! (cap/max)". An
increase past the ceiling still announces, with the cap unchanged.

## Permanent patches

Items 100-108 (`AP_PERM_PATCH_BASE` + `PatchKind`) each add one to that stat's entry in
`APSave.permanent_patches[9]`; Permanent All Up (8) adds one to all nine. Entries stop at 127.
Receiving one writes only the save and applies in any scene - not to be confused with All Up
(7), a one-time All Up in the current round.

The stats land at round start. `On3DLoadEnd` creates a proc that waits for the intro to end,
then for each human machine gives `Machine_GiveAllUp(min)` - `min` being the smallest of the
nine counts - and `Machine_GivePatch` for each stat's remainder: one All Up plus a few patches
instead of nine patch calls, each of which re-runs the appearance and attribute updates. The
proc does this once per scene load. Nothing applies a permanent patch mid-round, so the save is
the only source of truth: one received during a round counts from the next.

Where it applies follows the scene major and city mode, not `Gm_IsInCity()` (0x8000acb0), which
is true on the city map only:

| Round | Applied |
|---|---|
| City Trial Trial, city map | When Permanent Patches -> City Trial is on |
| City Trial Trial, closing stadium | Never - the machine carries its city stats in, patches included |
| Stadium mode | When Permanent Patches -> CT Stadium is on |
| City Trial Free Run | Never - it loads no item data, and a patch knocked loose would crash |
| Air Ride | When Permanent Patches -> Air Ride is on |
| Top Ride | Never reached; it has no machines |

The settings default on. The closing stadium is told apart with `CityTrial_IsInStadium`
(0x8000ad48).

### Interactions

- **The cap clamps them** like any other gain. With 5 permanent Boost patches and a cap of 3 the
  round gets 3; the rest stays in the save, so a higher cap later applies more. Patches picked up
  during a round stack on top and are gone next round.
- **Checklist credit.** `Machine_GivePatch` does not count as a pickup - only
  `Machine_OnTouchItem` calls `Ply_IncrementItemCollectNum` (0x8022fbcc) - so on the city map the
  mod adds each stat's actual rise to `PlayerStats.item_collect[<that stat's patch>]`. That lets
  permanent patches count toward the "get 10 or more X Patches in one game" cells, which
  `CityTrial_CheckForNewUnlocks` (0x8004db74) reads that way, and keeps the count honest when
  `Rider_SpawnDropPatchSeq` (0x8019ce50) later decrements it for a patch knocked loose. A patch
  the cap swallowed is not credited. The array is written directly because the increment
  function would also feed the first-20-seconds total. Stadiums and Air Ride get no credit.
- **EnergyLink** takes each player's stat baseline on the first frame after the intro, the frame
  the permanent patches land, so they mint no energy.
