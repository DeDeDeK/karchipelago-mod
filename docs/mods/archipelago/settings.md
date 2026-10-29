# Archipelago Settings

The archipelago mod's page in hoshi's settings menu, **Archipelago Settings**: the player's own
switches for the three links, the in-game messages, where permanent patches apply, and a few
gameplay conveniences. Nothing here is part of the seed. The values live in
`ap_menu_settings` (`APMenuSettings`) and are saved by hoshi's menu save rather than in
`APSave`; the slot options only seed the three link switches once.

The settings menu is a main-menu scene with no in-round overlay, so a change always lands before
the next round loads.

## The tree

| Path | Values | Default |
|---|---|---|
| Links -> Death Link | Off / On | Off, seeded on first connect |
| Links -> Trap Link | Off / On | Off, seeded on first connect |
| Links -> Energy Link -> Energy Link | Off / On | Off, seeded on first connect |
| Links -> Energy Link -> Machine Charge | Off / Slow / Med / Fast | Off |
| Links -> Energy Link -> Sources -> Objects / Patches / Charge | Off / On | On |
| Links -> Energy Link -> Spend | the energy shop | - |
| Messages -> Checks / Items / Hints / Status / Links | Off / On | On |
| Messages -> Chat | Off / On | Off |
| Messages -> Local -> Checks / Items / Links | Off / On | Off |
| Messages -> Local -> Goals | Off / On | On |
| Permanent Patches -> City Trial / CT Stadium / Air Ride | Off / On | On |
| Random Start Machine | Off / On | On |
| AP Box Rate | Rare / Low / Med / High | Med |
| Drop Ability | Off / On | On |
| Air Quick Spin | Off / On | On |
| On-Foot Zoom | Off / On | On |

Every change logs one `[Settings]` line from its `on_change`. hoshi also fires every value
option's `on_change` once at boot, after every mod's `OnSaveLoaded`, so the restored values log
too.

## Saving

hoshi stores each option as a row of the mod's save block, keyed by a hash of the option's name
and its parent menu's name - so the two "Checks" rows (under Messages and under Messages ->
Local) save separately, and renaming an option or its parent menu loses its saved value. Values
are copied in when the card is read, before the mod's `OnSaveLoaded`, and copied out before every
card write, so the settings go out with any save. A saved value out of range keeps the default.

## Seeding from the slot

The slot options carry the seed's starting choice for each link. On a save's first options write
(while `APSave.options_received` is still 0) the mod copies the options into the save and sets
Death Link, Trap Link and Energy Link from them. Later connections leave the settings alone, so a
player who turns a link off keeps it off. The seed reaches the card with the next save, like any
other change.

## The client mirrors

The link switches and the message filter are mirrored into `APData` for the client:
`deathlink_menu_enabled`, `energylink_menu_enabled`, `traplink_menu_enabled`, and
`text_menu_mask` (bit `1 << APTextKind` per Messages kind that is on). They are republished at
`OnSaveLoaded`, from each of those rows' `on_change`, and on every options write just before
`options_valid` is cleared. The client reads them every poll and joins or leaves the DeathLink,
TrapLink and EnergyLink server tags to match, so turning a link off also stops the server
delivering it. The Local rows are not mirrored.

## The rows

**Death Link and Trap Link** gate both directions. Each receive proc is created at round load
only when its switch is on at that moment; each send checks the switch when it fires.

**Energy Link** gates earning, Auto-Charge and the shop, which answers every purchase with "Turn
on Energy Link to spend energy" while it is off. Its procs are created at round load only when it
is on and re-check it every frame, since a first connect can seed it off mid-round. **Sources**
picks which activities earn energy; **Machine Charge** is Auto-Charge's rate.

**Messages** filters the lines the client composes, by `APTextKind`; **Local** filters the
lines the mod writes itself - "Check recorded", item and unlock announcements, goal lines, and
link lines. The local Checks, Items and Links default off because the client posts a fuller line
for the same event; Goals defaults on because nothing else names the row that finished.

**Permanent Patches** chooses the rounds in which received permanent patches are applied:
Trial rounds on the City Trial map, Stadium mode, and Air Ride. Received patches are kept in the
save either way.

**Random Start Machine** gives every slot of a City Trial Trial run a random unlocked machine;
off, everyone starts on the Compact Star, or a random machine while it is locked.

**AP Box Rate** sets how often an AP Box replaces a City Trial box drop.

**Drop Ability** lets a human press Z to discard a copy ability (or a Top Ride item power),
**Air Quick Spin** allows the quick spin in the air, and **On-Foot Zoom** gives the on-foot City
Trial camera the C-Stick zoom.
