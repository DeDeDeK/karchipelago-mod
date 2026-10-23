# DeathLink

DeathLink ties deaths together across a multiworld: when a human player dies here the mod tells
the client, which bounces a death to every other DeathLink player, and when a death arrives from
elsewhere the mod kills the local humans. City Trial, Air Ride and the stadiums share one
implementation; Top Ride, which has no riders, machines, HP or falls, has its own send trigger
and its own idea of "death".

The Death Link setting (Archipelago Settings -> Links, seeded from the slot's `death_link`
option on a save's first connect) gates both directions. On the wire a death out is
`APData.deathlink_send = 1` and a death in is `deathlink_receive = 1`; the client clears the
first, the game the second, and several deaths in either direction inside one poll collapse
into one.

## Sending

Three hooks report a death:

| Site | In | Death |
|---|---|---|
| 0x801a06d0 | `Rider_CheckToDieOnMachine` (0x801a06a8) | The machine ran out of HP |
| 0x801e6540 | `Machine_SetFallDead` (0x801e6520) | The machine fell off the course |
| 0x80331a94 | `TopRideSandPit_Update` (0x80331564) | Top Ride's sand-pit enemy on the Sand course spat out a Kirby it swallowed |

Each requires the setting on and the player's echo window (below) clear, then filters to humans:
`Ply_GetDescPKind` in 3D, `TopRide_GetPlayerKind == TR_PKIND_HMN` in Top Ride, where the send
also waits for the race to be running (`round_state == 2`). Every send also prints "DeathLink
sent!" under Settings -> Messages -> Local -> Links (off by default; the client's own line names
the players).

The fall hook sits 0x20 into `Machine_SetFallDead`, where the argument registers are still live:
`r4` for the clobbered `stw r4, 0x1b48(r31)`, `r5` for the respawn point copy, `r3` for the
machine class's fall-dead callback. Its prologue saves `r4` / `r5` and its epilogue restores all
three. The sand-pit hook replaces the load of the eject call's vtable slot and rebuilds that
call's arguments (`r3`-`r7`, `r12`) in its epilogue. The pit is the only Top Ride death proxy:
lava burns re-fire every frame, the spin and crush states are not scenery deaths, and Top Ride
courses have nowhere to fall.

### Echo suppression

A received death kills through the same paths the send hooks watch, and must not bounce back
out. A guard around the kill call cannot do it, because the HP path is asynchronous: `Ply_SetHP`
(0x8022ca38) only writes HP, `Machine_OnKO` (0x801e568c) sets the dead flag on a later machine
frame, and `RiderThink_DmgApply` (0x8018fa20) reaches the send hook later still. So each player
has a 60-frame countdown, armed just before the mod kills them, ticked by the receive proc, and
consumed by the first send it blocks. 60 frames covers that chain with margin and stays well
inside the 150-frame respawn, so a real death can never fall in the window. Both load-end hooks
clear it.

## Receiving in 3D

A per-frame proc (p_link 0) created at `On3DLoadEnd` - only when Death Link is on at that moment
- waits for the round's intro to end, then on `deathlink_receive == 1` kills every human who is
on a machine. It clears the flag only after killing someone: with every human on foot, or no
human in the round (the title's attract demo), the flag stays set and the death lands later. The
kill depends on the round:

| Round | Kill |
|---|---|
| City Trial map, Destruction Derby, Kirby Melee 1-2, Vs. King Dedede | HP to 0 |
| Air Ride and the other stadiums | Fall death at the player's checkpoint |

**HP death.** Before `Ply_SetHP(ply, 0)` the mod records the death with `Ply_AddDeath`
(0x8022f648), passing a copy of the machine's damage log with the attacker set to "nobody" so no
player is credited the KO. `Ply_AddDeath` tallies by vanilla machine kind, so for a machine
custom_machines registered it is handed the `VCKIND_WHEELVSDEDEDE` slot instead. Because this
direct call bypasses the KO callback, the mod also tells its AP checklist objectives the player
was knocked out.

**Fall death.** `Machine_SetFallDead(md, -1, pos)` respawns the player at their checkpoint, as
falling off would. `pos` is `MachineData.respawn_pos`, or `backup_respawn_pos` when
`use_backup_checkpoint` is set (the last good checkpoint, kept when the per-frame lookup fails).
Both hold course-spline parameters (segment, progress, height), not a world position, which is
why the machine's collision position cannot be used instead. The ground handle -1 is vanilla's
own "no dead-zone surface" case, the path `Machine_CheckFallDeath` (0x801e6464) takes for an
out-of-bounds fall. The HP-death stadiums have no out-of-bounds spline to respawn on, which is
why they use HP.

## Receiving in Top Ride

The Top Ride proc, created at `OnTopRideLoadEnd` under the same setting, waits for
`round_state == 2` - the Kirby state machine is not wired up during the countdown - then picks
one stun state at random and puts every human Kirby in it:

| Wrapper | State |
|---|---|
| `TopRide_KirbyPress` | Flattened |
| `TopRide_KirbyFreeze` | Frozen |
| `TopRide_KirbyNumb` | Paralyzed |
| `TopRide_KirbyConfuse` | Controls scrambled |

Speed Down is left to TrapLink. The Kirby's charge velocity is zeroed before
and after the call: some setters rescale it into a launch, and others normalize a zero vector
into NaN. As in 3D, the flag clears only once a human Kirby was hit.

## Debug

`ArchipelagoAPI.DebugTriggerDeathlinkReceive` sets `deathlink_receive` as the client would.
archipelago_debug binds it to **L + D-Pad Down** and to Links -> Arm DeathLink. The flag waits
for a round with a receive proc, so one armed from the menu lands in the next round.
