# Spawn Rate

## Overview

`AP_ITEM_SPAWN_RATE_UP` is a progressive AP filler that scales the rate at which items spawn during gameplay. The effective scale factor is `min_pct/100 + level * 0.1`, capped at **5×**, where `min_pct` is the AP slot option `spawn_rate_min` (default 100 = vanilla) and `level` is the count of items received. The scale drives three independent hooks — two in City Trial (timer + cap) and one in Top Ride (per-frame probability).

The AP world picks a `(min, max)` pair on the generation side and ships `(max - min) / 10` Spawn Rate Up items, so collecting them all bumps the player from `min` to `max`. The mod only knows about `min` and the per-item +10% step; `max` is purely a generation-side concept.

Air Ride is intentionally not covered: AR enemy spawning is proximity-driven per spawn slot, not timer/probability-driven, and AR's items are stage-placed rather than dynamically spawned. There is no analogous "rate" knob.

**Files:** `spawn_rate.c` / `spawn_rate.h`, `main.h` (save + item enum + slot option), `ap_item_handler.c` (dispatch), `archipelago_debug/src/debug_menu.c` (give action)

## Slot Options

```c
u32 spawn_rate_min;    // Percent: 100 = vanilla, 200 = 2.0x, capped at 500.
                       // 0 (no options received yet) treated as 100.
```

Lives in `APSlotOptions` and is set once when the AP client writes options at handshake.

## Save Data

```c
u8 spawn_rate_level;    // Number of Spawn Rate Up items received
```

One byte in `APSave`. Saturates at 255 in `SpawnRate_Increment` to prevent wraparound. Zeroed by `OnSaveInit` via `memset`.

## Scale Function

`SpawnRate_GetScale()`:

```c
u32 min_pct = ap_save->options.spawn_rate_min;
if (min_pct < 100) min_pct = 100;          // floor at vanilla; also handles options-not-received-yet
float scale = (float)min_pct / 100.0f
            + (float)level * 0.1f;
if (scale > 5.0f) scale = 5.0f;
return scale;
```

Examples:

| `spawn_rate_min` | `level` | scale | effective rate |
|------------------|---------|-------|----------------|
| 100 (default) | 0 | 1.0× | vanilla |
| 100 | 10 | 2.0× | 2× vanilla |
| 200 | 0 | 2.0× | 2× vanilla (no items collected yet) |
| 200 | 10 | 3.0× | 3× vanilla |
| 100 | 50 | 5.0× | cap |
| 500 | 5 | 5.0× | cap (already at min, items add nothing) |

The 5× cap is the symmetric ceiling for both modes:

- **CT timer** — vanilla 60-frame timers divided by 5 = 12 frames, well above the 4-frame floor (so the floor still bites at scale ≈ 15, not at the cap).
- **TR probability** — vanilla per-frame spawn probability multiplied by 5 lands roughly at CT's 15-spawns-per-second ceiling.

Without the cap, TR would saturate to "spawn every frame" while CT stayed bounded by its floor — the experience would diverge.

## Hooks

`SpawnRate_OnBoot()` installs four code patches:

| Address | Type | Purpose |
|---------|------|---------|
| `0x800ea8b0` | HOOKCREATE | CT timer scale (first store site in `CityItemSpawn_UpdateAndCheckToSpawn`) |
| `0x800ea990` | HOOKCREATE | CT timer scale (second store site, same function, different path) |
| `0x800eaa8c` | HOOKCONDITIONALCREATE | CT cap scale (replaces `cmpw cur_num_items, item_max`) |
| `0x8034bae0` | REPLACECALL | TR probability scale (wraps the `bl HSD_Randf` in `TopRideItem_SpawnTimed`) |

### CT Timer Scaling

The vanilla spawn system in `CityItemSpawn_UpdateAndCheckToSpawn` (0x800ea6e0) picks a random timer in `[ItemFallDesc.spawn_time_min, spawn_time_max]`, decrements it each frame, and triggers a spawn at zero. Two paths in the function reset the timer; both end with the pattern:

```
... random pick + clamp r0 to >= 4 ...
0x800ea8b0 / 0x800ea990:  lwz  r3, 1552(r13)   ; reload grBoxGeneInfo
0x800ea8b4 / 0x800ea994:  stw  r0, 44(r3)       ; *(grBoxGeneInfo + 0x2C) = timer
```

We hook the `lwz` (which the framework moves into the trampoline). The prologue `mr 3, 0` passes the fresh timer to `SpawnRate_ScaleCTTimer`; the epilogue `mr 0, 3` puts the scaled result back in `r0`. The original `lwz` then runs (reloading `r3` for the upcoming `stw`), and execution returns to the `stw` at +0x4.

`SpawnRate_ScaleCTTimer(timer)`:

- `level == 0` → pass through (vanilla speed)
- otherwise → `timer / scale`, floored at **4 frames** (≈15 spawns/sec ceiling at 60 fps)

The 4-frame floor is the sole reason CT cannot reach "spawn every frame" even at extreme levels.

### CT Cap Scaling

Faster spawning alone churns existing items rather than growing density — the simultaneous-item cap (`ItemFallDesc.item_max`) caps at the same value. To make density actually grow, the cap also needs to scale.

The cap check sits inside the same function:

```
0x800eaa84:  lwz  r3, 32(r5)    ; cur_num_items  (grBoxGeneInfo + 0x20)
0x800eaa88:  lwz  r0,  4(r30)   ; ItemFallDesc.item_max
0x800eaa8c:  cmpw r3, r0
0x800eaa90:  bge  0x800eab4c    ; skip-spawn when cur >= cap
0x800eaa94:  ...                 ; spawn-success path
```

`r30` resolves upstream to one of:

- The active `ItemFallDesc` from `grBoxGeneInfo->fall_timer_desc->desc[i]` (per-stage table, picked by `match_progress`).
- `&grBoxGeneInfo.cur_event_fall_desc` (= `grBoxGeneInfo + 0x3C`) when an event override is active. `_CityEvent_ModifyItemFallDesc` (0x800ed5b0) populates this from the same descriptor table; events do not introduce their own `item_max` values.

A single `HOOKCONDITIONALCREATE` at the `cmpw` covers both paths. `SpawnRate_CTCapReached(cur_num, cap)`:

```c
int scaled_cap = (int)((float)cap * SpawnRate_GetScale());
return cur_num >= scaled_cap;
```

Returns 1 (skip-spawn) or 0 (continue). On 0-return the original `cmpw` runs harmlessly (its flags are ignored — we branch past the `bge`). The epilogue `lwz 5, 1552(13)` restores `r5 = grBoxGeneInfo` since the `bl` to the C function clobbered it, and the spawn-success branch at `0x800eaa94` needs it.

Exit addresses:

- `0` (continue) → `0x800eaa94` (skip past the original `bge`)
- `1` (skip) → `0x800eab4c` (vanilla skip-spawn branch — sets `r30 = 3` and falls into the function epilogue)

### TR Probability Scaling

`TopRideItem_SpawnTimed` (0x8034b8c8) computes a per-frame spawn probability in `f30`, calls `HSD_Randf()`, and spawns when `random < probability`:

```
0x8034bae0:  bl    0x8041e610      ; HSD_Randf -> f1
0x8034bae4:  fcmpo cr0, f1, f30    ; compare random vs probability
0x8034bae8:  bge   0x8034bf14      ; skip-spawn if random >= probability
```

A `REPLACECALL` redirects the `bl` to `SpawnRate_ScaledRandf`, which divides the random by the scale:

```c
float r = HSD_Randf();
return r / SpawnRate_GetScale();
```

A smaller random makes the `random < probability` test more likely to succeed, so effective per-frame spawn probability scales by the same factor as the divisor. The natural saturation point is `effective_prob = scale * vanilla_prob`; once that crosses 1.0 every frame would spawn — which is exactly why the 5× cap exists.

## Increment Flow

`AP_ITEM_SPAWN_RATE_UP` is routed in `APItems_HandleItem`:

```c
case AP_ITEM_SPAWN_RATE_UP:
    SpawnRate_Increment();
    return 1;
```

`SpawnRate_Increment()`:

1. Saturate `ap_save->spawn_rate_level++` at 255
2. `OSReport` the new level + effective rate (`[SpawnRate] Level %d, effective rate %.0f%%.`)
3. `tb_api->EnqueueColoredNounFmt(NULL, "Spawn rate", tb_api->ItemColor, " increased (%.0f%%)", pct)` — emits **"Spawn rate increased (X%)"** with the noun colored in `ItemColor` (light green). `pct` is `SpawnRate_GetScale() * 100` — the absolute effective rate, not the delta from vanilla.

Showing absolute rate (e.g. `Spawn rate increased (250%)`) rather than a delta (`+150%`) keeps the textbox meaningful when the player has a non-vanilla floor. With a floor of 200%, the first item bumps to 210% — showing "(210%)" is clearer than "+10%" because the player sees where they actually are. The number also stops moving once the cap is hit, which honestly reflects the in-game state.

## Cross-Mode Coverage

| Mode | Scaled? | Mechanism |
|------|---------|-----------|
| City Trial — items | Yes | timer divisor + simultaneous-cap multiplier |
| Top Ride — items | Yes | per-frame probability multiplier |
| Air Ride — items | N/A | items are stage-placed, no rate to scale |
| Air Ride — enemies | No | proximity-driven per-slot, not periodic — would need a separate item (e.g. an "Enemy Density Up" trap) if ever wanted |
| City Trial — enemies | No | same reasoning as AR — out of scope for an *item* spawn-rate booster |

## `cur_num_items` is a count, not a cap

The field at `grBoxGeneInfo + 0x20` (`cur_num_items` in `game.h`) is a running count, not the cap — despite a name like `cur_max_items` being the tempting read. `CityItemSpawn_IncrementNum` (0x800ec57c) and `CityItemSpawn_DecrementNum` (0x800ec670) read/write it: incremented when an item spawns, decremented when one is destroyed. The actual cap lives in `ItemFallDesc.item_max`.

The adjacent `+0x24` (`total_spawn_count`) is a lifetime-only counter, written exclusively on positive deltas in `IncrementNum`. The field at `+0x28` (`total_num`) is touched by neither `IncrementNum` nor `DecrementNum`, so that label is unverified.

## Known Limitations

### `cur_event_fall_desc` populated from same source

Event overrides set `cur_event_fall_desc.item_max` by copying from the same per-stage descriptor table (see `_CityEvent_ModifyItemFallDesc` lines 0x800ed688-0x800ed698). So events that *should* allow more items than the baseline don't actually have higher caps in vanilla — they reuse the active descriptor's cap. Our scaling applies uniformly to both paths, which is the correct behavior given the actual data flow.

### GObj pool is a hidden ceiling

Items are GObj-allocated by `CityItem_Create` (0x8024eef4), drawn from a heap-allocated pool. There is no fixed-size array indexed by `item_max`, so raising the cap is structurally safe — but pool exhaustion at extreme levels is possible in theory. The failure mode is silent: `CityItem_Create` would return a null GObj and the spawn would just not happen. Not a crash, just missed spawns.

A sustained 5×-cap CT round is the scenario to watch for symptoms (missed spawns under extreme density).

### Scale is global, not per-mode

A single `SpawnRate_GetScale()` drives both CT and TR, with one shared `spawn_rate_min` floor and one shared item counter. There is no way to push CT harder while leaving TR alone (or vice versa) without splitting the level into two save fields and the option into two slot options. Currently fine — same item drives both, by design.

### Air Ride not covered

Receiving Spawn Rate Up while playing AR has no observable effect. The textbox still fires ("Spawn rate increased (X%)"), which could be confusing if AR were the player's only mode. Acceptable — the AP item is most useful in CT/TR, and a player exclusively in AR shouldn't see the item in their pool.

### `total_num` (+0x28) label is unverified

`IncrementNum` does not touch `+0x28`, so it's not the running count. The current `total_num` label in `game.h` is suspect. If a future investigation pins down its real purpose, rename it then.
