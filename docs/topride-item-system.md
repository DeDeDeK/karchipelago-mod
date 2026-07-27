# Top Ride Item System

Top Ride has a single item system, distinct from City Trial's box/spawn table system: 22 unique 2D power-up items managed by an ItemMgr singleton (`TRITEM_NUM = 22`), plus the always-available Mystery item (`a2dIT21`), which randomizes among the other Top Ride items. Players collect these during races to gain temporary attack/defense powers. It does not use `CityItem_Create`, `grBoxGeneObj` spawn tables, or `Machine_OnTouchItem`.

## Game System

**Top Ride has no copy abilities.** It creates neither `MachineData` nor `RiderData`, so `copy_kind` and every path that grants an ability (`Rider_GiveAbility`, `Rider_GiveRandomAbility`, `randomAbility_giveAbility`) are unreachable. The closest analogs are the four ability-themed items below (Fire, Freeze Fan, Bomb, Walky), each a timed kirby state.

**Top Ride loads no 3D stage collision.** `stc_grobj` (`0x805dd6cc`) is written only by `grLoadStage` (`0x800ce318`) and its two helpers, none of which Top Ride reaches. Ground collision attributes — including the attribute 0xF copy panels used in City Trial and Air Ride — do not exist here; TR collision is the tile/grid system (`TopRide_ApexGridScan`, `0x802cf83c`), and its courses are 2D tilemap archives (`A2a2dBG_*.dat`), not `Gr*.dat` HSD stages.

## ItemMgr Singleton

Pointer at `0x805ddba4` (`stc_topride_itemmgr`, r13+0xAC4). Allocated at runtime during Top Ride scene initialization by `TopRideItem_MgrInit` (`0x8034b5f4`), which is called from `TopRide_KirbyMgrInit` (`0x802dafb4`).

| Offset | Field | Description |
|--------|-------|-------------|
| 0x04 | `stage_data` | Per-stage config table; bytes at +0x38/+0x40/+0x41 select the spawn mode, +0x4a/+0x4b/+0x4c carry the three checklist-reward unlock flags |
| 0x24 | `enabled_mask` | u32 bitmask of which item kinds can spawn (bits 0–21). Items with their bit cleared are never selected |

Mystery (`a2dIT21`) is the roulette item and is always available; it is not part of the indexed bitmask.

## Item Descriptor Table

23 entries at `0x804ea2fc`. Each entry is 8 bytes: `{char *model_name, char *action_name}`. The first entry (index 0) is Mystery (`a2dIT21` / `AC_hatena`) — the roulette item, not in the bitmask. The 22 bitmask items follow at indices 1–22, mapping to `TopRideItemKind` 0–21.

Idx column below = `TopRideItemKind` value = bitmask bit; the corresponding descriptor entry is at `0x804ea2fc + (Idx + 1) * 8`.

| Idx | Bit | Model | Action | Item Name |
|-----|-----|-------|--------|-----------|
| 0 | 0x000001 | a2dIT1e | AC_hammer | Hammer |
| 1 | 0x000002 | a2dIT01 | AC_macron | Big Cake |
| 2 | 0x000004 | a2dIT02 | AC_speedUp | Speed Up |
| 3 | 0x000008 | a2dIT03 | AC_speedDown | Speed Down |
| 4 | 0x000010 | a2dIT04 | AC_BoostUp_Missile | Spinner |
| 5 | 0x000020 | a2dIT0c | AC_chargeUp | Charge Tank |
| 6 | 0x000040 | a2dIT0d | AC_muteki | Invincible Candy |
| 7 | 0x000080 | a2dIT0a | AC_Sdrill_kusdama_niseron | Buzz Saw |
| 8 | 0x000100 | a2dIT05 | AC_FrontSpeer | Drill |
| 9 | 0x000200 | a2dIT1b | AC_ice | Freeze Fan |
| 10 | 0x000400 | a2dIT07 | AC_BoostUp_Missile | Missile |
| 11 | 0x000800 | a2dIT06 | AC_AfterFlame | Fire |
| 12 | 0x001000 | a2dIT0b | AC_Sdrill_kusdama_niseron | Party Ball (alt) — kusdama variant (KirbyKusdama). Visually identical to slot 21; AP mirrors bit 21's unlock onto this bit. |
| 13 | 0x002000 | a2dIT08 | AC_bomb | Bomb |
| 14 | 0x004000 | a2dIT10 | AC_landbomb | Step-boom |
| 15 | 0x008000 | a2dIT11 | AC_lanthanum | Lantern |
| 16 | 0x010000 | a2dIT16 | AC_mike | Walky |
| 17 | 0x020000 | a2dIT12 | AC_clakko | Kracko |
| 18 | 0x040000 | a2dIT13 | AC_meta | Who? Paint |
| 19 | 0x080000 | a2dIT17 | AC_kemuron | Smokescreen |
| 20 | 0x100000 | a2dIT18 | AC_piyo | Chickie |
| 21 | 0x200000 | a2dIT20 | AC_usiro | Party Ball — ushiroyurerun variant (KirbyUshiroyurerun, "backward sway"). Canonical Party Ball slot for AP. |

Five action strings (`AC_ice`, `AC_bomb`, `AC_mike`, `AC_meta`, `AC_piyo`) live in the small-data block at `0x805db2c8..0x805db2ec` rather than alongside the rest at `0x804ea1xx`.

## Item Parameter Data

Per-item parameter blocks reached through `TopRideItem_GetDataByIndex` (`0x8034d204`), a 22-case jump table (`0x804eadb0`) into the data array based at `0x804ea548` (returned by `TopRideItem_GetDataBase`, `0x8034d1f8`). Out-of-range kinds fall through to `return kind` — an invalid pointer — so only call with 0..21.

| Offset | Description |
|--------|-------------|
| 0x00–0x10 | Weight columns (floats). `TopRideItem_SpawnTimed` selects a column via a computed byte offset (`lfsx` at `0x8034bb88` / `0x8034bc20`); column choice depends on player rank (0–2) or is forced to column 3 for certain modes. |
| 0x10 | The weight `TopRideItem_PartyBallUpdate`'s burst picker always reads (`lfs f0, 16(r3)` at `0x803574a8` / `0x803574d4`). |
| 0x14 | Time threshold (float) — minimum elapsed race time before this item can appear (`lfs f2, 20(r3)` at `0x8034bb6c`). |

## Key Functions

| Address | Size | Name | Description |
|---------|------|------|-------------|
| `0x8034b5f4` | 0x2d4 | `TopRideItem_MgrInit` | ItemMgr constructor. Picks the initial `enabled_mask`. |
| `0x8034b8c8` | 0x688 | `TopRideItem_SpawnTimed` | Per-player item selection and spawn. Weighted random from enabled items. |
| `0x8034ad08` | 0x674 | `TopRideItem_Create` | Item object constructor. Loads 2D model, sets up animation, lifetime. |
| `0x8034bf50` | 0x1e0 | `TopRideItem_SpawnAtPosition` | Spawns a single item at a position. Called from per-item widget handlers and the Party Ball burst. |
| `0x8034c130` | 0x7f4 | `TopRideItem_Update` | Per-frame item lifetime/render tick. |
| `0x8034d1f8` | 0x0c | `TopRideItem_GetDataBase` | Returns the item data array base `0x804ea548`. |
| `0x8034d204` | 0x30 | `TopRideItem_GetDataByIndex` | Returns the per-item parameter block by kind (switch 0–21). |
| `0x802d8cb4` | 0x5d8 | `TopRide_KirbyApplyItem` | Per-kind effect dispatcher: `(TopRideKirby *kirby, int item_kind)`. Switch over kinds 0–21 applying each item's gameplay effect. Same code path as the natural pickup flow. Requires `kirby+0x7c` non-null — true during active gameplay. Out-of-range kinds silently no-op. |
| `0x80356dac` | 0x9a0 | `TopRideItem_PartyBallUpdate` | Per-frame think for the Party Ball (slot 21 / `a2dIT20` / `AC_usiro`). |

### Call sites in the per-frame loop

`TopRide_KirbyMgrUpdate` (`0x802db74c`) drives the whole item flow:

| Address | Call |
|---------|------|
| 0x802db8a4 | `TopRide_KirbyPhysUpdate` (`0x802d5ec0`), per kirby |
| 0x802db8e8 / 0x802db914 | `TopRideItem_SpawnTimed`, twice (gated on `KirbyMgr.round_state == 2`) |
| 0x802dc578 | `TopRideItem_Update` (same gate) |
| 0x802dc5b8 | `TopRideItem_PartyBallUpdate` |

`TopRide_KirbyPhysUpdate`'s tail then runs the absorber-consume helper `zz_8034ac84_` (`0x8034ac84`, called at `0x802d6ba0`) followed by `TopRide_KirbyApplyItem` (called at `0x802d6bac`).

### Party Ball state machine

`TopRideItem_PartyBallUpdate` is keyed off a frame counter at `gobj+8`: frames 10–79 camera shake-in, 80–149 bounce-in, 150 wobble anim (`AC_OB0B_PURUPURU_LOOP`), 191 flash (`AC_OB0B_FLASH`), 255 open (`AC_OB0B_START` + burst loop: weighted-random `TopRideItem_SpawnAtPosition` × N), 317 loop (`AC_OB0B_LOOP`), 325–394 shake-out, ≥395 reset.

## Pickup Flow and Direct Apply

The natural pickup pipeline is: `TopRideItem_SpawnAtPosition` creates an item GObj that flies along its initial-velocity (orient) vector, decays per-frame inside `TopRideItem_Update`, and on overlap with a kirby's Absorber sub-object (`TopRideKirby+0xD00`, vtable `0x804bdc70`) writes:

- `absorber+0x0E` (u16) ← item kind
- `absorber+0x10` (s16) ← 3 (ACQUIRING animation state)

The acquiring state is just the visual handshake. The effect is applied later, per-frame, in `TopRide_KirbyPhysUpdate`'s tail: the absorber-consume helper reads `absorber+0x0E` (returning -1 if mode `+0x0C != 0`, otherwise resetting `+0x0E` to `0xFFFF`), and `TopRide_KirbyApplyItem(kirby, kind)` dispatches the effect.

**Implication for mod code:** to give a TR item directly to a specific kirby (no flying projectile, no position-based pickup), call `TopRide_KirbyApplyItem(kirby, kind)` directly. This applies the effect this frame, skips the absorption animation, and is unaffected by other kirbys' positions. Used by `GateTopRideItems_GiveItem` (AP item give + traplink TR receive).

## Ability-Power Item States (held powers)

The four **ability-themed** items — Fire (11), Freeze Fan (9), Bomb (13), Walky (16), the TR analogs of copy abilities — are not instantaneous: `TopRide_KirbyApplyItem` transitions the kirby's state machine (`state_handler` at `TopRideKirby+0x7c`) into a per-item **state** that persists for the power's duration, and records the active kind at `TopRideKirby+0x11` (`active_item_kind`; `0xFF` = none). Each item's setter calls `TopRide_KirbyNormalSetter` (`0x802df844`) to reset the state object, then overwrites its vtable with the item state:

| Item | Kind | Setter | State vtable |
|------|------|--------|--------------|
| Fire | 11 | `TopRide_KirbyItemFireSetter` (0x80302d04) | `0x804db288` |
| Freeze Fan | 9 | `TopRide_KirbyItemFreezeFanSetter` (0x8030b398) | `0x804dc6e4` |
| Bomb | 13 | `TopRide_KirbyItemBombSetter` (0x80303a98) | `0x804db088` |
| Walky | 16 | `TopRide_KirbyItemWalkySetter` (0x8030668c) | `0x804dc150` |

So a held ability power is detected by comparing `*(void**)kirby->state_handler` against these vtables (`TopRide_KirbyHasStateVtable`). `active_item_kind` is *not* reset on natural expiry, so it is not a reliable "currently active" indicator — use the state vtable.

**Reverting (drop):** `TopRide_KirbyNormalMethod` (`0x802da0f4`, Kirby `vtable[50]` / +0xC8, wrapped as `TopRide_KirbyNormal`) exits the current state via its `vt[2]` teardown — removing the power's aura/model/effects — then installs `KirbyNormal`. This is the engine's own revert when one power replaces another or a power times out. The AP "drop ability" control (`drop_ability.c`, gated by `ap_menu_settings.drop_ability_enabled`) reuses it: on the owning player's Z-press it calls `TopRide_KirbyNormal` and clears `active_item_kind`.

## Item Selection Algorithm (`TopRideItem_SpawnTimed`, 0x8034b8c8)

1. Roll `HSD_Randf()` against a spawn probability threshold (varies by time, player count, position).
2. Iterate all 22 item types (indices 0–21).
3. **Skip if bit not set** in `ItemMgr.enabled_mask` (+0x24).
4. Skip if elapsed time < the item's time threshold (parameter block +0x14).
5. Sum per-item weights from the active weight column.
6. Roll `HSD_Randf()` for weighted random selection among eligible items.
7. Roll `HSD_Randf()` + spline interpolation for spawn position along the track.
8. Call `TopRideItem_Create` (`0x8034ad08`).

## Default Enabled Mask

`TopRideItem_MgrInit` picks one of three base masks from bytes of the `stage_data` block (`ItemMgr+0x04`), then applies the checklist-reward clears:

| Condition | Mask | Items |
|-----------|------|-------|
| `stage_data+0x41 == 2` | `0x002783` | Hammer, Big Cake, Buzz Saw, Drill, Freeze Fan, Missile, Bomb (7) |
| else `stage_data+0x38 == 1` | `0x09C06E` | Big Cake, Speed Up, Speed Down, Charge Tank, Invincible Candy, Step-boom, Lantern, Walky, Smokescreen (9) |
| else | `0x3FFFFF` | All 22 |

Only the `0x3FFFFF` path falls through into the reward-clear block at `0x8034b854..0x8034b8a4` — the two restricted masks branch straight past it. Each clear is `stage_data+byte == 0` → clear one bit:

| Config byte | Bit cleared | Item | TR checklist reward |
|-------------|-------------|------|---------------------|
| +0x4a | 20 | Chickie (`a2dIT18` / piyo) | 8 |
| +0x4b | 18 | Who? Paint (`a2dIT13` / meta) | 9 |
| +0x4c | 15 | Lantern (`a2dIT11` / lanthanum) | 10 |

Those three flags flow through `TopRide_SetExtraUnlocks` (`0x8000b5dc`), invoked from `TopRide_OnCourseSelect` (`0x8002cc30`) and `TopRide_PreGameThink` (`0x8002c06c`). That call reads `ClearChecker_CheckUnlocked(mode=TR, reward_index=8|9|10)` and stores the booleans at `GameData+0x37e/+0x37f/+0x380` (`GameData.topride_extra_unlocks[0..2]`), which later land at `stage_data+0x4a/+0x4b/+0x4c`.

Because `MgrInit` gates these three on the received-reward path (not the enabled mask, which `GateTopRideItems_ApplyMask` only ANDs), no value of `topride_item_unlocked_mask` reaches them — the AND can clear a bit the engine set, never set one the engine cleared. Both gate states therefore mark the reward received in `ap_save->received_checklist_rewards[GMMODE_TOPRIDE]`, so `CheckUnlocked` returns true and the engine enables the type at course init:

- **Gating off**: `APOptions_ApplyUngatedCategories` marks TR reward indices 8–10 received at connect, alongside the all-1s mask.
- **Gating on**: `GateTopRideItems_UnlockItem` marks the matching index when that item's own unlock arrives (Chickie → 8, Who? Paint → 9, Lantern → 10), so the unlock item delivers its type like the other 18. Without it the three unlocks are inert, and since the AP world drops their checklist rewards from the pool as overlapping, nothing else could enable them.

Either path sets only the received bit, never `is_unlocked` or a `clear[]` write — those would badge the cell and send a spurious outbound check. The restricted base masks still apply on top, so Chickie and Who? Paint stay absent from the courses that use them exactly as in vanilla.

## Per-Item Widget Handlers

22 functions at `0x802b0b88`–`0x802b357c` (each `0x1fc` bytes). These are C++ virtual method overrides in a widget class tree. Each:

1. Reads item kind from `this + 0x1130`
2. Updates `ItemMgr.enabled_mask` (+0x24)
3. Computes spawn position via matrix transforms
4. Calls `TopRideItem_SpawnAtPosition` (`0x8034bf50`)

## Gating Approach

Implemented in `mods/archipelago/src/gate_topride_items.c` (+ `.h`). Installed at boot by `GateTopRideItems_OnBoot`, which logs `[TopRideItems] Top Ride item gating hooks installed`.

### Hooks

The enabled bitmask at `ItemMgr+0x24` is the primary hook point — the timed spawn path already respects it. But the Party Ball burst (`TopRideItem_PartyBallUpdate`) and a residual out-of-range case need two extra hooks. The unlock state lives in `u32 topride_item_unlocked_mask` in `APSave` (global `ap_save`, `main.h`); AP exposes 21 item unlocks, one per item kind except the Party Ball twin at slot 12.

| Site | Mechanism | Function | Purpose |
|------|-----------|----------|---------|
| 0x802db05c | `CODEPATCH_HOOKCREATE` | `GateTopRideItems_ApplyMask` | Runs right after `TopRideItem_MgrInit` returns inside `TopRide_KirbyMgrInit`. ANDs `mgr->enabled_mask` with `ap_save->topride_item_unlocked_mask` widened by the ability-derived bits, then mirrors the Party Ball bit. Clobbered instr: `lwz r6, 4(r30)`. |
| 0x8034bf50 | `CODEPATCH_HOOKCONDITIONALCREATE` | `GateTopRideItems_FilterSpawn` | Entry of `TopRideItem_SpawnAtPosition`. Returns 1 to block (kind out of range, or its `enabled_mask` bit clear), 0 to proceed. Block path branches to the function's epilogue blr at `0x8034c12c`. Guards against the Party Ball burst picking a locked/garbage kind (which would make `TopRideItem_Create` read past the descriptor table and crash). |
| 0x803574a4, 0x803574d0 | `CODEPATCH_REPLACECALL` ×2 | `GateTopRideItems_GetDataGated` | The two `bl TopRideItem_GetDataByIndex` calls inside `TopRideItem_PartyBallUpdate`'s weight-sum loop and pick loop. The wrapper returns a zeroed `locked_item_stub` (weight 0 at +0x10) for locked kinds so the burst's weighted random never lands on one. |

`GateTopRideItems_ApplyMask` also emits a per-init `[TopRideItems]` line showing the before/after enabled mask.

### Ability-themed items

Four items accept **either** of two keys — their own bit in `topride_item_unlocked_mask`, or the matching copy ability bit in `ability_unlocked_mask`:

| TRITEM | Index | CopyKind |
|--------|-------|----------|
| TRITEM_FREEZE_FAN | 9 | COPYKIND_FREEZE |
| TRITEM_FIRE | 11 | COPYKIND_FIRE |
| TRITEM_BOMB | 13 | COPYKIND_BOMB |
| TRITEM_WALKY | 16 | COPYKIND_MIC |

`GateTopRideItems_ApplyMask` walks `ability_items[]` and ORs each unlocked ability's item bit into the allowed mask before the AND. The ability key counts only while `ap_save->options.ability_gating_enabled` is set: an ability-ungated world carries an all-1s `ability_unlocked_mask`, which would free all four items outright and leave their AP item unlocks (`AP_TOPRIDE_ITEM_UNLOCK_FREEZE_FAN`, `_FIRE`, `_BOMB`, `_WALKY`) with nothing to do. With ability gating off, those four items are gated purely by their own TR item unlock, exactly like the other 17.

Copy abilities do not exist in Top Ride, so an ability unlock's only effect there is enabling its Top Ride item.

`TRITEM_PARTY_BALL_ALT` (slot 12) is **not** ability-gated — it's a Party Ball variant (KirbyKusdama). `GateTopRideItems_ApplyMask` instead mirrors bit 21's (`TRITEM_PARTY_BALL`) unlock state onto bit 12 so both Party Ball variants spawn together; AP never sends a separate slot-12 unlock.

### Unlock / give entry points

| Function | Role |
|----------|------|
| `GateTopRideItems_UnlockItem(kind, announce)` | Sets bit `kind` in `ap_save->topride_item_unlocked_mask`, and for Chickie / Who? Paint / Lantern also marks TR reward index 8 / 9 / 10 received, the only route to those three; optionally enqueues an "Unlocked Item: …" textbox (`TopRideItemColor`, mode color for Top Ride). Returns 0 for out-of-range kind. |
| `GateTopRideItems_GiveItem(kind)` | Direct apply (no flying pickup). Requires the TR `KirbyMgr` (`*stc_topride_kirbymgr`) and `round_state == 2`; iterates the 4 slots, and for each human (`TopRide_GetPlayerKind(slot) == TR_PKIND_HMN`) calls `TopRide_KirbyApplyItem(k, kind)`. Returns 1 if applied to ≥1 kirby, else 0 (caller retries). Used by the AP TR-item-give path and TrapLink-TR receive. Deliberately does **not** gate on `kirby->is_active`, which stays 0 in Time Attack / Free Run. |
| `GateTopRideItems_AbilityToItem(ability)` | Maps a `CopyKind` to its TR-item analog via `ability_items[]` (Freeze→Freeze Fan, Fire→Fire, Bomb→Bomb, Mic→Walky); returns -1 if none. |
