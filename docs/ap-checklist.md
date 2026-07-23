# AP Checklist (a custom checklist tab)

The **AP checklist** is the archipelago mod's custom checklist tab, registered on the
custom_checklist framework. Its cells are AP *locations* — objectives tracked by mod
code — and completing one sends an AP location check exactly like a vanilla checkbox;
multiworld items can be placed *on* its cells for display.

The framework owns the presentation (synthetic-mode plumbing, minor scene, grid build,
theme recolor, banner/emblem swap) and the per-frame check evaluation. This doc covers
only the AP-specific wiring — `mods/archipelago/src/ap_checklist.c`, with the AP-side
reward/check integration in `checklist_rewards.c` / `check_detection.c`.

## Two mode identities

`GMMODE_NUM` (3) means "the three real game modes". It still sizes the reward tables,
because the AP tab awards no native rewards of its own.

Per-checklist-mode *recorded* state is one row wider: `CHECKLIST_MODE_NUM` (4), with the
AP tab always at the fixed row `AP_CHECKLIST_ROW` (== `GMMODE_NUM`). That row index is
compile-time constant and is what the wire and the save both store.

The tab's *runtime* mode is different and is assigned dynamically: the framework appends
each registered tab to the next free mode index, so the AP tab's mode is whatever slot it
lands on, `>= GMMODE_NUM`. `APChecklist_Register` stores it in the global
`ap_checklist_mode` (defaulting to `GMMODE_NUM`).

`ChecklistModeRow(mode)` in `check_detection.h` maps a runtime mode to its row (and
`ChecklistRowMode(row)` back, for the game APIs like `gmGetClearcheckerTypeP` that index
by runtime mode). It is the single answer to "which row is this mode" — consumers call it
rather than re-deriving the mapping. So registration order across mods does not matter and
nothing depends on the tab being exactly mode 3.

## What the AP mod supplies

`APChecklist_Register` (called from `OnSaveLoaded`, after the framework mod has exported
its API) imports `CustomChecklistAPI` and hands it a descriptor with:

- **Checks** — a static `{ clear_kind, label, predicate }` table whose predicates read AP
  state (`ap_save` / `ap_data`). The current set is three stubs: boot the game, receive an
  item, receive 5 items.
- **Theme** — blue (`AP_CHECKLIST_NAME` / `AP_THEME_*` in `ap_checklist.h`, shared with
  every textbox that names the tab so the wording and tint stay one value; the tab's
  runtime mode is `>= GMMODE_NUM` and so has no `ModeColors[]` slot of its own).
- **Tab art** — `ApChecklistTex` (see Tab artwork below).
- **Persistence callbacks** — `is_recorded` / `record_complete` (below); the AP tab owns
  its storage because it routes to a wire field.
- **Readiness** — `is_ready` returns `ap_data && ap_data->game_ready`. The framework's
  evaluator no-ops until it holds, so `record_complete`'s textbox enqueue is safe.

The framework's evaluator runs from its own `OnFrameStart`, so every predicate is polled
every frame in every scene. Predicates must be cheap pure reads that are safe outside
gameplay.

## Recording a completion

The descriptor's callbacks bind the framework's presentation to AP's authoritative
record:

- `is_recorded(clear_kind)` reads `ap_save->sent_checks[AP_CHECKLIST_ROW]` (recorded =
  permanently complete, shown with no replay on a later boot).
- `record_complete(clear_kind)` calls `ClearChecker_SetNewUnlock(ap_checklist_mode,
  clear_kind)`, which `check_detection`'s `CODEPATCH_REPLACEFUNC`
  (`CheckDetection_SetNewUnlockReplacement`) intercepts for `ap_checklist_mode`: on a
  fresh cell it runs `RecordCheck`, which resolves the row via `ChecklistModeRow`, sets
  the `sent_checks` bit, fires the "Check sent" textbox and re-evaluates goals — and,
  mid-run (unlock cache invalid), sets `clear[].is_new` and plays the unlock SFX. The
  framework seeds the cell's `is_new`/`is_visible` afterward so the flip-and-sparkle runs
  even for a check satisfied outside any gamemode (e.g. "Boot the game", which hits the
  cache-valid short-circuit).

So the AP tab's completion path is unchanged from a plain checklist objective:
predicate → `ClearChecker_SetNewUnlock` → `check_detection` → `sent_checks` row → AP.
The framework only adds the cell flags and animation around it.

`check_detection` reads and writes the AP cells through
`gmGetClearcheckerTypeP(ap_checklist_mode)`, which the framework serves from the AP tab's
`GameClearData` block — so the AP record path and the framework presentation operate on
the same block.

The framework only marks `is_visible` on cells backed by a check-table entry, so
clear_kinds with no `ap_checks[]` entry do not draw.

## Wire layout

`APData` is read by the Python client *by field offset*, so its layout is a contract.
Every per-checklist-mode array in `APData` / `APSlotOptions` / `APSave` is
`CHECKLIST_MODE_NUM` wide (`goal`, `checklist_amount`, `goal_checks`, `sent_checks`,
`client_backfill`, `goal_announced`), with the AP tab's entry at `AP_CHECKLIST_ROW` — a
regular row, not an appended tail field. The client never sees the framework-assigned mode
number; it indexes the AP row directly, which is why the runtime mode can move freely.

A block of `_Static_assert(offsetof(APData, f) == 0xNNN, "")` in `main.c` pins the block
boundaries and both edges of every per-mode array. Nothing else checks the layout — the
compiler picks the offsets and the client restates them by hand — so extend that block
when adding fields, and move both sides together.

## Cross-mode rewards

`ChecklistRewards_ApplyCrossModeHasReward` (the post-reward-loop hook at `0x8017e07c`)
resolves its row with `ChecklistModeRow` and returns early on `-1`, so the AP tab hosts
cross-mode rewards like any other mode while any *other* custom tab — which has no row —
is skipped. `cross_mode_slots` is `CHECKLIST_MODE_NUM` wide to match.

`RebuildRewardTablesFromShuffle` is the one place that must *not* call `ChecklistModeRow`:
the wire encodes a reward's target as a checklist-mode **row** already (the client writes
`KARData.GameMode`, whose `ARCHIPELAGO` member is `AP_CHECKLIST_ROW` by definition), so it
bounds-checks the value instead of mapping it.

## Tab artwork

`mods/archipelago/assets/ApChecklistTex.dat` is a loadable HSD archive (staged to
the FST root) exporting two `_HSD_ImageDesc` publics:

- `apBannerImg` — RGB5A3 248×128 panel that backs the checkbox grid, the AP logo
  baked in as a faint watermark so the panel stays opaque under the grid.
- `apEmblemImg` — I4 64×64 intensity map of the logo for the top-right tab
  indicator; intensity doubles as alpha and the quad takes the blue theme tint.

`scripts/hsd/make_checklist_textures.py` authors the archive from
`mods/archipelago/assets/ap-icon.png`
(`uv run --with pillow python scripts/hsd/make_checklist_textures.py`). The framework
loads it by name (`tex_file = "ApChecklistTex"`) per tab build and swaps the
checklist's banner/emblem TObjs onto these descriptors.

## Goal

Replace the three stub checks with the real custom objective set and their tracking hooks.
The apworld side is done — 36 AP locations are defined, and the location code is
`361 + clear_kind`, so each `clear_kind` must match `ap_checks[]` order. That pairing is a
cross-repo wire contract with nothing mechanically catching a desync.

An AP box whose check never fires is worse than one that doesn't exist: the location still
exists in the multiworld and logic still treats it as reachable, so fill can strand
progression on it. Reusing the reward pipeline means items placed on AP cells display for
free.

## Files (mod side)

- `mods/archipelago/src/ap_checklist.c` / `.h` — the AP descriptor (checks + labels,
  blue theme, tab art, `is_recorded` / `record_complete` / `is_ready` callbacks) and
  `APChecklist_Register`.
- `mods/archipelago/assets/ApChecklistTex.dat` — the AP banner/emblem archive.
- `scripts/hsd/make_checklist_textures.py` — authors `ApChecklistTex.dat`.
- `mods/archipelago/src/main.h` / `main.c` — the wire structs, `CHECKLIST_MODE_NUM` /
  `AP_CHECKLIST_ROW`, the runtime `ap_checklist_mode`, and the offset assertions.
- `mods/archipelago/src/check_detection.c` / `.h` — `ChecklistModeRow` /
  `ChecklistRowMode`, and the `ClearChecker_SetNewUnlock` REPLACEFUNC that records AP
  completions.
- `mods/archipelago/src/checklist_rewards.c` — cross-mode reward placement onto AP cells.
- `mods/custom_checklist/` — the framework that renders the tab.
