# Gameplay Options

Three conveniences on the Archipelago Settings page that change how the game plays, independent
of the seed: **Air Quick Spin**, **Drop Ability** and **On-Foot Zoom**. All three default on, none
has items or save state, and turning one off restores vanilla. The settings menu only opens from
the main menu, so each is effectively fixed for a round.

## Air Quick Spin

Vanilla allows the quick spin - the stick-flick spin attack - only on the ground. The option
reinstates it in the air for all three riders, in City Trial and Air Ride.

**How the spin works.** `Rider_UpdateQuickSpinTimers` (0x80191a58) ticks two per-rider
counters, frames since the stick was last pushed hard right and hard left, and
`Rider_CheckQuickSpinInput` (0x80191980) reports a spin when one is at 0 while the other is still
inside the flick window. Each rider's grounded state logic runs an earlier interrupt check, then
the tick, then that rider's quick spin interrupt, skipping the last two when the earlier check
fired. The airborne logic stops short, differently per rider:

| Rider | Airborne logic | Missing |
|---|---|---|
| Kirby | `airControl` (0x801ac128) | The quick spin check, after the Tornado spin check; a dead `cmpwi r3, 0` is left where the grounded build branches |
| Dedede | `Rider_Dedede_AirControl` (0x801bf534) | The quick spin check after the tick, with nothing left over |
| Meta Knight | `Rider_MetaKnight_AirControl` (0x801c2b08) | The tick and the check after his charge check, with a dead `cmpwi r3, 0` |

This is also why the Tornado ability spins in the air in vanilla: its check
(`Rider_IASACheck_TornadoSpin`, 0x801b4d04) is still called.

**Hooks.** Kirby and Meta Knight get a hook on their dead `cmpwi` (0x801ac170, 0x801c2b28), where
`r31` is the `RiderData` and `r3` the result the grounded build would branch on (the Tornado spin
for Kirby, the charge for Meta Knight). The body reproduces the grounded guard and calls the
rider's quick spin interrupt; Meta Knight's ticks the counters first, since his air state never
does. Dedede has no dead slot, so his airborne `bl Rider_UpdateQuickSpinTimers` at 0x801bf560,
already the last call of the branch where his grounded logic checks the spin, is replaced with a
tick followed by the check.

The calls go through the same quick-spin enters the base-ability gate wraps, so a quick spin
locked by Archipelago stays locked in the air. Like the grounded spin, it works for every rider,
CPUs included. Top Ride's Kirby is a separate object system and is unaffected.

## Drop Ability

A human presses Z to discard a held ability on the spot: the copy ability in City Trial and Air
Ride, the held item power in Top Ride. Humans only, so the attract demo is untouched. Each drop
logs one `[DropAbility]` line.

**City Trial and Air Ride.** A proc created at `On3DLoadEnd` (only while the option is on) watches
each human with a copy ability for a Z rising edge in `RiderData.input.down`, then runs the same
two steps the game's ability timers end with:

1. `Rider_AbilityRemoveModel` (0x80191554) runs the installed removal callbacks, which end in
   `Rider_RevertCopyAbility` (0x801a7d70): the hat comes off, the ability clears and the poof
   plays.
2. `RiderState_LoseAbilityEnter` (0x801b0adc) plays the spit-out animation, which only animates.

Using the generic teardown rather than one ability's own means every ability drops the same way,
including Wheel and Wing, which keep a removal callback installed. The proc runs in every 3D
round, stadiums and Free Run included.

**Top Ride** has no copy abilities; its analogs are the four item powers
`TopRide_KirbyApplyItem` (0x802d8cb4) installs - Fire, Freeze Fan, Bomb and Walky. While one is
active, the Kirby's state handler carries that power's state vtable, so the proc (created at
`OnTopRideLoadEnd`, waiting for `round_state == 2`) identifies a held power by comparing against
the four. A Top Ride Kirby reads its pad through its own input reader, so the proc reads
`stc_engine_pads[port].down` directly, with the port from the slot's
`topride_config.slots[].controller_port`. The drop calls `TopRide_KirbyNormal`, which leaves the
current state through its teardown and removes the power's aura and model - the same revert the
game runs when one power replaces another - and clears `active_item_kind`.

## On-Foot Zoom

In vanilla the C-Stick zooms the camera only while riding; on foot it still orbits the camera,
but the zoom axis does nothing. The option gives the on-foot camera the machine camera's zoom.

**The camera.** `PlyCamGObj_Think` (0x800b3540) drives each player's `CamData` per frame through
callbacks picked by `CamData.kind` (1 riding, 6 on foot, 9 on a rail). Every frame, for every
kind, `PlyCam_MachineZoomAdjust` (0x800b61f4) rebuilds the final eye and interest from
`rotation_amt`, `zoom_amt` and `interest_raise` - but only while `CamData.zoom_enabled` is set;
otherwise it passes the solved camera through. The riding and rail input thinks call
`cameraControlThink` (0x800b67cc), which fills those fields from the C-Stick (X orbits, Y
accumulates zoom at `cmMainParamCommon.zoom_speed`, clamped to `zoom_dist_min..zoom_dist_max`,
with the interest raised in proportion) and sets `zoom_enabled`. The on-foot input think,
`PlyCam_OnFootThink` (0x800cb3b4), reads only the X axis, into its own orbit, and never sets
`zoom_enabled`; `PlyCam_SwitchKind` clears it on every kind change.

**Hook.** One hook at 0x800cb4dc, the branch that ends `PlyCam_OnFootThink`'s C-Stick handling -
past its gates for a pending rail transition, a locked camera and the title screen - so the zoom
is live exactly when the on-foot orbit is. With the option on and a target, the body sets
`zoom_enabled` and runs `cameraControlThink`'s Y-axis half against the same parameters, deadzone
included, so the on-foot zoom matches the machine camera's range and speed. The epilogue restores
the function's 0 return in `r3`. With the option off it does nothing, and the camera keeps
vanilla framing.

The on-foot camera only exists on the open City Trial map, the one place a rider leaves the
machine. `PlyCam_MachineZoomAdjust` saves a player's zoom distance only for the riding and rail
kinds, so on-foot zoom never overwrites the machine camera's, and each dismount starts from the
default on-foot framing.
