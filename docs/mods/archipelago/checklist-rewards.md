# Checklist Rewards

In a seed, every vanilla checklist reward - 46 in Air Ride, 33 in Top Ride, 44 in City Trial -
is an Archipelago item, and every checkbox is a location. A reward can come from another world
at any time, and the seed may place it on any cell: its own, one in another mode's checklist,
one on the Archipelago tab, or none at all. The mod makes receipt the only thing that unlocks a
reward, shows each reward on the cell the seed chose, and keeps the vanilla code that assumes a
reward sits on its native cell from misreading the shuffle.

## Two reward numberings

The game names a reward by its row in the mode's reward table (`stc_reward_table_ptrs[mode]`),
a fixed ROM order. The apworld numbers each mode's rewards by ascending native `clear_kind`, the
order the checkboxes appear, which is also the order of the reward IDs in `APItemId`
(`AP_REWARD_AR_COURSE_NEBULA_BELT` first). Everything inside
the mod - `shuffled_rewards`, `received_checklist_rewards`, the audio and stadium lookups, the
debug API - uses the game index, because several hardcoded vanilla tables are parallel to the
ROM order and cannot be reordered.

At boot, while the table still holds native clear_kinds, the mod ranks each mode's rows into a
permutation, and `ChecklistRewards_ApToGameIndex(mode, ap_ri)` applies it. It is used at exactly
the two places the client's numbering enters:

- **Item IDs.** A checklist reward arrives as `AP_CHECKLIST_REWARD_BASE (500) +
  mode * AP_CHECKLIST_REWARD_STRIDE (50) + ap_reward_index`; IDs in a stride's unused tail are
  dropped.
- **Placements.** The client writes `APData.locations[mode][ap_reward_index]`.

## Placement

A placement is a `u16`: `(target_row << 8) | clear_kind` for a reward on a local cell, `0xFFFF`
for one placed in another world. The high byte is a checklist row, not a runtime mode: rows 0-2
are the three game modes and `AP_CHECKLIST_ROW` (3) the Archipelago tab, whose real mode number
is assigned at run time. `APSave.shuffled_rewards[GMMODE_NUM][REWARD_COUNT_MAX]` holds one per
game reward index. Zero is a valid placement (Air Ride cell 0), so every save starts the array
filled with `0xFFFF`.

The client publishes placements by filling `locations[]` and setting `location_data_valid`.
`ChecklistRewards_ApplyLocations` runs from `OnFrameStart` while the flag is set: it copies each
entry to its game index, rebuilds the reward tables, regrants every received reward so rewards
received before the placements land on their cells, and clears the flag. It writes no card,
since the client resends placements on every connect.

At boot the mod copies the three reward tables to the heap and repoints
`stc_reward_table_ptrs`, so it can rewrite each row's `clear_kind` from the placements:

- **Same-mode local** (target row is the reward's own mode): `clear_kind` is the target cell, and
  vanilla code handles it unchanged.
- **Cross-mode or remote**: `clear_kind` is 0, a harmless in-range sentinel, and a cross-mode
  reward is recorded in `cross_mode_slots[row][clear_kind]` as (source mode, reward index). The
  Archipelago tab has no rewards of its own, so everything on it is a cross-mode placement.

The vanilla paths that act on a reward's cell - the reward loop, the legendary-parts check, the
hover text and the preview lookup - are gated to same-mode local rows or replaced, so the
sentinel is not read as a real cell there. One read is left: `Checklist_ProcessUnlock`'s City
Trial stadium block (from 0x8017f128) tests each stadium reward's cell to write the game's own
stadium unlock and "new" flags, and for a shuffled stadium tests cell 0 instead. The stadium gate
answers availability from its own mask, so this reaches at most the vanilla flags.
`ChecklistRewards_ResolveCell(mode, ck)`
answers "which reward sits on this cell" for the replacements: `cross_mode_slots` first, then a
match of the full `u16` against the mode's own placements.

## Unlocking and granting

`ClearChecker_CheckUnlocked` (0x80049e24) is replaced with a test of
`APSave.received_checklist_rewards[mode]`, bit per game reward index. Receipt is the only
authority; there is deliberately no `has_reward` fallback, because completing the reward's
native checkbox also raises `has_reward`, which would unlock it before the server sends the
item. That leak would reach every reward a menu checks through this function - sound test,
music, endings, bonus movie, pause power-ups, Top Ride extra rules.

`ChecklistRewards_Grant(mode, reward_index, announce)` handles a received reward:

1. Sets its `received_checklist_rewards` bit.
2. Announces it when `announce` is set and Messages -> Local -> Items is on (off by default).
3. Routes gated categories into their gate masks: the machines, King Dedede
   (`VCKIND_WHEELDEDEDE`) and Meta Knight (`VCKIND_WINGMETAKNIGHT`), Dragoon and Hydra, the four
   Kirby colors, the three Top Ride items, Nebula Belt, and City Trial's stadium rewards (by
   reward index, since their `reward_param` is 0). Those categories' menus read the gate masks,
   not the checklist. The legendary parts are not routed: they are checklist markers, separate
   from the in-round piece gates.
4. Sets `has_reward` on the placed cell, which draws the reward badge. It never sets
   `is_unlocked`, which belongs to check detection.
5. For a filler reward on a real receipt only, grants one checkbox filler to the reward's own
   mode, whatever cell it was placed on.

`RegrantAllReceivedRewards` replays `Grant` quietly for every received bit on every save load
and after every placement update, restoring badges and gate masks; it must not grant fillers, or
the count would grow every boot. The four standalone filler items (`AP_ITEM_CHECKBOX_FILLER_AIRRIDE` to
`AP_ITEM_CHECKBOX_FILLER_ARCHIPELAGO`), which EnergyLink purchases and debug gives also use,
bypass `Grant`; the Archipelago one is dropped when the tab never registered.

The apworld places checklist rewards one reward type at a time, per mode, and reports which
(mode, type) pairs it placed in `APSlotOptions.checklist_reward_placed_types`, bit
`mode * 9 + reward_type`. On the first options transfer `ChecklistRewards_GrantUnplaced` sets
the received bit for every reward of an unplaced pair, with no textbox and no filler. Only the
types with no gate mask of their own are eligible - filler, bonus movie, extra rule, sound test,
music, ending, pause power-ups; gated types follow their own gating options, and the legendary
parts are progression and always stay in the pool. Keeping the mask per mode is what lets a mode
the seed disabled still unlock its content.

## Engine seams

| Site | Patch | What it does |
|---|---|---|
| 0x80049e24 `ClearChecker_CheckUnlocked` | `REPLACEFUNC` | Received bit only |
| 0x80049ec4 `ClearChecker_GetRewardFromClearKind` | `REPLACEFUNC` | Resolves the cell through `ResolveCell`; same unlocked-or-filler precondition as vanilla |
| 0x8017dfd8 | conditional hook | Reward loop in `Checklist_SetRewardFlagOnUnlocks` skips every row that is not a same-mode local placement |
| 0x8017e00c | `b 0x8017e064` | Branches over the loop's filler grant, keeping the `has_reward` store before it |
| 0x8017e07c | hook after the loop | Sets `has_reward` on the current row's cross-mode cells that are `is_unlocked` or `is_filler` |
| 0x8017f044, 0x8017f0b4 | conditional hooks | "All Dragoon / Hydra parts" in `Checklist_ProcessUnlock` reads the received bits of the three parts, not their cells |
| 0x80182378, 0x8018238c, 0x801823a0 | `nop` | The per-mode SIS loads in `Checklist_Init` |
| 0x801823c4 | hook | Loads the current mode's SIS into slot 0 and the other two into slots 1 and 2 |
| 0x80181ee4 | hook, exits 0x80181f5c | Replaces the hover's reward scan; resolves the cell and records the hovered reward's source mode |
| 0x8018201c | hook | Reward text from the source mode's SIS slot, then `sis_id` back to 0 |
| 0x80181f8c | hook | Blank reward line with `sis_id` reset to 0 |
| 0x80182170 | hook | Hands `ClearChecker_GetRewardType` the source mode |
| 0x8018213c | `REPLACECALL` | Reward icon gate reads the source mode's `reward_param`; 0 when none resolved |
| 0x80180508 | hook, exits 0x80180560 | Audio preview scans the source mode's `stc_audio_preview_tables` |

The loop's own filler grant is disabled because it fires on cell completion and on the
`has_reward` that `Grant` writes, either of which would mint a filler the server did not send.
The legendary-parts hooks exist because a part placed elsewhere leaves the sentinel 0 as its
cell, so vanilla would test City Trial cell 0 instead: never assembling, or assembling off an
unrelated reward. On "all received" they fall through to vanilla's own store, which check
detection hooks to send cells 109 and 110.

## Cross-mode display

The hover hook resolves the cell under the cursor. It shows a reward once its bit is received;
before that, a same-mode reward shows once the cell has `has_reward`, and a cross-mode one once
it has `is_unlocked` or `has_reward` (the post-loop hook may not have mirrored the badge yet that
frame). It stores the reward's source mode, which the text, icon and audio hooks downstream read,
since a cross-mode reward index means nothing in the shown mode's table. The objective text still
comes from the shown mode's SIS, which is right: the objective belongs to the cell. Every
checklist SIS archive shares one font, so the text renders with slot 0's glyphs whatever slot its
commands came from. An ending reward needs no redirect: its preview only sets a UI state.

## Lifecycle

- **Boot.** `ChecklistRewards_OnBoot` copies the reward tables and builds the index permutation
  first, then installs the replacements and hooks. It runs before check detection's boot, since
  both patch `Checklist_ProcessUnlock`.
- **Save init.** `ChecklistRewards_OnSaveInit` fills `shuffled_rewards` with `0xFFFF` whenever the
  save block is (re)initialized.
- **Save load.** `ChecklistRewards_OnSaveLoaded` rebuilds the tables from `shuffled_rewards` and
  regrants. `APChecklist_Register` runs before it: marking a reward on an Archipelago-tab cell goes
  through `gmGetClearcheckerTypeP`, and the tab's clear data exists only once registered. Without
  `custom_checklist` in the build, cells on the Archipelago row are skipped, which also keeps
  vanilla's `mode >= 3` assert unreached.
