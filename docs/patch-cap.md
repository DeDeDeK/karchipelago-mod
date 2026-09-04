# Patch Cap

Patch cap turns vanilla City Trial's fixed stat ceiling (18 patches per stat) into a configurable, optionally progressive per-stat cap. A slot picks a min/max pair: the cap starts at `city_trial_patch_cap_min` and grows one step per `AP_ITEM_PATCH_CAP_INCREASE` received, up to `city_trial_patch_cap_max`.

**Files:** `mods/archipelago/src/patch_cap.c` / `.h`, with the save field and both options in `main.h`, the item dispatch in `ap_item_handler.c`, and the boot hook in `main.c`.

## Slot Options

| Option | Range | Default | Meaning |
|--------|-------|---------|---------|
| `city_trial_patch_cap_min` | 1-18 | 18 | Cap the player starts at. `0` (options not yet received) falls back to the max. |
| `city_trial_patch_cap_max` | 18-30 | 18 | Cap ceiling, the `GOAL_MAX_STATS_CT` threshold, and the value `Patch_GetMaxValue` reports for HUD/attribute normalization. `0`/unset is treated as `PATCH_STAT_MAX` (127); anything above 127 is clamped to it. |

There is no separate "progressive" toggle: `min == max` is a flat cap with no Patch Cap Increase items in the pool, `min < max` is progressive, and the AP world ships exactly `max - min` increase items. Both options are read at every clamp; nothing is precomputed at connect. The defaults (18/18) reproduce vanilla exactly.

Both options are measured in **patches**, not raw stat value. CT stats spawn at `-2`, except HP at `0`, so the raw ceiling is `start + cap` per stat and all nine hold the same number of patches. `PatchCap_GetStatStart(kind)` is the single source of that baseline, shared with the Max Stats goal in `goal_max_stats_ct.c`.

`patch_cap.h` exports only `PatchCap_OnBoot`, `PatchCap_Increment` and `PatchCap_GetStatStart`. The rest of the file is static, except the three replacement bodies, which are non-`static` only so `CODEPATCH_REPLACEFUNC` can take their address.

## Hooks

`PatchCap_OnBoot()` installs three `CODEPATCH_REPLACEFUNC` hooks:

| Replaced | Address | Replacement | Purpose |
|----------|---------|-------------|---------|
| `Patch_GetMaxValue` | 0x8000aaf0 | `PatchCap_GetMaxValue` | Returns the slot max - the HUD/attribute normalization range |
| `Machine_GivePatch` | 0x801cacf4 | `PatchCap_GivePatch` | Pre-clamp delta to the current cap, apply, update appearance + attributes |
| `Machine_GiveAllUp` | 0x801cad40 | `PatchCap_GiveAllUp` | Per-stat pre-clamp, apply, credit `Ply_SetAllUpCollected`, update |

`Machine_GivePatchOrCandy` (0x801cb1c0) calls `Machine_GivePatch`, so it is covered transitively.

### `PatchCap_GetMaxValue` returns the max, not the current cap

This is load-bearing. `Patch_GetMaxValue` is the denominator the game uses to *normalize* stats for the HUD bars and the per-vehicle attribute interpolation curve. Returning the per-slot max keeps that curve scaled to the full reachable range no matter how far the cap has grown from `min`; enforcement of "you can't go higher right now" is done separately by the delta clamp against the current effective cap. Because the hook returns the slot max rather than `PATCH_STAT_MAX`, this is not behavior-neutral: a slot with a max below 127 sees its HUD bars and attribute curve normalized to that lower value.

### Delta clamping

`PatchCap_ClampDelta(kind, current, delta)` passes non-positive deltas through unchanged, so stat-down patches, the drop-patches trap and every other reduction ignore the cap. For a positive delta it computes `room = (PatchCap_GetStatStart(kind) + cap) - current` and clamps to it, yielding 0 once the stat already holds `cap` patches.

The `start` offset is what makes the cap count patches rather than raw value. Without it the eight non-HP stats (spawn `-2`) would hold `cap + 2` patches while HP (spawn `0`) holds `cap`.

After pre-clamping, both replacements call `Machine_ApplyStatClamped` (0x801e094c), which tail-calls `Stat_AddClamped` (0x80194d80) for a secondary clamp to `[Patch_GetMinValue, Patch_GetMaxValue]`. Since our raw ceiling `start + cap` is never above `max`, that second clamp is a no-op.

The flip side: a non-HP stat tops out at raw `cap - 2`, two below the normalization range, so a fully-capped non-HP bar reads slightly under full while HP reads full. The shortfall is a fixed 2 raw units, conspicuous only at very low caps.

### Appearance and attribute refresh

Both replacements finish with `Machine_UpdateAppearance`, then `Machine_AdjustAttributes` unless `MachineData.suppress_attr_recalc` is set - the sign bit of the model/variant flag byte at `MachineData+0xc3b`, written by the vehicle's model-setup callback (`vcDataCommon+0x18`) at spawn. Only the transformation star variants set it: Wing Kirby (`VCKIND_WINGKIRBY`) and Compact Star (`VCKIND_COMPACT`), whose derived attributes are fixed rather than patch-driven. Vanilla `Machine_GivePatch` / `Machine_GiveAllUp` skip the recalc for them and the replacements preserve that gate exactly.

`PatchCap_GiveAllUp` loops all `PATCHKIND_NUM` (9) stats, pre-clamping each individually, then credits the player's all-up counter - but only when the machine is occupied (`md->rider_gobj` non-null; `RiderGObj_GetPly` returning 5 means no rider). It credits the **original** `num`, not the clamped value, matching vanilla: the counter tracks all-ups picked up, not effective stat gain. A capped all-up that produced no stat change still counts toward any checklist check keyed on that counter.

## Increment Flow

`AP_ITEM_PATCH_CAP_INCREASE` is routed in `APItems_HandleItem` (`ap_item_handler.c`) above the 3D scene gate, so it applies in any scene. `PatchCap_Increment()` bumps `ap_save->patch_cap_count`, logs whether the cap actually moved, and enqueues a yellow "Patch cap increased! (cap/max)" textbox. The denominator shown is the slot max, not 18 and not 127.

## Consumer Coverage

Every consumer of the stat cap goes through `Patch_GetMaxValue`, which is why one replacement is a complete interception. The struct field it reads (`gmGameParams.patch_max`, +0x18) has no other reader: the struct is reached only via `Gm_Get_gmDataAll()` (0x8000fcb0) and no caller dereferences +0x18 independently. `Patch_GetMaxValue` and `Patch_GetMinValue` (0x8000ab1c, +0x1c) are the sole readers of those two fields (each loaded as a word, then sign-extended from its low byte).

16 call sites across 7 functions read it:

| Function | Address | Sites |
|----------|---------|-------|
| `Machine_UpdateAppearance` | 0x801d6668 | 2 |
| `Machine_SetStatBlockClamped` | 0x80194f64 | 1 |
| `Stat_AddClamped` (tail-called by `Machine_ApplyStatClamped` 0x801e094c) | 0x80194d80 | 1 |
| `Stat_AddClampedAll` (tail-called by `Machine_ApplyAllStatsClamped` 0x801e096c) | 0x80194e60 | 1 |
| `PlayerView_Think` (HUD stat-bar denominator) | 0x80116d8c | 9, one per stat |
| `Machine_GetStatRatio` (per-stat attribute normalizer) | 0x801caa8c | 1 |
| `Machine_GetStatRatio2` (second normalizer, sibling) | 0x801cabd4 | 1 |

Per-vehicle attribute interpolation runs through the same normalizers. `Machine_AdjustAttributes` (0x801c7278) dispatches two callbacks per machine kind via `(&vcDataCommon_table)[vc_kind]->+0x1c/+0x20`; the +0x1c callback is an attribute memcpy that never touches `patch_max`, while the +0x20 callback is the stat-scaling pass - `Machine_AdjustAttributesStar` (0x801e906c) -> `Machine_ApplyStarStatScaling` (0x801e81e4) for the Warp Star family, `Machine_AdjustAttributesBike` (0x801f4dac) -> `Machine_ApplyBikeStatScaling` (0x801f3d44) for Rex Wheelie. Both end at `Machine_GetStatRatio` / `Machine_GetStatRatio2`, which `bl 0x8000aaf0` unconditionally. So the returned max scales the whole attribute-interpolation curve as well as the HUD fill ratio.

## Hardware Ceiling (`PATCH_STAT_MAX`)

`Patch_GetMaxValue` returns via `extsb` (sign-extend low byte) at 0x8000ab08, so 127 is a firm hardware ceiling: 128-255 sign-flip negative and collapse the effective cap to the floor. `PATCH_STAT_MAX` is 127 and `PatchCap_GetMax()` clamps the option to it, so a malformed YAML value can never blow past the limit. `ap_save->permanent_patches[kind]` is `u8` with `< PATCH_STAT_MAX` gates in `patch_item.c`, well within range at 127.

Two things that are *not* on the `Patch_GetMaxValue` path:

- HUD stat-bar segment dividers, if drawn as discrete ticks rather than continuous fill. The bar fill ratio scales correctly, but visual ticks may still render as 18 segments.
- `GOAL_MAX_STATS_CT`, which compares patches collected against `city_trial_patch_cap_max` directly in `goal_max_stats_ct.c` - not `PATCH_STAT_MAX`, and not the current effective cap. The goal is "collect the slot's ceiling worth of patches on every stat in one CT run", so a progressive slot must first receive every Patch Cap Increase item to make it reachable. It applies the same `start` offset, so HP needs raw `max` and every other stat raw `max - 2`; testing `value >= max` for all nine would silently cost the non-HP stats two extra patches each.

## Known Limitations

**Option name vs. scope.** Both options are named `city_trial_*`, but the hook is mode-agnostic - every `Machine_GivePatch` / `Machine_GiveAllUp` call is clamped, including the Air Ride race-start re-apply of accumulated permanent patches. Benign: Air Ride gameplay does not otherwise raise stats, and the clamped values are still applied.

**Saturation textbox.** Once `min + patch_cap_count >= max`, further increments still enqueue "Patch cap increased! (max/max)" even though nothing moved. The `OSReport` distinguishes the two cases; the textbox does not. Cosmetic only.

**`patch_cap_count` overflow.** The counter is `u8` and wraps at 256. Only `max - min` increments are ever useful and `PatchCap_GetCap()` clamps `min + count` to the max, so a wrap needs an APWorld shipping 256+ cap items - unreachable with the 18-30 max range, but a `patch_cap_count < max` guard in `Increment` would make it unconditionally safe.
