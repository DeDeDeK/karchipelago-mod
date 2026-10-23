# Stat Shuffle Event

A custom City Trial event (`CUSTOM_EVKIND_STAT_SHUFFLE`, kind 28) that deals every player another
player's patch stats for 30 seconds, then hands each their own back. No player keeps their own set:
the shuffle is a random derangement over every occupied slot, humans and CPUs alike. It runs through
the `custom_events` framework with a siren intro, sky preset 1 (Midnight), BGM file 0x35
(`event_syoukinkubi`), an 1800-frame duration and roll weight 20. Its `can_start` needs at least two
occupied slots (`Ply_GetPKind` not `PKIND_NONE`), so a solo round never rolls it and `Do` refuses it.

## Where Stats Live

A City Trial player's nine patch stats exist in three copies, and the game copies one way each
frame in `Rider_UpdateSomePlayerData` (0x801913d4):

| Copy | Offset | Role |
|---|---|---|
| `MachineData.stats` | `+0x94c` | The source while riding; `Machine_GivePatch` writes it and `Machine_AdjustAttributes` reads it |
| `RiderData.stats` | `+0x74c` | Copied from the machine while riding; the source on foot |
| `PlayerData.stats` | `+0x44` | Copied from the rider every frame; seeds a respawned rider and is what `CityTrial_MinorExit` (0x8003fdd4) saves for the stadium |

Boarding and respawning copy the rider's stats into the machine (`Rider_PushStatsToMachine`,
0x80193e78), so a set follows its holder from machine to machine. The event reads the live source
and writes all three, then runs `Machine_UpdateAppearance` and `Machine_AdjustAttributes` on a
ridden machine as a patch pickup does - a higher HP stat raises current HP by the difference, a
lower one clamps it. Only the patch stats move. `stat_aux` (`PlayerData+0x68`), where a CPU's
passive stat growth lands, stays with its slot.

## The Shuffle

`start` saves each player's own stats, draws a uniform derangement - Fisher-Yates,
redrawn while any player draws their own index, which keeps at least a third of the draws - and
writes each player the set the permutation hands them. Both sets are kept per slot for the restore.
Players with equal stats can still look unchanged; the derangement is over who held a set, not its
values.

## The Restore

Each player gets back their own stats plus whatever they gained or lost on the borrowed ones:
`own + (current - borrowed)`.

- **Riding.** The restore starts with state 3. A riding player's machine gets their own stats
  written back, and the per-stat delta is replayed through `Machine_GivePatch` (0x801cacf4). Any
  patch cap hooked onto that function therefore still bounds the gains, so a set that had more room
  under the cap than the owner's own cannot carry them past it.
- **On foot.** A player without a machine waits, and is restored on the first frame of state 3 they
  ride. Whoever is still on foot when state 3 ends is restored directly into the rider and player
  copies, clamped to `Patch_GetMinValue`..`Patch_GetMaxValue` by `Machine_SetStatBlockClamped`
  (0x80194f64); that path bypasses any patch cap on `Machine_GivePatch`.
- **Scene exit.** The minor's exit callback, where the framework runs `abort`, runs before the major's
  `CityTrial_MinorExit` saves `PlayerData.stats` for the stadium. `abort` writes the
  restored stats into the player copies only, clamped the same way, so a round cut off mid-event
  sends its players to the stadium with their own stats.

A natural roll leaves at least 2200 frames in the round, and state 3 starts 1980 frames after the
trigger, so only a direct trigger can be cut off before the restore.
