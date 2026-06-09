# Top Ride Item System

## Overview

Top Ride has **two completely separate item systems**, distinct from City Trial's box/spawn table system:

1. **Top Ride Items** — 22 unique 2D power-up items managed by an ItemMgr singleton (`TRITEM_NUM = 22`). Players collect these during races to gain temporary attack/defense abilities.
2. **Copy Ability Roulette** — Ground collision panels that trigger a spinning roulette wheel, granting one of 11 copy abilities. Uses the same `randomAbility_giveAbility` system as Air Ride/City Trial.

Neither system uses `CityItem_Create`, `grBoxGeneObj` spawn tables, or `Machine_OnTouchItem`.

## Top Ride Items

### ItemMgr Singleton

Pointer at `0x805ddba4`. Allocated at runtime during Top Ride scene initialization.

Key field: **enabled items bitmask** at offset `+0x24` (u32, bits 0–21). The item selection function checks this mask — items with their bit cleared are never selected for spawning.

Mystery (a2dIT21) is the roulette item and is always available; it is not part of the indexed bitmask.

### Item Descriptor Table

23 entries at `0x804ea2fc`. Each entry is 8 bytes: `{char *model_name, char *action_name}`. The first entry (index 0) is Mystery (`a2dIT21 AC_hatena`) — the roulette item, not in the bitmask. The 22 bitmask items follow at indices 1–22, mapping to `TopRideItemKind` 0–21.

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

### Item Weight Data

Per-item parameter data at `0x804ea548`. Each item has a parameter block at a varying offset (stride ~0x54). Key fields per item:

- Offsets 0x00–0x0C: 4 weight columns (floats). Column selection depends on player rank (0–2) or is forced to column 3 for certain modes.
- Offset 0x14: time threshold (float) — minimum elapsed race time before this item can appear.

### Key Functions

| Address | Size | Name | Description |
|---------|------|------|-------------|
| `0x8034b5f4` | 0x2d4 | `TopRideItem_MgrInit` | ItemMgr constructor. Sets initial enabled items bitmask at `+0x24`. |
| `0x8034b8c8` | 0x688 | `TopRideItem_SpawnTimed` | Per-player item selection and spawn. Weighted random from enabled items, called from orchestrator. |
| `0x8034ad08` | 0x674 | `TopRideItem_Create` | Item object constructor. Loads 2D model, sets up animation, lifetime. |
| `0x8034bf50` | 0x1e0 | `TopRideItem_SpawnAtPosition` | Spawns a single item at a position. Called from per-item widget handlers. |
| `0x802db74c` | 0xebc | `TopRide_KirbyMgrUpdate` | Per-frame KirbyMgr update. Iterates kirbys, calls the per-kirby update `TopRide_KirbyPhysUpdate` (0x802d5ec0, at 0x802db8a4), then (gated on `KirbyMgr.round_state == 2`) `TopRideItem_SpawnTimed` (twice, at 0x802db8e8 / 0x802db914) and `TopRideItem_Update`. |
| `0x802d5ec0` | 0xdcc | `TopRide_KirbyPhysUpdate` | Per-kirby physics/update. Tail invokes the absorber-consume helper `zz_8034ac84_` (0x8034ac84, called at 0x802d6ba0) then `TopRide_KirbyApplyItem` (called at 0x802d6bac). |
| `0x8034d1f8` | 0x0c | `TopRideItem_GetDataBase` | Returns base pointer to item data array at `0x804ea548`. |
| `0x8034d204` | 0x30 | `TopRideItem_GetDataByIndex` | Returns pointer to per-item parameter struct by index (switch 0–21). |
| `0x802d8cb4` | 0x5d8 | `TopRide_KirbyApplyItem` | Per-kind effect dispatcher: `(TopRideKirby *kirby, int item_kind)`. Switch over kinds 0–21 applying each item's gameplay effect to the kirby. Same code path used by the natural pickup flow (Absorber consume → dispatch). Requires `kirby+0x7c` (held item GObj) non-null — true during active gameplay. Out-of-range kinds silently no-op. |
| `0x80356dac` | 0x9a0 | `TopRideItem_PartyBallUpdate` | Per-frame think for the Party Ball (slot 21 / a2dIT20 / `AC_usiro`). State machine keyed off a frame counter at `gobj+8`: frames 10–79 camera shake-in, 80–149 bounce-in, 150 wobble anim (`AC_OB0B_PURUPURU_LOOP`), 191 flash (`AC_OB0B_FLASH`), 255 open (`AC_OB0B_START` + burst loop: weighted-random `TopRideItem_SpawnAtPosition` × N), 317 loop (`AC_OB0B_LOOP`), 325–394 shake-out, ≥395 reset. Called from `TopRide_KirbyMgrUpdate` (0x802dc5b8). |

### Direct Apply (Bypassing the Spawned-Pickup Flow)

The natural pickup pipeline is: `TopRideItem_SpawnAtPosition` creates an item GObj that flies along its initial-velocity (orient) vector, decays per-frame inside `TopRideItem_Update`, and on overlap with a kirby's Absorber sub-object (TopRideKirby+0xD00, vtable `0x804bdc70`) writes:

- `absorber+0x0E` (u16) ← item kind
- `absorber+0x10` (s16) ← 3 (ACQUIRING animation state)

The acquiring state is just the visual handshake. The actual effect application happens later, per-frame, in the per-kirby update `TopRide_KirbyPhysUpdate` (0x802d5ec0)'s tail: an absorber-consume helper (`zz_8034ac84_` at 0x8034ac84, called at 0x802d6ba0) reads `absorber+0x0E` (returning -1 if mode `+0x0C != 0`, otherwise resetting `+0x0E` to 0xFFFF), and `TopRide_KirbyApplyItem(kirby, kind)` (called at 0x802d6bac) dispatches the effect.

**Implication for mod code:** to give a TR item directly to a specific kirby (no flying projectile, no position-based pickup), call `TopRide_KirbyApplyItem(kirby, kind)` directly. This applies the effect this frame, skips the absorption animation, and is unaffected by other kirbys' positions. Used by `GateTopRideItems_GiveItem` (AP item give + traplink TR receive).

### Item Selection Algorithm (`TopRideItem_SpawnTimed`, 0x8034b8c8)

1. Roll `HSD_Randf()` against a spawn probability threshold (varies by time, player count, position).
2. Iterate all 22 item types (indices 0–21).
3. **Skip if bit not set** in enabled items bitmask (`ItemMgr + 0x24`).
4. Skip if elapsed time < item's time threshold (offset 0x14 in item data).
5. Sum per-item weights from the active weight column.
6. Roll `HSD_Randf()` for weighted random selection among eligible items.
7. Roll `HSD_Randf()` + spline interpolation for spawn position along the track.
8. Call `TopRideItem_Create` (0x8034ad08).

### Default Bitmask by Mode

- **Free Run**: `0x3FFFFF` (all 22 items), with conditional disables for three checklist-reward items:
  - bit **20** (piyo / **Chickie**) cleared unless the Chickie checklist reward has been earned.
  - bit **18** (meta / **Who? Paint**) cleared unless the Who? Paint checklist reward has been earned.
  - bit **15** (lanthanum / **Lantern**) cleared unless the Lantern checklist reward has been earned.
- **Time Attack**: Subset of ~9 items (hammer, macron, speedUp, missile, chargeUp, etc.).
- **Special modes**: Subset of ~7 items.

The three extra-unlock flags flow through `TopRide_SetExtraUnlocks` (0x8000b5dc), invoked from `TopRide_OnCourseSelect` (0x8002cc30) and `TopRide_PreGameThink` (0x8002c06c). That call reads `ClearChecker_CheckUnlocked(mode=TR, reward_index=8|9|10)` for Chickie / Who? Paint / Lantern respectively and stores the booleans at `GameData+0x37e/+0x37f/+0x380`. Those bytes later land at offsets `+0x4a/+0x4b/+0x4c` on the TR config struct that `TopRideItem_MgrInit` inspects; a zero byte clears the corresponding bit of the enabled mask.

### Per-Item Widget Handlers

22 functions at `0x802b0b88`–`0x802b357c` (each 0x1fc bytes). These are C++ virtual method overrides in a widget class tree. Each:
1. Reads item kind from `this + 0x1130`
2. Updates the enabled items bitmask at `ItemMgr + 0x24`
3. Computes spawn position via matrix transforms
4. Calls `TopRideItem_SpawnAtPosition` (0x8034bf50)

## Copy Ability Roulette

### Ground Collision Attribute 0xF

Specific floor polygons on Top Ride tracks are tagged with ground collision attribute 0xF. When a machine drives over such a polygon:

```
Machine_ProcessEnvColl (0x801e5108)
  └─ detects attribute 0xF via zz_80246f40_ (0x80246f40)
  └─ checks it's a new panel (different from last frame's stored ID)
  └─ calls Rider_GiveRandomAbility (0x80191fb8)
       └─ calls Rider_StartRandomCopyWheel (0x801ae4ec)
            └─ HSD_Randi(0xB) picks ability index 0–10
            └─ looks up ability from table at 0x804af690
            └─ calls Rider_StartCopyWheel (0x801ae550)
                 └─ sets up spinning wheel UI
                 └─ eventually calls randomAbility_giveAbility (0x801a61d4)
```

### Ability Table

11 entries at `0x804af690` (int array): `{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}` — identity mapping to CopyKind (FIRE, WHEEL, SLEEP, SWORD, BOMB, PLASMA, NEEDLE, MIC, FREEZE, TORNADO, BIRD).

Alternative table at `0x804af6bc` for melee mode (29 entries, weighted distribution).

### Existing Hooks

`randomAbility_giveAbility` is already replaced by `GateAbilities_RandomGiveAbility` via `CODEPATCH_REPLACEFUNC` in `gate_abilities.c`. This hook applies to ALL modes including Top Ride — if the wheel lands on a locked ability, it picks a random unlocked one instead. No additional hooks needed for copy ability gating in Top Ride.

## Gating Approach

Implemented in `mods/archipelago/src/gate_topride_items.c` (+ `.h`). Installed at boot by `GateTopRideItems_OnBoot`, which logs `[TopRideItems] Top Ride item gating hooks installed`.

### Top Ride Items

The enabled bitmask at `ItemMgr + 0x24` (`TopRideItemMgr.enabled_mask`) is the primary hook point — the timed spawn path already respects it. But the Party Ball burst (`TopRideItem_PartyBallUpdate`) and a residual out-of-range case need two extra hooks. The unlock state lives in `u32 topride_item_unlocked_mask` in `APSave` (global `ap_save`, `main.h`); AP exposes 17 non-ability item unlocks. The three installed hooks:

| Site | Mechanism | Function | Purpose |
|------|-----------|----------|---------|
| 0x802db05c | `CODEPATCH_HOOKCREATE` | `GateTopRideItems_ApplyMask` | Runs right after `TopRideItem_MgrInit` (0x8034b5f4) returns inside `TopRide_FielderInit` (0x802dafb4). ANDs `mgr->enabled_mask` with `ap_save->topride_item_unlocked_mask \| ABILITY_ITEM_BITS`, mirrors the Party Ball bit, then clears ability-themed bits whose ability is locked. Clobbered instr: `lwz r6, 4(r30)`. |
| 0x8034bf50 | `CODEPATCH_HOOKCONDITIONALCREATE` | `GateTopRideItems_FilterSpawn` | Entry of `TopRideItem_SpawnAtPosition`. Returns 1 to block (kind out of range, or its `enabled_mask` bit clear), 0 to proceed. Block path branches to the function's epilogue blr at 0x8034c12c. Guards against the Party Ball burst picking a locked/garbage kind (which would make `TopRideItem_Create` read past the descriptor table and crash). |
| 0x803574a4, 0x803574d0 | `CODEPATCH_REPLACECALL` ×2 | `GateTopRideItems_GetDataGated` | The two `bl TopRideItem_GetDataByIndex` calls inside `TopRideItem_PartyBallUpdate`'s weight sum loop and pick loop. The wrapper returns a zeroed `locked_item_stub` (weight 0 at +0x10) for locked kinds so the burst's weighted random never lands on one. |

`GateTopRideItems_ApplyMask` also emits a per-init `[TopRideItems]` line showing the before/after enabled mask.

### Ability-Themed Items

Four items are ability-themed and gated by `ability_unlocked_mask` instead of `topride_item_unlocked_mask`, matching how copy ability panels work in City Trial:

| TRITEM | Index | CopyKind |
|--------|-------|----------|
| TRITEM_FREEZE_FAN | 9 | COPYKIND_FREEZE |
| TRITEM_FIRE | 11 | COPYKIND_FIRE |
| TRITEM_BOMB | 13 | COPYKIND_BOMB |
| TRITEM_WALKY | 16 | COPYKIND_MIC |

These bits are force-set past the TR item mask (`| ABILITY_ITEM_BITS`) in `GateTopRideItems_ApplyMask`, then cleared if their ability is locked. No separate AP items exist for these — unlocking the copy ability (e.g., AP_ABILITY_UNLOCK_FREEZE) enables the corresponding TR item.

`TRITEM_PARTY_BALL_ALT` (slot 12) is **not** ability-gated — it's a Party Ball variant (KirbyKusdama). `GateTopRideItems_ApplyMask` instead mirrors bit 21's (`TRITEM_PARTY_BALL`) unlock state onto bit 12 so both Party Ball variants spawn together; AP never sends a separate slot-12 unlock.

### Unlock / Give Entry Points

| Function | Role |
|----------|------|
| `GateTopRideItems_UnlockItem(kind, announce)` | Sets bit `kind` in `ap_save->topride_item_unlocked_mask`; optionally enqueues an "Unlocked Item: …" textbox (`TopRideItemColor`, mode color for Top Ride). Returns 0 for out-of-range kind. |
| `GateTopRideItems_GiveItem(kind)` | Direct apply (no flying pickup). Requires the TR `KirbyMgr` (`*stc_topride_kirbymgr`) and `round_state == 2`; iterates the 4 slots, and for each human (`TopRide_GetPlayerKind(slot) == TR_PKIND_HMN`) calls `TopRide_KirbyApplyItem(k, kind)`. Returns 1 if applied to ≥1 kirby, else 0 (caller retries). Used by the AP TR-item-give path and TrapLink-TR receive. Deliberately does **not** gate on `kirby->is_active` (stays 0 in Time Attack / Free Run). |
| `GateTopRideItems_AbilityToItem(ability)` | Maps a `CopyKind` to its TR-item analog via `ability_items[]` (Freeze→Freeze Fan, Fire→Fire, Bomb→Bomb, Mic→Walky); returns -1 if none. |

### Copy Ability Roulette

Already gated by `GateAbilities_RandomGiveAbility` REPLACEFUNC hook. No additional work needed for Top Ride.
