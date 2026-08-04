# Patch Cap

Patch cap turns vanilla City Trial's fixed stat ceiling (18) into a configurable, optionally progressive per-stat cap. A slot picks a **min/max pair**: the cap starts at `city_trial_patch_cap_min` and grows one step per `AP_ITEM_PATCH_CAP_INCREASE` received, up to `city_trial_patch_cap_max`.

**Files:** `patch_cap.c` / `patch_cap.h`, `main.h` (save + options + `PATCH_STAT_MAX`), `ap_item_handler.c` (dispatch), `main.c` (boot hook), `mods/archipelago_debug/src/debug_menu.c` (test action).

## Slot Options

| Option | Range | Default | Meaning |
|--------|-------|---------|---------|
| `city_trial_patch_cap_min` | 1-18 | 18 | Cap the player starts at. `0` (options not yet received) falls back to the max. |
| `city_trial_patch_cap_max` | 18-30 | 18 | Cap ceiling, the `GOAL_MAX_STATS_CT` threshold, and the value `Patch_GetMaxValue` reports for HUD/attribute normalization. `0`/unset is treated as `PATCH_STAT_MAX` (127); anything above 127 is clamped to it. |

There is no separate "progressive" toggle: `min == max` is a flat cap with no Patch Cap Increase items in the pool, and `min < max` is progressive. The AP world ships exactly `max - min` of them. Both options are read at every clamp; nothing is precomputed at connect.

The defaults (`18`/`18`) reproduce vanilla exactly: a flat cap of 18 patches per stat and no cap items. The mod clamps to `PATCH_STAT_MAX` (127) rather than to the AP world's 30, so a hand-edited or malformed option can go higher without breaking the hardware limit.

Both options are measured in **patches**, not raw stat value. CT stats spawn at `-2` (HP at `0`), so the raw ceiling is `start + cap` per stat (HP `cap`, every other stat `cap - 2`) and all nine hold the same number of patches. `PatchCap_GetStatStart` is the single source of that baseline, shared by the clamp and the Max Stats goal.

## Entry Points

| Symbol | Kind | Where |
|--------|------|-------|
| `PatchCap_OnBoot()` | public, called from `OnBoot()` in `main.c` | installs the three replacement hooks |
| `PatchCap_Increment()` | public, called from `ap_item_handler.c` | bumps `patch_cap_count`, logs + textbox |
| `PatchCap_GetMax()` | static | reads `city_trial_patch_cap_max`, clamps to `PATCH_STAT_MAX` |
| `PatchCap_GetCap()` | static | current effective cap (`min + count`, clamped to the max) |
| `PatchCap_GetStatStart(int kind)` | public, declared in `patch_cap.h` | per-stat spawn baseline (`0` for HP, `-2` otherwise); shared with the Max Stats goal |
| `PatchCap_ClampDelta(int kind, float current, int delta)` | static | clamp a positive delta to `(start + cap) - current` |
| `PatchCap_GivePatch` / `PatchCap_GiveAllUp` / `PatchCap_GetMaxValue` | replacement functions | bodies of the three hooks |

`PatchCap_OnBoot`, `PatchCap_Increment`, and `PatchCap_GetStatStart` are declared in `patch_cap.h`; the rest are file-local (the three replacement functions are non-`static` only so `CODEPATCH_REPLACEFUNC` can take their address).

## Save Data & Options

```c
// APSave (main.h)
u8 patch_cap_count;    // Number of Patch Cap Increase items received

// APSlotOptions (main.h)
u32 city_trial_patch_cap_min;   // 1-127, starting cap
u32 city_trial_patch_cap_max;   // 1-127, ceiling

// main.h
#define PATCH_STAT_MAX 127  // absolute hardware ceiling
```

Cap computation (read at every clamp, no caching):

```c
max = city_trial_patch_cap_max;
if (max <= 0 || max > PATCH_STAT_MAX) max = PATCH_STAT_MAX;   // 0/unset or overflow -> 127

min = city_trial_patch_cap_min;
cap = (min == 0) ? max                          // options not received yet -> uncapped to the max
                 : min(min + patch_cap_count, max);
```

The growth step is `+1` per item and the start is the slot's `min`, so a slot reaches its ceiling after exactly `max - min` Patch Cap Increase items - which is how many the AP world puts in the pool.

## Hooks

`PatchCap_OnBoot()` installs three `CODEPATCH_REPLACEFUNC` hooks:

| Replaced | Address | Replacement | Purpose |
|----------|---------|-------------|---------|
| `Patch_GetMaxValue` | 0x8000aaf0 | `PatchCap_GetMaxValue` | Returns `PatchCap_GetMax()` - sets the HUD/attribute normalization range |
| `Machine_GivePatch` | 0x801cacf4 | `PatchCap_GivePatch` | Pre-clamp delta to current cap, apply, update appearance + attributes |
| `Machine_GiveAllUp` | 0x801cad40 | `PatchCap_GiveAllUp` | Per-stat pre-clamp, apply, credit `Ply_SetAllUpCollected`, update |

`Machine_GivePatchOrCandy` (0x801cb1c0) calls `Machine_GivePatch`, so it is covered transitively by the hook.

### `PatchCap_GetMaxValue` returns the max, not the current cap

This is intentional and load-bearing. `Patch_GetMaxValue` is the denominator the game uses to *normalize* stats for the HUD bars and the per-vehicle attribute interpolation curve. Returning the per-slot **max** keeps that curve scaled to the full reachable range regardless of how far the cap has grown from `min`. The actual "you can't go higher right now" enforcement is done separately by `PatchCap_ClampDelta` against `PatchCap_GetCap()` (the *current* effective cap), so returning the max here does not let a stat grow past the current cap.

Because the hook returns the max rather than `PATCH_STAT_MAX` (127), this is **not** behavior-neutral: a slot with a max below 127 sees its HUD bars and attribute curve normalized to that lower value.

### Delta clamping

`PatchCap_ClampDelta(int kind, float current, int delta)`:

- `delta <= 0` -> pass through unchanged. Stat-down patches, the drop-patches trap, and other reductions are never affected by the cap.
- `delta > 0` -> `room = (PatchCap_GetStatStart(kind) + cap) - current`; if `room <= 0` return 0; else clamp to `(int)room`. If the stat already holds `cap` patches the delta becomes 0 (no-op).

The `start` offset (`-2`, or `0` for HP) is what makes the cap count **patches** rather than raw value: each stat tops out at raw `start + cap`, i.e. exactly `cap` patches, regardless of where it spawned. Without it the eight non-HP stats (spawn `-2`) would hold `cap + 2` patches while HP (spawn `0`) holds `cap`.

After pre-clamping, the replacements call `Machine_ApplyStatClamped` (0x801e094c), which tail-calls `Stat_AddClamped` (0x80194d80) to do its own secondary clamp to `[Patch_GetMinValue, Patch_GetMaxValue]`. Since our per-stat raw ceiling is `start + cap <= max` and the secondary clamp's upper bound is `max`, the second clamp is a no-op.

Note this means a non-HP stat tops out at raw `cap - 2`, two below the HUD/attribute normalization range (`Patch_GetMaxValue` = max), so a fully-capped non-HP stat bar reads slightly under full while HP reads full. The shortfall is fixed at 2 raw units, so it's only conspicuous at very low caps.

### Appearance and attribute refresh

Both replacements finish by refreshing visuals and (conditionally) attributes:

```c
Machine_UpdateAppearance(md);
if (!md->suppress_attr_recalc)    // 0xc3b bit 0x80; replicates the vanilla gate
    Machine_AdjustAttributes(md);
```

`suppress_attr_recalc` is the sign bit of the per-vehicle model/variant flag byte at `MachineData+0xc3b`, set by the vehicle's model-setup callback (`vcDataCommon+0x18`) at spawn. It is only set for the special transformation star variants - Wing Kirby (`VCKIND_WINGKIRBY`, kind 0x11) and Compact Star (`VCKIND_COMPACT`, kind 0x1) - whose derived attributes are fixed rather than patch-driven, so vanilla `Machine_GivePatch` / `Machine_GiveAllUp` skip `Machine_AdjustAttributes` for them. The replacements preserve that gate exactly.

`PatchCap_GiveAllUp` loops `PATCHKIND_NUM` (9) stats, pre-clamping each individually, then credits the player's all-up counter - but only when the machine is occupied:

```c
ply = (md->rider_gobj == 0) ? 5 : RiderGObj_GetPly(md->rider_gobj);
if (ply != 5) {                                  // 5 == no rider; skip
    Ply_SetAllUpCollected(ply, num + Ply_GetAllUpCollected(ply));
}
```

### All-up tracking

`PatchCap_GiveAllUp` credits `Ply_SetAllUpCollected(ply, num + collected)` using the **original** `num`, not the per-stat clamped value. This matches vanilla - the counter tracks "all-ups picked up", not "effective stat gain". If any checklist check keys off that counter, a capped all-up that produced no stat change still counts.

## Increment Flow

`AP_ITEM_PATCH_CAP_INCREASE` is routed in `APItems_HandleItem` (`ap_item_handler.c`), above the 3D scene gate, so it applies in any scene:

```c
case AP_ITEM_PATCH_CAP_INCREASE:
    PatchCap_Increment();
    return 1;
```

`PatchCap_Increment()`:

1. `ap_save->patch_cap_count++`
2. `OSReport("[PatchCap] Patch cap increased to %d (max %d).\n", cap, max)`
3. Textbox via `tb_api->EnqueueColoredNounFmt(NULL, "Patch cap", tb_api->PatchColors[PATCHKIND_CHARGE], " increased! (%d/%d)", cap, max)` - yellow "Patch cap" noun, denominator is the **max** (not 18, not 127).

## Consumer Coverage

Every consumer of the stat cap goes through `Patch_GetMaxValue`. No code path reads `gmGameParams.patch_max` (+0x18) directly.

**Callers of `Patch_GetMaxValue` (0x8000aaf0) - 16 call sites across 7 functions:**

| Function | Address | Sites |
|----------|---------|-------|
| `Machine_UpdateAppearance` | 0x801d6668 | 2 |
| `Machine_SetStatBlockClamped` | 0x80194f64 | 1 |
| `Stat_AddClamped` (tail-called by `Machine_ApplyStatClamped` 0x801e094c) | 0x80194d80 | 1 |
| `Stat_AddClampedAll` (tail-called by `Machine_ApplyAllStatsClamped` 0x801e096c) | 0x80194e60 | 1 |
| `PlayerView_Think?` (HUD stat-bar denominator) | 0x80116d8c | 9 (one per stat) |
| `Machine_GetStatRatio` (per-stat attribute normalizer) | 0x801caa8c | 1 |
| `Machine_GetStatRatio2` (second normalizer, sibling) | 0x801cabd4 | 1 |

The 9 `PlayerView_Think?` sites correspond to `PATCHKIND_NUM` (9 stats); the bar fill ratio uses this as denominator.

**Direct readers of `gmGameParams.patch_max`:** None. The struct is reached only via `Gm_Get_gmDataAll()` (0x8000fcb0); across its callers nobody dereferences `+0x18` independently. `Patch_GetMaxValue` (+0x18) and `Patch_GetMinValue` (0x8000ab1c, +0x1c) are the sole readers of those two bytes.

**Per-vehicle attribute interpolation:** `Machine_AdjustAttributes` (0x801c7278) dispatches two callbacks per machine kind via `(&vcDataCommon_table)[vc_kind]->+0x1c/+0x20`.

| Machine | +0x1c callback | +0x20 callback |
|---------|----------------|----------------|
| Warp Star (VCKIND 0, `vcDataCommon @ 0x804b1658`) | `Machine_CopyCommonAttributes?` (0x801e812c) - attribute memcpy, does not touch `patch_max` | `Machine_AdjustAttributesStar` (0x801e906c) -> `Machine_ApplyStarStatScaling` (0x801e81e4) -> `Machine_GetStatRatio` + `Machine_ScaleFromRatio` (0x801cab4c) - **routes through `Patch_GetMaxValue`** |
| Rex Wheelie (VCKIND 1, `vcDataCommon @ 0x804b1c40`) | `RexWheelie_InitAttr` (0x801f3c94) - attribute memcpy | `Machine_AdjustAttributesBike` (0x801f4dac) -> `Machine_ApplyBikeStatScaling` (0x801f3d44) -> `Machine_GetStatRatio` + `Machine_GetStatRatio2` - **routes through `Patch_GetMaxValue`** |

The shared normalizers `Machine_GetStatRatio` (0x801caa8c) and `Machine_GetStatRatio2` (0x801cabd4) both unconditionally `bl 0x8000aaf0`. There is no direct-read escape hatch, so the `CODEPATCH_REPLACEFUNC` on `Patch_GetMaxValue` is a complete interception: the returned max scales the entire attribute-interpolation curve and the HUD stat-bar fill ratio.

## Hardware Ceiling (`PATCH_STAT_MAX`)

`Patch_GetMaxValue` returns via `extsb` (sign-extend low byte) at `0x8000ab08`. So **127 is the firm hardware ceiling**: values 128-255 sign-flip negative and collapse the effective cap to the floor. `PATCH_STAT_MAX` is set to that ceiling (127), and `PatchCap_GetMax()` clamps the option to it, so a malformed YAML value can never blow past the limit.

Secondary considerations if you ever touch this:

- `ap_save->permanent_patches[kind]` is `u8` with `< PATCH_STAT_MAX` gates in `patch_item.c`. At 127 these stay well within `u8`.
- The APWorld must ship enough `AP_ITEM_PATCH_CAP_INCREASE` items for a slot to reach its ceiling: exactly `max - min` increments are useful, and it ships that many.
- HUD stat-bar segment dividers (if drawn as discrete ticks rather than continuous fill) are **not** on the `Patch_GetMaxValue` path. The bar *fill ratio* scales correctly (denominator goes through our hook), but visual ticks may still render as 18 segments.
- `GOAL_MAX_STATS_CT` measures the number of *patches collected* on each stat against `city_trial_patch_cap_max`, in `goal_max_stats_ct.c` - **not** against `PATCH_STAT_MAX`, and **not** against the current effective cap. The goal is "collect the slot's ceiling worth of patches on every stat in one CT run", so a progressive slot must first receive every Patch Cap Increase item to make it reachable. CT stats spawn at `-2` (HP at `0`), so the per-stat test is `value >= start + max` (HP needs raw `max`, every other stat raw `max - 2`); comparing raw `value >= max` for all would silently make the eight non-HP stats require two extra patches each.

## Known Limitations

### Option name vs. scope

Both options are named `city_trial_*`, but the hook is mode-agnostic. Every `Machine_GivePatch` / `Machine_GiveAllUp` call is clamped, including Air Ride sessions when `ar_permanent_patches_enabled` re-applies accumulated permanent patches at race start. If the cap is 5, AR perm-patch application also caps at 5.

Currently benign (AR gameplay doesn't normally raise stats beyond perm-patches, and the clamped values are still applied), but the naming implies CT-only enforcement that we don't actually enforce. Decision: leave as-is (global) and accept the slightly aspirational names.

### Saturation textbox

Once `min + patch_cap_count >= max`, further `PatchCap_Increment()` calls still enqueue `"Patch cap increased! (max/max)"` and `OSReport` still says "increased to <max>", even though the cap didn't move. Cosmetic only; the clamp produces correct numbers.

### `patch_cap_count` overflow

`u8 patch_cap_count` wraps at 256. Only `max - min` increments are ever useful; an APWorld shipping 256+ cap items would wrap the counter. Because `PatchCap_GetCap()` clamps `min + count` to the max anyway, a wrap only matters if the count exceeds 255 - not reachable with the AP world's 18-30 max range, but a `patch_cap_count < max` guard in `Increment` would make it unconditionally safe.

### All-up credit on capped pickups

`Ply_SetAllUpCollected` is credited with the uncapped `num`. Matches vanilla behavior. If any clear_kind in `checklist-mappings.csv` keys off "N all-ups collected", a capped all-up still counts.
