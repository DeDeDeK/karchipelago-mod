# Archipelago Checklist Tab

The archipelago mod adds a fourth checklist, the blue "Archipelago" tab after City Trial, built
on the `custom_checklist` framework. Its 120 cells are objectives the mod detects itself, each an
Archipelago location; completing one sends a check exactly as a vanilla checkbox does, and the
seed can place items, including vanilla checklist rewards, on its cells. The framework draws the
tab and polls its predicates every frame; this mod supplies the objectives, decides where a
completion is recorded, and gives the tab a row of its own in the save and on the client wire.
Without `custom_checklist` in the build the tab does not exist and its row is skipped
everywhere.

## Two identities

The tab has a fixed **row** and a run-time **mode**:

- **Row.** Every per-checklist array in `APData`, `APSlotOptions` and `APSave` is
  `CHECKLIST_MODE_NUM` (4) wide, and the tab is always `AP_CHECKLIST_ROW` (3), right after the
  three game modes. Row-wide bitmasks (`APData.goal_satisfied_mask`, `APSave.goal_latched`) give
  it bit 3. The row is what the client and the save store.
- **Mode.** The framework hands each registered tab the next mode index from `GMMODE_NUM` up, so
  the tab's mode is whatever it was assigned. `APChecklist_Register` stores it in
  `ap_checklist_mode`; engine calls such as `gmGetClearcheckerTypeP` take it.

`ChecklistModeRow(mode)` maps a mode to its row (-1 for another mod's tab) and
`ChecklistRowMode(row)` maps back; code asks them rather than assuming the two are equal, so
registration order across mods does not matter. Reward tables stay `GMMODE_NUM` wide, since the
tab has no rewards of its own. `APChecklist_IsRegistered()` says whether the tab exists, and
everything that touches its clear data checks it first: without the framework, the vanilla
accessor asserts on any mode past City Trial.

## What the mod registers

`APChecklist_Register` runs from `OnSaveLoaded`, after every mod's `OnBoot` (the framework boots
after this one), imports `CustomChecklistAPI` and registers a descriptor:

| Field | Value |
|---|---|
| `name` | `AP_CHECKLIST_NAME`, "Archipelago" |
| `theme` | `AP_THEME_COLOR`, blue |
| `tex_file` | `ApChecklistTex`, exporting `apBannerImg` and `apEmblemImg` |
| `checks` | All 120 cells, each predicate `APCheckDetect_IsSet(ck)` |
| `is_recorded` | The tab's `sent_checks` bit |
| `record_complete` | `ClearChecker_SetNewUnlock(ap_checklist_mode, ck)` |
| `is_ready` | `APData.game_ready` |

The cells are numbered by `APCheckKind`; the check table and `APCK_NUM` are pinned to 120 by
static asserts, so the tab always fills its grid and every row mask takes the same 120-bit
bound as the vanilla rows. `APChecklist_RowName(row)` and `APChecklist_RowColor(row)` give every
textbox that names a row its wording and tint; they return the tab's name and theme for row 3,
since its mode has no slot in the textbox's `ModeColors[]`.

## Recording a completion

The framework calls `record_complete` the first time a predicate holds. That routes through
`ClearChecker_SetNewUnlock` with the tab's mode, which the mod has replaced: the replacement
records the row's `sent_checks` bit (and through it the client mirror and the goals) and sets
`is_new` on the tab's cell. The framework then sets `is_new` again itself, harmlessly. So a tab
completion takes the same path as any vanilla one, and the tab's `GameClearData` - served by the
framework through `gmGetClearcheckerTypeP(ap_checklist_mode)` - is the block both sides read.

`is_recorded` reads the saved bit, so a completion survives reboots, and a board that comes up
without the cell marked shows it complete with no replay.

## Labels

Each label is the cell's text and also the location's name in the apworld minus its leading
`Archipelago: `, the same relationship the vanilla tabs have to theirs. The apworld restates
every name by hand. Labels follow the vanilla wording: a title-case prefix (`City Trial: `,
`Stadium: `, `Air Ride: `, `Top Ride: `, or vanilla's `Time Attack: ` and `Free Run: ` for those
modes of either racing game), the stadium or course in capitals with no colon of its own, and a
closing `!`. Vanilla's Top Ride cells need no prefix because their tab names the mode; the
Archipelago tab mixes modes, so its Top Ride cells carry one.

Every label places its own line break with `\n`. The cell holds two lines and the engine squeezes
an over-wide line instead of wrapping, and the framework's fallback split balances on width alone,
which would part a stadium name from its number. Lines stay near the 25 characters vanilla renders
at full size and no wider than vanilla's widest unsqueezed line of 37, with a few exceptions that
restate vanilla's own longer cells. Every character used maps through `Text_CharToCommand`; an
unmapped one would be dropped silently.

## Revealing the board

The tab reveals outward from its completions like the vanilla boards. Since every one of the 120
cells has a check, no revealed box is ever an unreachable objective.

`APSlotOptions.reveal_checklists[row]` asks for a row to start fully revealed.
`ChecklistRewards_Reveal(row)` sets `is_visible` on all 120 cells of a game mode, or for
`AP_CHECKLIST_ROW` calls `APChecklist_RevealAll`, which goes through the framework's
`RevealAll` and is saved with the tab's board; neither touches unlock state.
`ChecklistRewards_RevealAll` does every row. The reveal is applied once the slot options arrive
and again on every boot after the tab registers, since a tab that found no save slot comes up on
a blank RAM board. The debug mod's Reveal Checklists page calls the same functions.

## On the wire

The client reads `APData` by field offset, restated by hand, so its layout is a contract. The tab
is an ordinary row of every per-row array - `APSlotOptions.reveal_checklists`, `goal`,
`checklist_amount`, `goal_checks`, and `APData.sent_checks` and `client_backfill` - never a field
appended at the end, and the client never sees the run-time mode. A reward placed on the tab is
encoded with row 3 in its placement's high byte. The tab's 120 cells are AP locations 361-480
(`361 + clear_kind`), and the AP Patch locations start right after them, at 481; the apworld
derives that base from the cell count, so the two blocks move together.

`APSave` is not part of the wire, but it is matched to a card block by mod-name hash and size
alone, so any layout change must also change `APSAVE_STAMP`, which reinitializes a block that
does not match. The tab's cross-round progress counters live in `APSave.checks`, an
`APCheckProgress` at the end of the save.

## Tab art

`ApChecklistTex.dat`, staged to the disc root, exports two `_HSD_ImageDesc` publics: `apBannerImg`,
an RGB5A3 248x128 panel with the Archipelago logo as a faint watermark that keeps the panel
opaque under the grid, and `apEmblemImg`, an I4 64x64 intensity map of the logo for the tab
indicator, which takes the blue tint. The framework loads it on each visit and swaps the banner
and emblem textures. The archive is generated from the Archipelago logo by an authoring script,
not edited by hand.
