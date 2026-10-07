# Custom Checklist (framework for extra checklist tabs)

`mods/custom_checklist/` adds mod-owned checklist tabs alongside the three vanilla ones
(Air Ride / Top Ride / City Trial), folded into the existing L/R tab rotation. Each
registered tab is a synthetic checklist mode (index `>= GMMODE_NUM`) backed by its own
`GameClearData`, served through the engine's clear-checker accessor, so the engine renders
its grid, completion counter, fillers and unlock animation as if it were a fourth game
mode. With nothing registered every patch reproduces vanilla, so a build with no consumer
is inert.

The framework owns the **presentation, the per-frame evaluation and the board's save**; a
registering mod owns the **objectives** (a static check table) and **where a completion
is recorded**.

## Architecture

An extra tab is cheap because the in-game checklist is **one shared, mode-parameterized
screen**, and it already cycles through the three modes with L/R. Adding a tab means adding
an entry to a rotation that already exists; almost everything renders for free once the
synthetic mode is plumbed through.

- The checklist is **minor-scene kinds `0x20`/`0x21`/`0x22`** (`MNRKIND_AIRRIDECHECKLIST`
  = 32 / `TOPRIDECHECKLIST` / `CITYCHECKLIST`), all sharing one set of load/think/leave
  callbacks. The shared `cb_Load`, `Checklist_MinorLoad` (`0x8004a768`), derives
  `mode = Scene_GetCurrentMinor() - 0x20` and calls `Checklist_Init(mode, fresh)`
  (`0x801822f4`), which writes `mode` into `ClearCheckerUI.mode` (`+0x14`).
- `gmGetClearcheckerTypeP(mode)` (`0x800076a0`) is a 3-way `switch` returning the per-mode
  `GameClearData` embedded in `GameData`. For `mode >= 3` it asserts and returns NULL -
  **this is the master lever**.
- Input in `Checklist_Think` (`0x8017f3bc`): **R|X -> next tab, L|Y -> prev tab, B -> exit**
  (`ClearCheckerPhase` 12 / 13 / 11), consumed by `Checklist_MinorThink` (`0x8004a648`).

## Engine Levers (mode-keyed surfaces)

The framework installs these at boot (`CCScene_InstallHooks`). They reproduce vanilla for the
real modes and handle the synthetic ones.

| Patch | Site | Custom-mode behavior |
|---|---|---|
| `REPLACEFUNC` `gmGetClearcheckerTypeP` | `0x800076a0` | serve the tab's `GameClearData`; NULL for unknown modes (no assert) |
| `REPLACEFUNC` `Checklist_GetRewardNum` | `0x80049c20` | `0` - gates every reward loop in the render path off and dodges the `mode >= 3` assert. Also `0` for `CITYTRIAL` while a tab build is active, since the build runs under that mode and `Checklist_SetRewardFlagOnUnlocks` would otherwise walk City Trial's reward table onto the tab's cells. Real modes read `stc_reward_num` (`0x805d51d0`, AR 46 / TR 33 / CT 44) |
| `REPLACEFUNC` `Checklist_GetClearKindFromRewardIndex` | `0x80049c84` | `0` - keeps `Checklist_ProcessUnlock`'s first new-unlock scan inert so the cell flip-animation can run, and dodges the assert |
| `REPLACEFUNC` `Checklist_MinorThink` | `0x8004a648` | the tab cycle with custom tabs folded into the ring |
| `REPLACEFUNC` `ClearChecker_CheckForNewUnlocks` | `0x8004a1a4` | vanilla result OR any custom tab pending, so a run that completed only a custom check still routes into the checklist. Both answer 0 during a LAN session, as vanilla does |
| `REPLACEFUNC` `Scene_SetNextMinor` | `0x800088c8` | post-run retarget to a custom tab when the played mode has nothing to animate |
| `REPLACECALL` `ClearChecker_GetRewardFromClearKind` | `0x801804dc` in `Checklist_Think` | `*out_reward_index = 0xFF`; real modes call through |
| `REPLACECALL` `JObj_AddSetAnim` x4 | `0x80181fb8`, `0x80181fe4`, `0x80182070`, `0x80182098` in `Checklist_UpdateCellInfo` | play City Trial's Prize1 animation |

Two mode-keyed surfaces sit outside `Checklist_GetRewardNum`'s reach:

- `ClearChecker_GetRewardFromClearKind` (`0x80049ec4`), the audio/ending preview lookup
  reached when A is pressed on an `is_unlocked` or `is_filler` cell, bounds the mode itself
  and indexes `stc_reward_num[mode]` / `stc_reward_table_ptrs[mode]` directly, so a
  completed custom cell would assert inside it. It is covered at its **only call site**,
  leaving the entry free for a consumer that keeps its own reward table - two mods must
  never patch one function entry. The custom-mode answer short-circuits before the call, so
  a cell on a custom tab never previews a reward.
- `Checklist_UpdateCellInfo` (`0x80181d70`) picks the hovered cell's Prize1 animation as
  `ClearCheckerUI.mode * 2`, `+1` while a reward shows. The `ScMenClearcheckerPrize1` set
  holds only the three real modes' pairs and `_JObj_AddSetAnim` (`0x80055a30`) does not bound
  the index, so mode 3 and up would read the arrays that follow as animations. The four call
  sites map an index past the real pairs onto City Trial's pair, keeping the reward bit.

## Registration

A consumer imports the API via `Hoshi_ImportMod(CUSTOM_CHECKLIST_MOD_NAME, ...)` and calls
`Register(desc)` from its **`OnSaveLoaded`**: mods boot in alphabetical order, so a
consumer's `OnBoot` can run before this mod exports the API. `Register` returns the
assigned mode (`GMMODE_NUM` for the first tab, then `+1` each) or `-1` on failure. Tabs are
capped at `CC_TAB_MAX` (4), which also sizes the save.

`CustomChecklistDesc` is the authoring contract: a name, a theme RGB, an optional
`tex_file`/`banner_symbol`/`emblem_symbol` art triple, the static check table, the
required `is_recorded`/`record_complete` pair and an optional `is_ready` gate. `Register`
copies the struct but **keeps every pointer inside it** - checks, labels and symbol names
must be static. It rejects a descriptor with no name, no checks, a missing callback, a
check with no label or predicate, or a `clear_kind` out of range or used twice, so the
per-frame paths never re-check any of that.

The `name` is the tab's stable identity: its `hash_32_str` hash keys the tab's save slot (below).
Renaming a tab abandons its saved board.

A `CustomCheck` is `{ clear_kind, label, is_complete }`. `clear_kind` is the grid cell index
(`0..CLEAR_KIND_NUM-1`, the 12x10 = 120-cell board); a tab may define any subset, the rest
render blank. `is_complete` is handed its own row's `clear_kind`, so a tab whose cells all
resolve through one lookup points every row at the same function.

Completion belongs to the consumer: `record_complete` runs on a cell's first completion and
`is_recorded` must hold for it from then on, across boots. The framework never stores
completion itself, so a consumer whose completion has to live somewhere specific - a wire
field another program reads, a per-seed save - keeps one source of truth.

`is_ready` gates the evaluator, so a tab whose predicates depend on late-arriving state can
hold it off.

`RevealAll(mode)` opens every cell of a tab that has a check behind it (below).
`GetBuildMode()` reports which tab a build is running for: during the build
`ClearCheckerUI.mode` still reads `CITYTRIAL` although `gmGetClearcheckerTypeP` already
serves the tab's block, so a consumer hook keyed off the UI mode has to remap through it or
it applies City Trial's reward rows to the custom tab's board.

## How a Tab Is Built (`CC_MinorLoad`)

`Register` clones the City Trial checklist minor-scene descriptor (`MNRKIND_CITYCHECKLIST`),
overrides its `cb_Load`, and installs it via `Hoshi_InstallMinorScene`, which appends past
`MNRKIND_NUM` and cannot fail. The replacement `cb_Load` resolves which tab it is from
`Scene_GetCurrentMinor()`, then:

- Runs `Checklist_PrepMenuData` and `Checklist_Init(GMMODE_CITYTRIAL, fresh)` - a **valid**
  mode, so no `mode >= 3` assert and no archetype-slot collision - while a build flag
  redirects `gmGetClearcheckerTypeP(CITYTRIAL)` to the tab's `GameClearData`. The build
  borrows City Trial's visual template but takes its cells, completion counter and fillers
  from the tab's data. `fresh` is the tab's own pending-unlock state (a check
  `is_new && !is_unlocked`), so the tab opens on the new-unlock presentation whenever it
  holds an unviewed unlock, however it is reached.
- Repoints SIS slot 0 and loads the tab art (below).
- Flips `ClearCheckerUI.mode` to the tab's synthetic mode, so the per-frame think/update
  path reads the tab's block.

The tab's `GameClearData` carries a **full `grid_mapping` permutation** over all 120 cells.
`Checklist_Update` reverse-scans `grid_mapping` to map a cursor position back to a
clear_kind; an unmapped position trips the "Clearchecker Number 120" assert, so the full
bijection is required even though most cells are invisible.

## Cell Labels (SIS text)

The checklist shows the selected cell's objective text via
`stc_sis_data[0][CLEARCHECKER_SIS_OBJECTIVE_BASE + clear_kind]` (base 4). `Checklist_Init`
loads City Trial's `SisClrChkCT` into slot 0; after the build the framework repoints slot 0
at its own pointer array. Every objective entry points at one shared bare-terminator entry
except the tab's own labels, composed into a buffer per `clear_kind`; every other entry
passes through to the loaded archive. Only one custom tab is on screen at a time, so one
buffer set is recomposed per build. The City Trial tab reloads slot 0 from the archive on
its own `cb_Load`, so its labels stay intact.

The array spans City Trial's whole entry range, not just the 124 entries before the reward
text: the reward panel reads slot 0 at `CLEARCHECKER_SIS_NO_REWARD` (`0x7C`, the "no reward"
line every custom cell shows) and at `CLEARCHECKER_SIS_REWARD_BASE + reward_index` (`0x7D`),
up to City Trial's 44 rewards - `SisClrChkCT` holds exactly `0x7D + 44` entries. A shorter
array would be indexed past its end and hand `Text_GX` a garbage stream pointer.

A composed entry holds only glyphs, `TEXTCMD_SPACE` word separators, an optional
`TEXTCMD_LINEBREAK` at the wrap point, and `TEXTCMD_TERMINATE` - the exact shape of the
vanilla objective entries in `SisClrChk2D`/`3D`/`CT`, which carry no align, fit, kerning,
color or scale opcodes and no trailing break. The checklist UI's `Text` object supplies all
of that, so a label that pushes its own renders unlike the three vanilla tabs; a
`TEXTCMD_SCALE` in particular shrinks the cell text against its neighbours. Of the 160-byte
entry the composer accepts glyphs only while under byte 157, at 2 bytes per character and 1
per space or break, and a longer label is truncated silently. Every vanilla entry fits in
128; the extra room is for longer custom labels. Any character `Text_CharToCommand` does not
map is dropped, also silently - it covers `0-9`, `A-Z`, `a-z`, and the common punctuation
including `!` and `:`.

**Wrapping is authored, not automatic.** The cell's box holds exactly two lines, and the
engine squeezes an over-wide line narrower instead of breaking it - which is why all 360
vanilla entries carry their break as an explicit `TEXTCMD_LINEBREAK` byte (349 are two
lines, 11 are one; none are three). Vanilla keeps single lines up to 37 characters, but
writes the overwhelming majority as two lines of ~25, which is the width its glyphs render
at full size.

`CC_ComposeSis` takes the break from the label when it holds a `\n`, and otherwise splits a
label over `CC_SIS_WRAP` (30) characters at the space nearest its midpoint. The automatic
split balances on width alone, so it will part a name from a trailing number -
`Stadium: SINGLE RACE / 8 Finish in 1st!` - where vanilla breaks after the whole
designation. A label whose break matters should therefore place its own; the automatic one
is the fallback for tabs that don't care. Either way a label must come out at two lines or
fewer, which the framework does not enforce: a second `\n` produces a third line the box
cannot show.

## Theme (target-color recolor)

Each checklist tab is tinted with a per-mode color carried in the **background scene's**
material **diffuse** values (`ScMenuCommon.clearchecker.bg_gobj` and the
`cross`/`prize1`/`prize2` marker GObjs). City Trial's diffuses are green-dominant and
**material-animated** - the menu's per-frame anim pass re-applies the green every frame - so
the recolor runs each frame after that pass, not once at load. It is the custom tab minor's
`cb_ThinkPostGObjProc2` - empty in the City Trial descriptor the tab clones - which
`updateFunction` calls right after `GObj_UpdateAll`, so it never runs outside a custom tab.
Both the recolor and the texture swaps walk
each GObj's root JOBJ and its child subtree, skipping the root's siblings, which belong to
other scenes.

A descriptor supplies a target color (`theme`, a `GXColor` whose alpha is unused). For each green-dominant diffuse the
framework preserves the material's brightness range `[min, green]` and redistributes it onto
the theme hue: `out[c] = min + (green - min) * theme[c] / max(theme)`. The **green-dominant
gate** (`g > r && g >= b`) selects only the per-mode tint materials (not the purple cell
tiles or other UI), and - because the theme itself must not be green-dominant - also skips a
material already retinted. A zero theme leaves City Trial's green.

## Tab Artwork (texture swap)

A descriptor's `tex_file` names a loadable HSD archive staged to the FST root that exports
two `_HSD_ImageDesc` publics:

- the **banner** - the scrolling 248x128 panel behind the checkbox grid on
  `ScMenuCommon.clearchecker.frame_gobj`, found by its unique 248 width. It carries its look
  in a texture over a white material, so the framework swaps the texture and holds the
  diffuse white so the replacement samples neutrally.
- the **emblem** - the tab-indicator silhouette, a quad inside the background scene. The
  *vanilla* TObj to replace is identified by its unique **40-wide I4** signature; after the
  swap the walk recognizes the replacement by pointer identity, so its own size and format
  are unconstrained. The emblem is an intensity mask modulated by the per-mode diffuse, so it
  rides the recolor walk and takes the theme tint.

The archive is loaded **per tab build** into the **reclaimable per-scene heap**
(`Gm_LoadGameFile`, after `Checklist_Init` so the build can't reset the heap under the load),
so it costs no permanent memory. `Gm_LoadGameFile` asserts on a file that is not on disc
(`File_GetSize`, "file isn't exist"), so the framework first checks
`DVDConvertPathToEntrynum` and keeps City Trial's art when the file is missing. The
descriptors are NULL'd before each load and on failure; the swaps skip on NULL. The emblem's
vanilla texture flipbook (`TObj.aobj` + `imagetbl`) is cleared so the anim pass can't fight
the swap. No TMEM flush of its own is needed: `updateFunction` calls `GXInvalidateTexAll`
(`0x80006a98`) every frame after the ticks and before render, so the swapped texels are
re-fetched.

## Per-Frame Evaluation

`OnFrameStart` walks every registered tab's check table, gated by the descriptor's
`is_ready`:

- **Not recorded, not on the board** (neither `is_new` nor `is_unlocked`), and the predicate
  now holds: call `record_complete`, then - outside a LAN session, where vanilla suppresses
  unlocks too - seed `clear[ck].is_new` and play the completion SFX. Only
  `ClearChecker_SetNewUnlock` sets `is_new` in the engine, and a tab need not record through
  it; the flip-and-sparkle runs on the next tab entry. The board test makes this fire once
  per cell, even if `record_complete` fails to make `is_recorded` true.
- **Recorded but not on the board**: raise `is_unlocked` and reveal the cell's neighbours
  (below). That covers a blank board meeting completions the consumer already holds - a
  freshly claimed save slot, or a tab left on its RAM block every boot - and shows them complete with no
  replay. A pending `is_new` is left for `Checklist_ProcessUnlock` to animate.

Predicates are therefore polled every frame in every scene, menus, loads and the title
attract demo included. They must be cheap pure reads of state latched elsewhere, and gate
themselves if they must not fire during the demo.

The completion SFX (`CLEARCHECKER_UNLOCK_SFX`, `0x10008`, what vanilla plays for a checkbox)
shares the engine's one-frame cooldown via `*stc_clearchecker_sfx_last_frame`, so a tab
whose `record_complete` routes through `ClearChecker_SetNewUnlock` never double-plays.

### Cell visibility: revealed, never listed

A custom tab hides its cells exactly the way the vanilla board does: the grid builder draws a
cell only for the bits it finds (`is_filler` / `has_reward` / `is_unlocked` / `is_visible`, in
that priority; none of them means no cell at all), so a fresh tab is a blank board and grows
outward from its completions. The framework never marks a cell `is_visible` because it has an
objective; only a completed neighbour does that.

`Checklist_ProcessUnlock` (`0x8017e490`) supplies the reveal for cells it animates: it maps
the unlocked cell through `grid_mapping` to a physical slot and sets `is_visible` on the four
orthogonal neighbours of that slot (edge-gated on col/row), then rebuilds the grid. It is the
only engine path that grants the bit, so a cell that **reaches the board already complete**
would never reveal anything around it. When the evaluator raises `is_unlocked` on such a cell
it therefore performs the same four-neighbour reveal itself.

The reveal is purely positional and ignores whether the neighbour has a check, matching the
engine. On a tab defining fewer than 120 cells that means some revealed boxes can never be
filled - they read as objectives the player has yet to reach.

`RevealAll(mode)` opens a whole tab at once, for a consumer whose own option asks for it. It
marks `is_visible` on every cell the descriptor defines a check for and leaves the rest hidden
(a revealed empty box is an objective that can never be completed), touching no unlock state.
It is saved with the board like any other flag.

## The Saved Board

A vanilla tab's `GameClearData` lives in `GameData`, so the game's own save carries every
cell flag, the `grid_mapping` layout and the checkbox-filler counters. A custom tab gets the
same from the framework's hoshi save block, `CCSave`: a `u32` stamp followed by
`CC_TAB_MAX` slots, each a tab-name hash and a whole `GameClearData` (4 + 4 x 248 = 996
bytes). Once bound, the tab's accessor serves its slot **directly**, so every engine write -
a filler placed, a cell animated, a neighbour revealed - lands in the save the moment it
happens. That matters because hoshi writes the card from the game's own save points, and some
of those follow an engine write in the same frame: `Checklist_Think`'s filler placement (phase
9) sets `is_filler`, decrements `checkbox_filler_num` and calls `Memcard_ReqSave` in one pass.
A copy of the board taken once per frame would miss it.

Slots are keyed by the `hash_32_str` (CRC32) hash of the tab's `name`, not its registry
index, so a board survives tabs being added, removed or reordered. A hash of 0 is bumped to 1,
since 0 marks an empty slot. Slots are never released: a renamed or removed tab keeps its slot
on that save, and once all four are held a new name finds none and runs on its RAM block,
reporting it. A slot claimed fresh starts blank with a `grid_mapping` Fisher-Yates shuffled by
`HSD_Randi`, the game RNG that `main` seeds from `OSGetTick`, as vanilla's own layout is.
There is no meta-cell pre-placement (vanilla's `Checklist_InitGridMapping` reserves positions
for its "fill in 100" cell before shuffling, but no custom tab has such a cell).

**Binding.** Each tab starts on a RAM block, blank and shuffled, at `Register`, and moves onto
its slot at the framework's `OnSaveLoaded` - or at `Register` itself for a consumer that
registers after that. It cannot bind earlier: a `Register` from a consumer's `OnBoot` would
see `save_ptr` still in hoshi's default layout, which the card load replaces, and the block's
stamp is only checked at this mod's own `OnSaveLoaded`. A consumer registers from its own
`OnSaveLoaded`, which runs first, and may write to the tab there - reward flags, `RevealAll`. Those writes are per-cell flag sets and filler grants, never
positional, so the bind merges the RAM block into the slot: clear bytes are OR-ed and filler
counts added (the list length capped at 5, as `Checklist_GrantFiller` caps it).

`CCSave` opens with its own version word (`stamp`, `CC_SAVE_STAMP`). `OnSaveInit` writes it
into hoshi's default block at boot, and `OnSaveLoaded` reinitializes the block when the
card's copy carries any other value - which is also what a freshly allocated block shows. hoshi
matches a card block to a mod by the hash of the mod's name plus the size it asks for and never
consults `ModDesc.version`, so a layout change of the same size would otherwise be read as
data; bump the stamp whenever `CCSave` changes.

The framework never calls `Hoshi_WriteSave`. That call mounts the card and rewrites the whole
`"hoshi"` file synchronously, stalling the frame, and checks complete mid-run. hoshi instead
flushes the block at every point the game saves its own file (it hooks the call sites of
`Memcard_ReqSave`, `0x80078990`: result screens, stage selects, checklist unlocks, main-menu
entry), hash-gated so an unchanged save costs nothing - which also keeps the custom boards in
step with the vanilla ones.

## Tab Cycle and Post-Run Presentation (`CC_MinorThink`)

The tab ring is `AR -> TR -> CT -> tab0 -> tab1 -> ... -> AR`, and a tab switch plays the
vanilla `CLEARCHECKER_TAB_SFX` (`0x1000A`) cue. `Checklist_MinorThink` phases:

- **12/13 (`NEXTTAB`/`PREVTAB`):** step the ring with wrap.
- **14 (`ENDING`):** raised when A is pressed on a cell whose reward has `REWARDPARAM_ENDING`;
  real tabs route to their mode's ending minor (`MNRKIND_AIRRIDEENDING`/`TOPRIDEENDING`/
  `CITYENDING` = 28/29/30) as vanilla. The `ClearChecker_GetRewardFromClearKind` call site
  finds no reward on a custom tab's cells, so a custom tab never raises this phase.
- **11 (`EXIT`):** post-run only, if a custom tab still has an unviewed unlock, detour to it so
  it animates before leaving (it raises `is_unlocked` once shown, so the next exit press falls
  through). Lets the played mode animate on its own tab first.

A round routes into the played mode's checklist tab only when its `*_MinorExit` finds
`ClearChecker_CheckForNewUnlocks(mode) != 0` - a scan of *that mode's* cells for
`is_new && !is_unlocked`. A custom check lives in the custom tab's block, so on its own it
never trips that gate. Two REPLACEFUNCs close the loop: `ClearChecker_CheckForNewUnlocks`
OR-s in "any custom tab pending", and `Scene_SetNextMinor` (the chokepoint where each
`*_MinorExit` requests the played mode's tab) retargets straight to a pending custom tab when
the played mode has nothing of its own to animate. The post-run session is flagged so the
exit can chain through any remaining pending custom tabs, and the flag is cleared on exit and
on leaving for an ending movie - confining the chain to runs.

The retarget is restricted to the transition *into* the checklist: `CODEPATCH_REPLACEFUNC`
branches from the patched function's entry, so `Checklist_MinorThink`'s own
`Scene_SetNextMinor` calls re-enter the replacement too. A post-run checklist session runs
under the played mode's major, not `MJRKIND_MENU`, so without the extra guard an L/R step
onto a vanilla tab would satisfy the retarget condition and be bounced straight back to the
pending custom tab - the player could never reach the Air Ride, Top Ride or City Trial tabs
while a custom check was still unviewed. The guard is therefore "the current minor is not
itself a checklist tab", which only the engine's `*_MinorExit` callers satisfy.

## Files

- `mods/custom_checklist/include/custom_checklist_api.h` - the public API: the
  `CustomChecklistDesc` / `CustomCheck` authoring contract and the `CustomChecklistAPI`
  (`Register`, `RevealAll`, `GetBuildMode`).
- `mods/custom_checklist/src/custom_checklist.h` - the shared registry (`CCTab`), `CCSave`,
  and the cross-file entry points.
- `mods/custom_checklist/src/main.c` - `ModDesc`, lifecycle callbacks, `Register` and the
  exported API.
- `mods/custom_checklist/src/tab_scene.c` - the engine patches, the replacement `cb_Load`,
  the tab ring and post-run routing.
- `mods/custom_checklist/src/tab_board.c` - the saved board: slot binding and merge, layout
  shuffle, neighbour reveal and the per-frame evaluator.
- `mods/custom_checklist/src/tab_labels.c` - the SIS slot-0 override and label composition.
- `mods/custom_checklist/src/tab_art.c` - tab art loading, the target-color recolor and the
  banner/emblem texture swap.
