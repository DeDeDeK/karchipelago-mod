# Permanent Patches

Permanent patches are stat boost items received from AP that persist across City Trial rounds and Air Ride races. Receiving one only writes save data; the accumulated totals are re-applied to every human player's machine at the start of every round or race.

**Files:** `mods/archipelago/src/patch_item.c` / `.h` (receive + apply), `main.h` (`APSave`), `settings_menu.c` / `.h` (per-mode toggles), `ap_item_handler.c` (receive routing), `main.c` (the `On3DLoadEnd` call).

## AP Items and Save Data

The nine per-stat items are `AP_PERM_PATCH_BASE + PatchKind`, i.e. IDs 100-108 in `PatchKind` order (`archipelago_api.h`). All-up is the standalone ID `AP_ITEM_PERM_PATCH_ALL_UP` (8), outside the 100-range block - not to be confused with `AP_ITEM_ALL_UP` (7), the one-shot in-round all-up.

Save state is a single `u8 permanent_patches[PATCHKIND_NUM]` in `APSave`: how many permanent +1 patches of each stat have been received. Incremented on receipt, guarded by `< PATCH_STAT_MAX` (127, the sign-extension ceiling of `Patch_GetMaxValue`), never decremented. There is no separate all-up counter - a received permanent all-up increments all nine entries, which makes the all-up consolidation at apply time purely an optimization rather than a data concern.

## Receiving

`PermanentPatch_GiveItem(kind)` and `PermanentPatch_GiveAllUp()` increment save data and enqueue a "Received: permanent +1 ..." textbox. No stat is touched at receive time.

Both sit above the 3D scene gate in `APItems_HandleItem` (`ap_item_handler.c`), so the increment always succeeds and the item is consumed from the queue immediately, whatever scene the player is in. The boost shows up the next time a gated scene loads.

### Why no immediate apply

Applying the +1 immediately would double-apply on the trial's stadium transition. Stats carry over from the city-driving phase into the stadium machine, but `On3DLoadEnd` fires again on stadium entry, so the round-start hook would re-apply every accumulated permanent patch on top of the carried-over stats. (Between rounds it balances out, since patches don't carry across rounds - the stadium case is the one that actually breaks.)

Deferring all application makes save data the single source of truth: the count determines the boost, and nothing applies a permanent patch outside the round-start path.

## Mode Gating

`PermanentPatch_ShouldApply()` dispatches on `Scene_GetCurrentMajor()` (0x8000aea8) and then `Gm_GetCityMode()` (0x8003f6cc):

| Scene major | City mode | Result |
|---|---|---|
| `MJRKIND_CITY` | `CITYMODE_FREERUN` | **Always skipped**, regardless of toggle |
| `MJRKIND_CITY` | `CITYMODE_STADIUM` | gated by `ct_stadium_permanent_patches_enabled` |
| `MJRKIND_CITY` | `CITYMODE_TRIAL` | gated by `ct_permanent_patches_enabled` |
| any other (Air Ride) | - | gated by `ar_permanent_patches_enabled` |

The three toggles live under *Archipelago Settings -> Permanent Patches* and all default **On**.

The dispatch cannot use `Gm_IsInCity()`: that helper is stage-based, true only on the CT main map (stage_kind 9/52), and so excludes the stadiums. The three City modes are distinct menu selections, not phases of one session - the trial's own stadium phase reuses the same `MachineData` and carries its stats over, so it needs no separate application, while `CITYMODE_STADIUM` is direct stadium entry with freshly initialized stats and gets its own.

**Free Run is never applied.** It does not load the item-data tables, so the inflated stats would crash `Item_GetItDataPtr` when the game tries to eject patches on damage.

**Top Ride is not reached at all.** It loads through `OnTopRideLoadEnd` (minor `MNRKIND_19`), so `On3DLoadEnd` never fires, and its 2D engine has no `MachineData` or stat system for `Machine_GivePatch` to modify.

Air Ride is applied at every race start. The HUD stat bar does not display there, so the boost is only felt in gameplay.

## Round-Start Application

`On3DLoadEnd()` in `main.c` calls `PermanentPatch_On3DLoadEnd()`, which checks the mode + menu gate, bails if nothing is accumulated (so an all-zero save never spawns a proc), resets the once-per-scene guard `permanent_patches_applied`, and creates a bare per-frame GObj via `GOBJ_EZCreator` running `PermanentPatch_PerFrame`.

The proc waits for `Gm_GetIntroState() == GMINTRO_END` (0x8000a958) - machines are fully initialized only then - sets the guard, and calls `PermanentPatch_DoApply()`. It does not delete itself; it keeps running and short-circuits on the guard, so `DoApply` runs exactly once per scene load.

`PermanentPatch_DoApply()` computes the minimum across all nine counts, then for each human player (`Ply_GetPKind` == `PKIND_HMN`) with a machine GObj it calls `Machine_GiveAllUp(md, min)` once when `min > 0`, followed by `Machine_GivePatch(md, i, count[i] - min)` for each stat with a remainder. For `[5, 3, 5, 4, 3, 5, 5, 4, 3]` that is one all-up plus five patch calls instead of nine patch calls. The consolidation matters because each `Machine_GivePatch` runs `Machine_UpdateAppearance` and `Machine_AdjustAttributes`; it also credits the player's all-up collected counter, which the per-stat path does not.

### Interaction with EnergyLink

EnergyLink tracks deltas from `RiderData` stats (rider stats are synced from machine stats by the normal update loop) and does not depend on hook ordering inside `On3DLoadEnd`. Its per-frame proc snapshots a per-player baseline on the first frame after `GMINTRO_END` - the same frame gate `PermanentPatch_PerFrame` uses - by which time rider stats already reflect the permanent patches, so they generate no energy. The direct-apply path in `Patch_GiveItem` calls `EnergyLink_RebaseStats` explicitly for the same reason; `PermanentPatch_DoApply` relies on the baseline snapshot instead.

### Interaction with the patch cap

`Machine_GivePatch` (0x801cacf4) and `Machine_GiveAllUp` (0x801cad40) are `CODEPATCH_REPLACEFUNC`'d in `patch_cap.c`, so permanent patches are clamped to the current per-stat patch cap like any other stat gain.

If the cap is below the accumulated count (5 permanent accel patches against a cap of 3), the excess is silently dropped for that round. No data is lost - the entitlement lives in save data, so raising the cap later makes subsequent rounds apply more.

Stats from natural gameplay stack on top and are always transient: 3 permanent accel patches plus 2 picked up in-round is 5 for that round, and the next round starts from 3 again.
