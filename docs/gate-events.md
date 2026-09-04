# City Trial Event Gating

Each of the 16 City Trial `EventKind`s can be individually locked behind an Archipelago unlock item. A locked event's weight in the per-round selection chance array is zeroed so it never triggers naturally. AP items 700-715 (`AP_EVENT_UNLOCK_BASE` + `EventKind`) route through `ap_item_handler.c` to `GateEvents_UnlockEvent`, which sets the bit in `APSave.event_unlocked_mask` and posts a textbox. The mask is exposed through `ArchipelagoAPI` as `AP_UNLOCK_EVENT`; when the slot option `event_gating_enabled` is 0 the connect-time pre-fill in `APOptions_ApplyUngatedCategories` (`main.c`) sets all 16 bits and the gate is inert.

`EVKIND_NUM` = 16; the kinds and their display names are `EventKind` / `EventKind_Names[]` in `externals/hoshi/include/event.h`.

**File:** `mods/archipelago/src/gate_events.c`.

## Two independent AP ranges touch events

Unlock IDs 700-715 control the **natural** event pool, handled here. Trigger IDs 200-215 (`AP_EVENT_BASE` + index) force-start an event immediately via `Event_GiveItem` -> `Event_Do(kind)` in `city_trial_event.c`, which sets the event state directly and never goes through the chance selection. The two are fully independent by design: the apworld can ship triggers as filler that fires a *locked* event while unlocks control the ambient pool, and `traplink.c` likewise treats a locked event as eligible trap content.

## Game System

Natural event selection runs in `CityEvent_Decide` (0x800edcf8). It builds a 16-entry chance array on its own stack (at `sp+0x08`) from the per-stadium-group weights table (`EventConfigData.event->weights`), then applies two history passes before a weighted-random pick:

1. A same-category diversity boost, adding +30 to events sharing the most recent event's category.
2. A recently-occurred exclusion pass that zeroes `chance_arr[prev_kind[i]]` for every `i < prev_kind_num`.

Both passes are skipped entirely when `prev_kind_num` is 0. The history itself lives in `EventCheckData` (`event.h`): `prev_kind[10]` holds the events that have occurred this match and `prev_kind_num` counts the valid entries.

That exclusion pass is what makes gating dangerous. With few events unlocked, every enabled event can already be recorded in `prev_kind[]`; the pass then zeroes all of their chances and nothing can fire for the rest of the match.

## Implementation

A single `CODEPATCH_HOOKCREATE` at `0x800ede24` (offset +0x12C into `CityEvent_Decide`) runs `GateEvents_FilterChances(chance_arr, ev_chk)`. At that point the chance array is populated from the weights table but neither history pass has run, so zeroing locked events there is what the passes then see. The clobbered instruction is `lwz r0, 64(r26)` - the reload of `prev_kind_num` for the history passes - and the hook's prologue supplies `addi r3, r1, 8` (the stack chance array) and `mr r4, r26` (`EventCheckData*`). Because the clobbered `lwz` re-executes after the C call, the passes pick up whatever value the filter left behind.

The filter zeroes the chance of every event whose unlock bit is clear, and counts as *enabled* only the events that are both unlocked and carry a positive base weight for the current stadium group - an unlocked event with weight 0 on this stage is not a candidate and must not be counted as one.

It then caps the history to break the deadlock:

```c
int max_history = (enabled_count * 5) / 8;
```

The integer form approximates x0.625 without floating point, and always leaves at least one slot selectable: with 3 enabled events the history shrinks to 1, keeping 2 candidates eligible. Without the cap, 2 unlocked events with both already in `prev_kind[]` means no event can ever fire again.
