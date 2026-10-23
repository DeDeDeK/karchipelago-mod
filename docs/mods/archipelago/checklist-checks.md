# Checklist Checks

Every checklist cell - the 360 vanilla ones and the Archipelago tab's 120 - is an Archipelago
location, and the mod is the source of truth for which ones the player has completed. It records
each first completion in a per-row bitmask that is both saved and published to the client, which
forwards new bits to the server as location checks. The server can also report checks the other
way, after a collect or on a new save, and the mod applies them to the board.

## The record

`APSave.sent_checks[CHECKLIST_MODE_NUM][2]` holds one bit per cell, two `u64` words per row
(cells 0-63 and 64-119). Rows 0-2 are the game modes and `AP_CHECKLIST_ROW` (3) the Archipelago
tab. `APData.sent_checks` mirrors it for the client and is written in the same breath, so the
client sees a check the frame it happens. `ChecklistModeRow(mode)` turns a runtime mode into its
row, or -1 for a custom tab the mod does not record, and `ChecklistRowMode(row)` goes back.

The record lives in two places that must agree: the bit in `APSave`, and `is_new` /
`is_unlocked` on the cell in the game's own clear data. Detection keys on the cell (below), so if
the game's save kept the cell while `APSave` lost the bit, the check could never fire again from
play.

## Detecting a completion

**The funnels.** `ClearChecker_SetNewUnlock` (0x8004a054) and
`ClearChecker_SetNewUnlockSilent` (0x80049fcc) are both replaced. They share one body: for a mode
`ChecklistModeRow` maps and a cell below 120, a cell with neither `is_new` nor `is_unlocked` is a
first completion and is recorded; then, unless a LAN session is up, `is_new` is set as vanilla
would. The loud variant also plays the unlock SFX on a fresh cell, keeping vanilla's one-frame
cooldown; Top Ride's evaluators play their own before calling the silent one. Recording happens
before the LAN check, so a completion during a session is still sent. The Archipelago tab's
completions come through the loud funnel too, with the tab's mode.

**The meta cells.** `Checklist_ProcessUnlock` writes the five auto-unlock cells (the three
"Fill in over 100 Checklist blocks!" cells and City Trial's Dragoon and Hydra parts cells) by
direct `stb`, bypassing the funnels. A conditional hook sits on the `li r3, 1` just before each
store - 0x8017efbc, 0x8017eff4, 0x8017f02c, 0x8017f0a8, 0x8017f11c. `r3` is the function's
return value and nothing after the store sets it again, so a hook on the store itself would
clobber it; hooked on the `li`, the relocated instruction re-runs after the call. The handler
records the cell, and the epilogue re-materializes the stored register (`r4` or `r0`).

If the cell already has `is_filler`, the handler instead sets `is_unlocked` itself and skips to
the function tail (0x8017f394), past the store and the display update. The store would overwrite
the byte with a bare `0x01` and erase the filler mark, which nothing restores. Setting
`is_unlocked` is required: each store is guarded by `!is_unlocked`, the skip returns 1 without
arming the phase timer, and `Checklist_Think` re-calls `Checklist_ProcessUnlock` until it
returns 0 - so a skip that changed nothing would repeat every frame and lock the screen.

**Spent fillers.** Spending a filler sets `is_filler` directly at 0x80180dbc without calling
either funnel. A hook on the next instruction (0x80180dc4, the start of the filler-count
decrement) records the cell from the UI mode (`r31+0x14`) and the cell in `r18`.

Recording is idempotent: setting a bit already set does nothing, so hooks that fire again on a
later pass are harmless. A new bit shows "Check recorded" when Messages -> Local -> Checks is on
(off by default) and re-evaluates the goals. Nothing on this path writes the memory card.

## Backfill from the client

The client sends checks the server already holds - a fresh save, a slot taken over, or another
player's collect - through `APData.client_backfill[CHECKLIST_MODE_NUM][2]`. It fills the words,
then sets `APData.backfill_valid`. `OnFrameStart` consumes only while the flag is set: the
checklist backfill, then the AP Patch backfill that shares the flag, each zeroing its array,
then clears the flag. A 64-bit store is not atomic on the GameCube, and these words are consumed
once, so a half-written word would apply the bits that had landed and zero the rest for good;
the `u32` flag cannot tear, and clearing it last stops the client (which waits for zero) writing
into an array mid-consume.

For each newly reported cell the mod sets the `sent_checks` bit, sets `is_unlocked` and
`is_visible` on the cell so the board shows it at once, and sets `has_reward` when a received
reward is placed there. It re-evaluates the goals once if anything was new. Cells on the
Archipelago row get only the bit when the tab never registered.

So a collect from another world can complete a goal with no input from the player, as standard
Archipelago behaviour expects. A release from another world arrives as items through the normal
item mailbox and touches no check.

## When the card is written

`Hoshi_WriteSave` mounts the card and rewrites hoshi's whole file synchronously, stalling the
frame, and checks are recorded mid-play, some from per-frame procs. So no detection path writes
the card. `APSave` is updated in RAM at once, and hoshi flushes it at every point the game
requests its own save - it hooks each call site of `Memcard_ReqSave` (0x80078990): results,
stage selects, checklist unlocks, menu entry - skipping the write when nothing changed. Writing
at the same moments as the game's save keeps the two records above from drifting apart.

The debug commands that clear or force checks, set or complete goals, reapply options or reset
progression write the card immediately. Placements and slot options write nothing, since the
client resends both on every connect.

## Lifecycle

`APChecks_OnBoot` installs the two funnel replacements, the five meta hooks and the filler hook.
`APChecks_OnSaveLoaded` copies `sent_checks` to the client mirror and evaluates the goals once,
which publishes the goal state from the saved latch.
