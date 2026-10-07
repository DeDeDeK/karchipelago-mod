# Archipelago Settings Menu

The archipelago mod's entry in hoshi's mod settings menu, **Archipelago Settings**: the
player-owned switches for the three multiworld links, the message filters, round-start
permanent patches and a handful of gameplay conveniences. Nothing here is part of the seed. The
values live in `ap_menu_settings` (`APMenuSettings`) and persist through hoshi's menu save, not
through `APSave`; the slot options only seed the three link toggles once, on a save's first
connect.

Implementation: `mods/archipelago/src/settings_menu.c` / `.h`. The tree is the `ModSettings`
`OptionDesc` that `mod_desc.option_desc` points at in `main.c`. The settings menu is a main-menu
scene with no in-round overlay, so a change always lands before the next round loads.

## The tree

| Path | Values | Default | Read by |
|---|---|---|---|
| Links -> Death Link | Off / On | Off, seeded on first connect | DeathLink sends and its round-load receive proc |
| Links -> Trap Link | Off / On | Off, seeded on first connect | TrapLink sends and its round-load receive proc |
| Links -> Energy Link -> Energy Link | Off / On | Off, seeded on first connect | EnergyLink deposits, Auto-Charge, the Spend shop |
| Links -> Energy Link -> Machine Charge | Off / Slow / Med / Fast | Off | Auto-Charge |
| Links -> Energy Link -> Sources -> Objects / Patches / Charge | Off / On | On | The matching EnergyLink deposit |
| Links -> Energy Link -> Spend | submenu | - | The energy shop |
| Messages -> Checks / Items / Hints / Status / Links | Off / On | On | Client-composed lines of that kind |
| Messages -> Chat | Off / On | Off | Client-composed chat lines |
| Messages -> Local -> Checks / Items / Links | Off / On | Off | Lines the mod writes itself |
| Messages -> Local -> Goals | Off / On | On | Goal lines the mod writes itself |
| Permanent Patches -> City Trial / CT Stadium / Air Ride | Off / On | On | `PermanentPatch_On3DLoadEnd` |
| Random Start Machine | Off / On | On | `GateMachines_FinalizeCTMachine` |
| AP Box Rate | Rare / Low / Med / High | Med | The AP Box roll in `ap_patches.c` |
| Drop Ability | Off / On | On | `drop_ability.c` |
| Air Quick Spin | Off / On | On | `air_quick_spin.c` |
| On-Foot Zoom | Off / On | On | `onfoot_zoom.c` |

The defaults are the `ap_menu_settings` initializer. The local Checks, Items and Links lines
default off because a connected client posts its own, richer line for the same event a poll
later.

Every change logs one `[Settings]` line from its `on_change` callback. hoshi also fires every
value option's `on_change` once at boot, after every mod's `OnSaveLoaded`
(`Menu_ExecAllOptionChange`), so the restored values print at boot as well.

## Persistence

hoshi keeps a mod's menu values in their own rows of the mod's save block, beside the `APSave`
user data. Each row is a 16-bit hash and a value. `Option_Hash` hashes the name of the option's
immediate parent menu concatenated with the option's own name - "Archipelago Settings" for the
root rows, "Links" for Death Link and Trap Link, "Energy Link" for its toggle and Machine Charge,
"Local" for the local message rows - so the two "Checks" rows under Messages and
Messages -> Local save separately.

- **Load.** `Mod_CopyFromSave` runs when the card's block is read and needs no re-init, before
  the mod's `OnSaveLoaded`. It writes each saved value over the default. A value at or past the
  option's `value_num` keeps the default, and an option whose hash is not in the block keeps it
  too - which is what renaming an option, or its parent menu, does to its saved value.
- **Store.** `KARPlusSave_Write` calls `Mod_CopyAllToSave` before every card write, so the
  current values go out with any `Hoshi_WriteSave`, not only the settings menu's own write on
  exit.

Spend's purchase rows are actions and store nothing.

## First-connect seeding

The slot options carry `death_link_enabled`, `energy_link_enabled` and `trap_link_enabled` as
the seed's starting choice for each link. `APOptions_OnFrameStart` copies the options into the
save on the client's first options write for a save - while `ap_save->options_received` is still
0 - and on that write only calls `SettingsMenu_SeedFromSlotOptions`, which sets Death Link and
Trap Link from their flags and the Energy Link toggle from its flag. Later connects leave the menu
alone, so a player who turns a link off keeps it off across reconnects.

The seed is not written to the card on the spot. It reaches the card with the next save write,
like any other menu change.

## Publishing to the client

`SyncMenuStateToAPData` copies the link toggles and the message filter into `APData` for the
client:

| `APData` field | Offset | Value |
|---|---|---|
| `deathlink_menu_enabled` | 0x294 | Death Link |
| `energylink_menu_enabled` | 0x298 | Energy Link (`SettingsMenu_EnergyLinkEnabled`) |
| `traplink_menu_enabled` | 0x29C | Trap Link |
| `text_menu_mask` | 0x2A4 | Bit `1 << APTextKind` set for each Messages kind that is on |

It runs from `OnSaveLoaded` once the saved values are in place, from the `on_change` of every
link and Messages row, and on every client options write, first connect or not. That last call
comes just before `options_valid` is cleared, and the client reads the clear as the mirrors now
holding the player's choices. From then on the client reads the mirrors every poll and joins or
leaves the DeathLink, TrapLink and EnergyLink server tags to match, so turning a link off also
stops the server delivering it. The Local rows are not mirrored; they never leave the game.

## The links

**Death Link** and **Trap Link** gate both directions. The receive side is a proc created at
round load (`On3DLoadEnd`, `OnTopRideLoadEnd`) only when the toggle is on at that moment; the
send side checks the toggle each time it would send.

**Energy Link -> Energy Link** is the link itself. Off sends no energy, runs no Auto-Charge,
and makes the Spend shop refuse every purchase with "Turn on Energy Link to spend energy". The
per-frame procs are created at round load only when it is on, and they check it again every
frame, because a save's first connect can seed it Off mid-round. While it reads Off they send
nothing; the seed runs once per save, so it does not come back on before the next round.

**Energy Link -> Sources** chooses which activity earns energy while the link is on: Objects
(destroyed objects, City Trial only), Patches (stat gains) and Charge (charge-meter gains). A
source that is off still advances its per-frame baseline, so nothing it counted while off is
sent later.

**Energy Link -> Machine Charge** is Auto-Charge's rate (`APAutoCharge`), and only runs while
the link is on.
`SettingsMenu_AutoChargeRate` maps Slow / Med / Fast to rates 0-2 (-1 for Off). Each frame
Auto-Charge adds at most 0.00555, 0.01111 or 0.02222 to the charge meter - about 180, 90 or 45
frames to fill from empty - and only while the pool balance is positive, withdrawing 5 units of
energy per full meter. Meta Knight's wing machine is skipped, since its `charge_value` is a raw
speed term with no meter behind it. In Top Ride, where the charge decays fast whenever A is not
held, Auto-Charge only tops up a held, ready charge.

**Spend** is the energy shop: categories of items priced in energy, each bought through the AP
item queue like a received item.

## Messages

The six kind rows filter the lines the client composes - Checks (which item a completed square
sent, and to whom), Items, Hints, Status (goal, release, collect and client connection changes),
Chat, and Links (DeathLink and TrapLink traffic). The client skips composing a kind whose bit is
clear in `text_menu_mask`, and `APText_Render` drops one whose row is off anyway, so the menu
wins even against a client a poll behind.

**Local** filters the lines the mod writes itself, with or without a client:

| Row | Gates |
|---|---|
| Checks | "Check recorded" as a checklist square completes (`RecordCheck`, `ap_checks.c`) |
| Items | Every line for an item applied from the AP queue, through `APAnnounce_Grant` / `APAnnounce_GrantSegments`: unlocks, gives, permanent patches, Patch Cap and Spawn Rate increases, Big / Small Kirby, checklist rewards and fillers |
| Goals | "<Row> goal complete!" and "All Goals complete!" from `APGoal_Evaluate` |
| Links | "DeathLink sent!" / "received!" and "TrapLink sent! (kind)" / "received!" |

The checklist-reward regrant at save load (`ap_regrant_quiet`) is silent whatever Items says.
The Spend shop's replies ("Bought ...", "Not enough energy ...") answer a menu action directly
and are not filtered.

## Gameplay rows

- **Permanent Patches** - whether received permanent patches are re-applied at round start:
  City Trial for Trial rounds on the city map, CT Stadium for stadiums picked from Stadium mode,
  Air Ride for every Air Ride round. Free Run and a Trial's closing stadium never apply them
  whatever these say. Received permanent patches are recorded in the save either way.
- **Random Start Machine** - City Trial Trial only. `GateMachines_FinalizeCTMachine`, hooked at
  `0x8002dea0` in `CitySelect_InitPlayerMachines` (`0x8002ddd8`), gives every human and CPU slot
  a random unlocked Kirby machine when on, and the Compact Star when off - or a random unlocked
  machine if the Compact Star is still locked.
- **AP Box Rate** - how often an AP Box replaces a City Trial box drop. Each step is a share of
  the box spawn ticks plus a floor on the gap between two AP Boxes: Rare 3% / 80 s, Low 6% / 40 s,
  Med 12% / 20 s, High 20% / 12 s, the floor divided by the spawn-rate scale.
- **Drop Ability** - Z discards a human's copy ability in City Trial and Air Ride, and a held
  ability power (Fire, Freeze Fan, Bomb, Walky) in Top Ride.
- **Air Quick Spin** - the quick spin also fires while airborne, in City Trial and Air Ride.
- **On-Foot Zoom** - the C-Stick's Y axis zooms the on-foot City Trial camera as it does the
  machine camera.
