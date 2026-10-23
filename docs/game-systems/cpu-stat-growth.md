# CPU Stat Growth

In City Trial every CPU rider is handed a pool of free machine stats at load, which drips onto
its machine over the round whether or not it ever picks up a patch. It is a flat head start
scaled by CPU level, not a rubber band on the leader. A mode that wants CPUs to live off real
pickups, or a mod that changes how strong CPUs end a round, works here.

## Seeding

`SceneLoad_3D` (0x8001442c) seeds the pool once, in a loop over the five slots that runs when
the scene's major is City Trial (6) and byte 5 of `Gm_GetCityData()` is 0. For each slot it
writes `GameData.city.cpu_stat_budget[slot]` (`+0x46c`, `float[5]`): for a CPU, the
`ct_cpu_stat_seed` entry for its CPU level; for a human, 0. The loop's slot increment (`addi
r25,r25,1` at 0x80014ad4) is reached once per slot right after the store, with `r25` still the
slot index - the natural place to rescale or clear a slot's pool.

## Spending

`CityTrial_GrowCpuStats` (0x80015a00), called from `Game_Think` every frame, acts every
`ct_cpu_stat_interval` frames of round time (`Gm_GetRoundFrames`). For each CPU slot with a
machine, budget left and a valid level (`Ply_GetCpuLevel`, 0x8022d7b0, -1 for non-CPUs) it reads
the slot's nine-entry `stat_aux` block (`Ply_GetStatAux`, 0x8022d0cc), takes
`ct_cpu_stat_rate[level] * HSD_Randf()` out of the budget, sprinkles it over the block in
sub-1.0 increments on randomly chosen stats (`HSD_Randi(9)`), and writes it back with
`Ply_SetStatAux` (0x8022d128).

Because both the stat and the increment are random, a CPU's nine stats (weight, boost, top
speed, turn, charge, glide, offense, defense, HP) drift unevenly, the way a human's do from
random patches. A higher level starts with a bigger pool and drains it faster, so it ramps up
over the first minute or so and plateaus once the pool is empty.

The growth lands in `PlayerData.stat_aux` (+0x68), not the `stats` block at +0x44.
`Ply_SetStatAux` pushes it into the ridden machine's added-patch block (`MachineData+0x9e8`)
through `MachineGObj_SetMasterStats` (0x801c8258), clamped per stat, and
`Machine_AdjustAttributes` recombines the machine's master stats (`+0x94c`) - the same pipeline
patch pickups feed, so downstream the growth is indistinguishable from collected patches.

## Tables

The three tunables are members of `gmGameParams` (`gmDataAll->game_params`), indexed by CPU
level 0-8:

| Level | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| `ct_cpu_stat_seed` (+0x154) | 3 | 5 | 10 | 15 | 20 | 25 | 30 | 35 | 42 |
| `ct_cpu_stat_rate` (+0x178) | 1.0 | 1.0 | 1.0 | 1.0 | 1.2 | 1.5 | 2.0 | 2.5 | 3.0 |

`ct_cpu_stat_interval` (+0x19c) is 180 frames, about three seconds.

## Related handicaps

Two other handicaps feed CPUs and are separate mechanisms: the King Dedede fight's catch-up
(`vsDedede_CPU_Cheating`, 0x80040b80), which compares slot 4 against the field's average HP, and
Air Ride's Speed Help (`AirRide_ApplySpeedHelp`, 0x800155d4). Speed Help runs in races with the Air
Ride Speed Help rule on; it rewrites every player's boost and top-speed `stat_aux` from how far
behind the leader they are, and adds per-level CPU bonuses on top.

## Notes for modders

Scaling or zeroing a CPU's growth means scaling its seeded budget at the slot increment above;
the per-tick drainer hands out whatever is left. No-oping `CityTrial_GrowCpuStats` removes the
growth outright, and editing the two `gmGameParams` tables reshapes it for every CPU.
