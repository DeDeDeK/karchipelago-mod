# TrapLink

TrapLink shares traps across a multiworld. When something bad happens to a human player here -
a bad patch picked up, Sleep caught, a Speed Down grabbed in Top Ride - the mod tells the client,
which bounces a named trap to every other TrapLink player; when a trap arrives from elsewhere,
the mod springs one of its own on the local humans. The Trap Link setting (Archipelago Settings
-> Links, seeded from the slot's `trap_link` option on a save's first connect) gates both
directions.

## On the wire

The game writes a `TrapLinkKind` into `APData.traplink_send` and the client clears it, sending a
Bounce tagged `TrapLink` with `{time, source, trap_name}`:

| Value | Kind | `trap_name` | Sent when |
|---|---|---|---|
| 1 | `TRAPLINK_KIND_BAD_PATCH` | Bad Patch | A human picks up a bad or fake patch in City Trial |
| 2 | `TRAPLINK_KIND_SLEEP` | Sleep | A human Kirby gains Sleep in City Trial or Air Ride |
| 3 | `TRAPLINK_KIND_SPEED_DOWN` | Speed Down | A human Kirby picks up a Speed Down in Top Ride |

The field is a value, not a queue, and the game writes it without waiting: several triggers
inside one 0.1 s poll - a box burst of bad patches - go out as one Bounce of the last kind
written. For an incoming Bounce the client ignores `trap_name` (KAR's traps map onto no other
world's names), counts it, and writes `traplink_receive = 1` whenever the field reads 0.

Two choices differ from other worlds' TrapLink: any incoming trap applies some local trap rather
than being dropped when its name is unknown, and a trap that cannot apply yet is held - through
the 3D intro, a Top Ride countdown, a round with every human on foot, menus and the attract
demo - and lands in the next round that can take it, rather than being discarded.

## Sending

`TrapLink_Send(kind)` is the single entry. It does nothing with Trap Link off or during the
receive guard below; otherwise it writes the kind and prints "TrapLink sent! (kind)" under
Settings -> Messages -> Local -> Links. Each trigger filters to humans itself, because the two
engines tell humans apart differently.

**Bad patch.** A hook at 0x801db504 in `Machine_OnTouchItem` (0x801db34c) sits on the branch
taken when `CityItem_IsGoodPatch` (0x802540a8) returns 0. That reads the item's effect group,
which is "bad" for every stat down, Speed Min and Charge None, and "fake" for the eight fakes,
so all of them land here. The rider comes from the machine (`Machine_GetRiderPly` returns 5 for
a riderless machine, which is dropped).

**Sleep.** Not a hook of its own: the ability gate's replacement for
`RiderGObj_CheckAndGiveAbility` sends when a human actually gains `COPYKIND_SLEEP` from a pickup
or an enemy - after `Rider_GiveAbility` reports success, since the grant fails in some states.

**Speed Down.** A hook at 0x8034c7dc in `TopRideItem_Update` (0x8034c130), where an item has
just been absorbed, receives the item's kind and the absorber's position. Nothing there names
the Kirby, so the mod takes the Kirby whose `charge.position` is nearest the absorber, and sends
when it is human. (`TopRideKirby.position` is the spawn point and never moves.) Speed Down is the
only Top Ride item that debuffs the one who picks it up; every other item buffs the picker, arms
an attack, or hits someone else.

## Receiving

A per-frame proc (p_link 0), created at `On3DLoadEnd` and `OnTopRideLoadEnd` only while Trap
Link is on at that moment, applies a pending trap once the 3D intro is over (Top Ride has no
intro and reads as over). What it does depends on the scene major:

| Where | Trap |
|---|---|
| City Trial map | One of the trap items, below |
| City Trial stadium | Sleep, as in Air Ride - stadium riders are always mounted |
| City Trial Free Run | None; the trap is dropped and cleared, since Free Run loads no item data |
| Air Ride | Sleep on every human Kirby that is on a machine |
| Top Ride | Speed Down on every human Kirby, once `round_state == 2` |
| Anything else (menus, the attract demo) | Held |

**City Trial's trap items** are Sleep, Speed Min, Charge None, the eight stat downs, the eight
fake patches, 1 HP and Drop Patches. The proc shuffles them and offers each to the item handler
in turn within the same frame until one applies, so a trap that cannot apply right now does not
hold the flag for frames. No event is a candidate: an event outlasts the receive guard, and a
received Fake Powerups would scatter fakes that send traps straight back.

**Sleep** goes straight through `Rider_GiveAbility` (via `Ability_GiveHumans`), skipping the
ability gate and its own Sleep trigger, and skipping riders on foot, whose sleep animation would
read a null machine. **Speed Down** goes through `TopRide_KirbyApplyItem`, which installs the
debuff directly with no item spawned.

When a trap applies the proc clears the flag and prints "TrapLink received!" under Local ->
Links; when nothing applied the flag stays set for a later frame.

### The receive guard

A received trap can re-fire a send trigger: a bad patch the City Trial trap spawns is touched on
the spot, inside the same call, and a fake patch a frame or more later by collision. So before
applying, the proc arms a 120-frame window during which `TrapLink_Send` does nothing, and puts
the old value back if nothing applied. The window ticks down every frame the proc runs and resets
at each load. It is a recursion guard, not a rate limit, and it also mutes genuine sends during
the window.

## Debug

`ArchipelagoAPI.DebugTriggerTraplinkReceive` sets `traplink_receive` as the client would.
archipelago_debug binds it to **R + D-Pad Up** and to Links -> Arm TrapLink; the receiving round
picks the trap, exactly as for a real Bounce.
