# AP Checklist (a custom checklist tab)

The **AP checklist** is the archipelago mod's custom checklist tab, registered on the
custom_checklist framework (`custom-checklist.md`). Its cells are AP *locations* —
objectives tracked by mod code — and completing one sends an AP location check exactly
like a vanilla checkbox; multiworld items can be placed *on* its cells for display.

The framework owns the presentation (synthetic-mode plumbing, minor scene, grid build,
theme recolor, banner/emblem swap) and the per-frame check evaluation. This doc covers
only the AP-specific wiring — `mods/archipelago/src/ap_checklist.c`, with the AP-side
reward/check integration in `checklist_rewards.c` / `check_detection.c`. For the tab
machinery itself, see `custom-checklist.md`.

## What the AP mod supplies

`APChecklist_Register` (called from `OnSaveLoaded`, after the framework mod has exported
its API) imports `CustomChecklistAPI` and hands it a descriptor with:

- **Checks** — a static `{ clear_kind, label, predicate }` table whose predicates read AP
  state (`ap_save` / `ap_data`) fed by hooks the mod already owns (`custom_items` pickups,
  `custom_events` triggers, EnergyLink thresholds, deathlink-survived, …) or lightweight
  counters. (The current set is Phase-1 stubs: boot the game, receive an item, receive 5
  items.)
- **Theme** — blue (`AP_THEME_*` in `ap_checklist.c`).
- **Tab art** — `ApChecklistTex` (see Tab artwork below).
- **Persistence callbacks** — `is_recorded` / `record_complete` (below); the AP tab owns
  its storage because it routes to a wire field.

`Register` returns the assigned mode. The framework appends each registered tab to the
next free mode index, so the AP tab's mode is **assigned at runtime** rather than fixed:
`APChecklist_Register` stores the return value in the global `ap_checklist_mode`
(defaulting to `GMMODE_NUM`), and the AP record path reads that variable. So the AP tab
adapts to whatever slot it lands on and registration order across mods does not matter —
nothing depends on it being exactly mode 3.

## Recording a completion

The descriptor's callbacks bind the framework's presentation to AP's authoritative
record:

- `is_recorded(clear_kind)` reads the `sent_checks_ap` bitmask (recorded =
  permanently complete, shown with no replay on a later boot).
- `record_complete(clear_kind)` calls `ClearChecker_SetNewUnlock(ap_checklist_mode,
  clear_kind)`, which `check_detection`'s REPLACEFUNC
  (`CheckDetection_SetNewUnlockReplacement`) intercepts for `ap_checklist_mode`: on a
  fresh cell it runs `RecordCheck` (sets `sent_checks_ap`, fires the "Check sent"
  textbox, re-evaluates goals) and, mid-run (unlock cache invalid), sets
  `clear[].is_new` and plays the unlock SFX. The framework seeds the cell's
  `is_new`/`is_visible` afterward so the flip-and-sparkle runs even for a check
  satisfied outside any gamemode (e.g. "Boot the game", which hits the cache-valid
  short-circuit).

So the AP tab's completion path is unchanged from a plain checklist objective:
predicate → `ClearChecker_SetNewUnlock` → `check_detection` → `sent_checks_ap` → AP.
The framework only adds the cell flags and animation around it.

`check_detection` reads/writes the AP cells through
`gmGetClearcheckerTypeP(ap_checklist_mode)`, which the framework serves from the AP
tab's `GameClearData` block — so the AP record path and the framework presentation
operate on the same block.

## Wire layout: append, don't widen in place

`APData` is read by the Python client *by field offset*. Widening
`sent_checks[3][2] → [4][2]` in place would shift every field after it
(deathlink/energylink mirrors, `goal_complete`) and break the live client. Instead
the AP-checklist slot is an **appended** field (`sent_checks_ap[2]`) at the end of
`APData`/`APSave`: existing offsets are preserved, and the client reads the trailing
field for the AP checklist. A `check_detection` slot accessor maps `mode →
sent_checks_ap` for any `mode >= GMMODE_NUM`, and `IsRecordedChecklistMode` accepts
the 3 real modes plus `ap_checklist_mode` — so the client's wire offsets stay fixed
no matter which slot the framework assigns the AP tab. The client never sees the mode
number; it reads `sent_checks_ap` directly.

`ChecklistRewards_ApplyCrossModeHasReward` (the post-reward-loop hook at
`0x8017e07c`) early-returns for `mode >= GMMODE_NUM`, so a custom mode never indexes
the `[GMMODE_NUM]`-sized `cross_mode_slots` out of bounds.

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

Replace the stub checks with the real custom objective set and their tracking hooks.
AP-protocol/apworld work (outside this repo): new location definitions, item-ID
range, and client reads for the appended `sent_checks_ap` field. Reusing the reward
pipeline means items placed on AP cells display for free.

## Files (mod side)

- `mods/archipelago/src/ap_checklist.c` / `.h` — the AP descriptor (checks + labels,
  blue theme, tab art, `is_recorded` / `record_complete` / `is_ready` callbacks) and
  `APChecklist_Register`.
- `mods/archipelago/assets/ApChecklistTex.dat` — the AP banner/emblem archive.
- `scripts/hsd/make_checklist_textures.py` — authors `ApChecklistTex.dat`.
- `mods/archipelago/src/main.h` / `main.c` — the runtime `ap_checklist_mode` (the
  framework-assigned mode) and the appended `sent_checks_ap`.
- `mods/archipelago/src/check_detection.c` — the mode-3 `sent_checks` slot accessor
  and the `ClearChecker_SetNewUnlock` REPLACEFUNC that records AP completions.
- `mods/custom_checklist/` — the framework that renders the tab (see
  `custom-checklist.md`).
