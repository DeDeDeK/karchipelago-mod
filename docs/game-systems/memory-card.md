# Memory Card

How Kirby Air Ride reads and writes its save file, and how any GameCube card file gets its tile -
the banner, icon and two comment lines the card manager shows. A mod that keeps its own card file
next to the game's needs both: when the game touches the card, and how to give a file a tile.

## Card-Manager Tile Format

A file's tile is described by five read/write fields of its `CARDStat` directory entry:

| Field | Meaning |
|---|---|
| `bannerFormat` | None, C8 or RGB5A3; the banner is always 96x32 |
| `iconAddr` | Byte offset of the image data in the file |
| `iconFormat` | 2 bits per frame, up to `CARD_ICON_MAX` (8) frames |
| `iconSpeed` | 2 bits per frame; the first `CARD_STAT_SPEED_NONE` (0) frame ends the animation |
| `commentAddr` | Byte offset of two 32-byte strings, title then description |

Both addresses are offsets into the file's own bytes: the images and comment have to be inside the
data written with `CARDWrite`, and the directory entry only points at them. The image block is one
contiguous run - the banner, when there is one, then the icon frames - and the CARD library finds
the icon at `iconAddr` plus the banner size. `CARDSetStatus` (0x803e84e0, a synchronous wrapper
over `CARDSetStatusAsync` and `__CARDSync`) rejects an `iconAddr` at or past 512 with
`CARD_RESULT_FATAL_ERROR`, so the images have to start in the file's first 512 bytes.

The procedure is `CARDGetStatus`, set the five fields, `CARDSetStatus`, on an open file.

## The Game's Save

All of the game's card I/O runs on the `GCP_MemCard` worker thread, which tags its debug output
`[GCP_MemCard]`. Callers queue a request under a mutex and return at once: `Memcard_CreateSave`
(0x8007875c) for a new file, `Memcard_ReqSave` (0x80078990) to save. The worker mounts the card,
creates or writes the file, and applies its tile with `CARDGetStatus` and `CARDSetStatus`.

| Function | Address | Returns |
|---|---|---|
| `Memcard_GetState` | 0x80452790 | `MemcardStatus`: `MEMCARD_SUCCESS`, `_NOFILE`, `_BUSY`, `_ERROR` (no card) |
| `Memcard_GetSaveStatus` | 0x80078e30 | `MemcardSaveStatus`: `CARDSAVE_EXIST` (0), `CARDSAVE_NOEXIST` (2), `CARDSAVE_IGNORE` (4) |
| `Memcard_SetSaveStatus` | 0x80078e40 | Sets it |

**At boot**, the memory card prompt (`MJRKIND_CARD`, minor 38) checks for the save file.
`CardPrompt_MajorEnter` (0x800476b8) tries to load an existing file and leaves the prompt at once
when that works; otherwise the prompt's message window walks the player through creating a file or
continuing without one, and `CardPrompt_MinorExit` (0x800477ac) leaves for the title or the main
menu. With `CARDSAVE_NOEXIST`, the game prompts to insert a card on the next scene change. When
`Memcard_CheckIfUnitialized` reports the card gone at a minor change, `Gm_Minor` runs minor 38
instead of the queued minor.

**During play**, the game saves only through `Memcard_ReqSave`, called from main menu entry
(`MainMenu_MajorEnter`, `MainMenu_MinorEnter`), the select-screen loads (`CSS_airRide_ModeDispatch`,
`AirRideSelect_Init`, `TopRide_CourseSelectInit`, `TopRide_LobbyInit`, `CitySelect_MinorLoad`), the
results screen (`ResultScreen_LoadMinor`), the checklist (`Checklist_ProcessUnlock`,
`Checklist_Think`), the rumble, sound test and data delete panels of the Options menu, two more
sites, and the card prompt, the LAN menu and a debug test. Nothing saves mid-round.

## The Game's Tile

The tile art is in `LbMcGame.dat`, preloaded into `PRELOADHEAPKIND_INIT`:

| Public | Offset | Size | Format |
|---|---|---|---|
| `MemCardBanner_01` | 0x0000 | 0x1800 | 96x32 RGB5A3 |
| `MemCardIcon_01` | 0x1800 | 0x2200 | 8 frames of 32x32 CI8 (0x2000) and one 256-entry RGB5A3 palette (0x200) |
| `MemCardBannerData`, `MemCardIconData` | 0x3a00, 0x3a08 | - | Format and speed descriptors |

The icon descriptor is reached through the pointer at `stc_save_info+0x64` (0x8059a880); its first
two bytes feed `bannerFormat`, and each frame entry carries its format at +2 and speed at +10. The
create path writes `commentAddr = 0` (the 64-byte comment at the start of the file),
`iconAddr = 0x40` (the animated icon right after it) and `bannerFormat` none: the game's tile is
icon-only, though the banner asset exists. The comment is the game's Japanese title and "save
data".
