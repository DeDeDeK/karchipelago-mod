# Checklist Grid Geometry (the 12×10 cell layout)

How the checklist's visible grid of checkboxes is built, positioned, navigated,
and counted — and exactly which functions hardcode the **12 columns × 10 rows =
120 cells** layout. This is the layout/rendering layer; the completion *data* it
reads (`GameClearData.clear[]` / `grid_mapping[]`) is in `clearchecker-system.md`,
and the synthetic-mode plumbing that lets a mod own a tab is in
`custom-checklist.md`.

## The grid is procedural, not baked art

There is **no 120-cell model**. The checklist art archive `MnClCheckAll.dat`
(public root `ScMenClearchecker_scene_data`, an `HSD_SOBJ`) contains only
**single-quad** building blocks:

| Public (`ScMenClearchecker…_scene_models`) | Role |
|---|---|
| `Bg` | backdrop panels (18 joints, decorative — no cell grid) |
| `Frame1/2/3` | the per-mode 248×128 banner panel (one textured quad each) |
| `Pos` | **6 corner reference joints** — the grid's spacing/extent source |
| `Gray`, `Green`, `Gold`, `Red`, `Purple1/2` | one cell tile each (a single quad) |
| `Cross`, `Complete` | the X / checkmark overlays (single quads) |
| `Cursol` | the moving cursor highlight (single quad) |
| `Prize1/2`, `Win` | reward/win indicators |

The visible grid is assembled at runtime by **instancing one cell-tile quad per
occupied cell**, each repositioned to its grid slot. A cell's screen position is
derived from the `Pos` model's corner joints (giving a fixed per-cell `xstep` /
`ystep`) times its `(col, row)`. Because spacing is fixed per cell, the grid is
top-left anchored; it does not stretch to fill the panel.

**Consequence for resizing:** reshaping the grid needs **no new art** — cells
reflow from arithmetic. What is hardcoded is that arithmetic (the column count 12
and total 120), spread across the functions below.

## The cell-GObj array and its lifecycle

The occupied cells are **persistent GObjs**, one per grid slot, held in the array
**`MainMenuData + 0xf0c[120]`** (indexed by physical slot). The cell-state model
templates are loaded into `MainMenuData + 0xef0 + state*4`.

| Stage | Function | Address | Role |
|---|---|---|---|
| Load templates | `Checklist_LoadModels` | `0x801821ac` | `Archive_GetSymbols` binds `Bg`/`Frame*`/cell-state/`Pos`/`Cursol`… into `MainMenuData` slots (`0xecc`…`0x1128`). |
| Build screen | `Checklist_Init` | `0x801822f4` | Creates the grid element (`+0xed0`, runs `Checklist_Think`), the banner (`+0xee4`), the `Pos`-spacing element (`+0xeec`, joints → `xstep`/`ystep`/origin in its user-data), the filler indicators, the cursor (`+0x1104`, `Checklist_Update`), the hover/info (`+0x110c`, `Checklist_UpdateCellInfo`), the reward icon, and the completion-counter text. Calls the builder. |
| **Build/refresh cells** | `Checklist_SetRewardFlagOnUnlocks` | `0x8017df5c` | **The grid builder** (below). |
| Tear down | `Checklist_DestroyElements` | `0x80182cac` | Destroys `+0xed0`/`+0xee4`/`+0xeec`, the `0xf0c[120]` cell array, the filler array, and the cursor/info/icon elements. |
| Scene leave | `Checklist_MinorLeave` | `0x80138e00` | Cleanup wrapper that calls `Checklist_DestroyElements`. |

## The grid builder (`Checklist_SetRewardFlagOnUnlocks`, `0x8017df5c`)

Despite its name (it also sets the `has_reward` flag on reward cells in an earlier
loop), this function **creates and positions the cell tiles**. The cell loop
(≈`0x8017e0c0`–`0x8017e2cc`):

```
for phys_slot in 0 .. 119:                       // hardcoded 0x77 bound
    clear_kind = reverse_scan(grid_mapping, phys_slot)   // 12×10 unrolled search
    state = state_from_bits(clear[clear_kind]):
        is_filler(0x02) -> 3
        has_reward(0x08) -> 1
        is_unlocked(0x04) -> 2
        is_visible(0x10) -> 0
        none            -> -1                     // no cell drawn at all
    cell = MMD[0xf0c + phys_slot*4]
    if state == -1:  (leave empty)
    elif cell == NULL:
        cell = CreateElementGObj(MMD[0xef0 + state*4])   // template by state
        MMD[0xf0c + phys_slot*4] = cell
        col = phys_slot % 12 ; row = phys_slot / 12       // hardcoded 12
        cell.x = (col - origin) * xstep + originX          // from Pos element
        cell.y =  row          * ystep + originY
        cell.state = state + 1
    elif cell.state != state+1:
        destroy + recreate (state changed)
```

Key facts:
- **A non-visible cell (`clear[]` has none of `is_visible`/`has_reward`/
  `is_unlocked`/`is_filler`) gets no GObj** — it is truly absent, not an empty
  box. So the number of boxes already equals the number of cells the data marks
  visible. A custom tab that sets `is_visible` on only N cells already draws only
  N boxes.
- The **column count 12** appears here as `phys_slot % 12` / `phys_slot / 12`.
- Cell **size/spacing** comes from the `Pos` element's joint-derived `xstep` /
  `ystep` (`Checklist_Init` reads them); it is independent of the column count.

## Cursor + readout: the rest of the 12×10 hardcoding

Every surface that maps between a cursor `(col, row)` and a `clear_kind` uses
`phys_slot = col + row*12` plus a reverse scan of `grid_mapping`. The reverse
scans search all 120 entries and are **column-count-agnostic**; only the
`phys_slot` arithmetic and the cursor bounds bake in 12.

| Function | Address | Hardcoded geometry |
|---|---|---|
| `Checklist_Think` | `0x8017f3bc` | cursor movement: `phys_slot = col + row*12`; column bound `col < 12`; last-column test `col % 12 < 11`; skip-empty navigation; reverse-scan loops |
| `Checklist_Update` | `0x8018161c` | cursor-highlight position `X = col*xstep, Y = row*ystep`; reverse-scan; the "Clearchecker Number 120" assert when a cursor slot is unmapped |
| `Checklist_UpdateCellInfo` | `0x80181d70` | hover tooltip: unrolled 12×10 reverse-scan cursor→clear_kind |
| `Checklist_InitGridMapping` | `0x8004a2bc` | fills `grid_mapping[120]`, pre-places the meta cells, randomizes the remainder (per-mode). Custom tabs bypass it — the framework writes an identity `grid_mapping`. |
| `Checklist_Init` (counter) | `0x801822f4` | completion counter scans 12×10 cells counting `clear[k] & 0x06` and prints the number. Order-independent: counts completed cells regardless of layout, so the count is unaffected by a column-count change. |

## Resizing the visible grid — what would change

Because the grid is procedural, a different shape is a code change, not an art
change. To render N visible cells in **C columns × R rows** (per tab):

1. **`Checklist_SetRewardFlagOnUnlocks`** — position cells with `col = slot % C`,
   `row = slot / C` instead of `% 12` / `/ 12`.
2. **`Checklist_Think`** — cursor `phys_slot = col + row*C`; column bound `C`;
   last-column test `col % C < C-1`; row bound `R`.
3. **`Checklist_Update`** / **`Checklist_UpdateCellInfo`** — same `col + row*C`
   for highlight position and hover lookup.
4. `grid_mapping` stays identity (already set by the framework); every reachable
   cursor `phys_slot` must map to a `clear_kind` or `Checklist_Update` asserts, so
   cursor bounds and the occupied slots must agree.
5. The completion counter and the reverse scans need **no** change.

The cell **size** is unchanged (it comes from the `Pos` joints); to rescale or
center a smaller grid, also scale the `xstep`/`ystep` that `Checklist_Init`
derives from the `Pos` element.

**Scoping constraint:** these functions are shared by all checklist modes, so any
resize must be **mode-aware** — vanilla `12×10` for Air Ride / Top Ride / City
Trial (modes 0–2), custom `C×R` only for a synthetic tab (`mode >= GMMODE_NUM`).
That rules out blind instruction patches (which can't branch on mode) and points
at `REPLACEFUNC` reimplementations that read a per-tab `(cols, rows)`.

## What "the grid" is visually: the banner panel, not the cells

The full board of empty checkbox outlines a player sees is **not** drawn by the
cell GObjs. Only cells with a state bit get a GObj (an unused slot draws nothing),
so a tab with N visible checks already creates exactly N cell quads. The board
*backdrop* — the gray panel that reads as a 12×10 grid — is the single **banner
quad** at `MMD+0xee4` (the `Frame1/2/3` model, a 248×128 textured quad), anchored
at the top-left cell and sized to span the whole board. The cell quads draw their
fill/colour *on top of* that panel.

So shrinking the cell count alone leaves the full-board panel behind it, and the
board still looks 12×10. A visible resize needs **both** halves.

## The shipped resize (custom_checklist `grid_cols`)

The framework takes a lighter path than the `REPLACEFUNC` reimplementations above,
and it is mode-scoped for free (it only ever touches a custom tab's own data):

1. **Cells — reshape via `grid_mapping`.** `CC_InitClearData` keeps the engine's
   native `% 12` / `/ 12` placement but re-points the tab's `grid_mapping` so the
   visible cells occupy a `cols`-wide top-left block (packed row-major), and the
   hidden cells take the remaining slots. `grid_mapping` stays a full bijection of
   `0..119`, so `Checklist_Update`'s reverse scan never trips the
   "Clearchecker Number 120" assert. No engine function is replaced; the builder
   and cursor stay self-consistent through the permutation.
2. **Banner panel — scale it.** `CC_RetargetBannerJObj` finds the 248-wide quad
   and, when `grid_cols` is `1..11`, scales it `x *= cols/12`, `y *= rows/10`
   (trans untouched; the top-left anchor keeps the block aligned).
   `rows = ceil(check_num / cols)`. The natural scale is captured per banner
   instance for an idempotent per-frame re-apply (recaptured across rebuilds).
3. **Cursor — clamp it into the block.** `Checklist_Think`'s cursor movement
   (cases 4 and 8) is plain `col±1` / `row±1` with hardcoded `11` / `9` wrap and
   **no skip-empty step** — left to itself the cursor roams (and can select) all
   120 board positions regardless of which cells exist. `CC_ClampChecklistCursor`,
   hooked at `0x80181678` in `Checklist_Update` (right after the grid element's
   user data `chk` is resolved and before the cursor col/row at `chk+0x17`/`+0x18`
   are first read to position the highlight), clamps `col` to `[0, cols-1]` and
   `row` to `[0, rows-1]` and writes them back. Because the highlight is positioned
   from the clamped value the cursor never draws outside the block, and the
   write-back means the next `Checklist_Think` resumes in-block (so a press into the
   edge stalls there rather than wrapping into the empty region). The `0x81`
   filler-browse sentinel is left untouched. No-op for the native layout.

This shrinks the *fill block* and the *banner panel* and confines the *cursor*
without reimplementing `Checklist_Think`/`Update`. The cursor still moves in
12-wide logical space; the clamp keeps it inside the block. The cell **size** is
unchanged — a narrower grid is left-aligned, not centred.

What is **not** reshaped: the City Trial board's decorative chrome — the rounded
frame (beaded corner/edge arcs in the `Bg` scene at `MMD+0xed0`, I8 grayscale
pieces sized for the 12×10 board), the L/R tab arrows, the tab emblem, and the
completion counter — is laid out for the full board and still spans it, so a
narrowed tab reads as a small block inside a full-size frame.

Note: live-poking these JObjs from an external memory tool (e.g. dolphin-memory)
to derive the scale is unreliable — the async write races the per-frame
`HSD_JObjAnimAll` tree walk and faults. Apply geometry changes from in-frame mod
code (as above), where the writes are synchronous.
