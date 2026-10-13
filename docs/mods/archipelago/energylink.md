# EnergyLink

EnergyLink is the Archipelago link that pools "energy" across a multiworld. Here, playing earns
energy - destroying objects, collecting stat patches, charging the machine - and the pool can be
spent on items in an in-game shop or poured back into the charge meter. The mod keeps local
counts; the AP server holds the pool, and the client moves energy between them.

The Energy Link setting (Archipelago Settings -> Links -> Energy Link, seeded from the slot's
`energy_link` option on a save's first connect) gates all of it. Under it:

| Setting | Values | Does |
|---|---|---|
| Machine Charge | Off / Slow / Med / Fast | Auto-Charge rate |
| Sources -> Objects / Patches / Charge | Off / On each, default On | Which activities earn energy |
| Spend | submenu | The shop |

## On the wire

Energy is in whole MJ on the wire; the server's pool is in joules (1 MJ = 1,000,000 J).

| Field | Writer | Meaning |
|---|---|---|
| `energy_balance` (s64) | Client | The pool, written every poll |
| `energy_deposit_total` (u32) | Game | MJ earned this boot, only ever rising |
| `energy_withdraw_total` (u32) | Game | MJ spent this boot, only ever rising |

The game only adds to the two counters; the client reads them every 0.1 s, sends the net change
since its last read to the server, and never writes them. Any number of game-side changes
between two polls fold into one delta, so the game needs no flush and no handshake. They are
two rising u32s rather than one signed net value because a u32 store is atomic and a 64-bit one
is not: a read straddling a signed net crossing zero could decode as a huge positive and be sent
as a deposit. They reset at each boot.

Energy arriving from other players just raises `energy_balance` on the next poll, with no
in-game message.

## Earning

A per-frame proc on each human rider (attached at `On3DLoadEnd`, priority `RDPRI_HITCOLL + 1`)
compares this frame with the last, per player:

| Source | Measures | Worth |
|---|---|---|
| Objects | `PlayerStats.objects_destroyed_num` | 1 MJ each (City Trial; Air Ride never counts it) |
| Patches | The rise of each of the nine `RiderData.stats` values | 1 MJ per +1 |
| Charge | The rise of `MachineData.charge_value` | 5 MJ per full 0 -> 1 charge |

Only rises count. A source switched off still advances its baseline, so nothing it saw while off
is sent later. Fractions collect in a float carry that survives scene loads, and each whole MJ
goes onto the matching counter; the conversion goes through s32 because no float-to-s64 routine
is linked.

The first frame of a round only takes a baseline, and waits for the intro to end, so the
permanent patches applied at round start are not counted as earned. Stat patches the mod gives
directly (in Air Ride, and All Down) re-snap the baseline too; the City Trial pickups it spawns do
not, and refund a little energy - far less than they cost.

## Auto-Charge

With Machine Charge on and the pool above 0, each frame tops the meter up by at most a fixed
step - 0.00555, 0.01111 or 0.02222 for Slow, Med or Fast, about 180, 90 or 45 frames from empty
to full - and withdraws 5 MJ per full meter's worth. Capping the step makes it assist the
player's own charging rather than snap the meter full. Any step costs under 1 MJ, so any positive
balance covers it.

After injecting, the proc re-snaps its charge baseline; otherwise the injected charge would read
as earned charge next frame and mint back the energy it cost. Each withdrawal also lowers
`energy_balance` at once (carrying the fraction): the client only rewrites the balance on its
poll, about every six frames, and without the local decrement Auto-Charge would keep spending a
stale balance in between.

In Air Ride and City Trial an idle charge does not drain, so Auto-Charge fills the meter even
when A is not held. Auto-Charge skips Meta Knight's wing machine, which still earns energy from
charging.

## Spending

The Spend menu sells items by category - stat and permanent patches, copy abilities, food,
legendary pieces, City Trial events, Top Ride items, checkbox fillers and more - each at a fixed
price in MJ. A purchase queues the item's AP ID
through the same queue received items use, so it applies under the same scene rules, then adds
the price to `energy_withdraw_total` and subtracts it from `energy_balance` on the spot. Doing
both immediately matters: the shop is a main-menu scene with no gameplay frame to flush a
withdrawal later.

Prices are steep (from 200 MJ for an Apple to 50,000 for a checkbox filler) because a strong
City Trial round earns energy fast. Nothing sold is progression: abilities, events and Top Ride
items are sold as gives, never unlocks, and Patch Cap Increase and Spawn Rate Up are not sold.

A purchase is refused, with a text box line, when:

1. Energy Link is off;
2. it is an event whose unlock has not arrived - energy cannot fire an event the seed has not
   granted;
3. it is a legendary piece or a whole legendary the goal logic does not yet allow: every one
   needs the Red Box unlocked (the pieces ride the red carrier), a piece needs its own unlock, and
   a whole machine all of its pieces;
4. the balance is below the price;
5. the item queue is full.

Several players can spend the same pool before any of their withdrawals reaches the server; the
server floors the pool at 0, and the client logs that a withdrawal came up short. Nothing on the
game side can prevent it without asking the server first.

## Top Ride

Top Ride has no riders or machines, so a standalone proc walks the Top Ride Kirbys instead. Charge
is the only source there. Auto-Charge only adds while the player holds a charge
(`charge.is_charging && charge.charge_ready`): on every other frame `TopRide_ChargeUpdate`
drains the meter far faster than the step, so injecting outside a held charge is wasted. There is
no intro, so no baseline frame.

## Lifecycle

The procs are created at `On3DLoadEnd` / `OnTopRideLoadEnd` only while Energy Link is on, and
check it every frame, because a save's first connect can seed it off mid-round. Each load resets
the per-player baselines but not the counters or the carry, which live for the whole boot. The
settings menu only opens from the main menu, so no other setting changes mid-round.

`EnergyLink_GetBalance` / `EnergyLink_DebugSetBalance` back archipelago_debug's EnergyLink page.
The debug write sets the balance only - lowering a counter would read as a withdrawal of about
four billion MJ - and holds only while no client is attached.
