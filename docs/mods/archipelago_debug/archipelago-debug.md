# Archipelago Debug

`archipelago_debug` is the test harness for the archipelago mod: it flips unlock masks, gives
items, edits checks, goals and slot options, and fakes the client's link and text writes, so
every gated, granted or checked thing can be exercised without a server or a seed that happens to
reach it. It ships no game content and drives the other mods only through their exported APIs,
so a build without it behaves identically.

It has two surfaces. The **settings menu** page, "Archipelago Debug", holds everything that arms
state or acts outside a round. **Pad bindings** on controller 1 cover what only makes sense
inside a running round. The split exists because hoshi's settings menu is a main-menu scene with
no in-round overlay.

## Imports

The archipelago side goes through `ArchipelagoAPI`, imported at `OnSaveLoaded` (the first point
every mod has exported). Without it every row and binding does nothing. It also imports
custom_events, for the Scale Change binding, and textbox, for the confirmation line most menu
actions post; without either, only that feature is lost.

## Pad bindings

All read controller port 1 (`stc_engine_pads[0]`) on the rising edge, from `OnFrameStart`.
Everything but Z is limited to a live round - minor `MNRKIND_3D` (Air Ride and City Trial) or
`MNRKIND_TOPRIDE`, under the City Trial, Air Ride or Top Ride major - because the settings menu
and the checklist grid are both navigated with the D-Pad on the same controller. The major test
also keeps the title's attract demo out, which runs a real round under `MJRKIND_TITLE`.

| Binding | Action |
|---|---|
| D-Pad Left | Grant one checklist reward the seed placed remotely, chosen at random |
| L + D-Pad Left | End the round now |
| D-Pad Right | Queue one random unlock or persistent progression item |
| D-Pad Down | Drop an AP Box in front of the target player |
| R + D-Pad Down | Drop the next Archipelago Star sphere in front of the target player |
| L + D-Pad Down | Arm a DeathLink receive |
| D-Pad Up | Start Sky Rings, then Drive Backwards, Reverse Controls, Bomb TACs, Treasure Rain, then Mini UFOs, then Danger Rain raining meteors, Gordos, sensor bombs, bombs and dragons, then Stat Shuffle, Jumping Items, Minefield, Cannon, Truck Storm, Giant Mode and Tiny Mode, one per press (custom_events, City Trial with no event running) |
| L + D-Pad Up | Queue one random item that suits the running mode |
| R + D-Pad Up | Arm a TrapLink receive |
| Z (checklist screen) | Complete the cell under the cursor |

The two drops act on the **Target Player** row (slots 1-4), so per-player behavior can be tested
on any slot. The sphere cycle restarts at the first sphere each round and skips a locked one. An
AP Box only exists in a round that loaded with `ap_patches` above 0.

**Ending the round** zeroes `GameData.time_seconds`, the configured limit. Each mode's branch of
`Game_Think` ends the round on `seconds_passed >= time_seconds` once its own end condition has not
fired, so the round ends through the engine's own timeout - results, the closing stadium and every
round-end check run as at a natural finish. A lap-limited Air Ride race ends on laps instead. The
limit is re-seeded for the next round. This is what makes the round-end goals (Hydra and Dragoon,
King Dedede) testable at all.

**The Z unlock** asks `ArchipelagoAPI.GetHoveredCell` for the cell under the checklist cursor (0
with no checklist up, so Z elsewhere does nothing; vanilla navigation does not use Z), then calls
`ClearChecker_SetNewUnlock` (0x8004a054), which the archipelago mod has replaced - so the check is
recorded and the goals evaluated exactly as for a real completion. The replacement only sets
`is_new`, so the binding sets `is_unlocked` and `is_visible` too. With **Auto-Grant on Z Unlock**
on (Checks page, off by default) it also grants the reward placed on that cell; off is right with
a client attached, which delivers the reward itself.

**The random items.** D-Pad Right draws one ID uniformly from every unlock ID plus the
progression items (permanent patches, Permanent All Up, Patch Cap Increase, Spawn Rate Up),
skipping the machine IDs the apworld never ships. L + D-Pad Up draws from what the mode can
apply: in City Trial the events, every `ItemKind` give and the standalone gives; in Air Ride
only copy abilities, since other items need the city; in Top Ride the Top Ride item gives.

## The menu

The page holds submenus for unlock gates, item gives, checks, goals, slot options, cross-session
check progress, canned messages, link triggers and the EnergyLink balance, plus the Target Player
row, **Report State** (logs every mask with its gating flag, the patch cap and spawn rate, the
permanent patches, each row's goal and square count, the goal latches, AP Patches, the energy
counters and the check progress) and **Reset Progression** (rolls back what receipts accumulate -
patch cap, spawn rate, permanent patches, check progress, the max-stats flag, the received count
and the queue - leaving unlock masks and slot options alone).

Most rows are thin: a give row queues one AP item through `QueueItem`, so it takes the receive
path a client delivery would; a gate row is one bit of one unlock mask. The design rules are in
how rows write back.

### Unlock gate rows

Each gate page is one category's mask, one Disabled / Enabled row per bit (the machine page's
last row is the Archipelago Star's bit), with Unlock All, Lock All and Give Random. The rows read
from a cached array per category, which the menu draws. Rebuilding the mask from that array on a
toggle would revoke any bit gained elsewhere since - a delivery, a give, a reward - so each
category also keeps the mask it last synced, and a toggle owns only the bits that differ from it:

```
owned = built ^ synced
mask  = (live & ~owned) | (built & owned)
```

The rows are re-derived from the live masks at save load and every scene change. They are
`no_save`: a saved copy would only be overwritten.

### The boot replay

hoshi fires every value row's `on_change` once at boot, after every mod's `OnSaveLoaded`, and
`no_save` does not exempt a row. A row that writes AP state would therefore push the menu's value
over the seed's on every launch. So each such row keeps the value it last mirrored, refreshed
from the live state at `OnSaveLoaded` and every scene change, and its callback does nothing when
handed that same value. This matters most where a row shows a rounded value: the AP Patches row
offers Off / 8 / 64 / 512 and shows the nearest at or below the live count, and replaying that
would rewrite a 20-patch seed down to 8 and drop its collected patches past 8.

### Goals

One row per checklist row, a shared **Squares for N** count, and **Apply**. The rows only select
and Apply writes all four at once, because goals are judged as a set: victory needs one row with
a goal and every such row met. Writing on every D-Pad tick could stop briefly on a combination
that is already won - three rows on `GOAL_NONE` and the fourth on a met goal - and raise
`goal_complete`, which the client reports at once. Apply clears the latch of a row whose goal it
changes, passes the count only to rows on the count goal, and writes the card once. Before the
save has its slot options nothing is judged, and the first connect replaces the goals.

### Slot options

Overrides for what the seed ships: the per-category gating flags, the patch cap range and the
spawn rate floor, and **Re-apply**. The gating flags are read only at the first connect (the Top
Ride item gate is the one live reader), so Re-apply clears every unlock mask and re-runs the
connect-time reveal and ungated pre-fill; the masks are cleared first because the pre-fill only
sets bits. On a save that never connected every flag reads 0, so Re-apply unlocks everything; on
a connected save it discards unlocks received in gated categories. Each patch cap row writes only
its own bound, since the rows show the nearest offered step and writing both would round the
other down. Before the save has its slot options the mod ignores these values (the cap stays at
vanilla's 18, the spawn rate at 100%) and the first connect replaces them.

### Checks, progress, messages, links, energy

- **Checks**: clear or force-mark every sent check (force-mark also sets every AP Patch and the
  max-stats flag and latches every goal), latch the goals, reveal a row (visual only), simulate a
  random reward placement, wipe all checklist data (including the AP tab's objective progress,
  which would otherwise re-complete the wiped cells), and the AP Patch count, collect and clear.
  Clearing collected AP Patches clears the save bits, the wire mirror and any pending backfill
  together - any one left would restore the others. The check-state commands write the card.
- **Check Progress**: the counters behind the AP tab's cross-session objectives, so one can be set
  a step short of done and finished by one real event. They are not written to the card on each
  change.
- **Messages**: one canned client line per `APTextKind`, written as the client writes it, plus an
  overlong line that shows the wrap and truncation.
- **Links**: arm `deathlink_receive` or `traplink_receive`. Both wait for a round with a receive
  proc, so arming from the menu lands in the next round.
- **EnergyLink**: set the balance (offered values bracket the shop's cheapest and dearest items),
  add 1000, or drain to zero. It writes only the balance - lowering a rising counter would read as
  a huge withdrawal - and a connected client overwrites it on its next poll. Spending a debug
  balance reaches the server as a real withdrawal.
