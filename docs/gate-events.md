# City Trial Event Gating

Each of the 16 City Trial `EventKind`s can be individually locked behind an Archipelago unlock item. A locked event's weight in the per-round selection chance array is zeroed so it never triggers naturally. AP items 700-715 (`AP_EVENT_UNLOCK_BASE` + `EventKind`) route through `ap_item_handler.c` to `GateEvents_UnlockEvent`, which sets the bit in `APSave.event_unlocked_mask` and announces "Unlocked Event: <name>" through `APAnnounce_Grant` (shown only with Messages -> Local -> Items on, default Off). The mask is exposed through `ArchipelagoAPI` as `AP_UNLOCK_EVENT`; when the slot option `event_gating_enabled` is 0 the pre-fill in `APOptions_ApplyUngatedCategories` (`ap_options.c`), run when the first slot options arrive, sets all 16 bits and the gate is inert. `GateEvents_IsUnlocked(kind)` is the shared mask read.

`EVKIND_NUM` = 16; the kinds and their display names are `EventKind` / `EventKind_Names[]` in `externals/hoshi/include/event.h`.

**File:** `mods/archipelago/src/gate_events.c`.

## Two independent AP ranges touch events

Unlock IDs 700-715 control the **natural** event pool, handled here. Trigger IDs 200-215 (`AP_EVENT_BASE` + index) force-start an event immediately via `CTEvent_Give` -> `CTEvent_Start(kind)` in `city_trial_event.c` - `CityEvent_ForceStart` (0x800ee778) without its reserve queue - which sets the event state directly and never goes through the chance selection. It returns 0, so the item handler keeps the give queued, outside the city stage, while another event is running, when the kind's own check fails, and for a `once_only` kind whose `occurrence_count` is nonzero - that one waits for a later round, since the stage reserves a once-only event's collision for a single run, and a second Restoration Area asserts in grcoll.c. The item handler does not consult the unlock mask for a trigger, so a received trigger fires even a *locked* event while unlocks control the ambient pool. The Energy Link shop (`energylink_spend.c`) does consult it through `GateEvents_IsUnlocked` and refuses to buy a locked one. A received TrapLink never starts an event.

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
