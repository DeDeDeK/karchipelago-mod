# Spawn Rate

`AP_ITEM_SPAWN_RATE_UP` is a progressive AP filler that scales how often items spawn during gameplay. `SpawnRate_GetScale()` in `mods/archipelago/src/spawn_rate.c` returns `min_pct/100 + level * 0.1`, capped at 3.0 (`SPAWN_RATE_SCALE_MAX`), where `min_pct` is the AP slot option `spawn_rate_min` and `level` is `ap_save->spawn_rate_level`, the count of items received. That one scale drives three independent behaviors: the City Trial spawn timer, the City Trial simultaneous-item cap, and the Top Ride per-frame spawn probability.

Supporting files: `main.h` (save byte + item enum + slot option), `ap_item_handler.c` (dispatch), `archipelago_debug/src/debug_menu.c` (give action).

## Scale

`spawn_rate_min` is a percentage in `APSlotOptions`, written once when the AP client sends options at handshake. The AP world's range is 10-100, so a slot can start **below** vanilla: at `min_pct = 10` items spawn a tenth as often and Spawn Rate Up items climb back toward (and past) vanilla. Sub-vanilla suppression is a deliberate option, not a degenerate case. `SpawnRate_GetScale` treats 0 (options not yet received) as 100 and floors anything nonzero below 10 at 10, because the scale is used as a **divisor** in both the CT timer and the TR probability hooks.

`ap_save->spawn_rate_level` is one byte, saturated at 255 in `SpawnRate_Increment` and zeroed by `OnSaveInit`'s `memset`.

The paired generation-side option `spawn_rate_max` (range 100-300, snapped to a multiple of 10) is **not** shipped to the game - it only sizes the item pool. The world ships `(max - min) / 10` Spawn Rate Up items, so collecting them all bumps the player from `min` to `max`. That 300 ceiling is why `SPAWN_RATE_SCALE_MAX` is 3.0: a slot that collects everything lands exactly on the cap rather than overshooting. It is also the symmetric ceiling for both modes - CT's vanilla 60-frame timers divided by 3 give 20 frames, well clear of the 4-frame floor, while TR's per-frame probability tripled stays short of saturating to "spawn every frame". Without the cap TR would saturate while CT stayed bounded by its floor, and the two modes would diverge.

## Hooks

`SpawnRate_OnBoot()` installs four code patches:

| Address | Type | Purpose |
|---------|------|---------|
| `0x800ea8b0` | HOOKCREATE | CT timer scale (first store site in `CityItemSpawn_UpdateAndCheckToSpawn`) |
| `0x800ea990` | HOOKCREATE | CT timer scale (second store site, same function, different path) |
| `0x800eaa8c` | HOOKCONDITIONALCREATE | CT cap scale (replaces `cmpw cur_num_items, item_max`) |
| `0x8034bae0` | REPLACECALL | TR probability scale (wraps the `bl HSD_Randf` in `TopRideItem_SpawnTimed`) |

### City Trial timer scaling

`CityItemSpawn_UpdateAndCheckToSpawn` (0x800ea6e0) picks a random timer in `[ItemFallDesc.spawn_time_min, spawn_time_max]`, decrements it each frame, and triggers a spawn at zero. Two paths in the function reset the timer; both end with the same pattern:

```
0x800ea8b0 / 0x800ea990:  lwz  r3, 1552(r13)   ; reload grBoxGeneInfo
0x800ea8b4 / 0x800ea994:  stw  r0, 44(r3)      ; *(grBoxGeneInfo + 0x2C) = timer
```

The hook sits on the `lwz` (which the framework moves into the trampoline). The prologue `mr 3, 0` passes the fresh timer to `SpawnRate_ScaleCTTimer`, the epilogue `mr 0, 3` puts the scaled result back in r0, and the original `lwz` then runs to reload r3 for the upcoming `stw`.

`SpawnRate_ScaleCTTimer` returns `timer / scale`, floored at **4 frames** (~15 spawns/sec at 60 fps). There is no early-out for the unscaled case: at `scale == 1.0` the division is an identity, and at `scale < 1.0` it *lengthens* the timer, which is how a sub-vanilla `spawn_rate_min` suppresses spawns. That 4-frame floor is the sole reason CT cannot reach "spawn every frame" even at extreme levels.

### City Trial cap scaling

Faster spawning alone churns existing items rather than growing density, because the simultaneous-item cap (`ItemFallDesc.item_max`) stays put. The cap check sits in the same function:

```
0x800eaa84:  lwz  r3, 32(r5)    ; cur_num_items  (grBoxGeneInfo + 0x20)
0x800eaa88:  lwz  r0,  4(r30)   ; ItemFallDesc.item_max
0x800eaa8c:  cmpw r3, r0
0x800eaa90:  bge  0x800eab4c    ; skip-spawn when cur >= cap
0x800eaa94:  ...                ; spawn-success path
```

`r30` resolves upstream to either the active `ItemFallDesc` from `grBoxGeneInfo->fall_timer_desc->desc[i]` (per-stage table, picked by `match_progress`) or `&grBoxGeneInfo.cur_event_fall_desc` (= `grBoxGeneInfo + 0x3C`) when an event override is active. A single `HOOKCONDITIONALCREATE` at the `cmpw` covers both.

`SpawnRate_CTCapReached(cur_num, cap)` compares `cur_num` against `cap * scale`, but **only ever scales the cap up**: for a sub-vanilla rate the lengthened timer already suppresses spawns, and a down-scaled cap could truncate to 0 and block them outright. It returns 1 to skip the spawn, 0 to continue; on the 0-return the original `cmpw` runs harmlessly since the conditional hook branches past the `bge`. The epilogue `lwz 5, 1552(13)` restores `r5 = grBoxGeneInfo`, which the `bl` to the C function clobbered and the spawn-success branch needs.

Exit addresses: `0` continues at `0x800eaa94`, past the original `bge`; `1` jumps to `0x800eab4c`, the vanilla skip-spawn branch that sets `r30 = 3` and falls into the function epilogue.

Event overrides populate `cur_event_fall_desc.item_max` by copying from the same per-stage descriptor table (`_CityEvent_ModifyItemFallDesc`, 0x800ed5b0, copy sequence at 0x800ed688-0x800ed698), so events never carry a higher baseline cap than the active descriptor. Scaling applies uniformly to both paths.

### Top Ride probability scaling

`TopRideItem_SpawnTimed` (0x8034b8c8) computes a per-frame spawn probability in f30, calls `HSD_Randf()`, and spawns when `random < probability`:

```
0x8034bae0:  bl    0x8041e610      ; HSD_Randf -> f1
0x8034bae4:  fcmpo cr0, f1, f30
0x8034bae8:  bge   0x8034bf14      ; skip-spawn if random >= probability
```

A `REPLACECALL` redirects the `bl` to `SpawnRate_ScaledRandf`, which returns `HSD_Randf() / scale`. A smaller random makes the test more likely to succeed, so effective per-frame spawn probability scales by the same factor. Its natural saturation point is `scale * vanilla_prob >= 1.0`, at which point every frame would spawn - which is exactly what the 3x cap keeps out of reach.

## Increment

`APItems_HandleItem` routes `AP_ITEM_SPAWN_RATE_UP` to `SpawnRate_Increment()` above the 3D scene gate, so it applies in any scene. The increment saturates the save byte, logs the new level, and enqueues a textbox reading "Spawn rate increased (X%)" with the noun in `ItemColor`.

`X` is `SpawnRate_GetScale() * 100` - the absolute effective rate, not the delta from vanilla. With a non-vanilla floor of 200%, the first item lands at 210%, and showing "(210%)" is clearer than "+10%" because the player sees where they actually are. The number also stops moving once the cap is hit, which honestly reflects the in-game state.

## Cross-Mode Coverage

| Mode | Scaled? | Mechanism |
|------|---------|-----------|
| City Trial - items | Yes | timer divisor + simultaneous-cap multiplier |
| Top Ride - items | Yes | per-frame probability multiplier |
| Air Ride - items | No | items are stage-placed, not dynamically spawned - no rate knob exists |
| Air Ride / City Trial - enemies | No | proximity-driven per spawn slot, not timer- or probability-driven; scaling them would need a separate item |

Receiving Spawn Rate Up while playing Air Ride therefore has no observable effect, though the textbox still fires. Acceptable, since the item is most useful in CT/TR and an AR-only player should not see it in their pool.

A single `SpawnRate_GetScale()` drives both CT and TR from one `spawn_rate_min` and one counter. Pushing CT harder while leaving TR alone would mean splitting the level into two save fields and the option into two slot options.

## grBoxGeneInfo Counters

`grBoxGeneInfo + 0x20` (`cur_num_items` in `game.h`) is a running count of live items, not the cap - despite a name like `cur_max_items` being the tempting read. `CityItemSpawn_IncrementNum` (0x800ec57c) and `CityItemSpawn_DecrementNum` (0x800ec670) move it as items spawn and die. The actual cap lives in `ItemFallDesc.item_max`. The adjacent `+0x24` (`total_spawn_count`) is a lifetime-only counter written on positive deltas in `IncrementNum`; `+0x28` (`total_num`) is touched by neither.

## GObj Pool Ceiling

Items are GObj-allocated by `CityItem_Create` (0x8024eef4) from a heap-allocated pool. There is no fixed-size array indexed by `item_max`, so raising the cap is structurally safe, but pool exhaustion at extreme levels is possible. The failure mode is silent rather than fatal: `CityItem_Create` returns a null GObj and the spawn simply does not happen. A sustained 3x-cap CT round is the scenario to watch.
