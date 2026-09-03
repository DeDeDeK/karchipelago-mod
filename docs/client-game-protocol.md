# Client-Game Protocol

The shared-memory wire contract between the Python Archipelago client and the game mod. The client drives it with `dolphin-memory-engine` while the mod runs in Dolphin. The mod side lives in `mods/archipelago/src/main.c` (struct, handshake, per-frame poll), `ap_item_handler.c` (item application), `check_detection.c` (location sends, goal evaluation), and `deathlink.c` / `energylink.c` / `traplink.c`.

## Shared Memory Access

`OnBoot` allocates the `APData` struct and stores its pointer at the static address `0x805d52d4`; the client reads that pointer to locate the struct. The allocation has to happen in `OnBoot` specifically - `HSD_MemAlloc` there persists for the whole runtime, while anywhere else it would be freed at the next scene change.

## APData Layout

Offsets are relative to the struct base and are pinned by `_Static_assert(offsetof(APData, ...))` lines in `main.c`. Adding a field shifts everything after it, so those asserts and the client's offset table move together; the field order in `APData` / `APSlotOptions` (`mods/archipelago/src/main.h`) is the canonical reference.

All 32-bit fields are 4-byte aligned and atomic on PPC at that alignment. The 64-bit fields (`energy_balance`, `energy_sent_total`, `sent_checks`, `client_backfill`, `goal_checks`) are NOT atomic on PPC32 - a reader may observe a torn value mid-write. For `energy_sent_total` this is self-correcting: the client reads-and-diffs a cumulative counter, so a torn read only skews one poll's delta and sets `last_seen` to whatever it read; the next poll's diff compensates exactly. Per-frame deltas are far below 2^31, bounding the magnitude of any single torn read.

### Communication Fields

| Offset | Type   | Field                | Writer       | Reader/Clearer |
|--------|--------|----------------------|--------------|----------------|
| 0x000  | s64    | `energy_balance`     | Client       | Game (reads; may locally adjust for purchase UI - client write is authoritative) |
| 0x008  | s64    | `energy_sent_total`  | Game         | Client (read-and-diff; NEVER writes) |
| 0x010  | u32    | `deathlink_receive`  | Client       | Game (clear to 0) |
| 0x014  | u32    | `deathlink_send`     | Game         | Client (clear to 0) |
| 0x018  | u32    | `traplink_receive`   | Client       | Game (clear to 0) |
| 0x01C  | u32    | `traplink_send`      | Game         | Client (clear to 0); value is a `TrapLinkKind` enum |
| 0x020  | u32    | `incoming_item_id`   | Client       | Game (clear to 0) |
| 0x024  | u32    | `item_received_index`| Game         | Client (read-only) |

`energy_balance` and `energy_sent_total` are denominated in raw MJ units (1 raw unit = 1 MJ in the AP pool, by the client-side `ENERGY_LINK_EXCHANGE_RATE = 1_000_000` convention). The fields are signed 64-bit so the mod can faithfully mirror multiworld pools that exceed u64 *joules* (i.e. > ~1.8x10^19 J), which translates to ~1.8x10^13 raw MJ on the mod side. `energy_sent_total` is *net* (deposits minus withdrawals) and resets to 0 each mod boot, so it stays far below even that figure.

### Handshake and Options Fields

| Offset | Type | Field | Writer | Reader | Description |
|--------|------|-------|--------|--------|-------------|
| 0x028  | u32  | `game_ready` | Game | Client | 1 when mod is fully initialized |
| 0x02C  | u32  | `options_valid` | Both | Both | Client sets 1 once every option is written; the game clears it as the transfer ack |
| 0x030  | APSlotOptions | `options` | Client | Game | Slot options block, 0x030-0x0F7 |

### Location Data Fields

| Offset | Type        | Field                 | Writer | Reader            | Description |
|--------|-------------|-----------------------|--------|-------------------|-------------|
| 0x0F8  | u32         | `location_data_valid` | Client | Game (clear to 0) | 1 after client has written `locations` |
| 0x0FC  | u16[3][46]  | `locations`           | Client | Game              | `locations[source_mode][source_reward_index]` = destination cell for this slot's checklist reward |

`locations` is indexed by **source reward** - the entry at `[m][i]` says where in the checklist grid this slot's reward `i` of mode `m` lives. Only the 3 real game modes are indexed (the AP checklist tab has no native rewards). Rows are padded to 46; meaningful entry counts are AR=46, TR=33, CT=44, and unused trailing entries should be `0xFFFF`.

| Value         | Meaning |
|---------------|---------|
| `0xFFFF`      | Remote (or unused slot). The reward is owned by another slot - no local cell displays it. |
| `(target_mode << 8) \| clear_kind` | Local placement. `target_mode` is the mode whose checklist holds this reward (0=AR, 1=TR, 2=CT); `clear_kind` is the cell index within that mode. `target_mode != source_mode` is a cross-mode placement. |

**`reward_index` convention (wire = clear_kind-sorted).** Both `locations[m][i]` and the checklist-reward item IDs (`500 + mode*50 + reward_index`) index rewards in clear_kind-sorted order - the order of the `AP_REWARD_*` enum in `mods/archipelago/include/archipelago_api.h` and of `checklist-mappings.csv`, which is the apworld's natural numbering. The game's internal reward table (`stc_reward_table_ptrs`) is in a different ROM order, so `ChecklistRewards_ApToGameIndex` (`checklist_rewards.c`) translates wire index to game index at both boundaries; it builds the permutation once by sorting each mode's table on native `clear_kind`. The client never needs the internal order.

`locations` carries only vanilla checklist rewards (AP item IDs `500..649`). Non-vanilla items placed on this slot's cells (fillers, traps, gating unlocks) are delivered through the standard `incoming_item_id` mailbox when the player checks the cell, so the checklist can only render a "this cell has a reward" star for vanilla placements - non-vanilla local placements look identical to empty cells until earned.

### Check Detection Fields

| Offset | Type | Field | Writer | Reader | Description |
|--------|------|-------|--------|--------|-------------|
| 0x210 | u64[4][2] | `sent_checks`     | Game   | Client | Bitmask of checkboxes the player has completed in gameplay or via filler. Bit `(k % 64)` of word `(k / 64)` for clear_kind `k`. Mirror of `APSave.sent_checks`. |
| 0x250 | u64[4][2] | `client_backfill` | Client | Game (clears) | Additive backfill: client writes bits for checks the AP server already knows about (fresh save, slot takeover, `!collect`). |
| 0x290 | u8        | `goal_complete`   | Game   | Client | Sticky once set. 1 when the active goal condition is satisfied. Persisted to `APSave.goal_complete`. |
| 0x291 | u8        | `goal_satisfied_mask` | Game | Client | Bit `r` = row `r`'s own goal satisfied; rows set to `GOAL_NONE` stay clear. Per-row sticky, since every goal reduces to sticky bits. Recomputed on every goal evaluation, including after `goal_complete` latches, so a save load repopulates it. Not persisted - it is derived from `APSave.sent_checks`. |

Both bitmasks are `CHECKLIST_MODE_NUM` (4) rows: rows 0-2 are Air Ride / Top Ride / City Trial, row 3 is the synthetic AP checklist tab.

### AP Patch Fields

| Offset | Type | Field | Writer | Reader | Description |
|--------|------|-------|--------|--------|-------------|
| 0x3A8 | u64[8] | `ap_patch_checks`   | Game   | Client        | Bit `i` of word `w` = AP Patch `w * 64 + i` collected, location code `413 + w * 64 + i`. Mirror of `APSave.ap_patch_collected`. |
| 0x3E8 | u64[8] | `ap_patch_backfill` | Client | Game (clears) | Additive backfill, the same protocol as `client_backfill`. |

`APData` ends at 0x428. AP Patches are one flat block rather than per-mode rows: they are City Trial content only, and they carry no checklist cell, so they never decode through the mode / clear_kind codec. The client reads-and-diffs `ap_patch_checks` every poll and reports new bits as ordinary location checks, which gives them the ordinary check line with no text work on either side.

Like the other u64 fields these are not written atomically, so a torn read is possible; the client only ever ORs newly-seen bits, so a torn read costs one poll.

### Menu Toggle State

Live mirror of the Settings menu toggles. Game-owned: client reads, never writes. `SyncMenuStateToAPData` (`settings_menu.c`) writes them on boot after save-restore, on the first-connect option transfer, and from each toggle's `on_change`.

| Offset | Type   | Field |
|--------|--------|-------|
| 0x294 | u32     | `deathlink_menu_enabled` |
| 0x298 | u32     | `energylink_menu_enabled` |
| 0x29C | u32     | `traplink_menu_enabled` |
| 0x2A4 | u32     | `text_menu_mask`, bit `1 << APTextKind` |

The link toggles are the authoritative current state, not `APSlotOptions.death_link_enabled` / `energy_link_enabled` / `trap_link_enabled` - those only set the *initial* values and are never updated by later toggles. The player can flip a toggle mid-session, so the client must diff all three against last-seen every poll and forward the change to the AP server (`ConnectUpdate` `tags` for DeathLink, the equivalent for TrapLink/EnergyLink), and read all three on connect.

The first of those reads must wait for `options_valid` to clear. The client writes the options and the game takes them in on its next frame, so until the ack lands the mirrors still hold the values the save booted with - and a client that diffs them straight after writing sees the slot's own links as "off", reads that as the player having turned them off in the menu, and drops the DeathLink and TrapLink tags until the mod corrects it a poll later. Any bounce in that window is lost.

`text_menu_mask` is an optimization, not a gate: the mod filters every message by kind as it renders, so the menu stays authoritative even against a stale client. Reading it keeps the client from composing lines the player has turned off, and from parking one in the mailbox.

### Text Fields

Client-authored text-box messages. The client composes the whole line - it owns every name the mod cannot know - and the mod only renders it.

| Offset | Type          | Field | Writer | Reader |
|--------|---------------|-------|--------|--------|
| 0x2A0  | u32           | `text_pending`  | Both   | Both |
| 0x2A4  | u32           | `text_menu_mask`| Game   | Client |
| 0x2A8  | APTextMessage | `text_msg`      | Client | Game |

There is no attachment flag or heartbeat. The game never asks whether a client is there: it renders whatever reaches the mailbox, and the lines it composes itself turn on their own Messages -> Local toggles, so nothing it prints depends on the answer. The client posts its own connect and disconnect lines as ordinary messages, which is the only place the distinction shows up in game.

`text_msg` is a single-slot mailbox with the same handshake as `incoming_item_id`: the client writes the body and only then sets `text_pending` to 1, and must not write while `text_pending` is non-zero; the game renders the message and clears the flag. The game holds a pending message while the text box has no screen canvas (scene loads), so a full mailbox is backpressure and the message survives the load rather than being dropped. One message per client poll is the ceiling, which is far above what the text box can retire - it shows at most 8 at a time for several seconds each - so the client keeps its own unbounded backlog rather than a shared ring.

`text_msg` is a fixed 256-byte `APTextMessage`:

| Offset | Type      | Field | Description |
|--------|-----------|-------|-------------|
| 0x00   | u8        | `kind`      | `APTextKind`: 0 check, 1 item, 2 hint, 3 status, 4 chat, 5 link |
| 0x01   | u8        | `seg_count` | 1..8 colored runs |
| 0x02   | u8[8]     | `colors`    | `APTextColor` per run |
| 0x0A   | u8[2]     | `pad`       | |
| 0x0C   | char[244] | `text`      | `seg_count` NUL-terminated strings, back to back |

`APTextColor` is 0 for the text box's own default plus Archipelago's twelve GUI color names in order: black, red, green, yellow, blue, magenta, cyan, white, orange, slateblue, plum, salmon.

A whole message is therefore at most **243 rendered characters** across at most 8 colored runs. That is deliberately more than any font size can show, because the mod owns the fit: around 103 characters fit on a line at Small, 77 at Med and 56 at Large, and the text box wraps onto three lines and truncates the remainder with `..`. Nothing is scaled down to fit.

## Protocol Rules

Almost every shared field follows one rule: **exactly one side writes, the other side reads and clears.** Aligned 32-bit reads and writes are atomic on the GameCube's PowerPC, so no locking is needed for 32-bit fields.

**Flag fields** (`deathlink_receive`, `deathlink_send`, `traplink_receive`, `traplink_send`) and **mailbox fields** (`incoming_item_id`): the writer sets a non-zero value, the reader acts on it and writes `0`, and the writer waits for `0` before writing again.

`deathlink_receive` is the exception to the wait: the client writes `1` on every incoming DeathLink bounce without waiting for the game to clear the previous one. Concurrent deaths collapse to a single kill - Kirby can't die-while-dying, so dropping the second event matches the observable game behavior.

`energy_sent_total` is not a mailbox at all but a **single-writer cumulative counter**. The game owns it and only ever adds or subtracts; the client only ever reads and diffs it, never writing and never clearing.

## Connection Handshake

The client and game must synchronize before data exchange:

1. **Wait for mod**: poll the pointer at `0x805d52d4` until non-zero. (`OnBoot` allocates the struct and stores it.)
2. **Wait for initialization**: poll `game_ready` (base + `0x028`) until `1`. The mod sets it in `OnSaveLoaded`, so this also guarantees `item_received_index` is valid.
3. **Write slot options**, then set `options_valid = 1`, and poll it until the game clears it back to 0. The link toggle mirrors are only the slot's once that ack lands.
4. **Read `item_received_index`** and skip all items with index < this value - the game already received them.
5. **Write `locations`**, then set `location_data_valid = 1`.
6. **Begin normal operation**: item delivery, deathlink/energylink/traplink polling, location checking, and text queue.

`text_pending` is not reset by the handshake. A client reconnecting to a game that never rebooted must read it rather than assume 0, or its first message overwrites one the mod has not rendered.

The game's `OnFrameStart` picks up `options_valid` (copying options into save data and setting the initial menu toggles) and `location_data_valid` (applying the placement table and persisting), clearing each as it consumes it. `options_valid` is cleared on every client write, including a reconnect's, even though the copy into save data happens only once - the clear is what tells the client the mirrors are current, so a reconnect must get one too.

The client should write options and location data on **every connection**. The game deduplicates: options are copied to persistent save data only on the first connection for a given save file, while location data is always re-applied.

## Slot Options

### APSlotOptions Layout

All fields are `u32` unless noted. Per-mode arrays are `CHECKLIST_MODE_NUM` (4) wide, indexed by checklist-mode row: 0=Air Ride, 1=Top Ride, 2=City Trial, 3=the synthetic AP checklist tab. Offsets are relative to the `APData` base.

| Offset | Field                             | Values | Description |
|--------|-----------------------------------|--------|-------------|
| 0x030 | `death_link_enabled`              | 0 or 1 | Sets initial deathlink menu toggle |
| 0x034 | `energy_link_enabled`             | 0 or 1 | Sets initial energylink menu toggle |
| 0x038 | `trap_link_enabled`               | 0 or 1 | Sets initial traplink menu toggle |
| 0x03C | `reveal_checklists[4]`            | 0 or 1 | Per row: reveal every square of that checklist from the start (visual only) |
| 0x04C | `goal[4]`                         | GoalKind | Completion condition per row |
| 0x05C | `checklist_amount[4]`             | 1-120  | N for GOAL_N_CHECKLIST per row |
| 0x06C | `city_trial_patch_cap_min`        | 1-127  | Per-stat patch cap the player starts at. Each Patch Cap Increase item adds +1. 0 is treated as the max. |
| 0x070 | `city_trial_patch_cap_max`        | 1-127  | Patch cap ceiling (also the threshold for GOAL_MAX_STATS_CT). AP world ships `max - min` Patch Cap Increase items so collecting all reaches it. `min == max` is a flat cap. 0 is treated as `PATCH_STAT_MAX` (127). |
| 0x074 | `spawn_rate_min`                  | 10-100 (percent) | Spawn rate floor for CT/TR items. 100 = vanilla, below 100 suppresses spawns. Each Spawn Rate Up item adds +10% on top, capped at 300. AP world ships `(max - min) / 10` items so collecting all reaches the configured max. 0 is treated as 100. |
| 0x078 | `goal_checks[4][2]`               | u64 bitmask | Required checkboxes per row for GOAL_CHECKLIST_LIST (64 bytes) |
| 0x0B8 | `machine_gating_enabled`          | 0 or 1 | See gating block below |
| 0x0BC | `ability_gating_enabled`          | 0 or 1 | Copy abilities |
| 0x0C0 | `event_gating_enabled`            | 0 or 1 | City Trial events |
| 0x0C4 | `patch_gating_enabled`            | 0 or 1 | Patch types |
| 0x0C8 | `item_gating_enabled`             | 0 or 1 | Item categories (All-Up, food, fireworks, ...) **and** the six Archipelago Star spheres |
| 0x0CC | `box_gating_enabled`              | 0 or 1 | Box types |
| 0x0D0 | `airride_stage_gating_enabled`    | 0 or 1 | Air Ride stages |
| 0x0D4 | `topride_stage_gating_enabled`    | 0 or 1 | Top Ride courses |
| 0x0D8 | `topride_item_gating_enabled`     | 0 or 1 | Top Ride items (ability-gated TR items remain gated by `ability_gating_enabled`) |
| 0x0DC | `color_gating_enabled`            | 0 or 1 | Kirby colors |
| 0x0E0 | `stadium_gating_enabled`          | 0 or 1 | City Trial stadiums |
| 0x0E4 | `base_ability_gating_enabled`     | 0 or 1 | Inhale / quick spin / charge |
| 0x0E8 | `checklist_reward_placed_types`   | bitmask | Checklist rewards placed as AP items, per (mode, type), see below |
| 0x0EC | `goal_forced_gates`               | bitmask | See below |
| 0x0F0 | `ap_patches`                      | 0-512  | AP Patch locations in this seed. 0 = the category is off and neither AP item is registered. The mod accepts and clamps to 512; the AP world's option tops out at 200, the width of its location block |

`APSlotOptions` holds `u64`s, so it is 8-byte aligned and 4 bytes of tail padding follow `ap_patches`. The block ends at 0x0F8 either way, which is what keeps every offset below it fixed.

Every `*_gating_enabled` field uses the same convention: `1` = gated (default), the AP world ships unlock items for that category; `0` = ungated, the mod pre-fills that category's unlock mask at connect (`APOptions_ApplyUngatedCategories` in `main.c`) and the AP world must not generate unlock items for it. Twelve of the thirteen `APUnlockCategory` masks have their own toggle; `AP_UNLOCK_AP_STAR_PIECE` has none and rides `item_gating_enabled`, since the AP world classifies the six spheres as City Trial item unlocks.

`checklist_reward_placed_types` is not a gate flag. It holds one bit per (mode, reward type) pair at index `mode * CHECKLIST_REWARD_MODE_BITS + reward_type`, with `CHECKLIST_REWARD_MODE_BITS` = 9: `RewardType` tops out at `REWARD_PAUSE_POWERUPS` (8), so the three reward-bearing modes pack into 27 bits. A set bit means the AP world placed that mode's rewards of that type as items. At connect the mod calls `ChecklistRewards_GrantUnplaced`, which marks every reward whose pair is clear as received and tracks the result in `received_checklist_rewards` rather than an unlock mask.

Only the seven reward types with no gate mask of their own can appear - `REWARD_FILLER` (0), `BONUS_MOVIE` (1), `EXTRA_RULE` (2), `SOUND_TEST` (4), `MUSIC` (5), `ENDING` (6), `PAUSE_POWERUPS` (8). The AP world builds the mask from the rewards it actually minted, so a mode the seed disabled ships no bits at all and the mod unlocks that mode's rewards outright - without that, its rewards would be neither placed nor granted. The 6 Dragoon/Hydra part markers are progression and are unaffected by the mask.

`APSlotOptions` is 8-byte aligned, so the block ends at 0x0F8 with `location_data_valid` immediately after.

### Goal-Forced Gates

Two City Trial goals are a single in-game feat rather than a checklist count, and each rests on one thing the category pre-fill would otherwise hand over at connect: `GOAL_HYDRA_AND_DRAGOON` needs the six legendary pieces to spawn, and `GOAL_BEAT_KING_DEDEDE` needs the Vs. King Dedede stadium to come up in the rotation. With `item_gating_enabled` / `stadium_gating_enabled` at 0 the seed would be winnable in the first match before a single item arrived, so the AP world keeps just those unlocks in the pool and sets the matching bit here; the rest of the category stays ungated.

| Bit | Name | Holds back |
|-----|------|------------|
| 0 | `GOALGATE_LEGENDARY_PIECES` | `ITUNLOCK_HYDRA1-3` / `ITUNLOCK_DRAGOON1-3` |
| 1 | `GOALGATE_VS_KING_DEDEDE` | `STKIND_VSKINGDEDEDE` |
| 2 | `GOALGATE_AP_STAR_PIECES` | every `APStarPiece` |

The field is only read in the ungated branch; a bit is 0 when its own category is gated, since that flag already keeps every bit locked. The unlock items themselves are delivered through the normal handler path, which never consults a gating flag.

### GoalKind Enum

| Value | Name                  | AP Option String       | Modes |
|-------|-----------------------|------------------------|-------|
| 0     | `GOAL_100_CHECKLIST`  | `100_checklist_blocks` | All   |
| 1     | `GOAL_N_CHECKLIST`    | `n_checklist_blocks`   | All   |
| 2     | `GOAL_HYDRA_AND_DRAGOON` | `hydra_and_dragoon` | City Trial only |
| 3     | `GOAL_BEAT_KING_DEDEDE`  | `beat_king_dedede`  | City Trial only |
| 4     | `GOAL_NONE`           | `none`                 | All   |
| 5     | `GOAL_CHECKLIST_LIST` | `checklist_list`       | All   |
| 6     | `GOAL_MAX_STATS_CT`   | `max_stats_ct`         | City Trial only |
| 7     | `GOAL_ASSEMBLE_AP_STAR` | `assemble_archipelago_star` | Archipelago row only |
| 8     | `GOAL_ALL_LEGENDARIES_CT` | `all_three_legendaries_in_one_run` | Archipelago row only |

### goal_checks Layout (GOAL_CHECKLIST_LIST)

When a row's goal is `GOAL_CHECKLIST_LIST`, the required checkboxes are specified in `goal_checks[row][2]` (2 x u64 = 128 bits per row, clear_kinds 0-119). Same encoding as `sent_checks`: bit `(k % 64)` of word `(k / 64)`. The goal is satisfied when every set bit in `goal_checks[row]` is also set in `sent_checks[row]`.

Rows sit at 0x078 (Air Ride), 0x088 (Top Ride), 0x098 (City Trial), 0x0A8 (AP checklist tab). Client writes big-endian u64s (dolphin-memory-engine handles byte order) and zero-fills any row that does not use `GOAL_CHECKLIST_LIST`. Fillers are blocked on goal-list checkboxes to prevent cheesing.

### KAROptions.py to APSlotOptions Mapping

The client reads slot options from the AP server (as defined in `KAROptions.py`) and writes the corresponding values to `APSlotOptions`. Most fields map directly by name and value - `death_link`, `energy_link`, `trap_link`, the per-mode `*_checklist_amount` / `*_reveal_checklist`, `city_trial_patch_cap_min` / `_max`, `spawn_rate_min`. The rest need conversion or have mismatched names:

| KAROptions.py Field | APSlotOptions Field | Conversion |
|---------------------|---------------------|------------|
| `air_ride_goal` / `top_ride_goal` / `city_trial_goal` | `goal[mode]` | TextChoice to GoalKind (see enum table) |
| `archipelago_reveal_checklist` | `reveal_checklists[AP_CHECKLIST_ROW]` | Direct; the AP tab is row 3 |
| `*_goal_locations` | `goal_checks[mode][]` | List of checkbox names to u64[2]: resolve each name to `(mode, clear_kind)` via `checklist-mappings.csv`, set bit `(k % 64)` in word `(k / 64)` |
| `machines_gated` | `machine_gating_enabled` | |
| `abilities_gated` | `ability_gating_enabled` | |
| `city_trial_events_gated` | `event_gating_enabled` | |
| `city_trial_patches_gated` | `patch_gating_enabled` | |
| `city_trial_items_gated` | `item_gating_enabled` | |
| `city_trial_boxes_gated` | `box_gating_enabled` | |
| `air_ride_courses_gated` | `airride_stage_gating_enabled` | |
| `top_ride_courses_gated` | `topride_stage_gating_enabled` | |
| `top_ride_items_gated` | `topride_item_gating_enabled` | |
| `colors_gated` | `color_gating_enabled` | |
| `city_trial_stadiums_gated` | `stadium_gating_enabled` | |
| `base_abilities_gated` | `base_ability_gating_enabled` | |
| `ap_patches` | `ap_patches` | AP Patch locations only - no goal reads it |
| `checklist_rewards` | `checklist_reward_placed_types` | The AP world ships the rewards it actually minted, already folded into a per-(mode, `RewardType`) bitmask |
| `legendary_pieces_goal_gated` / `vs_king_dedede_goal_gated` / `ap_star_pieces_goal_gated` | `goal_forced_gates` bits 0 / 1 / 2 | Each is 1 only when its category ships ungated *and* the seed's goal is gated on those unlocks |

Options **not written to the mod**, used at AP generation time or carried only in `slot_data` for the client's own logic: `trap_chance` (the client's trap-roll logic), `spawn_rate_max` (item-count generation), `ap_patch_placement` (which AP Patch locations may hold progression), `city_trial_permanent_patches` (whether permanent-patch items enter the pool - the mod has no corresponding field and always treats permanent patches as an active item category), and the per-mode `*_checkbox_fillers` fields.

## Item Delivery

Item receipt and application are decoupled. When the game reads an item from the mailbox, it immediately acknowledges receipt by incrementing `item_received_index` and appends the ID to `APSave.unprocessed_items`. Items are applied from that list when their conditions are met, and items that can't apply yet (for example an event while another event is active) are skipped so items behind them still process.

**Client side** is minimal: on connect, read `item_received_index` and skip all items below it; for each new item from the AP server, wait until `incoming_item_id == 0` and write the AP item ID. The game handles storage, scene-gating, and application.

**Game side** runs from `APItems_PerFrame` (`ap_item_handler.c`), a GObj re-created on every scene change by `APItems_OnSceneChange`:

1. `APItems_CheckMailbox` reads `incoming_item_id`; if non-zero it appends to `unprocessed_items`, increments `item_received_count`, mirrors it to `item_received_index`, and clears the mailbox.
   - **Queue full:** if `unprocessed_items` is already at `MAX_RECEIVED_ITEMS`, the game does **not** clear the mailbox and does **not** increment the counter. Leaving `incoming_item_id` set retries the same item each frame as the list drains, and because the client gates its next write on `incoming_item_id == 0` and only advances its send cursor after a successful write, holding the value stalls the client safely. Clearing it would lose the item permanently - the client has already advanced past it and the counter was never bumped for it.
2. `APItems_HandleItem` is then tried against the queue, resolving at most one item per frame. It returns APPLIED, RETRY, or DROP.
3. Both APPLIED and DROP remove the item from `unprocessed_items` (swap-with-last). Only RETRY keeps it queued, so a malformed or out-of-range ID can't wedge the queue.

### Scene gating inside `APItems_HandleItem`

The handler is a ladder, and where a range sits in that ladder is what determines its scene requirement:

- **Above any scene check** (apply anywhere, including menus): the four checkbox fillers, patch cap increase, spawn rate up, checklist rewards, permanent patches (save-only; the stat lands at the next round start), and every `*_UNLOCK_` category.
- **Above the 3D gate but with their own checks**: the cosmetic Kirby scale items (`KirbyScale_HandleItem`, returns RETRY until Kirby models exist) and the Top Ride item gives (`GateTopRideItems_GiveItem`). Both also apply in Top Ride, which uses `MNRKIND_19` and would never satisfy the 3D gate.
- **Top Ride copy-ability remap**: in `MJRKIND_TOP`, an ITKIND copy item is translated to its Top Ride analog via `Ability_ItKindToCopyKind` then `GateTopRideItems_AbilityToItem`, because Top Ride has no `RiderData` Kirbys. Abilities with no TR analog return RETRY and land in City Trial or Air Ride instead.
- **The 3D gate**: everything below requires major `MJRKIND_CITY` / `MJRKIND_AIR` / `MJRKIND_TOP`, minor `MNRKIND_3D`, and `Gm_GetIntroState() == GMINTRO_END`. The minor check matters: the CSS shares the major, and `intro_state` reads `GMINTRO_END` outside 3D.
- **Copy ability gives (IDs 328-338)** clear the 3D gate and nothing more. They grant through the rider API (`Ability_GiveItem` -> `Rider_GiveAbility`), which only indexes the static `stc_ability_init_table`, so they apply in every 3D mode including the stadiums and City Trial Free Run, and they bypass the ability unlock gate.
- **Free Run / stadium exclusion**: the remaining spawn-pipeline items additionally reject `CITYMODE_FREERUN` and `CityTrial_IsInStadium()`. Those scenes don't load the item data tables, so the spawn pipeline would fault in `Item_GetItDataPtr`.

### AP Item ID Map

These IDs must match between the APWorld Python code and the game mod, where they are the `APItemId` enum in `mods/archipelago/include/archipelago_api.h`. Ranges are spaced so future additions don't shift existing values.

**Standalone items (1-99):**

| ID  | Enum Name                  | Game Behavior |
|-----|----------------------------|---------------|
| 1   | `AP_ITEM_CHECKBOX_FILLER_AIRRIDE` | `Checklist_GrantFiller(GMMODE_AIRRIDE)` |
| 2   | `AP_ITEM_CHECKBOX_FILLER_TOPRIDE` | `Checklist_GrantFiller(GMMODE_TOPRIDE)` |
| 3   | `AP_ITEM_CHECKBOX_FILLER_CITYTRIAL` | `Checklist_GrantFiller(GMMODE_CITYTRIAL)` |
| 4   | `AP_ITEM_CHECKBOX_FILLER_ARCHIPELAGO` | `Checklist_GrantFiller(ap_checklist_mode)`. Dropped if the custom_checklist framework never registered the AP tab. |
| 5   | `AP_ITEM_PATCH_CAP_INCREASE` | `PatchCap_Increment()` - raises the patch cap by 1 |
| 6   | `AP_ITEM_1_HP_TRAP`        | `Machine_GiveDamage` down to exactly 1 HP on every human player's machine |
| 7   | `AP_ITEM_ALL_UP`           | `Patch_AllUp_GiveItem(+1)` - raise every stat by 1 for each human player |
| 8   | `AP_ITEM_PERM_PATCH_ALL_UP`| `PermanentPatch_GiveAllUp()` - permanent +1 to all stats, increments every `ap_save->permanent_patches[]` slot |
| 9   | `AP_ITEM_ALL_DOWN`         | `Patch_AllUp_GiveItem(-1)` |
| 10  | `AP_ITEM_GIVE_DRAGOON`     | `GateMachines_GiveLegendaryMachine(0)` - cinematic assembled-machine grant, not the three parts |
| 11  | `AP_ITEM_GIVE_HYDRA`       | `GateMachines_GiveLegendaryMachine(1)` |
| 12  | `AP_ITEM_SPAWN_RATE_UP`    | `SpawnRate_Increment()` - adds +10% to the CT/TR item spawn rate scale (capped at 5x) |
| 13  | `AP_ITEM_DROP_PATCHES_TRAP`| `Patch_DropTrap()` - ejects every human rider's equipped stat patches behind the machine (CT only) |
| 14  | `AP_ITEM_GIVE_AP_STAR`     | `GateApStar_GiveStar()` - runs the Archipelago Star's assembly for the first human rider it can, spheres not required (CT only) |

**Permanent +1 patches (100-108, aligned to PatchKind):** `AP_PERM_PATCH_*` = `100 + PatchKind`, in PatchKind order (Weight, Accel/Boost, TopSpeed, Turn, Charge, Glide, Offense, Defense, HP). Each calls `PermanentPatch_GiveItem(kind)`, incrementing `ap_save->permanent_patches[kind]`.

**City Trial events (200-215, aligned to EventKind):** `AP_EVENT_*` = `200 + EventKind`, calling `Event_GiveItem(kind)`. Order matches the `EventKind` enum: Dynablade, Tac, Meteor, Pillar, Run Amok, Restoration Area, Rail Fire, Same Item, Lighthouse, Secret Chamber, Prediction, Machine Formation, UFO, Bounce, Fog, Fake Powerups.

**Direct game items (300+, aligned to ItemKind):**

AP item ID = `300 + ItemKind`. How the item lands depends on its class:

- **Boxes (300-302)** spawn ahead of each human rider (`SpawnBoxHumansForward`, offset along the machine's forward vector) so the player drives into and breaks them, rather than on top of the rider. City Trial only.
- **Stat patches and downs** route through `Patch_GiveItem`, which spawns a real pickup in City Trial and applies directly via `Machine_GivePatch` in Air Ride. Top Ride has no `MachineData`, so these retry there.
- **Copy abilities (328-338)** never spawn a pickup - they grant straight to each human Kirby rider through `Ability_GiveItem`.
- **Everything else** spawns at every human player's location via `SpawnItemHumans` (`externals/hoshi/include/inline.h`), City Trial only. For non-`*FAKE` kinds `SpawnItemPlayer` invokes `Machine_OnTouchItem` immediately so the pickup applies the same frame. `ITKIND_*FAKE` kinds (`ITKIND_ACCELFAKE` through `ITKIND_WEIGHTFAKE`) are deliberately left for next-frame natural collision: manually invoking `Machine_OnTouchItem` outside the per-frame collision pipeline writes a hit-coll log entry that the next `HitColl_Init` clears before `HitColl_ActOnCollision` runs, so the fake-patch effect would silently drop.

| ID Range | Items |
|----------|-------|
| 300-302  | Boxes (Blue, Green, Red) |
| 303-318  | Stat patches and stat downs (Accel, TopSpeed, Offense, Defense, Turn, Glide, Charge, Weight, and their downs) |
| 319-320  | HP, All Up |
| 321-326  | Speed Max/Min, Offense Max, Defense Max, Charge Max/None |
| 327      | Candy |
| 328-338  | Copy abilities (Bomb, Fire, Freeze, Sleep, Tire, Bird, Plasma, Tornado, Sword, Spike, Mic) |
| 339-350  | Food items |
| 351      | Fireworks |
| 352-354  | Panic Spin, Sensor Bomb, Gordo |
| 355-360  | Hydra parts (1-3), Dragoon parts (1-3) |
| 361-368  | Fake patches (Accel, TopSpeed, Offense, Defense, Turn, Glide, Charge, Weight) |

**Stadium unlock items (400-423, aligned to StadiumKind):**

| ID Range | Items |
|----------|-------|
| 400-403  | Drag Race 1-4 |
| 404      | Air Glider |
| 405      | Target Flight |
| 406      | High Jump |
| 407-408  | Kirby Melee 1-2 |
| 409-413  | Destruction Derby 1-5 |
| 414-422  | Single Race 1-9 |
| 423      | Vs. King Dedede |

**Checklist reward items (500-649, encoded as `500 + mode*50 + reward_index`):**

The rewards from the game's three checklists (machines, characters, music, ...) treated as AP items, so they can be placed in any world in the multiworld. Decode with `mode = (id - 500) / 50`, `reward_index = (id - 500) % 50`, then apply via `ChecklistRewards_ApToGameIndex` and `ChecklistRewards_Grant`. Bands are stride-50 but each mode uses fewer entries, so IDs in the gaps are dropped.

| ID Range | Mode | Reward Indices | Count |
|----------|------|----------------|-------|
| 500-545  | Air Ride (0)   | 0-45 | 46 (machines, colors, music, sound tests, course, ...) |
| 550-582  | Top Ride (1)   | 0-32 | 33 (extra rules, items, colors, music, sound tests, ...) |
| 600-643  | City Trial (2) | 0-43 | 44 (legendary machines, stadiums, colors, music, ...) |

**Access gating unlock items (700-921):**

Each category sets a bit in a save-data mask; see the `gate_*.c` files.

| ID Range | Base | Enum Prefix | Category | Count | Save Field |
|----------|------|-------------|----------|-------|------------|
| 700-715 | 700 | `AP_EVENT_UNLOCK_` | City Trial events (aligned to EventKind) | 16 | `event_unlocked_mask` |
| 760-770 | 760 | `AP_ABILITY_UNLOCK_` | Copy abilities (aligned to CopyKind) | 11 | `ability_unlocked_mask` |
| 771-773 | 771 | `AP_BASE_ABILITY_UNLOCK_` | Kirby's base moves - inhale, quick spin, charge (aligned to `BaseAbilityKind`) | 3 | `base_ability_unlocked_mask` |
| 780-788 | 780 | `AP_PATCH_UNLOCK_` | Patch types (aligned to PatchKind) | 9 | `patch_unlocked_mask` |
| 790-819 | 790 | `AP_ITEM_UNLOCK_` | Item groups (aligned to ItemUnlockKind, `ITUNLOCK_NUM` = 30) | 30 | `item_unlocked_mask` |
| 820-825 | 820 | `AP_STAR_PIECE_UNLOCK_` | Archipelago Star assembly spheres (in `APStarPiece` order) | 6 | `ap_star_piece_unlocked_mask` |
| 830-854, 856 | 830 | `AP_MACHINE_UNLOCK_` | Machines (aligned to MachineKind) | 23 | `machine_unlocked_mask` |
| 860-862 | 860 | `AP_BOX_UNLOCK_` | Box types (Blue, Green, Red) | 3 | `box_unlocked_mask` |
| 870-878 | 870 | `AP_STAGE_UNLOCK_AIRRIDE_` | Air Ride stages | 9 | `airride_stage_unlocked_mask` |
| 880-887 | 880 | `AP_COLOR_UNLOCK_` | Kirby colors (aligned to KirbyColor; Pink/880 is the always-unlocked default, so its item is generated but is a no-op in-game) | 8 | `color_unlocked_mask` |
| 890-896 | 890 | `AP_STAGE_UNLOCK_TOPRIDE_` | Top Ride courses | 7 | `topride_stage_unlocked_mask` |
| 900-921 | 900 | `AP_TOPRIDE_ITEM_UNLOCK_` | Top Ride items (22 indices, 17 generated) | 17 | `topride_item_unlocked_mask` |

**Top Ride item note:** of the 22 `TopRideItemKind` indices, 5 are excluded from AP generation (IDs 909, 911, 912, 913, 916):

- **4 are ability-gated.** These spawn once the matching copy ability is unlocked, driven by `ability_unlocked_mask` rather than `topride_item_unlocked_mask` (the `ability_items[]` table in `gate_topride_items.c`). The fold only applies while ability gating is on: an ungated world holds an all-1s ability mask, which would otherwise free the four items outright.

  | Index | Engine item | Gating ability |
  |-------|-------------|----------------|
  | 9  | `TRITEM_FREEZE_FAN` | `COPYKIND_FREEZE` |
  | 11 | `TRITEM_FIRE`       | `COPYKIND_FIRE` |
  | 13 | `TRITEM_BOMB`       | `COPYKIND_BOMB` |
  | 16 | `TRITEM_WALKY`      | `COPYKIND_MIC` |

- **1 is an engine duplicate.** Index 12 is `TRITEM_PARTY_BALL_ALT` (the KirbyKusdama Party Ball variant). AP exposes only one Party Ball, at index 21 (`TRITEM_PARTY_BALL`); the mod mirrors bit 21's unlock onto bit 12 so both spawn together, and AP never sends ID 912 directly.

There is no Needle Top Ride item - `COPYKIND_NEEDLE` exists as a copy ability but has no corresponding `TRITEM_*`.

**Top Ride item gives (950-971, aligned to TopRideItemKind):**

AP item ID = `950 + TopRideItemKind`. Applies the item to every human Kirby via `GateTopRideItems_GiveItem` -> `TopRide_KirbyApplyItem` - a direct apply, not a position spawn plus next-frame collision. Only effective inside an active Top Ride round: the give returns RETRY whenever there is no Top Ride `KirbyMgr` or `round_state != 2`, because `TopRide_KirbyApplyItem` dereferences the Kirby's held-item GObj, which is only populated once the race is running. It is deliberately *not* gated on `kirby->is_active`, which is only set during a Race round and never in Time Attack or Free Run even while a human is playing.

| ID Range | Items |
|----------|-------|
| 950-971 | Hammer, Big Cake, Speed Up, Speed Down, Spinner, Charge Tank, Invincible Candy, Buzz Saw, Drill, Freeze Fan, Missile, Fire, Party Ball (alt), Bomb, Step-Boom, Lantern, Walky, Kracko, Who? Paint, Smokescreen, Chickie, Party Ball |

**Cosmetic items (972-973):**

Not unlocks and not gated, so they carry no game-enum alignment.

| ID  | Enum Name             | Game Behavior |
|-----|-----------------------|---------------|
| 972 | `AP_ITEM_BIG_KIRBY`   | Scale every human Kirby model by x1.5, clamped to 2.0 |
| 973 | `AP_ITEM_SMALL_KIRBY` | Scale every human Kirby model by x0.5, clamped to 0.5 |

**Archipelago Star sphere gives (980-985, in APStarPiece order):**

AP item ID = `980 + APStarPiece`. Adds that sphere to every human rider's collected set through `GateApStar_GivePiece`, the sphere counterpart of the 300-band Hydra and Dragoon part gives, and announces it under the name the `ap_star` mod carries for it. The sixth completes the set and runs the assembly. City Trial only, returning RETRY elsewhere. The collect is direct rather than a spawned pickup, so it does not need the sphere's unlock (820-825) to have arrived - the same way a Hydra or Dragoon part give ignores that part's unlock.

| ID Range | Spheres |
|----------|---------|
| 980-985 | Rose, Green, Violet, Tan, Blue, Yellow |

**Machine unlock note:** IDs 830-854 cover VCKINDs 0-24. VCKIND 25 (WHEELVSDEDEDE) is the Vs. King Dedede stadium's CPU-only machine - ID 855 is explicitly rejected by the handler and is not a valid machine unlock. IDs 856 and up continue the alignment into the MachineKinds `custom_machines` registers, in the order it discovers `machines/*.dat`.

The AP world (`worlds/kirby_air_ride/KARItems.py`) generates 23 unlock items as `progression`: 830-846, 848, 851-854 and 856. Three caveats for modders:

- **Top Ride machines are live gates, not placeholders.** 845 (FREE) and 846 (STEER) are read by the mod's Top Ride lobby gating (`GateMachines_TRLobbyCanStart` / `IsTRMachineUnlocked`, in `gate_machines.c`), which hard-blocks starting a Top Ride race unless at least one is unlocked. In the apworld they are tagged `source_modes=_TR` (they don't spawn in City Trial via `CT_SPAWN_EXCLUDED_MASK` and aren't Air Ride machines), and a guaranteed Top Ride machine starter - one of Free/Steer, precollected when `machines_gated` and Top Ride is in play - keeps the gate satisfiable in every seed config. AP logic doesn't model the lobby gate, so without that precollect the `_TR`-confined unlocks could land behind it (circular placement, Top-Ride-only softlock). Free/Steer are also excluded from the AR/CT machine starter pool since they can't be ridden there. When `machine_gating_enabled == 0`, the mod sets every gateable bit (`MachineGateMask()`, bits 0 through `MachineKind_Num() - 1`) at connect, so the lobby is freely startable.
- **Three in-range IDs ship no item.** 847 (WINGKIRBY), 849 (WHEELNORMAL) and 850 (WHEELKIRBY) are not selectable player machines - no character rides them in player-controlled contexts, and they are force-excluded from City Trial spawns. The mod still accepts the IDs and sets their bits, but no game code reads them. The canonical Dedede unlock is 854 (WHEELDEDEDE), which is what `CharacterDesc[CKIND_DEDEDE]` resolves to.
- **856 is positional, not fixed.** The Archipelago Star's MachineKind is assigned at boot from FST discovery order, so 856 names it only while `ap_star` ships the sole `machines/*.dat` (`mods/ap_star/assets/machines/VcStarAp.dat`). A second drop-in machine sorting ahead of it shifts the numbering, and the AP world's hardcoded 856 would then unlock the other machine.

## Location Data

In Archipelago, each checkbox in the game's three checklists is a **location**, and the multiworld generator decides what item is placed at each one. From this slot's perspective there are three cases:

- **Local vanilla rewards** - one of this slot's vanilla checklist rewards (AP IDs `500..649`). The multiworld can place any vanilla reward on any cell in any mode; when source mode differs from target mode, that's a cross-mode placement. This mapping is exactly what `locations` carries.
- **Local non-vanilla items** - anything else this slot owns (fillers, traps, gating unlocks, permanent patches). Delivered through the `incoming_item_id` mailbox when the cell is checked. Not part of `locations`, so the checklist UI has no advance notice.
- **Remote items** - owned by another slot. Earning the cell sends a location check to the AP server, which routes the item to its owner; locally the cell is treated as having no reward.

### Client Responsibilities

1. After connecting, scout every AP location belonging to this slot. For each scout result whose `item` is in `500..649`, decode `source_mode = (item - 500) / 50`, `source_reward_index = (item - 500) % 50`, and resolve the location code to `(target_mode, clear_kind)` via `checklist-mappings.csv`.
2. Write `locations[source_mode][source_reward_index] = (target_mode << 8) | clear_kind`. Default every unset entry to `0xFFFF`.
3. Set `location_data_valid = 1`, and re-send the whole array on every connection.

### Game Responsibilities

When `location_data_valid` is `1`, `OnFrameStart` calls `ChecklistRewards_ApplyLocations()`, which copies `ap_data->locations` into `ap_save->shuffled_rewards` (the canonical persisted form), rebuilds derived state, clears the flag, and persists the save. The rebuild walks each `(source_mode, reward_index)`:

- `0xFFFF` (remote/unused): set `stc_reward_table_ptrs[source_mode][i].clear_kind = 0` as a sentinel.
- `target_mode == source_mode` (same-mode local): write `clear_kind` into that `RewardEntry` so the vanilla checklist scan finds it.
- `target_mode != source_mode` (cross-mode local): leave the source's `clear_kind` at the `0` sentinel and populate `cross_mode_slots[target_mode][clear_kind]` with `(source_mode, reward_index)` for the cross-mode display hooks.

It then re-grants any already-received items so their local checklist slots are marked correctly under the new shuffle. On subsequent boots `ChecklistRewards_OnSaveLoaded` rebuilds the same derived state from `shuffled_rewards`, so the game works before the client reconnects.

**Why the sentinel is `0`:** any value `>= 120` (the size of `clear[]`, including the natural out-of-band choice `0xFF`) trips the vanilla OOB assert at `0x8004a08c` when a vanilla code path uses `clear_kind` as an array index. `0` is the smallest in-range value, and is safe only because every vanilla read of `RewardEntry.clear_kind` is gated on `shuffled_rewards != 0xFFFF`.

### Hiding remote and cross-mode cells

Remote rewards and cross-mode source rows must never read or write a same-mode placement:

- `ChecklistRewards_ShouldSkipReward` skips reward indices whose `shuffled_rewards[src_mode][src_ri]` is `0xFFFF` or whose target mode differs from the current mode (via `IsSameModeLocalPlacement`), so vanilla never sets `has_reward` on a non-existent same-mode placement.
- `ChecklistRewards_CheckUnlocked` replaces `ClearChecker_CheckUnlocked` and gates on the same predicate, returning 0 for remote without touching `clear[]`.

### Display

Local vanilla rewards ride the vanilla checklist flow: the reward's `RewardEntry.clear_kind` points at a checkbox; when the player completes that checkbox's objective, `Checklist_SetRewardFlagOnUnlocks` sets `has_reward` on it (the mod's hook allows this for same-mode local rewards, and a post-loop hook handles cross-mode); the star icon appears and `ClearChecker_CheckUnlocked` returns 1, unlocking the machine/color/music.

For cross-mode display, `cross_mode_slots[4][120]` maps `(target_mode, clear_kind)` back to `(source_mode, source_reward_index)`. All three checklist SIS files are loaded simultaneously so reward text and icons from any mode can be drawn. Implementation is in `checklist_rewards.c`.

## Location Checking (Sending)

When a player completes a checkbox, that location must be reported to the AP server so the item placed there reaches its owner.

The mod is the source of truth: it owns `sent_checks[4][2]` in both shared memory and save data. The client polls the bitmask, diffs against last-seen state, and sends new checks. **The client never reads `GameClearData.clear[]` directly.**

### Client Responsibilities

1. **On connect**: read `sent_checks` and `goal_complete`. For each set bit, decode `(mode, clear_kind)`, resolve the AP location code via `checklist-mappings.csv`, and send it as a location check (the server dedupes). If `goal_complete == 1`, send victory. Then diff the server's `checked_locations` for this slot against `sent_checks` and write anything the mod is missing into `client_backfill`.
2. **Steady state** (poll cadence is flexible; 1 Hz is fine): diff `sent_checks` against last-known state and send each newly set bit; forward `goal_complete` if newly set; watch `RoomUpdate.checked_locations` (for example from `!collect`) and write anything new into `client_backfill`.
3. **Decoding**: for mode `m` and clear_kind `k`, the bit is `sent_checks[m][k / 64] & (1 << (k % 64))`.

### Game Responsibilities

1. **On gameplay completion**: the mod replaces `ClearChecker_SetNewUnlock` (`0x8004a054`) with a wrapper that detects the moment of transition and writes the bit into both `ap_save->sent_checks` and `ap_data->sent_checks`. As a whole-function replacement it intercepts every caller automatically (AR/CT/TR objectives, stadium results, free run). **Manual filler placement does not route through this function** - it is caught by a separate hook at `0x80180dc4` (the vanilla filler store site inside `Checklist_Think`).
2. **Meta auto-unlock hooks**: five "meta" checkboxes bypass `SetNewUnlock` because vanilla sets them via direct stores inside `Checklist_ProcessUnlock`. The mod hooks each of the 5 store sites directly - it does not poll per frame - and forwards `is_unlocked` transitions: AR `0x18`, TR `0x77`, CT `0x37` (the native "Fill in over 100 Checklist blocks!" cells) and CT `0x6D` / `0x6E` (the Dragoon-parts and Hydra-parts cells, which auto-complete when the corresponding part rewards are received). These two are distinct from the Hydra-and-Dragoon goal cell at CT `0x77`.
3. **Backfill processing**: each frame `CheckDetection_OnFrameStart` runs `ProcessBackfill`, which ORs `client_backfill` bits into `sent_checks`, sets `clear[].is_unlocked` and `clear[].is_visible` for visual consistency, sets `has_reward` where a local AP placement exists for that checkbox *and* the source item has been received, re-evaluates the goal, then clears `client_backfill`.
4. **Goal evaluation**: after every check transition and on save load, the mod evaluates the active goal and sets `goal_complete = 1` if satisfied. Sticky and persisted across reboots.

### Goal Evaluation (Mod-Side)

Evaluated in `check_detection.c` against `ap_save->options` (the slot options copied at handshake). Almost every goal type reduces to bit reads on `sent_checks`:

| Goal | Detection |
|------|-----------|
| `GOAL_NONE` | Vacuously satisfied for that mode |
| `GOAL_100_CHECKLIST` | Single bit: the native "Fill in over 100 Checklist blocks!" cell (AR `0x18` / TR `0x77` / CT `0x37`). **Not** a popcount - it reads the same vanilla cell the meta-unlock hook sets. |
| `GOAL_N_CHECKLIST` | `popcount(sent_checks[mode]) >= checklist_amount[mode]` |
| `GOAL_HYDRA_AND_DRAGOON` | Single bit `0x77` in `sent_checks[CT]` (the native "complete both Dragoon and Hydra in one match" cell). **Not** bits `0x6D`/`0x6E`, which are the separate part-unlock cells. |
| `GOAL_BEAT_KING_DEDEDE` | Bit `0x2F` in `sent_checks[CT]` |
| `GOAL_CHECKLIST_LIST` | `(sent_checks[mode] & goal_checks[mode]) == goal_checks[mode]` |
| `GOAL_ASSEMBLE_AP_STAR` | The Archipelago row's bit for `APCK_ASSEMBLE_AP_STAR` (clear_kind 50), set when a human collects all six Archipelago spheres in one `CITYMODE_TRIAL` round. Each sphere needs its own unlock item (820-825) to spawn at all. |
| `GOAL_ALL_LEGENDARIES_CT` | The Archipelago row's bit for `APCK_ASSEMBLE_ALL_LEGENDARY` (clear_kind 51), set when one human assembles Dragoon, Hydra and the Archipelago Star inside a single `CITYMODE_TRIAL` round. |
| `GOAL_MAX_STATS_CT` | Sticky save bit `max_stats_ct_achieved`, set when any human player's 9 CT stats simultaneously reach `city_trial_patch_cap_max` (1-127; **not** `PATCH_STAT_MAX`, the absolute clamp ceiling of 127) during a `CITYMODE_TRIAL` round. Stadium and Free Run do not count. When `min < max` the player must first receive every Patch Cap Increase item to make the ceiling reachable. |

Victory fires only if at least one mode has a non-NONE goal AND every mode's goal is satisfied. Mode goals are independent - set a mode's goal to `GOAL_NONE` to keep it out of the victory condition.

Per-mode satisfaction is published as `goal_satisfied_mask` alongside the aggregate `goal_complete`, and the mod announces each mode's goal as it lands. The client feeds the mask to Universal Tracker: UT's go-mode readout is the world's completion condition, and under UT the apworld swaps the AND-every-victory rule for "some goal is still outstanding and in logic" - the mask is what tells it which goals are already done, since logic reachability alone can only say a goal is *available*.

### Collect / Release

- **`!release`**: items destined for this player arrive through the normal `incoming_item_id` mailbox. The grant sets `has_reward` on the local placement if there is one. No effect on `sent_checks`.
- **`!collect`**: another player pulls their items out of this world, and the AP server marks those locations checked. The client detects this via `RoomUpdate.checked_locations` and writes the affected bits into `client_backfill`; the mod marks them completed locally and re-evaluates the goal. This can trigger `goal_complete` passively, which matches standard AP semantics.

## DeathLink

### Client Responsibilities

- **Sending**: watch `deathlink_send`; when it becomes `1`, send a DeathLink bounce and clear it to `0`.
- **Receiving**: on an incoming bounce, write `1` to `deathlink_receive` immediately - no need to wait for the game to clear the previous flag (see Protocol Rules). Skip the write if Dolphin isn't hooked; bounces arriving during a disconnect are dropped rather than queued, to avoid a flood on reconnect.

### Game Responsibilities

- **Detecting death**: three hooks set `deathlink_send = 1` when any human player dies - one inside `Rider_CheckToDieOnMachine` (`0x801a06d0`) for HP-zero deaths, one inside `Machine_SetFallDead` (`0x801e6540`) for fall-off-course deaths, and one in the Top Ride sand-pit death path (`0x80331a94`).
- **Applying death (3D modes)**: a per-frame GObj checks `deathlink_receive`, gated on `GmIntroState == GMINTRO_END`. On `1` it kills every human player using the mechanism the current mode supports - HP-zeroing in City Trial / Destruction Derby / Melee / Vs. King Dedede, fall-off-course death via `Machine_SetFallDead` at the player's current checkpoint in Air Ride and the racing stadiums - then clears the flag.
- **Top Ride** has its own path: `DeathLink_OnTopRideLoadEnd` installs `DeathLink_TopRidePerFrame`, which on receive applies a random damage-class Kirby state (Press / Freeze / Numb / Confuse) to every human Kirby and clears the flag. It is **not** gated on `GmIntroState`, since Top Ride has no intro countdown.

## EnergyLink

### Units

The AP server stores the EnergyLink pool in integer Joules; the mod stores `energy_balance` and `energy_sent_total` in raw MJ. The client scales in both directions (multiply by 1,000,000 going to the server, integer-divide coming back). All values sent to the server MUST be integers - the AP data-storage protocol does not accept floats.

### Client Responsibilities

**Processing sends** must happen *before* updating the balance; the ordering is what closes the overdraw window.

- **Seeding / restart detection**: maintain a `last_seen` watermark, re-seeded (record the current value, apply nothing) whenever a fresh game session starts - on connect, on a struct-pointer change at `0x805d52d4`, and on a `game_ready` 1-to-0 transition. The mod sets `game_ready` once in `OnSaveLoaded` and never clears it during play, so the reboot `memset` zeroing it is the restart signal. Do **not** persist `last_seen` across sessions: the counter resets to 0 each boot, so a persisted watermark would turn the boot's drop to 0 into a phantom withdrawal. There is no magnitude backstop - a small reset-to-0 is indistinguishable from ordinary spending, so the client relies on the `game_ready` signal alone.
- **Each poll (~1s)**: `cur = read(energy_sent_total)`, `delta = cur - last_seen`.
  - `delta == 0`: nothing to send.
  - `delta > 0` (deposit): send `Set` with `add: delta * 1_000_000` and no tag.
  - `delta < 0` (withdrawal): send `Set` with operations `[add: delta * 1_000_000, max: 0]`, plus a unique `tag` (uuid) and `want_reply: true`. On the matching `SetReply`, compare `original_value - value` against the requested subtraction; if the server subtracted less (pool ran out), log the discrepancy - the mod's local balance already overshot and will be corrected on the next `set_notify` push.
  - Advance `last_seen = cur`, then optimistically fold the delta into the cached pool: `current_energy_link_value = max(0, current_energy_link_value + delta_joules)`.
- **Optional torn-read guard**: read the field twice and skip the poll if the reads disagree. Belt-and-suspenders only - because the counter is cumulative rather than consume-once, an unguarded torn read self-heals on the next poll's diff.

**Updating balance**: after processing sends, write the current AP pool total (in raw MJ, `pool_joules // 1_000_000`) to `energy_balance` as an s64, **unconditionally** every poll - so seed polls, `delta == 0` polls, and other players' deposits all keep the mod's view fresh. Sub-MJ remainders are not representable on the mod side and are dropped.

**Why order and the optimistic fold matter (overdraw prevention).** The balance is sourced from `current_energy_link_value`, the last server-pushed pool. Writing it before the diff - or without folding in the just-sent delta - would bounce the mod's immediate local decrement from a purchase back up to the stale pre-purchase pool. Across the one-to-two-poll server round trip the affordability gate would then see a stale-high balance and permit a self-induced overdraw. Processing the send first and applying the `max(0, value + delta_joules)` update makes the balance reflect the spend now. The optimistic guess is harmless: `set_notify`'s `SetReply` reassigns `current_energy_link_value` to the server's absolute pool value on every change. What remains is the irreducible shared-pool race - a concurrent change by another player leaves the value off by at most one poll.

**`set_notify`**: subscribe to `EnergyLink{team}` once on connect; the standard CommonClient handler updates `current_energy_link_value` from each broadcast SetReply.

### Game Responsibilities

- **Generating energy** (`energylink.c`): accumulates locally from destroyed objects, collected patches, and machine charging. Sub-MJ precision is kept in a float carry that persists across scene loads (charge gain produces fractional MJ per frame); whenever the carry crosses a whole MJ, that whole part is added to `energy_sent_total` and the remainder rolls forward. There is no flush and no slot check - the counter is written directly and the client diffs it.
- **Spending energy** (`energylink_spend.c`): a purchase in the in-game EnergyLink menu queues the bought item ID into `unprocessed_items` (the same path AP-delivered items take), then subtracts the integer cost from both `energy_sent_total` (which the client diffs and forwards as a withdrawal) and `energy_balance` (immediate UI feedback and the affordability gate). This happens on the purchase event itself in **any** scene - no gameplay frame is required, so menu purchases reliably reach the pool. Purchases are rejected when the queue is full. Auto-Charge's per-frame fractional withdrawals fold into the same carry.

## TrapLink

### Client Responsibilities

- **Receiving traps**: wait until `traplink_receive == 0`, then write `1`. The incoming Bounce's `trap_name` is **ignored** - KAR applies a random local trap from its own pool regardless of which named trap the source world sent. This is deliberate: KAR's trap pool has no clean 1:1 mapping to other worlds' trap names, so any incoming TrapLink Bounce means "apply some trap locally".
- **Sending traps**: watch `traplink_send`. When it becomes non-zero, read the value as a `TrapLinkKind`, look up the corresponding `trap_name`, send a TrapLink Bounce with `{"time", "source", "trap_name"}`, then clear the field - one Bounce per non-zero read. The game has no send cooldown, but the field is a single `u32` rather than a queue, so several triggers inside one poll window (picking up multiple bad items from one box burst, say) collapse into the last kind written. No further client-side debounce is needed.

### `traplink_send` kind enum

Values are defined in `mods/archipelago/src/traplink.h`. Unknown kinds (future additions seen by older clients) should fall back to a generic name like `"Trap"`.

| Value | TrapLinkKind             | Suggested `trap_name` | Triggered by |
|-------|--------------------------|-----------------------|--------------|
| 0     | `TRAPLINK_KIND_NONE`     | (no send pending)     | - |
| 1     | `TRAPLINK_KIND_BAD_PATCH`| `"Bad Patch"`         | City Trial - bad/fake patch pickup (SPEEDMIN, CHARGENONE, `*DOWN`, `*FAKE`) |
| 2     | `TRAPLINK_KIND_SLEEP`    | `"Sleep"`             | City Trial / Air Ride - sleep copy ability granted |
| 3     | `TRAPLINK_KIND_SPEED_DOWN`| `"Speed Down"`       | Top Ride - `TRITEM_SPEED_DOWN` pickup |

### Game Responsibilities

- **Applying traps**: `TrapLink_PerFrame` is a GObj installed only in 3D and Top Ride scenes. It checks `traplink_receive` gated on `Gm_GetIntroState() == GMINTRO_END` (which only bites in 3D; Top Ride has no intro and defaults to `GMINTRO_END`), then dispatches on the scene major:
  - **City Trial**: picks a random trap from a table (stat downs, sleep, meteors, rail fire, bounce, fake powerups, run amok, fake patch) and applies it through `APItems_HandleItem`. Free Run drops the trap outright, since item data tables aren't loaded and the spawn would crash; stadiums fall back to the Air Ride sleep trap, where riders are always mounted.
  - **Air Ride**: gives `COPYKIND_SLEEP` to every human rider directly through `Rider_GiveAbility`, bypassing `Rider_CheckAndGiveAbility` so neither the ability gate nor the sleep-send hook re-triggers. Riders not currently on a machine are skipped - the sleep animation's MObj callback calls `Rider_CopyInputToMachine` and would deref a null machine GObj.
  - **Top Ride**: applies `TRITEM_SPEED_DOWN` through the shared give path (`GateTopRideItems_GiveItem` -> `TopRide_KirbyApplyItem`) - a direct apply, not a position spawn. Only items whose TR dispatcher installs a self-debuff state qualify as traps; most TR items buff the user or arm an attack.

  If the trap can't apply the flag stays set and it retries next frame; once applied the flag is cleared.
- **Detecting traps**: code hooks on natural negative gameplay events set `traplink_send` to the corresponding kind. AP-delivered items can also trip these hooks (a received SPEEDMIN trap re-fires the bad-patch detector), so the client must handle deduplication.
- **Receive recursion guard**: after applying an incoming trap, the mod suppresses outgoing sends for 120 frames (`TRAPLINK_RECV_GUARD_FRAMES`). This stops a received trap whose effect re-fires a send hook - an applied bad patch tripping the bad-patch send detector - from echoing straight back out as a new Bounce. It is a recursion guard, not a rate limit; burst collapsing and dedup of distinct logical traps remain the client's responsibility.

## In-Game Messages

The text box shows Archipelago traffic as one-line messages. The client is the author: it is the side that knows other worlds' item and location names, every player's name, and Archipelago's color conventions.

### Client Responsibilities

- **Status**: post "Archipelago client connected" as a `STATUS` message once the handshake completes, and "disconnected" on a clean shutdown - the latter written straight into the mailbox, since nothing drains the backlog after that, and skipped if the mailbox is still full. A client killed outright posts neither, and the game keeps showing whatever it last received.
- **Checks**: when `check_locations` confirms new locations, compose one line per location from `locations_info` - the scout cache the connect-time `LocationScouts` fills for every location this slot owns - so no server round trip is needed. One line per location, always - a burst goes into the outgoing queue like any other backlog.
- **Items**: compose one line as each item goes into the `incoming_item_id` mailbox. The `NetworkItem` there carries the sending player and the item flags. Skip an item this slot placed for itself while check messages are on - its check line already named it.
- **Links**: compose one line each time a DeathLink or TrapLink Bounce is accepted or forwarded, naming the other player on the incoming ones and the `trap_name` on both TrapLink directions. The DeathLink `cause` is dropped - it restates the source name in a sentence that costs most of a screen line.
- **Server text**: relay `Hint`, `Goal`, `Release`, `Collect`, `Chat` and `ServerChat` `PrintJSON` packets. **Not `ItemSend`** - the server broadcasts one to the whole team for every check anyone makes, and this slot's own are already covered by the check and item lines.
- **Encoding**: fold text to the glyphs the game font renders before packing.

### Game Responsibilities

- `APText_OnFrameStart` (`ap_text.c`) renders a pending message into the text box, holding it while the text box reports it has no canvas. It keeps no client state.
- Messages are filtered by `kind` against the Messages settings menu on render, so the menu is authoritative even against a stale client.
- The lines the mod composes about AP traffic have their own toggles under Messages -> Local, keyed by `APLocalKind`: `APLOCAL_CHECK` for "Check recorded", `APLOCAL_ITEM` for an applied grant, `APLOCAL_GOAL` for the mode and seed goal lines, `APLOCAL_LINK` for a DeathLink or TrapLink firing or landing. All but the goal lines default Off, so each event normally produces one line - the client's; a player running without a client turns them on.
- Every grant announce routes through `APAnnounce_Grant` / `APAnnounce_GrantSegments` (`ap_announce.c`), which is where the `APLOCAL_ITEM` toggle and the boot regrant's `ap_regrant_quiet` both apply. The check and goal lines have one call site each and test `APAnnounce_LocalEnabled` directly. Announces that report a consequence the AP item name does not carry (patch cap percentage, spawn rate percentage) call the text box directly and keep printing, as do all non-AP paths: EnergyLink purchases, in-game pickups, gate prompts.

## Invariants

- **ID 0 is reserved** as the "empty" sentinel for `incoming_item_id`. Never use 0 as a valid AP item ID.
- **Items can process out of order.** An item that can't apply yet is skipped and items behind it still apply. The unprocessed list shrinks as items resolve.
- **`item_received_index` reflects receipt, not application.** It increments as soon as an item is read from the mailbox, before the item is applied. The client uses it only to avoid re-sending.
- **The unprocessed item list persists in save data.** `unprocessed_items` is stored on the memory card and survives reboots, so items received but not yet applied (the player powered off before an event item could fire) are retained. `item_received_count` is also persisted and mirrored to `item_received_index` on boot so the client can resume from the right position.
- **One item per frame.** At most one item resolves from the unprocessed list per frame, so 60 items/sec is the ceiling. The client can write to the mailbox as fast as the game clears it.
- **Queue capacity is `MAX_RECEIVED_ITEMS` (512).** In-game EnergyLink purchases share the same queue, so a player can fill it without the client sending anything.
- **Text messages are advisory.** Nothing in the game state depends on one arriving. The mod drops a message whose `kind` or `seg_count` is out of range.
