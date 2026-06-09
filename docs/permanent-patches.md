# Permanent Patches

## Overview

Permanent patches are stat boost items received from AP that persist across both City Trial rounds and Air Ride races. The game tracks how many of each patch type the player has accumulated and re-applies them at the start of every round/race. Application is gated per-mode by three menu toggles under *Archipelago Settings → Permanent Patches* (all default **On**, declared in `settings_menu.h`):

- **City Trial** (the standard Trial mode, `CITYMODE_TRIAL`) — `ap_menu_settings.ct_permanent_patches_enabled`
- **CT Stadium** (`CITYMODE_STADIUM`) — `ap_menu_settings.ct_stadium_permanent_patches_enabled`
- **Air Ride** (and any other non-City 3D major) — `ap_menu_settings.ar_permanent_patches_enabled`

The three City modes are distinct menu selections, not phases of one session. `PermanentPatch_ShouldApply()` dispatches on `Scene_GetCurrentMajor() == MJRKIND_CITY` and then `Gm_GetCityMode()` to pick the right toggle (see [Mode gating](#mode-gating) below). It does **not** use `Gm_IsStadiumMode()`.

**City Trial Free Run (`CITYMODE_FREERUN`) is never applied** — `PermanentPatch_ShouldApply()` returns 0 for it unconditionally, regardless of any toggle. Free Run does not load the item-data tables, so the inflated stats from permanent patches would crash `Item_GetItDataPtr` when the game tries to eject patches on damage.

Top Ride is not reached at all: it loads through `OnTopRideLoad` (minor `MNRKIND_19`), so `On3DLoadEnd` never fires for it. It also has no `MachineData` (see `topride-system.md`).

**Files:** `patch_item.c` / `patch_item.h` (receive + apply), `main.h` (`APSave` save data), `settings_menu.h` / `settings_menu.c` (per-mode toggles), `ap_item_handler.c` (receive routing)

## Implementation

- AP item IDs (`APItemId` enum, `archipelago_api.h`): per-stat `AP_PERM_PATCH_BASE` + PatchKind (`100`–`108`, since `PATCHKIND_NUM == 9`) and `AP_ITEM_PERM_PATCH_ALL_UP` (`7`)
- `PermanentPatch_GiveItem(kind)` and `PermanentPatch_GiveAllUp()` in `patch_item.c` — increment `ap_save->permanent_patches[]` (clamped at `PATCH_STAT_MAX`) and enqueue a textbox "Received" notification; no immediate stat application
- `APItems_HandleItem` (in `ap_item_handler.c`) routes these items above the scene gate; the receive path is a pure save-data update and always returns 1
- `PermanentPatch_On3DLoadEnd()` (gated by `PermanentPatch_ShouldApply()`) creates a per-frame GObj that waits for `GMINTRO_END`, then calls `PermanentPatch_DoApply()` once
- `PermanentPatch_DoApply()` (`static`) applies accumulated counts from `ap_save->permanent_patches[PATCHKIND_NUM]` using all-up consolidation (see below)
- Patch cap system in `patch_cap.c` replaces `Machine_GivePatch` / `Machine_GiveAllUp` with `PatchCap_GivePatch` / `PatchCap_GiveAllUp` (capped versions) via `CODEPATCH_REPLACEFUNC`

### Patch kinds & item IDs

`PatchKind` (`item.h`) and its matching `+1` `ItemKind` and AP item ID line up 1:1, in this order:

| PatchKind (value) | ItemKind | Per-stat AP item ID |
|---|---|---|
| `PATCHKIND_WEIGHT` (0)   | `ITKIND_WEIGHT`   | `AP_PERM_PATCH_WEIGHT` (100) |
| `PATCHKIND_ACCEL` (1)    | `ITKIND_ACCEL`    | `AP_PERM_PATCH_BOOST` (101) |
| `PATCHKIND_TOPSPEED` (2) | `ITKIND_TOPSPEED` | `AP_PERM_PATCH_TOPSPEED` (102) |
| `PATCHKIND_TURN` (3)     | `ITKIND_TURN`     | `AP_PERM_PATCH_TURN` (103) |
| `PATCHKIND_CHARGE` (4)   | `ITKIND_CHARGE`   | `AP_PERM_PATCH_CHARGE` (104) |
| `PATCHKIND_GLIDE` (5)    | `ITKIND_GLIDE`    | `AP_PERM_PATCH_GLIDE` (105) |
| `PATCHKIND_OFFENSE` (6)  | `ITKIND_OFFENSE`  | `AP_PERM_PATCH_OFFENSE` (106) |
| `PATCHKIND_DEFENSE` (7)  | `ITKIND_DEFENSE`  | `AP_PERM_PATCH_DEFENSE` (107) |
| `PATCHKIND_HP` (8)       | `ITKIND_HP`       | `AP_PERM_PATCH_HP` (108) |

All-up is the separate standalone ID `AP_ITEM_PERM_PATCH_ALL_UP` (7), not part of the 100-range block.

### Game functions used (addresses from `GKYE01.map`)

| Function | Address | Role |
|---|---|---|
| `Machine_GivePatch(md, kind, num)` | `0x801cacf4` | Apply +num to one stat (replaced by `PatchCap_GivePatch`) |
| `Machine_GiveAllUp(md, num)`       | `0x801cad40` | Apply +num to all 9 stats (replaced by `PatchCap_GiveAllUp`) |
| `Scene_GetCurrentMajor()`          | `0x8000AEA8` | Major scene kind (`MJRKIND_CITY` vs other) |
| `Gm_GetCityMode()`                 | `0x8003F6CC` | Selected City mode (Trial/Stadium/Free Run) |
| `Gm_GetIntroState()`               | `0x8000A958` | Intro/countdown state; `GMINTRO_END` == fully started |
| `Ply_GetPKind(p)`                  | `0x8022C858` | Player kind; `PKIND_HMN` filters human players |
| `Ply_GetMachineGObj(p)`            | `0x8022d230` | Machine GObj whose `userdata` is `MachineData` |

## Save Data

`APSave` (in `main.h`, accessed via the global `ap_save`) contains:

```c
u8 permanent_patches[PATCHKIND_NUM];    // Accumulated permanent patch count per stat (0-PATCH_STAT_MAX)
```

9 bytes total (`PATCHKIND_NUM == 9`). Each entry tracks how many permanent +1 patches of that PatchKind have been received from AP. Incremented when the item is received (clamped at `PATCH_STAT_MAX`, which is 127 — the hardware sign-extension ceiling, see `patch-cap.md`), never decremented.

No separate `permanent_allup_count` field is needed — when an all-up is received, increment all 9 entries by 1. This simplifies storage and makes the all-up consolidation at application time purely an optimization, not a data concern.

## Receiving Permanent Patches

`PermanentPatch_GiveItem` and `PermanentPatch_GiveAllUp` in `patch_item.c` only increment save data — `ap_save->permanent_patches[kind]++` (or all 9 for all-up), each clamped at `PATCH_STAT_MAX`. They also enqueue a "Received: permanent +1 …" textbox notification. No stat is applied at receive time.

Permanent patch handling in `APItems_HandleItem` (`ap_item_handler.c`) sits above the scene gate. The save data increment always succeeds and returns 1 immediately — the item is consumed from the queue. The round-start hook below applies the accumulated counts the next time a gated CT-Trial / CT-Stadium / Air Ride scene loads (never in Free Run).

### Why no immediate apply?

Applying the +1 immediately (via `Patch_GiveItem` / `Patch_AllUp_GiveItem`) would double-apply in two places:

- **Stadium load after CT pickup.** Stats carry over from a Trial's city-driving phase into the stadium machine, but `On3DLoadEnd` fires again on stadium entry. The round-start hook would then re-apply every accumulated permanent patch on top of the carried-over stats. (Here "stadium" refers to the trial's stadium phase, not the distinct `CITYMODE_STADIUM` menu mode.)
- **Subsequent rounds.** The first round after receiving a patch would already have +1 from the immediate apply. The next round's `On3DLoadEnd` would apply the count from save data on top — but since patches don't carry across rounds, this case balances out. The stadium case is the one that actually breaks.

Deferring all application to `PermanentPatch_DoApply` makes save data the single source of truth: the count is what determines the boost, and nothing applies a patch outside the round-start path.

## Round-Start Application

`On3DLoadEnd()` calls `PermanentPatch_On3DLoadEnd()`. That function first checks the mode + menu gate (`PermanentPatch_ShouldApply()`), bails early if there is nothing accumulated, resets the once-per-scene guard, then spawns a per-frame GObj proc (`PermanentPatch_PerFrame`):

```c
static int permanent_patches_applied;   // once-per-scene guard

static void PermanentPatch_PerFrame(GOBJ *g)
{
    if (permanent_patches_applied)
        return;                              // already done — no-op (the proc is NOT removed)
    if (Gm_GetIntroState() != GMINTRO_END)
        return;                              // wait for the intro/countdown to finish
    permanent_patches_applied = 1;
    PermanentPatch_DoApply();
}

void PermanentPatch_On3DLoadEnd()
{
    if (!PermanentPatch_ShouldApply())       // mode + per-mode menu toggle (see below)
        return;

    int total = 0;                           // skip the GObj entirely if nothing to apply
    for (int i = 0; i < PATCHKIND_NUM; i++)
        total += ap_save->permanent_patches[i];
    if (total == 0)
        return;

    permanent_patches_applied = 0;
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, PermanentPatch_PerFrame, 0, 0, 0, 0);
}
```

The proc does **not** delete itself once it fires — it keeps running but short-circuits on the `permanent_patches_applied` static flag, so `DoApply` runs exactly once per scene load. Application is deferred until `GMINTRO_END` so machines are fully initialized.

`PermanentPatch_DoApply()` (single loop over players, all-up first then per-stat remainders — see [consolidation](#all-up-consolidation-reducing-diff)):

```c
static void PermanentPatch_DoApply()
{
    // Minimum across all 9 stats = how many all-ups we can consolidate
    u8 min_patches = ap_save->permanent_patches[0];
    for (int i = 1; i < PATCHKIND_NUM; i++)
        if (ap_save->permanent_patches[i] < min_patches)
            min_patches = ap_save->permanent_patches[i];

    // (an OSReport here logs min_patches, the total, and the per-stat counts)

    for (int p = 0; p < 5; p++)
    {
        if (Ply_GetPKind(p) != PKIND_HMN)
            continue;
        GOBJ *mg = Ply_GetMachineGObj(p);
        if (!mg)
            continue;
        MachineData *md = mg->userdata;

        if (min_patches > 0)                                 // consolidated all-ups
            Machine_GiveAllUp(md, min_patches);

        for (int i = 0; i < PATCHKIND_NUM; i++)              // per-stat remainders
        {
            int remainder = ap_save->permanent_patches[i] - min_patches;
            if (remainder > 0)
                Machine_GivePatch(md, i, remainder);
        }
    }
}
```

### Mode gating

`PermanentPatch_ShouldApply()` decides whether to apply at all and which toggle governs:

| Scene major | `Gm_GetCityMode()` | Result |
|---|---|---|
| `MJRKIND_CITY` | `CITYMODE_FREERUN` (2) | **Always skipped** (item data not loaded; logs and returns 0) |
| `MJRKIND_CITY` | `CITYMODE_STADIUM` (1) | gated by `ct_stadium_permanent_patches_enabled` |
| `MJRKIND_CITY` | `CITYMODE_TRIAL` (0) | gated by `ct_permanent_patches_enabled` |
| any other (Air Ride) | — | gated by `ar_permanent_patches_enabled` |

Note the code dispatches on `Gm_GetCityMode()`, *not* `Gm_IsStadiumMode()`. The comment in `patch_item.c` explains why it cannot use `Gm_IsInCity()` (that helper is stage-based — only the CT main map, stage_kind 9/52 — and excludes stadiums).

### Ordering in `On3DLoadEnd`

The actual call sequence in `main.c` (LinkLink hooks are wrapped in their menu-enable guards):

```c
GateAbilities_On3DLoadEnd();
ItemSpawnFilter_On3DLoadEnd();
PermanentPatch_On3DLoadEnd();
if (ap_menu_settings.deathlink_enabled)  DeathLink_On3DLoadEnd();
if (ap_menu_settings.energylink_enabled) EnergyLink_On3DLoadEnd();
if (ap_menu_settings.traplink_enabled)   TrapLink_On3DLoadEnd();
GoalMaxStatsCT_On3DLoadEnd();
```

EnergyLink tracks deltas from RiderData stats (not MachineData), and rider stats are synced from machine stats by the game's normal update loop. Crucially, EnergyLink does not rely on this ordering: its per-frame proc snapshots a per-player baseline (`prev_stats`) on the **first frame after `GMINTRO_END`** (guarded by `needs_baseline[ply]`), which is the same frame gate `PermanentPatch_PerFrame` uses. By the time EnergyLink takes that baseline, rider stats already reflect the permanent patches — so permanent patches do not generate energy. (The direct-apply path in `Patch_GiveItem` additionally calls `EnergyLink_RebaseStats`; `PermanentPatch_DoApply` relies on the baseline snapshot instead.)

## All-Up Consolidation (Reducing Diff)

Instead of calling `Machine_GivePatch` 9 times (once per stat), `PermanentPatch_DoApply` finds the minimum accumulated count across all stats and calls `Machine_GiveAllUp` once with that value. Then it only calls `Machine_GivePatch` for the leftover per-stat differences.

**Example:** If permanent_patches = [5, 3, 5, 4, 3, 5, 5, 4, 3]:
- min = 3 → `Machine_GiveAllUp(md, 3)` (applies +3 to all 9 stats)
- Remainders: [2, 0, 2, 1, 0, 2, 2, 1, 0] → 5 individual `Machine_GivePatch` calls
- Total: 6 calls instead of 9

**Why this matters:** `Machine_GivePatch` calls `Machine_UpdateAppearance` and `Machine_AdjustAttributes` each time. Fewer calls = less redundant work at round start. The all-up path also correctly tracks the player's all-up collected count.

## Patch Cap Interaction

Permanent patches flow through the replaced `Machine_GivePatch` / `Machine_GiveAllUp` (via `PatchCap_GivePatch` / `PatchCap_GiveAllUp`), so they respect the current patch cap automatically.

**Edge case:** If the patch cap is lower than the permanent patch count (e.g., 5 permanent accel patches but cap is 3), the excess is silently clamped. This is correct — the player's *entitlement* is stored in save data, and if the cap is later raised, subsequent rounds will apply more. No data is lost.

**Edge case:** Stats from natural gameplay + permanent patches. If a player has 3 permanent accel patches and picks up 2 more accel patches during the round, they have 5 total. At next round start, they get 3 again from permanent patches. Natural gameplay patches are always transient — this is the intended design.

## Mode Considerations

- **City Trial (standard Trial mode, `CITYMODE_TRIAL`):** Permanent patches apply at round start on the main city map, gated by `ct_permanent_patches_enabled`. The stats carry over into the trial's stadium phase (same `MachineData`), so they are already present there.
- **City Trial (Stadium-only mode, `CITYMODE_STADIUM`):** This is a distinct menu selection (direct stadium entry). Permanent patches apply on stadium load, gated by `ct_stadium_permanent_patches_enabled`, selected via `Gm_GetCityMode()` (not `Gm_IsStadiumMode()`).
- **City Trial (Free Run, `CITYMODE_FREERUN`):** **Never applied.** `PermanentPatch_ShouldApply()` returns 0 unconditionally — Free Run does not load item-data tables, and inflated stats would crash `Item_GetItDataPtr` on damage-driven patch ejection. The toggles do not affect this.
- **Air Ride:** Permanent patches apply at every race start, gated by `ar_permanent_patches_enabled` (the fall-through for any non-`MJRKIND_CITY` major). The HUD stat bar does not display in Air Ride, so the boost is only felt in gameplay.
- **Top Ride:** Not reached. Top Ride loads via `OnTopRideLoad` (minor `MNRKIND_19`), so `On3DLoadEnd` — and therefore `PermanentPatch_On3DLoadEnd()` — never fires for it. It also has a separate 2D engine with no `MachineData` or stat system, so there is nothing for `Machine_GivePatch` to modify.

## Stadium Consideration

In the standard Trial mode, after the free-run period, players enter a stadium minigame. The machine and its stats carry over from the trial phase (the same `MachineData`), so the permanent patches applied at the city-map round start are already present — **no separate hook for the trial's stadium phase is needed.**

The Stadium-only menu mode (`CITYMODE_STADIUM`) is a different case: it is a direct stadium entry with freshly initialized stats, and it gets its own `On3DLoadEnd` application gated by the separate `ct_stadium_permanent_patches_enabled` toggle (see [Mode gating](#mode-gating)).
