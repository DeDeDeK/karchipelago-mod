# AP Checklist (Custom 4th Checklist)

A fourth, mod-owned checklist tab — the **AP checklist** — alongside the three
vanilla ones (Air Ride / Top Ride / City Trial). Its cells are custom AP
*locations*: objectives defined and tracked by mod code rather than the vanilla
clear-checker. Completing one sends an AP location check exactly like a vanilla
checkbox; multiworld items can be placed *on* its cells for display.

This builds on the vanilla clear-checker (`clearchecker-system.md`) and the
mod's reward/check integration (`checklist_rewards.c` / `check_detection.c`).

## Why a 4th tab is cheap

The in-game checklist is **one shared, mode-parameterized screen**, not three
separate ones, and it **already cycles through the three modes with L/R**. Adding
a fourth mode = adding a fourth tab to a rotation that already exists; almost
everything renders for free once `mode == 3` is plumbed through.

- The checklist is **minor-scene kinds `0x20`/`0x21`/`0x22`** (AR/TR/CT), all
  sharing one set of load/think/leave callbacks. `Checklist_MinorLoad`
  (`0x8004a768`) derives `mode = Scene_GetCurrentMinor() − 0x20` (`0x8004a798`)
  and calls `Checklist_Init(mode)` (`0x801822F4`), which writes `mode` into the
  checklist UI struct at `+0x14` (the single write site, `0x80182418`).
- `gmGetClearcheckerTypeP(mode)` (`0x800076a0`) is a 3-way `switch` returning the
  per-mode `GameClearData` embedded in `GameData` (`airride_clear` @ `+0xD68`,
  `topride_clear` @ `+0xE80`, `city_clear` @ `+0xFA8`). For `mode >= 3` it hits an
  assert and returns NULL — **this is the master lever**.
- Input in `Checklist_Think` (`0x8017F3BC`): **R|X → next-mode tab, L|Y →
  prev-mode tab, B → exit** (UI phases 12/13/11), consumed by
  `Checklist_MinorThink` (`0x8004a648`) as `Scene_SetNextMinor(currentMinor ± 1)`
  with wrap `0x22 ↔ 0x20`. Switching mode re-runs the whole `Checklist_Init`
  pipeline via `Scene_SetNextMinor` + `Scene_ExitMinor`. `Z` and `Start` are
  unused on this screen.

## Engine facts (mode-keyed surfaces)

| Surface | Address | Count | mode=3 behavior | Action for mode 3 |
|---|---|---|---|---|
| `gmGetClearcheckerTypeP` switch | `0x800076a0` | 3 cases | assert → NULL | REPLACEFUNC: add a mod-owned `GameClearData` for mode 3 |
| `stc_reward_table_ptrs` | `0x8049755C` | 3 ptrs | reads rodata "erro" → crash | relocate to a 4-entry array; mode-3 entry = empty table |
| `Checklist_RewardCounts` (`GetRewardNum`) | `0x805D51D0` (r13−0x7F10) | 3 bytes `{46,33,44}` | reads padding → 0 | hook/relocate; mode-3 count = 0 |
| `stc_audio_preview_tables` | `0x804AD2EC` | 3 ptrs | reads ASCII → crash | relocate; mode-3 = empty/stub |
| `stc_special_rewards` | `0x804AD270` | 3×5 bytes | OOB → bogus filler cells | relocate; mode-3 = "none" row |
| `GameData+0xD50` unlock cache | `0x80536728` | AR+CT only | not referenced for mode 3 | none, or parallel cache (mod's `CheckUnlocked` keys on AP bitfield anyway) |
| SIS slots `stc_sis_*[5]` | `0x8059a848` / `0x8059a85c` | 5 (room) | spare slot available | load AP objective strings into a spare slot |
| `mode >= 3` asserts (render path) | `gmGetClearcheckerTypeP 0x800076e4`, `GetRewardNum 0x80049c40`, `GetClearKindFromRewardIndex 0x80049cb8`, `Checklist_Init 0x801823a8` (CheckUnlocked / GetRewardFromClearKind are already REPLACEFUNC'd) | — | panic before OOB | relax / handle mode 3. `GetClearKindFromRewardIndex` is REPLACEFUNC'd → 0 for mode 3 (needed so `Checklist_ProcessUnlock`'s unlock-animation scan runs on the AP tab) |

`Checklist_InitGridMapping` (`0x8004A2BC`) and `Checklist_UpdateCellInfo`
(`0x80181D70`) are already mode-safe — they key off the `GameClearData*` and cell
geometry, not a mode array. Cells left `is_visible = 0` render blank, so the AP
checklist may define any number of checks (it need not fill 120).

## Design decisions

- **True 4th mode.** The AP checklist integrates as game-mode index `3` in the
  existing reward + check pipeline (rather than an isolated namespace), so its
  cells flow through `ClearChecker_SetNewUnlock` / `check_detection` and can host
  cross-mode reward placements like any other cell. `GMMODE_NUM` stays `3` (it
  legitimately means "the three real game modes" in many places); a distinct
  `AP_CHECKLIST_MODE = 3` / `CHECKLIST_MODE_NUM = 4` covers the structures that
  need the extra slot.
- **Entry via the existing L/R cycle.** The fourth tab joins the R|X / L|Y
  rotation (`AR → TR → CT → AP → AR`) by registering a fourth minor-scene
  descriptor and hooking the `±1` wrap in `Checklist_MinorThink` to include it.
- **Custom checks run a mod-side evaluator, not a vanilla jump-table splice.**
  The vanilla evaluators (`CityTrial_CheckForNewUnlocks`, the Air Ride Table A/B
  handler dispatch at `0x804975b8` / `0x80497630`) are fixed-size and only run
  during their mode's gameplay. Instead a mod-side evaluator iterates a
  `{ predicate, clear_kind }` table each frame and calls
  `ClearChecker_SetNewUnlock(AP_CHECKLIST_MODE, clear_kind)` on completion —
  isolated, runnable in any context, and it reuses `check_detection`'s existing
  `SetNewUnlock` REPLACEFUNC unchanged.
- **Wire layout: append, don't widen in place.** `APData` is read by the Python
  client *by field offset*. Widening `sent_checks[3][2] → [4][2]` in place would
  shift every field after it (deathlink/energylink mirrors, goal_complete) and
  break the live client. Instead the mode-3 slot is an **appended** field
  (`sent_checks_ap[2]`) at the end of `APData`/`APSave`: existing offsets are
  preserved, and the client only needs to learn the new trailing field. A small
  accessor maps `mode` → the right slot so call sites stay `slot(mode)`.

## Custom checks

A custom check is `{ int clear_kind; int (*is_complete)(void); }`. The evaluator
(`APChecklist_OnFrameStart`) skips checks already recorded, then for each
remaining check whose predicate is true calls
`ClearChecker_SetNewUnlock(AP_CHECKLIST_MODE, clear_kind)`. That funnels through
`CheckDetection_SetNewUnlockReplacement` → `RecordCheck` → the mode-3
`sent_checks` slot → AP. The call is idempotent (transition-gated), so
re-evaluating every frame is harmless.

Tracking state lives in `APSave` (persistent) or runtime, fed by hooks the mod
already owns — `custom_items` pickup handler, `custom_events` triggers,
EnergyLink thresholds, deathlink-survived, etc. — or by new lightweight counters.

## Phasing

**Phase 1 — data layer + check pipeline (testable via logging).**
- `AP_CHECKLIST_MODE` / `CHECKLIST_MODE_NUM`; append `sent_checks_ap[2]` to
  `APSave` + `APData`.
- Mod-owned mode-3 `GameClearData`; REPLACEFUNC `gmGetClearcheckerTypeP` to serve
  it (modes 0/1/2 unchanged, mode 3 → mod block, else NULL).
- `check_detection` accepts mode 3 (guards widened to `CHECKLIST_MODE_NUM`,
  `sent_checks` access via the slot accessor).
- A mod-side evaluator with 1–3 **stub checks** (observable conditions) proving
  the path end-to-end: stub completes → `[Check] mode=3 …` log + "Check sent"
  textbox + `sent_checks_ap` bit set + persisted.

The render-path table relocations and `mode >= 3` assert relaxations (the
`stc_reward_table_ptrs` / `RewardCounts` / `audio_preview_tables` /
`special_rewards` rows and `Checklist_Init`'s assert) are **deferred to Phase 2**:
they are only exercised once the tab actually renders, and several require
relocating fixed-size SDA/rodata tables that can't be unit-tested without the UI.

**Phase 2 — the visible tab (implemented; cell rendering pending live confirm).**

The render-table relocations originally planned here turned out to be unnecessary.
A 4th game-mode value is never fed to `Checklist_Init` at all:

- **Entry — a real 4th minor scene.** The minor lifecycle (`SceneChange_InitHeaps`
  `0x8000891c`, `Gm_Minor` `0x80008ad4`) looks a minor up by **linear search over
  `minor_scene_descs[]` matching `desc.idx`, bounded by `minor_scene_num`** — exactly
  what hoshi's `Hoshi_InstallMinorScene` populates. So `APChecklist_InstallMinor`
  clones the City Trial checklist descriptor (`minor_scene_descs[34]`), overrides its
  `cb_Load`, and installs it; the returned id is reachable via `Scene_SetNextMinor`.
- **Cycle — REPLACEFUNC `Checklist_MinorThink` (`0x8004a648`).** Reimplements the L/R
  tab rotation (phases 11/12/13/14) with the AP tab folded in: `AR→TR→CT→AP→AR`.
- **`cb_Load` — genuine mode-3 build via a scoped clear-data redirect.**
  `Checklist_Init`'s two mode-keyed lookups only have entries for the three real
  modes: the SIS string-group switch (`0x80182344`) asserts for `mode>=3`, and the
  cell-archetype reads (`0x80182494`, `0x801824e4`) index
  `MainMenuData[0xED8 + mode*4]`, where the mode-3 slot `0xEE4` is the **output slot**
  the function writes between the two reads (self-clobbering — un-patchable in place).
  But those two lookups are only the *visual template*; everything data-driven goes
  through `gmGetClearcheckerTypeP(chk->mode)`. So `APChecklist_MinorLoad` runs
  `Checklist_Init(GMMODE_CITYTRIAL)` (a valid mode → CT's template, no assert, no
  collision) while `g_ap_build_active` redirects `gmGetClearcheckerTypeP(CITYTRIAL)`
  to `AP_CLEAR` for the duration of the call. The build's columns, completion counter,
  and per-cell layout therefore come from the AP checklist's data; only the template
  is borrowed from CT. The UI mode byte (`chk+0x14`, reached as
  `MainMenu_GetData()[0xed0]→user_data`) is then flipped to `AP_CHECKLIST_MODE` so the
  per-frame think/update path also reads `AP_CLEAR`, and the redirect flag is cleared
  so the real City Trial tab still sees `city_clear`.
- **Render patch: REPLACEFUNC `Checklist_GetRewardNum` → 0 for mode 3.**
  The AP checklist has no native rewards, so reporting 0 gates every reward loop in
  the render path off (and dodges that function's own `mode>=3` assert). With the
  reward loops skipped, `stc_reward_table_ptrs` / `RewardCounts` /
  `audio_preview_tables` / `special_rewards` are never indexed at mode 3, so no table
  relocation is required. `ChecklistRewards_GetRewardFromClearKind` already returns
  the `0xFF` no-reward sentinel for `mode>=GMMODE_NUM`, which keeps the (button-gated)
  audio-preview path from dereferencing the OOB `audio_preview_tables[3]` pointer.
  One reward hook needed an explicit guard: `ChecklistRewards_ApplyCrossModeHasReward`
  (the post-reward-loop hook at `0x8017e07c`) runs even when the loop iterates zero
  times, and indexes `cross_mode_slots[current_mode]` (sized `[GMMODE_NUM][..]`). On the
  AP tab `current_mode == AP_CHECKLIST_MODE`, so it read `cross_mode_slots[3]` out of
  bounds and badged any unlocked AP cell (e.g. an animated custom check) with a spurious
  `has_reward`; it now early-returns for `mode>=GMMODE_NUM` like `ResolveCell`.
- **Full `grid_mapping` permutation (the navigation-crash fix).** `Checklist_Update`
  (`0x8018161c`) reverse-maps each cursor grid position (`row + col*12`) back to a
  clear_kind by linear-scanning `grid_mapping[0..119]`; a position with no kind falls
  through to clear_kind 120 and the engine asserts ("Clearchecker Number 120",
  `ClearChecker_GetKindClear` `0x8004a130` → `gmclearchecker.c:198`). Vanilla never
  hits this because `Checklist_InitGridMapping` (`0x8004a2bc`, called once from the
  save-init region `0x80006f14`–`0x80007a38`, **not** per checklist-open) writes a
  **full 120-entry permutation** — every position resolves to *some* kind, and
  `is_visible` decides which cells draw. The AP mode has no save-init, so
  `APChecklist_InitClearData` lays the permutation out itself (identity:
  `grid_mapping[k]=k`) and sets `is_visible` only for the defined checks. The earlier
  sparse `0xFF`-filled mapping was the actual cause of the 120 crash.

**Confirmed live (Phase 2/B):** the AP tab is reachable via the L/R cycle, builds its
grid from AP data (only the defined checks' cells draw — **`is_visible` gates cell
drawing**, so the identity `grid_mapping` over all 120 kinds is safe and no
navigation bounding is needed), and cursor-scrolls across the whole grid with no
"Number 120" assert. It borrows CT's tab icon + grid template + SIS group, so the
cells show CT's label text until custom SIS lands. (The first build flipped to mode 3
*after* a CT-data init with a sparse `grid_mapping`, which showed CT's full cell set
and crashed on scroll; the redirect build + full permutation fixed both.)

- **Custom cell labels (SIS).** The checklist shows the selected cell's objective
  text via `Text_StorePremadeText` (`0x8044f9d4`), which reads
  `stc_sis_data[text->sis_id][index]`; `Checklist_Update` fetches index
  `clear_kind + 4` from slot 0. `Checklist_Init`'s mode switch is a
  `Text_LoadSisFile(slot 0, ...)` that loads each mode's SIS file
  (`SisClrChk3D`/`SisClrChk2D`/`SisClrChkCT` for AR/TR/CT) — the AP build loads CT's.
  After the build, `APChecklist_InitSis` points slot 0 at an AP-owned pointer array:
  CT's header entries (0..3) preserved, the rest blank, each check's label composed
  in (the `custom_events` `ComposeSisText` recipe) and slotted at `clear_kind + 4`.
  Building a *separate* array (not editing CT's in place — the two tabs share the same
  `SisClrChkCT` archive) keeps the real CT tab's labels intact, and the CT tab
  reloads slot 0 on its own `cb_Load` regardless.

- **Cell completion and the unlock animation.** `ClearChecker_SetNewUnlock`
  (`0x8004a054`) is itself REPLACEFUNC'd by `check_detection`
  (`CheckDetection_SetNewUnlockReplacement`), which accepts `AP_CHECKLIST_MODE`: on a
  fresh cell it runs `RecordCheck` (sets `sent_checks_ap`, fires the "Check sent"
  textbox, re-evaluates goals) and, when the unlock cache is invalid (i.e. mid-run),
  also sets `clear[].is_new` and plays the unlock SFX — exactly as vanilla flags a
  gameplay objective. `APChecklist_OnFrameStart` drives the rest: for an unrecorded
  check whose predicate is true it calls `SetNewUnlock` once, then seeds
  `clear[ck].is_new`/`is_visible` itself — because a check satisfied *outside* any
  gamemode (e.g. "Boot the game", which vanilla never has) hits the cache-valid
  short-circuit and would otherwise never get `is_new`. It deliberately does **not**
  force `is_unlocked` on a fresh check: leaving the cell `is_new && !is_unlocked` is
  what lets `Checklist_ProcessUnlock` play the flip-and-sparkle (the animation raises
  `is_unlocked` itself, writing the cell byte to `0x04`). For an already-recorded check
  it re-asserts `is_visible` and raises `is_unlocked` only once no `is_new` is pending —
  so a check recorded on a prior boot (`AP_CLEAR` is BSS-zeroed each boot) shows
  complete with no replay, while a this-session completion still animates once.

- **Presentation gate: `Checklist_Init`'s `fresh` flag.** Leaving a cell `is_new` is
  necessary but not sufficient. `Checklist_Init` (`0x801822f4`) writes the Think
  state byte (`chk+0x15`) from its `fresh` arg: `fresh=1` → state **0** (the new-unlock
  presentation: a brief delay, then `Checklist_Think` calls `Checklist_ProcessUnlock`
  per frame to animate each pending cell), `fresh=0` → state **4** (jump straight to
  browsing, animation skipped). *This* is the engine's "only after a run" gate — vanilla
  passes `fresh=1` only when entering the checklist out of a gameplay session, never on
  manual navigation. The AP tab is never a post-run scene, so `APChecklist_MinorLoad`
  computes `fresh` itself from `APChecklist_HasPendingUnlock()` (any defined check with
  `is_new && !is_unlocked`): the AP tab enters the presentation state exactly when an AP
  check is newly completed, and jumps to browsing otherwise. `ProcessUnlock` raising
  `is_unlocked` after the flip clears the pending state, so re-entry returns `fresh=0`
  and the cell doesn't replay.

- **Post-run presentation: showing AP unlocks after a round.** A round routes into the
  checklist scene only when its mode's `*_MinorExit` (`CityTrial_MinorExit 0x8003fdd4`,
  `TopRide_MinorExit 0x8003eed8`, the Air Ride exit ~`0x8003e1xx`) finds
  `ClearChecker_CheckForNewUnlocks(mode) != 0` — a cache-stale scan of *that mode's*
  cells. An AP custom check spans any mode but lives in `AP_CLEAR`, so on its own it
  never trips that gate and the post-run checklist would be skipped. Two REPLACEFUNCs
  close that:
  - **`ClearChecker_CheckForNewUnlocks` → `APChecklist_CheckForNewUnlocks`**: returns the
    vanilla result OR `APChecklist_HasPendingUnlock()`, so any round that completes an AP
    check brings up the post-run checklist even with no coinciding vanilla cell. All 18
    call sites live inside the three `*_MinorExit` state machines and use it purely as the
    "is there something to show → route to checklist" decision, so OR-ing in AP-pending is
    behaviour-neutral for vanilla unlocks.
  - **`Scene_SetNextMinor` → `APChecklist_SetNextMinor`** (vanilla a one-line store to
    `GameData+0x7d8`): the chokepoint where each `*_MinorExit` requests the played mode's
    checklist tab (`MNRKIND_AIRRIDECHECKLIST/TOPRIDECHECKLIST/CITYCHECKLIST` = 32/33/34).
    When the request comes from a gameplay major (`Scene_GetCurrentMajor() != MJRKIND_MENU`
    — i.e. a post-run transition, not menu navigation) it raises `g_ap_postrun`, and if the
    played mode has no vanilla pending cell while the AP tab does, it retargets straight to
    the AP minor so the AP unlock animates directly (no detour through an empty played-mode
    tab). Menu navigation (major already `MENU`) and tab switching pass through untouched.

  The `g_ap_postrun` marker then drives the exit chokepoint in `APChecklist_MinorThink`
  (phase 11): when leaving a non-AP checklist tab post-run with an AP unlock still
  unviewed, it detours to the AP tab (which animates) instead of exiting; the next exit
  press falls through (the flip raised `is_unlocked`, so `HasPendingUnlock` is now false).
  So after a round that unlocked **both** a vanilla cell and an AP check, the played mode
  animates on its own tab, then the exit press swings to the AP tab to animate that — both
  checklists present, each on its tab. The marker is cleared on exit (phase 11) and when
  leaving to the records screen (phase 14), confining the chain to runs.

- **Mode-3 animation patch: REPLACEFUNC `Checklist_GetClearKindFromRewardIndex` → 0
  for mode 3.** `Checklist_ProcessUnlock` (`0x8017e490`) is the vanilla animation
  driver — `Checklist_Think` calls it on entry to animate `is_new && !is_unlocked`
  cells — and it reads its mode from the chk UI struct (`chk+0x14`), which on the AP
  tab is `AP_CHECKLIST_MODE`, so it walks `AP_CLEAR`. Its first new-unlock scan
  (`0x8017e4ec`) calls `Checklist_GetClearKindFromRewardIndex` for every `is_new` cell,
  and that function panics for `mode>=3` — so without this patch the first `is_new` AP
  cell would crash the game on tab entry. Returning 0 (AP has no rewards) keeps that
  scan inert; the second, reward-free scan then drives the animation. The function's
  other 12 call sites and all five `Checklist_ProcessUnlock` meta auto-unlock blocks
  (100-checkbox, Dragoon/Hydra) sit behind explicit `mode == 0/1/2` gates, so mode 3
  skips them and none of `check_detection`'s meta-unlock hooks can misfire on the AP
  tab. Real modes reproduce vanilla exactly (`reward_tables[mode][index].clear_kind`).

**Blue theme.** Each checklist tab is tinted with a per-mode color. That color is
carried two ways: the **frame border** is a per-mode asset (`Frame1/2/3_scene_models`,
built into `MainMenuData+0xEE4`) whose color is baked into a CMPR texture over a white
material — not runtime-recolorable; and the **background scene** (GObj at
`MainMenuData+0xED0`, the dominant color field) carries its color in its materials'
**diffuse** values (grayscale textures × diffuse). City Trial's background diffuses are
green-dominant, and they are **material-animated** — the menu's per-frame anim pass
re-applies the green tint every frame. So the recolor runs each frame from
`OnFrameEnd` (after that pass), not once at load. `APChecklist_RecolorScene` walks the
AP scene and rotates each diffuse `(R,G,B) -> (B,R,G)`, gated on green-dominant
(`g > r && g >= b`). The gate makes the pass self-stabilizing: an animated material is
reset to green each frame then rotated to blue, while a blue result is no longer
green-dominant so it is never double-rotated — static materials don't strobe and
non-green UI is left alone. The (always-purple) cell tiles are unaffected by the
recolor; the banner/frame and the tab emblem carry their look in textures (white
material), so instead of recoloring them the AP tab swaps those textures outright for
AP art (next section).

**Tab artwork (texture swap).** The AP logo art ships in a loadable HSD archive,
`ApChecklistTex.dat`, staged to the FST root from `mods/archipelago/assets/` and
authored by `scripts/hsd/make_checklist_textures.py` from `assets/ap-icon.png`. It
exports two `_HSD_ImageDesc` publics:

- `apBannerImg` — RGB5A3 248×128 panel that backs the checkbox grid (the scrolling
  banner quad at `MainMenuData+0xEE4`), the AP logo baked in as a faint watermark so
  the panel stays opaque under the grid (the vanilla banner is likewise a subtle
  low-contrast logo embossed into a gray panel, not a bold graphic).
- `apEmblemImg` — I4 64×64 intensity map of the logo for the top-right tab indicator
  (the 40×40 I4 silhouette quad inside the background scene at `MainMenuData+0xED0`).
  Intensity doubles as alpha (transparent background) and the quad takes the per-mode
  recolor tint, so the emblem renders through the vanilla path and reads as the AP
  logo while the surrounding circle layers and the blue tint stay.

The archive is loaded **per tab build** the same way the native checklist tabs get
their art: `APChecklist_LoadTextures` (called from `APChecklist_MinorLoad`, *after*
`Checklist_Init` so the scene build can't reset the heap under the load) runs
`Gm_LoadGameFile("ApChecklistTex", …)`, which drops it into the **reclaimable
per-scene heap** — so its descriptors live exactly as long as the AP-tab scene and
cost zero permanent memory. Because that heap is overwritten when the next scene loads
its archives, the descriptors are **not** cached across tabs: they are reloaded each
`MinorLoad` and NULL'd before each reload and on any load failure. The two swap walks
(`APChecklist_ProcessJObj` for the emblem, riding the recolor walk; `APChecklist_RetargetBanner`
for the banner quad) find the target TObjs by their unique texture signature (248-wide
banner, 40×40 I4 emblem), skip when the descriptor is NULL (leaving the borrowed CT art
in place), and are idempotent. The emblem's vanilla texture flipbook (`TObj.aobj` +
`imagetbl`) is cleared so the anim pass can't fight the swap. Because the descriptors
are repointed at runtime, `APChecklist_RecolorScene` issues one `GXInvalidateTexAll`
per frame while the AP tab is up so GX re-fetches the swapped texels rather than
rendering a stale TMEM cache.

**Still open in Phase 2:** optionally a custom tab title (SIS header entries 0..3);
reward badges on AP cells via the cross-mode path (needs `cross_mode_slots` widened to
the 4th target mode). The distinct AP tab icon and banner are done (the texture swap
above replaces CT's borrowed art).

**Phase 3 — real checks + AP protocol companion.**
- Replace stubs with the real custom objective set and their tracking hooks.
- AP-protocol/apworld work (outside this repo): new location definitions, item-ID
  range, and client reads for the appended `sent_checks_ap` field. Reusing the
  reward pipeline means items placed on AP cells display for free.

## Files (mod side)

- `mods/archipelago/src/ap_checklist.c` / `.h` — the mode-3 `GameClearData` (with its
  full `grid_mapping` permutation), the `gmGetClearcheckerTypeP` (with the build-time
  redirect) / `Checklist_GetRewardNum` / `Checklist_GetClearKindFromRewardIndex` /
  `Checklist_MinorThink` / `ClearChecker_CheckForNewUnlocks` / `Scene_SetNextMinor`
  REPLACEFUNCs (the last two driving the post-run AP-unlock presentation), the AP
  minor-scene install + `cb_Load`, the custom-check table (with per-check `label`) +
  evaluator, the slot-0 SIS override (`APChecklist_InitSis` / `ComposeSis`), and the
  per-scene texture load + banner/emblem swap (`APChecklist_LoadTextures` /
  `RecolorScene`).
- `mods/archipelago/assets/ApChecklistTex.dat` — the loadable HSD archive holding the
  AP banner (`apBannerImg`) and tab-emblem (`apEmblemImg`) `_HSD_ImageDesc` publics,
  staged to the FST root and loaded per AP-tab build.
- `scripts/hsd/make_checklist_textures.py` — authors `ApChecklistTex.dat` from
  `mods/archipelago/assets/ap-icon.png` (`uv run --with pillow python …`).
- `mods/archipelago/src/main.h` — `AP_CHECKLIST_MODE` / `CHECKLIST_MODE_NUM`;
  appended `sent_checks_ap`.
- `mods/archipelago/src/check_detection.c` — mode-3 `sent_checks` slot accessor +
  widened guards.
- `externals/hoshi/packtool/link.ld` + `include/game.h` — symbols/prototypes for
  `Checklist_Init`, `Checklist_MinorThink`, `Checklist_PrepMenuData`,
  `MainMenu_GetData`, `loadMainMenuMusic`, `MainMenu_ClearSoundTestSongThunk`.
