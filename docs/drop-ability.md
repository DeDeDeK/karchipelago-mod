# Drop Ability

With the **Drop Ability** setting on, a human player presses Z to discard a held ability on the
spot, rather than waiting for the game to end it: the copy ability in City Trial and Air Ride,
the held ability power in Top Ride. It is a convenience, not an Archipelago gate - no items, no
save state.

Implementation: `mods/archipelago/src/drop_ability.c`, with the option in
`mods/archipelago/src/settings_menu.c`.

## City Trial and Air Ride

`DropAbility_On3DLoadEnd` creates a bare per-frame GObj running `DropAbility_PerFrame`. Each frame it walks the human players (`Ply_GetPKind ==
PKIND_HMN`), skips any whose `RiderData.copy_kind` is `COPYKIND_NONE`, and acts on a Z in
`RiderData.input.down` (+0x3e4), the rider's rising-edge button word.

The drop is the same two steps the engine's own ability timers end on - a teardown, then the
lose-ability state (`abilityTimer_Ice_checkIf0`, for one, runs `abilityTimer_Ice_remove` then
`RiderState_LoseAbilityEnter`):

1. `Rider_AbilityRemoveModel` (`0x80191554`) runs the installed `cb_status_remove` and
   `cb_ability_remove` teardowns. They end in `Rider_RevertCopyAbility` (`0x801a7d70`), which removes
   the hat and runs `Rider_TeardownCopyAbility`, setting `copy_kind` to -1 with the poof effect
   and SFX.
2. `RiderState_LoseAbilityEnter` (`0x801b0adc`) plays the spit-out animation. It only animates;
   the teardown is what actually clears the ability.

Using the generic teardown rather than one ability's own remove means every copy kind drops the
same way, including the two that keep a persistent `cb_ability_remove` (Wheel, Wing).

## Top Ride

Top Ride has no copy abilities; its analogs are the four item powers `TopRide_KirbyApplyItem`
installs - Fire, Freeze Fan, Bomb and Walky. While one is active the kirby's `state_handler`
carries that power's state vtable (`TR_ITEMPOWER_VT_FIRE` `0x804db288`,
`TR_ITEMPOWER_VT_FREEZE_FAN` `0x804dc6e4`, `TR_ITEMPOWER_VT_BOMB` `0x804db088`,
`TR_ITEMPOWER_VT_WALKY` `0x804dc150`), so `TopRide_KirbyStateVtable` identifies a held power by
comparing against the four `power_vt`s in `topride_ability_items[]` (`gate_topride_items.c`), the
same table that pairs each item with its copy ability for gating. Any other state - including every
other item - is left alone.

`DropAbility_OnTopRideLoadEnd` creates the per-frame GObj running `DropAbility_TopRidePerFrame`.
It waits for `TopRideKirbyMgr.round_state == 2`, since `kirbys[]` and `state_handler` are wired
only once the race is active. A Top Ride kirby reads its pad through a polymorphic
`input_reader` rather than a `RiderData` input block, so the drop reads the pad directly:
`stc_engine_pads[port].down`, with `port` taken from the slot's
`topride_config.slots[player_slot].controller_port` in `GameData` (a port of 4 or above is
skipped).

The drop calls `TopRide_KirbyNormal` - the Kirby-class method at vtable slot 50 (wrapper
`0x802da0f4`), which exits the current state through its teardown and removes the power's
aura, model and effects, the same revert the engine runs when one item power replaces another -
and sets `active_item_kind` to 0xFF (none).

## Scope

Humans only, in both halves: a CPU never loses an ability this way, and the title screen's
attract demo, which has no human, is untouched. The CT / AR proc runs in every 3D scene, City
Trial stadiums and Free Run included.

## Menu

`ap_menu_settings.drop_ability_enabled` (`APMenuSettings`), the **Drop Ability** On/Off toggle
in the Archipelago Settings menu. Default **On**. The settings menu only opens from the main
menu, so the toggle is read once per round: the archipelago mod's load-end hooks call
`DropAbility_On3DLoadEnd` and `DropAbility_OnTopRideLoadEnd` only while it is on. Each drop logs one
`[DropAbility]` line naming the player and what was dropped; toggle changes log through
`OnToggleDropAbility`.
