# Stadium Gating

Each of the 24 City Trial stadiums can be individually locked behind an Archipelago unlock item. AP items 400-423 (`AP_STADIUM_UNLOCK_BASE` + `StadiumKind`) route through `ap_item_handler.c` to `GateStadiums_UnlockStadium(kind, /*announce=*/1)`, which sets the bit in `APSave.stadium_unlocked_mask`, ORs the kind into the vanilla "NEW" badge bitfield, and posts an `"Unlocked Stadium: <name>"` textbox with `tb_api->StadiumColor`. A locked stadium is excluded from both shuffle-mode and group-mode round selection and hidden from the stadium-list UI. The vanilla unlock-check pipeline and the per-round selector are both replaced outright.

**File:** `mods/archipelago/src/gate_stadiums.c`.

| Group | Stadiums | AP items |
|-------|----------|----------|
| Drag Race | DRAG1, DRAG2, DRAG3, DRAG4 | 400-403 |
| Air Glider | AIRGLIDER | 404 |
| Target Flight | TARGETFLIGHT | 405 |
| High Jump | HIGHJUMP | 406 |
| Kirby Melee | MELEE1, MELEE2 | 407-408 |
| Destruction Derby | DESTRUCTION1-5 | 409-413 |
| Single Race | SINGLERACE1-9 | 414-422 |
| VS King Dedede | VSKINGDEDEDE | 423 |

## Stadium to Stage to Ground

`Stadium_ApplyDescConfig` (0x800404c4) configures a round from a 24-entry descriptor table at `(*stc_gmdataall)+0x08`, stride 6: `{u8 city_kind, u8 stage_kind, u16 time_seconds, u8 flags, u8 pad}`. It writes `GameData.city_kind` (+0xa94) and `GameData.stage_kind` (+0xa97), and takes `is_damage_enabled` from `flags & 0x02` and `is_perma_death_enabled` from `flags & 0x01`. `stage_kind` is always `StadiumKind + 10`, and `Gm_GetGrKindFromStageKind` (0x80261ce8) maps that to the physical GroundKind through the `Stage.dat` table (`**(r13+0x7FC)`, stride 0x58, GroundKind at +0x00, ItemposId at +0x24).

| Stadiums | city_kind | GroundKind -> file |
|---|---|---|
| Drag Race 1-4 | 7 | 13/11/10/12 -> `GrZeroyon5`/`3`/`1`/`4` |
| Air Glider | 8 | 20 -> `GrJump3` |
| Target Flight | 9 | 19 -> `GrJump2` |
| High Jump | 11 | 18 -> `GrJump1` |
| Kirby Melee 1-2 | 13 | 14 -> `GrPasture1`, 17 -> `GrColosseum5` |
| Destruction Derby 1-5 | 14 | 15 -> `GrColosseum1`, 16 -> `GrColosseum3`, 21 -> `GrDedede1`, 9 -> `GrCity1` (x2) |
| Single Race 1-9 | 15 | 0,1,2,8,7,4,5,3,6 - the nine Air Ride course grounds |
| Vs. King Dedede | 18 | 15 -> `GrColosseum1` |

Two pairings are worth knowing because the names invert what they suggest: **Destruction Derby 3 is the one fought in `GrDedede1`**, while **Vs. King Dedede reuses Destruction Derby 1's `GrColosseum1`** (with its own ItemposId, so a different item pool); Derby 4 and 5 are fought in the City Trial city itself.

**No stadium is fought on foot.** `enableKirbyToExitVehicle` (0x801918d0) is `Gm_IsInCity()`, and it is the only gate on the walk state - its three callers are `Rider_InitWalk` (0x8018e1d8), `Rider_LoadFile` (0x801905ac) and `Rider_DropPatches` (0x8019d350) - so the open City Trial map is the one place a rider dismounts. Every stadium round is ridden, Kirby Melee included. `isRuleKirbyMelee` (0x80191930) is `Gm_GetCityKind() == 13` but has nothing to do with dismounting: its only caller is `Rider_StartCopyWheel` (0x801ae550), where it swaps the copy wheel's pool from `stc_copy_wheel_normal` to `stc_copy_wheel_melee`. King Dedede himself occupies player slot 4 on `VCKIND_WHEELVSDEDEDE` (`vsDedede_CPU_Cheating`, 0x80040b80).

## Vanilla Availability Pipeline

Four functions report stadium availability, and all four are `CODEPATCH_REPLACEFUNC`ed onto the same one-line mask read, `GateStadiums_IsUnlocked(kind)`:

| Function | Address | What vanilla does |
|----------|---------|-------------------|
| `Gm_StadiumIsDefaultUnlocked` | 0x8000C148 | Jump table of stadiums available by default: 1 for the low Drag Race kinds (0-2), else 0. |
| `Gm_StadiumIsUnlocked` | 0x8000C17C | Maps kinds 3-22 (the checklist-gated stadiums) through a jump table to clear/reward indices, then consults `Checklist_CheckCachedUnlock_CityTrial` (0x80007E8C, menu open) or `ClearChecker_CheckUnlocked` (0x80049E24, mode 2). Returns 0 outside 3-22. |
| `Gm_StadiumIsAvailable` | 0x8000C228 | Composite check that inlines its **own** copies of both jump tables and calls `Gm_StadiumCheckUnlocked` / the two checklist queries directly. It does not call the two standalone functions. |
| `Gm_StadiumCheckUnlocked` | 0x80007EE4 | Reads the runtime unlock bitfield: the temporary cache at 0x80536738 while the checklist menu is open, otherwise the live bitfield at 0x80536EE8. Cache writes are discarded on menu close. |

All four have to be replaced precisely because `Gm_StadiumIsAvailable` inlines the tables - replacing only the standalone functions would leave its callers reading them. `GateStadiums_IsUnlocked` returns 0 when `ap_save` is NULL, which matters because `Gm_StadiumCheckUnlocked` is called during early game init, before `OnSaveLoaded`.

With those in place, the game's own unlock bitfield at 0x80536EE8 (`stc_stadium_unlocked`) and its checklist cache layer are dead with respect to availability. The save mask is the single source of truth and is read live at every check, so a mid-session change (debug menu, late AP delivery) takes effect immediately with no sync step and no risk of the cache layer shadowing a write. `Gm_StadiumCheckNewLabel` (0x80008038) is deliberately *not* replaced - the checklist UI still consults the vanilla `stc_stadium_new_label` bitfield at 0x80536EEC for the "NEW" badge, which is why the unlock path sets that bit itself.

## Per-Round Selection

`CityTrial_DecideStadium` (0x8003F808) picks the stadium for each City Trial round. Vanilla branches on `gd->city.menu_stadium_selection` (`GameData+0x396`, `u8`; 0 = shuffle, 1+ = `StadiumGroup` + 1):

- **Shuffle**: walks all 24 kinds, excludes the last 4 picks via the `prev_stadium_kind[5]` history (`GameData+0x45E`; only entries 0-3 are consulted), keeps any kind passing `IsDefaultUnlocked || IsUnlocked`, then rolls a weighted pick from `gda->stadium_weights->weights[]` (`stc_gmdataall` at r13+0x494, weights at +0x4) via `HSD_Randi(weight_total)`.
- **Group**: skips the history exclusion and keeps only kinds whose `Gm_GetStadiumGroupFromKind` (0x8000BA20) equals `menu_stadium_selection - 1`.

It then shifts the 4-entry history, writing the pick to `prev_stadium_kind[0]` and to `gd->city.stadium_kind` (`GameData+0x5AD`).

**The history exclusion is a latent vanilla bug under restricted availability.** It is hardcoded to 4 entries, so with fewer than 5 stadiums unlocked every available pick can fall inside the history, leaving zero candidates and `weight_total == 0` - and `HSD_Randi(weight_total)` is still called unconditionally at 0x8003F908, giving `HSD_Randi(0)`.

`GateStadiums_DecideStadium` replaces the function primarily to fix that; the unlock-check replacement alone would already make an unmodified vanilla selector respect the mask. It builds the candidate pool from the mask, honors `menu_stadium_selection` the same way, and then differs in two places: the history exclusion is sized `min(unlocked_count - 1, 4)` clamped at 0, which guarantees at least one candidate (with 1 unlocked stadium there is no exclusion, with 5+ it is the full vanilla 4), and a group with no unlocked entries falls back to all unlocked stadiums rather than soft-locking. The weighted roll and the history/`stadium_kind` writes are as vanilla.

## Stadium List UI Side Channels

`CityTrial_BuildStadiumList` (0x80046DF0) feeds the stadium selection UI. It calls the replaced `Gm_StadiumCheckUnlocked` per kind, so most of it already respects the mask, but two paths re-add locked stadiums and are patched out with `CODEPATCH_REPLACEINSTRUCTION`:

- **Phase 1 auto-unlock loop**, gated by `progress` (r13+0x550) `>= 3` (the `blt` at 0x80046E1C) plus a `(flags & 0x28) == 0x28` check. When both pass, the loop body at 0x80046E34 walks every kind the mask reports as locked and calls `Gm_StadiumWriteUnlocked(kind, 1)` and `Gm_StadiumWriteNewLabel(kind, 1)`. The unlock write is harmless (that bitfield is ignored now), but the new-label write would badge every locked stadium "NEW" for late-game players. The `blt` is overwritten with an unconditional `b 0x80046E6C` (`0x48000050`), skipping the flag check and the loop.
- **Phase 2 checklist fallback**, in the main list-build loop (kind iterator from 0x80046EEC): when `Gm_StadiumCheckUnlocked` reports a stadium locked, the `beq` at 0x80046EF8 branches into a fallback at 0x80046F44 that calls `Checklist_CheckCachedUnlock_CityTrial` and `ClearChecker_CheckUnlocked`, either of which can re-add the stadium. That `beq` is retargeted to the next loop iteration at 0x80046FC4 (`0x418200CC`).

## Save Data

`u32 stadium_unlocked_mask` in `APSave` (`main.h`, via the global `ap_save`) - bit N = `StadiumKind` N. It is exposed through `ArchipelagoAPI` as `AP_UNLOCK_STADIUM`, and when the slot option `stadium_gating_enabled` is 0, `APOptions_ApplyUngatedCategories` (`main.c`) pre-fills it with `(1u << STKIND_NUM) - 1` at connect.

**`STKIND_VSKINGDEDEDE` is the one exception to that pre-fill.** KOing King Dedede there is a City Trial goal, and with stadiums ungated his stadium comes up in the rotation from the first match, so the seed would be winnable before a single AP item arrived. When the AP world sets that goal it keeps the Vs. King Dedede unlock in the pool despite the gate being off and sets `GOALGATE_VS_KING_DEDEDE` in the `goal_forced_gates` slot option; the pre-fill then clears that one bit and hands over the other 23. The unlock item arrives through the normal `GateStadiums_UnlockStadium` path, which never consults the gate flag.
