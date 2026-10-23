# Stadium Gating

Each of City Trial's 24 stadiums is locked behind an unlock item (400-423,
`AP_STADIUM_UNLOCK_BASE` + `StadiumKind`). A locked stadium is never chosen for a Trial's
closing round and is hidden from the stadium list. The mask is the u32
`APSave.stadium_unlocked_mask`; the vanilla unlock checks and the per-round selector are both
replaced outright, so the mask is the only authority.

| Group | Stadiums | IDs |
|---|---|---|
| Drag Race | 1-4 | 400-403 |
| Air Glider | | 404 |
| Target Flight | | 405 |
| High Jump | | 406 |
| Kirby Melee | 1-2 | 407-408 |
| Destruction Derby | 1-5 | 409-413 |
| Single Race | 1-9 | 414-422 |
| Vs. King Dedede | | 423 |

`GateStadiums_UnlockStadium` sets the bit, sets the stadium's vanilla "NEW" badge bit, and
announces "Unlocked Stadium: <name>". During the quiet re-grant of checklist rewards it sets only
the mask bit - a re-grant is not new.

## Availability

Vanilla answers "is this stadium available" through four functions, and all four are replaced
with the same mask read:

| Function | Vanilla |
|---|---|
| `Gm_StadiumIsDefaultUnlocked` (0x8000c148) | A jump table: every stadium but six is open by default (Drag Race 4, Kirby Melee 2, Destruction Derby 3-5, Single Race 9) |
| `Gm_StadiumIsUnlocked` (0x8000c17c) | Maps those six to their checklist rewards and asks the checklist |
| `Gm_StadiumIsAvailable` (0x8000c228) | Both of the above, with its own inlined copies of the tables |
| `Gm_StadiumCheckUnlocked` (0x80007ee4) | The runtime unlock bitfield, or its LAN-session snapshot during a session |

All four have to go because `Gm_StadiumIsAvailable` inlines its own tables rather than calling
the other two. The replacement returns 0 while `ap_save` is NULL, since `Gm_InitData`
(0x8000714c) asks before the save block exists. With these in place the game's own unlock
bitfield and its session cache no longer affect availability, and a mask change - a late
delivery, a debug toggle - takes effect at the next check. `Gm_StadiumCheckNewLabel`
(0x80008038) is left alone: the checklist screen still reads the vanilla "NEW" bitfield, which
is why the unlock sets it.

## Choosing the closing stadium

`CityTrial_DecideStadium` (0x8003f808) picks the stadium that ends each Trial. It follows the
menu's stadium selection: in shuffle mode it takes any available stadium not among the last four
picked; for a chosen group, any available stadium in that group; then a weighted roll from the
stadium weight table. The four-pick history is a latent vanilla bug once availability is
restricted - with fewer than five stadiums open, every candidate can be in the history, and the
roll becomes `HSD_Randi(0)`.

The replacement builds candidates from the mask and makes the same weighted roll, with two
differences: the history is sized `min(unlocked - 1, 4)`, so one stadium is always eligible, and
a selected group with nothing unlocked falls back to every unlocked stadium instead of
soft-locking. It records the pick in the history and in `GameData.city.stadium_kind` as vanilla
does.

## The stadium list

`CityTrial_BuildStadiumList` (0x80046df0) builds the list for the stadium-select UI through the
replaced check, but has two paths that re-add locked stadiums, both patched out:

- **A debug unlock-all**, taken at debug level 3 or more with R + D-Up held, which would badge
  every locked stadium "NEW". The `blt` at 0x80046e1c becomes an unconditional branch past it.
- **A checklist fallback**: when a stadium reads locked, the `beq` at 0x80046ef8 enters a block
  that asks the checklist and can add it back. It is retargeted to the next loop iteration.

## Vs. King Dedede and the goal

Knocking out King Dedede is a City Trial goal. With stadiums ungated his stadium would be in the
rotation from the first match, so when the seed's goal is that feat the apworld keeps his unlock
in the pool, sets `GOALGATE_VS_KING_DEDEDE`, and the ungated pre-fill leaves his bit clear while
opening the other 23.
