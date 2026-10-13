# Item Delivery

How an Archipelago item becomes something in the game. The client hands item IDs over one at a
time; the mod banks each in a save-backed queue and applies it when the game is in a state that
can take it - an unlock in any scene, a stat patch only once a round is underway. This doc covers
the queue, the scene each kind of item waits for, and how each give lands. The wire side (the
mailbox, the ID ranges) is the client protocol; each gating category's unlock is described with
its gate.

## Receipt and the queue

The client writes an AP item ID into `APData.incoming_item_id`. Every frame, from
`OnFrameStart` (so in every scene and through every pause), the mod takes it:

1. It appends the ID to `APSave.unprocessed_items`, increments `APSave.item_received_count`,
   mirrors that to `APData.item_received_index`, and clears the mailbox.
2. It walks the queue and resolves at most one item, the first whose handler does not answer
   *retry*. An applied or dropped item is removed (swap with last); a retried one stays, and the
   items behind it still get their turn.

The queue holds `MAX_RECEIVED_ITEMS` (512). When it is full the mod leaves the ID in the mailbox
and counts nothing, so the client - which only advances after the mailbox empties - waits rather
than losing the item. Because the queue and the count are in the save, an item received but not
yet applied survives a power-off.

Two other producers share the queue through `ArchipelagoAPI.QueueItem`: EnergyLink purchases
and the debug mod's gives. A queued item is applied exactly like a received one.

Each handler answers one of three:

| Result | Meaning |
|---|---|
| `AP_ITEM_APPLIED` | Done, remove it |
| `AP_ITEM_RETRY` | Not now - wrong scene, no rider to give it to, an event already running |
| `AP_ITEM_DROP` | Never: an unknown ID, a reward index past the mode's count, a give whose mod is not in the build |

## Where each item can apply

`APItems_HandleItem` is a ladder. Where a range sits on it decides what it waits for:

| Items | Applies |
|---|---|
| Checkbox fillers, Patch Cap Increase, Spawn Rate Up, checklist rewards, every unlock, permanent patches | Anywhere, at once - they only write the save |
| Big / Small Kirby | Once a round is underway in any mode, Top Ride included |
| Top Ride item gives (950-971) | In a running Top Ride race |
| A copy ability received during Top Ride | As its Top Ride item, in a running race |
| Copy abilities | In an Air Ride or City Trial round past its intro, stadiums and Free Run included |
| Everything else | The same, but not in City Trial Free Run or a stadium |

"A round past its intro" is major `MJRKIND_CITY` or `MJRKIND_AIR`, minor `MNRKIND_3D` and
`Gm_GetIntroState() == GMINTRO_END`. The minor matters: the character select shares the major,
and the intro state reads `GMINTRO_END` outside 3D. Free Run and the stadiums load no item data
tables, so any give that spawns an item would fault in `Item_GetItDataPtr` there; those wait for
an open City Trial or Air Ride round.

## How the gives land

Every give targets each human player (`PKIND_HMN`) that has the needed object, and retries when
none does.

**Boxes (300-302)** spawn ten units ahead of each human's machine along its forward vector, so
the player drives into them, carrying their own color and a size rolled off the stage's box
table. Breaking one opens that color's pool for the vanilla 1 / 2 / 4 items. City Trial map only.

**The nine +1 stat patches** spawn a real pickup on each human in City Trial, which applies the
same frame, and call `Machine_GivePatch` directly in Air Ride. **All Up (7)** does the same with
All Up pickups; **All Down (9)** always applies directly. Direct applies re-snap EnergyLink's
stat baseline so the change mints no energy; the City Trial pickups do not, and refund a little.

**Other City Trial items** - stat downs, the max/min items, food, Candy, Fireworks, Panic Spin,
Sensor Bomb, Gordo, the six legendary pieces, the fake patches - spawn on each human's machine
through hoshi's `SpawnItemHumans`, City Trial map only. Real items are touched
(`Machine_OnTouchItem`) as they spawn so they apply the same frame; the fakes are left for the
next frame's natural collision, because a touch outside the collision pass logs a hit the next
pass clears before acting on it. The mod also replaces
`ItemGObj_ProcessFakeItem` (0x802542dc), which vanilla leaves inert until the round's first Fake
Powerups event, so a received fake patch works in any round; it reads the fake table from the
stage's event config, which is loaded even with events off.

**Copy abilities (328-338)** never spawn a pickup. `Ability_GiveItem` calls `Rider_GiveAbility`
on each human Kirby rider that is on a machine (an ability given on foot crashes in the sleep
animation's machine read). This bypasses the ability gate on purpose: a received ability is the
item itself, and it reaches every 3D mode because it touches no item data.

**Events (200-215)** start through `CTEvent_Give`: `CityEvent_ForceStart` (0x800ee778) without
its reserve queue. It waits while another event runs, off the city stage, when the event's own
check refuses, and for a once-only event that already ran this round - the stage holds a
once-only event's resources for one run, and a second Restoration Area overflows the collision
zone pool. It does not consult the event unlock mask: a received event fires even while locked.

**Give Dragoon / Hydra (10, 11)** run the legendary assembly cutscene through custom_machines,
for the first human it accepts; **Give Archipelago Star (14)** runs the star's assembly through
the ap_star mod. **Sphere gives (980-985)** add the sphere to every human's collected set
through ap_star, and the sixth completes the star. None of these needs the matching unlock, and
each is dropped in a build without the mod that owns it.

**Top Ride item gives (950-971)** call `TopRide_KirbyApplyItem` on each human Kirby - a direct
apply, not a spawned item - and wait for `round_state == 2`, since the call reads the held-item
object that only exists once the race runs. A copy ability received in Top Ride gives its Top
Ride analog instead (Fire, Freeze Fan, Bomb, Walky); one with no analog waits for a 3D round.

**Traps.** 1 HP (6) damages each human machine down to exactly 1 HP through
`Machine_GiveDamage`. Drop Patches (13) queues the game's own patch drop, `Rider_DropPatches`
(0x8019d330), for each human rider with a random drop mode - in front or behind, a small fixed
count or one scaled by the rider's stats - City Trial map only, since the dropped patches are
items.

**Fillers and rewards** apply anywhere: a checkbox filler adds one to that checklist's filler
count (the Archipelago tab's is dropped in a build without custom_checklist), and a checklist
reward is granted by its game index.

## Big and Small Kirby

972 and 973 are cosmetic filler for every mode. Each multiplies a shared target scale by 1.5 or
0.5, clamped to 0.5-2.0, and a per-frame proc eases every human Kirby's model toward it over 60
frames with a smoothstep; a second item mid-ease restarts from the current size.

Both engines multiply a per-object scale into the model every frame, so writing it is the whole
mechanism:

- In 3D, `RiderData.model_scale` (+0x348) feeds `Rider_ApplyModelMatrix` (0x80190848), run
  from `Rider_ModelMatrixThink` (0x8018f79c). Only the rider grows; the machine has its own
  `MachineData.model_scale`.
- In Top Ride, `TopRideKirby` +0x524 (`charge.model_scale`) feeds `TopRide_KirbyModelThink`
  (0x802e26dc). Kirby and his star share one model tree there, so both scale together.

The item retries until a round is underway: past the intro in 3D, `round_state == 2` in Top
Ride (which has no intro; scaling during the countdown would finish before the race starts).
The effect lasts until the next scene change, where the objects are rebuilt at 1.0 and the mod
snaps its state back to neutral.

## Announcements

Every give and unlock announces through `APAnnounce_Grant` / `APAnnounce_GrantSegments`, which
print only while Settings -> Messages -> Local -> Items is on (off by default, since the client
posts its own line for every item). The same funnel stays silent while `ap_regrant_quiet` is
set, during the re-grant of checklist rewards at save load and on each placement write.
