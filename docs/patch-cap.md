# Patch Cap

Patch cap turns vanilla City Trial's fixed stat ceiling - raw 16 from `gmGameParams.patch_max` (+0x18), which is 18 patches for the eight stats that spawn at -2 and 16 for HP, which spawns at 0 - into a configurable, optionally progressive per-stat cap. A slot picks a min/max pair: the cap starts at `city_trial_patch_cap_min` and grows one step per `AP_ITEM_PATCH_CAP_INCREASE` received, up to `city_trial_patch_cap_max`.

**Files:** `mods/archipelago/src/patch_cap.c` / `.h`, with the save field and both options in `main.h`, the item dispatch in `ap_item_handler.c`, and the boot hook in `main.c`.

## Slot Options

| Option | Range | Default | Meaning |
|--------|-------|---------|---------|
| `city_trial_patch_cap_min` | 1-18 | 18 | Cap the player starts at. |
| `city_trial_patch_cap_max` | 18-30 | 18 | Cap ceiling and the `GOAL_MAX_STATS_CT` threshold. `Patch_GetMaxValue` reports `max - 2`, the non-HP raw ceiling, as the stat clamp and the HUD/attribute normalization range. Anything above 127 resolves to `PATCH_STAT_MAX` (127). |

There is no separate "progressive" toggle: `min == max` is a flat cap with no Patch Cap Increase items in the pool, `min < max` is progressive, and the AP world ships exactly `max - min` increase items. Both options are read at every clamp; nothing is precomputed at connect. Until `ap_save->options_received` is set neither is read at all: `PatchCap_GetMax` and `PatchCap_GetCap` both return 18, vanilla's ceiling, so a save that has never connected plays like vanilla. The defaults (18/18) reproduce vanilla too: 18 patches on the eight non-HP stats, 16 on HP, normalized against raw 16.

Both options are measured in **patches**, not raw stat value. CT stats spawn at `-2`, except HP at `0`, so the raw ceiling is `start + cap` per stat and all nine hold the same number of patches, until HP meets the normalization ceiling `max - 2` (below). `PatchCap_GetStatStart(kind)` is the single source of that baseline, and `PatchCap_IsStatAt(values, kind, patches)` tests a stat against a patch count measured from it, or against the raw ceiling when that is lower - HP's case near the slot max. It is the form the Max Stats goal in `goal_max_stats_ct.c` uses.

`patch_cap.h` exports `PatchCap_OnBoot`, `PatchCap_Increment`, `PatchCap_GetMax` (the slot ceiling), `PatchCap_GetCap` (the cap in force now), `PatchCap_GetStatStart` and `PatchCap_IsStatAt`. The three replacement bodies are `static`.

## Hooks

`PatchCap_OnBoot()` installs three `CODEPATCH_REPLACEFUNC` hooks:

| Replaced | Address | Replacement | Purpose |
|----------|---------|-------------|---------|
| `Patch_GetMaxValue` | 0x8000aaf0 | `PatchCap_GetMaxValue` | Returns `max - 2` - the stat clamp and the HUD/attribute normalization range |
| `Machine_GivePatch` | 0x801cacf4 | `PatchCap_GivePatch` | Pre-clamp delta to the current cap, apply, update appearance + attributes |
| `Machine_GiveAllUp` | 0x801cad40 | `PatchCap_GiveAllUp` | Per-stat pre-clamp, apply, credit `Ply_SetAllUpCollected`, update |

`Machine_GivePatch` has two vanilla callers, `Machine_OnTouchItem` (call at 0x801db478) and `zz_801c8210_` (call at 0x801c8220), and both reach the replacement. `Machine_GivePatchOrCandy` (0x801cb1c0) does not call it despite its name - its only give is `Machine_GiveCandy`.

### `PatchCap_GetMaxValue` returns the ceiling, not the current cap

This is load-bearing. `Patch_GetMaxValue` is both the upper bound of `Stat_AddClamped` and the denominator the game uses to *normalize* stats for the HUD bars and the per-vehicle attribute interpolation curve (`Machine_GetStatRatio`, clamped to [-1, 1]). It returns `PatchCap_GetMax() - 2`, the raw value a non-HP stat reaches at the slot max, floored at 1 so a tiny debug ceiling can never divide by zero or flip the ratio's sign.

A non-HP stat at the slot max therefore reads ratio 1, exactly like a vanilla stat at 18 patches, and the default ceiling of 18 reproduces vanilla's raw 16. HP spawns at 0, so the same bound stops it at `max - 2` patches - two short of the others, as in vanilla. Returning the slot ceiling rather than the current cap keeps the curve fixed while the cap grows from `min`; "you can't go higher right now" is enforced separately by the delta clamp against the current cap. A ceiling above 18 widens the range, so each patch moves the curve proportionally less.

### Delta clamping

`PatchCap_ClampDelta(kind, current, delta)` passes non-positive deltas through unchanged, so stat-down patches, the drop-patches trap and every other reduction ignore the cap. For a positive delta it computes `room = (PatchCap_GetStatStart(kind) + cap) - current` and clamps to it, yielding 0 once the stat already holds `cap` patches.

The `start` offset is what makes the cap count patches rather than raw value. Without it the eight non-HP stats (spawn `-2`) would hold `cap + 2` patches while HP (spawn `0`) holds `cap`.

After pre-clamping, both replacements call `Machine_ApplyStatClamped` (0x801e094c), which tail-calls `Stat_AddClamped` (0x80194d80) for a secondary clamp to `[Patch_GetMinValue, Patch_GetMaxValue]`. For the eight non-HP stats the raw ceiling `start + cap` never exceeds `max - 2`, so that clamp is a no-op. For HP it is the bound that holds it at `max - 2` once the cap climbs past that.

At the slot ceiling every bar reads full: the non-HP stats sit at raw `max - 2` after `max` patches, and HP at raw `max - 2` after `max - 2`.

### Appearance and attribute refresh

Both replacements finish with `Machine_UpdateAppearance`, then `Machine_AdjustAttributes` unless `MachineData.suppress_attr_recalc` is set - the sign bit of the variant flag byte at `MachineData+0xc3b`, written at spawn by the class's `MachineClassDesc.setup_model` (`stc_machine_class_desc`, `r13-0x6148`, indexed by `is_bike`). Only two machines set it, whose derived attributes are fixed rather than patch-driven: `Machine_Star_SetupModel` (0x801e7ad4) sets it for Wing Kirby (star slot 17, `VCKIND_WINGKIRBY`) and `Machine_Wheel_SetupModel` (0x801f37d4) for Wheel Kirby (bike slot 1, `VCKIND_WHEELKIRBY`). Vanilla `Machine_GivePatch` / `Machine_GiveAllUp` skip the recalc for them and the replacements preserve that gate exactly.

`PatchCap_GiveAllUp` loops all `PATCHKIND_NUM` (9) stats, pre-clamping each individually, then credits the player's all-up counter - but only when the machine is occupied (`md->rider_gobj` non-null; `RiderGObj_GetPly` returning 5 means no rider). It credits the **original** `num`, not the clamped value, matching vanilla: the counter tracks all-ups picked up, not effective stat gain. A capped all-up that produced no stat change still counts toward any checklist check keyed on that counter.

## Increment Flow

`AP_ITEM_PATCH_CAP_INCREASE` is routed in `APItems_HandleItem` (`ap_item_handler.c`) above the 3D scene gate, so it applies in any scene. `PatchCap_Increment()` bumps `ap_save->patch_cap_count` (a `u8`, saturating at 255), logs whether the cap actually moved, and announces "Patch cap increased! (cap/max)" through `APAnnounce_Grant`, like every other received item - so the line shows only while *Messages -> Local -> Items* is on, which it is not by default. The denominator shown is the slot max, not 18 and not 127.

## Consumer Coverage

Every consumer of the stat cap goes through `Patch_GetMaxValue`, which is why one replacement is a complete interception. The struct field it reads (`gmGameParams.patch_max`, +0x18) has no other reader: the struct is reached only via `Gm_Get_gmDataAll()` (0x8000fcb0) and no caller dereferences +0x18 independently. `Patch_GetMaxValue` and `Patch_GetMinValue` (0x8000ab1c, +0x1c) are the sole readers of those two fields (each loaded as a word, then sign-extended from its low byte).

16 call sites across 7 functions read it:

| Function | Address | Sites |
|----------|---------|-------|
| `Machine_UpdateAppearance` | 0x801d6668 | 2 |
| `Machine_SetStatBlockClamped` | 0x80194f64 | 1 |
| `Stat_AddClamped` (tail-called by `Machine_ApplyStatClamped` 0x801e094c) | 0x80194d80 | 1 |
| `Stat_AddClampedAll` (tail-called by `Machine_ApplyAllStatsClamped` 0x801e096c) | 0x80194e60 | 1 |
| `PlyView_HudThink` (HUD stat-bar denominator) | 0x80116d8c | 9, one per stat |
| `Machine_GetStatRatio` (per-stat attribute normalizer, clamped to [-1,1]) | 0x801caa8c | 1 |
| `Machine_GetStatRatio2` (second normalizer, sibling) | 0x801cabd4 | 1 |

Per-vehicle attribute interpolation runs through the same normalizers. `Machine_AdjustAttributes` (0x801c7278) dispatches two callbacks per machine class via `stc_machine_class_desc[is_bike]`: `copy_attr` (+0x1c) is an attribute memcpy that never touches `patch_max`, while `adjust_attr` (+0x20) is the stat-scaling pass - `Machine_AdjustAttributesStar` (0x801e906c) -> `Machine_ApplyStarStatScaling` (0x801e81e4) for the star class, `Machine_AdjustAttributesBike` (0x801f4dac) -> `Machine_ApplyBikeStatScaling` (0x801f3d44) for the bike class. Both end at `Machine_GetStatRatio` / `Machine_GetStatRatio2`, which `bl 0x8000aaf0` unconditionally. So the returned ceiling scales the whole attribute-interpolation curve as well as the HUD fill ratio.

## Hardware Ceiling (`PATCH_STAT_MAX`)

`Patch_GetMaxValue` returns via `extsb` (sign-extend low byte) at 0x8000ab08, so 127 is a firm hardware ceiling: 128-255 sign-flip negative and collapse the effective cap to the floor. `PATCH_STAT_MAX` is 127 and `PatchCap_GetMax()` resolves anything above it to 127, so a malformed YAML value can never blow past the limit. `ap_save->permanent_patches[kind]` is `u8` with `< PATCH_STAT_MAX` gates in `permanent_patch.c`, well within range at 127.

`GOAL_MAX_STATS_CT` is not on the `Patch_GetMaxValue` path. `goal_max_stats_ct.c` tests every stat with `PatchCap_IsStatAt` against `PatchCap_GetMax()` patches - the slot ceiling, not the current effective cap. The goal is "collect the slot's ceiling worth of patches on every stat in one CT run", so a progressive slot must first receive every Patch Cap Increase item to make it reachable. Every stat needs raw `max - 2`: the eight non-HP stats reach it after `max` patches from their -2 start, and HP, held by the raw ceiling, after `max - 2`. `PatchCap_IsStatAt` caps its target at that ceiling, which is what lets HP qualify.

## Known Limitations

**Option name vs. scope.** Both options are named `city_trial_*`, but the hook is mode-agnostic - every `Machine_GivePatch` / `Machine_GiveAllUp` call is clamped, including the Air Ride race-start re-apply of accumulated permanent patches. Benign: Air Ride gameplay does not otherwise raise stats, and the clamped values are still applied.

**Saturation announcement.** Once `min + patch_cap_count >= max`, further increments still announce "Patch cap increased! (max/max)" even though nothing moved. The `OSReport` distinguishes the two cases; the announcement does not. Cosmetic only.

## Debug override

The cap range normally arrives once, with the slot options, and is immutable for the seed.
`archipelago_debug`'s Slot Options page writes `city_trial_patch_cap_min` and `_max` directly
through `ArchipelagoAPI.DebugSetPatchCapMin` / `DebugSetPatchCapMax`, one bound per row, so the
flat-cap case (`min == max`), the one-per-item climb and the `PATCH_STAT_MAX` ceiling can each be
exercised on one save.

Both rows read back through `GetPatchCapRange`. The overrides only bite on a save that has
received its slot options: before that the cap is vanilla's 18 whatever is stored, and the
rows show the stored 0 as 127. A ceiling of 1 or 2 floors the raw ceiling at 1. The effective cap, the received Patch Cap
Increase count and the seed range all appear together in that mod's Report State output.
