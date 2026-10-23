# Spawn Rate

The item spawn rate is a slot option and a progressive item: a seed can start City Trial and
Top Ride with fewer items than vanilla, or exactly vanilla, and each Spawn Rate Up (item 12)
adds 10%. One scale drives three hooks - City Trial's spawn timer, City Trial's cap on items in
play, and Top Ride's per-frame spawn chance.

## The scale

`SpawnRate_GetScale()` is `spawn_rate_min / 100 + spawn_rate_level * 0.1`, capped at 3.0.
`spawn_rate_min` is the slot option (10-100 in the apworld) and
`APSave.spawn_rate_level` counts Spawn Rate Up items received (saturating at 255). Until the
save takes its slot options the scale is 1.0; after that `spawn_rate_min` is floored at 10,
since the scale divides the City Trial timer.

The apworld's `spawn_rate_max` (100-300) is not sent to the game; it only decides how many Spawn
Rate Up items the pool holds, `(max - min) / 10`, so collecting them all lands on `max`. The 3.0
cap matches the top of that range, and keeps Top Ride's spawn chance short of firing every frame.

A Spawn Rate Up applies in any scene and announces "Spawn rate increased (X%)" with the new
absolute rate.

## City Trial

`CityItemSpawn_UpdateAndCheckToSpawn` (0x800ea6e0) counts a timer down and spawns when it
reaches 0, re-rolling it from the current `ItemFallDesc`'s range on two paths. Hooks at both
stores (0x800ea8b0, 0x800ea990) replace the new timer with `timer / scale`, at least 4 frames.
Below 1.0 the division lengthens the timer, which is how a low `spawn_rate_min` thins the field.

Spawning faster alone would only churn items, because the field also caps how many items are in
play (`ItemFallDesc.item_max`, against the live count `grBoxGeneInfo.cur_num_items`). A
conditional hook on that comparison (0x800eaa8c) scales the cap by the same factor - only ever
up, since a slower timer already thins the field and a scaled-down cap could round to 0. It
covers both the stage's descriptor and an event's override, which the same register points at,
and restores `r5` (`grBoxGeneInfo`), which both exits read.

## Top Ride

`TopRideItem_SpawnTimed` (0x8034b8c8) spawns when `HSD_Randf()` falls below a per-frame
probability. The `bl HSD_Randf` at 0x8034bae0 is replaced with `HSD_Randf() / scale`, which
scales the probability by the same factor.

## Where it does nothing

Air Ride items are placed by the stage, not spawned on a timer, and enemies spawn by proximity,
so the rate has no effect there; the apworld tags Spawn Rate Up to City Trial and Top Ride.
