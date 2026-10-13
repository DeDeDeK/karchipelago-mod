# Custom Checklist

`custom_checklist` lets a mod add its own checklist tabs after the three vanilla ones, in the
same L/R tab rotation. Each registered tab is a synthetic checklist mode (`>= GMMODE_NUM`) with
its own `GameClearData`, served through the engine's clear-data accessor, so the engine draws its
grid, completion counter, checkbox fillers and unlock animation exactly as for a real mode. The
framework owns the presentation, the per-frame evaluation and the saved board; the registering
mod owns the objectives and where a completion is recorded. With no tab registered, every patch
behaves as vanilla.

The checklist screen is one mode-parameterized screen, which is why a tab is cheap: the three
vanilla tabs are minor scenes `MNRKIND_AIRRIDECHECKLIST` to `CITYCHECKLIST` sharing one set of
callbacks, and nearly everything mode-specific goes through `gmGetClearcheckerTypeP(mode)`.

## Registering a tab

A consumer imports `CustomChecklistAPI` with `Hoshi_ImportMod(CUSTOM_CHECKLIST_MOD_NAME, ...)`
and calls `Register(desc)` from its `OnSaveLoaded`. Mods boot in alphabetical order, so a
consumer's `OnBoot` may run before this mod exports the API. `Register` returns the tab's mode -
`GMMODE_NUM` for the first tab, one more for each after - or -1 when it rejects the descriptor
or `CC_TAB_MAX` tabs already exist. The consumer passes that mode to any engine call for its tab
(`ClearChecker_SetNewUnlock`, `Checklist_GrantFiller`).

`CustomChecklistDesc` is the authoring contract. `Register` copies it but keeps every pointer
inside it, so labels, checks and symbol names must be static.

| Field | Meaning |
|---|---|
| `name` | The tab's stable identity; its hash keys the saved board, so renaming a tab abandons it |
| `theme` | Tab tint (`GXColor`, alpha unused); black keeps City Trial's green. Must not be green-dominant |
| `tex_file`, `banner_symbol`, `emblem_symbol` | Optional art archive on the disc root and its two image publics |
| `checks`, `check_num` | The `CustomCheck` table |
| `is_recorded(ck)` | True once a cell's completion is recorded, across boots |
| `record_complete(ck)` | Called once, on a cell's first completion |
| `is_ready()` | Optional gate; evaluation waits until it returns nonzero |

A `CustomCheck` is `{ clear_kind, label, is_complete }`. `clear_kind` is the cell, 0-119; a tab
may define any subset of the 120 and the rest stay blank. `is_complete(ck)` receives its own
cell, so one predicate can back every row. `Register` rejects a descriptor with no name, no
checks, a missing callback, a check with no label or predicate, or a `clear_kind` out of range
or used twice, so nothing downstream checks those again.

Completion belongs to the consumer. The framework never stores it, so a consumer whose record has
to live somewhere specific - a field another program reads, a per-seed save - keeps a single
source of truth.

The API also has `RevealAll(mode)`, which shows every cell of a tab that has a check behind it,
and `GetBuildMode()`, the mode of the tab being built (below), or -1.

## Engine patches

`OnBoot` installs these unconditionally; for the three real modes each reproduces vanilla.

| Patch | Site | For a custom tab |
|---|---|---|
| `REPLACEFUNC gmGetClearcheckerTypeP` | 0x800076a0 | Serves the tab's block; NULL, not an assert, for an unknown mode |
| `REPLACEFUNC Checklist_GetRewardNum` | 0x80049c20 | 0, which keeps every reward loop off the tab; also 0 for City Trial during a tab build |
| `REPLACEFUNC Checklist_GetClearKindFromRewardIndex` | 0x80049c84 | 0, keeping `Checklist_ProcessUnlock`'s reward scan inert |
| `REPLACEFUNC Checklist_MinorThink` | 0x8004a648 | The tab ring and post-run routing |
| `REPLACEFUNC ClearChecker_CheckForNewUnlocks` | 0x8004a1a4 | Also true when any custom tab has an unviewed unlock; 0 during a LAN session |
| `REPLACEFUNC Scene_SetNextMinor` | 0x800088c8 | Retargets a round's exit to a pending custom tab |
| `REPLACECALL ClearChecker_GetRewardFromClearKind` | 0x801804dc in `Checklist_Think` | No reward (`0xFF`); real modes call through |
| `REPLACECALL JObj_AddSetAnim` | 0x80181fb8, 0x80181fe4, 0x80182070, 0x80182098 in `Checklist_UpdateCellInfo` | City Trial's Prize1 animation |

Two mode-keyed surfaces sit outside the reward-count gate. `ClearChecker_GetRewardFromClearKind`
(0x80049ec4), the audio and ending preview lookup run when A is pressed on a completed cell,
bounds the mode itself and would assert on a custom cell, so the framework patches its only call
site and leaves the function entry free for a mod that keeps its own reward table - two mods must
never patch one entry. And `Checklist_UpdateCellInfo` picks the Prize1 animation as
`ClearCheckerUI.mode * 2` (+1 with a reward shown) from a set holding only the three real modes'
pairs; the four call sites map anything past them onto City Trial's pair.

## Building a tab

`Register` clones City Trial's checklist minor-scene descriptor with its own `cb_Load` and the
art pass in the empty `cb_ThinkPostGObjProc2`, and installs it with `Hoshi_InstallMinorScene`.
That `cb_Load` finds its tab by the current minor, then:

1. Runs `Checklist_PrepMenuData` and `Checklist_Init(GMMODE_CITYTRIAL, fresh)` - a real mode, so
   no assert - while a build flag makes the accessor serve the tab's block for City Trial. The
   screen takes City Trial's look but the tab's cells, counter and fillers. `fresh` is set when
   the tab holds an unviewed unlock, so it opens on the unlock presentation however it is reached.
2. Points SIS slot 0 at the tab's labels and loads its art.
3. Sets `ClearCheckerUI.mode` to the tab's mode, so the per-frame screen code reads the tab.

During step 1 `ClearCheckerUI.mode` still reads City Trial while the accessor already serves the
tab. A consumer hook that keys off the UI mode during a build has to remap through
`GetBuildMode()`, or it applies City Trial's data to the tab's board.

## Evaluation

`OnFrameStart` walks every tab's checks once `is_ready` holds:

- **Not recorded and not on the board** (neither `is_new` nor `is_unlocked`), and the predicate
  holds: call `record_complete`, then - outside a LAN session - set `is_new` and play the unlock
  SFX. The flip plays on the next visit to the tab. Testing the board rather than `is_recorded`
  makes this fire once even if `record_complete` fails to record.
- **Recorded but not on the board**: set `is_unlocked` and reveal the four neighbours, as
  `Checklist_ProcessUnlock` would. That covers a fresh board meeting completions the consumer
  already holds, which then show complete with no replay.

Predicates are therefore polled every frame in every scene - menus, loads and the title attract
demo included. Each must be a cheap read of state latched elsewhere and gate itself if it must
not fire in the demo. The SFX shares the engine's one-frame cooldown
(`stc_clearchecker_sfx_last_frame`), so a `record_complete` that goes through
`ClearChecker_SetNewUnlock` never plays it twice.

A tab hides cells the way the vanilla board does: a cell draws only once it is completed or a
completed neighbour has revealed it, so a fresh tab is blank and grows outward. The reveal is
positional and ignores whether a neighbour has a check, so on a tab with fewer than 120 checks
some revealed boxes can never fill. `RevealAll` sets `is_visible` only on cells backed by a
check, touches no unlock state, and is saved with the board.

## The saved board

The vanilla tabs' clear data lives in `GameData` and rides the game's save. A custom tab's lives
in the framework's hoshi save block, `CCSave`: a `u32` stamp, then `CC_TAB_MAX` slots of
`{ name_hash, GameClearData }`. Once bound, the accessor serves the slot itself, so every engine
write - a filler placed, a cell animated, a neighbour revealed - lands in the save the moment it
happens. That matters because hoshi writes the card at the game's own save requests, and
`Checklist_Think`'s filler placement sets `is_filler`, decrements the count and requests a save
in one pass; a once-per-frame copy would miss it.

Slots are keyed by the `hash_32_str` of the tab's name (0 bumped to 1, since 0 marks a free
slot), so a board survives tabs being added or reordered. Slots are never released; once all are
held, a new name runs on a RAM board and says so. A slot claimed fresh starts blank with
`grid_mapping` shuffled by `HSD_Randi`, with no pinned cells - no custom tab has a "fill in 100"
cell.

A tab starts on a RAM board at `Register` and binds to its slot at this mod's `OnSaveLoaded`, or
at `Register` for a later consumer. It cannot bind earlier: before the card load, `save_ptr` is
hoshi's default block. A consumer's `OnSaveLoaded` runs first and may already write to its tab -
reward badges, `RevealAll`, fillers. Those writes are per-cell flags and filler grants, never
positional, so the bind merges the RAM board into the slot: status bytes are OR-ed and filler
counts added, the icon list capped at 5.

`CCSave` starts with its own version word, `CC_SAVE_STAMP`. hoshi matches a card block to a mod
by name hash and size only, so `OnSaveLoaded` reinitializes the block when the stamp differs;
the stamp must change whenever `CCSave` does. The framework never calls `Hoshi_WriteSave`, which
rewrites the whole card file synchronously; hoshi flushes the block at every `Memcard_ReqSave`
the game makes, which also keeps custom boards in step with the vanilla ones.

## Labels

The screen shows a cell's objective from SIS slot 0 at `CLEARCHECKER_SIS_OBJECTIVE_BASE +
clear_kind`. After `Checklist_Init` loads City Trial's `SisClrChkCT` there, the framework points
slot 0 at its own pointer array: the tab's composed labels at their cells, one shared empty entry
for every other objective, and City Trial's own entries for the rest of the range - the blank
reward line (`0x7C`) and the 44 reward lines after it, which the reward panel reads. City Trial's
own `cb_Load` reloads the archive, so its labels are untouched.

A label is composed into a 160-byte entry the way vanilla's are: glyphs, `TEXTCMD_SPACE`, at
most one `TEXTCMD_LINEBREAK`, and a terminator, with no scale, color or layout opcodes - the
screen's `Text` object supplies those, and a label that pushes its own renders unlike the
vanilla tabs. A glyph costs 2 bytes and a space or break 1; the label is cut silently where it
would overrun, and a character `Text_CharToCommand` cannot map is dropped. The cell box holds
two lines and the engine squeezes an over-wide line rather than wrapping it, which is why every
vanilla entry carries its break explicitly. A label places its break with `\n`; without one, a
label over 30 characters breaks at the space nearest its midpoint, which can split a name from
its number. The framework does not stop a second `\n`, which makes a third line the box cannot
show.

## Theme and art

City Trial's tint lives in material diffuse colors in the background scene and the Cross,
Prize1 and Prize2 marker GObjs, and the menu's material animation rewrites them every frame. The
tab's `cb_ThinkPostGObjProc2`, which `updateFunction` runs after the GObj procs, recolors them each
frame: every green-dominant diffuse (`g > r && g >= b`) keeps its brightness range
`[min, green]` mapped onto the theme hue, `out = min + (green - min) * theme / max(theme)`. The
gate picks only the tint materials, and since the theme is not green-dominant it skips a material
already recolored. The walks cover each GObj's root JObj and its children, not the root's
siblings, which belong to other scenes.

`tex_file` names an HSD archive on the disc root exporting two `_HSD_ImageDesc` publics:

- the **banner**, the scrolling 248x128 panel behind the grid on `frame_gobj`, found by its
  unique 248 width. Its texture is swapped and its diffuse forced white so the image shows
  neutrally.
- the **emblem**, the tab indicator in the background scene, found as the one 40-wide I4 texture;
  after the swap it is recognized by pointer, so the replacement's size and format are free. It
  is an intensity mask, so it takes the theme tint. Its flipbook animation is cleared so the anim
  pass cannot swap it back.

The archive is loaded into the per-scene heap on each tab build, after `Checklist_Init` so the
build cannot reset the heap under it, and costs no permanent memory. `Gm_LoadGameFile` asserts on
a missing file, so the framework checks `DVDConvertPathToEntrynum` first and keeps City Trial's
art if it is absent. No texture cache flush is needed: `updateFunction` invalidates the cache
every frame before rendering.

## Tab ring and post-run routing

The ring is AR, TR, CT, then the custom tabs in registration order, wrapping, with the vanilla
tab-switch sound. `Checklist_MinorThink`'s phases:

- `NEXTTAB` / `PREVTAB` step the ring.
- `ENDING` goes to the shown tab's ending minor; a custom cell never raises it, since its
  reward lookup finds nothing.
- `EXIT`, after a round, first detours to any custom tab with an unviewed unlock so it animates
  before leaving; it raises `is_unlocked` once shown, so the next exit falls through.

A round only routes into the checklist when its minor exit finds an unviewed unlock in the
played mode, and a custom check lives in its tab's block. So `ClearChecker_CheckForNewUnlocks`
also reports pending custom tabs, and `Scene_SetNextMinor` - where each mode's minor exit
requests its checklist - sends a round with nothing of its own to animate straight to the first
pending custom tab. The retarget applies only on the way in: `Checklist_MinorThink`'s own tab
steps call `Scene_SetNextMinor` too, and a post-run session runs under the played mode's major,
so a step onto a vanilla tab would otherwise bounce back to the pending one. The guard is that
the current minor is not already a checklist tab.
