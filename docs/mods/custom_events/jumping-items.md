# Jumping Items

A custom City Trial event (`CUSTOM_EVKIND_JUMPING_ITEMS`, kind 27, "Items are jumping!") that makes
every item already lying on the ground hop around for 30 seconds. Patches, food, copy abilities,
boxes and Legendary pieces all hop in random directions, from small bobs in
place to long arcs across a district. The event spawns nothing. An item that spawns during the event
starts hopping once it lands. It runs through the `custom_events` framework with a siren intro, sky
preset `CITYSKY_PINK_SKY`, BGM file 0x2f (`event_itembound`), an 1800-frame duration and roll weight
20, and registers `start`, `active` and `end2`.

## Item States

Every item family has the same rest state, 4, and a fall state, 5 for every non-box family and 6
for boxes. The engine changes a resting item to its fall state when the floor under it is retired:
`Item_RestEnvColl` (0x80254d4c) calls the family's fall entry, `ItemGObj_PowerUpEnterFall`
(0x80258698) or the box family's `ItemGObj_EnterFall` (0x802578c8). Each non-box family has its own
copy of the power-up entry, identical down to the frame and rate, and `CityItem_StateChange` resolves
state 5 through the item's own state table, so the power-up entry serves all of them. A fall entry
sets `is_airborne`, aims `accel` along `fall_dir` at `CityItem_GetGravity` and changes state. It does
not touch `vel`.

An item counts as resting when it is in its rest state with `is_airborne` 0. That excludes items
still spawning or falling, patches mid-toss toward a rider, expiring items and boxes reacting to a
hit.

## Launching an Item

A hop replicates `CityItem_Throw` (0x80253ce4) on an item that already exists:

1. `CityItem_UpdateGravity` refreshes `fall_dir`, which a resting item stops updating.
2. The family's fall entry runs, then the launch velocity is written to `vel`.
3. `CityItem_AllocCollData(id, ITEM_COLL_MPCOLL)` (0x80254318) and `CityItem_InitCollData`
   (0x80254384) give the item a sphere `CollData`, as `CityItem_Create` does for a thrown item.

From there the flight is vanilla. `Item_GenericEnvColl` (0x80255438) zeroes the velocity on a wall
or ceiling contact and runs `ItemColl_BounceLand` (0x802557bc) on a floor contact, which bounces
the item with `ItCommon.dat`'s damping until it lands. The landing destroys the `CollData` and
returns the item to its rest state.

## Hops

Each hop takes an independent random speed of 0.4-1.8 units per frame and a pitch of 40-88 degrees
above the horizon, measured from the item's own gravity up. Against item gravity 0.019, that spans a
bob of about 2 units to a leap of 80 units, and a horizontal reach from nothing to about 170 units.

Before launching, the event predicts the landing spot on flat ground and casts `Raycast_Ground`
(0x800d1ac4) from 40 units above it to 120 below. A heading whose spot has no floor in that range,
or whose floor is the sea or the barrier (`grGetGroundTypeFromTriangleID`, 0x800cec28), is
rejected. After four rejected headings the item hops straight up at the same speed. The prediction
ignores terrain height and the fall-speed cap, so it is a guard rather than an exact landing point.
It keeps long hops from carrying items off the island into the sea. Out-of-bounds destruction
(`CityItem_IsOutOfBounds`, 0x80255f90) uses the stage's out-of-bounds box, which in City Trial lies
hundreds of units past the shoreline.

## Timing

`active` walks the item list every frame and keeps a delay for each resting item: 30-120 frames,
rolled when the item is first seen at rest, after spawning or after each landing. When the delay
runs out the item hops. Items leave the delay table when they stop resting, so a landing always
rolls a new delay.

At most 48 items may be airborne at once, counting the ones the event did not launch. Every hop
holds a `CollData` from `mpColl_Create`'s pool, which grows from the scene heap. An item whose delay
has run out waits at the cap and launches on the first frame there is room.

The event ends with nothing to undo. Items still in flight land normally, and `start` clears the
delay table, so a run cut off by the scene exit needs no `abort`.
