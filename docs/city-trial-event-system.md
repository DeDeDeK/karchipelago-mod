# City Trial Event System

## Overview

City Trial events are random occurrences during a City Trial match (e.g., Dyna Blade attack, meteor shower, dense fog). The event system manages selection, activation, and cleanup of these events via a state machine. Event data is loaded at runtime from the `GrCity1Event.dat` archive (HSD DAT archive, root name `grEventDataAllCity1`).

## Event Kinds

16 event kinds defined in `EventKind` enum:

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

The main event state struct, stored as GOBJ userdata. Pointer to the GOBJ is at `stc_eventcheck_gobj` (0x805dd6f8).

```
+0x00: data             -> EventConfigData (archive root, 4 pointers)
+0x04: state            (0=idle, 1=starting, 2=active, 3=cleanup)
+0x08: cur_kind         (EventKind, -1 when no event)
+0x0C: xc               (unknown)
+0x10: timer            (counts up each frame)
+0x14: event_time       (delay target — event advances when timer >= this)
+0x18: prev_kind[10]    (history ring of recent event kinds)
+0x40: prev_kind_num    (number of events in history)
+0x44: occurrence_count[16] (per-event-kind occurrence counter)
+0x84: reserve[16]      (reserve queue of event kinds)
+0xC4: reserve_kind_num (count of reserved events)
```

### EventConfigData (pointed to by ev_chk->data)

| Offset | Field | Description |
|--------|-------|-------------|
| 0x00 | `event` | Pointer to inner config (timing, chances, weights, params) |
| 0x04 | `bgm_sky` | Pointer to per-event BGM/sky table (0x14 bytes per kind) |
| 0x08 | (self-pointer) | |
| 0x0C | (extra data) | Used during setup |

### Inner Config (ev_chk->data->event)

| Offset | Value | Field | Description |
|--------|-------|-------|-------------|
| 0x00 | 3300 | delay_min | Min frames between events (~55s) |
| 0x04 | 7500 | delay_max | Max frames between events (~125s) |
| 0x08 | 70 | occur_chance | Weight for "event occurs" in occur/skip roll |
| 0x0C | 30 | skip_chance | Weight for "skip this cycle" |
| 0x10 | — | (unknown) | Not read by the state machine (see note below) |
| 0x14 | 2200 | min_time | Min match time before events start (~37s). `CityEvent_StateIdle` gates on `City_GetMinSecMs?() >= event->[0x14]` (confirmed via disasm/Ghidra) |
| 0x18 | 4 | prev_kind_max | Max history entries |
| 0x1C | 180 | music_fadeout_frames | Frames to fade out BGM (3s) |
| 0x20 | 180 | starting_delay | Frames in state 1 before transitioning to state 2 (3s) |
| 0x24 | 300 | cleanup_delay | Frames in state 3 before returning to idle (5s) |
| 0x28 | 420 | hud_display_frames | Frames to display event HUD text (7s) |
| 0x2C | PTR | weights | -> `int[STGROUP_NUM][EVKIND_NUM]` chance weights |
| 0x30 | PTR | param | -> EventParam table (0xC bytes per kind) |

> **`min_time` offset:** `min_time` lives at inner-config `+0x14` (not `+0x10`); `+0x10` is an unverified slot. Confirmed via disasm/Ghidra of `CityEvent_StateIdle` (0x800ee270), whose match-time gate reads `City_GetMinSecMs?() >= *(inner_config + 0x14)`. `externals/hoshi/include/event.h` was corrected to match this layout (2026-06-08).

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

Each EventKind has an entry with:

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

### Fake Powerups outside the event (AP fake-patch traps)

`CityItem_ProcessFakeItem` (0x802542dc) normally returns 0 unless the in-game Fake Powerups event is active (`stc_city_item_mgr->fake_event_data != NULL`); the caller (`Machine_OnTouchItem` at 0x801db8c0) then skips `bl Machine_ApplyHurt` and the fake patch silently does nothing. AP traps, however, spawn `ITKIND_*FAKE` items *outside* the event, so the archipelago mod `REPLACEFUNC`s this with `fake_patches.c`'s `ProcessFakeItem`, which looks the fake-data table up from the loaded archive directly.

It reads `gr->gr_data->event_config->bgm_sky[EVKIND_FAKEPOWERUPS].event_data` (the `event_data` pointer above) rather than `*stc_eventcheck_gobj`, because the event GOBJ is never created when City Trial events are disabled in the menu (`CityEvent_Init` bails when `Gm_CheckEnemyEnabled()` returns 0). The archive — and therefore the `bgm_sky` table — is loaded unconditionally by `fn_grSetupCityEventData` on every City Trial load, so `event_data` is always available. `Event_FakeItems_FillHurtParams(fake_data, hurt_params)` then fills the hurt params and the function returns 1.

## State Machine

Driven by `CityEvent_Think` (0x800ee60c), called each frame. Increments `timer` and dispatches based on `state`:

```
State 0 (Idle) ──[decide]──> State 1 (Starting) ──[delay]──> State 2 (Active) ──[end]──> State 3 (Cleanup) ──[delay]──> State 0
```

### State 0 — Idle (CityEvent_StateIdle, 0x800ee270)

- Waits until match time >= `min_time` (2200 frames)
- Waits until `timer` >= `event_time` (random delay)
- Rolls `HSD_Randi(occur_chance + skip_chance)`:
  - If roll < `occur_chance` (70): calls `CityEvent_Decide` to select an event
  - Otherwise: sets new random delay in [delay_min, delay_max], resets timer

### State 1 — Starting (CityEvent_StateActive, 0x800ee328)

- Waits until `timer` >= `starting_delay` (180 frames / 3s). This is the siren/announcement period.
- Transitions to state 2:
  - Pushes `cur_kind` onto `prev_kind[]` history
  - Increments `occurrence_count[cur_kind]`
  - If `is_siren`: plays secondary BGM via `BGM_PlaySecondaryFile`
  - Shows HUD slide-in text (event name announcement)
  - Calls event's **start** function if non-NULL

### State 2 — Active (CityEvent_StateEnding, 0x800ee4c0)

- If event has an **active** function, calls it each frame
  - The active function is responsible for ending the event when its duration expires (by calling `CityEvent_EndWithSkyRestore`, which transitions to state 3)
- If the active function is NULL, immediately calls `CityEvent_EndWithSkyRestore`

### State 3 — Cleanup (CityEvent_StateCleanup, 0x800ee50c)

- If event has an **end** function, calls it each frame (gradual cleanup)
- Waits until `timer` >= `cleanup_delay` (300 frames / 5s)
- Calls event's **end2** function if non-NULL (final one-time cleanup)
- If `is_siren`: stops secondary music
- Sets new random delay, transitions to state 0, resets `cur_kind` to -1

## Event Selection (CityEvent_Decide, 0x800edcf8)

1. Copies base weights for the current stadium group into a local array
2. **Diversity boost**: For events whose category differs from the most recent event's category, add +30 to weight (if nonzero)
3. **Zero out history**: Sets weight=0 for all events in `prev_kind[]` history
4. **Once-only filter**: If `once_only` is set and `occurrence_count > 0`, sets weight=0
5. **Reserve queue**: Checks reserved events first (see below)
6. **Weighted random**: Calls `Gm_Roll(weights, 16)` for weighted random selection
7. If all weights are zero: sets new delay and returns to idle
8. **Check function**: If the event has a `check` function, calls it. If it fails, adds event to reserve queue and retries
9. **On success**: Sets `state=1`, `cur_kind`, resets timer. If `is_siren`:
   - Fades out music
   - Plays siren SFX (0x130002)
   - Calls `Sky_TransitionGlobal(bgm_sky[kind].sky_preset)` to change sky/lighting/fog

## Reserve Queue

The reserve queue (`ev_chk->reserve[]`, max 16 entries) acts as a priority list for events whose check function failed:

- When `CityEvent_Decide` selects an event and its check function returns 0, the event is appended to the reserve queue (with duplicate and overflow checks)
- On subsequent decide cycles, reserved events are tried **first** (in order) before falling back to `Gm_Roll`
- When a reserved event succeeds, it is removed from the queue
- `CityEvent_ForceStart` (0x800ee778, the game's built-in force-trigger function) also uses this — if the forced event's check fails, it queues it in reserve for later

## Event Function Table (0x804a5410)

Each `EventFunction` has 5 pointers (0x14 bytes):

| Pointer | Name | Called When | Purpose |
|---------|------|------------|---------|
| x0 | start | State 1 → 2 transition | Setup event actors, modify item tables, init state |
| x4 | active | Each frame during state 2 | Run event logic, end event when duration expires |
| x8 | end | Each frame during state 3 | Gradual cleanup effects |
| xC | end2 | End of state 3 (once) | Final cleanup |
| check | check | Before starting | Validate preconditions (return 0 = fail) |

### Per-Event Functions

| Event | start | active | end | end2 | check |
|-------|-------|--------|-----|------|-------|
| DYNABLADE | Yes | Yes | Yes | Yes | - |
| TAC | Yes | Yes | Yes | Yes | - |
| METEOR | Yes | Yes | Yes | Yes | - |
| PILLAR | Yes | Yes | - | Yes | - |
| RUNAMOK | Yes | Yes | Yes | Yes | - |
| RESTORATION | Yes | - | - | - | - |
| RAILFIRE | Yes | Yes | - | - | - |
| SAMEITEM | Yes | Yes | - | - | - |
| LIGHTHOUSE | Yes | Yes | - | - | - |
| SECRETCHAMBER | Yes | Yes | - | - | - |
| PREDICTION | - | - | - | - | - |
| FORMATION | Yes | Yes | Yes | Yes | **Yes** |
| UFO | Yes | Yes | - | - | - |
| BOUNCE | Yes | Yes | - | - | - |
| FOG | Yes* | Yes | Yes* | Yes* | - |
| FAKEPOWERUPS | Yes | Yes | - | - | - |

*FOG's start, end, and end2 functions are empty stubs. The fog visual effect comes entirely from the sky preset transition (preset 9), not from these functions.

## Sky/Lighting System

Events with `is_siren=1` change the sky/lighting during the event:

- **Start**: `Sky_TransitionGlobal(sky_preset)` begins a smooth transition to the event's sky preset (changed colors, fog, lighting). Called in `CityEvent_Decide`.
- **End**: `Sky_RestoreGlobal` (called via `CityEvent_EndWithSkyRestore`) transitions back to the default sky preset.

Events with `sky_preset = -1` do not modify the sky even if `is_siren=1` (e.g., RUNAMOK changes music but not sky).

## Helper Functions

| Address | Name | Description |
|---------|------|-------------|
| 0x800ee6c4 | CityEvent_GetTimer | Returns `ev_chk->timer` |
| 0x800ee6cc | CityEvent_GetDuration | Returns `param[cur_kind].duration` |
| 0x800ee6ec | CityEvent_GetLocationIndex | Returns `bgm_sky[cur_kind].location_idx` |
| 0x800ee708 | CityEvent_GetLocationCount | Returns `bgm_sky[cur_kind].location_count` |
| 0x800ee660 | CityEvent_EndWithSkyRestore | Sets state=3, resets timer, restores sky if is_siren |
| 0x800ee778 | CityEvent_ForceStart | Game's built-in force-trigger function for a specific event kind |

## Key Addresses

| Address | Symbol | Description |
|---------|--------|-------------|
| 0x805dd6f8 | stc_eventcheck_gobj | Pointer to event system GOBJ |
| 0x804a5410 | stc_event_function | Event function table (16 entries x 0x14 bytes) |
| 0x804a5604 | (state dispatch) | State machine function pointer table (4 entries) |
| 0x800edcf8 | CityEvent_Decide | Event selection logic |
| 0x800ee60c | CityEvent_Think | Per-frame event state machine driver |
| 0x800d5444 | Sky_TransitionGlobal | Transition sky to a preset |
| 0x800d546c | Sky_RestoreGlobal | Restore sky to default |
