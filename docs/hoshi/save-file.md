# Save File

hoshi keeps every mod's persistent data in its own memory card file, named `hoshi`, next to the
game's save. Each mod gets a block in it holding its settings-menu values and, if it asks for one,
a struct of its own. hoshi creates and loads the file at boot and writes it whenever the game
saves its own file. A modder needs this to size a save struct, to know when a change reaches the
card, and to keep a struct readable across builds.

## Why a Separate File

The game's save is a fixed-size, checksummed struct that the game rewrites whole: mod bytes
appended to it would be clobbered and would fail the game's validation, risking a reformat that
wipes real progress, and a card file cannot grow in place. In a file of its own, a corrupt,
oversized or mismatched mod save cannot touch the game's progress, and removing hoshi leaves the
game's save as it was.

## Layout

The file is one `KARPlusSave`, rounded up to whole 8 KB card blocks (`SAVE_SIZE`):

```c
typedef struct KARPlusSave
{
    u8 version_major, version_minor; // VERSION_MAJOR / VERSION_MINOR, informational
    u8 mod_num;                      // metadata[] entries in use
    u8 pad;
    KARPlusSaveTile tile;            // card-manager comment, banner and icon
    struct { int mod_hash; ...; menu_data {offset, size}; user_data {offset, size}; } metadata[50];
    u8 data[];                       // every mod's block, packed end to end
} KARPlusSave;
```

`KARPlusSave_Alloc` gives each mod a block in `data[]`, recorded in a `metadata[]` entry keyed by
a CRC-32 of `ModDesc.name`. A block is the mod's menu rows - one 3-byte `MenuSave` per saved
settings option (a 16-bit option hash and a `u8` value index) - followed by `ModDesc.save_size`
bytes of the mod's own struct, which hoshi points `ModDesc.save_ptr` at. A mod that is not loaded
keeps its block in the file, so turning a mod off for a session does not lose its data. All mods
share whatever is left of the rounded file after the header, the tile and the metadata table.

## Boot Sequence

1. `Gm_InitData` runs `Mods_SetDefaultSaveData`: every mod with save data gets a block in the
   in-memory struct, its menu rows take the options' current values, and its `OnSaveInit` runs to
   fill its struct with defaults.
2. The memory card prompt scene creates or reads the card file. An existing file must be exactly
   `SAVE_SIZE`; otherwise it is not read.
3. For each mod, an existing block is matched by name hash. `KARPlusSave_VerifySize` compares its
   size with what the mod now needs and resizes it in place, keeping the old bytes. A block that
   matched at the same size has its menu values copied into the mod's options; a resized or new
   block instead gets the mod's current option values written over it.
4. Every mod's `OnSaveLoaded` runs, then every value option's `on_change`.

Reading the file replaces the whole in-memory struct, including the defaults `OnSaveInit` wrote in
step 1, and `OnSaveInit` does not run again. A block the file did not have yet, or the part a
resize added, therefore starts with whatever bytes the file held there - zeros in a fresh file -
so `OnSaveLoaded` has to recognize an uninitialized struct itself.

With no card inserted, step 2 is skipped, the defaults from step 1 stand, and `OnSaveLoaded` still
runs. hoshi also skips the prompt then: it marks the game's save as ignored, goes straight to the
main menu, marks the tutorial movies as seen, unlocks everything and fills all three checklists.

## Versioning

hoshi does not check a mod's save layout. Blocks are matched by name hash and size only, and a size
change keeps the old bytes, so a mod whose struct changes shape must guard it itself. The usual
guard is a stamp word as the struct's first field, written by `OnSaveInit` and checked in
`OnSaveLoaded`, where a mismatch - including the zeros of a new block - reinitializes the struct.
It goes first so a resize cannot move it, and it changes whenever the struct's layout does, even
for a same-size reorder. Saves are not migrated.

`ModDesc.version.major` is read in two places, neither of them a layout check. `Hoshi_ImportMod`
asserts when the importer was built against an older major of the exporting mod.
`Hoshi_RestoreModSave`, which restores a backup taken with `Hoshi_BackupModSave` from the mods
whose `ModDesc.affects_gameplay` is set, copies a mod's struct back only while the backup's major
is no higher than the installed mod's.

## When the File Is Written

`KARPlusSave_Write` is synchronous: it mounts the card, rewrites the whole file and unmounts, on
the game thread. It first copies every mod's live option values into its menu rows
(`Mod_CopyAllToSave`), then skips the card entirely when a hash of the struct is unchanged.

It runs from a hook at each call site of the game's own save request, `Memcard_ReqSave`
(0x80078990), so the file is written exactly when the game saves: main menu entry
(`MainMenu_MajorEnter`, `MainMenu_MinorEnter`), the select-screen loads, the results screen,
`Checklist_ProcessUnlock` and `Checklist_Think`, the rumble, sound test and data delete panels, and
two more call sites - every caller but the card prompt, the LAN menu and a debug test. It also runs
when the player leaves the settings menu. Two properties follow:

- **The two files stay in step.** Mod state that mirrors the game's save never ends up a boot
  behind it, which could re-grant an item or strand a completed checklist cell.
- **Card I/O stays out of gameplay.** Mods change their block in memory and let the next save
  flush it. The game's own save is asynchronous - `Memcard_ReqSave` only queues a request for its
  `GCP_MemCard` worker thread - so a synchronous write mid-round would stall the frame.

The hooks sit on the call sites rather than at `Memcard_ReqSave`'s entry because a code-patch hook
calls out with a bare `bl` and no register save: right before the site's own `bl` the clobbered
registers are dead, while at a function's entry the prologue would save the clobbered LR and the
function would return into the hook forever. The hook does nothing until the boot-time create or
load has run, because writing the default-filled struct before then would overwrite a good file.

A mod may call `Hoshi_WriteSave` directly, but the write is long enough to be felt, so it suits
one-shot events such as a debug reset, not repeatable player actions.

## Card Tile

A new `hoshi` file has a blank card-manager tile unless a mod supplies one from `OnBoot`:

- `Hoshi_SetSaveIconFile(title, description, icon_file, frame_num, speed)` sets the two comment
  lines (31 characters each) and the icon animation, and clears the whole tile first.
- `Hoshi_SetSaveBannerFile(banner_file)` adds a 96x32 banner, so it is called after the icon.

The image files are raw RGB5A3 pixel blobs on the disc - `frame_num` 2 KB frames for the icon,
6 KB for the banner - named without the `.dat` extension, which the loader appends. A name must
contain no `_` or `.`, because the loader treats such a name as a complete path. The pixels are
read into the tile only when the file is created, just before the first card write, so an existing
file keeps the art it was created with. A missing or wrong-size blob leaves that image blank rather
than crashing, since the file presence is checked first.

Three constraints shape the tile's place in the struct:

- The card directory entry points at the comment and images by byte offset, so they live inside
  the file's own bytes.
- `CARDSetStatus` rejects an `iconAddr` at or past 512 with `CARD_RESULT_FATAL_ERROR`, which is why
  the tile sits before the 1000-byte metadata table.
- The CARD library finds the icon at `iconAddr` plus the banner size, so the banner directly
  precedes the icon and `iconAddr` points at the banner when there is one.

Each boot, if the tile is set, `KARPlusSave_ApplyTile` points the directory entry at it through
`CARDGetStatus` and `CARDSetStatus` (0x803e84e0). Changing the tile's size changes the file size,
and hoshi never reads a file of the old size again, so it has to be deleted by hand.
