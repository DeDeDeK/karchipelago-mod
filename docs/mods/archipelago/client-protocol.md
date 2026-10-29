# Client Protocol

The Python Archipelago client and the archipelago mod talk through one struct in game memory,
`APData`, which the client reads and writes with `dolphin-memory-engine` while the game runs in
Dolphin. Nothing else crosses: items, location checks, slot options, links and text all go
through fields of this struct. It is a contract - a field moved, an ID renumbered or a rule
changed on one side breaks the other - so this doc gives the layout exactly. The client's
offset table and `APData` / `APSlotOptions` / `APItemId` in the mod must agree.

## Finding the struct

`OnBoot` allocates `APData` with `HSD_MemAlloc` (an allocation made in `OnBoot` lasts the whole
run; one made anywhere else is freed at the next scene change), zeroes it, and stores its
address at `0x805d52d4` (`AP_DATA_ANCHOR`). That word is the `"ram\0"` tail of the `.sdata`
string `"pvparam"` at 0x805d52d0, which the game only ever reads as assert text, so overwriting
it is harmless.

The client polls the anchor until it holds a MEM1 address, then `game_ready` until it reads 1.
The mod sets `game_ready` once, in `OnSaveLoaded`, and never clears it, so a change of pointer or
`game_ready` falling back to 0 (the boot-time zeroing) means the game restarted and the client
starts the handshake over.

All values are big-endian. Aligned 32-bit reads and writes are atomic on the GameCube; 64-bit
ones are two stores, and a reader can see half of one. Every u64 field is shaped so a torn read
costs nothing: the client overwrites `energy_balance` every poll, read-and-diff fields
(`sent_checks`, `ap_patch_checks`) pick up a missed bit on the next poll, `goal_checks` is
gated by `options_valid`, and the two consume-once backfill arrays are published behind the u32
`backfill_valid`.

## Layout

Offsets are from the `APData` base. Field order is the contract; a new field shifts everything
after it.

| Offset | Type | Field | Writer | Meaning |
|---|---|---|---|---|
| 0x000 | s64 | `energy_balance` | Client | EnergyLink pool in whole MJ |
| 0x008 | u32 | `energy_deposit_total` | Game | MJ deposited this boot, rising |
| 0x00C | u32 | `energy_withdraw_total` | Game | MJ withdrawn this boot, rising |
| 0x010 | u32 | `deathlink_receive` | Client | 1 = kill the local players; game clears |
| 0x014 | u32 | `deathlink_send` | Game | 1 = a human died; client clears |
| 0x018 | u32 | `traplink_receive` | Client | 1 = apply a trap; game clears |
| 0x01C | u32 | `traplink_send` | Game | `TrapLinkKind` of a trap sprung here; client clears |
| 0x020 | u32 | `incoming_item_id` | Client | Item mailbox; game clears |
| 0x024 | u32 | `item_received_index` | Game | Items taken from the mailbox, all time |
| 0x028 | u32 | `game_ready` | Game | 1 once the save is loaded |
| 0x02C | u32 | `options_valid` | Both | Client sets after writing `options`; game clears as the ack |
| 0x030 | `APSlotOptions` | `options` | Client | Slot options, 0x030-0x0F7 |
| 0x0F8 | u32 | `location_data_valid` | Client | Set after writing `locations`; game clears |
| 0x0FC | u16[3][46] | `locations` | Client | Where this slot's checklist rewards sit |
| 0x210 | u64[4][2] | `sent_checks` | Game | Completed checkboxes per checklist row |
| 0x250 | u64[4][2] | `client_backfill` | Client | Checks the server knows; game ORs in and clears |
| 0x290 | u8 | `goal_complete` | Game | 1 once every row with a goal has met it |
| 0x291 | u8 | `goal_satisfied_mask` | Game | Bit r = row r's goal met |
| 0x294 | u32 | `deathlink_menu_enabled` | Game | Death Link setting |
| 0x298 | u32 | `energylink_menu_enabled` | Game | Energy Link setting |
| 0x29C | u32 | `traplink_menu_enabled` | Game | Trap Link setting |
| 0x2A0 | u32 | `text_pending` | Both | Text mailbox flag |
| 0x2A4 | u32 | `text_menu_mask` | Game | Bit `1 << APTextKind` = that kind shown |
| 0x2A8 | `APTextMessage` | `text_msg` | Client | One 256-byte message |
| 0x3A8 | u64[8] | `ap_patch_checks` | Game | AP Patches collected |
| 0x3E8 | u64[8] | `ap_patch_backfill` | Client | AP Patches the server knows; game ORs in and clears |
| 0x428 | u32 | `backfill_valid` | Client | Both backfill arrays written; game clears last |

The struct ends at 0x42C, padded to 0x430 by its u64 alignment.

A **checklist row** is one of the four checklists the mod records: 0 Air Ride, 1 Top Ride,
2 City Trial (the vanilla `GameMode` order) and 3 the Archipelago tab. A checkbox is a
`clear_kind`, 0-119 within its row, stored as bit `k % 64` of word `k / 64`.

## Protocol rules

Every field has one writer. A **flag** or **mailbox** (`deathlink_*`, `traplink_*`,
`incoming_item_id`, `text_pending`) is set non-zero by its writer, acted on and zeroed by the
reader, and the writer waits for zero before writing again. Three writers skip the wait on
purpose:

- the client writes `deathlink_receive = 1` on every incoming DeathLink, so deaths that arrive
  together collapse into one kill;
- the game writes `deathlink_send` and `traplink_send` the same way, so several sends inside
  one client poll go out as one Bounce (for TrapLink, the last kind written).

`energy_deposit_total` and `energy_withdraw_total` are not mailboxes but rising counters: the
game only adds, the client only reads and diffs, and neither side clears them. They reset to 0
at each boot.

The menu mirrors (0x294-0x2A4) are game-owned. The game republishes them at `OnSaveLoaded`,
from each toggle's `on_change`, and on every options write just before it clears
`options_valid`. They are the live state of the player's settings; the `*_link_enabled` slot
options only seed the settings once.

## Handshake

1. Wait for the anchor and `game_ready == 1`.
2. Wait for `incoming_item_id == 0`. An item still in the mailbox is not yet counted in
   `item_received_index`, so reading the index first would deliver it twice.
3. Write every `APSlotOptions` field, set `options_valid = 1`, and read `item_received_index`:
   items below it are already in the game.
4. Wait for the game to clear `options_valid`. Until then the menu mirrors still hold the
   values the save booted with, and a client that read them early would see the slot's links
   as switched off and drop their tags.
5. Write `locations`, set `location_data_valid = 1`.
6. Poll: deliver items, diff checks, forward the goal, read the menu mirrors, run the links,
   push backfill, flush text. The client polls every 0.1 s.

The client writes options and locations on every connection. The game copies the options into
`APSave` only on the first write a save ever takes (`options_received`), seeding the three link
settings and pre-filling ungated categories then; later writes are acknowledged and otherwise
ignored. Locations are re-applied every time. Neither write touches the memory card - both reach
it with the game's next own save, and the next connection resends them anyway.

A save belongs to one seed and one build: nothing records which seed the options came from.
The client reads every slot_data key it writes without a default, so slot_data from another
apworld version fails loudly rather than being patched up.

`text_pending` is not reset by the handshake. A client reconnecting to a game that never
rebooted reads it before writing a message.

## Slot options

`APSlotOptions` sits at 0x030. Fields are u32 unless noted; per-row arrays have four entries in
checklist-row order. Offsets below are from the `APData` base.

| Offset | Field | Values |
|---|---|---|
| 0x030 | `death_link_enabled` | 0/1, seeds the Death Link setting |
| 0x034 | `energy_link_enabled` | 0/1, seeds the Energy Link setting |
| 0x038 | `trap_link_enabled` | 0/1, seeds the Trap Link setting |
| 0x03C | `reveal_checklists[4]` | 0/1, every square of that row starts revealed (visual only) |
| 0x04C | `goal[4]` | `APGoalKind` per row |
| 0x05C | `checklist_amount[4]` | 1-120, the count for `GOAL_N_CHECKLIST` |
| 0x06C | `city_trial_patch_cap_min` | Patch cap the player starts at (apworld 1-18) |
| 0x070 | `city_trial_patch_cap_max` | Patch cap ceiling (apworld 18-30) |
| 0x074 | `spawn_rate_min` | Starting item spawn rate, percent (apworld 10-100) |
| 0x078 | u64 `goal_checks[4][2]` | Checkboxes `GOAL_CHECKLIST_LIST` requires per row |
| 0x0B8 | `machine_gating_enabled` | 1 = the category holds unlock items |
| 0x0BC | `ability_gating_enabled` | copy abilities |
| 0x0C0 | `event_gating_enabled` | City Trial events |
| 0x0C4 | `patch_gating_enabled` | patch types |
| 0x0C8 | `item_gating_enabled` | City Trial items, and the Archipelago Star spheres |
| 0x0CC | `box_gating_enabled` | box colors |
| 0x0D0 | `airride_stage_gating_enabled` | Air Ride courses |
| 0x0D4 | `topride_stage_gating_enabled` | Top Ride courses |
| 0x0D8 | `topride_item_gating_enabled` | Top Ride items |
| 0x0DC | `color_gating_enabled` | Kirby colors |
| 0x0E0 | `stadium_gating_enabled` | City Trial stadiums |
| 0x0E4 | `base_ability_gating_enabled` | inhale, quick spin, charge |
| 0x0E8 | `checklist_reward_placed_types` | Bit `mode * 9 + RewardType` = that reward type was placed as items |
| 0x0EC | `goal_forced_gates` | `GOALGATE_*` bits |
| 0x0F0 | `ap_patches` | AP Patch locations in the seed, 0 = off (mod accepts up to 512, apworld up to 200) |

The struct is 8-byte aligned, so 4 bytes of padding end it at 0x0F8.

The gating flags follow `APUnlockCategory` order. The apworld sends 1 only for a category that
really holds unlock items this seed; a 0 tells the mod to fill that category's unlock mask on
the first options write, since no unlock item for it will ever arrive. The Archipelago Star
spheres have no flag of their own and follow `item_gating_enabled`.

`goal_forced_gates` holds unlocks back from that fill. A few goals are one feat that a filled
category would hand over at once - the six legendary pieces, the Vs. King Dedede stadium, the
six spheres - so with their category ungated the apworld still ships just those unlocks and sets
the bit:

| Bit | Name | Kept locked |
|---|---|---|
| 0x1 | `GOALGATE_LEGENDARY_PIECES` | `ITUNLOCK_HYDRA1-3`, `ITUNLOCK_DRAGOON1-3` |
| 0x2 | `GOALGATE_VS_KING_DEDEDE` | `STKIND_VSKINGDEDEDE` |
| 0x4 | `GOALGATE_AP_STAR_PIECES` | every `APStarPiece` |

`checklist_reward_placed_types` covers the `RewardType`s no gate mask owns: `REWARD_FILLER` (0),
`REWARD_BONUS_MOVIE` (1), `REWARD_EXTRA_RULE` (2), `REWARD_SOUND_TEST` (4), `REWARD_MUSIC` (5),
`REWARD_ENDING` (6) and `REWARD_PAUSE_POWERUPS` (8), nine bits per vanilla mode. On the first
options write the mod marks every reward whose (mode, type) bit is clear as received, so a
reward the seed did not place is simply owned - a mode left out of the seed ships no bits and
all its rewards unlock outright.

The patch cap fields read as vanilla's 18 until the options arrive, as 18 when 0, and are
clamped to 127 (`PATCH_STAT_MAX`). `spawn_rate_min` reads as 100% until then and is floored at
10.

### Goal kinds

One enum for all four rows; each row's apworld option uses the same values.

| Value | `APGoalKind` | apworld choice | Rows |
|---|---|---|---|
| 0 | `GOAL_100_CHECKLIST` | `100_checklist_blocks` | Air Ride, Top Ride, City Trial |
| 1 | `GOAL_N_CHECKLIST` | `n_checklist_blocks` | all |
| 2 | `GOAL_CHECKLIST_LIST` | `checklist_list` | all |
| 3 | `GOAL_HYDRA_AND_DRAGOON` | `hydra_and_dragoon` | City Trial |
| 4 | `GOAL_BEAT_KING_DEDEDE` | `beat_king_dedede` | City Trial |
| 5 | `GOAL_MAX_STATS_CT` | `max_stats_in_one_run` | City Trial |
| 6 | `GOAL_ASSEMBLE_AP_STAR` | `assemble_archipelago_star` | Archipelago |
| 7 | `GOAL_ALL_LEGENDARIES_CT` | `all_three_legendaries_in_one_run` | Archipelago |
| 8 | `GOAL_NONE` | `none` | all |

`goal_checks` rows sit at 0x078, 0x088, 0x098 and 0x0A8; the client builds each from that row's
`*_goal_locations` and zero-fills a row whose goal is not `GOAL_CHECKLIST_LIST`.

The client fills each field from the slot_data key of the same meaning: `death_link` and the
other links, `<mode>_goal`, `<mode>_checklist_amount`, `<mode>_reveal_checklist` and
`<mode>_goal_locations` per row, the apworld's `<category>_gated` options for the gating flags
(`machines_gated`, `city_trial_items_gated`, `top_ride_courses_gated`, ...), `checklist_rewards`
for the placed reward types, and the three `*_goal_gated` keys for `goal_forced_gates`.

## Item IDs

`APItemId` in the mod's public header is the authoritative list; the apworld's IDs must match
it. ID 0 is the empty mailbox and never an item. Ranges are aligned to a game enum where one
exists, so the ID minus the base is the enum value.

| IDs | Base | Items |
|---|---|---|
| 1-4 | - | Checkbox filler for Air Ride, Top Ride, City Trial, the Archipelago tab |
| 5 | - | Patch Cap Increase |
| 6, 9, 13 | - | 1 HP trap, All Down, Drop Patches trap |
| 7, 8 | - | All Up (this round), Permanent All Up |
| 10, 11, 14 | - | Give Dragoon, Give Hydra, Give Archipelago Star (assembled) |
| 12 | - | Spawn Rate Up |
| 100-108 | `PatchKind` | Permanent +1 patch |
| 200-215 | `EventKind` | Start a City Trial event |
| 300-368 | `ItemKind` | Give a City Trial item (boxes, patches, copy abilities, food, pieces, fakes) |
| 400-423 | `StadiumKind` | Stadium unlock |
| 500-643 | mode * 50 + index | Checklist reward |
| 700-715 | `EventKind` | Event unlock |
| 760-770 | `CopyKind` | Copy ability unlock |
| 771-773 | `BaseAbilityKind` | Inhale, quick spin, charge unlock |
| 780-788 | `PatchKind` | Patch type unlock |
| 790-819 | `ItemUnlockKind` | City Trial item unlock |
| 820-825 | `APStarPiece` | Archipelago Star sphere unlock |
| 830-856 | machine mask bit | Machine unlock |
| 860-862 | `BoxKind` | Box color unlock |
| 870-878 | `AirRideCourse` | Air Ride course unlock |
| 880-887 | `KirbyColor` | Kirby color unlock |
| 890-896 | `TopRideCourse` | Top Ride course unlock |
| 900-921 | `TopRideItemKind` | Top Ride item unlock |
| 950-971 | `TopRideItemKind` | Give a Top Ride item |
| 972, 973 | - | Big Kirby, Small Kirby |
| 980-985 | `APStarPiece` | Give an Archipelago Star sphere |

Gaps the apworld never sends, which the mod drops or ignores:

- **Machines.** IDs are 830 + mask bit: the vanilla `MachineKind`s, then 856 for the
  Archipelago Star (bit 26). 855 (`VCKIND_WHEELVSDEDEDE`, King Dedede's stadium machine) is
  rejected. 847, 849 and 850 (`WINGKIRBY`, `WHEELNORMAL`, `WHEELKIRBY`, transformation forms) are
  accepted but never shipped. 854 is King Dedede and 848 Meta Knight.
- **Top Ride items.** 912 is the engine's duplicate Party Ball (`TRITEM_PARTY_BALL_ALT`); only
  921 ships, and the mod mirrors it onto 912's bit.
- **Checklist rewards.** Each mode's band is 50 wide but holds 46 (Air Ride), 33 (Top Ride) or
  44 (City Trial) rewards; an ID in the unused tail is dropped.

The checklist reward index is the apworld's order: rewards sorted by their native `clear_kind`,
the same order as the `AP_REWARD_*` enum. The game's own reward table is in a different order,
and the mod translates at the wire (`ChecklistRewards_ApToGameIndex`); the client never sees the
game's order.

## Locations

Every checkbox of every row is an AP location, and so is every AP Patch. A location code is
arithmetic:

| Locations | Codes |
|---|---|
| City Trial row | 1 + `clear_kind` (1-120) |
| Air Ride row | 121 + `clear_kind` (121-240) |
| Top Ride row | 241 + `clear_kind` (241-360) |
| Archipelago row | 361 + `clear_kind` (361-480) |
| AP Patch n | 481 + n |

### Reward placement

`locations[source_mode][reward_index]` tells the game where this slot's own checklist rewards
(items 500-643) sit, so the checklist can draw their icons. Rows are the three vanilla modes,
padded to 46 entries.

| Value | Meaning |
|---|---|
| `0xFFFF` | Not placed on any of this slot's cells (or an unused entry) |
| `(target_row << 8) \| clear_kind` | On that cell. `target_row` is a checklist row, 0-3; a row other than the source mode is a cross-mode placement |

The client builds the table from a `LocationScouts` of every location the slot owns, keeping the
results whose item is one of this slot's rewards. Anything else placed on a local cell - a
filler, a trap, an unlock - is delivered through the item mailbox when the cell is checked, and
the cell shows no reward icon beforehand.

The game copies the table into `APSave.shuffled_rewards` and rebuilds its reward tables from it,
and does the same at every boot, so the checklist works before the client reconnects. A reward
unlocks only when its item arrives through the mailbox, never by completing the cell it sits
on.

### Checks

The mod owns `sent_checks`, the authoritative record of completed checkboxes, mirrored from
`APSave.sent_checks`. The client never reads the game's checklist state.

- **Each poll** the client diffs `sent_checks` and `ap_patch_checks` against what it last saw
  and sends every new bit as a location check.
- **On connect** it starts from zero, so every set bit is resent (the server ignores repeats).
- **Backfill.** Checks the server has that the game lacks - a fresh save, another client on the
  slot, `!collect` - go the other way. The client waits for `backfill_valid == 0`, writes the
  missing bits into `client_backfill` and `ap_patch_backfill`, then sets `backfill_valid`. On
  its next `OnFrameStart` the game ORs both into the save, marks the cells complete and visible,
  badges a cell whose reward has arrived, re-evaluates the goals, zeroes both arrays and clears
  `backfill_valid` last.

`goal_complete` and `goal_satisfied_mask` are latched in `APSave.goal_latched`, so they survive
reboots and never fall back. Both stay 0 until the slot options arrive. The client sends victory
when `goal_complete` reads 1, and feeds `goal_satisfied_mask` to Universal Tracker so it knows
which row goals are already done.

## Links

**DeathLink.** The client sends a Bounce when `deathlink_send` reads 1 and clears it, and
writes `deathlink_receive = 1` for each incoming DeathLink while attached to the game; bounces
during a disconnect are dropped, not queued.

**TrapLink.** `traplink_send` carries a `TrapLinkKind`; the client turns it into a Bounce with
that `trap_name`, falling back to `"Trap"` for a kind it does not know.

| Value | `TrapLinkKind` | `trap_name` |
|---|---|---|
| 0 | `TRAPLINK_KIND_NONE` | nothing pending |
| 1 | `TRAPLINK_KIND_BAD_PATCH` | `Bad Patch` |
| 2 | `TRAPLINK_KIND_SLEEP` | `Sleep` |
| 3 | `TRAPLINK_KIND_SPEED_DOWN` | `Speed Down` |

An incoming TrapLink's `trap_name` is ignored: the client counts pending traps and writes
`traplink_receive = 1` for each one whenever the field reads 0, and the game picks the trap.

**EnergyLink.** The server pool is in joules; the wire is whole MJ (1,000,000 J). Each poll,
while `energylink_menu_enabled` reads 1, the client:

1. computes `cur = energy_deposit_total - energy_withdraw_total` and the change since its
   watermark, which it seeds on attaching and never persists (the counters restart at 0 each
   boot);
2. sends a positive change as a `Set` add, and a negative one as a tagged `Set` with
   `[add, max 0]` and `want_reply`, logging a reply that shows the pool could not cover it;
3. folds the change into its cached pool value, then writes the pool, in MJ, to
   `energy_balance`.

Sending before writing the balance is what keeps a purchase from bouncing the game's balance
back up to the pre-purchase pool for a poll or two.

## Text

`text_msg` is a single-slot mailbox with the item mailbox's handshake: the client writes the
whole record, then sets `text_pending = 1`, and writes nothing while the flag is set; the game
renders and clears it. The game holds a pending message while no screen canvas exists
(`*stc_textcanvas_first` is NULL, as during a scene load), so a load delays a message rather
than losing it. With the textbox mod absent the message is consumed and dropped.

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0x00 | u8 | `kind` | `APTextKind`: 0 check, 1 item, 2 hint, 3 status, 4 chat, 5 link |
| 0x01 | u8 | `seg_count` | 1-8 colored runs |
| 0x02 | u8[8] | `colors` | `APTextColor` per run |
| 0x0A | u8[2] | `pad` | |
| 0x0C | char[244] | `text` | `seg_count` NUL-terminated strings back to back |

`APTextColor` is 0 for the text box's default, then Archipelago's twelve GUI colors in order:
black, red, green, yellow, blue, magenta, cyan, white, orange, slateblue, plum, salmon. The game
drops a message whose `kind` or `seg_count` is out of range, and renders it not at all when the
player has turned its kind off; `text_menu_mask` lets the client skip composing those.

## Invariants

- `item_received_index` counts receipt, not application. It rises the moment the game takes an
  item from the mailbox, and the client uses it only to resume sending.
- Items can apply out of order and slowly: an item that cannot apply yet waits while the ones
  behind it go ahead, and at most one resolves per frame.
- The game's queue holds 512 items (`MAX_RECEIVED_ITEMS`), and EnergyLink purchases share it.
  When it is full the game leaves the item in the mailbox, which stalls the client safely.
- The queue and `item_received_count` are in the save, so items received but not applied
  survive a reboot.
- Text is advisory. Nothing in the game depends on a message arriving.
