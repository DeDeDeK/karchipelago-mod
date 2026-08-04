# Box Type Gating

Each City Trial item-box color can be individually locked behind an Archipelago unlock item; a locked color never spawns. A color is also auto-disabled when the other gating systems have emptied its item pool, so opening a box always awards something.

## What Is Gated

The 3 `BoxKind` values (`item.h`), one bit each in `box_unlocked_mask`:

| Bit | `BoxKind` | Name | AP item |
|----:|-----------|------|--------:|
| 0 | `BOXKIND_BLUE` | Blue | 860 |
| 1 | `BOXKIND_GREEN` | Green | 861 |
| 2 | `BOXKIND_RED` | Red | 862 |

Auto-disable is layered on top: a color whose post-filter pool has no entry with `chance > 0` is treated as locked for this spawn, even with its bit set. The abilities, patches, and items gates each zero or remove their own entries from those pools (`gate-abilities.md`, `gate-patches.md`, `gate-items.md`), so any of them can empty a color.

## Entry Points

**Files:** `mods/archipelago/src/gate_boxes.c` / `gate_boxes.h`

| Symbol | Kind | Where | Role |
|--------|------|-------|------|
| `GateBoxes_OnBoot()` | mod | gate_boxes.c | Installs the hook at boot (called from `main.c`). |
| `GateBoxes_DetermineBoxType(int *box_color, int *box_size)` | mod | gate_boxes.c | Replacement for `GrBoxGeneratorDetermine`; picks an unlocked, non-empty box color/size. |
| `BoxHasItems(grBoxGeneObj *obj, int box)` | mod (static) | gate_boxes.c | True if box color `box` still has ≥1 item with `chance > 0` in its post-filter pool. |
| `GateBoxes_UnlockBox(BoxKind kind)` | mod | gate_boxes.c | Sets the unlock bit, logs, and posts a textbox notification. Called from `ap_item_handler.c`. |
| `GrBoxGeneratorDetermine` | game | 0x800ebc04 | Vanilla box color/size picker (replaced). |
| `CityItemSpawn_Think` | game | 0x800eb108 | Direct caller of the picker (only for spawn category 1). |
| `PowerUp_SpawnFromSky` | game | 0x800ecdf4 | Places the box; bails on a `-1` color. |

## Game System

City Trial decides what to spawn in `CityItemSpawn_Think` (0x800eb108). It first calls `CityItemSpawn_UpdateAndCheckToSpawn` (0x800ea6e0), which returns a spawn *category*:

| Category | Meaning | What `CityItemSpawn_Think` does | Routes through the hook? |
|----------|---------|----------------------------------|--------------------------|
| 0 | Patch | `CityItemSpawn_GetRandomItemID` (0x800eb7e4); `box_color = box_size = -1` | No (patches gated separately) |
| 1 | Item box (**any** color) | `GrBoxGeneratorDetermine` at **0x800eb20c** → return saved in r30 (0x800eb210) | **Yes** |
| 2 | Legendary machine-piece carrier | hardcodes `box_color = 2`, `box_size = 2` (a red box) at 0x800eb218, then `CityItemSpawn_SpawnLegendaryPiece` (0x800ed384) attaches the Dragoon/Hydra part | No (see note) |
| 3 | Nothing | returns early | — |

The category value has two sources inside `UpdateAndCheckToSpawn`. `CityItemSpawn_CheckToSpawnLegendaryPiece` (0x800ed2f0) returns **2** when a legendary piece is pending (the Dragoon/Hydra part flag is set and the round's progress threshold has been passed) or **3** otherwise; when no piece is pending and the spawn cooldown has elapsed, a script-byte table (`DAT_805d617c`, values `{0, 1, …}`) selects **0** (patch) or **1** (box). **The script path never yields 2** — category 2 is reachable *only* through the legendary-piece subsystem.

In every spawn case the resulting `box_color` (the picker's return value, or the hardcoded value) is forwarded as the first argument to `PowerUp_SpawnFromSky` (0x800ecdf4) at 0x800eb260, which actually places the box.

> **Note — "red box" is not one thing.** The `GKYE01.map` comment labels category 1 "blue/green box" and category 2 "red box", but that is misleading. Category 1's picker (`GrBoxGeneratorDetermine`) selects from a 9-entry table that **includes red** (color 2), so *normal* red item boxes are fully gated by our hook. Category 2 is not a normal box at all — it is the legendary machine-piece carrier, which is intentionally **not** subject to box-color gating (it belongs to the machine / legendary-piece gating systems). Locking the Red Box suppresses every random red box; it does not — and should not — hide Dragoon/Hydra part deliveries. A carrier box is still a real red box, so it bumps the red-box break counter and stays available with the Red Box locked or the red pool empty.

A carrier only stops spawning when `GateItems_FilterLegendaryPieces` clears `is_enabled`, which needs **all three** of that machine's pieces locked. `GateItems_MarkAsSpawnedGated` suppresses a single locked piece by skipping `LegendaryPiece_MarkAsSpawned`, which leaves the box's `forced_item` at -1 (random roll) and still advances `next_piece_index` — so one unlocked piece keeps all three of that machine's carrier boxes spawning. Breaking one while the red pool is empty is harmless: `CityItemSpawn_GetRandomItemID` (0x800eb7e4) walks a zero-length pool, `HSD_Randi(0)` returns 0 without dividing, and the roll falls through to a -1 return, which spawns no item.

`GrBoxGeneratorDetermine` reads a 9-entry chance table from `grBoxGeneInfo->item_desc->box_spawn_chances` (`grBoxGeneInfo` at r13+0x610; `item_desc` at +0xc; `box_spawn_chances` at +0x0 — see `game.h`). The table is 3 colors × 3 sizes, color-major: `[blue_small, blue_medium, blue_large, green_*, red_*]`. Vanilla sums the nine entries, rolls `HSD_Randi(total)`, walks the cumulative distribution to a `selected` index, then writes `selected / 3` → `*box_color` and `selected % 3` → `*box_size`.

The chance table lives in read-only `.dat` file data shared across spawn cycles, so it cannot be modified in place.

## Implementation

`GateBoxes_OnBoot()` installs `CODEPATCH_REPLACEFUNC(GrBoxGeneratorDetermine, GateBoxes_DetermineBoxType)`. The replacement (`GateBoxes_DetermineBoxType`):

1. Resolves `*stc_grBoxGeneInfo` (r13+0x610) and `*stc_grBoxGeneObj` (r13+0x608); returns `-1` if any of `info`, `info->item_desc`, `info->item_desc->box_spawn_chances`, or `obj` is null.
2. `memcpy`s the 9-entry chance table into a local `u8 chances[9]` (preserving the original `.dat` data).
3. For each color, if it is **not** unlocked in `ap_save->box_unlocked_mask` **or** `BoxHasItems(obj, color)` is false, zeros all three of its size entries (`chances[color*3 + {0,1,2}]`).
4. Sums the surviving chances; returns `-1` if the total is 0.
5. Rolls `HSD_Randi(total)`, walks the cumulative distribution to `selected`, writes `selected / 3` → `*box_color`, `selected % 3` → `*box_size`, and returns `*box_color`.

### The -1 return is safe

When no color is eligible the replacement returns `-1` (and, in that early-exit path, leaves `*box_color`/`*box_size` unwritten). `CityItemSpawn_Think` saves the `-1` return (r30) and forwards it as the first argument to `PowerUp_SpawnFromSky`, which checks `box_color == -1` at entry (0x800ecdfc) and returns 0 immediately — before touching `box_color`/`box_size` — so no box is placed.

This is **safer than vanilla**, which never returns `-1` on an all-zero chance table: with `total == 0` the cumulative walk never matches, so `selected` falls through to `9`, making `box_color = 9 / 3 = 3`. That trips the `box_color < 3` bounds check at 0x800ebda4, which calls `__assert` (0x804284b8) and panics. Returning `-1` instead masks that crash path on the locked-out edge case.

### Box auto-disable

A color is treated as locked for selection if its post-filter pool is empty, even when its `box_unlocked_mask` bit is set. `BoxHasItems` scans `obj->item_group_spawn[box].chance[i]` for `i < .num` (struct `grBoxGeneObj` in `game.h`) at decision time, so it always reflects the current pool after the items/patches/abilities filters have zeroed their entries — no separate update pass or cross-system ordering rule is needed. Without this, a player could open (e.g.) a green box and get nothing because every green-box item was independently locked.

The three pools are **disjoint**. `CityItemSpawn_InitItemFallChances` (0x800eb374) walks the stage's `item_spawn` table and appends each kind to exactly one `item_group_spawn[]` slot, chosen by `Gm_GetItemsCommonAttr(kind)->box_kind` (`ItemCommonAttr` +0x1c) — so which gate can empty a color is fixed:

| Color | Contents | Emptied by |
|-------|----------|-----------|
| Blue | the 8 stat patches with their 8 down variants, HP, All Up and the 12 foods | patches **and** items gating together |
| Green | Speed Max/Min, Offense Max, Defense Max, Charge Max/None, Candy, Fireworks, Panic Spin, Sensor Bomb, Gordo | items gating |
| Red | the 11 copy-ability panels | abilities gating |

The table has 52 entries, `ITKIND_ACCEL` through `ITKIND_GORDO`. The fake patches and the 6 Hydra/Dragoon pieces have no entry at all, so they never reach a box pool — the pieces are delivered only by the category 2 carrier box, and the fakes only by the event drop paths.

An entry is also skipped when its `fall_chance` for the current `StadiumGroup` column is 0. All Up is the one kind zeroed in City Trial's column, so vanilla never drops it from a city box; `GateItems_EnsureAllUpInSpawnPools` appends it to all three pools by hand under the Max Stats Insanity goal.

## Save Data

`u8 box_unlocked_mask` in `APSave` (`mods/archipelago/src/main.h`, accessed via the global `ap_save`). Bit N = `BoxKind` N (0 = BLUE, 1 = GREEN, 2 = RED).

The mask is also exposed through `ArchipelagoAPI` as the `AP_UNLOCK_BOX` category (`archipelago_api.c`), so `archipelago_debug` can read/write it. When the slot option `box_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` in `main.c` pre-fills the mask with `(1 << BOXKIND_NUM) - 1` at connect, so every color spawns with no AP items required.

## AP Items

3 AP items: `AP_BOX_UNLOCK_BASE` (860, `archipelago_api.h`) + `BoxKind` index → IDs 860–862. `ap_item_handler.c` maps a received ID in `[860, 860 + BOXKIND_NUM)` to `GateBoxes_UnlockBox(id - AP_BOX_UNLOCK_BASE)`.

`GateBoxes_UnlockBox` sets the bit, logs `[GateBoxes] Box %d (%s) unlocked (mask = %s)`, and enqueues `"Unlocked Box: <name>"` via `tb_api->EnqueueColoredNoun` using `BoxKind_Names[kind]` and `tb_api->BoxColors[kind]`.

## Design Decisions

**Copy-then-zero over in-place modification:** The chance table lives in `.dat` file data that's reloaded from disc but shared within a session. Modifying it in place would corrupt future spawn cycles. The local copy is safe and cheap (9 bytes).

**Auto-disable checked inline:** Rather than having each item/patch/ability filter track whether it emptied a box pool, `BoxHasItems` is evaluated when a box is being picked. This keeps the individual filters simple — they just remove items without worrying about box-level consequences.
