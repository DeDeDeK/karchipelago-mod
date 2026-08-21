# Patch Drop System

The patch drop system ejects a rider's collected stat patches back into the world as physical
items when something forces them to "drop their stats" - damage, boost overuse, or our trap
item. It is a producer/consumer queue: `Rider_DropPatches` (0x8019d330) enqueues an event by
writing fields on `RiderData`, and `Rider_TickDropPatches` (0x8019dc74) drains that queue one
item per frame-cooldown. The engine side is unmodified by the mod; `Patch_DropTrap` in
`mods/archipelago/src/patch_item.c` only calls into it.

## Pipeline

`Rider_DropPatches` is the producer. It writes `patch_drop_count` (how many items are owed),
`patch_drop_mode`, and for modes 0 and 2 adds to `allups_dropped`. On a fresh session (queue
was empty) it zeroes `patch_drop_cooldown` and `patch_drop_progress`; calling it again while a
queue is still draining *adds* to the count and updates the mode without resetting either.

`Rider_TickDropPatches` is the per-frame consumer. It returns early while the cooldown is
still ticking, then dispatches: all-ups first if any are owed, otherwise patches.

`Rider_TickDropAllUp` (0x8019d55c) handles the all-up phase. It picks the matching
`Game3dData.patch_drop_modeN_params` block and calls `CityItem_Throw` directly - but what it
throws is a collected **Legendary-machine piece** (item kinds 0x37-0x3c; Hydra 0x37-0x39,
Dragoon 0x3a-0x3c), *not* `ITKIND_ALLUP`. The thrown piece is also cleared from the rider's
Hydra/Dragoon collection mask. Each successful spawn decrements `allups_dropped`, increments
`patch_drop_progress`, and drains one from `patch_drop_count`. It negates `forward` when
`patch_drop_mode == 1`, but that branch is dead in practice: mode 1 never queues all-ups.

`Rider_TickDropPatch` (0x8019d9b4) handles the patch phase and has two paths. While
`patch_drop_progress < Game3dData.patch_drop_burst_threshold` it defers to
`Rider_SpawnDropPatchSeq`; once the threshold is crossed it switches to a silent burst path
that mutates stats without spawning anything (see below).

`Rider_SpawnDropPatchSeq` (0x8019ce50) is the only function in the pipeline that actually
throws patches. A predicate at 0x8019d274 - `patch_drop_count >= 9` **and** all nine stats
positive - chooses between its two branches. When it holds, it throws one **All Up** item
(table index 9), dumps all nine stats through 0x80191294 with `delta = -1`, and drains
`patch_drop_count` by 9. Otherwise it picks one random positive stat, maps it to an `ItemKind`
through the table at 0x804AE2C8, throws it, and drains one. Both branches call `CityItem_Throw`
and bump `patch_drop_progress`.

Both sub-handlers call `Ply_DecrementItemCollectNum` (0x8022fb58) after a successful throw -
the drop-side partner of `Ply_IncrementItemCollectNum` (0x8022fbcc). It decrements one slot of
the per-itemkind collection counter array on `PlayerData`, plus the aggregate counter for
itemkind > 2.

`CityItem_Throw` (0x80253ce4) takes `(item_kind, spawn_group, pos, throw_dir, flag,
elev_angle, speed)`. It builds a spawn descriptor via `CityItem_InitDesc` (0x802509a0), maps
the `ItemKind` through `CityItem_GetUnkKindFromItemKind` (0x8024ea54), hands the descriptor to
`CityItem_Create` (0x8024eef4), and asserts the throw direction is non-zero and non-pathological
(`itlib.c`: `"*** Item throw front dir is Zero!"` / `"*** Item throw front dir is
Irregul(%f, %f, %f)!"`). The caller's `flag` lands at `(item+0x2c)+0x248`.

The stat math itself is separate from spawning. 0x80191294 calls `Stat_AddClampedAll`
(0x80194e60) on all nine stats and adjusts the all-up counter via `Ply_GetAllUpCollected`
(0x8022d024) / `Ply_SetAllUpCollected`. Single-stat paths use `Stat_AddClamped` (0x80194d80),
which clamps one `stat_array` entry to `[Patch_GetMinValue(), Patch_GetMaxValue()]`. Neither
spawns anything.

## Queue State on RiderData

The queue lives entirely in `RiderData` fields (declared in `externals/hoshi/include/rider.h`):

- `patch_drop_cooldown` (0x590) - ticked down by the consumer, reset to
  `Game3dData.patch_drop_cooldown_init` after each spawn.
- `patch_drop_progress` (0x594) - drops dispatched this session, incremented on **every**
  successful spawn by both sub-handlers. Because the all-up phase drains first, all-up spawns
  already advance progress before any patch is thrown - which is what pushes the patch phase
  toward the burst threshold sooner than the patch count alone suggests.
- `patch_drop_count` (0x598) - items still owed; the consumer returns when it hits 0.
- `patch_drop_mode` (0x59c) - 0/1/2, read by both sub-handlers.
- `allups_dropped` (0x5a4) - all-ups still queued (a live countdown, despite the name). The
  producer adds to it, capped so the total never exceeds
  `Ply_GetHydraCollection + Ply_GetDragoonCollection` - the rider's quota, earned by collecting
  Legendary Air Ride Machine pieces. It is **not** reset on a fresh session.
- 0x5a0 is scratch: the Legendary-piece item kind `Rider_TickDropAllUp` picked for the current
  spawn, written and consumed within one call.

Spawn geometry reads `hand_bone_pos` (0x318) as the origin and `forward` (0x324) as the throw
direction, with `forward` negated for mode 1 so drops fly behind the rider.

## Drop Modes

`drop_mode` is the third argument to `Rider_DropPatches`.

| Mode | Drop count | All-ups | Direction |
|------|------------|---------|-----------|
| 0 | Fixed `patch_drop_mode0_count` (the `stat_array` argument is ignored) | Probabilistic single all-up: RNG roll vs. remaining quota using `patch_drop_allup_rng_max` | Forward |
| 1 | Sum of positive stats x `patch_drop_mode1_factor` | None - the all-up block is skipped entirely | Behind |
| 2 | Sum of positive stats x `patch_drop_mode2_factor` | All remaining all-ups in the quota, deterministically | Forward |

## Tuning in Game3dData

Each mode has a `PatchDropModeParams` block (24 bytes, three float pairs):
`patch_drop_mode0_params` (0x1d4), `patch_drop_mode1_params` (0x1ec),
`patch_drop_mode2_params` (0x204). The pairs are `lerp(lo, hi, rand)` ranges:

| Pair | Offset | Use |
|------|--------|-----|
| A | +0x00 / +0x04 | Throw **speed** - the magnitude the pitched direction is scaled by. |
| B | +0x08 / +0x0c | Throw **elevation angle**, degrees, multiplied by `deg2rad` before use. |
| C | +0x10 / +0x14 | Forward **spawn offset** - scales the normalized fanned forward and adds it to the hand-bone position. |

The scalar fields (all named in `game.h`):

| Field | Offset | Meaning |
|-------|--------|---------|
| `patch_drop_mode0_count` | 0x1bc | Queue length for a mode-0 drop. `Rider_DropPatches` takes no count argument. |
| `patch_drop_spawn_arg7` | 0x1c0 | Passed verbatim as `CityItem_Throw`'s `flag`, stored at `item+0x248`. |
| `patch_drop_spawn_y_bias` | 0x1c4 | Added to spawn Y, lifting drops off the hand bone. |
| `patch_drop_mode2_factor` | 0x1c8 | Multiplier on the sum of positive stats for mode 2. |
| `patch_drop_mode1_factor` | 0x1cc | Same for mode 1. |
| `patch_drop_throw_spread` | 0x1d0 | Max throw-spread half-angle in degrees. Scaled by `deg2rad` and a random `[0,1)` factor, with the sign flipped on odd `patch_drop_count` values, so successive drops fan out left/right. |
| `patch_drop_cooldown_init` | 0x21c | Frames written into `patch_drop_cooldown` after each spawn. |
| `patch_drop_burst_threshold` | 0x220 | Progress at which the patch sub-handler goes silent-burst. |
| `patch_drop_allup_rng_max` | 0x224 | Mode-0 only: ceiling for the all-up roll (`HSD_Randi(this) >= remaining_quota` means no all-up). |

## Throw Geometry

`Rider_SpawnDropPatchSeq` fans the rider's `forward` left/right by `patch_drop_throw_spread`
to get the throw direction. Pair **C** then scales the *normalized* fanned forward and adds it
to the hand-bone position: that is the spawn origin, not a velocity.

`CityItem_Throw` does the rest. It builds a horizontal axis from `Gm_GetDownVector` +
`VEC_CrossNormalizeSnap(down, throw_dir)`, pitches the direction around that axis by the pair-**B**
elevation angle via `RotateVecAroundAxis`, scales each component by the pair-**A** speed, and
writes the result to the item's velocity at `(item+0x2c)+0xc4/0xc8/0xcc`. Neither float argument
reaches the spawn descriptor.

## Stat to ItemKind Table

`Rider_SpawnDropPatchSeq` maps the picked stat slot (`PatchKind` index 0..8) to an `ItemKind`
through the read-only table at **0x804AE2C8**. It has a 10th entry at index 9 (`0x804AE2EC`)
used by the consolidation branch:

| Index | ItemKind |
|-------|----------|
| 0 (Weight)   | 17 = `ITKIND_WEIGHT` |
| 1 (Accel)    | 3  = `ITKIND_ACCEL` |
| 2 (TopSpeed) | 5  = `ITKIND_TOPSPEED` |
| 3 (Turn)     | 11 = `ITKIND_TURN` |
| 4 (Charge)   | 15 = `ITKIND_CHARGE` |
| 5 (Glide)    | 13 = `ITKIND_GLIDE` |
| 6 (Offense)  | 7  = `ITKIND_OFFENSE` |
| 7 (Defense)  | 9  = `ITKIND_DEFENSE` |
| 8 (HP)       | 19 = `ITKIND_HP` |
| 9 (All Up - consolidation only) | 20 = `ITKIND_ALLUP` |

Indices 0..8 are the same mapping as `stc_patch_itkinds[]` in `patch_item.c`. Words at index
10/11 (`0x1e`/`0x1f`) are not part of this lookup - the drop code only reads 0..9.

## The Burst Path Spawns Nothing

Once `patch_drop_progress >= patch_drop_burst_threshold`, `Rider_TickDropPatch` mirrors the
sequential spawner's two branches - "all nine positive, consolidate" vs "one random stat" - but
strips the spawn out of both. It calls 0x80191294 or `Stat_AddClamped` to mutate stats, then
only appearance/HUD-refresh helpers (0x80191334, which calls 0x80193e78 and 0x80191374, and
0x80193718). None of those reach `CityItem_Throw`.

So past the threshold stats are deleted with no flying patch at all. Vanilla gets away with it
because only modes 1 and 2 generate counts large enough to cross the threshold, and the
threshold is tuned so a normal-stats rider does not. It matters for `Patch_DropTrap`: a very
large `patch_drop_count` would silently zero stats instead of fountaining patches.

## Spawn-Source Tag

`CityItem_Throw`'s `spawn_group` argument identifies what spawned the item. The patch-drop
pipeline passes **3** from both sub-handlers; the yakumono-break helpers (`zz_8021c8ec_`,
`zz_8021db44_`, `zz_8021efd8_`) pass 4/5/6. `CityItem_InitDesc` stores it at desc+8 and
`CityItem_InitData` (0x8024eaf4) copies it to `(item+0x2c)+0x20`, where it is written once at
creation and never overwritten.

No gameplay logic branches on it. Inside the city-item subsystem it is read only by two
ground-collision assertions - in `CityItem_LifetimeThink`
(`"Item pos is ground center(%d:%d,%d)"`) and the under-ground check
(`"*** Error : Why? Item under ground not found!(%d,%d:%d,%d)"`) - which print it next to the
item id (`+0x1c`) and spawn-location indices (`+0x34`/`+0x38`). It is a debug source-attribution
tag, not a control input.

## Mod Use

`Patch_DropTrap` calls `Rider_DropPatches(rd, rd->stats.values, drop_mode)` for each human
rider, picking `drop_mode` with `HSD_Randi(3)` per player so the trap varies its visual
signature.

Its caller, `APItems_HandleItem` in `ap_item_handler.c`, only reaches it when `Gm_IsInCity()`
is true - the City Trial open-city phase. Free Run and the stadium phase are already blocked
further up by the shared scene gate (`Gm_GetCityMode() == CITYMODE_FREERUN ||
CityTrial_IsInStadium()` -> defer), because neither loads the item data tables and the spawn
path (`CityItem_Throw` -> `CityItem_Create` -> `Item_GetItDataPtr`) would crash there.
