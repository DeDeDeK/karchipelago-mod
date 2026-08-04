# Permanent Patches

Permanent patches are stat boost items received from AP that persist across both City Trial rounds and Air Ride races. The game tracks how many of each patch type the player has accumulated and re-applies them at the start of every round/race; receiving one only writes save data, never a live stat.

**Files:** `patch_item.c` / `patch_item.h` (receive + apply), `main.h` (`APSave` save data), `settings_menu.h` / `settings_menu.c` (per-mode toggles), `ap_item_handler.c` (receive routing)

## Mode Gating

Application is gated per-mode by three menu toggles under *Archipelago Settings -> Permanent Patches* (all default **On**, declared in `settings_menu.h`):

- **City Trial** (the standard Trial mode, `CITYMODE_TRIAL`) - `ap_menu_settings.ct_permanent_patches_enabled`
- **CT Stadium** (`CITYMODE_STADIUM`) - `ap_menu_settings.ct_stadium_permanent_patches_enabled`
- **Air Ride** (and any other non-City 3D major) - `ap_menu_settings.ar_permanent_patches_enabled`

The three City modes are distinct menu selections, not phases of one session. `PermanentPatch_ShouldApply()` dispatches on `Scene_GetCurrentMajor() == MJRKIND_CITY` and then `Gm_GetCityMode()` to pick the right toggle:

| Scene major | `Gm_GetCityMode()` | Result |
|---|---|---|
| `MJRKIND_CITY` | `CITYMODE_FREERUN` (2) | **Always skipped** (item data not loaded; logs and returns 0) |
| `MJRKIND_CITY` | `CITYMODE_STADIUM` (1) | gated by `ct_stadium_permanent_patches_enabled` |
| `MJRKIND_CITY` | `CITYMODE_TRIAL` (0) | gated by `ct_permanent_patches_enabled` |
| any other (Air Ride) | - | gated by `ar_permanent_patches_enabled` |

The dispatch cannot use `Gm_IsInCity()`: that helper is stage-based (true only on the CT main map, stage_kind 9/52) and so excludes stadiums.

**City Trial Free Run (`CITYMODE_FREERUN`) is never applied** - `PermanentPatch_ShouldApply()` returns 0 for it unconditionally, regardless of any toggle. Free Run does not load the item-data tables, so the inflated stats from permanent patches would crash `Item_GetItDataPtr` when the game tries to eject patches on damage.

Top Ride is not reached at all: it loads through `OnTopRideLoadEnd` (minor `MNRKIND_19`), so `On3DLoadEnd` never fires for it. It also has a separate 2D engine with no `MachineData` or stat system, so there is nothing for `Machine_GivePatch` to modify.

## AP Items

Per-stat items are `AP_PERM_PATCH_BASE + PatchKind` (`100`-`108`, since `PATCHKIND_NUM == 9`). `PatchKind`, its matching `+1` `ItemKind`, and the AP item ID line up 1:1:

| PatchKind (value) | ItemKind | Per-stat AP item ID |
|---|---|---|
| `PATCHKIND_WEIGHT` (0)   | `ITKIND_WEIGHT` (17)   | `AP_PERM_PATCH_WEIGHT` (100) |
| `PATCHKIND_ACCEL` (1)    | `ITKIND_ACCEL` (3)     | `AP_PERM_PATCH_BOOST` (101) |
| `PATCHKIND_TOPSPEED` (2) | `ITKIND_TOPSPEED` (5)  | `AP_PERM_PATCH_TOPSPEED` (102) |
| `PATCHKIND_TURN` (3)     | `ITKIND_TURN` (11)     | `AP_PERM_PATCH_TURN` (103) |
| `PATCHKIND_CHARGE` (4)   | `ITKIND_CHARGE` (15)   | `AP_PERM_PATCH_CHARGE` (104) |
| `PATCHKIND_GLIDE` (5)    | `ITKIND_GLIDE` (13)    | `AP_PERM_PATCH_GLIDE` (105) |
| `PATCHKIND_OFFENSE` (6)  | `ITKIND_OFFENSE` (7)   | `AP_PERM_PATCH_OFFENSE` (106) |
| `PATCHKIND_DEFENSE` (7)  | `ITKIND_DEFENSE` (9)   | `AP_PERM_PATCH_DEFENSE` (107) |
| `PATCHKIND_HP` (8)       | `ITKIND_HP` (19)       | `AP_PERM_PATCH_HP` (108) |

All-up is the separate standalone ID `AP_ITEM_PERM_PATCH_ALL_UP` (**8**), not part of the 100-range block. Do not confuse it with `AP_ITEM_ALL_UP` (7), the one-shot in-round all-up.

## Save Data

`APSave` (in `main.h`, accessed via the global `ap_save`) contains:

```c
u8 permanent_patches[PATCHKIND_NUM];    // Accumulated permanent patch count per stat (0-PATCH_STAT_MAX)
```

9 bytes total (`PATCHKIND_NUM == 9`). Each entry tracks how many permanent +1 patches of that PatchKind have been received from AP. Incremented when the item is received, guarded by `< PATCH_STAT_MAX` (127, the hardware sign-extension ceiling of `Patch_GetMaxValue`), never decremented.

No separate `permanent_allup_count` field is needed - when an all-up is received, increment all 9 entries by 1. This simplifies storage and makes the all-up consolidation at application time purely an optimization, not a data concern.

## Receiving

`PermanentPatch_GiveItem(kind)` and `PermanentPatch_GiveAllUp()` in `patch_item.c` only increment save data - `ap_save->permanent_patches[kind]++` (or all 9 for all-up), each guarded at `PATCH_STAT_MAX`. They also enqueue a "Received: permanent +1 ..." textbox notification. No stat is applied at receive time.

Permanent patch handling in `APItems_HandleItem` (`ap_item_handler.c`) sits above the scene gate. The save data increment always succeeds and returns 1 immediately - the item is consumed from the queue. The round-start hook applies the accumulated counts the next time a gated CT-Trial / CT-Stadium / Air Ride scene loads (never in Free Run).

### Why no immediate apply?

Applying the +1 immediately (via `Patch_GiveItem` / `Patch_AllUp_GiveItem`) would double-apply in two places:

- **Stadium load after CT pickup.** Stats carry over from a Trial's city-driving phase into the stadium machine, but `On3DLoadEnd` fires again on stadium entry. The round-start hook would then re-apply every accumulated permanent patch on top of the carried-over stats. (Here "stadium" refers to the trial's stadium phase, not the distinct `CITYMODE_STADIUM` menu mode.)
- **Subsequent rounds.** The first round after receiving a patch would already have +1 from the immediate apply. The next round's `On3DLoadEnd` would apply the count from save data on top - but since patches don't carry across rounds, this case balances out. The stadium case is the one that actually breaks.

Deferring all application to `PermanentPatch_DoApply` makes save data the single source of truth: the count is what determines the boost, and nothing applies a patch outside the round-start path.

## Round-Start Application

`On3DLoadEnd()` in `main.c` calls `PermanentPatch_On3DLoadEnd()`. That function checks the mode + menu gate (`PermanentPatch_ShouldApply()`), bails early if nothing is accumulated (so a save with an all-zero array never spawns a proc), resets the once-per-scene guard `permanent_patches_applied`, then creates a bare per-frame GObj via `GOBJ_EZCreator` running `PermanentPatch_PerFrame`.

The proc waits until `Gm_GetIntroState() == GMINTRO_END` (machines are fully initialized only then), sets the guard, and calls `PermanentPatch_DoApply()` once. It does **not** delete itself once it fires - it keeps running but short-circuits on the guard, so `DoApply` runs exactly once per scene load.

`PermanentPatch_DoApply()` (`static`) computes the minimum across all 9 stat counts, then for each human player (`Ply_GetPKind(p) == PKIND_HMN`) with a machine GObj (`Ply_GetMachineGObj`) it calls `Machine_GiveAllUp(md, min)` once when `min > 0`, followed by `Machine_GivePatch(md, i, count[i] - min)` for each stat with a remainder. An `OSReport` logs `min`, the total, and the per-stat counts.

### Interaction with EnergyLink

EnergyLink tracks deltas from `RiderData` stats (not `MachineData`), and rider stats are synced from machine stats by the game's normal update loop. It does not rely on hook ordering within `On3DLoadEnd`: its per-frame proc snapshots a per-player baseline (`prev_stats`) on the **first frame after `GMINTRO_END`** (guarded by `needs_baseline[ply]`), which is the same frame gate `PermanentPatch_PerFrame` uses. By the time EnergyLink takes that baseline, rider stats already reflect the permanent patches - so permanent patches do not generate energy.

The direct-apply path in `Patch_GiveItem` additionally calls `EnergyLink_RebaseStats`; `PermanentPatch_DoApply` relies on the baseline snapshot instead.

## All-Up Consolidation

Instead of calling `Machine_GivePatch` 9 times (once per stat), `PermanentPatch_DoApply` finds the minimum accumulated count across all stats and calls `Machine_GiveAllUp` once with that value. Then it only calls `Machine_GivePatch` for the leftover per-stat differences.

**Example:** If `permanent_patches` = [5, 3, 5, 4, 3, 5, 5, 4, 3]:
- min = 3 -> `Machine_GiveAllUp(md, 3)` (applies +3 to all 9 stats)
- Remainders: [2, 0, 2, 1, 0, 2, 2, 1, 0] -> 5 individual `Machine_GivePatch` calls
- Total: 6 calls instead of 9

**Why this matters:** `Machine_GivePatch` calls `Machine_UpdateAppearance` and `Machine_AdjustAttributes` each time. Fewer calls = less redundant work at round start. The all-up path also correctly tracks the player's all-up collected count.

## Patch Cap Interaction

`Machine_GivePatch` / `Machine_GiveAllUp` are `CODEPATCH_REPLACEFUNC`'d by the patch cap system in `patch_cap.c` (to `PatchCap_GivePatch` / `PatchCap_GiveAllUp`), so permanent patches respect the current patch cap automatically.

**Edge case:** If the patch cap is lower than the permanent patch count (e.g. 5 permanent accel patches but cap 3), the excess is silently clamped. This is correct - the player's *entitlement* is stored in save data, and if the cap is later raised, subsequent rounds will apply more. No data is lost.

**Edge case:** Stats from natural gameplay stack on top. If a player has 3 permanent accel patches and picks up 2 more during the round, they have 5 total; at next round start they get 3 again. Natural gameplay patches are always transient - this is the intended design.

## Mode Coverage

- **City Trial (standard Trial mode, `CITYMODE_TRIAL`):** Applied at round start on the main city map, gated by `ct_permanent_patches_enabled`. After the free-run period players enter a stadium minigame, and the machine and its stats carry over from the trial phase (the same `MachineData`), so the patches are already present there - **no separate hook for the trial's stadium phase is needed.**
- **City Trial (Stadium-only mode, `CITYMODE_STADIUM`):** A distinct menu selection (direct stadium entry) with freshly initialized stats, so it gets its own `On3DLoadEnd` application gated by `ct_stadium_permanent_patches_enabled`.
- **City Trial (Free Run, `CITYMODE_FREERUN`):** **Never applied**, regardless of toggle.
- **Air Ride:** Applied at every race start, gated by `ar_permanent_patches_enabled` (the fall-through for any non-`MJRKIND_CITY` major). The HUD stat bar does not display in Air Ride, so the boost is only felt in gameplay.
- **Top Ride:** Not reached - `On3DLoadEnd` never fires for it.

## Key Functions

| Function | Address | Role |
|---|---|---|
| `Machine_GivePatch(md, kind, num)` | `0x801cacf4` | Apply +num to one stat (replaced by `PatchCap_GivePatch`) |
| `Machine_GiveAllUp(md, num)`       | `0x801cad40` | Apply +num to all 9 stats (replaced by `PatchCap_GiveAllUp`) |
| `Scene_GetCurrentMajor()`          | `0x8000aea8` | Major scene kind (`MJRKIND_CITY` vs other) |
| `Gm_GetCityMode()`                 | `0x8003f6cc` | Selected City mode (Trial/Stadium/Free Run) |
| `Gm_GetIntroState()`               | `0x8000a958` | Intro/countdown state; `GMINTRO_END` == fully started |
| `Ply_GetPKind(p)`                  | `0x8022c858` | Player kind; `PKIND_HMN` filters human players |
| `Ply_GetMachineGObj(p)`            | `0x8022d230` | Machine GObj whose `userdata` is `MachineData` |
