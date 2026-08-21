# City Trial Event System

City Trial events are random occurrences during a match (Dyna Blade attack, meteor shower, dense fog, ...). A four-state machine selects, activates and cleans up one event at a time. All tuning - timings, per-stadium-group weights, per-event params, BGM/sky assignments - is loaded at runtime from the `GrCity1Event.dat` archive (root name `grEventDataAllCity1`, root type `EventConfigData`), so none of it is baked into the DOL.

There are 16 kinds, the `EventKind` enum in `externals/hoshi/include/event.h`, which also carries `EventKind_Names[]` for display. Every per-kind table in the engine is sized to exactly 16.

## Data Ownership

`fn_grSetupCityEventData` (0x8010f7c4) loads the archive on every City Trial load and stashes the root at `GrData.event_config`, **regardless** of the City Trial events on/off setting. `CityEvent_Init` (0x800edb88), by contrast, bails without creating the event GOBJ when `Gm_CheckEnemyEnabled` (0x8000a348) returns 0. So with events disabled the config is still resident and readable through `GrData`, but `stc_eventcheck_gobj` (0x805dd6f8, r13+0x618) stays NULL and `EventCheckData.data` never gets set. Anything that wants event *data* without an event *running* must go through `GrData.event_config`.

`EventCheckData` (200 bytes, `HSD_MemAlloc`'d, stored as the event GOBJ's userdata) is the live state: current state and kind, a frame timer, the next-event delay target, a `prev_kind[]` history ring, a 16-entry occurrence counter at `+0x44`, and a 16-entry reserve queue at `+0x84`. The struct is in `event.h`.

`EventConfigData` (also in `event.h`) has two arms: `event` (global timing/chance parameters plus pointers to the weight and param tables) and `bgm_sky` (0x14 bytes per kind: BGM file, sky preset, event location index/count, and a pointer to event-specific data).

### Global timing values (City Trial archive)

| Field | Value | Meaning |
|-------|-------|---------|
| `delay_min` / `delay_max` | 3300 / 7500 | Random inter-event delay range (~55s to ~125s) |
| `occur_chance` / `skip_chance` | 70 / 30 | Weights for the occur-or-skip roll in state 0 |
| `min_time` | 2200 | Match frames (~37s) that must elapse before events start. `CityEvent_StateIdle` gates on `City_GetMinSecMs?()` (0x8000a0f8) `>= event->min_time` at `+0x14`, not the unknown word at `+0x10` |
| `prev_kind_max` | 4 | Max history entries |
| `music_fadeout_frames` | 180 | BGM fadeout on a siren event |
| `starting_delay` | 180 | Frames in state 1 (the siren/announcement period) |
| `cleanup_delay` | 300 | Frames in state 3 before returning to idle |
| `hud_display_frames` | 420 | Frames the event HUD text stays up |

## State Machine

`CityEvent_Think` (0x800ee60c) runs each frame, increments `timer`, and dispatches through the 4-entry function table at `stc_event_state_table` (0x804a5604):

```
0 Idle --[decide]--> 1 Starting --[delay]--> 2 Active --[end]--> 3 Cleanup --[delay]--> 0
```

The map symbol names for slots 1 and 2 are shifted one off the state they handle: slot 1 (Starting) is `CityEvent_StateActive`, slot 2 (Active) is `CityEvent_StateEnding`. **Index by slot number, not by name.**

- **State 0, `CityEvent_StateIdle` (0x800ee270)** - waits for match time `>= min_time`, then for `timer >= event_time`. Rolls `HSD_Randi(occur_chance + skip_chance)`; a roll below `occur_chance` calls `CityEvent_Decide`, otherwise it picks a new random delay in `[delay_min, delay_max]` and resets the timer.
- **State 1, `CityEvent_StateActive` (0x800ee328)** - waits `starting_delay` frames, then pushes `cur_kind` onto `prev_kind[]`, increments `occurrence_count[cur_kind]` (the write at `ev_chk+0x44+kind*4`, 0x800ee420-0x800ee434), starts the secondary BGM for a siren event, shows the HUD announcement, and calls the event's `start` function.
- **State 2, `CityEvent_StateEnding` (0x800ee4c0)** - calls the event's `active` function each frame. **The active function is what ends the event**, by calling `CityEvent_EndWithSkyRestore` once the duration expires. If there is no active function, state 2 ends immediately.
- **State 3, `CityEvent_StateCleanup` (0x800ee50c)** - calls the event's `end` function each frame for gradual cleanup, then after `cleanup_delay` calls `end2` once, stops the secondary music, picks a new delay, and returns to state 0 with `cur_kind = -1`.

## Event Selection

`CityEvent_Decide` (0x800edcf8) builds a 16-entry weight array on the stack and rolls it:

1. Copies the current stadium group's base weights into a local array (unrolled 16-entry copy, 0x800edda4-0x800ede20).
2. **Diversity boost**: any event whose `category` differs from the most recent event's category gets +30 (if its weight is nonzero).
3. Zeroes the weight of every kind still in `prev_kind[]`.
4. Zeroes any `once_only` event whose `occurrence_count` is already nonzero (loop at 0x800edf5c-0x800edff4).
5. Tries the reserve queue first; otherwise `Gm_Roll(chance_arr, 16)` at 0x800ee098 picks a winner.
6. If every weight is zero, it sets a new delay and stays idle.
7. If the winner has a `check` function that fails, the kind is appended to the reserve queue and the roll retries.
8. On success: `state = 1`, `cur_kind` set, timer reset. For a siren event it also fades the music, plays SFX `0x130002`, and calls `Sky_TransitionGlobal(bgm_sky[kind].sky_preset)`.

The **reserve queue** (`ev_chk->reserve[]`, 16 entries) is the priority list for events whose check failed. Reserved kinds are retried ahead of the weighted roll on later cycles and removed on success. `CityEvent_ForceStart` (0x800ee778) feeds it too - a forced event whose check fails is queued instead of dropped. Its overflow guard (`reserve_kind_num < 16`) is at 0x800ee80c.

## Event Function Table

`stc_event_function` (0x804a5410) is 16 x 0x14 bytes. Each `EventFunction` is `{start, active, end, end2, check}`:

- `start` - once on the state 1 -> 2 transition. Spawns actors, modifies item tables, inits state.
- `active` - every frame in state 2. Runs the event and is responsible for ending it.
- `end` - every frame in state 3, for gradual cleanup.
- `end2` - once when `cleanup_delay` expires.
- `check` - before starting; returning 0 blocks the trigger and queues the kind in reserve.

## Per-Event Data

`category` feeds the diversity boost, `duration` is the state-2 length in frames, `once_only` limits the event to one occurrence per match, and `is_siren` selects the siren + music-fade + sky-transition treatment. Sky preset `-1` means the sky is left alone even for a siren event (RUNAMOK changes music only).

| Kind | Cat | Duration | Once | Siren | BGM | Sky | Locs | EvData |
|------|----:|---------:|------|-------|----:|----:|-----:|--------|
| 0 DYNABLADE | 0 | 3800 | - | yes | 0x32 | 1 | 10 | - |
| 1 TAC | 0 | 3600 | - | yes | 0x35 | 5 | 10 | - |
| 2 METEOR | 0 | 3600 | - | yes | 0x31 | 17 | 10 | yes |
| 3 PILLAR | 0 | 3000 | - | yes | 0x30 | 2 | 10 | yes |
| 4 RUNAMOK | 1 | 1500 | - | yes | 0x34 | -1 | - | - |
| 5 RESTORATIONAREA | 0 | 0 | yes | - | 0x00 | -1 | 20 | yes |
| 6 RAILFIRE | 0 | 3000 | - | yes | 0x33 | 3 | 10 | yes |
| 7 SAMEITEM | 1 | 2500 | - | yes | 0x2E | 6 | - | - |
| 8 LIGHTHOUSE | 0 | 2200 | - | yes | 0x36 | 18 | - | yes |
| 9 SECRETCHAMBER | 0 | 500 | yes | - | 0x00 | -1 | 10 | yes |
| 10 PREDICTION | 1 | 0 | yes | - | 0x00 | -1 | - | - |
| 11 MACHINEFORMATION | 0 | 3000 | - | yes | 0x40 | -1 | 20 | yes |
| 12 UFO | 0 | 0 | - | yes | 0x33 | 7 | 10 | yes |
| 13 BOUNCE | 1 | 2200 | - | yes | 0x2F | 8 | - | yes |
| 14 FOG | 1 | 4200 | - | yes | 0x2D | 9 | - | - |
| 15 FAKEPOWERUPS | 1 | 3200 | - | yes | 0x35 | 4 | - | yes |

| Kind | start | active | end | end2 | check |
|------|-------|--------|-----|------|-------|
| DYNABLADE | 0x80110184 | 0x8011024c | 0x80110444 | 0x80110448 | - |
| TAC | 0x801104cc | 0x801104fc | 0x80110634 | 0x80110638 | - |
| METEOR | 0x80110b74 | 0x80110c0c | 0x80110f34 | 0x80110f38 | - |
| PILLAR | 0x8011111c | 0x80111604 | - | 0x80111710 | - |
| RUNAMOK | 0x80110f88 | 0x80110ff0 | 0x80111070 | 0x80111074 | - |
| RESTORATIONAREA | 0x80111078 | - | - | - | - |
| RAILFIRE | 0x80111738 | 0x8011179c | - | - | - |
| SAMEITEM | 0x80111b9c | 0x80111bc4 | - | - | - |
| LIGHTHOUSE | 0x80111f00 | 0x80111fb0 | - | - | - |
| SECRETCHAMBER | 0x801117f0 | 0x80111884 | - | - | - |
| PREDICTION | - | - | - | - | - |
| MACHINEFORMATION | 0x80111cc0 | 0x80111d0c | 0x80111d68 | 0x80111d6c | 0x80111d70 |
| UFO | 0x80111934 | 0x80111968 | - | - | - |
| BOUNCE | 0x80111c20 | 0x80111c64 | - | - | - |
| FOG | 0x801118d8 | 0x801118dc | 0x8011192c | 0x80111930 | - |
| FAKEPOWERUPS | 0x801119d8 | 0x80111a04 | - | - | - |

FOG's `start`, `end` and `end2` are empty stubs - the fog is entirely the sky preset 9 transition. MACHINEFORMATION is the only vanilla event with a `check`.

The map names the functions after internal event names that differ from the enum: `event_stationFire` = RAILFIRE, `event_rubberyItems` = BOUNCE, `event_denseFog` = FOG, `event_sameItems` = SAMEITEM, `event_restorationAreas` = RESTORATIONAREA, `event_fakeItems` = FAKEPOWERUPS, `event_formation_init` = the MACHINEFORMATION check.

## Sky and Lighting

A siren event begins its sky change in `CityEvent_Decide`, not in the event's own `start`: `Sky_TransitionGlobal` (0x800d5444) smoothly moves to the event's preset (colors, fog, lighting). `CityEvent_EndWithSkyRestore` (0x800ee660) sets state 3 and calls `Sky_RestoreGlobal` (0x800d546c) to transition back.

## Item Drop Biasing

An active event can bias the city's item-fall chances, and the whole path is keyed on the vanilla kind:

| Address | Symbol | Role |
|---------|--------|------|
| 0x800ed784 | `CityEvent_ModifyItemFallDesc(kind)` | Public wrapper. Its special-flag step (`grBoxGeneInfo+0x38`) recognizes only kind 15 (fake-powerups -> bit 2) and kind 7 (same-item -> bit 4) |
| 0x800ed5b0 | `_CityEvent_ModifyItemFallDesc` | Worker. Linear-searches an event table by `entry[0] == kind`; no match falls back to the default chances |
| 0x800eb568 | `CityItemSpawn_SetEventsItemFallChances(kind)` | Reads each item's per-event chance at `*(short *)(entry + 4 + kind*2)`, an array sized to the 16 vanilla kinds inside each `0x28`-byte entry |

Because every step indexes by kind against 16-wide storage, a kind `>= 16` reads past the array into adjacent struct fields and yields meaningless chances.

## Event HUD Text

`CityEvent_ShowHudText` (0x80113fb4) gates on IsInCity/IsInStadium and calls `stadiumPrediction` (0x80127864), which drives the popup through a cached HUD GObj pointer in the D-data global (`+0xbe8`). Two paths:

- **First event of the match** (cached GObj == 0): creates the popup GObj, stores the kind -> SIS-table index at HUD-data `+0x18`, and installs the per-frame proc `CityEvent_HudPredictionThink` (0x801276c0), which calls `CityEvent_SetSisText` once the slide-in animation finishes. This path additionally special-cases `kind == 10` (PREDICTION), remapping it to a stadium-name SIS id.
- **Subsequent events**: reuses the cached GObj and calls `CityEvent_SetSisText` (0x801169fc) directly.

Both resolve the text as `stc_event_sis_id_table[kind]` (0x804a7b98) -> a SIS index -> an entry in `stc_sis_data[0]`. The id table holds 40 vanilla entries: 0-15 are the event names, 16-39 the stadium names the PREDICTION event looks up.

## Fake Powerups Outside the Event

`CityItem_ProcessFakeItem` (0x802542dc) returns 0 unless the in-game Fake Powerups event is running (`stc_city_item_mgr->fake_event_data != NULL`); its caller `Machine_OnTouchItem` (0x801db34c, call site 0x801db8c0) then skips `bl Machine_ApplyHurt`, so a fake patch touched outside the event does nothing.

Because the archipelago mod spawns `ITKIND_*FAKE` items as traps outside the event, `mods/archipelago/src/fake_patches.c` `REPLACEFUNC`s it. The replacement reads the fake-item data from `gr->gr_data->event_config->bgm_sky[EVKIND_FAKEPOWERUPS].event_data` rather than from `*stc_eventcheck_gobj`, precisely because the event GOBJ is never created when City Trial events are off while the archive is loaded unconditionally. `Event_FakeItems_FillHurtParams` (0x80111a60) then fills the hurt params and the function returns 1.

## Accessors

Thin readers over `EventCheckData` and the config, all in the `0x800ee6xx`-`0x800ee9xx` block:

| Address | Name | Returns |
|---------|------|---------|
| 0x800ee6c4 | `CityEvent_GetTimer` | `ev_chk->timer` |
| 0x800ee6cc | `CityEvent_GetDuration` | `param[cur_kind].duration` |
| 0x800ee6ec | `CityEvent_GetLocationIndex` | `bgm_sky[cur_kind].location_idx` |
| 0x800ee708 | `CityEvent_GetLocationCount` | `bgm_sky[cur_kind].location_count` |
| 0x800ee724 | `CityEvent_GetLocationIndexForKind` | same, for an explicit kind |
| 0x800ee73c | `CityEvent_GetFakeItemData` | `bgm_sky[EVKIND_FAKEPOWERUPS].event_data` |
| 0x800ee758 | `CityEvent_GetEventDataForKind` | `bgm_sky[kind].event_data` |
| 0x800ee770 | `CityEvent_GetGObj` | `*stc_eventcheck_gobj` |
| 0x800ee93c | `CityEvent_GetOccurrenceCount` | `occurrence_count[kind]` |

## Key Addresses

| Address | Symbol | Description |
|---------|--------|-------------|
| 0x805dd6f8 | `stc_eventcheck_gobj` | `GOBJ**` for the event system (r13+0x618) |
| 0x804a5410 | `stc_event_function` | Event function table (16 x 0x14) |
| 0x804a5604 | `stc_event_state_table` | State handler dispatch table (4 entries) |
| 0x804a7b98 | `stc_event_sis_id_table` | Event/stadium name -> SIS index (40 vanilla entries) |
| 0x800edb88 | `CityEvent_Init` | Creates the event GOBJ; zeros the 16 occurrence counters at 0x800edc30-0x800edc6c |
| 0x800edcf8 | `CityEvent_Decide` | Event selection |
| 0x800ee60c | `CityEvent_Think` | Per-frame state machine driver |
| 0x800ee660 | `CityEvent_EndWithSkyRestore` | Sets state 3, resets the timer, restores the sky |
| 0x800ee778 | `CityEvent_ForceStart` | Built-in force-trigger for a specific kind |
| 0x8010f7c4 | `fn_grSetupCityEventData` | Loads `GrCity1Event.dat` on City Trial load |
| 0x800db2b8 | `Gm_Roll` | Weighted random selection |
