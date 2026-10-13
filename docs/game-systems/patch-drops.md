# Patch Drops

When a City Trial rider is forced to drop stats - by a heavy hit, or by anything else that calls the
producer - the engine throws the rider's patches back into the world as items. It is a
producer/consumer queue. `Rider_DropPatches` (0x8019d330) queues a drop by writing fields on
`RiderData`, and `Rider_TickDropPatches` (0x8019dc74) drains the queue one item per cooldown. A mod
that wants a rider to shed stats calls the producer; a mod that wants to retune the drops writes the
shared tuning block.

## Pipeline

**Producer.** `Rider_DropPatches(rd, stat_array, drop_mode)` does nothing outside the open City
Trial map: its first call, at 0x801918d0, is a `Gm_IsInCity` test. Otherwise it works out how many
items are owed from the mode (below) and adds any all-ups. Modes 0 and 2 add to `allups_dropped`,
capped so the queued total never exceeds `Ply_GetHydraCollection + Ply_GetDragoonCollection`. On an
empty queue it writes `patch_drop_count` and the mode and zeroes the cooldown and the progress
counter. While a queue is still draining, it adds to the count and replaces the mode, resetting
neither.

**Consumer.** `Rider_TickDropPatches` returns early while the cooldown runs, then dispatches the
all-up phase first if any all-ups are owed, otherwise the patch phase.

**All-up phase.** `Rider_TickDropAllUp` (0x8019d55c) throws a collected Legendary piece, not
`ITKIND_ALLUP`. The candidates are the set bits of the rider's Hydra and Dragoon piece masks, packed
into a stack list. The thrown kind is a random list entry plus 0x37, so Hydra's pieces are
0x37-0x39 and Dragoon's 0x3a-0x3c. The picked kind is stored in `drop_piece_kind` and read back
after the throw to clear that bit from the matching mask. With an empty list the first entry is
still -1 and the code adds 0x37 anyway, so the kind would be 54; vanilla never gets there, because
the quota is zero when no piece is held. Each throw decrements `allups_dropped`, advances
`patch_drop_progress` and drains one from `patch_drop_count`.

**Patch phase.** `Rider_TickDropPatch` (0x8019d9b4) defers to `Rider_SpawnDropPatchSeq` (0x8019ce50)
while `patch_drop_progress` is below `patch_drop_burst_threshold`, and switches to a burst path
past it. The sequential spawner chooses between two branches:

- When `patch_drop_count >= 9` and all nine stats are positive, it throws one All Up, takes 1 off
  every stat, and drains 9.
- Otherwise it picks one random positive stat, maps it to an `ItemKind` (table below), throws it,
  and drains 1.

**The burst path spawns nothing.** Past the threshold, `Rider_TickDropPatch` runs the same two
branches with the throw removed. It changes the stats through the stat helpers and refreshes the
rider's appearance and HUD, but nothing reaches `CityItem_Throw`. Stats past the threshold are
deleted with no item. Vanilla gets away with it because only modes 1 and 2 queue enough items to
reach the threshold. A mod that queues a large count gets silent stat loss instead of a fountain of
patches.

Both throwing paths call `Ply_DecrementItemCollectNum` (0x8022fb58) after a successful throw. It is
the partner of `Ply_IncrementItemCollectNum` (0x8022fbcc) and takes one off the player's per-kind
collection counter.

## Queue state on RiderData

| Field | Offset | Meaning |
|---|---|---|
| `patch_drop_cooldown` | `0x590` | Frames until the next throw; reset to `patch_drop_cooldown_init` after each |
| `patch_drop_progress` | `0x594` | Items thrown this session, by both phases. All-ups drain first and count toward the burst threshold |
| `patch_drop_count` | `0x598` | Items still owed; the consumer stops at 0 |
| `patch_drop_mode` | `0x59c` | The last mode queued |
| `drop_piece_kind` | `0x5a0` | The Legendary piece kind being thrown, written and read within one all-up throw |
| `allups_dropped` | `0x5a4` | All-ups still queued - a live countdown despite the name, and never reset by a fresh session |

Throws start from `hand_bone_pos` (`0x318`) and aim along `forward` (`0x324`), negated in mode 1 so
the drops fly behind the rider.

## Drop modes

| Mode | Items owed | All-ups | Direction |
|---|---|---|---|
| 0 | `patch_drop_mode0_count`; the stat array is ignored | One, if `HSD_Randi(patch_drop_allup_rng_max)` is below the remaining quota | Forward |
| 1 | Sum of positive stats x `patch_drop_mode1_factor` | None | Behind |
| 2 | Sum of positive stats x `patch_drop_mode2_factor` | The whole remaining quota | Forward |

**The damage trigger.** `Rider_DropPatchesOnDamage` (0x8019cdfc) calls
`Rider_DropPatches(rd, stats, 0)` when the hit's damage, truncated to an int, exceeds
`patch_drop_damage_min` (8.0). It has two callers. A ridden machine reaches it through
`Machine_GiveDamage` -> `Machine_DropPatchesOnDamage` (0x801e09ac) ->
`RiderGObj_DropPatchesOnDamage` (0x80192980), passing the machine's live stats (`MachineData.stats`,
`+0x94c`). The rider on foot reaches it from 0x801a06f0 with its own stats. Every
`Machine_GiveDamage` call is covered, including direct ones from mod code. Both callers pass a hand
position and a direction that the function ignores.

## Tuning

Every value lives in `RiderCommonParam`, the first member of the `rdDataCommon` root of
`RdCommon.dat`. `fn_rdLoadCommon` (0x80190418) loads it to the stay heap once and stores the root at
`r13 + 0x730` (`stc_rd_common_data`) and the block at `r13 + 0x734` (`stc_rider_param`). Every
function in the pipeline reads through `*stc_rider_param`, so the values are shared by all riders and
modes, and writing the loaded block retunes the drops.

| Field | Offset | Vanilla | Meaning |
|---|---|---|---|
| `patch_drop_damage_min` | `0x1b8` | 8.0 | Damage above which a hit drops patches |
| `patch_drop_mode0_count` | `0x1bc` | 1 | Items owed by a mode-0 drop |
| `patch_drop_throw_flag` | `0x1c0` | 50 | Passed as `CityItem_Throw`'s `flag`, stored at `item + 0x248` |
| `patch_drop_spawn_y_bias` | `0x1c4` | 2.0 | Lift added to the spawn Y |
| `patch_drop_mode2_factor` | `0x1c8` | 0.5 | Mode-2 multiplier on the positive-stat sum |
| `patch_drop_mode1_factor` | `0x1cc` | 0.25 | Mode-1 multiplier |
| `patch_drop_throw_spread` | `0x1d0` | 70.0 | Maximum fan half-angle in degrees, times a random `[0, 1)`; the side alternates with the parity of `patch_drop_count` |
| `patch_drop_mode0_params` | `0x1d4` | below | Mode 0's `PatchDropModeParams` |
| `patch_drop_mode1_params` | `0x1ec` | below | Mode 1's |
| `patch_drop_mode2_params` | `0x204` | below | Mode 2's |
| `patch_drop_cooldown_init` | `0x21c` | 4 | Frames between throws |
| `patch_drop_burst_threshold` | `0x220` | 10 | Progress at which the patch phase goes silent |
| `patch_drop_allup_rng_max` | `0x224` | 3 | Mode-0 all-up roll ceiling |

A `PatchDropModeParams` is three `lerp(lo, hi, rand)` ranges:

| Pair | Offset | Use | Mode 0 | Mode 1 | Mode 2 |
|---|---|---|---|---|---|
| A | `+0x00` | Throw speed | 0.81-1.134 | 0.567-0.81 | 0.324-0.405 |
| B | `+0x08` | Throw elevation, degrees | 65-80 | 65-80 | 87-20 |
| C | `+0x10` | Spawn offset along the fanned forward, added to the hand-bone position | 0.4-1.6 | 1.0-1.2 | 0.7-1.2 |

**Throw geometry.** The spawner fans `forward` by the spread angle, normalizes it, and offsets the
spawn point along it by pair C. `CityItem_Throw` (0x80253ce4) takes
`(item_kind, spawn_group, pos, throw_dir, flag, elev_angle, speed)`. It pitches the direction up by
the pair-B angle around a horizontal axis built from `Gr_GetDownVector`, scales it by the pair-A
speed, and writes the item's velocity. It asserts that the direction is neither zero nor degenerate
(`itlib.c`: `"*** Item throw front dir is Zero!"`).

## Stat to item table

`Rider_SpawnDropPatchSeq` maps the picked `PatchKind` to an `ItemKind` through the table at
0x804ae2c8. Index 9 is the All Up the nine-stat branch throws:

| Index | Stat | ItemKind |
|---|---|---|
| 0 | Weight | 17 `ITKIND_WEIGHT` |
| 1 | Boost | 3 `ITKIND_ACCEL` |
| 2 | Top Speed | 5 `ITKIND_TOPSPEED` |
| 3 | Turn | 11 `ITKIND_TURN` |
| 4 | Charge | 15 `ITKIND_CHARGE` |
| 5 | Glide | 13 `ITKIND_GLIDE` |
| 6 | Offense | 7 `ITKIND_OFFENSE` |
| 7 | Defense | 9 `ITKIND_DEFENSE` |
| 8 | HP | 19 `ITKIND_HP` |
| 9 | All Up | 20 `ITKIND_ALLUP` |

## Spawn source tag

`CityItem_Throw`'s `spawn_group` names what threw the item. Both patch-drop phases pass 3. The event
actors pass 4 (`Tac_ScatterItems`, 0x8021c8ec), 5 (`DynaBlade_ThrowItems`, 0x8021db44) and 6 (a
landed Meteor, 0x8021efd8). `Item_InitDesc` (0x802509a0) stores it in `ItemDesc.spawn_type`
(`+0x08`), and `CityItem_InitData` (0x8024eaf4) copies it to `ItemData.spawn_type` (`+0x20`) once at
creation. No gameplay logic branches on it: it is printed by two ground-collision assertions and is a
debug source tag.

## Hook points

- **Shedding stats.** Call `Rider_DropPatches` with the mode that fits. It is a no-op outside the
  open City Trial map, and a large count past the burst threshold deletes stats silently.
- **Adding a droppable piece.** The all-up phase is written against the two vanilla piece masks. A
  mod that wants another kind of piece knocked out of a rider has four places to supply it:
  - the quota, at the `bl Ply_GetDragoonCollection` at 0x8019d4bc;
  - the chosen kind, stored at 0x8019d868;
  - the collection counter, at the `bl Ply_DecrementItemCollectNum` at 0x8019d8d4. That function
    does not bound the kind;
  - the mask clear, at the reload of `drop_piece_kind` at 0x8019d8f8. The cooldown reset at
    0x8019d950 is past both vanilla mask branches.
