# Checklist

The checklist - the game's clear checker, `plclearcheckerlib.c` in its assert strings - is the
12x10 grid of objectives each mode carries: Air Ride, Top Ride and City Trial, 120 cells each.
Gameplay marks a cell complete; the checklist screen then plays its unlock, reveals the cells
around it and shows the reward it carries. Every unlockable in the game - machines, characters,
colors, stadiums, music, endings - is a checklist reward, and every menu that gates on one asks
the checklist. This doc covers the storage, how a cell becomes complete and visible, rewards,
the screen and its grid, and the seams a mod can hook.

## Clear data

Each mode has one `GameClearData` (0xF4 bytes) inside `GameData`, followed directly by that mode's
cross-game accumulators: `AirRideClearData` at `+0xd68`, `TopRideClearData` at `+0xe80` and
`CityTrialClearData` at `+0xfa8`. The game's own save carries all three.
`gmGetClearcheckerTypeP(mode)` (0x800076a0) is the accessor almost everything goes through; it
switches over the three modes and asserts on `mode >= 3`.

```
GameClearData                  0xF4
  0x00  new_unlock_flag
  0x01  display_state
  0x02  checkbox_filler_num        fillers the player holds
  0x03  checkbox_filler_list_len   filler icons shown, capped at 5
  0x04  grid_mapping[120]          clear_kind -> grid slot (col = slot % 12, row = slot / 12)
  0x7C  clear[120]                 one status byte per clear_kind
```

A cell is named by its `clear_kind`, the index into `clear[]`; where it sits on screen is
`grid_mapping[clear_kind]`. Five bits of the status byte carry everything:

| Bit | Name | Meaning |
|---|---|---|
| 0x01 | `is_new` | Completed, unlock not yet shown |
| 0x02 | `is_filler` | Completed by spending a checkbox filler |
| 0x04 | `is_unlocked` | Completed and shown |
| 0x08 | `has_reward` | Completed, and a reward sits on it |
| 0x10 | `is_visible` | Drawn as an empty box |

`ClearChecker_GetKindClear(mode, ck)` (0x8004a130) returns the byte; evaluators test `& 5`
(`is_new | is_unlocked`) to skip a cell already done. `Checklist_InitGridMapping` (0x8004a2bc)
builds a fresh layout: it pins the "Fill in over 100 Checklist blocks!" cell to slot 119 (in
City Trial also two cells to slots 0 and 11) and deals the rest out with `HSD_Randi`, so every
save has its own board.

## Completing a cell

Gameplay code never writes `clear[]` directly. It calls one of two funnels:

- `ClearChecker_SetNewUnlock(mode, ck)` (0x8004a054) sets `is_new` and plays
  `CLEARCHECKER_UNLOCK_SFX` (0x10008), at most once per engine frame. Air Ride and City Trial
  complete all their gameplay cells through it.
- `ClearChecker_SetNewUnlockSilent(mode, ck)` (0x80049fcc) sets `is_new` with no sound. Top Ride's
  evaluators use it alone; each plays the SFX itself first.

Both return without writing while `Net_IsSessionActive()` (0x8007b650) reports a LAN
multi-console session. During one, unlock queries read `GameData.unlock_cache` (`+0xd50`), the
snapshot `Checklist_BuildUnlockBitfields` (0x80007af0) takes when the session connects.

When a round ends, each mode's minor exit asks `ClearChecker_CheckForNewUnlocks(mode)`
(0x8004a1a4) - any `is_new && !is_unlocked` cell - and if so routes to that mode's checklist
screen instead of the menu, which is where the unlock plays.

Five meta cells bypass the funnels entirely. `Checklist_ProcessUnlock` stores them itself, as
`stb` of 1 into `clear[]`, while the checklist screen runs:

| Mode | clear_kind | Condition | Store |
|---|---|---|---|
| Air Ride | 24 (0x18) | 100+ cells with `is_unlocked` or `is_filler` | 0x8017efc0 |
| Top Ride | 119 (0x77) | same | 0x8017eff8 |
| City Trial | 55 (0x37) | same | 0x8017f030 |
| City Trial | 109 (0x6D) | cells of Dragoon Parts A/B/C (reward indices 27-29) all `has_reward` | 0x8017f0ac |
| City Trial | 110 (0x6E) | cells of Hydra Parts X/Y/Z (reward indices 31-33) all `has_reward` | 0x8017f120 |

Each store is guarded by `!is_unlocked` on its cell and is followed by the function's return
of 1. The instruction before each store is the `li r3, 1` that carries that return value, and
the store is the last instruction before a tail that never sets `r3` again.

## Unlock animation and reveal

`Checklist_ProcessUnlock` (0x8017e490) plays one unlock per call. It finds the first
`is_new && !is_unlocked` cell, overwrites its byte with `is_unlocked` alone, and sets
`is_visible` on the four orthogonal neighbours of its grid slot (left and right unless at
column 0 or 11, up and down unless at row 0 or 9), each mapped back to a `clear_kind` by a
reverse scan of `grid_mapping`. It then rebuilds the grid, moves the cursor onto the cell and
returns 1. `Checklist_Think` calls it from two sites (0x8017f4c8 and 0x801815b4) every 45 frames
of its unlock phase and moves on only once it returns 0, so a run's batch of unlocks plays in
sequence.

That is the only code that sets `is_visible`. A board starts nearly blank and grows outward from
its completions. A cell completed without the animation - written while the screen is not
showing it, or restored into fresh clear data - reveals nothing around it.

## Rewards

Each mode has a reward table of 3-byte `RewardEntry { reward_type, reward_param, clear_kind }`,
reached through `stc_reward_table_ptrs[mode]` (0x8049755c). `stc_reward_num` (0x805d51d0) holds
the counts: Air Ride 46, Top Ride 33, City Trial 44. A reward is named by its `reward_index`,
its row in that table; the rows are in a fixed ROM order, not `clear_kind` order. `reward_type`
is a `RewardType` (filler, bonus movie, extra rule, stadium, sound test, music, ending, course,
pause power-ups, the thirteen machines, the two characters, Dragoon and Hydra and their parts,
the four colors, the three Top Ride items); `reward_param` is `REWARDPARAM_AUDIO` for sound
test and music rows, `REWARDPARAM_ENDING` for endings, else 0.

| Function | Address | Role |
|---|---|---|
| `Checklist_GetRewardNum(mode)` | 0x80049c20 | The mode's reward count |
| `Checklist_GetClearKindFromRewardIndex(mode, ri)` | 0x80049c84 | The cell a reward sits on |
| `ClearChecker_GetRewardType(mode, ri)` | 0x80049d10 | `reward_type`; asserts `mode < 3` |
| `ClearChecker_GetRewardParam(mode, ri)` | 0x80049d98 | `reward_param`; asserts `mode < 3` |
| `ClearChecker_CheckUnlocked(mode, ri)` | 0x80049e24 | Is the reward unlocked: `has_reward` on its cell |
| `ClearChecker_GetRewardFromClearKind(mode, ck, &ri, &param)` | 0x80049ec4 | Reverse lookup; checks the mode bound itself |

`Checklist_SetRewardFlagOnUnlocks` (0x8017df5c) turns completions into rewards. For each
reward it takes the cell from `Checklist_GetClearKindFromRewardIndex`, and if that cell is
`is_unlocked` or `is_filler` but not yet `has_reward`, sets `has_reward`; when the reward is one
of the mode's five fillers (`stc_filler_reward_indices`, 0x804ad270) that same transition grants
a checkbox filler. It then builds the visible grid (below).

`ClearChecker_CheckUnlocked` is the game's single "is this unlocked" question. The character
and color select screens, the course and stadium lists, the sound test, `setLevelMusic`, the
Options movies (`AirRide_CheckBonusUnlocked`, reward indices 35 and 36), the pause stats
(`Pause_CheckStatsUnlocked`, City Trial reward 43), the Top Ride rules rows
(`TopRideRules_BuildRows`, 0x8001da14) and Top Ride's item set (`TopRide_OnCourseSelect`, Top
Ride reward indices 8-10) all call it with a reward index. Replacing it changes what every one
of them sees at once.

City Trial's six stadium rewards (indices 37-42) are the exception that is pushed rather than
asked. Their `reward_param` is 0; `Checklist_ProcessUnlock` (from 0x8017f128) tests each one's
cell for `has_reward` and writes the stadium's unlock and "new" flags itself
(`Gm_StadiumWriteUnlocked`, `Gm_StadiumWriteNewLabel`), with the reward-to-stadium mapping
hardcoded: 37 DRAG RACE 4, 38 KIRBY MELEE 2, 39-41 DESTRUCTION DERBY 3-5, 42 SINGLE RACE 9.

## Checkbox fillers

A filler is a token that completes any empty cell. Granting one increments
`checkbox_filler_num` and `checkbox_filler_list_len`; vanilla caps both at 5, while hoshi's
`Checklist_GrantFiller` leaves the count uncapped and caps only the icon list. Spending one runs
through `Checklist_Think`'s filler phases (`CLEARCHECKER_PHASE_FILLERLIST` to `FILLERPLACE`):
the chosen cell gets `is_filler` by a direct `ori`/`stb` at 0x80180dbc - not through
`SetNewUnlock` - the count drops and the game requests a card save.

`Checklist_Think` refuses three slots with an error sound, by immediate compares at
0x80180a74-0x80180a98: slot 119 in every mode, and slots 0 and 11 in City Trial. Those are the
slots `Checklist_InitGridMapping` pins the meta cells to, so a filler can never trigger an
auto-unlock.

## The checklist screen

The three checklists are minor scenes `MNRKIND_AIRRIDECHECKLIST`, `TOPRIDECHECKLIST` and
`CITYCHECKLIST` (0x20-0x22). They share one set of callbacks: `Checklist_MinorLoad`
(0x8004a768) takes `mode = minor - 0x20` and calls `Checklist_Init(mode, fresh)` (0x801822f4),
which loads the mode's SIS text, builds the screen and stores the mode in `ClearCheckerUI.mode`
(`+0x14`). `Checklist_Think` (0x8017f3bc) runs the state machine (`ClearCheckerPhase`) and the
cursor; R or X raises `NEXTTAB`, L or Y `PREVTAB`, B `EXIT`, and A on a cell whose reward is an
ending `ENDING`. `Checklist_MinorThink` (0x8004a648) consumes those phases: it cycles AR -> TR
-> CT with `CLEARCHECKER_TAB_SFX` (0x1000A), or leaves.

Pressing A on a completed cell whose reward is audio previews it: `Checklist_Think` resolves the
cell with `ClearChecker_GetRewardFromClearKind` (call at 0x801804dc), scans
`stc_audio_preview_tables[mode]` (0x804ad2ec) for the reward index from 0x80180508, plays the song
with `BGM_Play` and stores it in `GameData.main_menu.soundtest_bgm_kind`.

### Hover text

`Checklist_UpdateCellInfo` (0x80181d70) runs every frame for the cell under the cursor. It
reverse-scans `grid_mapping` for the cursor's slot and, when the cell changed, scans the mode's
reward table for a row on that `clear_kind` that `ClearChecker_CheckUnlocked` accepts (the block
0x80181ee4-0x80181f5c, leaving the index in `r27`). With no reward it shows the blank line
(`Text_InitPremadeText` at 0x80181f8c); with one it starts the Prize1 animation (four
`JObj_AddSetAnim` calls picking `mode * 2`, +1 with a reward) and, a few frames later, the
reward text (0x8018201c). `Checklist_RewardIconProc` (0x801820b4) draws the type icon: it asks
`ClearChecker_GetRewardParam` (call at 0x8018213c) whether to draw one, then
`ClearChecker_GetRewardType` (call at 0x80182178) which.

Text comes from the mode's SIS archive - `SisClrChk3D.dat`, `SisClrChk2D.dat` or
`SisClrChkCT.dat` - which `Checklist_Init` loads into SIS slot 0 (calls at 0x80182378,
0x8018238c, 0x801823a0). The game has five slots (`stc_sis_data[5]` at 0x8059a85c) and the
checklist uses only slot 0. `Text_InitPremadeText(text, index)` (0x8044f8c8) reads
`stc_sis_data[text->sis_id][index]`:

| Index | Text |
|---|---|
| `clear_kind + 4` | The objective |
| `0x7C` | The blank reward line |
| `reward_index + 0x7D` | The reward |

The objective entries hold only glyphs, word spaces, one explicit line break and a terminator;
the screen's `Text` object supplies the font, scale and layout, and squeezes a line too wide for
the box rather than wrapping it.

## The grid

There is no 120-cell model. `MnClCheckAll.dat` holds single-quad building blocks: the
background scene (`Bg`), the per-mode 248x128 banner (`Frame1`-`Frame3`), the cell-state tiles
(`Gray`, `Green`, `Gold`, `Red`, `Purple1`, `Purple2`), the overlays (`Cross`, `Complete`,
`Cursol`, `Prize1`, `Prize2`, `Win`) and `Pos`, whose corner joints give the per-cell spacing.
`Checklist_LoadModels` (0x801821ac) binds them into `ScMenuCommon.clearchecker`.

`Checklist_SetRewardFlagOnUnlocks` is the grid builder. For each of the 120 slots it
reverse-maps `grid_mapping` to a `clear_kind` and picks a tile from that cell's bits: `is_filler`,
else `has_reward`, else `is_unlocked`, else `is_visible`; a cell with none of them gets no tile
at all. Tiles are GObjs in `ScMenuCommon.clearchecker.cell_gobj[120]`, positioned at
`(slot % 12, slot / 12)` times the `Pos` spacing from the top-left, and recreated when a cell's
state changes. The board of empty boxes behind them is not cells: it is the single banner quad
(`frame_gobj`), sized to the full 12x10. `Checklist_DestroyElements` (0x80182cac) tears it all
down when the scene leaves.

The 12x10 shape is arithmetic spread across the screen code, not art:

| Function | Address | Geometry it hardcodes |
|---|---|---|
| `Checklist_SetRewardFlagOnUnlocks` | 0x8017df5c | Slot loop to 119; `slot % 12`, `slot / 12` |
| `Checklist_Think` | 0x8017f3bc | Cursor `col + row * 12`, wrap at column 11 and row 9 |
| `Checklist_Update` | 0x8018161c | Cursor highlight position; asserts "Clearchecker Number 120" on an unmapped slot |
| `Checklist_UpdateCellInfo` | 0x80181d70 | Unrolled 12x10 reverse scan |
| `Checklist_Init` | 0x801822f4 | Completion counter over all 120 cells (`is_unlocked` or `is_filler`) |

The cursor moves over all 120 slots whether or not a tile is there, so `grid_mapping` must stay
a full permutation of 0-119: an unmapped slot trips `Checklist_Update`'s assert. A board with
fewer than 120 objectives still reads as a sparse 12x10, banner and all. Reshaping it means
replacing the cursor bounds and slot arithmetic above per mode and rescaling the `Pos` spacing,
since the same functions serve every mode.

## Notes for modders

- **Extra modes.** Every mode-keyed function above indexes three-entry tables or asserts on
  `mode >= 3`: `gmGetClearcheckerTypeP`, the reward getters, `ClearChecker_GetRewardFromClearKind`, and the
  Prize1 animation index. Adding a checklist tab means covering each of them.
- **One question for every unlock.** Replacing `ClearChecker_CheckUnlocked` redefines unlocking
  game-wide. Writing `has_reward` alone does not, once that function is replaced.
- **Completion funnels.** Hooking both `SetNewUnlock` variants sees every gameplay completion. The
  five meta stores and the filler spend at 0x80180dbc are the only other writers.
- **Hooking a meta store.** A hook on one of the five `stb`s destroys the `r3 = 1` the function
  returns. Hook the `li r3, 1` before it instead, which a relocated instruction re-runs, and
  re-materialize the stored register (`r4` or `r0`) in the epilogue.
- **Moving rewards.** The reward loop indexes `clear[]` with each `RewardEntry.clear_kind`
  unchecked, so a table edited to move rewards between cells must keep every value below 120,
  and two rows on one cell both mark it. The hover text and the audio preview find a cell's
  reward by scanning the same field, so they follow the edit too.
