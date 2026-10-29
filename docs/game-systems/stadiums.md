# Stadiums

A City Trial round ends in one of 24 stadium kinds (`StadiumKind`), grouped into eight
`StadiumGroup`s - Drag Race, Air Glider, Target Flight, High Jump, Kirby Melee, Destruction Derby,
Single Race and Vs. King Dedede. This covers how a stadium kind becomes a stage and a ground file,
which stadiums the game makes available, and how it picks one for a round: the pieces a mod needs
to gate, force or re-skin a stadium.

## Stadium to Stage to Ground

`Stadium_ApplyDescConfig` (0x800404c4) configures a stadium round from a 24-entry descriptor
table at `(*stc_gmdataall)+0x08`, 6 bytes per kind: `{u8 city_kind, u8 stage_kind, u16
time_seconds, u8 flags, u8 pad}`. It writes `GameData.city_kind` and `GameData.stage_kind`, and
the top bits of `flags` feed `is_damage_enabled` (0x80), `is_perma_death_enabled` (0x40) and
`is_machine_hitbox_enabled` (0x20). `stage_kind` is always the stadium kind plus 10, and
`Gm_GetGrKindFromStageKind` (0x80261ce8) maps it to the physical ground through the `Stage.dat`
table (`stData`, 0x58 bytes per stage kind: the `GroundKind` at +0x00, the item-pool id at +0x24).

| Stadiums | `city_kind` | Ground |
|---|---|---|
| Drag Race 1-4 | 7 | `GrZeroyon5`, `GrZeroyon3`, `GrZeroyon1`, `GrZeroyon4` |
| Air Glider | 8 | `GrJump3` |
| Target Flight | 9 | `GrJump2` |
| High Jump | 11 | `GrJump1` |
| Kirby Melee 1-2 | 13 | `GrPasture1`, `GrColosseum5` |
| Destruction Derby 1-5 | 14 | `GrColosseum1`, `GrColosseum3`, `GrDedede1`, `GrCity1`, `GrCity1` |
| Single Race 1-9 | 15 | The nine Air Ride course grounds |
| Vs. King Dedede | 18 | `GrColosseum1` |

Two pairings invert what the names suggest: **Destruction Derby 3 is fought in `GrDedede1`**,
while **Vs. King Dedede reuses Destruction Derby 1's `GrColosseum1`**, with its own item-pool id and
so a different item pool. Derby 4 and 5 are fought in the City Trial city itself, on item pools 1
and 2 of `GrCity1`. King Dedede himself is player slot 4.

**No stadium is fought on foot.** `enableKirbyToExitVehicle` (0x801918d0) is just
`Gm_IsInCity()`, and it is the only gate on walking - its callers are `Rider_InitWalk`,
`Rider_LoadFile` and `Rider_DropPatches` - so the open city is the one place a rider dismounts.
`isRuleKirbyMelee` (0x80191930, `city_kind == 13`) only swaps the copy-ability wheel's pool in
`Rider_StartCopyWheel`.

## Availability

| Function | Address | Vanilla behavior |
|---|---|---|
| `Gm_StadiumIsDefaultUnlocked` | 0x8000c148 | 1 for every kind except the six checklist-gated ones: 3 (Drag Race 4), 8 (Kirby Melee 2), 11-13 (Destruction Derby 3-5) and 22 (Single Race 9) |
| `Gm_StadiumIsUnlocked` | 0x8000c17c | For those six, City Trial checklist rewards 37-42 through `ClearChecker_CheckUnlocked`, or `Checklist_CheckCachedUnlock_CityTrial` (0x80007e8c) in a LAN session; 0 for every other kind |
| `Gm_StadiumIsAvailable` | 0x8000c228 | The two combined, with its **own inlined copies** of both tables |
| `Gm_StadiumCheckUnlocked` | 0x80007ee4 | The runtime unlock bitfield: the live one at 0x80536ee8, or a LAN-session snapshot while `Net_IsSessionActive` |

Because `Gm_StadiumIsAvailable` inlines the tables instead of calling the other two, a mod that
changes availability has to replace all of them. `Gm_StadiumCheckNewLabel` (0x80008038) reads the
separate "NEW" badge bitfield at 0x80536eec.

## Picking the Round's Stadium

`CityTrial_DecideStadium` (0x8003f808) picks each round's stadium from the rules setting
`city.menu_stadium_selection` (`GameData+0x396`: 0 Shuffle, otherwise `StadiumGroup + 1`):

- **Shuffle** walks all 24 kinds, drops the last four picks in `city.prev_stadium_kind`
  (`GameData+0x45e`), keeps those passing `Gm_StadiumIsDefaultUnlocked` or
  `Gm_StadiumIsUnlocked`, and rolls a weighted pick from the per-kind weights in
  `*stc_gmdataall`.
- **A group** keeps only that group's available kinds, with no history exclusion.

It then shifts the history and writes the pick to `city.stadium_kind` (`GameData+0x5ad`).

The four-entry history is hardcoded. With fewer than five stadiums available, every candidate can
fall inside it; the weight total is then 0, `HSD_Randi(0)` is still called, and the pick falls back
to kind 0. A mod that restricts availability has to size the exclusion to what is available.

## The Stadium List

`CityTrial_BuildStadiumList` (0x80046df0) builds the rules screen's stadium list from
`Gm_StadiumCheckUnlocked`, with two side paths that add stadiums anyway:

- A debug unlock-all, taken with the debug level at 3 or more (the `blt` at 0x80046e1c) and R plus
  D-Pad Up held on port 1, which writes every locked kind's unlock and "NEW" bits.
- A checklist fallback: when `Gm_StadiumCheckUnlocked` reports a kind locked, the `beq` at
  0x80046ef8 branches to code that asks the two checklist queries, either of which can re-add it.
