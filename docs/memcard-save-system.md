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

`KARPlusSave` (`save.h`) is a header (`version`, `mod_num`, a 50-entry `metadata[]`) followed
by a flat `data[]`. Each mod is handed a sub-region of that one file by `KARPlusSave_Alloc`
(keyed by a hash of the mod name), with `KARPlusSave_VerifySize` resizing in place across
versions. **All mods share this single file**, so the file has exactly one tile.

### Why a separate file (not the game's save)

- The vanilla save is a fixed-size, checksummed struct the game **rewrites whole** on its own
  saves; appended mod bytes would be clobbered and would fail the game's validation, risking a
  reformat that wipes real progress.
- Card files can't grow in place, and isolation means a corrupt/oversized/version-mismatched
  mod save can never endanger the vanilla save; uninstalling hoshi leaves it pristine.

### The tile (icon API)

By default the `"hoshi"` file has a blank card-manager tile: `KARPlusSave_CreateOrLoad` only
`CARDCreate`s/`CARDWrite`s, so `bannerFormat`/`iconFormat` stay `NONE`. A mod gives it an icon via:

```c
void Hoshi_SetSaveIcon(const char *title, const char *description,
                       const void *frames_rgb5a3, int frame_num, int speed);
```

- `KARPlusSave` carries a fixed `KARPlusSaveTile` header (comment + `HOSHI_SAVE_ICON_FRAMES`
  RGB5A3 32×32 frames, no banner) ahead of the per-mod `data[]`. The frame count dominates the
  file size; `HOSHI_SAVE_ICON_FRAMES` defaults to 1 (static icon, save stays one 8 KB block),
  raise it (≤ `CARD_ICON_MAX`) for an animated icon at the cost of a bigger save.
- `Hoshi_SetSaveIcon` (→ `KARPlusSave_SetIcon`) writes straight into the live struct's tile; mods
  boot after `KARPlusSave_Init`, so the struct already exists. Call it once at boot.
- `KARPlusSave_CreateOrLoad` calls `KARPlusSave_ApplyTile` (`CARDGetStatus` → set
  `commentAddr`/`iconAddr`/`iconFormat`/`iconSpeed` → `CARDSetStatus`) whenever `tile.is_set`. The
  icon bytes ride along in the normal `CARDWrite` of the save struct.
- There is no save migration: the tile changed `KARPlusSave`'s size, and an on-card file whose
  size ≠ `SAVE_SIZE` just fails to load (the player is expected to start fresh). `VERSION_MAJOR`
  was bumped to mark the layout change but is informational only.
