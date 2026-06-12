# Patch Drop System

## Overview

The patch drop system spawns patch items behind/in front of a rider when something forces them to "drop their stats" (typically damage, boost overuse, or our trap item). It's a producer/consumer queue: `Rider_DropPatches` enqueues an event by writing fields on `RiderData`, then `Rider_TickDropPatches` runs per-frame and drains the queue, spawning one item at a time on a cooldown.

This doc captures what we learned reverse-engineering the pipeline; the in-game implementation is unmodified by the mod (we only call into it from `Patch_DropTrap` in `patch_item.c`).

## Function Map

| Address | Name | Role |
|---------|------|------|
| 0x8019d330 | `Rider_DropPatches` | Producer. Enqueues a drop event by writing `patch_drop_count`, `patch_drop_mode`, and (for modes 0/2) incrementing `allups_dropped`. Resets `patch_drop_cooldown` and `patch_drop_progress` to 0 on a fresh session. |
| 0x8019dc74 | `Rider_TickDropPatches` | Per-frame consumer. Skips while cooldown is active. Dispatches to all-up sub-handler if any all-ups are owed, else to the patch sub-handler. |
| 0x8019d55c | `Rider_TickDropAllUp` | All-up sub-handler. Negates `forward` if `patch_drop_mode == 1`, looks up the matching `Game3dData.patch_drop_modeN_params` block, calls `CityItem_Throw` directly — throwing a collected **Legendary-machine piece** (item kinds 0x37–0x3c; Hydra 0x37–0x39, Dragoon 0x3a–0x3c), **not** `ITKIND_ALLUP`. The thrown piece is also cleared from the rider's Hydra/Dragoon collection mask. **Decrements `allups_dropped` by 1 and increments `patch_drop_progress` by 1 per successful spawn**, then returns the new `patch_drop_count` (count − 1). (In practice mode is never 1 here — mode 1 never queues all-ups — so the negation branch is dead.) |
| 0x8019d9b4 | `Rider_TickDropPatch` | Patch sub-handler. Two paths: while `patch_drop_progress < Game3dData.patch_drop_burst_threshold`, calls `Rider_SpawnDropPatchSeq`; once the threshold is crossed, switches to a **silent** burst path (random single stat, or all-stats dump if count ≥ 9 and all 9 stats are positive). The burst path mutates stats but does **not** call `CityItem_Throw` — see "Burst path is silent" below. |
| 0x8019ce50 | `Rider_SpawnDropPatchSeq` | Inner sequential spawner. Two branches, gated by the consolidation predicate at 0x8019d274 (`patch_drop_count >= 9 && all 9 stats > 0`): if it holds, throws a single **All Up** item (table index 9 = `ITKIND_ALLUP`), dumps all 9 stats via 0x80191294 (`delta = -1`), and drains `patch_drop_count` by 9; otherwise picks one random positive stat, looks up its ItemKind in the table at 0x804AE2C8, throws it, and drains count by 1. Both branches call `CityItem_Throw` and increment `patch_drop_progress` by 1. |
| 0x8022fb58 | `Ply_DecrementItemCollectNum` | Drop-side accounting partner of `Ply_IncrementItemCollectNum` (0x8022fbcc). Decrements one slot of the per-itemkind collection counter array on `PlyData` and (for itemkind > 2 and a guard field) the aggregate counter. Called by both drop sub-handlers after a successful `CityItem_Throw`. |
| 0x80253ce4 | `CityItem_Throw` | Spawns one item with a randomized throw velocity. Builds a spawn descriptor via `CityItem_InitDesc` (0x802509a0), maps the `ItemKind` via `CityItem_GetUnkKindFromItemKind` (0x8024ea54), hands the descriptor to `CityItem_Create` (0x8024eef4), then asserts the throw direction is non-zero/non-pathological (`__assert` in `itlib.c`: `"*** Item throw front dir is Zero!"` / `"*** Item throw front dir is Irregul(%f, %f, %f)!"`). Rotates the rider's `forward` (via `VEC_CrossNormalizeSnap` + `RotateVecAroundAxis`) into the item's velocity and writes the caller-supplied flag to item+0x248. The second int arg is the spawn-group tag stored on the descriptor: the patch-drop pipeline passes **3** (verified `li r4,3` before the call in both drop sub-handlers); the yakumono-break helpers (`zz_8021c8ec_`, `zz_8021db44_`, `zz_8021efd8_`) pass 4/5/6 respectively. |

Helpers still unnamed (broader use across the codebase, not just drops):

- **0x8019d274** — Consolidation predicate. Returns 1 iff `patch_drop_count >= 9` **and** all 9 stats are positive. Gates the "consolidate into one All Up" branch in both `Rider_SpawnDropPatchSeq` (sequential, throws) and the inlined copy in `Rider_TickDropPatch` (burst, silent). Still `zz_`-labelled in the map.
- **0x80194d80** — `Stats_ClampValues?` (Ghidra label). Adds a delta to one entry of `stat_array` and clamps to `[Patch_GetMinValue(), Patch_GetMaxValue()]`. Pure stat math, no spawn. Used by the single-stat paths with `delta = -1`; 4 other callers in the rider system.
- **0x80191294** — Calls a sibling clamp-all variant (`Stats_ClampValues2?` at 0x80194e60) on every stat plus `Ply_SetAllUpCollected(ply, allups + delta)` (reading the current count with `Ply_GetAllUpCollected` at 0x8022d024). The "dump all 9 stats" step with `delta = -1`. Pure stat/all-up math, no spawn. 2 callers: the burst all-stats path (silent) and the sequential All-Up consolidation in `Rider_SpawnDropPatchSeq` (which does throw an item first).

## RiderData Fields

| Offset | Name | Meaning |
|--------|------|---------|
| 0x318 | `hand_bone_pos` (Vec3) | Position passed as the spawn origin. |
| 0x324 | `forward` (Vec3) | Velocity passed to the spawn; **negated** by both sub-handlers when `patch_drop_mode == 1` (drops fly behind), used as-is for modes 0/2 (drops fly forward). |
| 0x590 | `patch_drop_cooldown` | Per-spawn cooldown; ticked down by `Rider_TickDropPatches`. Reset to `Game3dData.patch_drop_cooldown_init` after each spawn. Reset to 0 on a fresh `Rider_DropPatches` call. |
| 0x594 | `patch_drop_progress` | Drops dispatched this session. Compared against `Game3dData.patch_drop_burst_threshold` to switch the patch sub-handler from sequential to burst. Reset to 0 on a fresh session. Incremented by 1 on **every** successful spawn — by `Rider_SpawnDropPatchSeq` (2 sites: single-stat and All-Up-consolidation) **and** by `Rider_TickDropAllUp`. So it counts spawns across both phases; because the all-up phase drains first, those all-up spawns already advance `progress` before the patch phase runs. |
| 0x598 | `patch_drop_count` | Queue length: number of items still to spawn. Drained one per spawn (the consolidation branch drains 9 at once); consumer returns early when this is 0. A fresh `Rider_DropPatches` call with `count == 0` seeds it; calling again while `count != 0` **adds** to the existing queue (and updates `patch_drop_mode`) without resetting cooldown/progress. |
| 0x59c | `patch_drop_mode` | 0/1/2; written by `Rider_DropPatches`, read by sub-handlers. |
| 0x5a0 | (scratch) | Transient: the Legendary-piece item kind (0x37–0x3c) `Rider_TickDropAllUp` selected for the current spawn — written then immediately consumed within the same call. |
| 0x5a4 | `allups_dropped` | All-ups still queued to drop (a live countdown, despite the name). The producer (`Rider_DropPatches`, modes 0/2) **adds** to it, capped so the total never exceeds `Ply_GetHydraCollection + Ply_GetDragoonCollection` — the rider's quota of all-ups, granted by collecting Legendary Air Ride Machine pieces. The all-up consumer (`Rider_TickDropAllUp`) **decrements** it by 1 per spawn. While non-zero, `Rider_TickDropPatches` routes to the all-up sub-handler; only once it hits 0 does the patch sub-handler run. Not reset on a fresh `Rider_DropPatches` session. |

## Drop Mode Semantics

`drop_mode` is the third argument to `Rider_DropPatches` (matching the `rider.h` declaration). The verified behavior:

| Mode | Drop Count | All-ups | Direction |
|------|------------|---------|-----------|
| 0 | Small fixed count from `patch_drop_mode0_count` | Probabilistic single all-up (RNG roll vs. remaining quota using `patch_drop_allup_rng_max`) | Forward |
| 1 | Sum of positive stats × `patch_drop_mode1_factor` | None — sub-handler skips the all-up consolidation block entirely | Behind (forward vector negated) |
| 2 | Sum of positive stats × `patch_drop_mode2_factor` | All remaining all-ups in the quota (deterministic) | Forward |

Mode-specific tuning lives in three `PatchDropModeParams` blocks (24 bytes / 6 floats) in `Game3dData`:

- `Game3dData.patch_drop_mode0_params` (0x1d4)
- `Game3dData.patch_drop_mode1_params` (0x1ec)
- `Game3dData.patch_drop_mode2_params` (0x204)

Each block is three (lo, hi) float pairs:

| Field | Offset | Use in sub-handlers |
|-------|--------|---------------------|
| `lo_a` / `hi_a` | +0x00 / +0x04 | `lerp(lo, hi, rand)` → **second** float arg to `CityItem_Throw` (plain scalar, no further scaling) |
| `lo_b` / `hi_b` | +0x08 / +0x0c | `lerp(lo, hi, rand)` × `deg2rad` (≈0.01745) → **first** float arg to `CityItem_Throw` (an angle supplied in degrees) |
| `lo_c` / `hi_c` | +0x10 / +0x14 | `lerp(lo, hi, rand)` → spawn-velocity multiplier (scales the rotated, normalized throw direction) |

Note the cross-mapping (verified in both `Rider_TickDropAllUp` and `Rider_SpawnDropPatchSeq`): pair **A** feeds the *second* `CityItem_Throw` float arg and pair **B** the *first*. Pair B's lerp result is multiplied by a degrees-to-radians constant (`≈0.01745`) immediately before the call, so that argument is an **angle in degrees**; pair A's is passed through unscaled. Both flow into `CityItem_InitDesc` and end up on the spawn descriptor — their exact role inside the throw isn't fully traced. The third pair is unambiguously a velocity scale.

Other patch-drop tuning fields in `Game3dData` (all named in `game.h`):

| Field | Offset | Meaning |
|-------|--------|---------|
| `patch_drop_mode0_count` | 0x1bc | Queue length for a mode-0 drop. The producer uses this **unconditionally** for mode 0 and ignores the `stat_array` argument (only modes 1/2 derive the count from summed stats). `Rider_DropPatches` takes no count argument. |
| `patch_drop_spawn_arg7` | 0x1c0 | Passed verbatim as `CityItem_Throw`'s last int arg, which gets stored at `item+0x248` (item flag word). |
| `patch_drop_spawn_y_bias` | 0x1c4 | Float added to spawn position Y before calling `CityItem_Throw` (lifts drops off the hand bone). |
| `patch_drop_mode2_factor` | 0x1c8 | Float; multiplied with the sum of positive stats to size mode-2 drops. |
| `patch_drop_mode1_factor` | 0x1cc | Same role for mode 1. |
| `patch_drop_throw_spread` | 0x1d0 | Max throw-spread half-angle (degrees, `f32`). Read by `Rider_TickDropAllUp` and `Rider_SpawnDropPatchSeq`: multiplied by `deg2rad` (≈0.01745) and a random `[0,1)` factor, then — with the sign flipped on odd `patch_drop_count` values — used to rotate the throw direction so successive drops fan out left/right. (Previously documented as the unused `x1d0`.) |
| `patch_drop_cooldown_init` | 0x21c | Frame count written into `RiderData.patch_drop_cooldown` after each successful spawn. |
| `patch_drop_burst_threshold` | 0x220 | Once `patch_drop_progress` reaches this, the patch sub-handler switches from sequential to silent-burst. |
| `patch_drop_allup_rng_max` | 0x224 | Mode-0 only: ceiling for the all-up RNG roll (`HSD_Randi(this) >= remaining_quota` → no all-up this drop). |

## Stat → ItemKind Table

`Rider_SpawnDropPatchSeq` translates the picked stat slot (`PatchKind` index 0..8) into an `ItemKind` via the read-only table at **0x804AE2C8**. Indices 0..8 are the nine stat patches; the same array also has a **10th entry at index 9** (`0x804AE2EC`) that the consolidation branch uses for the All-Up throw:

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
| 9 (All Up — consolidation only) | 20 = `ITKIND_ALLUP` |

Indices 0..8 are the same mapping as our local `stc_patch_itkinds[]` in `patch_item.c`. The duplication is harmless but worth knowing — if we ever need this lookup outside `patch_item.c`, prefer a hoshi-side declaration over re-duplicating. (Words at index 10/11 — `0x1e`/`0x1f` — are *not* part of this lookup; the drop code only reads indices 0..9.)

## Mod Use

`Patch_DropTrap` in `patch_item.c` calls `Rider_DropPatches(rd, rd->stats.values, drop_mode)` for each human rider (`rd->stats.values` is the rider's `float values[9]` stat array), picking `drop_mode` randomly per player via `HSD_Randi(3)` so the trap varies its visual signature.

The caller — `APItems_HandleItem` in `ap_item_handler.c` — only reaches `Patch_DropTrap` when `Gm_IsInCity()` is true, i.e. the **City Trial open-city phase**. Both Free Run and the stadium phase are blocked further up by the shared scene gate (`ap_item_handler.c`: `Gm_GetCityMode() == CITYMODE_FREERUN || Gm_IsStadiumMode()` → defer), because neither loads the item data tables; the spawn path inside `Rider_DropPatches` (`CityItem_Throw` → `CityItem_Create`, ultimately `Item_GetItDataPtr`) would crash there.

## Burst Path Is Silent

When `patch_drop_progress >= patch_drop_burst_threshold`, the patch sub-handler switches paths and **stops spawning items entirely** (no `CityItem_Throw` call). Tracing the all-stats path (`Rider_TickDropPatch` → 0x80191294) and the random-single-stat path (→ `Stats_ClampValues?` at 0x80194d80):

- Both helpers only mutate `stat_array` (clamping against `Patch_GetMinValue`/`Patch_GetMaxValue`) and, for the all-stats variant, the all-up collected counter (`Ply_GetAllUpCollected`/`Ply_SetAllUpCollected`).
- The only post-mutation calls `Rider_TickDropPatch` makes are appearance/HUD-refresh helpers — 0x80191334 (which itself calls 0x80193e78 and 0x80191374) and 0x80193718. None of them reach `CityItem_Throw`. (The 0x8019xxxx helper names are inferred from usage; they are still `zz_`-labelled in the map.)
- `Rider_SpawnDropPatchSeq` is the only path that calls `CityItem_Throw`; it runs while progress is below the threshold.

The burst path mirrors the sequential spawner's two branches — "all 9 positive → consolidate" vs "one random stat" — but strips the spawn out of both. Below the threshold, the all-9-positive case throws a single visible **All Up** item (and removes 9 stats); above it, the identical stat removal happens with no item at all.

So once you cross the burst threshold, stats are deleted with no flying patch visual. This is fine for vanilla because `Rider_DropPatches` only generates large drop counts in mode 1 / mode 2 (proportional to summed stats), and the threshold is tuned so a normal-stats rider doesn't hit it. But it's worth knowing for `Patch_DropTrap`: if we ever pass a very large `patch_drop_count`, we'll silently zero out stats past the threshold rather than fountain patches.

## Open Questions

- The role inside `CityItem_Throw` of its two `f32` args — the **first** sourced from `lo_b/hi_b` (a degrees angle, ×`deg2rad`), the **second** from `lo_a/hi_a` (a plain scalar). They flow through `CityItem_InitDesc` onto the spawn descriptor (offsets +0x30/+0x34 observed but unconfirmed) — the angle is plausibly an extra throw rotation and the scalar a lifetime/duration — but how they are consumed still needs confirmation.
- The "spawn group" tag passed as `CityItem_Throw`'s second int arg (3 = rider drop, 4/5/6 = various yakumono breaks). Stored on the spawn descriptor; the consumer hasn't been traced.
