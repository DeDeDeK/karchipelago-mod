# Treasure Rain

A custom City Trial event (`CUSTOM_EVKIND_TREASURE_RAIN`, kind 30, "Treasure Rain!") that rains
good items onto the city's roads for 30 seconds - stat patches, All Ups more often than anything
else in the game gives them, and the occasional temporary power-up - while the regular sky
spawner fills the field faster and fuller than usual. It runs through the `custom_events`
framework with a siren intro, sky preset `CITYSKY_DUSK`, BGM file 0x2f (`event_itembound`), an
1800-frame duration and roll weight 20, and registers `start`, `active`, `end` and `end2`.

## The Pool

Each drop is a weighted pick:

| Item | Weight | Share |
|---|---|---|
| Each of the nine up patches | 10 | 7.9% each |
| All Up | 12 | 9.5% |
| Speed Max, Offense Max, Defense Max, Charge Max | 4 | 3.2% each |
| Maxim Tomato | 5 | 4.0% |
| Candy | 3 | 2.4% |

At the start the event keeps only the kinds the round offers: a kind listed with a non-zero
chance in one of the three box pools of `grBoxGeneObj`, or weighted in any column of the stage's
`event_source_drop` table. All Up has a box chance only in the VS King Dedede stadium group, but
TAC, the meteor, the destructibles, the Secret Chamber and the UFO all drop it, so it passes. A
mod that locks an item kind compacts it out of the box pools and zeroes its event-drop row, so the
rain honours the lock
without knowing about it. Shares grow in proportion when a kind drops out. With nothing offered
the rain stays dry and only the sky spawner changes.

## Where It Rains

A drop lands on a road anywhere in the city. A spot is a random point between the two road edges
of a random CPU range pair, keeping 10% of the width clear at each edge, dropped onto the floor
under it with `Raycast_Ground` (0x800d1ac4). The spot is rejected when its floor triangle has
`GRCOLL_FLAG_MOVING` set, or when `Raycast_Any` (0x800d1a54) from 150 units above the floor down to
4 units above it hits anything, since a tunnel, an overhang or the garage has no sky to rain from.
Up to 8 picks are drawn per drop.

## The Drops

Each drop is created 150 units above its floor through `Item_InitDesc` (0x802509a0) and
`CityItem_Create` (0x8024eef4), with scale 1, up (0, 1, 0), a random-yaw forward, no box, no spawn
slot, `is_airborne` 1 and `ITEM_COLL_POINT`: any `is_airborne` but -1 runs creation's ground
raycast, and a point-collision item lands on the floor it finds, or is not created when that
floor is invalid. It falls at item gravity, capped at `ItCommon.dat`'s terminal speed of 0.9 units per
frame, so a drop takes about three seconds to land. It keeps the normal item lifetime of
1200-1440 frames, so the last drops outlast the event by about 20 seconds.

The drops carry `spawn_type` 0x5452, a tag no vanilla source uses, which is how the event counts
its own items on the item list. Nothing in the engine branches on the value:
`Ply_IncrementItemCollectNum` (0x8022fbcc) only compares it with 4. The drops do not get
`ITEM_X35A_SKY_SPAWNED`: an item with that flag takes one off the sky spawner's `cur_num_items`
when it is destroyed, and the rain never added to it.

## Pacing

`active` drops one item every 15 frames while all of these hold:

- fewer than 30 rain items are alive;
- `CityItem_CanSpawnNMore(12)` (0x80252d40) passes, leaving room under the engine's 101-item cap
  for box contents and rider patch drops, which fail to spawn when it is full;
- `OSCheckHeap(HSD_GetHeapID())` leaves 512 KB free, because an allocation the heap cannot serve
  crashes the game rather than failing the spawn.

## The Sky Spawner

`CityItemSpawn_UpdateAndCheckToSpawn` (0x800ea6e0) normally takes its field cap and spawn interval
from the stage's match-progress table: in City Trial 45-60 items, and a spawn every 20-50 frames
once the opening fill is done. While `grBoxGeneInfo.event_active_flags` has
`GRBOXGENE_EVENT_FALLDESC` (bit 0) set it reads `cur_event_fall_desc` instead, the override
`CityEvent_ModifyItemFallDesc` (0x800ed784) installs for the vanilla events the stage lists.

At the start the event saves `cur_event_fall_desc` and bit 0, writes a cap of 65 items - bounded
by the field's spawn slots, `item_area_pos_num + item_pos_num`, as the vanilla override is - and
an interval of 10-20 frames, sets bit 0 and zeroes `spawn_timer` so the faster pace starts at
once. The spawner's pools and box mix are untouched. On the first frame of state 3 the saved desc
and bit go back, so the spawner returns to its own pace as the rain stops. A run the scene exit
cuts off needs no cleanup: the next scene builds a fresh `grBoxGeneInfo`, and the next start
resets the event's state.
