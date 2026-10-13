# Checklist Goals

A seed's victory condition is set per checklist row: each of Air Ride, Top Ride, City Trial and
the Archipelago tab carries its own goal, chosen in the slot options. The seed is won when at
least one row has a goal and every row that has one has met it. The mod evaluates the goals
whenever a check is recorded, latches each row once met, and publishes the result for the
client to report. It also keeps a checkbox filler from completing a goal's cell for free.

## Goal kinds

`APSlotOptions.goal[row]` is an `APGoalKind`. `checklist_amount[row]` and `goal_checks[row][2]`
parameterize two of them.

| Kind | Met when |
|---|---|
| `GOAL_100_CHECKLIST` | The row's own "Fill in over 100 Checklist blocks!" cell is checked: Air Ride 24, Top Ride 119, City Trial 55. Never on the Archipelago row, which has no such cell |
| `GOAL_N_CHECKLIST` | The row's checked count reaches `checklist_amount[row]` |
| `GOAL_CHECKLIST_LIST` | Every cell in `goal_checks[row]` is checked. An empty list is never met; a subset test against nothing would pass at once |
| `GOAL_HYDRA_AND_DRAGOON` | City Trial cell 119, "In one match, complete both Dragoon and Hydra!" - not the two parts cells |
| `GOAL_BEAT_KING_DEDEDE` | City Trial cell 47, VS. KING DEDEDE in under a minute |
| `GOAL_MAX_STATS_CT` | `APSave.max_stats_ct_achieved`, a sticky bit a rider proc sets when every stat of a human's machine reaches the patch-cap ceiling (`PatchCap_GetMax`) in one City Trial round. The proc runs only while the City Trial row carries this goal |
| `GOAL_ASSEMBLE_AP_STAR` | Archipelago cell 49, assembling the Archipelago Star |
| `GOAL_ALL_LEGENDARIES_CT` | Archipelago cell 50, Dragoon, Hydra and the Archipelago Star in one round |
| `GOAL_NONE` | Never evaluated; does not block victory |

A feat goal must not be free at connect. When the goal's feat depends on unlocks whose category
the seed leaves ungated, the apworld still ships them as items and sets
`APSlotOptions.goal_forced_gates`; the connect-time pre-fill of ungated categories then leaves
those bits locked - the six Dragoon and Hydra pieces (`GOALGATE_LEGENDARY_PIECES`), the
VS. KING DEDEDE stadium (`GOALGATE_VS_KING_DEDEDE`) and the six Archipelago Star spheres
(`GOALGATE_AP_STAR_PIECES`).

"Checked" means the cell's `sent_checks` bit, so a backfilled check counts like one earned in
play. The City Trial and Archipelago kinds read their fixed row whichever row carries them.
`GOAL_100_CHECKLIST` reads the vanilla auto-unlock cell rather than counting, so it binds to a
real checkbox the same way the other cell goals do; `GOAL_N_CHECKLIST` is the count.

## Evaluation

`APGoal_Evaluate()` runs after every newly recorded check, after a backfill, on save load, once
the slot options first arrive, and when `GOAL_MAX_STATS_CT`'s bit is set. It does nothing before
the options arrive, since every goal would read as kind 0. For each row with a goal not yet met,
it tests the goal and latches the row into `APSave.goal_latched` (bit per row). A latched row is
never tested again, so a goal stays met for the rest of the seed.

Each evaluation then publishes, for the client:

- `APData.goal_satisfied_mask` - `goal_latched` limited to rows that have a goal;
- `APData.goal_complete` - at least one row has a goal and all of them are latched.

Neither is saved; both derive from the latch. A newly met row shows "<Row> goal complete!" in
the row's name and tint (`APChecklist_RowName` / `APChecklist_RowColor`); when the evaluation
completes the whole set, a single "All Goals complete!" replaces those lines. Both need
Messages -> Local -> Goals, on by default.

## Filler gate

`Checklist_Think` refuses a checkbox filler on three fixed grid slots: 119 in every mode and, in
City Trial, 0 and 11 - where the game pins its auto-unlock cells, so a filler cannot trigger one.
Under reward shuffle those cells can hold real items, and a filler belongs anywhere the player
wants it, except where it would satisfy a goal without doing the objective.

A conditional hook at 0x80180a64 (the `lbz r3, 20(r31)` that loads the mode) replaces all three
refusals. Its prologue recomputes the cursor's slot, `col + row * 12` from `ClearCheckerUI`
`+0x17` and `+0x18`, into `r18`, where the code after the refusals expects it. It then asks
`FillerGate_IsRejected(mode, slot)`: on accept it re-runs the load and continues past the vanilla
compares (0x80180a9c); on refuse it branches to the error-sound path (0x80180c24).

A slot is refused only when it holds a goal cell of the row's current goal: the 100-blocks cell
for `GOAL_100_CHECKLIST`, cell 119 or 47 for the two City Trial cell goals (on the City Trial row
only), cell 49 or 50 for the two Archipelago goals (on the Archipelago row only), and every listed
cell for `GOAL_CHECKLIST_LIST`. Count goals refuse nothing, since a filler still costs a token.
Each goal cell is mapped to its slot through the row's `grid_mapping`. A custom tab other than
the Archipelago one is never refused.

## Testing goals

`options.goal[row]` comes from the seed, so testing a kind normally needs a seed with it. The
debug API overrides it: `APGoal_DebugSetGoals(goals, amount)` writes every row's goal at once,
applies `amount` to rows set to `GOAL_N_CHECKLIST`, clears the latch of each row whose goal
changed, evaluates once and writes the card; `APGoal_Get(row, &amount)` reads one back. It
takes the whole set because victory is judged over all rows: setting rows one at a time could
pass through a combination that completes the set - and reports it to the server - on a value
the user was only scrolling past. `archipelago_debug` drives it from its Goals page.

A latched row stays latched until a debug clear (`APChecks_DebugClearAll` or
`ChecklistRewards_DebugClearAll`), which also resets `max_stats_ct_achieved`.
`APGoal_DebugComplete` latches every row. The two City Trial cell goals complete only when the
vanilla results screen runs its evaluator, which the debug mod's end-the-round binding provides.
