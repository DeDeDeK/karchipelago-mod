# Custom Checklist (framework for extra checklist tabs)

`mods/custom_checklist/` adds mod-owned checklist tabs alongside the three vanilla ones
(Air Ride / Top Ride / City Trial), folded into the existing L/R tab rotation. Each
registered tab is a synthetic checklist mode (index `>= GMMODE_NUM`) backed by a mod-owned
`GameClearData` served through the engine's clear-checker accessor, so the engine renders
its grid, completion counter and unlock animation as if it were a fourth game mode.

## Architecture

The framework owns the **presentation + per-frame evaluation**; a registering mod owns
the **objectives** (a static check table) and optionally **where a completion is
recorded** (persistence callbacks). With nothing registered the installed REPLACEFUNCs
reproduce vanilla behavior, so a build with no consumer is inert. The archipelago mod is
the first consumer (`ap-checklist.md`).

An extra tab is cheap because the in-game checklist is **one shared, mode-parameterized
screen**, not three separate ones, and it **already cycles through the three modes with
L/R**. Adding a tab means adding an entry to a rotation that already exists; almost
everything renders for free once the synthetic mode is plumbed through.

- The checklist is **minor-scene kinds `0x20`/`0x21`/`0x22`** (`MNRKIND_AIRRIDECHECKLIST`
  = 32 / `TOPRIDECHECKLIST` / `CITYCHECKLIST`), all sharing one set of load/think/leave
  callbacks. The shared `cb_Load` (`0x8004a768`, unnamed in the symbol map) derives
  `mode = Scene_GetCurrentMinor() − 0x20` and calls `Checklist_Init(mode, fresh)`
  (`0x801822f4`), which writes `mode` into `ClearCheckerUI` (`+0x14`).
- `gmGetClearcheckerTypeP(mode)` (`0x800076a0`) is a 3-way `switch` returning the per-mode
  `GameClearData` embedded in `GameData`. For `mode >= 3` it asserts and returns NULL —
  **this is the master lever**.
- Input in `Checklist_Think` (`0x8017f3bc`): **R|X → next tab, L|Y → prev tab, B → exit**
  (`ClearCheckerPhase` 12 / 13 / 11), consumed by `Checklist_MinorThink` (`0x8004a648`).

## Engine Levers (mode-keyed surfaces)

The framework installs these REPLACEFUNCs at boot. They reproduce vanilla for the real
modes and handle the synthetic ones; with no tab registered they are behaviour-neutral.

| Function | Address | Custom-mode behavior |
|---|---|---|
| `gmGetClearcheckerTypeP` | `0x800076a0` | serve the registered tab's `GameClearData` for its mode; NULL for unknown modes (no assert) |
| `Checklist_GetRewardNum` | `0x80049c20` | `0` (custom tabs host no native rewards) — gates every reward loop in the render path off and dodges the `mode>=3` assert. Real modes keep the vanilla counts (AR 46 / TR 33 / CT 44), restated as constants |
| `Checklist_GetClearKindFromRewardIndex` | `0x80049c84` | `0` (no rewards) — keeps `Checklist_ProcessUnlock`'s first new-unlock scan inert so the cell flip-animation can run, and dodges the assert |
| `Checklist_MinorThink` | `0x8004a648` | reimplements the tab cycle with custom tabs folded into the ring |
| `ClearChecker_CheckForNewUnlocks` | `0x8004a1a4` | vanilla result OR any custom tab pending — routes the post-run checklist even when only a custom check went new |
| `Scene_SetNextMinor` | `0x800088c8` | post-run retarget to a custom tab when the played mode has nothing to animate |

Because `Checklist_GetRewardNum` reports 0 for the custom mode, the per-mode reward tables
(`stc_reward_table_ptrs`, the reward counts, the audio-preview and special-reward tables)
are never indexed at the custom mode, so no table relocation is needed.
`Checklist_InitGridMapping` and `Checklist_UpdateCellInfo` are already mode-safe (they key
off the `GameClearData*` and cell geometry).

## Registration

A consumer imports the API via `Hoshi_ImportMod(CUSTOM_CHECKLIST_MOD_NAME, …)` and calls
`Register(desc)`. Because the framework mod boots after most others (alphabetical order),
import + register run from **`OnSaveLoaded`**, not `OnBoot`. `Register` returns the
assigned mode (`GMMODE_NUM` for the first registrant, then `+1` each) or `-1` on failure.

The descriptor (`custom_checklist_api.h`):

```c
typedef struct CustomChecklistDesc {
    const char *name;                    // identification / logging
    u8 theme_r, theme_g, theme_b;        // tab tint
    const char *tex_file;                // tab art archive (NULL = keep CT art)
    const char *banner_symbol;           // 248x128 RGB5A3 banner image-desc public
    const char *emblem_symbol;           // I4 tab-emblem image-desc public
    const CustomCheck *checks;           // static check table (kept by pointer)
    int check_num;
    int  (*is_recorded)(int clear_kind);     // OPTIONAL - already completed?
    void (*record_complete)(int clear_kind); // OPTIONAL - mod owns persistence
    void (*on_complete)(int clear_kind);     // OPTIONAL - cue on first completion
    int  (*is_ready)(void);                  // gate the evaluator (NULL = always)
} CustomChecklistDesc;
```

The persistence callbacks are **optional**. Leave both NULL (the common case) and the
framework persists the tab in its own save (below) — a tab then needs only checks, theme
and art. Provide both only when the mod must own where a completion is stored (the AP tab
mirrors it to a wire field its client reads). A half-provided pair falls back to framework
persistence.

`on_complete` is an **optional** notification, orthogonal to persistence: the framework
calls it once, the first frame a check completes, whichever side persists. It is the seam
for a framework-persisted tab to raise a mod-specific cue (a textbox, a sound, an outbound
event) without taking over storage — the thing a NULL `record_complete` otherwise gives
up. A mod owning persistence may instead notify inside `record_complete` (the AP tab does);
then `on_complete` is left NULL.

A `CustomCheck` is `{ int clear_kind; const char *label; int (*is_complete)(int clear_kind); }`.
`clear_kind` is the grid cell index (`0..CC_CLEAR_KIND_NUM-1`, the 12×10 = 120-cell board);
a tab may define any subset, the rest render blank. `is_complete` is handed its own row's
`clear_kind`, so a tab whose cells all resolve through one lookup points every row at the
same function instead of generating a predicate per cell — which is what the AP tab does.

The framework caps tabs at `CC_MAX_CHECKLISTS` (16) and copies the descriptor; the pointers
it holds (checks, labels, symbol names) must stay valid for the program lifetime (pass
static data).

## How a Tab Is Built (`CC_MinorLoad`)

`Register` clones the City Trial checklist minor-scene descriptor (`MNRKIND_CITYCHECKLIST`),
overrides its `cb_Load`, and installs it via `Hoshi_InstallMinorScene`; the returned id is
reachable through the tab cycle. The shared `cb_Load` resolves which tab it is from
`Scene_GetCurrentMinor()`, then:

- Runs `Checklist_PrepMenuData` and `Checklist_Init(GMMODE_CITYTRIAL, fresh)` — a **valid**
  mode, so no `mode>=3` assert and no archetype-slot collision — while a `g_build_active`
  flag redirects `gmGetClearcheckerTypeP(CITYTRIAL)` to the tab's `GameClearData`. The build
  borrows City Trial's visual template but takes its columns, completion counter and cell
  layout from the tab's data. `fresh` is driven from the tab's own pending-unlock state (a
  check `is_new && !is_unlocked`), so the tab enters the new-unlock presentation exactly
  when one of its checks is freshly completed.
- Flips the UI mode (`ClearCheckerUI.mode`) to the tab's synthetic mode, so the per-frame
  think/update path also reads the tab's block.
- Repoints SIS slot 0 and loads the tab art (below).

The tab's `GameClearData` carries a **full `grid_mapping` permutation** over all 120 cells,
and every cell starts hidden — the board reveals outward from completions (below).
`Checklist_Update` reverse-scans `grid_mapping` to map a cursor position back to a
clear_kind; an unmapped position trips the "Clearchecker Number 120" assert, so the full
bijection is required even though most cells are invisible. `Register` seeds it as identity;
the saved shuffle (below) replaces it on the first evaluated frame after the save is
available.

## Cell Labels (SIS text)

The checklist shows the selected cell's objective text via `stc_sis_data[0][clear_kind + 4]`.
`Checklist_Init` loads City Trial's `SisClrChkCT` into slot 0; after the build the framework
repoints slot 0 at its own pointer array (CT's header entries 0..3 kept, the rest blank, each
check's label composed in and slotted at `clear_kind + 4`). Only one custom tab is on screen
at a time, so a single shared buffer set is recomposed per build. The CT tab reloads slot 0
from the archive on its own `cb_Load`, so its labels stay intact.

A composed entry holds only glyphs, `TEXTCMD_SPACE` word separators, an optional
`TEXTCMD_LINEBREAK` at the wrap point, and `TEXTCMD_TERMINATE` — the exact shape of the
vanilla objective entries in `SisClrChk2D`/`3D`/`CT`, which carry no align, fit, kerning,
color or scale opcodes and no trailing break. The checklist UI's `Text` object supplies all
of that, so a label that pushes its own renders unlike the three vanilla tabs; a
`TEXTCMD_SCALE` in particular shrinks the cell text against its neighbours. Of the 160-byte
entry the composer accepts glyphs only while under byte 157, at 2 bytes per character and 1
per space or break, and a longer label is truncated silently. Every vanilla entry fits in
128; the extra room is for custom labels that restate a longer Archipelago location name. Any character
`Text_CharToCommand` does not map is dropped, also silently — it covers `0-9`, `A-Z`, `a-z`,
and the common punctuation including `!` and `:`.

**Wrapping is authored, not automatic.** The cell's box holds exactly two lines, and the
engine squeezes an over-wide line narrower instead of breaking it — which is why all 360
vanilla entries carry their break as an explicit `TEXTCMD_LINEBREAK` byte (349 are two
lines, 11 are one; none are three). Vanilla keeps single lines up to 37 characters, but
writes the overwhelming majority as two lines of ~25, which is the width its glyphs render
at full size.

`CC_ComposeSis` takes the break from the label when it holds a `\n`, and otherwise splits a
label over `CC_SIS_WRAP` (30) characters at the space nearest its midpoint. The automatic
split balances on width alone, so it will part a name from a trailing number —
`Stadium: SINGLE RACE / 8 Finish in 1st!` — where vanilla breaks after the whole
designation. A label whose break matters should therefore place its own; the automatic one
is the fallback for tabs that don't care. Either way a label must come out at two lines or
fewer, which the framework does not enforce: a second `\n` produces a third line the box
cannot show.

## Theme (target-color recolor)

Each checklist tab is tinted with a per-mode color carried in the **background scene's**
material **diffuse** values (`ScMenuCommon.clearchecker.bg_gobj` and the
`cross`/`prize1`/`prize2` marker GObjs). City Trial's diffuses are green-dominant and
**material-animated** — the menu's per-frame anim pass re-applies the green every frame — so
the recolor runs each frame from `OnFrameEnd` (after that pass), not once at load. It is a
no-op unless a custom tab is the current scene.

A descriptor supplies a target RGB (`theme_r/g/b`). For each green-dominant diffuse the
framework preserves the material's brightness range `[min, green]` and redistributes it onto
the theme hue: `out[c] = min + (green − min) · theme[c] / max(theme)`. The **green-dominant
gate** (`g > r && g >= b`) does double duty: it selects only the per-mode tint materials (not
the purple cell tiles or other UI), and it makes the pass idempotent (a non-green theme result
is no longer green-dominant, so it is never re-tinted within a frame). A zero theme leaves
City Trial's green.

## Tab Artwork (texture swap)

The per-mode banner and tab emblem carry their look in **textures over white materials**, not
recolorable diffuses, so the framework swaps the textures outright. A descriptor's `tex_file`
names a loadable HSD archive staged to the FST root that exports two `_HSD_ImageDesc` publics:

- the **banner** — RGB5A3 248×128 panel that backs the checkbox grid (the scrolling quad on
  `ScMenuCommon.clearchecker.frame_gobj`, found by its unique 248 width); and
- the **emblem** — the tab-indicator silhouette (a quad inside the background scene). The
  *vanilla* TObj to replace is identified by its unique **40×40 I4** signature; the
  replacement descriptor's own dimensions are unconstrained (the AP tab ships 64×64 I4),
  since after the swap the walk recognises it by pointer identity. It rides the recolor walk
  and takes the theme tint.

The archive is loaded **per tab build** into the **reclaimable per-scene heap**
(`Gm_LoadGameFile`, after `Checklist_Init` so the build can't reset the heap under the load),
so it costs zero permanent memory and is reloaded each tab load. The descriptors are NULL'd
before each reload and on failure; the swap walks skip on NULL (leaving the borrowed CT art).
The emblem's vanilla texture flipbook (`TObj.aobj` + `imagetbl`) is cleared so the anim pass
can't fight the swap, and `CC_RecolorScene` issues one `GXInvalidateTexAll` per frame so GX
re-fetches the swapped texels rather than a stale TMEM cache. With no `tex_file`, the tab
keeps City Trial's borrowed banner/emblem.

## Per-Frame Evaluation and Persistence

`OnFrameStart` iterates every registered tab's check table (gated by the descriptor's
`is_ready`):

- **Not yet recorded** (`is_recorded(ck)` is false) and the predicate now holds: record it
  (the mod's `record_complete`, or the framework's own save), fire the optional
  `on_complete(ck)` cue, then seed `clear[ck].is_new` and play the completion SFX. A check
  satisfied outside any gamemode never gets `is_new` from the engine, so the framework sets
  it; the flip-and-sparkle runs on the next tab entry.
- **Already recorded, with no `is_new` pending**: raise `is_unlocked` and reveal the cell's
  neighbours (below) — so a completion from a prior boot (the block is BSS-zeroed at boot)
  shows complete with no replay. A pending `is_new` is left alone, so a this-session
  completion still animates once.

The framework owns the **entire presentation** — the cell flags, the on-tab-entry
flip-and-sparkle animation, the post-run popup (below), and the mid-run completion cue
(`CC_PlayUnlockSfx`, the same `0x10008` SFX vanilla plays for a checkbox, suppressed when
the unlock cache is valid and sharing the engine's one-frame cooldown via
`*stc_clearchecker_sfx_last_frame`, so a tab whose `record_complete` also routes through
`ClearChecker_SetNewUnlock` never double-plays). Every tab — AP or not — gets identical
animations and sound.

### Cell visibility: revealed, never listed

A custom tab hides its cells exactly the way the vanilla board does: the grid builder draws a
cell only for the bits it finds (`is_filler` / `has_reward` / `is_unlocked` / `is_visible`, in
that priority; none of them means no cell at all), so a fresh tab is a blank board and grows
outward from its completions. The framework never marks a cell `is_visible` because it has an
objective; only a completed neighbour does that.

`Checklist_ProcessUnlock` supplies the reveal for cells it animates: it maps the unlocked cell
through `grid_mapping` to a physical slot and sets `is_visible` on the four orthogonal
neighbours of that slot (edge-gated on col/row), then rebuilds the grid. It is the only engine
path that grants the bit, so a cell that is **already complete when it arrives** — restored
from a prior boot into BSS-zeroed clear data, or back-filled by a consumer — would never
reveal anything around it. When the evaluator raises `is_unlocked` on such a cell it therefore
performs the same four-neighbour reveal itself, once per clear_kind, tracked in a per-tab
bitmask that resets whenever the layout changes (reveals are positional, so a reshuffle
invalidates them).

The reveal is purely positional and ignores whether the neighbour has a check, matching the
engine. On a tab defining fewer than 120 cells that means some revealed boxes can never be
filled — they read as objectives the player has yet to reach.

### Recorded state: framework-managed by default

A tab that leaves `is_recorded`/`record_complete` NULL delegates its recorded state to the
framework. `custom_checklist` carries its own hoshi save (`CCSave`): a per-tab
completed-`clear_kind` bitmask (2 × `u64`), in slots **keyed by an FNV-1a hash of the tab's
`name`** (not its registry index, so saved bits survive mods being added/removed or reordered
— the same stable-id approach `custom_items` uses). On completion the framework sets the bit;
on query it reads it back; the slot is resolved (and lazily claimed) on first access, after
the save loads. There are as many slots as tabs, so there is always room, and an unresolved
slot reports not-recorded so the check simply re-evaluates next frame. A typical tab is fully
persistent with **zero persistence code**.

The framework never calls `Hoshi_WriteSave` itself. That call mounts the card and rewrites the
whole `"hoshi"` file synchronously, stalling the frame, and checks complete mid-run — so
`CCSave` (recorded bits and `layout_seed`) is only mutated in RAM. hoshi flushes it at every
point the game saves its own file (it hooks the call sites of `Memcard_ReqSave`, `0x80078990`:
result screens, stage selects, checklist unlocks, main-menu entry), hash-gated so an unchanged
save costs nothing. A tab that owns its persistence should do the same — and writing at the
game's save points, rather than on completion, is also what keeps a tab's recorded bits from
drifting out of step with the `GameClearData.clear[]` flags that ride the vanilla save.

A mod provides both callbacks only when it must own the storage — the AP tab records through
its `sent_checks` / `check_detection` path (with its own "Check sent" textbox and goal
re-eval) because that field sits at a fixed wire offset its Python client reads, which the
framework's generic save can't provide. That is the one split that remains mod-specific;
everything else is identical for every tab.

### Grid layout: seeded shuffle

Vanilla scatters a tab's cells with `Checklist_InitGridMapping` and persists the resulting
`grid_mapping` inside `GameClearData`. A custom tab's clear storage is BSS, so `CCSave`
instead holds a single 4-byte `layout_seed` (minted from `OSGetTime` and avalanched, once per
save file) and each tab regenerates its own permutation from it: `seed ^ name_hash` drives a
private xorshift32 Fisher-Yates over `0..119`. Per-tab streams mean tabs don't share a layout
and adding or removing one doesn't reshuffle the others; the private PRNG — rather than
`HSD_Randi`, whose one global state every other caller advances by an unknowable amount — is
what makes a layout reproducible from the seed alone. Four bytes of seed are persisted instead
of 120 bytes of layout.

The shuffle is applied **once per tab per session**, lazily: a consumer registers from its own
`OnSaveLoaded`, which can run before the framework's (mods boot in alphabetical order), so
until the seed is readable the tab keeps the identity mapping — itself a valid bijection that
renders fine. It runs ahead of the descriptor's `is_ready` gate, since deferring it would show
the identity layout and then visibly reshuffle the moment the tab became ready. It writes
`grid_mapping` only — the `clear[]` completion flags are live by then — except that it drops
every `is_visible` bit and the reveal bookkeeping, which are positional and so stale the moment
cells move. There is no meta-cell pre-placement (vanilla reserves positions for its "fill in
100" cell before shuffling, but no custom tab has such a cell — the AP mod's
`Fill100ClearKind` returns `0xFF` for the AP row), so a tab that ever gains one needs that
handling added.

## Tab Cycle and Post-Run Presentation (`CC_MinorThink`)

The tab ring is `AR → TR → CT → tab0 → tab1 → … → AR`, and a tab switch plays the vanilla
`0x1000A` cue. `Checklist_MinorThink` phases:

- **12/13 (`NEXTTAB`/`PREVTAB`):** step the ring with wrap.
- **14 (`ENDING`):** raised when A is pressed on a cell whose reward has `REWARDPARAM_ENDING`;
  real tabs route to their mode's ending minor (`MNRKIND_AIRRIDEENDING`/`TOPRIDEENDING`/
  `CITYENDING` = 28/29/30) as vanilla. A custom tab reports no rewards, so it can never raise
  this phase.
- **11 (`EXIT`):** post-run only, if a custom tab still has an unviewed unlock, detour to it so
  it animates before leaving (it raises `is_unlocked` once shown, so the next exit press falls
  through). Lets the played mode animate on its own tab first.

A round routes into the played mode's checklist tab only when its `*_MinorExit` finds
`ClearChecker_CheckForNewUnlocks(mode) != 0` — a cache-stale scan of *that mode's* cells. A
custom check lives in the custom tab's block, so on its own it never trips that gate. Two
REPLACEFUNCs close the loop: `ClearChecker_CheckForNewUnlocks` OR-s in "any custom tab
pending", and `Scene_SetNextMinor` (the chokepoint where each `*_MinorExit` requests the played
mode's tab) retargets straight to a pending custom tab when the played mode has nothing of its
own to animate. The post-run session is flagged (`g_postrun`) so the exit chokepoint can chain
through any remaining pending custom tabs, and is cleared on exit and on leaving for an ending
movie — confining the chain to runs.

## Files

- `mods/custom_checklist/include/custom_checklist_api.h` — the public API: the
  `CustomChecklistDesc` / `CustomCheck` authoring contract and the `CustomChecklistAPI`
  (`Register`).
- `mods/custom_checklist/src/custom_checklist.c` — the registry, `CCSave`, the minor-scene
  install + shared `cb_Load`, the six REPLACEFUNCs, the per-frame evaluator, the grid shuffle
  and neighbour reveal, the SIS slot-0 override, the target-color recolor, and the
  banner/emblem texture swap.
- `Makefile` — `mods/custom_checklist/include` added to `INCLUDES` (public header consumed by
  the archipelago mod).
