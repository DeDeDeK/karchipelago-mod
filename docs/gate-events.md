# City Trial Event Gating

Each of the 16 City Trial `EventKind`s can be individually locked behind an Archipelago unlock item. A locked event's weight in the per-round selection chance array is zeroed so it never triggers naturally; the separate AP event-*trigger* items still force-fire it on demand.

## What Is Gated

The 16 `EventKind`s (`externals/hoshi/include/event.h`), one bit each in `event_unlocked_mask`. Two independent AP item ranges touch events: trigger ID = `200 + index` (force-fires the event now, bypassing the gate — handled by `Event_GiveItem` in `city_trial_event.c`), unlock ID = `700 + index` (handled here).

| Idx | `EventKind` | Display name | Trigger | Unlock |
|----:|-------------|--------------|--------:|-------:|
| 0  | `EVKIND_DYNABLADE`        | Dyna Blade        | 200 | 700 |
| 1  | `EVKIND_TAC`             | Tac               | 201 | 701 |
| 2  | `EVKIND_METEOR`          | Meteor            | 202 | 702 |
| 3  | `EVKIND_PILLAR`          | Pillar            | 203 | 703 |
| 4  | `EVKIND_RUNAMOK`         | Run Amok          | 204 | 704 |
| 5  | `EVKIND_RESTORATIONAREA` | Restoration Area  | 205 | 705 |
| 6  | `EVKIND_RAILFIRE`        | Rail Fire         | 206 | 706 |
| 7  | `EVKIND_SAMEITEM`        | All Same Item     | 207 | 707 |
| 8  | `EVKIND_LIGHTHOUSE`      | Lighthouse        | 208 | 708 |
| 9  | `EVKIND_SECRETCHAMBER`   | Secret Chamber    | 209 | 709 |
| 10 | `EVKIND_PREDICTION`      | Prediction        | 210 | 710 |
| 11 | `EVKIND_MACHINEFORMATION`| Machine Formation | 211 | 711 |
| 12 | `EVKIND_UFO`             | UFO               | 212 | 712 |
| 13 | `EVKIND_BOUNCE`          | Bounce            | 213 | 713 |
| 14 | `EVKIND_FOG`             | Fog               | 214 | 714 |
| 15 | `EVKIND_FAKEPOWERUPS`    | Fake Powerups     | 215 | 715 |

`EVKIND_NUM` = 16. Display names come from `EventKind_Names[]` in `event.h`.

## Entry Points

**Files:** `mods/archipelago/src/gate_events.c` / `gate_events.h`

| Symbol | Kind | Where | Role |
|--------|------|-------|------|
| `GateEvents_OnBoot()` | mod | gate_events.c | Applies the hook at boot (called from `main.c`). |
| `GateEvents_FilterChances(int *chance_arr, EventCheckData *ev_chk)` | mod | gate_events.c | Hook body — zeroes locked events and caps the history buffer. |
| `GateEvents_UnlockEvent(int kind)` | mod | gate_events.c | Sets the unlock bit and posts a textbox. Called from `ap_item_handler.c` for IDs 700–715. |
| `CityEvent_Decide` | game | 0x800edcf8 (size 0x578) | Vanilla per-round event selector; hooked at +0x12C. |
| `Event_Do(kind)` | game | — | Force-start path used by the trigger items, via `Event_GiveItem` in `city_trial_event.c`. |

## Game System

Natural event selection runs in **`CityEvent_Decide`** (0x800edcf8). It builds a 16-entry chance array on its own stack (at `sp+0x08`) from the per-stadium-group weights table (`EventConfigData.event->weights`), then applies two history passes before a weighted-random pick:

1. A same-category diversity boost (adds `+30` to events sharing the most-recent event's category).
2. A **recently-occurred exclusion pass** that zeroes `chance_arr[prev_kind[i]]` for every `i` in `[0, prev_kind_num)`.

Both passes are gated by `prev_kind_num` (skipped entirely when it is 0). The relevant `EventCheckData` fields (`event.h`):

| Offset | Field | Meaning |
|-------:|-------|---------|
| 0x08 | `cur_kind` | Currently selected event |
| 0x18 | `prev_kind[10]` | History buffer — the events that have occurred this match |
| 0x40 | `prev_kind_num` | Number of valid entries in `prev_kind[]` |

When few events are unlocked the exclusion pass can deadlock: if every enabled event is already recorded in `prev_kind[]`, all their chances get zeroed and nothing can fire. The hook fixes this by capping `prev_kind_num`.

For the full event state machine, weights/param/BGM tables, and reserve queue, see `city-trial-event-system.md`.

## Implementation

`CODEPATCH_HOOKCREATE` at `0x800ede24` (offset `+0x12C` into `CityEvent_Decide`). The chance array has been populated from the weights table at this point, but the two history passes have **not** run yet — so zeroing locked events here, before history adjustment, is correct.

| Item | Value |
|------|-------|
| Clobbered instruction | `lwz r0, 64(r26)` (reload `prev_kind_num` for the history passes) |
| Injected setup | `addi r3, r1, 8` (r3 = `&chance_arr`, the stack array at `sp+0x08`) |
|                | `mr r4, r26` (r4 = `EventCheckData*`) |

After `GateEvents_FilterChances` returns, the clobbered `lwz` reloads `prev_kind_num` (which the hook may have lowered) for the subsequent history loops.

### Filter logic

For each `EventKind`:

- If its unlock bit is clear in `ap_save->event_unlocked_mask`, set `chance_arr[i] = 0`.
- Otherwise, if `chance_arr[i] > 0`, count it as enabled.

`enabled_count` therefore counts only events that are both unlocked **and** have a positive base weight for the current stadium group (an unlocked event with weight 0 on this stage is not counted).

The history buffer is then capped to break the deadlock:

```c
int max_history = (enabled_count * 5) / 8;
if (ev_chk->prev_kind_num > max_history)
    ev_chk->prev_kind_num = max_history;
```

### Interaction with the trigger items

Trigger items (200–215) force-start an event via `Event_GiveItem(kind)` → `Event_Do(kind)` (`city_trial_event.c`), which sets the event state directly rather than going through `CityEvent_Decide`'s chance selection. The gate and the triggers are fully independent: the apworld can ship triggers as filler that fires a locked event, while unlocks control the ambient (natural) event pool. `traplink.c` likewise treats a locked event as eligible trap content.

## Save Data

`u32 event_unlocked_mask` in `APSave` (`main.h`, accessed via the global `ap_save`) — bit N = `EventKind` N.

The mask is exposed through `ArchipelagoAPI` as `AP_UNLOCK_EVENT`. When the slot option `event_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` in `main.c` pre-fills the mask with `(1u << EVKIND_NUM) - 1` at connect, effectively disabling this gate.

## AP Items

16 AP items, `AP_EVENT_UNLOCK_BASE` (700, `archipelago_api.h`) + `EventKind` index → IDs 700–715. `ap_item_handler.c` routes IDs in `[700, 700 + EVKIND_NUM)` to `GateEvents_UnlockEvent(id - AP_EVENT_UNLOCK_BASE)`, which sets the bit, logs, and enqueues `"Unlocked Event: <name>"` via `tb_api->EnqueueColoredNoun` with `tb_api->EventColor`.

## Design Decisions

**History cap `(enabled_count * 5) / 8`:** Borrowed from KAR Deluxe. Without it, if only 2 events are unlocked and `prev_kind[]` already holds both, the exclusion pass zeroes both chances and no event can ever fire. The integer formula approximates ×0.625 without floating point, so at least one slot is always left selectable — e.g. with 3 enabled events the history shrinks to 1, guaranteeing 2 candidates stay eligible.

**Separate trigger and unlock ranges:** Triggers (200–215) and unlocks (700–715) are distinct AP item ranges so the apworld has maximum flexibility — triggers can be progressive filler that force a specific event, while unlocks control the natural pool.
