# Memory Card / Save System

Two separate files live on the card: the **vanilla game save**, created and managed by the game's
`GCP_MemCard` worker, and the **hoshi save**, a file hoshi creates for mod data
(`externals/hoshi/src/save.c`). This covers both, plus how their card-manager tiles (banner /
icon / comment) are produced.

## Card-Manager Tile Format

A GameCube file's tile is described by five read/write fields of `CARDStat`
(`externals/hoshi/include/os.h`): `bannerFormat` (none / C8 / RGB5A3; the banner is always 96x32),
`iconAddr`, `iconFormat` and `iconSpeed` (2 bits per frame, up to `CARD_ICON_MAX` = 8 frames; the
first `SPEED_NONE` frame ends the loop), and `commentAddr`, pointing at two 32-byte strings, title
then description.

`iconAddr` and `commentAddr` are byte offsets into the file's **own payload** - the image bytes and
comment text must be inside the bytes written with `CARDWrite`; the `CARDStat` fields only point at
them. The procedure is `CARDGetStatus` -> set those five fields -> `CARDSetStatus`
(`0x803e84e0`, a sync wrapper over `CARDSetStatusAsync` + `__CARDSync`).

## Vanilla Game Save

The boot-time **CardPrompt scene** (`CardPrompt_*`, `0x80047xxx`-`0x80048xxx`) checks for /
prompts to create the game save. Actual card I/O runs on a **threaded `GCP_MemCard` worker**:
callers enqueue a request (mutex-guarded queue, e.g. `Memcard_CreateSave` `0x8007875c` -> enqueue
`0x80459e90`), then a worker thread processes it through the `0x8045xxxx` handlers, which mount,
create/write, and apply the tile via `CARDGetStatus` + `CARDSetStatus`. Debug output is tagged
`[GCP_MemCard]`.

Tile source assets are publics in **`LbMcGame.dat`** (preloaded into `PRELOADHEAPKIND_INIT`):

| Public | Off | Size | Format |
|--------|-----|------|--------|
| `MemCardBanner_01` | 0x0000 | 0x1800 (6144) | 96x32 RGB5A3 |
| `MemCardIcon_01`   | 0x1800 | 0x2200 (8704) | 8 x 32x32 CI8 (8192) + one 256-entry RGB5A3 TLUT (512) |
| `MemCardBannerData`/`MemCardIconData` | 0x3a00/0x3a08 | - | format/speed descriptors |

The icon descriptor is reached at `*(stc_save_info + 0x64)`; its first two bytes feed
`bannerFormat`, and each frame entry carries the per-frame format (+2) and speed (+10) bits.
In the create path the game writes `commentAddr = 0` (64-byte comment at file start),
`iconAddr = 0x40` (animated icon immediately after), and `bannerFormat = NONE` - i.e. the
tile is icon-only despite the banner asset being present. The comment is
"カービィのエアライド" (title) / "セーブデータ" (description), each padded to 32 bytes.

## hoshi Save

hoshi keeps mod data in its **own** card file named `"hoshi"`, sized up to a whole number of
`CARD_BLOCK_SIZE` (8 KB) blocks (`SAVE_SIZE`). `KARPlusSave_CreateOrLoad` mounts the card,
`CARDOpen`s `"hoshi"`, and `CARDCreate` + `CARDWrite`s it if missing; `KARPlusSave_Write`
rewrites it (hash-gated). Both hooks piggyback on the vanilla memcard init so the prompt/no-card
flow is shared.

The `KARPlusSave` struct (`externals/hoshi/src/save.h`) is a four-byte version header
(`VERSION_MAJOR` / `VERSION_MINOR` / `mod_num`), then the card tile, then a 50-entry `metadata[]`
(20 bytes each, 1000 total), then a flat `data[]`. Each mod is handed a sub-region of `data[]` by
`KARPlusSave_Alloc` (keyed by a hash of the mod name), with `KARPlusSave_VerifySize` resizing in
place across versions. **All mods share this single file**, so the file has exactly one tile. The
tile's placement ahead of `metadata[]` is a hard requirement, not a style choice - see the tile
rules below.

### Why a separate file (not the game's save)

The vanilla save is a fixed-size, checksummed struct the game **rewrites whole** on its own saves;
appended mod bytes would be clobbered and would fail the game's validation, risking a reformat that
wipes real progress. Card files also can't grow in place. Isolation means a corrupt, oversized or
version-mismatched mod save can never endanger the vanilla save, and uninstalling hoshi leaves it
pristine.

### The settings-menu block

A mod's sub-region opens with its menu block: 3 bytes (`MenuSave`) for every persisted
`OPTKIND_VALUE` option reachable from `ModDesc.option_desc`, walked recursively by
`Menu_GetSaveSize`. Each entry is keyed by `Option_Hash` - a 16-bit hash of the parent
menu's name concatenated with the option's own name - so options are matched by name, not
position, and reordering a menu is free. Two consequences shape how menus are edited:

- **Names are the key.** Renaming an option, or moving it under a differently-named
  parent, orphans its saved value and the option falls back to its initializer. Two
  options whose parent+name pairs collide share one slot and overwrite each other.
- **Changing the persisted count rewrites the block.** `KARPlusSave_VerifySize` reports
  any grow or shrink, and hoshi answers by running `Mod_CopyToSave` - writing current
  defaults over that mod's whole menu block - instead of `Mod_CopyFromSave`. Adding or
  removing one toggle therefore resets every other toggle in the same mod, so menu edits
  are best batched per mod.

Set `OptionDesc.no_save` on an option that should not consume a slot: it is skipped by
the sizing walk and both copy directions, so it neither costs card space nor lets a
runtime-assigned name shift a hash. It suits options whose value is re-derived at load
from some other source, which is why every gate toggle in `archipelago_debug` carries it.

### When the hoshi file is written

`KARPlusSave_OnReqSave` is hooked at each **call site** of `Memcard_ReqSave`
(`0x80078990`), the vanilla save request, so the hoshi file is written at exactly the
points the game saves its own - `MainMenu_MajorEnter`/`MainMenu_MinorEnter`,
`ResultScreen_LoadMinor`, the stage and course selects, `Checklist_ProcessUnlock` /
`Checklist_Think`, and the rumble / sound-test toggles - and at no other time. (The
card-prompt, LAN and debug sites are left alone.) Two properties follow:

- **The two files stay in step.** Mod state that mirrors the vanilla save (checkbox
  filler counts, `GameClearData.clear[]` flags) can never end up a boot behind the
  vanilla file, which would let an item be re-granted or a completed checkbox become
  unreachable.
- **Card I/O stays out of gameplay.** `KARPlusSave_Write` is synchronous - it mounts,
  rewrites the whole file, and unmounts on the game thread - so a mod that wrote on a
  gameplay event would stall the frame. Mods mutate their save region in RAM and let this
  hook flush it. (Vanilla's own save is async: `Memcard_ReqSave` only enqueues onto the
  `GCP_MemCard` worker.)

`stc_hoshi_save_ready` gates the hook: a save can be requested before our own
create/load has run, and writing the default-filled struct then would overwrite a good
on-card file.

The hooks go on the call sites, not on `Memcard_ReqSave`'s entry. `_CodePatch_HookApply`
injects a bare `bl` to the hook function with no register or LR save (that is what the
macro's `_prologue`/`_epilogue` are for), so the injection is only safe where the
clobbered volatiles are already dead. Immediately before a `bl` they are: the site's own
call overwrites LR and the callee takes no arguments. At the function head the prologue
would spill the clobbered LR and `blr` back into the injection, re-entering the function
forever - a hang, not a crash.

A mod may still call `Hoshi_WriteSave` directly, but the synchronous card write is long
enough to be felt, so it is reserved for one-shot events like applying slot data on connect
or a debug reset - never a repeatable player action. An EnergyLink purchase, for instance,
accepts that a power-off between the withdrawal and the next vanilla save loses the goods
while the pool withdrawal still reaches the server.

### The tile (icon + banner API)

A freshly created `"hoshi"` file has a blank card-manager tile - `KARPlusSave_Init` leaves
`tile.is_set = 0`, so `KARPlusSave_CreateOrLoad` never touches `CARDSetStatus` and
`bannerFormat`/`iconFormat` stay `NONE`. A mod populates it from `OnBoot` with
`Hoshi_SetSaveIconFile(title, description, icon_file, frame_num, speed)` and then
`Hoshi_SetSaveBannerFile(banner_file)` - in that order, because the icon call clears the whole
tile first.

- `KARPlusSave` carries a fixed `KARPlusSaveTile` (comment, an optional 96x32 RGB5A3 banner, and
  `HOSHI_SAVE_ICON_FRAMES` RGB5A3 32x32 icon frames) **between the version header and `metadata[]`**,
  not after it. This placement is required: `CARDSetStatus` rejects an `iconAddr` >= 512 with
  `CARD_RESULT_FATAL_ERROR`, and `metadata[]` alone is 1000 bytes, so a tile after it would put the
  image past the file's first 512 bytes - `CARDWrite` still stores the image bytes, but the
  directory entry is never updated and the tile silently stays blank.
- The image block is one **contiguous banner-then-icon run**: the CARD library derives each icon
  frame's offset as `iconAddr + banner size`, so `iconAddr` points at the banner when one is present
  (and the banner must physically precede the icon in the struct). With no banner `iconAddr` points
  straight at the icon.
- Size: `HOSHI_SAVE_ICON_FRAMES` (currently 1) costs `CARD_ICON_SIZE_RGB5A3` (2 KB) per frame, up to
  `CARD_ICON_MAX` (8). `HOSHI_SAVE_BANNER` (currently 1, since the AP tab ships a banner) reserves a
  further `CARD_BANNER_SIZE_RGB5A3` (6 KB). One icon frame alone fits the save in a single 8 KB block;
  with the banner enabled it takes two.
- **The art is loaded from disc, not baked into the mod.** The two API calls only record the comment,
  animation params, and the RGB5A3 blob filenames (`<name>.dat`, raw `frame_num*2 KB` / 6 KB pixel
  files at the disc root). The registered names carry **no `_` or `.`** (the AP tab ships
  `ApIcon.dat` / `ApBanner.dat`, from `mods/archipelago/assets/`): the DVD loader appends `.dat` only
  to a name without one, and treats any name that already contains a `_`/`.` as a complete path (so an
  `ap_icon` would be looked up verbatim, with no extension, and not found). The pixels are read into
  the tile by `KARPlusSave_LoadTileArt` on the **create path** of `KARPlusSave_CreateOrLoad`, just
  before `CARDWrite` - an existing file already carries its art, so the load path takes the pixels
  straight off the card. Because the create/load runs at the CardPrompt scene (after boot), the read
  can bounce through a *freeable* `HSD_MemAlloc` buffer (presence check via `KARPlusSave_FileExists`
  -> `File_GetSize` size-check -> `File_LoadSync` into an aligned temp -> `memcpy` into the unaligned
  tile field -> `HSD_Free`); nothing image-sized stays resident outside the save struct itself. The
  presence check matters because `File_GetSize`/`File_LoadSync` **panic** (assert) on a missing file -
  a build that ships without the blob just gets a blank tile instead of a crash.
- `KARPlusSave_CreateOrLoad` calls `KARPlusSave_ApplyTile` (`CARDGetStatus` -> set
  `commentAddr`/`iconAddr`/`bannerFormat`/`iconFormat`/`iconSpeed` -> `CARDSetStatus`) whenever
  `tile.is_set`. The comment/banner/icon bytes ride along in the normal `CARDWrite` of the struct.
- There is no save migration: changing the tile changes `KARPlusSave`'s layout, and an on-card file
  from an older `VERSION_MAJOR`/size is expected to be deleted by hand (the player starts fresh).
