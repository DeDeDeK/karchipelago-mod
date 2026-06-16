# Stadium Gating

## Overview

Each `StadiumKind` (24 total) can be individually locked. When locked, the stadium cannot be selected — it is excluded from both shuffle-mode random selection and group-specific selection in City Trial, and it is hidden from any list-building UI that the game presents to the player. The vanilla unlock-check pipeline and the per-round selection logic are both replaced.

## What is Gated

24 `StadiumKind` values across 8 groups:

| Group | Stadiums |
|-------|----------|
| Drag Race | DRAG1, DRAG2, DRAG3, DRAG4 |
| Air Glider | AIRGLIDER |
| Target Flight | TARGETFLIGHT |
| High Jump | HIGHJUMP |
| Kirby Melee | MELEE1, MELEE2 |
| Destruction Derby | DESTRUCTION1–5 |
| Single Race | SINGLERACE1–9 |
| VS King Dedede | VSKINGDEDEDE |

## Vanilla Game System

The vanilla game has four functions that report stadium availability, plus a per-round selector. (Names follow `externals/hoshi/include/stadium.h` / `link.ld`; the symbol map names two of them differently — `0x8000C148` = `CityTrial_CheckIfStadiumIsDefaultUnlocked`, `0x8000C17C` = `CityTrial_CheckStadiumIsUnlocked` — but the mod and headers use the `Gm_Stadium*` names.)

| Function | Address | Role |
|----------|---------|------|
| `Gm_StadiumIsDefaultUnlocked` | 0x8000C148 | Jump table of stadiums available by default. Returns 1 for the low Drag Race kinds (0–2), else 0. |
| `Gm_StadiumIsUnlocked` | 0x8000C17C | Checklist-based check. Maps kinds 3–22 (the checklist-gated stadiums) through a jump table to clear/reward indices, then consults `Checklist_CheckCachedUnlock_CityTrial` (menu open) or `ClearChecker_CheckUnlocked` (`mode=2`). Returns 0 for kinds outside 3–22 (the default low kinds and Vs. King Dedede, kind 23). |
| `Gm_StadiumIsAvailable` | 0x8000C228 | Composite availability check. Inlines its **own** copies of the default-unlock and checklist-unlock jump tables and calls `Gm_StadiumCheckUnlocked` / `Checklist_CheckCachedUnlock_CityTrial` / `ClearChecker_CheckUnlocked` directly. It does **not** call the standalone `IsDefaultUnlocked` / `IsUnlocked` — replacing only those would leave `IsAvailable` reading the inlined tables. |
| `Gm_StadiumCheckUnlocked` | 0x80007EE4 | Reads the runtime unlock bitfield. With the checklist menu open it reads the temporary cache at `0x80536738`; otherwise the live bitfield at `0x80536EE8`. Cache writes are discarded on menu close. |

Per-round selection is handled by **`CityTrial_DecideStadium`** (0x8003F808). It branches on `gd->city.menu_stadium_selection`:

- **Shuffle mode** (`menu_stadium_selection == 0`): walks all 24 kinds, excludes the last 4 picks via the `prev_stadium_kind[5]` history (only entries 0–3 are consulted), keeps any kind passing `IsDefaultUnlocked` || `IsUnlocked`, then rolls a weighted pick from `gda->stadium_weights->weights[]` via `HSD_Randi(weight_total)`.
- **Group mode** (`menu_stadium_selection >= 1`): skips the history exclusion and instead keeps only kinds whose `Gm_GetStadiumGroupFromKind` equals `menu_stadium_selection - 1`.

After the loop it shifts the 4-entry history (`prev_stadium_kind[0..3]`) and writes the pick to `prev_stadium_kind[0]` and `gd->city.stadium_kind`.

### Vanilla History Buffer Bug

In shuffle mode the history exclusion is hardcoded to 4 entries. With fewer than 5 stadiums unlocked, the available picks can all fall inside the 4-entry history, leaving zero candidates and `weight_total == 0` — yet `HSD_Randi(weight_total)` is still called unconditionally (at 0x8003F908), giving `HSD_Randi(0)` (undefined behavior). This is latent in the original game and only surfaces when stadium availability is artificially restricted.

### Data references

| Symbol / field | Location | Notes |
|----------------|----------|-------|
| `gd->city.menu_stadium_selection` | GameData +0x396 (`u8`) | 0 = shuffle, 1+ = group (`StadiumGroup` + 1). |
| `gd->city.prev_stadium_kind[5]` | GameData +0x45E (`u8[5]`) | History buffer; only entries 0–3 used. |
| `gd->city.stadium_kind` | GameData +0x5AD (`u8`) | The decided stadium for the round. |
| `stc_gmdataall` | r13 (0x805DD0E0) +0x494 | `gmDataAll **`; `->stadium_weights->weights[STKIND_NUM]` (weights at +0x4). |
| Unlock bitfield | `0x80536EE8` | Live runtime bitfield. |
| New-label bitfield | `0x80536EEC` | "NEW" badge bits. |
| Checklist cache | `0x80536738` | Temporary unlock cache used while the checklist menu is open. |

## Implementation

**Files:** `gate_stadiums.c` / `gate_stadiums.h`

`GateStadiums_OnBoot` installs **five** `CODEPATCH_REPLACEFUNC` patches and **two** `CODEPATCH_REPLACEINSTRUCTION` patches.

### 1. Unlock Checks — `GateStadiums_IsUnlocked`

Replaces all four vanilla unlock-check functions with one mask read:

```c
static int GateStadiums_IsUnlocked(StadiumKind kind)
{
    if (!ap_save || kind < 0 || kind >= STKIND_NUM)
        return 0;
    return (ap_save->stadium_unlocked_mask & (1 << kind)) != 0;
}
```

All four must be replaced because `Gm_StadiumIsAvailable` inlines the `IsDefaultUnlocked` and `IsUnlocked` jump tables. Replacing only the standalone functions would still leave callers of `Gm_StadiumIsAvailable` reading the inlined tables.

**NULL guard:** `ap_save` is NULL before `OnSaveLoaded` runs, but `Gm_StadiumCheckUnlocked` is called during early game init. The replacement returns 0 in that window.

The runtime bitfield at `0x80536EE8`, its checklist cache layer, and `Gm_StadiumSetUnlockedDirect` / `Gm_StadiumClearUnlockedDirect` are now all dead code with respect to availability checks. Our save mask is the single source of truth, read live at every check, so mid-session changes (debug menu, AP item arrival) take effect immediately.

### 2. Stadium Selection — `GateStadiums_DecideStadium`

Replaces `CityTrial_DecideStadium`. The replacement exists primarily to fix the history-buffer bug; with the unlock-check replacement, an unmodified vanilla `DecideStadium` would already respect the mask.

The replacement:
- Builds the candidate pool by iterating `ap_save->stadium_unlocked_mask`.
- Respects the menu's stadium group selection (`menu_stadium_selection`): 0 = shuffle (all groups), 1+ = a specific group via `Gm_GetStadiumGroupFromKind`.
- Sizes the history exclusion dynamically: `min(unlocked_count - 1, 4)`. With 1 stadium unlocked the history is empty (no exclusion); with 5+ the full vanilla 4-entry exclusion is used. This guarantees at least one candidate.
- Falls back to "all unlocked stadiums" if the chosen group has no unlocked entries (prevents the soft-lock where the game can't pick anything).
- Rolls a weighted pick from `gda->stadium_weights->weights[]`.

### 3. `CityTrial_BuildStadiumList` Side-Channel Patches

`CityTrial_BuildStadiumList` (0x80046DF0) feeds the stadium selection UI. It calls the (now-replaced) `Gm_StadiumCheckUnlocked` per kind, so most of it already respects our mask — but it has two paths that re-add locked stadiums anyway:

- **Phase 1 auto-unlock loop** — gated by `progress (r13+0x550) >= 3` (the `blt` at **0x80046E1C**) plus a flag check `(flags & 0x28) == 0x28`. When both pass, the loop body at **0x80046E34** walks every kind our mask reports as locked and calls `Gm_StadiumWriteUnlocked(kind, 1)` and `Gm_StadiumWriteNewLabel(kind, 1)`. The unlock write is harmless (we ignore that bitfield), but the new-label write would put a "NEW" badge on every locked stadium in the UI for late-game players. The patch overwrites the `blt 0x80046E6C` at 0x80046E1C with an unconditional `b 0x80046E6C` (encoding `0x48000050`), skipping the flag check and the loop entirely.

- **Phase 2 checklist fallback** — in the main list-build loop (kind iterator starting at 0x80046EEC), when the replaced `Gm_StadiumCheckUnlocked` reports a stadium locked, the `beq` at **0x80046EF8** branches into a fallback (0x80046F44) that calls `Checklist_CheckCachedUnlock_CityTrial` and `ClearChecker_CheckUnlocked`. Either can re-add the stadium to the list. The patch retargets that `beq` from 0x80046F44 to the next loop iteration at 0x80046FC4 (encoding `0x418200CC`), skipping the fallback for locked stadiums.

## Save Data

`u32 stadium_unlocked_mask` in `APSave` — bit N = `StadiumKind` N.

## AP Items

24 unlock items in `AP_STADIUM_UNLOCK_BASE` (400) + `StadiumKind`. IDs 400–423. Routed through `GateStadiums_UnlockStadium`, which sets the mask bit, sets the vanilla "NEW" badge bit (so the checklist UI reflects the unlock), logs, and queues a textbox notification.

## Design Decisions

**Bypassing the game's bitfield entirely.** The mask is read directly at check time rather than synced into the game's bitfield at `0x80536EE8` (via `Gm_StadiumSetUnlockedDirect` / `ClearUnlockedDirect` during `OnSaveLoaded`). Syncing would create two problems: (1) mid-session mask changes (debug menu, late AP delivery) would need an extra sync step, and (2) the checklist cache layer could shadow writes. Reading directly avoids both — and matches every other `gate_*` system.

**Setting the "NEW" bitfield on unlock.** `Gm_StadiumCheckNewLabel` is *not* replaced — the checklist UI still consults the vanilla bitfield to decide which stadiums get a "NEW" badge. `GateStadiums_UnlockStadium` calls `Gm_StadiumSetNewLabelDirect` so newly unlocked stadiums show the badge.

**Dynamic history sizing.** With N unlocked stadiums, history size is `min(N-1, 4)`. This prevents the vanilla `HSD_Randi(0)` crash without sacrificing variety once enough stadiums are available.

**Group fallback.** If the player picks a specific group (e.g. Destruction Derby) but no stadium in that group is unlocked, selection falls back to all unlocked stadiums regardless of group, avoiding a soft-lock.
