# Checklist Grid Geometry (the 12x10 cell layout)

How the checklist's visible grid of checkboxes is built, positioned, navigated and
counted, and exactly which functions hardcode the **12 columns x 10 rows = 120 cells**
layout. This is the layout/rendering layer only; it reads the completion data and never
writes it.

The data it reads is one `GameClearData` per checklist mode (`gmGetClearcheckerTypeP`,
`0x800076a0`): a `grid_mapping[120]` permutation giving each `clear_kind` its physical
slot, and a parallel `clear[120]` of per-cell bits - `is_new` 0x01, `is_filler` 0x02,
`is_unlocked` 0x04, `has_reward` 0x08, `is_visible` 0x10. The constants live in
`externals/hoshi/include/game.h` as `CHECKLIST_GRID_COLS` (12) / `CHECKLIST_GRID_ROWS` (10).

## The Grid Is Procedural, Not Baked Art

There is **no 120-cell model**. The checklist art archive `MnClCheckAll.dat`
(public root `ScMenClearchecker_scene_data`, an `HSD_SOBJ`) contains only
**single-quad** building blocks:

| Public (`ScMenClearchecker..._scene_models`) | Role |
|---|---|
| `Bg` | backdrop panels (18 joints, decorative - no cell grid) |
| `Frame1/2/3` | the per-mode 248x128 banner panel (one textured quad each) |
| `Pos` | **6 corner reference joints** - the grid's spacing/extent source |
| `Gray`, `Green`, `Gold`, `Red`, `Purple1/2` | one cell tile each (a single quad) |
| `Cross`, `Complete` | the X / checkmark overlays (single quads) |
| `Cursol` | the moving cursor highlight (single quad) |
| `Prize1/2`, `Win` | reward/win indicators |

The visible grid is assembled at runtime by **instancing one cell-tile quad per
occupied cell**, each repositioned to its grid slot. A cell's screen position is
derived from the `Pos` model's corner joints (giving a fixed per-cell `xstep` /
`ystep`) times its `(col, row)`. Because spacing is fixed per cell, the grid is
top-left anchored; it does not stretch to fill the panel.

So reshaping the grid needs **no new art** - cells reflow from arithmetic. What is
hardcoded is that arithmetic (the column count 12 and total 120), spread across the
functions below.

## Cell GObjs and Their Lifecycle

The occupied cells are **persistent GObjs**, one per grid slot, held in the array
**`MainMenuData + 0xf0c[120]`** (indexed by physical slot). The cell-state model
templates are loaded into `MainMenuData + 0xef0 + state*4`.

| Stage | Function | Address | Role |
|---|---|---|---|
| Load templates | `Checklist_LoadModels` | `0x801821ac` | `Archive_GetSymbols` binds `Bg`/`Frame*`/cell-state/`Pos`/`Cursol`... into `MainMenuData` slots (`0xecc`...`0x1128`). |
| Build screen | `Checklist_Init` | `0x801822f4` | Creates the grid element (`+0xed0`, runs `Checklist_Think`), the banner (`+0xee4`), the `Pos`-spacing element (`+0xeec`, joints -> `xstep`/`ystep`/origin in its user-data), the filler indicators, the cursor (`+0x1104`, `Checklist_Update`), the hover/info (`+0x110c`, `Checklist_UpdateCellInfo`), the reward icon, and the completion-counter text. Calls the builder. |
| **Build/refresh cells** | `Checklist_SetRewardFlagOnUnlocks` | `0x8017df5c` | **The grid builder** (below). |
| Tear down | `Checklist_DestroyElements` | `0x80182cac` | Destroys `+0xed0`/`+0xee4`/`+0xeec`, the `0xf0c[120]` cell array, the filler array, and the cursor/info/icon elements. |
| Scene leave | `Checklist_MinorLeave` | `0x80138e00` | Cleanup wrapper that calls `Checklist_DestroyElements`. |

## The Grid Builder (`Checklist_SetRewardFlagOnUnlocks`, `0x8017df5c`)

Despite its name (an earlier loop does set `has_reward` on reward cells), this function
**creates and positions the cell tiles**. Its cell loop (~`0x8017e0c0`-`0x8017e2cc`)
walks physical slots `0..119` against a hardcoded `0x77` bound, reverse-scans
`grid_mapping` for the `clear_kind` living in each slot, and picks a cell-state template
from that cell's bits in priority order: `is_filler` -> 3, `has_reward` -> 1,
`is_unlocked` -> 2, `is_visible` -> 0, **none -> no cell at all**. It creates the GObj on
first use, caches it in `MMD+0xf0c[slot]`, and destroys and recreates it when the state
changes.

- **A cell with none of those four bits gets no GObj** - it is truly absent, not an
  empty box. The number of boxes therefore already equals the number of cells the data
  marks visible, and since `is_visible` is only ever granted to the neighbours of a
  completed cell, a fresh board draws nothing at all.
- The **column count 12** appears here as `slot % 12` (column) and `slot / 12` (row),
  which then multiply the `Pos`-derived `xstep`/`ystep` to give the tile's translation.
- Cell **size/spacing** comes from the `Pos` element's joints (`Checklist_Init` reads
  them); it is independent of the column count.

## Where 12x10 Is Hardcoded

Every surface that maps between a cursor `(col, row)` and a `clear_kind` uses
`phys_slot = col + row*12` plus a reverse scan of `grid_mapping`. The reverse scans
search all 120 entries and are **column-count-agnostic**; only the `phys_slot`
arithmetic and the cursor bounds bake in 12. `ClearCheckerUI` holds the cursor as
`cursor_col` (`+0x17`) and `cursor_row` (`+0x18`), both `0x81` until first placed.

| Function | Address | Hardcoded geometry |
|---|---|---|
| `Checklist_Think` | `0x8017f3bc` | cursor movement: `phys_slot = col + row*12`; column bound `col < 12`; last-column test `col % 12 < 11`; skip-empty navigation; reverse-scan loops |
| `Checklist_Update` | `0x8018161c` | cursor-highlight position `X = col*xstep, Y = row*ystep` (`stfs` to the cursor JObj's `+0x38` translation, from `cursor_col` at `+0x17`); reverse-scan; the "Clearchecker Number 120" assert when a cursor slot is unmapped |
| `Checklist_UpdateCellInfo` | `0x80181d70` | hover tooltip: unrolled 12x10 reverse-scan cursor -> clear_kind |
| `Checklist_InitGridMapping` | `0x8004a2bc` | fills `grid_mapping[120]`, pre-places the meta cells, randomizes the remainder (per-mode) |
| `Checklist_Init` (counter) | `0x801822f4` | completion counter scans 12x10 cells counting `clear[k] & 0x06` and prints the number. Order-independent, so a column-count change would not affect it. |

The reverse scan is why `grid_mapping` must stay a **full bijection over `0..119`** even
when most cells are invisible: any cursor slot that maps to no `clear_kind` trips
`Checklist_Update`'s "Clearchecker Number 120" assert.

## The Board Backdrop Is the Banner Quad, Not the Cells

The full board of empty checkbox outlines a player sees is **not** drawn by the cell
GObjs. Only cells with a state bit get a GObj, so a tab with N visible checks creates
exactly N cell quads. The board *backdrop* - the gray panel that reads as a 12x10 grid -
is the single **banner quad** at `MMD+0xee4` (the `Frame1/2/3` model, a 248x128 textured
quad), anchored at the top-left cell and sized to span the whole board. The cell quads
draw their fill/colour *on top of* that panel.

So shrinking the cell count alone leaves the full-board panel behind it, and the board
still looks 12x10. A visible resize needs **both** halves.

## Resizing Is a Code Change, and Mode-Aware

No resize ships. Every checklist tab - the three vanilla modes and any mod-registered
synthetic one - uses the full 12x10 board with every engine function above unpatched.
A tab with fewer than 120 checks therefore reads as a sparse full-size board rather than
a small dense one: the banner panel and the City Trial chrome (the rounded frame in the
`Bg` scene at `MMD+0xed0`, the L/R tab arrows, the tab emblem, the completion counter)
still span 12x10, and the **cursor roams all 120 positions**. `Checklist_Think`'s
movement (cases 4 and 8) is plain `col+-1` / `row+-1` against the hardcoded `11` / `9`
wrap with no skip-empty step, so it can sit on - and spend a checkbox filler on - any
slot, occupied or not.

Reshaping to `C` columns x `R` rows means changing the cell positioning in
`Checklist_SetRewardFlagOnUnlocks`, the cursor arithmetic and bounds in
`Checklist_Think`, and the `col + row*C` lookups in `Checklist_Update` /
`Checklist_UpdateCellInfo`; the reverse scans and the completion counter need nothing.
Cell size would also have to change, by scaling the `xstep`/`ystep` `Checklist_Init`
derives from the `Pos` element.

Two constraints bound any such change. First, these functions are **shared by all
checklist modes**, so a resize has to branch on mode - vanilla 12x10 for modes 0-2,
custom `C x R` only for a synthetic tab (`mode >= GMMODE_NUM`). That rules out blind
instruction patches and points at `REPLACEFUNC` reimplementations reading a per-tab
`(cols, rows)`. Second, cursor bounds and occupied slots have to agree, because
`grid_mapping` must stay a bijection over all 120 entries regardless of the visible
shape.

Geometry writes must come from in-frame mod code: an asynchronous write to these
JObjs from outside the frame races the per-frame `HSD_JObjAnimAll` tree walk.
