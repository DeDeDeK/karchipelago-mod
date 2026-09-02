# Box Type Gating

Each City Trial item-box color can be individually locked behind an Archipelago unlock item; a locked color never spawns. AP items 860-862 (`AP_BOX_UNLOCK_BASE` + `BoxKind`: 0 Blue, 1 Green, 2 Red) route through `ap_item_handler.c` to `GateBoxes_UnlockBox`, which sets the bit in `APSave.box_unlocked_mask` and posts a textbox. The mask is exposed through `ArchipelagoAPI` as `AP_UNLOCK_BOX`; when the slot option `box_gating_enabled` is 0 the connect-time pre-fill in `APOptions_ApplyUngatedCategories` (`main.c`) sets all three bits.

On top of the mask, a color is auto-disabled whenever the item gates have emptied its pool, so opening a box always awards something.

**File:** `mods/archipelago/src/gate_boxes.c`.

## Where the ID block ends

The machine unlock block sits directly below at 830-859 and its branch in `ap_item_handler.c` runs **before** the box branch, so its upper bound is clamped to the width of that block rather than following the number of machine kinds `custom_machines` has registered. Without the clamp the fifth registered machine would take ID 860 and Blue would never unlock.

## Game System

City Trial decides what to spawn in `CityItemSpawn_Think` (0x800eb108). It first calls `CityItemSpawn_UpdateAndCheckToSpawn` (0x800ea6e0), which returns a spawn *category*:

| Category | Meaning | What `CityItemSpawn_Think` does | Routes through the hook? |
|----------|---------|----------------------------------|--------------------------|
| 0 | Patch | `CityItemSpawn_GetRandomItemID` (0x800eb7e4); `box_color = box_size = -1` | No (patches gated separately) |
| 1 | Item box (**any** color) | `GrBoxGeneratorDetermine` at 0x800eb20c, return saved in r30 | **Yes** |
| 2 | Legendary machine-piece carrier | hardcodes `box_color = 2`, `box_size = 2` at 0x800eb218, then `CityItemSpawn_SpawnLegendaryPiece` (0x800ed384) attaches the Dragoon/Hydra part | No |
| 3 | Nothing | returns early | - |

The category has two sources inside `UpdateAndCheckToSpawn`. `CityItemSpawn_CheckToSpawnLegendaryPiece` (0x800ed2f0) returns **2** when a legendary piece is pending (the part flag is set and the round's progress threshold has passed) or **3** otherwise; when no piece is pending and the spawn cooldown has elapsed, a script-byte table (`DAT_805d617c`) selects **0** (patch) or **1** (box). **The script path never yields 2** - category 2 is reachable only through the legendary-piece subsystem.

Whichever branch runs, the resulting `box_color` is forwarded as the first argument to `PowerUp_SpawnFromSky` (0x800ecdf4) at 0x800eb260, which is what actually places the box.

**"Red box" is not one thing.** `GKYE01.map` labels category 1 "blue/green box" and category 2 "red box", which is misleading: category 1's picker selects from a 9-entry table that *includes* red, so normal red item boxes are fully gated here. Category 2 is not a normal box at all - it is the legendary piece carrier, which deliberately stays outside box-color gating. Locking Red suppresses every random red box; it does not hide Dragoon/Hydra part deliveries. A carrier is still a real red box, so it bumps the red-box break counter and keeps spawning with Red locked or the red pool empty. Breaking one while the red pool is empty is harmless: `CityItemSpawn_GetRandomItemID` walks a zero-length pool, `HSD_Randi(0)` returns 0 without dividing, and the roll falls through to `-1`, which spawns no item.

`GrBoxGeneratorDetermine` (0x800ebc04) reads a 9-entry chance table from `grBoxGeneInfo->item_desc->box_spawn_chances`. It is 3 colors x 3 sizes, color-major (`[blue_small, blue_medium, blue_large, green_*, red_*]`). Vanilla sums the nine, rolls `HSD_Randi(total)`, walks the cumulative distribution to a `selected` index, then writes `selected / 3` to `*box_color` and `selected % 3` to `*box_size`. The table lives in read-only `.dat` data shared across spawn cycles, so it cannot be edited in place - the replacement copies the nine bytes to the stack and zeroes there.

## Implementation

`GateBoxes_OnBoot()` installs `CODEPATCH_REPLACEFUNC(GrBoxGeneratorDetermine, GateBoxes_DetermineBoxType)`. The replacement runs vanilla's roll over a local copy of the chance table with every ineligible color's three size entries zeroed, and returns `-1` when nothing survives. A color is ineligible if its `box_unlocked_mask` bit is clear **or** `BoxHasItems()` finds no entry with `chance > 0` left in `obj->item_group_spawn[color]`.

### The -1 return is safe, and safer than vanilla

The picker's return is the box's `ItemKind`, which for the three vanilla colors is the color itself. `CityItemSpawn_Think` saves it in r30 and forwards it as `PowerUp_SpawnFromSky`'s `kind` argument, which tests `kind == -1` at entry (0x800ecdfc) and returns immediately, before touching `box_color`/`box_size` - so no box is placed and the unwritten out-params never matter.

The one thing that can still place a box on a `-1` is the AP Patch category, whose seam sits on the `bl` at 0x800eb20c and overrides the picker's return with the AP box's own kind. Box gating never sees that box: no color gate applies to it, and on a `-1` it writes its own color and size before the return reaches the spawn.

Vanilla has no such exit. With an all-zero chance table its cumulative walk never matches, `selected` falls through to 9, and `box_color` becomes 3. That trips the `box_color < 3` bounds check at 0x800ebda4, which calls `__assert` (0x804284b8) and panics. Returning `-1` masks that crash path on exactly the edge case gating creates.

### Auto-disable

`BoxHasItems` is evaluated at decision time rather than tracked by the filters, so it always reflects the pool as the copy-ability, patch and item gates have left it - no separate update pass and no cross-system ordering rule. Without it a player could open a green box and get nothing because every green-box item was independently locked.

The three pools are **disjoint**, so which gate can empty which color is fixed. `CityItemSpawn_InitItemFallChances` (0x800eb374) walks the stage's 52-entry `item_spawn` table (`ITKIND_ACCEL` through `ITKIND_GORDO`) and appends each kind to exactly one `item_group_spawn[]` slot, chosen by `Gm_GetItemsCommonAttr(kind)->box_kind`:

| Color | Contents | Emptied by |
|-------|----------|-----------|
| Blue | the 8 stat patches with their 8 down variants, HP, All Up and the 12 foods | patch **and** item gating together |
| Green | Speed Max/Min, Offense Max, Defense Max, Charge Max/None, Candy, Fireworks, Panic Spin, Sensor Bomb, Gordo | item gating |
| Red | the 11 copy-ability panels | copy-ability gating |

The fake patches and the six Hydra/Dragoon pieces have no `item_spawn` entry at all, so they never reach a box pool - the pieces are delivered only by the category 2 carrier, the fakes only by the event drop paths. An entry is also skipped when its `fall_chance` for the current `StadiumGroup` column is 0; All Up is the one kind zeroed in City Trial's column, so vanilla never drops it from a city box.
