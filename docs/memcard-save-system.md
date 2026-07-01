# Memory Card / Save System

Covers two save files on the card and how their card-manager tiles (banner / icon /
comment) are produced:

1. **The vanilla game save** — created and managed by the game's `GCP_MemCard` worker.
2. **The `hoshi` save** — a separate file hoshi creates for mod data (`externals/hoshi/src/save.c`).

## Card-manager tile format (applies to any file)

A GameCube card file's tile is described by the read/write fields of `CARDStat`
(`externals/hoshi/include/os.h`), which are byte offsets into the file's own payload:

| Field | Off | Meaning |
|-------|-----|---------|
| `bannerFormat` | 0x2e | `CARD_STAT_BANNER_*` (NONE / C8 / RGB5A3), banner is 96×32 |
| `iconAddr`     | 0x30 | file offset of the icon image data |
| `iconFormat`   | 0x34 | 2 bits/frame, `CARD_STAT_ICON_*`, up to `CARD_ICON_MAX` (8) frames |
| `iconSpeed`    | 0x36 | 2 bits/frame, `CARD_STAT_SPEED_*`; first `SPEED_NONE` frame ends the loop |
| `commentAddr`  | 0x38 | file offset of the comment block: two 32-byte strings (title + description) |

The image bytes and comment text must live **inside the file** (the bytes written with
`CARDWrite`); the `CARDStat` fields just point at them. Procedure to set a tile:
`CARDGetStatus` → set those five fields → `CARDSetStatus`. `CARDGetStatus` (sync) and
`CARDSetStatus` (sync, `0x803e84e0` — wraps `CARDSetStatusAsync` + `__CARDSync`) are both
linked; `CARDSetStatusAsync` is also available.

## Vanilla game save

The boot-time **CardPrompt scene** (`CardPrompt_*`, `0x80047xxx`–`0x80048xxx`) checks for /
prompts to create the game save. Actual card I/O runs on a **threaded `GCP_MemCard`
worker**: callers enqueue a request (mutex-guarded queue, e.g. `Memcard_CreateSave`
`0x8007875c` → enqueue `0x80459e90`), then a worker thread processes it through the
`0x8045xxxx` handlers, which mount, create/write, and apply the tile via
`CARDGetStatus` + `CARDSetStatus`. Debug output is tagged `[GCP_MemCard]`.

Tile source assets are publics in **`LbMcGame.dat`** (preloaded into `PRELOADHEAPKIND_INIT`):

| Public | Off | Size | Format |
|--------|-----|------|--------|
| `MemCardBanner_01` | 0x0000 | 0x1800 (6144) | 96×32 RGB5A3 |
| `MemCardIcon_01`   | 0x1800 | 0x2200 (8704) | 8 × 32×32 CI8 (8192) + one 256-entry RGB5A3 TLUT (512) |
| `MemCardBannerData`/`MemCardIconData` | 0x3a00/0x3a08 | — | format/speed descriptors |

The icon descriptor is reached at `*(stc_save_info + 0x64)`; its first two bytes feed
`bannerFormat`, and each frame entry carries the per-frame format (+2) and speed (+10) bits.
In the create path the game writes `commentAddr = 0` (64-byte comment at file start),
`iconAddr = 0x40` (animated icon immediately after), and `bannerFormat = NONE` — i.e. the
tile is icon-only despite the banner asset being present. The comment is
"カービィのエアライド" (title) / "セーブデータ" (description), each padded to 32 bytes.

## hoshi save (`save.c`)

hoshi keeps mod data in its **own** card file named `"hoshi"`, sized up to a whole number of
`CARD_BLOCK_SIZE` (8 KB) blocks (`SAVE_SIZE`). `KARPlusSave_CreateOrLoad` mounts the card,
`CARDOpen`s `"hoshi"`, and `CARDCreate` + `CARDWrite`s it if missing; `KARPlusSave_Write`
rewrites it (hash-gated) on main-menu entry. Both hooks piggyback on the vanilla memcard
init so the prompt/no-card flow is shared.

`KARPlusSave` (`save.h`) is a small version header, then the card tile, then a 50-entry
`metadata[]`, then a flat `data[]`. The tile sits up front (ahead of `metadata[]`) on purpose -
see the tile section below. Each mod is handed a sub-region of `data[]` by `KARPlusSave_Alloc`
(keyed by a hash of the mod name), with `KARPlusSave_VerifySize` resizing in place across
versions. **All mods share this single file**, so the file has exactly one tile.

### Why a separate file (not the game's save)

- The vanilla save is a fixed-size, checksummed struct the game **rewrites whole** on its own
  saves; appended mod bytes would be clobbered and would fail the game's validation, risking a
  reformat that wipes real progress.
- Card files can't grow in place, and isolation means a corrupt/oversized/version-mismatched
  mod save can never endanger the vanilla save; uninstalling hoshi leaves it pristine.

### The tile (icon + banner API)

By default the `"hoshi"` file has a blank card-manager tile: `KARPlusSave_CreateOrLoad` only
`CARDCreate`s/`CARDWrite`s, so `bannerFormat`/`iconFormat` stay `NONE`. A mod populates it via:

```c
void Hoshi_SetSaveIconFile(const char *title, const char *description,
                           const char *icon_file, int frame_num, int speed);
void Hoshi_SetSaveBannerFile(const char *banner_file); // 96×32 RGB5A3; call AFTER the icon
```

- `KARPlusSave` carries a fixed `KARPlusSaveTile` (comment, an optional 96×32 RGB5A3 banner, and
  `HOSHI_SAVE_ICON_FRAMES` RGB5A3 32×32 icon frames) **between the version header and `metadata[]`**,
  not after it. This placement is required: `CARDSetStatus` rejects an `iconAddr` ≥ 512 with
  `CARD_RESULT_FATAL_ERROR`, and `metadata[]` alone is 1000 bytes, so a tile after it would put the
  image past the file's first 512 bytes - `CARDWrite` still stores the image bytes, but the
  directory entry is never updated and the tile silently stays blank.
- The image block is one **contiguous banner-then-icon run**: the CARD library derives each icon
  frame's offset as `iconAddr + banner size`, so `iconAddr` points at the banner when one is present
  (and the banner must physically precede the icon in the struct). With no banner `iconAddr` points
  straight at the icon.
- Size: `HOSHI_SAVE_ICON_FRAMES` (default 1) keeps the save at one 8 KB block, and each extra frame
  (≤ `CARD_ICON_MAX`) adds 2 KB. `HOSHI_SAVE_BANNER` (off unless a mod ships a banner) reserves
  `CARD_BANNER_SIZE_RGB5A3` (6 KB), which pushes the save to two blocks.
- **The art is loaded from disc, not baked into the mod.** `Hoshi_SetSaveIconFile`/`SetSaveBannerFile`
  (→ `KARPlusSave_SetIconFile`/`SetBannerFile`) only record the comment, animation params, and the
  RGB5A3 blob filenames (`<name>.dat`, raw `frame_num*2 KB` / 6 KB pixel files at the disc root). The
  registered names carry **no `_` or `.`** (the AP tab ships `ApIcon.dat` / `ApBanner.dat`): the DVD
  loader appends `.dat` only to a name without one, and treats any name that already contains a `_`/`.`
  as a complete path (so an `ap_icon` would be looked up verbatim, with no extension, and not found).
  The pixels are read into the tile by `KARPlusSave_LoadTileArt` on the **create path** of
  `KARPlusSave_CreateOrLoad`, just before `CARDWrite` - an existing file already carries its art, so
  the load path takes the pixels straight off the card. Because the create/load runs at the CardPrompt
  scene (after boot), the read can bounce through a *freeable* `HSD_MemAlloc` buffer (presence check via
  `KARPlusSave_FileExists` → `File_GetSize` size-check → `File_LoadSync` into an aligned temp → `memcpy`
  into the unaligned tile field → `HSD_Free`); nothing image-sized stays resident outside the save
  struct itself. The presence check matters because `File_GetSize`/`File_LoadSync` **panic** (assert)
  on a missing file - a build that ships without the blob just gets a blank tile instead of a crash.
  Call once at boot, and register the banner **after** the icon - the icon call clears the whole tile
  first.
- `KARPlusSave_CreateOrLoad` calls `KARPlusSave_ApplyTile` (`CARDGetStatus` → set
  `commentAddr`/`iconAddr`/`bannerFormat`/`iconFormat`/`iconSpeed` → `CARDSetStatus`) whenever
  `tile.is_set`. The comment/banner/icon bytes ride along in the normal `CARDWrite` of the struct.
- There is no save migration: changing the tile changes `KARPlusSave`'s layout, and an on-card file
  from an older `VERSION_MAJOR`/size is expected to be deleted by hand (the player starts fresh).
