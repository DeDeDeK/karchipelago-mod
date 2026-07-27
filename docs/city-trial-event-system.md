# City Trial Event System

City Trial events are random occurrences during a City Trial match (Dyna Blade attack, meteor shower, dense fog, ...). A state machine selects, activates and cleans up one event at a time. All tuning data - timings, per-stadium-group weights, per-event params, BGM/sky assignments - is loaded at runtime from the `GrCity1Event.dat` archive (HSD DAT archive, root name `grEventDataAllCity1`, root type `EventConfigData`).

## Event Kinds

16 event kinds, defined in the `EventKind` enum (`externals/hoshi/include/event.h`):

| Index | Kind | Description |
|-------|------|-------------|
| 0 | DYNABLADE | Dyna Blade flies over the city |
| 1 | TAC | TAC steals items from players |
| 2 | METEOR | Meteors rain down |
| 3 | PILLAR | Pillars rise from the ground |
| 4 | RUNAMOK | Enemies run amok |
| 5 | RESTORATIONAREA | Restoration areas appear (once-only) |
| 6 | RAILFIRE | Rail station catches fire |
| 7 | SAMEITEM | All items become the same type |
| 8 | LIGHTHOUSE | Lighthouse activates |
| 9 | SECRETCHAMBER | Secret chamber opens (once-only) |
| 10 | PREDICTION | Stadium prediction announcement (once-only) |
| 11 | MACHINEFORMATION | Machine formation spawns |
| 12 | UFO | UFO appears |
| 13 | BOUNCE | Bouncy physics |
| 14 | FOG | Dense fog covers the city |
| 15 | FAKEPOWERUPS | Fake powerup items appear |

## Data Hierarchy

### EventCheckData (200 bytes, allocated via HSD_MemAlloc)

The main event state struct, stored as GOBJ userdata. Pointer to the GOBJ is at `stc_eventcheck_gobj` (0x805dd6f8, r13+0x618).

| Offset | Field | Description |
|--------|-------|-------------|
| 0x00 | `data` | -> `EventConfigData` (archive root) |
| 0x04 | `state` | 0 = idle, 1 = starting, 2 = active, 3 = cleanup |
| 0x08 | `cur_kind` | `EventKind`, -1 when no event |
| 0x0C | `xc` | read by the state machine, purpose unidentified |
| 0x10 | `timer` | counts up each frame |
| 0x14 | `event_time` | delay target - event advances when `timer >= this` |
| 0x18 | `prev_kind[10]` | history ring of recent event kinds |
| 0x40 | `prev_kind_num` | number of events in history |
| 0x44 | `occurrence_count[16]` | per-event-kind occurrence counter |
| 0x84 | `reserve[16]` | reserve queue of event kinds |
| 0xC4 | `reserve_kind_num` | count of reserved events |

### EventConfigData (ev_chk->data)

| Offset | Field | Description |
|--------|-------|-------------|
| 0x00 | `event` | Pointer to inner config (timing, chances, weights, params) |
| 0x04 | `bgm_sky` | Pointer to per-event BGM/sky table (0x14 bytes per kind) |
| 0x08 | (self-pointer) | |
| 0x0C | (extra data) | Used during setup |

The archive is loaded by `fn_grSetupCityEventData` (0x8010f7c4) on every City Trial load, and stashed at `GrData.event_config`, **regardless** of the City Trial events on/off setting. `EventCheckData.data` holds the same pointer, but only when events are enabled - `CityEvent_Init` (0x800edb88) bails without creating the event GOBJ when `Gm_CheckEnemyEnabled` (0x8000a348) returns 0.

### Inner Config (ev_chk->data->event)

| Offset | Value | Field | Description |
|--------|-------|-------|-------------|
| 0x00 | 3300 | delay_min | Min frames between events (~55s) |
| 0x04 | 7500 | delay_max | Max frames between events (~125s) |
| 0x08 | 70 | occur_chance | Weight for "event occurs" in occur/skip roll |
| 0x0C | 30 | skip_chance | Weight for "skip this cycle" |
| 0x10 | - | (unknown) | Present in the archive, not read by the state machine |
| 0x14 | 2200 | min_time | Min match time before events start (~37s). `CityEvent_StateIdle` gates on `City_GetMinSecMs?() >= event->[0x14]` - the gate is at `+0x14`, not `+0x10` |
| 0x18 | 4 | prev_kind_max | Max history entries |
| 0x1C | 180 | music_fadeout_frames | Frames to fade out BGM (3s) |
| 0x20 | 180 | starting_delay | Frames in state 1 before transitioning to state 2 (3s) |
| 0x24 | 300 | cleanup_delay | Frames in state 3 before returning to idle (5s) |
| 0x28 | 420 | hud_display_frames | Frames to display event HUD text (7s) |
| 0x2C | PTR | weights | -> `int[STGROUP_NUM][EVKIND_NUM]` chance weights |
| 0x30 | PTR | param | -> EventParam table (0xC bytes per kind) |

### EventParam (per-event, 0xC bytes each)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 4 | category | Category for diversity boost (0 or 1) |
| 0x04 | 4 | duration | Event duration in frames |
| 0x08 | 1 | once_only | If 1, can only occur once per match |
| 0x09 | 1 | is_siren | If 1, plays siren + fades music + changes sky |

| Event | Category | Duration | Once-Only | Siren |
|-------|----------|----------|-----------|-------|
| DYNABLADE | 0 | 3800 (63s) | No | Yes |
| TAC | 0 | 3600 (60s) | No | Yes |
| METEOR | 0 | 3600 (60s) | No | Yes |
| PILLAR | 0 | 3000 (50s) | No | Yes |
| RUNAMOK | 1 | 1500 (25s) | No | Yes |
| RESTORATIONAREA | 0 | 0 | Yes | No |
| RAILFIRE | 0 | 3000 (50s) | No | Yes |
| SAMEITEM | 1 | 2500 (42s) | No | Yes |
| LIGHTHOUSE | 0 | 2200 (37s) | No | Yes |
| SECRETCHAMBER | 0 | 500 (8s) | Yes | No |
| PREDICTION | 1 | 0 | Yes | No |
| FORMATION | 0 | 3000 (50s) | No | Yes |
| UFO | 0 | 0 | No | Yes |
| BOUNCE | 1 | 2200 (37s) | No | Yes |
| FOG | 1 | 4200 (70s) | No | Yes |
| FAKEPOWERUPS | 1 | 3200 (53s) | No | Yes |

### BGM/Sky Table (per-event, 0x14 bytes each)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 4 | bgm_file | BGM file index for secondary music |
| 0x04 | 4 | sky_preset | Sky preset index for sky transition (-1 = no change) |
| 0x08 | 4 | location_idx | Index into event location array |
| 0x0C | 4 | location_count | Number of event locations |
| 0x10 | 4 | event_data | Pointer to event-specific data |

| Event | BGM | Sky Preset | Has Locations | Has Event Data |
|-------|-----|------------|---------------|----------------|
| DYNABLADE | 0x32 | 1 | Yes (10) | No |
| TAC | 0x35 | 5 | Yes (10) | No |
| METEOR | 0x31 | 17 | Yes (10) | Yes |
| PILLAR | 0x30 | 2 | Yes (10) | Yes |
| RUNAMOK | 0x34 | -1 | No | No |
| RESTORATIONAREA | 0x00 | -1 | Yes (20) | Yes |
| RAILFIRE | 0x33 | 3 | Yes (10) | Yes |
| SAMEITEM | 0x2E | 6 | No | No |
| LIGHTHOUSE | 0x36 | 18 | No | Yes |
| SECRETCHAMBER | 0x00 | -1 | Yes (10) | Yes |
| PREDICTION | 0x00 | -1 | No | No |
| FORMATION | 0x40 | -1 | Yes (20) | Yes |
| UFO | 0x33 | 7 | Yes (10) | Yes |
| BOUNCE | 0x2F | 8 | No | Yes |
| FOG | 0x2D | 9 | No | No |
| FAKEPOWERUPS | 0x35 | 4 | No | Yes |

## State Machine

Driven by `CityEvent_Think` (0x800ee60c), called each frame. It increments `timer` and dispatches through the state function table at `0x804a5604`:

```
State 0 (Idle) --[decide]--> State 1 (Starting) --[delay]--> State 2 (Active) --[end]--> State 3 (Cleanup) --[delay]--> State 0
```

The map symbol names for slots 1 and 2 are shifted by one relative to the state they handle: slot 1 (Starting) is named `CityEvent_StateActive`, slot 2 (Active) is named `CityEvent_StateEnding`. Index by slot number, not by name.

### State 0 - Idle (CityEvent_StateIdle, 0x800ee270)

- Waits until match time >= `min_time` (2200 frames)
- Waits until `timer` >= `event_time` (random delay)
- Rolls `HSD_Randi(occur_chance + skip_chance)`:
  - If roll < `occur_chance` (70): calls `CityEvent_Decide` to select an event
  - Otherwise: sets new random delay in [delay_min, delay_max], resets timer

### State 1 - Starting (CityEvent_StateActive, 0x800ee328)

- Waits until `timer` >= `starting_delay` (180 frames / 3s). This is the siren/announcement period.
- Transitions to state 2:
  - Pushes `cur_kind` onto `prev_kind[]` history
  - Increments `occurrence_count[cur_kind]`
  - If `is_siren`: plays secondary BGM via `BGM_PlaySecondaryFile`
  - Shows HUD slide-in text (event name announcement)
  - Calls event's **start** function if non-NULL

### State 2 - Active (CityEvent_StateEnding, 0x800ee4c0)

- If event has an **active** function, calls it each frame
  - The active function is responsible for ending the event when its duration expires (by calling `CityEvent_EndWithSkyRestore`, which transitions to state 3)
- If the active function is NULL, immediately calls `CityEvent_EndWithSkyRestore`

### State 3 - Cleanup (CityEvent_StateCleanup, 0x800ee50c)

- If event has an **end** function, calls it each frame (gradual cleanup)
- Waits until `timer` >= `cleanup_delay` (300 frames / 5s)
- Calls event's **end2** function if non-NULL (final one-time cleanup)
- If `is_siren`: stops secondary music
- Sets new random delay, transitions to state 0, resets `cur_kind` to -1

## Event Selection (CityEvent_Decide, 0x800edcf8)

1. Copies base weights for the current stadium group into a local stack array (unrolled 16-entry copy at 0x800edda4-0x800ede20)
2. **Diversity boost**: For events whose category differs from the most recent event's category, add +30 to weight (if nonzero)
3. **Zero out history**: Sets weight=0 for all events in `prev_kind[]` history
4. **Once-only filter**: If `once_only` is set and `occurrence_count > 0`, sets weight=0 (loop at 0x800edf5c-0x800edff4)
5. **Reserve queue**: Checks reserved events first
6. **Weighted random**: `Gm_Roll(chance_arr, 16)` at 0x800ee098
7. If all weights are zero: sets new delay and returns to idle
8. **Check function**: If the event has a `check` function, calls it. If it fails, adds event to reserve queue and retries
9. **On success**: Sets `state=1`, `cur_kind`, resets timer. If `is_siren`:
   - Fades out music
   - Plays siren SFX (0x130002)
   - Calls `Sky_TransitionGlobal(bgm_sky[kind].sky_preset)` to change sky/lighting/fog

## Reserve Queue

The reserve queue (`ev_chk->reserve[]`, max 16 entries) is a priority list for events whose check function failed:

- When `CityEvent_Decide` selects an event and its check function returns 0, the event is appended to the reserve queue (with duplicate and overflow checks)
- On subsequent decide cycles, reserved events are tried **first** (in order) before falling back to `Gm_Roll`
- When a reserved event succeeds, it is removed from the queue
- `CityEvent_ForceStart` (0x800ee778, the game's built-in force-trigger function) also uses this - if the forced event's check fails, it queues it in reserve for later. Its overflow guard (`reserve_kind_num < 16`) is at 0x800ee80c.

## Event Function Table (0x804a5410)

Each `EventFunction` has 5 pointers (0x14 bytes):

| Pointer | Name | Called When | Purpose |
|---------|------|------------|---------|
| x0 | start | State 1 -> 2 transition | Setup event actors, modify item tables, init state |
| x4 | active | Each frame during state 2 | Run event logic, end event when duration expires |
| x8 | end | Each frame during state 3 | Gradual cleanup effects |
| xC | end2 | End of state 3 (once) | Final cleanup |
| check | check | Before starting | Validate preconditions (return 0 = fail) |

### Per-Event Functions

| Event | start | active | end | end2 | check |
|-------|-------|--------|-----|------|-------|
| DYNABLADE | 0x80110184 | 0x8011024c | 0x80110444 | 0x80110448 | - |
| TAC | 0x801104cc | 0x801104fc | 0x80110634 | 0x80110638 | - |
| METEOR | 0x80110b74 | 0x80110c0c | 0x80110f34 | 0x80110f38 | - |
| PILLAR | 0x8011111c | 0x80111604 | - | 0x80111710 | - |
| RUNAMOK | 0x80110f88 | 0x80110ff0 | 0x80111070 | 0x80111074 | - |
| RESTORATION | 0x80111078 | - | - | - | - |
| RAILFIRE | 0x80111738 | 0x8011179c | - | - | - |
| SAMEITEM | 0x80111b9c | 0x80111bc4 | - | - | - |
| LIGHTHOUSE | 0x80111f00 | 0x80111fb0 | - | - | - |
| SECRETCHAMBER | 0x801117f0 | 0x80111884 | - | - | - |
| PREDICTION | - | - | - | - | - |
| FORMATION | 0x80111cc0 | 0x80111d0c | 0x80111d68 | 0x80111d6c | **0x80111d70** |
| UFO | 0x80111934 | 0x80111968 | - | - | - |
| BOUNCE | 0x80111c20 | 0x80111c64 | - | - | - |
| FOG | 0x801118d8* | 0x801118dc | 0x8011192c* | 0x80111930* | - |
| FAKEPOWERUPS | 0x801119d8 | 0x80111a04 | - | - | - |

*FOG's start, end, and end2 functions are empty stubs. The fog visual effect comes entirely from the sky preset transition (preset 9), not from these functions.

FORMATION is the only vanilla event with a `check` function.

## Sky/Lighting

Events with `is_siren=1` change the sky/lighting during the event:

- **Start**: `Sky_TransitionGlobal(sky_preset)` begins a smooth transition to the event's sky preset (changed colors, fog, lighting). Called in `CityEvent_Decide`.
- **End**: `Sky_RestoreGlobal` (called via `CityEvent_EndWithSkyRestore`) transitions back to the default sky preset.

Events with `sky_preset = -1` do not modify the sky even if `is_siren=1` (e.g. RUNAMOK changes music but not sky).

## Item Drop Biasing

An active event can bias the city's item-fall chances. The path is keyed entirely on the vanilla event kind:

| Address | Symbol | Role |
|---------|--------|------|
| 0x800ed784 | `CityEvent_ModifyItemFallDesc(kind)` | Public wrapper. Its special-flag step (`grBoxGeneInfo+0x38`) recognizes only kind 15 (fake-powerups -> bit 2) and kind 7 (same-item -> bit 4) |
| 0x800ed5b0 | `_CityEvent_ModifyItemFallDesc` | Worker. Linear-searches an event table by `entry[0] == kind`; no match falls back to the default chances |
| 0x800eb568 | `CityItemSpawn_SetEventsItemFallChances(kind)` | Reads each item's per-event chance at `*(short *)(entry + 4 + kind*2)`, an array sized to the 16 vanilla kinds inside each `0x28`-byte entry |

Because every step indexes by kind against 16-wide storage, a kind >= 16 reads past the array into adjacent struct fields and yields meaningless chances.

## Event HUD Text

`CityEvent_ShowHudText` (0x80113fb4) gates on IsInCity/IsInStadium and calls `stadiumPrediction` (0x80127864), which drives the popup through a cached HUD GObj pointer in the D-data global (`+0xbe8`). Two paths:

- **First event of the match** (cached GObj == 0): creates the popup GObj, stores the kind -> SIS-table index at HUD-data `+0x18`, and installs the per-frame proc `CityEvent_HudPredictionThink` (0x801276c0), which calls `CityEvent_SetSisText(stc_event_sis_id_table[data+0x18])` once the slide-in animation finishes.
- **Subsequent events** (GObj cached): reuses the GObj and calls `CityEvent_SetSisText(stc_event_sis_id_table[data+0x18])` directly.

Both paths resolve the text through `stc_event_sis_id_table` (`0x804a7b98[kind]`) -> a SIS index -> `stc_sis_data[0]`. The first path additionally special-cases `kind == 10` (PREDICTION), remapping it to a stadium-name SIS id.

The SIS id table holds 40 vanilla entries: indices 0-15 are the event names, 16-39 are the stadium name lookups used by the PREDICTION event.

## Fake Powerups Outside the Event

`CityItem_ProcessFakeItem` (0x802542dc) normally returns 0 unless the in-game Fake Powerups event is active (`stc_city_item_mgr->fake_event_data != NULL`); its caller `Machine_OnTouchItem` (0x801db34c, call site 0x801db8c0) then skips `bl Machine_ApplyHurt` and the fake patch silently does nothing.

AP traps spawn `ITKIND_*FAKE` items *outside* the event, so the archipelago mod `REPLACEFUNC`s this with `fake_patches.c`'s `ProcessFakeItem`. That replacement reads the fake-item data from `gr->gr_data->event_config->bgm_sky[EVKIND_FAKEPOWERUPS].event_data` rather than `*stc_eventcheck_gobj`, because the event GOBJ is never created when City Trial events are disabled - whereas the archive (and therefore the `bgm_sky` table) is loaded unconditionally, so `event_data` is always available. `Event_FakeItems_FillHurtParams` (0x80111a60) then fills the hurt params and the function returns 1.

## Helper Functions

| Address | Name | Description |
|---------|------|-------------|
| 0x800ee6c4 | CityEvent_GetTimer | Returns `ev_chk->timer` |
| 0x800ee6cc | CityEvent_GetDuration | Returns `param[cur_kind].duration` |
| 0x800ee6ec | CityEvent_GetLocationIndex | Returns `bgm_sky[cur_kind].location_idx` |
| 0x800ee708 | CityEvent_GetLocationCount | Returns `bgm_sky[cur_kind].location_count` |
| 0x800ee724 | CityEvent_GetLocationIndexForKind | Same, for an explicit kind |
| 0x800ee73c | CityEvent_GetFakeItemData | `bgm_sky[EVKIND_FAKEPOWERUPS].event_data` |
| 0x800ee758 | CityEvent_GetEventDataForKind | `bgm_sky[kind].event_data` |
| 0x800ee770 | CityEvent_GetGObj | Returns `*stc_eventcheck_gobj` |
| 0x800ee660 | CityEvent_EndWithSkyRestore | Sets state=3, resets timer, restores sky if is_siren |
| 0x800ee778 | CityEvent_ForceStart | Game's built-in force-trigger function for a specific event kind |
| 0x800ee93c | CityEvent_GetOccurrenceCount | Returns `occurrence_count[kind]` |

## Key Addresses

| Address | Symbol | Description |
|---------|--------|-------------|
| 0x805dd6f8 | stc_eventcheck_gobj | GOBJ** for the event system (r13+0x618) |
| 0x804a5410 | stc_event_function | Event function table (16 entries x 0x14 bytes) |
| 0x804a5604 | stc_event_state_table | State machine function pointer table (4 entries) |
| 0x804a7b98 | stc_event_sis_id_table | Event/stadium name -> SIS index lookup (40 vanilla entries) |
| 0x800edb88 | CityEvent_Init | Creates the event GOBJ; zeros the 16 occurrence counters at 0x800edc30-0x800edc6c |
| 0x800edcf8 | CityEvent_Decide | Event selection logic |
| 0x800ee60c | CityEvent_Think | Per-frame event state machine driver |
| 0x8010f7c4 | fn_grSetupCityEventData | Loads `GrCity1Event.dat` on City Trial load |
| 0x800d5444 | Sky_TransitionGlobal | Transition sky to a preset |
| 0x800d546c | Sky_RestoreGlobal | Restore sky to default |
| 0x800db2b8 | Gm_Roll | Weighted random selection |
