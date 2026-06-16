# Patch Cap

## Overview

Patch cap turns vanilla City Trial's fixed stat ceiling (18) into a configurable / progressive cap. There are two layers:

- **Target cap** — the per-slot ceiling chosen by the YAML option `city_trial_patch_cap_amount` (1–127). This is the number of **patches** a stat can hold for that slot, and also the threshold the Max Stats goal (`GOAL_MAX_STATS_CT`) measures against. Because CT stats spawn at `-2` (HP at `0`), the cap is measured in patches, not raw value: the raw ceiling is `start + cap` per stat (HP `cap`, every other stat `cap - 2`), so all nine hold the same number of patches. See `PatchCap_GetStatStart` — the single source of that baseline, shared by the clamp and the goal.
- **Progressive cap** — when `city_trial_progressive_patch_caps` is on, the *effective* cap starts at `PATCH_CAP_PROGRESSIVE_START` (1) and grows by 1 each time an `AP_ITEM_PATCH_CAP_INCREASE` is received, clamped up to the target.

Both options are read at runtime; nothing is precomputed at connect.

| Option | Range | Meaning |
|--------|-------|---------|
| `city_trial_progressive_patch_caps` | 0/1 | If 1, the effective cap starts at 1 and grows with Patch Cap Increase items. If 0, the cap is fixed at the target. |
| `city_trial_patch_cap_amount` | 1–127 | Target (ceiling) cap. `0`/unset is treated as `PATCH_STAT_MAX` (127). |

When progressive is **off**, `PatchCap_GetCap()` returns the target directly (a fixed cap, e.g. 10 if the slot chose 10). With an unset target this is 127, i.e. effectively uncapped within the hardware limit — *not* the vanilla 18.

**Files:** `patch_cap.c` / `patch_cap.h`, `main.h` (save + options + `PATCH_STAT_MAX`), `ap_item_handler.c` (dispatch), `main.c` (boot hook), `mods/archipelago_debug/src/debug_menu.c` (test action).

## Entry points

| Symbol | Kind | Where |
|--------|------|-------|
| `PatchCap_OnBoot()` | public, called from `OnBoot()` (`main.c:95`) | installs the three replacement hooks |
| `PatchCap_Increment()` | public, called from `ap_item_handler.c:142` | bumps `patch_cap_count`, logs + textbox |
| `PatchCap_GetTarget()` | static | reads `city_trial_patch_cap_amount`, clamps to `PATCH_STAT_MAX` |
| `PatchCap_GetCap()` | static | current effective cap (target, or `1 + count` clamped to target) |
| `PatchCap_GetStatStart(int kind)` | public, declared in `patch_cap.h` | per-stat spawn baseline (`0` for HP, `-2` otherwise); shared with the Max Stats goal |
| `PatchCap_ClampDelta(int kind, float current, int delta)` | static | clamp a positive delta to `(start + cap) - current` |
| `PatchCap_GivePatch` / `PatchCap_GiveAllUp` / `PatchCap_GetMaxValue` | replacement functions | bodies of the three hooks |

`PatchCap_OnBoot`, `PatchCap_Increment`, and `PatchCap_GetStatStart` are declared in `patch_cap.h`; the rest are file-local (the three replacement functions are non-`static` only so `CODEPATCH_REPLACEFUNC` can take their address).

## Save Data & Options

```c
// APSave (main.h)
u8 patch_cap_count;    // Number of Patch Cap Increase items received

// APSlotOptions (main.h)
u32 city_trial_progressive_patch_caps; // 0/1
u32 city_trial_patch_cap_amount;       // 1-127 target

// main.h
#define PATCH_STAT_MAX 127  // absolute hardware ceiling (see "Hardware ceiling")
```

Cap computation (read at every clamp, no caching):

```c
target = clamp(city_trial_patch_cap_amount, 1..PATCH_STAT_MAX)   // 0/unset -> 127
cap    = progressive ? min(PATCH_CAP_PROGRESSIVE_START + patch_cap_count, target)
                     : target
```

`PATCH_CAP_PROGRESSIVE_START` is `1` (defined at top of `patch_cap.c`). Note the starting value is this fixed constant, **not** `city_trial_patch_cap_amount` — the option is the *target*, not the start.

## Hooks

`PatchCap_OnBoot()` installs three `CODEPATCH_REPLACEFUNC` hooks:

| Replaced | Address | Replacement | Purpose |
|----------|---------|-------------|---------|
| `Patch_GetMaxValue` | 0x8000aaf0 | `PatchCap_GetMaxValue` | Returns `PatchCap_GetTarget()` — sets the HUD/attribute normalization range |
| `Machine_GivePatch` | 0x801cacf4 | `PatchCap_GivePatch` | Pre-clamp delta to current cap, apply, update appearance + attributes |
| `Machine_GiveAllUp` | 0x801cad40 | `PatchCap_GiveAllUp` | Per-stat pre-clamp, apply, credit `Ply_SetAllUpCollected`, update |

`Machine_GivePatchOrCandy` (0x801cb1c0) calls `Machine_GivePatch`, so it is covered transitively by the hook.

### `PatchCap_GetMaxValue` returns the **target**, not the current cap

This is intentional and load-bearing. `Patch_GetMaxValue` is the denominator the game uses to *normalize* stats for the HUD bars and the per-vehicle attribute interpolation curve (see Consumer Coverage). Returning the per-slot **target** keeps that curve scaled to the full reachable range regardless of how far a progressive cap has grown. The actual "you can't go higher right now" enforcement is done separately by `PatchCap_ClampDelta` against `PatchCap_GetCap()` (the *current* effective cap), so returning the target here does not let a stat grow past the current cap.

> Because the hook returns the target rather than `PATCH_STAT_MAX` (127), this is **not** behavior-neutral: a slot with a target below 127 sees its HUD bars and attribute curve normalized to that lower target.

### Delta Clamping

`PatchCap_ClampDelta(int kind, float current, int delta)`:

- `delta <= 0` → pass through unchanged. Stat-down patches, the drop-patches trap, and other reductions are never affected by the cap.
- `delta > 0` → `room = (PatchCap_GetStatStart(kind) + cap) - current`; if `room <= 0` return 0; else clamp to `(int)room`. If the stat already holds `cap` patches the delta becomes 0 (no-op).

The `start` offset (`-2`, or `0` for HP) is what makes the cap count **patches** rather than raw value: each stat tops out at raw `start + cap`, i.e. exactly `cap` patches, regardless of where it spawned. Without it the eight non-HP stats (spawn `-2`) would hold `cap + 2` patches while HP (spawn `0`) holds `cap`.

After pre-clamping, the replacements call `Machine_ApplyStatClamped` (0x801e094c), which does its own secondary clamp to `[Patch_GetMinValue, Patch_GetMaxValue]`. Since our per-stat raw ceiling is `start + cap ≤ target` and the secondary clamp's upper bound is `target`, the second clamp is a no-op.

Note this means a non-HP stat tops out at raw `cap - 2`, two below the HUD/attribute normalization range (`Patch_GetMaxValue` = target), so a fully-capped non-HP stat bar reads slightly under full while HP reads full. The shortfall is fixed at 2 raw units, so it's only conspicuous at very low cap targets.

### `PatchCap_GivePatch` / `PatchCap_GiveAllUp` details

Both replacements finish by refreshing visuals and (conditionally) attributes:

```c
Machine_UpdateAppearance(md);
if (!md->suppress_attr_recalc)    // 0xc3b bit 0x80; replicates the vanilla gate
    Machine_AdjustAttributes(md);
```

`suppress_attr_recalc` is the sign bit of the per-vehicle model/variant flag byte at `MachineData+0xc3b`, set by the vehicle's model-setup callback (`vcDataCommon+0x18`) at spawn. It is only set for the special transformation star variants — Wing Kirby (`VCKIND_WINGKIRBY`, kind 0x11) and Compact Star (`VCKIND_COMPACT`, kind 0x1) — whose derived attributes are fixed rather than patch-driven, so vanilla `Machine_GivePatch` / `Machine_GiveAllUp` skip `Machine_AdjustAttributes` for them. The replacements preserve that gate exactly.

`PatchCap_GiveAllUp` loops `PATCHKIND_NUM` (9) stats, pre-clamping each individually, then credits the player's all-up counter — but only when the machine is occupied:

```c
ply = (md->rider_gobj == 0) ? 5 : RiderGObj_GetPly(md->rider_gobj);
if (ply != 5) {                                  // 5 == no rider; skip
    Ply_SetAllUpCollected(ply, num + Ply_GetAllUpCollected(ply));
}
```

### All-Up Tracking

`PatchCap_GiveAllUp` credits `Ply_SetAllUpCollected(ply, num + collected)` using the **original** `num`, not the per-stat clamped value. This matches vanilla — the counter tracks "all-ups picked up", not "effective stat gain". If any checklist check keys off that counter, a capped all-up that produced no stat change still counts.

## Increment Flow

`AP_ITEM_PATCH_CAP_INCREASE` is routed in `APItems_HandleItem` (`ap_item_handler.c:141`):

```c
case AP_ITEM_PATCH_CAP_INCREASE:
    PatchCap_Increment();
    return 1;
```

`PatchCap_Increment()`:

1. `ap_save->patch_cap_count++`
2. `OSReport("[PatchCap] Patch cap increased to %d (target %d).\n", cap, target)`
3. Textbox via `tb_api->EnqueueColoredNounFmt(NULL, "Patch cap", tb_api->PatchColors[PATCHKIND_CHARGE], " increased! (%d/%d)", cap, target)` — yellow "Patch cap" noun, denominator is the **target** (not 18, not 127).

## Consumer Coverage

Every consumer of the stat cap goes through `Patch_GetMaxValue`. No code path reads `gmGameParams.patch_max` (+0x18) directly.

**Callers of `Patch_GetMaxValue` (0x8000aaf0) — 16 call sites across 7 functions:**

| Function | Address | Sites |
|----------|---------|-------|
| `Machine_UpdateAppearance` | 0x801d6668 | 2 |
| `zz_80194f64_` (still unnamed) | 0x80194f64 | 1 |
| `Stats_ClampValues?` (tail-called by `Machine_ApplyStatClamped` 0x801e094c) | 0x80194d80 | 1 |
| `Stats_ClampValues2?` (tail-called by `Machine_ApplyAllStatsClamped` 0x801e096c) | 0x80194e60 | 1 |
| `PlayerView_Think?` (HUD stat-bar denominator) | 0x80116d8c | 9 (one per stat) |
| `Machine_GetStatRatio` (per-stat attribute normalizer) | 0x801caa8c | 1 |
| `Machine_GetStatRatio2` (second normalizer, sibling) | 0x801cabd4 | 1 |

The 9 `PlayerView_Think?` sites correspond to `PATCHKIND_NUM` (9 stats); the bar fill ratio uses this as denominator.

**Direct readers of `gmGameParams.patch_max`:** None. The struct is reached only via `Gm_Get_gmDataAll()` (0x8000fcb0); across its callers nobody dereferences `+0x18` independently. `Patch_GetMaxValue` (+0x18) and `Patch_GetMinValue` (0x8000ab1c, +0x1c) are the sole readers of those two bytes.

**Per-vehicle attribute interpolation:** `Machine_AdjustAttributes` (0x801c7278) dispatches two callbacks per machine kind via `(&vcDataCommon_table)[vc_kind]->+0x1c/+0x20`.

| Machine | +0x1c callback | +0x20 callback |
|---------|----------------|----------------|
| Warp Star (VCKIND 0, `vcDataCommon @ 0x804b1658`) | `Machine_CopyCommonAttributes?` (0x801e812c) — attribute memcpy, does not touch `patch_max` | `Machine_AdjustAttributesStar` (0x801e906c) → `Machine_ApplyStarStatScaling` (0x801e81e4) → `Machine_GetStatRatio` + `Machine_ScaleFromRatio` (0x801cab4c) — **routes through `Patch_GetMaxValue`** |
| Rex Wheelie (VCKIND 1, `vcDataCommon @ 0x804b1c40`) | `RexWheelie_InitAttr` (0x801f3c94) — attribute memcpy | `Machine_AdjustAttributesBike` (0x801f4dac) → `Machine_ApplyBikeStatScaling` (0x801f3d44) → `Machine_GetStatRatio` + `Machine_GetStatRatio2` — **routes through `Patch_GetMaxValue`** |

The shared normalizers `Machine_GetStatRatio` (0x801caa8c) and `Machine_GetStatRatio2` (0x801cabd4) both unconditionally `bl 0x8000aaf0`. There is no direct-read escape hatch.

**Bottom line:** our `CODEPATCH_REPLACEFUNC` on `Patch_GetMaxValue` is a complete interception. The returned target value scales the entire attribute-interpolation curve and the HUD stat-bar fill ratio.

## Hardware ceiling (`PATCH_STAT_MAX`)

`Patch_GetMaxValue` returns via `extsb` (sign-extend low byte) at `0x8000ab08`. So **127 is the firm hardware ceiling**: values 128–255 sign-flip negative and collapse the effective cap to the floor. `PATCH_STAT_MAX` is set to that ceiling (127), and `PatchCap_GetTarget()` clamps the option to it, so a malformed YAML value can never blow past the limit.

Secondary considerations if you ever touch this:

- `ap_save->permanent_patches[kind]` is `u8` with `< PATCH_STAT_MAX` gates in `patch_item.c:138,153`. At 127 these stay well within `u8`.
- The APWorld must ship enough `AP_ITEM_PATCH_CAP_INCREASE` items for a progressive slot to reach its target: at most `target - PATCH_CAP_PROGRESSIVE_START` (= `target - 1`) increments are useful.
- HUD stat-bar segment dividers (if drawn as discrete ticks rather than continuous fill) are **not** on the `Patch_GetMaxValue` path. The bar *fill ratio* scales correctly (denominator goes through our hook), but visual ticks may still render as 18 segments. Eyeball in-game if you push the target high.
- `GOAL_MAX_STATS_CT` measures the number of *patches collected* on each stat against `city_trial_patch_cap_amount` (the target), in `goal_max_stats_ct.c` — **not** against `PATCH_STAT_MAX`. The goal is "collect the slot's target patches on every stat in one CT run". CT stats spawn at `-2` (HP at `0`), so the per-stat test is `value >= start + target` (HP needs raw `target`, every other stat raw `target - 2`); comparing raw `value >= target` for all would silently make the eight non-HP stats require two extra patches each.

## Known Limitations

### Option name vs. scope

The option is named `city_trial_progressive_patch_caps`, but the hook is mode-agnostic. Every `Machine_GivePatch` / `Machine_GiveAllUp` call is clamped, including Air Ride sessions when `ar_permanent_patches_enabled` re-applies accumulated permanent patches at race start (`patch_item.c:212`, inside the perm-patch apply loop). If the target cap = 5, AR perm-patch application also caps at 5.

Currently benign (AR gameplay doesn't normally raise stats beyond perm-patches, and the clamped values are still applied), but the naming implies CT-only enforcement that we don't actually enforce. Decision: leave as-is (global) and accept the slightly aspirational name.

### Saturation textbox

Once `1 + patch_cap_count >= target`, further `PatchCap_Increment()` calls still enqueue `"Patch cap increased! (target/target)"` and `OSReport` still says "increased to <target>", even though the cap didn't move. Cosmetic only; the clamp produces correct numbers.

### `patch_cap_count` overflow

`u8 patch_cap_count` wraps at 256. Only `target - PATCH_CAP_PROGRESSIVE_START` increments are ever useful; an APWorld shipping 256+ cap items would wrap the counter. Because `PatchCap_GetCap()` clamps `1 + count` to the target anyway, a wrap only matters if the count exceeds 255 — not currently reachable, but a `patch_cap_count < target` guard in `Increment` would make it unconditionally safe.

### All-Up credit on capped pickups

As noted above, `Ply_SetAllUpCollected` is credited with the uncapped `num`. Matches vanilla behavior. If any clear_kind in `checklist-mappings.csv` keys off "N all-ups collected", a capped all-up still counts.
