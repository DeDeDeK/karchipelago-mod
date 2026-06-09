# Client-Game Protocol

This document describes the shared memory interface between the Python Archipelago client and the Kirby Air Ride game mod. The client uses `dolphin-memory-engine` to read/write game memory while the mod is running in Dolphin.

## Shared Memory Access

The game allocates an `APData` struct in `OnBoot()` and stores a pointer to it at the static address `0x805d52d4`. The client reads this pointer to locate the struct.

```python
# Python client startup
ap_data_ptr = memory.read_u32(0x805d52d4)
# ap_data_ptr now points to the APData struct
```

## APData Layout

All 32-bit fields are 4-byte aligned and atomic on PPC at this alignment. 64-bit fields (`energy_balance`, `energy_sent_total`, `sent_checks`, `client_backfill`, `goal_checks`) are NOT atomic on PPC32 — readers may observe a torn value during a writer's update. For `energy_sent_total` this is self-correcting: it's a cumulative counter the client reads-and-diffs, so a torn read only skews one poll's delta and the client sets `last_seen` to whatever it read — the next poll's diff compensates exactly (the old clear-to-zero mailbox, by contrast, applied a torn delta permanently). Per-frame deltas are also far below 2^31, bounding the magnitude of any single torn read.

Offsets are relative to the struct base address. Struct size and individual field offsets may shift as fields are added; the Python client should locate the struct via the static pointer at `0x805d52d4` and use the field order in `mods/archipelago/src/main.h` (`APData` struct) as the canonical reference.

### Communication Fields

| Offset | Type   | Field                | Writer       | Reader/Clearer |
|--------|--------|----------------------|--------------|----------------|
| 0x000  | s64    | `energy_balance`     | Client       | Game (reads; may locally adjust for purchase UI — client write is authoritative) |
| 0x008  | s64    | `energy_sent_total`  | Game         | Client (read-and-diff; NEVER writes) |
| 0x010  | u32    | `deathlink_receive`  | Client       | Game (clear to 0) |
| 0x014  | u32    | `deathlink_send`     | Game         | Client (clear to 0) |
| 0x018  | u32    | `traplink_receive`   | Client       | Game (clear to 0) |
| 0x01C  | u32    | `traplink_send`      | Game         | Client (clear to 0) — value is a TrapLinkKind enum, see TrapLink section |
| 0x020  | u32    | `incoming_item_id`   | Client       | Game (clear to 0) |
| 0x024  | u32    | `item_received_index`| Game         | Client (read-only) |

`energy_balance` and `energy_sent_total` are denominated in **raw MJ units** (1 raw unit = 1 MJ in the AP pool, by the `ENERGY_LINK_EXCHANGE_RATE = 1_000_000` convention). The fields are signed 64-bit so the mod can faithfully mirror multiworld pools that exceed u64 *joules* (i.e., > ~1.8×10^19 J), which translates to ~1.8×10^13 raw MJ on the mod side. `energy_sent_total` is *net* (deposits − withdrawals) and resets to 0 each mod boot, so it stays far below even that figure — nowhere near the s64 ceiling (~9.2×10^18).

### Handshake and Options Fields

| Offset | Type | Field | Writer | Reader | Description |
|--------|------|-------|--------|--------|-------------|
| 0x028  | u32  | `game_ready` | Game | Client | 1 when mod is fully initialized |
| 0x02C  | u32  | `options_valid` | Client | Game | 1 after client has written all options |
| 0x030  | APSlotOptions | `options` | Client | Game | Slot options block (see layout below) |

### Location Data Fields

| Offset | Type        | Field                 | Writer | Reader            | Description |
|--------|-------------|-----------------------|--------|-------------------|-------------|
| 0x0C8  | u32         | `location_data_valid` | Client | Game (clear to 0) | 1 after client has written `locations` |
| 0x0CC  | u16[3][46]  | `locations`           | Client | Game              | `locations[source_mode][source_reward_index]` = destination cell for this slot's checklist reward. Padded to 46 per mode. |

`locations` is indexed by **source reward** — the entry at `[m][i]` tells the game where in the checklist grid this slot's reward `i` of mode `m` lives. Per-mode meaningful entry counts: AR=46, TR=33, CT=44. Unused trailing entries (e.g. TR indices 33-45) should be `0xFFFF`.

> **`reward_index` convention (wire = clear_kind-sorted).** Both `locations[m][i]` and the checklist-reward item IDs (`500 + mode*50 + reward_index`) index rewards in **clear_kind-sorted order** — the order they appear in the `AP_REWARD_*` enum and in `checklist-mappings.csv`, which is the apworld's natural numbering. The game's *internal* reward table (`stc_reward_table_ptrs`) is in a different ROM order, so the mod translates wire `reward_index` → internal game index at both boundaries (see `ChecklistRewards_ApToGameIndex` in `clearchecker-system.md`). The client does **not** need to know the internal order — it only ever uses the clear_kind-sorted index.

| Value         | Meaning |
|---------------|---------|
| `0xFFFF`      | Remote (or unused slot). The reward is owned by another slot — no local cell to display it on. |
| `(target_mode << 8) \| clear_kind` | Local placement. `target_mode` is the mode whose checklist holds this reward (0=AR, 1=TR, 2=CT). `clear_kind` is the cell index within that mode. If `target_mode == source_mode` the reward stays in its native checklist; otherwise it's a cross-mode placement. |

This carries only vanilla checklist rewards (AP item IDs `500..649`); non-vanilla items placed on this slot's cells (fillers, traps, gating unlocks) are not part of the layout — they're delivered through the standard `incoming_item_id` mailbox when the player checks the cell. The checklist therefore can only render a "this cell has a reward" star for vanilla placements; non-vanilla local placements are visually indistinguishable from empty cells until the player earns the check.

### Check Detection Fields

| Type | Field | Writer | Reader | Description |
|------|-------|--------|--------|-------------|
| u64[3][2] | `sent_checks`     | Game   | Client | Bitmask of checkboxes the player has completed in gameplay or via filler. Bit `(k % 64)` of word `(k / 64)` for clear_kind `k` in mode `m`. Mirror of `APSave.sent_checks`. |
| u64[3][2] | `client_backfill` | Client | Game (clears) | Additive backfill: client writes bits for checks the AP server already knows about (e.g., fresh save / slot takeover). Mod ORs new bits into `sent_checks`, also sets `clear[].is_unlocked` and `has_reward` where applicable, re-evaluates goal, then clears this field. |
| u8        | `goal_complete`   | Game   | Client | Sticky once set. 1 when the active goal condition is satisfied. Mod evaluates per-frame after every check transition; client reads on connect and on poll, forwards victory to AP server. Persisted to `APSave.goal_complete`. |

### Menu Toggle State

Live mirror of the Settings menu toggles. Written by the game on boot (after save-restore), on first-connect option transfer, and every time the player changes a toggle. Game-owned: client reads, never writes. The player can disable/re-enable these mid-session, and the client must forward the change to the AP server (e.g., `ConnectUpdate` `tags` for DeathLink, and the equivalent for TrapLink/EnergyLink).

| Type | Field | Writer | Reader | Description |
|------|-------|--------|--------|-------------|
| u32  | `deathlink_menu_enabled`  | Game | Client | 1 if DeathLink toggle is On. Diff against last-seen to detect toggles. |
| u32  | `energylink_menu_enabled` | Game | Client | 1 if EnergyLink toggle is On. |
| u32  | `traplink_menu_enabled`   | Game | Client | 1 if TrapLink toggle is On. |

On connect, the client should read all three and send the corresponding state to the AP server. `APSlotOptions.death_link_enabled` / `energy_link_enabled` / `trap_link_enabled` only set the *initial* values and are not updated on subsequent toggles — these mirrors are the authoritative current state.

The exact byte offsets are computed by the compiler and may shift as fields are added to `APData`. The Python client should locate the struct via the static pointer at `0x805d52d4` and use the field order in `mods/archipelago/src/main.h` (`APData` struct) as the canonical reference.

## Connection Handshake

The client and game must synchronize before data exchange. The sequence is:

```
Client                                Game
  |                                     |
  |  1. Poll 0x805d52d4 until != 0      |  OnBoot: alloc struct, store pointer
  |  2. Read pointer, poll game_ready   |  OnSaveLoaded: load save, set game_ready=1
  |     until == 1                      |
  |  3. Write options fields            |
  |  4. Set options_valid = 1           |
  |  5. Read item_received_index        |  OnFrameStart: detect options_valid,
  |  6. Write locations                 |    copy to save, set menu toggles
  |  7. Set location_data_valid = 1     |  OnFrameStart: detect location flag,
  |  8. Begin item delivery             |    apply to locations, save
  |                                     |
```

### Client Connection Procedure

1. **Wait for mod**: Poll the pointer at `0x805d52d4` until it is non-zero.
2. **Wait for initialization**: Read `game_ready` at base + `0x028`. Poll until it equals `1`. This ensures save data is loaded and `item_received_index` is valid.
3. **Write slot options**: Write all `APSlotOptions` fields.
4. **Signal options complete**: Write `1` to `options_valid`.
5. **Read item index**: Read `item_received_index`. Skip all items with index < this value.
6. **Write `locations`**: For each of this slot's checklist rewards (vanilla reward source-indexed by `(source_mode, source_reward_index)`), write the destination cell where the multiworld placed it: `(target_mode << 8) | clear_kind`, or `0xFFFF` if the reward is owned by another slot. See Location Data below.
7. **Signal location data complete**: Write `1` to `location_data_valid`.
8. **Begin normal operation**: Start item delivery, deathlink/energylink/traplink polling, and location checking.

The client should write options and location data on **every connection**. The game handles deduplication: on first connection for a given save file, it copies options to persistent save data and sets initial menu toggles. On subsequent connections (same save), the write is accepted but the game does not re-copy. Location data is always re-applied on each connection.

## Slot Options

### APSlotOptions Layout

All fields are `u32` unless noted. Field order in `APSlotOptions` in `main.h` is the canonical reference for offsets.

| Field                             | Values | Description |
|-----------------------------------|--------|-------------|
| `death_link_enabled`              | 0 or 1 | Sets initial deathlink menu toggle |
| `energy_link_enabled`             | 0 or 1 | Sets initial energylink menu toggle |
| `trap_link_enabled`               | 0 or 1 | Sets initial traplink menu toggle |
| `reveal_checklists`               | 0 or 1 | Reveal all checklist squares |
| `goal[3]`                         | GoalKind | Completion condition per mode (indexed by GameMode) |
| `checklist_amount[3]`             | 1-120  | N for GOAL_N_CHECKLIST per mode (indexed by GameMode) |
| `city_trial_progressive_patch_caps` | 0 or 1 | Patch cap starts low, items raise it |
| `city_trial_patch_cap_amount`     | 1-127  | Target patch cap (also the threshold for GOAL_MAX_STATS_CT) |
| `spawn_rate_min`                  | 100-500 (percent) | Spawn rate floor for CT/TR items. 100 = vanilla. Each Spawn Rate Up item adds +10% on top, capped at 500. AP world ships `(max - min) / 10` items so collecting all reaches the configured max. 0 is treated as 100. |
| `goal_checks[3][2]`              | u64 bitmask | Required checkboxes per mode for GOAL_CHECKLIST_LIST (48 bytes, see below) |
| `machine_gating_enabled`         | 0 or 1 | 1 = gated (default). 0 = all machine unlocks pre-applied at connect; AP world ships no machine unlock items. |
| `ability_gating_enabled`         | 0 or 1 | 1 = gated. 0 = all copy abilities unlocked at connect; AP world ships no ability unlock items. |
| `event_gating_enabled`           | 0 or 1 | 1 = gated. 0 = all CT events unlocked at connect; AP world ships no event unlock items. |
| `patch_gating_enabled`           | 0 or 1 | 1 = gated. 0 = all patch types unlocked at connect; AP world ships no patch unlock items. |
| `item_gating_enabled`            | 0 or 1 | 1 = gated. 0 = all item-category unlocks (All-Up, food, fireworks, etc.) applied at connect; AP world ships no item unlock items. |
| `box_gating_enabled`             | 0 or 1 | 1 = gated. 0 = all box types unlocked at connect; AP world ships no box unlock items. |
| `airride_stage_gating_enabled`   | 0 or 1 | 1 = gated. 0 = all Air Ride stages unlocked at connect; AP world ships no AR stage unlock items. |
| `topride_stage_gating_enabled`   | 0 or 1 | 1 = gated. 0 = all Top Ride courses unlocked at connect; AP world ships no TR stage unlock items. |
| `topride_item_gating_enabled`    | 0 or 1 | 1 = gated. 0 = all Top Ride items unlocked at connect; AP world ships no TR item unlock items. (Ability-gated TR items remain gated by `ability_gating_enabled`.) |
| `color_gating_enabled`           | 0 or 1 | 1 = gated. 0 = all Kirby colors unlocked at connect; AP world ships no color unlock items. |
| `stadium_gating_enabled`         | 0 or 1 | 1 = gated. 0 = all stadiums unlocked at connect; AP world ships no stadium unlock items. (The KAROptions toggle `city_trial_stadiums_gated` maps directly to this.) |
| `checklist_rewards_gating_enabled` | 0 or 1 | 1 = gated (default): each non-progression checklist reward (music, sound test, extra rules, endings, filler boxes, …) is an AP item the player finds. 0 = ungated: the mod marks every such reward received at connect (`ChecklistRewards_GrantAllCosmetic`, tracked via `received_checklist_rewards` — **not** a mask), and the AP world ships none. The 6 Dragoon/Hydra part markers are progression and are **not** affected by this flag. |

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

### goal_checks Layout (GOAL_CHECKLIST_LIST)

When a mode's goal is `GOAL_CHECKLIST_LIST`, the required checkboxes are specified in `goal_checks[mode][2]` (2 × u64 = 128 bits per mode). Same encoding as `sent_checks`: bit `(k % 64)` of word `(k / 64)` for clear_kind `k`. The goal is satisfied when every set bit in `goal_checks[mode]` is also set in `sent_checks[mode]`.

Offsets relative to `APData` struct base:

| Offset | Field | Description |
|--------|-------|-------------|
| 0x068  | `goal_checks[0][0..1]` | Air Ride required checkboxes (2 × u64, clear_kinds 0-119) |
| 0x078  | `goal_checks[1][0..1]` | Top Ride required checkboxes (2 × u64, clear_kinds 0-119) |
| 0x088  | `goal_checks[2][0..1]` | City Trial required checkboxes (2 × u64, clear_kinds 0-119) |

Client writes big-endian u64s (dolphin-memory-engine handles byte order). Zero-fill any mode that does not use `GOAL_CHECKLIST_LIST`. Fillers are blocked on goal-list checkboxes to prevent cheesing.

### KAROptions.py to APSlotOptions Mapping

The client reads slot options from the AP server (as defined in `KAROptions.py`) and writes the corresponding values to `APSlotOptions`. Most fields map directly by name. Notable differences:

| KAROptions.py Field | APSlotOptions Field | Notes |
|---------------------|---------------------|-------|
| `death_link` (DeathLinkMixin) | `death_link_enabled` | Toggle, direct value |
| `energy_link` | `energy_link_enabled` | Toggle, direct value |
| `trap_link` | `trap_link_enabled` | Toggle, direct value |
| `reveal_checklists` | `reveal_checklists` | Toggle, direct value |
| `city_trial_goal` | `goal[GMMODE_CITYTRIAL]` | **TextChoice -> GoalKind conversion** (see enum table) |
| `city_trial_checklist_amount` | `checklist_amount[GMMODE_CITYTRIAL]` | Range, direct value |
| `city_trial_progressive_patch_caps` | `city_trial_progressive_patch_caps` | Toggle, direct value |
| `city_trial_patch_cap_amount` | `city_trial_patch_cap_amount` | Range, direct value |
| `city_trial_stadiums_gated` | `stadium_gating_enabled` | Same semantic (1 = gated/items-required, 0 = ungated/all unlocked). |
| `checklist_rewards_gated` | `checklist_rewards_gating_enabled` | Toggle, direct value (1 = each cosmetic reward is an AP item; 0 = mod pre-grants them all at connect) |
| `spawn_rate_min` | `spawn_rate_min` | Range (percent), direct value. AP world also has `spawn_rate_max` for item-count generation but does not write it to the mod. |
| `air_ride_goal` | `goal[GMMODE_AIRRIDE]` | **TextChoice -> GoalKind conversion** |
| `air_ride_checklist_amount` | `checklist_amount[GMMODE_AIRRIDE]` | Range, direct value |
| `top_ride_goal` | `goal[GMMODE_TOPRIDE]` | **TextChoice -> GoalKind conversion** |
| `top_ride_checklist_amount` | `checklist_amount[GMMODE_TOPRIDE]` | Range, direct value |
| `*_goal_locations` | `goal_checks[mode][]` | **List of checkbox names → u64[2] bitmask.** Client converts each location name to `(mode, clear_kind)` via checklist-mappings.csv, then sets bit `(k % 64)` in word `(k / 64)`. |

Per-category gating toggles (one slot option per `APUnlockCategory`). Each KAROptions.py progression toggle (true = gated, false = ungated) maps to the matching `*_gating_enabled` field. When `0`, the mod pre-fills that category's unlock mask at connect and the AP world must not generate unlock items for it.

| KAROptions.py Field | APSlotOptions Field |
|---------------------|---------------------|
| `machines_gated`                  | `machine_gating_enabled` |
| `abilities_gated`                 | `ability_gating_enabled` |
| `city_trial_events_gated`         | `event_gating_enabled` |
| `city_trial_patches_gated`        | `patch_gating_enabled` |
| `city_trial_items_gated`          | `item_gating_enabled` |
| `city_trial_boxes_gated`          | `box_gating_enabled` |
| `air_ride_courses_gated`          | `airride_stage_gating_enabled` |
| `top_ride_courses_gated`          | `topride_stage_gating_enabled` |
| `top_ride_items_gated`            | `topride_item_gating_enabled` |
| `colors_gated`                    | `color_gating_enabled` |
| `city_trial_stadiums_gated`       | `stadium_gating_enabled` |

`checklist_rewards_gated` → `checklist_rewards_gating_enabled` follows the same true=gated convention, but is **not** a mask-backed `APUnlockCategory`: when ungated the mod pre-grants the cosmetic rewards via `ChecklistRewards_GrantAllCosmetic` rather than filling a bitmask.

Options **not written to the mod** (used at AP generation time, or carried only in `slot_data` for the client's own logic): `trap_chance`, `spawn_rate_max`, `city_trial_permanent_patches` (gen-time only — controls whether permanent-patch items enter the pool; the mod has no corresponding slot field and always treats permanent patches as an active item category), and the per-mode `air_ride_checkbox_fillers` / `top_ride_checkbox_fillers` / `city_trial_checkbox_fillers` fields. (`trap_chance` is present in `slot_data` for the client's trap-roll logic but is not written into `APSlotOptions`.)

### Menu Toggle Interaction

The `death_link_enabled`, `energy_link_enabled`, and `trap_link_enabled` options set the **initial** values of the in-game menu toggles (on first connection only). The player can override them locally via the Settings menu at any time. The menu toggle is the authoritative source for whether each feature is active.

The current menu state is exposed live via `deathlink_menu_enabled`, `energylink_menu_enabled`, and `traplink_menu_enabled` in `APData` (see Menu Toggle State above). The client should watch those fields and forward changes to the AP server so server-side membership (DeathLink tag, TrapLink/EnergyLink participation) tracks the player's current choice.

## Protocol Rules

Almost every shared field follows one rule: **exactly one side writes, the other side reads and clears.** Aligned 32-bit reads and writes are atomic on the GameCube's PowerPC processor, so no locking is needed for 32-bit fields. 64-bit fields are not atomic; see the APData Layout note for the torn-read risk on `energy_balance` / `energy_sent_total`.

The one exception is `energy_sent_total` (see EnergyLink below): it is a **single-writer cumulative counter**, not a mailbox. The game owns it and only ever adds/subtracts; the client only ever **reads and diffs** it (never writes, never clears).

### General Pattern

**Flag fields** (`deathlink_receive`, `deathlink_send`, `traplink_receive`, `traplink_send`):
1. Writer sets the field to `1`
2. Reader sees `1`, acts on it, then writes `0`
3. Writer waits for `0` before writing `1` again

`deathlink_receive` is an exception: the client writes `1` on every incoming DeathLink bounce without waiting for the game to clear the previous one. Concurrent deaths (a second bounce arriving while the game is still applying the previous one) collapse to a single kill — Kirby can't die-while-dying, so dropping the second event matches the observable game behavior.

**Mailbox fields** (`incoming_item_id`):
1. Writer writes a non-zero value
2. Reader sees a non-zero value, processes it, then writes `0`
3. Writer waits for `0` before writing the next value

**Cumulative counter** (`energy_sent_total`) — does *not* follow the mailbox pattern:
1. Game writes the running net total freely (no slot check, no "is it free?" gate)
2. Client reads once per poll, forwards `current − last_seen` to the server, advances `last_seen`
3. Client never writes the field. See EnergyLink below for seeding/reset rules.

## Item Delivery

### Overview

Item receipt and application are decoupled. When the game reads an item from the mailbox, it immediately acknowledges receipt by incrementing `item_received_index` and adds the item to an unprocessed list in save data. Items are applied from the unprocessed list when their conditions are met, and items that can't apply yet (e.g., an event while another event is active) are skipped so that items behind them can still process.

### Client Responsibilities

1. **On connect/reconnect**: Read `item_received_index` from `APData`. Skip all items with index < this value (already received by the game).
2. **For each new item from the AP server**:
   - Wait until `incoming_item_id == 0`
   - Write the AP item ID to `incoming_item_id`
3. **That's it.** The game handles storage, scene-gating, and application.

### Game Responsibilities

1. **Every frame**: Check `incoming_item_id`. If non-zero:
   - Add the item ID to the `unprocessed_items` list in save data
   - Increment `item_received_count` and sync it to `item_received_index` in shared memory
   - Clear the mailbox to `0`
   - **Exception — queue full:** if `unprocessed_items` is already at capacity (`MAX_RECEIVED_ITEMS`), the game does **not** clear the mailbox and does **not** increment `item_received_count`. It leaves `incoming_item_id` set so the same item is retried each frame as the list drains. This is the protocol's backpressure: because the client gates its next write on `incoming_item_id == 0` and only advances its send cursor after a successful write, holding the value stalls the client safely. Clearing it would lose the item permanently — the client has already advanced past it and `item_received_count` was never bumped for it.
2. **Every frame**: Scan the `unprocessed_items` list and attempt to apply the first item whose conditions are met:
   - Scene-independent items (checkbox fillers, patch cap increase, checklist rewards, stadium unlocks) apply immediately.
   - All other items require the game to be in a 3D scene (`MJRKIND_CITY`, `MJRKIND_AIR`, or `MJRKIND_TOP`) with the intro countdown finished (`GmIntroState == GMINTRO_END`).
   - If an item can't apply (e.g., event blocked), it is skipped and the next item is tried.
3. **On successful application**: Remove the item from the `unprocessed_items` list.

### AP Item ID Map

These IDs must match between the APWorld Python code and the game mod (defined as the `APItemId` enum in `main.h`). IDs use spaced ranges to allow future additions without shifting values.

**Standalone items (1-99):**

| ID  | Enum Name                  | Game Behavior |
|-----|----------------------------|---------------|
| 1   | `AP_ITEM_CHECKBOX_FILLER_AIRRIDE` | `Checklist_GrantFiller(GMMODE_AIRRIDE)` — grants a checkbox filler for Air Ride |
| 2   | `AP_ITEM_CHECKBOX_FILLER_TOPRIDE` | `Checklist_GrantFiller(GMMODE_TOPRIDE)` — grants a checkbox filler for Top Ride |
| 3   | `AP_ITEM_CHECKBOX_FILLER_CITYTRIAL` | `Checklist_GrantFiller(GMMODE_CITYTRIAL)` — grants a checkbox filler for City Trial |
| 4   | `AP_ITEM_PATCH_CAP_INCREASE` | `PatchCap_Increment()` — raises the patch cap by 1 |
| 5   | `AP_ITEM_1_HP_TRAP`        | Damage every human player's machine to 1 HP |
| 6   | `AP_ITEM_ALL_UP`           | `Patch_AllUp_GiveItem(+1)` — raise every stat by 1 for each human player |
| 7   | `AP_ITEM_PERM_PATCH_ALL_UP`| `PermanentPatch_GiveAllUp()` — permanent +1 to all stats, increments every `ap_save->permanent_patches[]` slot |
| 8   | `AP_ITEM_ALL_DOWN`         | `Patch_AllUp_GiveItem(-1)` — drop every stat by 1 for each human player |
| 9   | `AP_ITEM_GIVE_DRAGOON`     | `GateMachines_GiveLegendaryMachine(0)` — assembles the Dragoon for the player (cinematic legendary-machine grant, not the three individual parts) |
| 10  | `AP_ITEM_GIVE_HYDRA`       | `GateMachines_GiveLegendaryMachine(1)` — assembles the Hydra for the player (cinematic legendary-machine grant, not the three individual parts) |
| 11  | `AP_ITEM_SPAWN_RATE_UP`    | `SpawnRate_Increment()` — adds +10% to the CT/TR item spawn rate scale (capped at 5×) |
| 12  | `AP_ITEM_DROP_PATCHES_TRAP`| `Patch_DropTrap()` — ejects every human rider's equipped stat patches behind the machine (CT only) |

**Permanent +1 patches (100-108, aligned to PatchKind):**

| ID  | Enum Name             | Game Behavior |
|-----|-----------------------|---------------|
| 100 | `AP_PERM_PATCH_WEIGHT`   | `PermanentPatch_GiveItem(PATCHKIND_WEIGHT)` — increments `ap_save->permanent_patches[PATCHKIND_WEIGHT]` |
| 101 | `AP_PERM_PATCH_BOOST`    | `PermanentPatch_GiveItem(PATCHKIND_ACCEL)` |
| 102 | `AP_PERM_PATCH_TOPSPEED` | `PermanentPatch_GiveItem(PATCHKIND_TOPSPEED)` |
| 103 | `AP_PERM_PATCH_TURN`     | `PermanentPatch_GiveItem(PATCHKIND_TURN)` |
| 104 | `AP_PERM_PATCH_CHARGE`   | `PermanentPatch_GiveItem(PATCHKIND_CHARGE)` |
| 105 | `AP_PERM_PATCH_GLIDE`    | `PermanentPatch_GiveItem(PATCHKIND_GLIDE)` |
| 106 | `AP_PERM_PATCH_OFFENSE`  | `PermanentPatch_GiveItem(PATCHKIND_OFFENSE)` |
| 107 | `AP_PERM_PATCH_DEFENSE`  | `PermanentPatch_GiveItem(PATCHKIND_DEFENSE)` |
| 108 | `AP_PERM_PATCH_HP`       | `PermanentPatch_GiveItem(PATCHKIND_HP)` |

**City Trial events (200-215, aligned to EventKind):**

| ID  | Enum Name                  | Game Behavior |
|-----|----------------------------|---------------|
| 200 | `AP_EVENT_DYNABLADE`       | `Event_GiveItem(EVKIND_DYNABLADE)` |
| 201 | `AP_EVENT_TAC`             | `Event_GiveItem(EVKIND_TAC)` |
| 202 | `AP_EVENT_METEOR`          | `Event_GiveItem(EVKIND_METEOR)` |
| 203 | `AP_EVENT_PILLAR`          | `Event_GiveItem(EVKIND_PILLAR)` |
| 204 | `AP_EVENT_RUNAMOK`         | `Event_GiveItem(EVKIND_RUNAMOK)` |
| 205 | `AP_EVENT_RESTORATIONAREA` | `Event_GiveItem(EVKIND_RESTORATIONAREA)` |
| 206 | `AP_EVENT_RAILFIRE`        | `Event_GiveItem(EVKIND_RAILFIRE)` |
| 207 | `AP_EVENT_SAMEITEM`        | `Event_GiveItem(EVKIND_SAMEITEM)` |
| 208 | `AP_EVENT_LIGHTHOUSE`      | `Event_GiveItem(EVKIND_LIGHTHOUSE)` |
| 209 | `AP_EVENT_SECRETCHAMBER`   | `Event_GiveItem(EVKIND_SECRETCHAMBER)` |
| 210 | `AP_EVENT_PREDICTION`      | `Event_GiveItem(EVKIND_PREDICTION)` |
| 211 | `AP_EVENT_MACHINEFORMATION`| `Event_GiveItem(EVKIND_MACHINEFORMATION)` |
| 212 | `AP_EVENT_UFO`             | `Event_GiveItem(EVKIND_UFO)` |
| 213 | `AP_EVENT_BOUNCE`          | `Event_GiveItem(EVKIND_BOUNCE)` |
| 214 | `AP_EVENT_FOG`             | `Event_GiveItem(EVKIND_FOG)` |
| 215 | `AP_EVENT_FAKEPOWERUPS`    | `Event_GiveItem(EVKIND_FAKEPOWERUPS)` |

**Direct game items (300+, aligned to ItemKind):**

AP item ID = `300 + ItemKind` value. The game spawns the item at all human players' locations via `SpawnItemHumans` (which loops human players and calls `SpawnItemPlayer`, in `externals/hoshi/include/inline.h`). For non-`*FAKE` kinds, `Machine_OnTouchItem` is invoked immediately so the pickup applies the same frame. `ITKIND_*FAKE` kinds (`ITKIND_ACCELFAKE`–`ITKIND_WEIGHTFAKE`) are left for next-frame natural collision instead — manually invoking `Machine_OnTouchItem` outside the per-frame collision pipeline would write a hit-coll log entry that the next `HitColl_Init` clears before `HitColl_ActOnCollision` runs, so the fake-patch effect would silently drop.

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

AP item ID = `400 + StadiumKind` value. Unlocks the corresponding stadium.

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

**Checklist reward items (500-649, encoded as `base + mode*50 + reward_index`):**

These are the rewards from the game's three checklists (machines, characters, music, etc.) treated as AP items. They can exist in any world in the multiworld.

| ID Range | Mode | Reward Indices | Description |
|----------|------|----------------|-------------|
| 500-545  | Air Ride (0)   | 0-45 | 46 rewards (machines, colors, music, sound tests, course, etc.) |
| 550-582  | Top Ride (1)   | 0-32 | 33 rewards (extra rules, items, colors, music, sound tests, etc.) |
| 600-643  | City Trial (2) | 0-43 | 44 rewards (legendary machines, stadiums, colors, music, etc.) |

Decoding: `mode = (id - 500) / 50`, `reward_index = (id - 500) % 50`. Apply via `ChecklistRewards_Grant(mode, reward_index)`.

**Access gating unlock items (700-921):**

These items unlock gated game features. Each category uses a bitmask in save data. See the `gate_*.c` files for implementation details.

| ID Range | Base | Enum Prefix | Category | Count | Save Field |
|----------|------|-------------|----------|-------|------------|
| 700-715 | 700 | `AP_EVENT_UNLOCK_` | City Trial events (aligned to EventKind) | 16 | `event_unlocked_mask` |
| 760-770 | 760 | `AP_ABILITY_UNLOCK_` | Copy abilities (aligned to CopyKind) | 11 | `ability_unlocked_mask` |
| 780-788 | 780 | `AP_PATCH_UNLOCK_` | Patch types (aligned to PatchKind) | 9 | `patch_unlocked_mask` |
| 790-819 | 790 | `AP_ITEM_UNLOCK_` | Item groups (aligned to ItemUnlockKind, `ITUNLOCK_NUM` = 30) | 30 | `item_unlocked_mask` |
| 830-854 | 830 | `AP_MACHINE_UNLOCK_` | Machines (aligned to VCKIND, contiguous — see note) | 25 | `machine_unlocked_mask` |
| 860-862 | 860 | `AP_BOX_UNLOCK_` | Box types (Blue, Green, Red) | 3 | `box_unlocked_mask` |
| 870-878 | 870 | `AP_STAGE_UNLOCK_AIRRIDE_` | Air Ride stages | 9 | `airride_stage_unlocked_mask` |
| 880-887 | 880 | `AP_COLOR_UNLOCK_` | Kirby colors (8 IDs aligned to KirbyColor; Pink/880 is the always-unlocked default — its unlock item is still generated but is a no-op in-game) | 8 | `color_unlocked_mask` |
| 890-896 | 890 | `AP_STAGE_UNLOCK_TOPRIDE_` | Top Ride courses | 7 | `topride_stage_unlocked_mask` |
| 900-921 | 900 | `AP_TOPRIDE_ITEM_UNLOCK_` | Top Ride items (22 indices; 4 ability-gated + 1 engine duplicate excluded — see below) | 17 | `topride_item_unlocked_mask` |

**Top Ride item note:** Of the 22 `TopRideItemKind` indices, 5 are excluded from AP generation (IDs 909, 911, 912, 913, 916), leaving 17:

- **4 are ability-gated** — these spawn automatically when the matching copy ability is unlocked, so they're driven by `ability_unlocked_mask`, not `topride_item_unlocked_mask`. Do not generate AP items for them (the `ability_items[]` table in `gate_topride_items.c`):

  | Index | Engine item | Gating ability |
  |-------|-------------|----------------|
  | 9  | `TRITEM_FREEZE_FAN` | `COPYKIND_FREEZE` |
  | 11 | `TRITEM_FIRE`       | `COPYKIND_FIRE` |
  | 13 | `TRITEM_BOMB`       | `COPYKIND_BOMB` |
  | 16 | `TRITEM_WALKY`      | `COPYKIND_MIC` |

- **1 is an engine duplicate** — index 12 is `TRITEM_PARTY_BALL_ALT` (the KirbyKusdama Party Ball variant). AP exposes only one Party Ball, at index 21 (`TRITEM_PARTY_BALL`); the mod mirrors bit 21's unlock onto bit 12 so both spawn together, and **AP never sends ID 912 directly**. This is *not* an ability gate.

(There is no Needle Top Ride item — `COPYKIND_NEEDLE` exists as a copy ability but has no corresponding `TRITEM_*`.)

**Top Ride item gives (950-971, aligned to TopRideItemKind):**

AP item ID = `950 + TopRideItemKind` value. Applies the matching Top Ride item directly to every human Kirby via `GateTopRideItems_GiveItem` → `TopRide_KirbyApplyItem` (a direct apply, not a position spawn + next-frame collision). Only effective inside an active Top Ride round; queued by the unprocessed-items list and retried until then — `GateTopRideItems_GiveItem` returns retry whenever there is no Top Ride `KirbyMgr` or the round isn't running (`round_state != 2`), rather than checking the scene major (Top Ride uses `MNRKIND_19`, so this give path is handled above the generic 3D-scene gate).

| ID Range | Items |
|----------|-------|
| 950-971 | Hammer, Big Cake, Speed Up, Speed Down, Spinner, Charge Tank, Invincible Candy, Buzz Saw, Drill, Freeze Fan, Missile, Fire, Party Ball (alt), Bomb, Step-Boom, Lantern, Walky, Kracko, Who? Paint, Smokescreen, Chickie, Party Ball |

**Machine unlock note:** The range covers VCKINDs 0–24 (IDs 830–854). VCKIND 25 (WHEELVSDEDEDE) is the Vs. King Dedede stadium's CPU-only machine — it is omitted from the AP item range entirely; ID 855 is rejected by the mod handler and is not a valid machine unlock.

The AP world (`worlds/kirby_air_ride/KARItems.py`) generates an unlock item for **all 25 in-range IDs (830–854)** as `progression`; only 855 is excluded. Two caveats for modders:

- **Top Ride machines are live gates, not placeholders:** 845 (FREE) and 846 (STEER) are read by the mod's Top Ride lobby gating (`GateMachines_TRLobbyCanStart` / `IsTRMachineUnlocked`, in `gate_machines.c`). The mod **hard-blocks** starting a Top Ride race unless at least one is unlocked. In the apworld they're tagged `source_modes=_TR` (they're Top Ride control machines — they don't spawn in City Trial via `CT_SPAWN_EXCLUDED_MASK`, and aren't Air Ride machines), and a **guaranteed Top Ride machine starter** (one of Free/Steer, precollected when `machines_gated` + Top Ride) keeps the gate satisfiable in every seed config. When `machine_gating_enabled == 0`, the mod sets the whole machine mask (`(1u << VCKIND_NUM) - 1`, bits 0–25 incl. Free/Steer) at connect, so the Top Ride lobby is freely startable.
- **A few bits are cosmetic in-game:** 847 (WINGKIRBY), 849 (WHEELNORMAL), 850 (WHEELKIRBY) are set by their unlock items but read by no game code — no character rides them in player-controlled contexts, and they are force-excluded from City Trial spawns (`CT_SPAWN_EXCLUDED_MASK`). The items still exist for AP logic, but the grant has no in-game effect. The canonical Dedede unlock is 854 (WHEELDEDEDE), which is what `CharacterDesc[CKIND_DEDEDE]` resolves to.

> **Resolved (was Q3, apworld side):** Free (845) / Steer (846) are now tagged `source_modes=_TR` in `KARItems.py` (they're Top Ride machines, not `_AR_CT`). Because the mod hard-gates the Top Ride lobby on Free/Steer and AP logic doesn't model that gate, the apworld also precollects a **guaranteed Top Ride machine starter** (one of Free/Steer when `machines_gated` + Top Ride) so the `_TR`-confined unlocks always sit on reachable Top Ride locations — no circular placement, no Top-Ride-only softlock. Free/Steer are also excluded from the AR/CT machine starter pool (they can't be ridden there). See `doc-review-tracking.md`.

## Location Data

### Concept

In Archipelago, each checkbox in the game's three checklists is a **location**. The multiworld generator decides what *item* is placed at each location. From this slot's perspective:

- **Local vanilla rewards** — one of this slot's vanilla checklist rewards (machines, colors, music, etc., AP IDs `500..649`). The multiworld can place any vanilla reward on any cell in any mode. The shared-memory protocol carries exactly this mapping: source reward `(src_mode, src_ri)` → destination cell `(target_mode, clear_kind)`. When source mode ≠ target mode, that's a **cross-mode** placement.
- **Local non-vanilla items** — anything else this slot owns (fillers, traps, gating unlocks, permanent patches). These are delivered to the player when the cell is checked, via the standard `incoming_item_id` mailbox flow. They are **not** part of `locations`; the checklist UI has no advance notice that the cell carries one.
- **Remote items** — items owned by another slot. Earning the cell sends a location check to the AP server, which routes the item to its owner. Locally the player gets nothing and the cell is treated as not having a reward.

The `locations` array carries vanilla placement only. The mod's checklist visuals (reward star icon, cross-mode rendering) are driven entirely by this array; non-vanilla local items and remote items have no UI presence until the player completes the cell.

### Client Responsibilities

1. **After connecting to the AP server**, scout every AP location belonging to this slot. For each scout result whose `item` falls in `500..649`, decode it as a vanilla reward:
   - `source_mode = (item - 500) / 50`
   - `source_reward_index = (item - 500) % 50`
   - `(target_mode, clear_kind) = ` look up the location code in `checklist-mappings.csv`
2. **Populate `locations[source_mode][source_reward_index]`** with `(target_mode << 8) | clear_kind`. Default unset entries to `0xFFFF` (the reward is owned by another slot, or the reward_index isn't used in this mode).
3. **Set `location_data_valid = 1`** after the array is written.
4. **Write location data on every connection.** The game re-applies it each time.

### Game Responsibilities

1. **Every frame**: Check `location_data_valid`. When `1`, call `ChecklistRewards_ApplyLocations()`:
   - Copy `ap_data->locations` into `ap_save->shuffled_rewards` (the canonical persisted form).
   - Rebuild derived state by walking each `(source_mode, reward_index)`:
     - `0xFFFF` → remote/unused. Set `stc_reward_table_ptrs[source_mode][i].clear_kind = 0` (sentinel; every vanilla read of this field is gated on `shuffled_rewards != 0xFFFF`).
     - `target_mode == source_mode` → same-mode local. Write `clear_kind` into `stc_reward_table_ptrs[source_mode][i].clear_kind` so the vanilla checklist scan finds it.
     - `target_mode != source_mode` → cross-mode local. Leave the source's `clear_kind` at the `0` sentinel, and populate `cross_mode_slots[target_mode][clear_kind]` with `(source_mode, reward_index)` for the cross-mode display hooks.
   - Re-grant any already-received items so their local checklist slots are correctly marked (eager `has_reward` on whatever cells they map to under the new shuffle).
   - Clear `location_data_valid` to `0` and persist save.
2. **On subsequent boots**: `ChecklistRewards_OnSaveLoaded` rebuilds the same derived state from `shuffled_rewards`, so the game works correctly even before the client reconnects and re-sends.

### How Remote and Cross-Mode Cells Are Hidden

Remote rewards and cross-mode source rows never read or write a same-mode placement:
- `ChecklistRewards_ShouldSkipReward` skips reward indices whose `shuffled_rewards[src_mode][src_ri]` is `0xFFFF` or whose target mode differs from the current mode (see `IsSameModeLocalPlacement`), so vanilla never sets `has_reward` on a non-existent same-mode placement.
- `ChecklistRewards_CheckUnlocked` (replacing `ClearChecker_CheckUnlocked`) gates on the same predicate — returns 0 for remote without touching `clear[]`.
- The `clear_kind = 0` sentinel in `RewardEntry.clear_kind` is used because any value `>= 120` (the size of `clear[]`, including the natural OOB choice `0xFF`) trips the vanilla OOB assert at `0x8004a08c` when a vanilla code path uses `clear_kind` as an array index. `0` is the smallest in-range value and is safe so long as every vanilla read of `RewardEntry.clear_kind` is gated on `shuffled_rewards != 0xFFFF`.

### Cross-Mode Reward Display

Vanilla rewards can be shuffled across modes (e.g., an Air Ride reward appearing on a City Trial checkbox). `cross_mode_slots[3][120]` maps `(target_mode, clear_kind)` → `(source_mode, source_reward_index)`, populated during the per-cell rebuild above. All three checklist SIS files are loaded simultaneously so reward text and icons from any mode can be displayed. See `checklist_rewards.c` for implementation.

### How Local Rewards Are Displayed

Local vanilla rewards work through the vanilla checklist flow:
1. The reward's `clear_kind` (in `RewardEntry`) points to a specific checkbox.
2. When the player completes that checkbox's objective, `Checklist_SetRewardFlagOnUnlocks` sets `has_reward` on it (our hook allows this for same-mode local rewards; the post-loop hook handles cross-mode).
3. The star icon appears on the checkbox, and `ClearChecker_CheckUnlocked` returns 1, unlocking the corresponding game feature (machine, color, music, etc.).

Local non-vanilla items (fillers, traps, gating unlocks) are not part of the `locations` array. They arrive via the standard `incoming_item_id` mailbox when the player earns the cell, and the mod applies their effects then. The cell carries no advance "this has a reward" marker — visually it looks the same as a remote or empty cell until completion.

## Location Checking (Sending)

When a player completes a checkbox, that location needs to be reported to the AP server so the item placed there can be delivered to whoever owns it.

The mod is the source of truth: it owns a `sent_checks[3][2]` bitmask in shared memory and in save data. The client polls the bitmask, diffs against last-seen state, and sends new checks to the AP server. **The client never reads `GameClearData.clear[]` directly.**

### Client Responsibilities

1. **On connect**:
   - Read `sent_checks[3][2]` and `goal_complete` from `APData`.
   - For each set bit in `sent_checks`, decode `(mode, clear_kind)` and look up the AP location code via the per-world checklist mappings (see `docs/checklist-mappings.csv`). Send each as a location check (the AP server dedupes against its existing record).
   - If `goal_complete == 1`, send the victory message.
   - Compute the diff between AP server's `checked_locations` for this slot and the bits that are set in `sent_checks`. For any locations the server knows about that the mod does not (e.g., fresh save / slot takeover), write those bits into `client_backfill` so the mod can mark them as completed locally.

2. **Steady state (poll cadence is flexible — 1 Hz is fine)**:
   - Read `sent_checks[3][2]` and diff against the last-known state. For each newly set bit, send the corresponding location check.
   - Read `goal_complete`. If newly set, forward victory.
   - Watch the AP server's `RoomUpdate` events for `checked_locations` updates (e.g., from `!collect`). For any new entries the mod doesn't know about, write them into `client_backfill`.

3. **Decoding `sent_checks` bits**: For mode `m` and clear_kind `k`, the bit is `sent_checks[m][k / 64] & (1 << (k % 64))`.

### Game Responsibilities

1. **On gameplay completion**: Mod replaces `ClearChecker_SetNewUnlock` (`0x8004A054`) with a wrapper that detects the moment of transition and writes the bit into `ap_save->sent_checks` and `ap_data->sent_checks`. As a whole-function replacement it intercepts every caller of `ClearChecker_SetNewUnlock` automatically (AR/CT/TR objectives, stadium results, free run, etc.). **Manual filler placement does NOT route through this function** — it is caught by a separate hook at `0x80180dc4` (the vanilla filler store site).
2. **Meta auto-unlock hooks**: Five "meta" checkboxes bypass `SetNewUnlock` (vanilla sets them via direct stores inside `Checklist_ProcessUnlock`). The mod hooks each of the 5 store sites directly — it does **not** poll per frame — and forwards `is_unlocked` transitions:
   - AR `0x18`, TR `0x77`, CT `0x37` — the native "Fill in over 100 Checklist blocks!" cells.
   - CT `0x6D` (Dragoon parts), CT `0x6E` (Hydra parts) — the part-unlock checklist cells that auto-complete when the corresponding part rewards are received. **These are distinct from the Hydra-and-Dragoon goal cell (CT `0x77`)** — see Goal Evaluation below.
3. **Backfill processing**: Each frame (`CheckDetection_OnFrameStart` → `ProcessBackfill`), the mod ORs `client_backfill` bits into `sent_checks`, sets `clear[].is_unlocked` and `clear[].is_visible` for visual consistency, sets `has_reward` if a local AP placement exists for that checkbox AND the source item has been received, re-evaluates the goal, and clears `client_backfill`.
4. **Goal evaluation**: After every check transition (and on save load), the mod evaluates the active goal condition and sets `goal_complete = 1` if satisfied. Sticky and persisted across reboots.

### Goal Evaluation (Mod-Side)

The mod evaluates the goal condition mod-side using `ap_save->options` (the slot options copied from `APSlotOptions` at handshake). All goal types reduce to bit reads on `sent_checks`:

| Goal | Detection |
|------|-----------|
| `GOAL_NONE` | Vacuously satisfied for that mode |
| `GOAL_100_CHECKLIST` | Single bit: the native "Fill in over 100 Checklist blocks!" cell (AR `0x18` / TR `0x77` / CT `0x37`) set in `sent_checks[mode]`. **Not** a popcount — it reads the same vanilla cell the meta-unlock hook sets. |
| `GOAL_N_CHECKLIST` | `popcount(sent_checks[mode]) >= N` (from `checklist_amount[mode]`) |
| `GOAL_HYDRA_AND_DRAGOON` | Single bit `0x77` set in `sent_checks[CT]` (the native "complete both Dragoon and Hydra in one match" cell). **Not** bits `0x6D`/`0x6E` — those are the separate part-unlock cells. |
| `GOAL_BEAT_KING_DEDEDE` | Bit `0x2F` set in `sent_checks[CT]` |
| `GOAL_CHECKLIST_LIST` | `(sent_checks[mode] & goal_checks[mode]) == goal_checks[mode]` — all required bits set |
| `GOAL_MAX_STATS_CT` | Sticky save bit `max_stats_ct_achieved`; set when any human player's 9 CT stats simultaneously reach the runtime patch-cap target (`city_trial_patch_cap_amount`, 1–127 — **not** `PATCH_STAT_MAX`, which is the absolute clamp ceiling of 127) during a `CITYMODE_TRIAL` round. Stadium and Free Run do not count. With `city_trial_progressive_patch_caps` enabled, the player must receive enough Patch Cap Increase items to make the target reachable. |

Victory fires only if at least one mode has a non-NONE goal AND every mode's goal is satisfied. Mode goals are independent — set `*_goal = GOAL_NONE` for modes that should not contribute to victory.

### Notes on Collect / Release

- **`!release`**: Items destined for this player arrive via the standard `incoming_item_id` mailbox. The mod's `Grant()` sets `has_reward` on the local placement (if any). No effect on `sent_checks`.
- **`!collect`**: Other player pulls their items from this player's world. The AP server marks those locations as checked. The client should detect this via `RoomUpdate.checked_locations`, write the affected bits into `client_backfill`, and the mod will mark them as completed locally and re-evaluate the goal. This may trigger `goal_complete` passively, which matches standard AP semantics.

## DeathLink

### Client Responsibilities

- **Sending death**: Watch `deathlink_send`. When it becomes `1`, send a DeathLink bounce to the AP server, then clear it to `0`.
- **Receiving death**: When a DeathLink bounce arrives from the AP server, write `1` to `deathlink_receive` immediately. No need to wait for the game to clear the previous flag — if it's still `1`, writing `1` again is a no-op and the second bounce collapses into the in-progress death (Kirby can't die-while-dying). Skip the write if Dolphin isn't currently hooked; bounces that arrive during disconnect are dropped rather than queued, to avoid a flood on reconnect.

### Game Responsibilities

- **Detecting death**: Three code hooks set `deathlink_send = 1` when any human player dies — one inside `Rider_CheckToDieOnMachine` (`0x801a06d0`) for HP-zero deaths, one inside `Machine_SetFallDead` (`0x801e6540`) for fall-off-course deaths, and one in the Top Ride sand-pit death path (`0x80331a94`).
- **Applying death (3D modes)**: A per-frame GOBJ checks `deathlink_receive` (gated on `GmIntroState == GMINTRO_END`). When `1`, it kills every human player using the right mechanism for the current mode — HP-zeroing in City Trial / Destruction Derby / Melee / Vs. King Dedede, fall-off-course death (via `Machine_SetFallDead` at the player's current checkpoint) in Air Ride and the racing stadiums. Clears the flag to `0` after.
- **Top Ride is active**: Top Ride has its own DeathLink path (`DeathLink_OnTopRideLoad` installs `DeathLink_TopRidePerFrame`). On receive it applies a random damage-class Kirby state (Press / Freeze / Numb / Confuse) to every human Kirby and clears the flag; on the sand-pit death hook it sends. The TR receive path is **not** gated on `GmIntroState` (Top Ride has no intro countdown).

## EnergyLink

### Units

The AP server stores the EnergyLink pool in **integer Joules**. The mod side stores `energy_balance` and `energy_sent_total` in **raw MJ units** (`ENERGY_LINK_EXCHANGE_RATE = 1_000_000`). The client scales: read MJ from the mod and multiply by 1,000,000 to get the Joules value to send to the server; divide AP pool Joules by 1,000,000 to get raw MJ to write back to the mod. All values sent to the server MUST be integers — the AP data-storage protocol does not accept floats.

### Client Responsibilities

- **Processing sends**: `energy_sent_total` (signed s64, raw MJ) is a **game-owned cumulative counter** — read-and-diff it, never write it. Handle this **before** updating the balance (below); the ordering is what closes the overdraw window.
  - **Seeding / restart detection**: maintain a `last_seen` watermark, **re-seeded** (record the current value, apply nothing) whenever a fresh game session starts — on connect, on a struct-pointer change at `0x805d52d4`, and on a `game_ready` 1→0 transition. The mod sets `game_ready` once in `OnBoot` and never clears it during play, so the reboot `memset` zeroing it (observing 0 after 1) is the restart signal. Do **not** persist `last_seen` across sessions — the counter resets to 0 each boot, so a persisted watermark would turn the boot's `N→0` drop into a phantom withdrawal. (There is no magnitude backstop: a small reset-to-0 is indistinguishable from ordinary spending, so the client relies on the `game_ready` signal alone.)
  - **Each poll (~1s)**: `cur = read(energy_sent_total)`, `delta = cur − last_seen`.
    - `delta == 0`: nothing to send.
    - `delta > 0` (deposit): send `Set` with `add: delta * 1_000_000` and no tag.
    - `delta < 0` (withdrawal): send `Set` with operations `[add: delta * 1_000_000, max: 0]`, plus a unique `tag` (uuid) and `want_reply: true`. On the matching `SetReply`, compare `original_value - value` against the requested subtraction; if the server actually subtracted less (pool ran out), log the discrepancy — the mod's local balance already overshot and will be corrected on the next `set_notify` push.
    - Advance `last_seen = cur` after handling the delta, then **optimistically** fold the delta into the cached pool: `current_energy_link_value = max(0, current_energy_link_value + delta_joules)`. See "Updating balance" for why.
  - **Optional torn-read guard:** read the field twice and skip the poll if the two reads disagree. Belt-and-suspenders only — because the counter is cumulative (not consume-once), an unguarded torn read self-heals on the very next poll's diff.
- **Updating balance**: **After** processing sends, write the current AP EnergyLink pool total (in raw MJ — `pool_joules // 1_000_000`) to `energy_balance` as a u64, **unconditionally** every poll (so seed polls, `delta==0` polls, and other players' deposits all keep the mod's pool view fresh). Sub-MJ remainders aren't representable on the mod side and are dropped.
  - **Why order + optimistic fold matter (overdraw fix):** the balance is sourced from `current_energy_link_value` (the last server-pushed pool). If the balance were written *before* the diff — or without folding in the just-sent delta — it would bounce the mod's immediate local decrement (from a purchase) back up to the stale pre-purchase pool. Across the ~1–2 poll server round-trip the affordability gate would then see a stale-high balance and permit a **self-induced overdraw**. Processing the send first and applying the `max(0, value + delta_joules)` optimistic update makes the balance reflect the spend *now*. The optimistic guess is harmless: `set_notify`'s `SetReply` reassigns `current_energy_link_value` to the server's **absolute** pool value on every change, overwriting the guess with no double-count. Residual: a *concurrent shared-pool change by another player* still leaves the value off by ≤1 poll until the next absolute push — the irreducible shared-pool race.
- **`set_notify`**: Subscribe to `EnergyLink{team}` once on connect. The standard CommonClient handler updates `current_energy_link_value` from each broadcast SetReply (to the server's absolute pool value).

### Game Responsibilities

- **Generating energy**: Accumulates energy locally from destroyed objects, collected patches, and machine charging. Sub-MJ precision is kept in a float carry (charge gain produces fractional MJ per frame); whenever the carry crosses a whole MJ, that whole part is *added* to `energy_sent_total` and the remainder rolls forward. No flush and no slot check — the counter is written directly and the client diffs it.
- **Spending energy**: When the player purchases an item via the in-game EnergyLink menu, subtracts the (integer) cost directly from both `energy_sent_total` (the client diffs it and forwards the withdrawal) and the local `energy_balance` (immediate UI feedback). This happens on the purchase event itself in **any** scene — no gameplay frame or flush is required, which is why menu purchases now reliably reach the pool. Auto-Charge's per-frame fractional withdrawals fold into the same `energy_sent_total` carry.

## TrapLink

### Client Responsibilities

- **Receiving traps**: When the AP server sends a trap to this player, wait until `traplink_receive == 0`, then write `1`. The incoming Bounce's `trap_name` field is **ignored** — KAR applies a random local trap from its own pool (see "Game Responsibilities" below) regardless of which named trap the source world sent. This is a deliberate design choice: KAR's trap pool doesn't have a clean 1:1 mapping to other worlds' trap names, so we treat any incoming TrapLink Bounce as "apply some trap locally."
- **Sending traps**: Watch `traplink_send`. When it becomes non-zero, read the value as a `TrapLinkKind` enum (see below), look up the corresponding `trap_name` string, send a TrapLink Bounce to the AP server with `{"time", "source", "trap_name"}`, then clear `traplink_send` to `0`. The game may set the field multiple times in quick succession (e.g., picking up several bad items from one box burst). The client is responsible for debouncing — it should not forward every set as a separate trap. The game has no send cooldown.

### `traplink_send` kind enum

The mod writes one of the following kind values into `traplink_send`. Values are defined in `mods/archipelago/src/traplink.h`.

| Value | TrapLinkKind             | Suggested `trap_name` | Triggered by |
|-------|--------------------------|-----------------------|--------------|
| 0     | `TRAPLINK_KIND_NONE`     | (no send pending)     | — |
| 1     | `TRAPLINK_KIND_BAD_PATCH`| `"Bad Patch"`         | City Trial — bad/fake patch pickup (SPEEDMIN, CHARGENONE, `*DOWN`, `*FAKE`) |
| 2     | `TRAPLINK_KIND_SLEEP`    | `"Sleep"`             | City Trial / Air Ride — sleep copy ability granted |
| 3     | `TRAPLINK_KIND_SPEED_DOWN`| `"Speed Down"`       | Top Ride — `TRITEM_SPEED_DOWN` pickup |

Unknown kinds (future additions seen by older clients) should fall back to a generic name like `"Trap"`.

### Game Responsibilities

- **Applying traps**: A per-frame GOBJ checks `traplink_receive` (gated on `GmIntroState == GMINTRO_END`). When `1`, the effect depends on the current scene major:
  - **City Trial**: picks a random trap from a predefined table (stat downs, sleep, meteors, rail fire, bounce, fake powerups, run amok, fake patch, etc.) and applies it via `APItems_HandleItem`. Free Run drops the trap (item data tables aren't loaded); stadiums fall back to the Air Ride sleep trap.
  - **Air Ride**: gives `COPYKIND_SLEEP` to every human rider directly via `Rider_GiveAbility`.
  - **Top Ride**: applies `TRITEM_SPEED_DOWN` directly to every human Kirby via the shared item-give path (`GateTopRideItems_GiveItem` → `TopRide_KirbyApplyItem`) — a direct apply, not a position spawn.

  If the trap can't apply (e.g., event slot busy), it retries next frame. Once applied, clears the flag to `0`. See `docs/traplink-send.md` for the full dispatch table.
- **Detecting traps**: Code hooks detect natural negative gameplay events and set `traplink_send` to the corresponding `TrapLinkKind` value. Current triggers map to the enum above.

  AP-delivered items may also trigger the flag (e.g., a received SPEEDMIN trap re-fires the bad-patch hook); the client must handle deduplication.
- **Receive recursion guard**: After applying an *incoming* trap, the mod suppresses *outgoing* sends for ~120 frames (`recv_suppress_frames`). This stops a received trap whose effect re-fires a send hook (e.g. an applied bad-patch tripping the bad-patch send detector) from echoing straight back out as a new Bounce. It is a recursion guard, not a rate-limit — burst collapsing and dedup of distinct logical traps are still the client's responsibility.

## Important Notes

- **ID 0 is reserved** as the "empty" sentinel for `incoming_item_id`. Never use 0 as a valid AP item ID.
- **Items can process out of order.** If an item can't apply (e.g., an event while another is active), it is skipped and items behind it can still be applied. The unprocessed list shrinks as items are applied.
- **`item_received_index` reflects receipt, not application.** It increments as soon as an item is read from the mailbox, before the item is applied. The client should use this to avoid re-sending items.
- **The unprocessed item list persists in save data.** `unprocessed_items` is stored on the memory card and survives reboots, so items received but not yet applied (e.g., player powered off before an event item could fire) are retained. `item_received_count` is also persisted and mirrored to `item_received_index` on boot so the client can resume sending from the right position.
- **One item per frame.** The game applies at most one item from the unprocessed list per frame (60 items/sec max throughput). The client can write to the mailbox as fast as the game clears it.
