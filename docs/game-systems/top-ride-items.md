# Top Ride Items

Top Ride has its own item system, separate from City Trial's boxes and spawn tables: 22 2D
power-up kinds (`TopRideItemKind`, `TRITEM_NUM` = 22) managed by an item manager, plus the Mystery
item, a roulette that becomes one of the others. Players fly into items during a race for
temporary attacks and defenses. None of City Trial's item code - `CityItem_Create`, the box spawn
tables, `Machine_OnTouchItem` - runs here. A mod that gives, gates or reacts to Top Ride items works
through the item manager's enabled mask and the kirby's apply function described here.

Two things City Trial has do not exist in Top Ride. **There are no copy abilities**: with no
`RiderData` or `MachineData`, nothing that grants an ability is reachable, and the closest analogs
are the four ability-themed items below. **There is no 3D stage collision**: `stc_grobj` is written
only by `grLoadStage` (0x800ce270) and its helpers, which Top Ride never reaches, so there are no
ground attributes (copy panels included); Top Ride's courses are 2D tile-map archives
(`A2a2dBG_*.dat`) with their own tile collision (`TopRide_ApexGridScan`, 0x802cf83c).

## The Item Manager

`TopRideItemMgr` is built in place inside the KirbyMgr by `TopRideItem_MgrInit` (0x8034b5f4)
during the kirby manager's construction; a pointer to it is at 0x805ddba4 (`stc_topride_itemmgr`).
Two fields drive everything:

- `stage_data` (+0x04), the course's config, whose bytes select the spawn mode and carry three
  checklist-reward unlock flags.
- `enabled_mask` (+0x24), a bitmask of the kinds that can spawn (bits 0-21). A kind whose bit is
  clear is never picked. Mystery is not in the mask and is always available.

**The initial mask.** `TopRideItem_MgrInit` picks one of three:

| Condition | Mask | Items |
|---|---|---|
| config byte +0x41 is 2 | 0x002783 | Hammer, Big Cake, Buzz Saw, Drill, Freeze Fan, Missile, Bomb |
| else config byte +0x38 is 1 | 0x09C06E | Big Cake, Speed Up, Speed Down, Charge Tank, Invincible Candy, Step-boom, Lantern, Walky, Smokescreen |
| else | 0x3FFFFF | All 22 |

Only the all-items path then applies the checklist-reward clears: `stage_data+0x4a` clear removes
Chickie (bit 20), `+0x4b` Who? Paint (bit 18), `+0x4c` Lantern (bit 15). Those three bytes come
from `TopRide_SetExtraUnlocks` (0x8000b5dc), called from the course and character selection, which
stores `ClearChecker_CheckUnlocked` of Top Ride checklist rewards 8, 9 and 10 into
`GameData.topride_extra_unlocks` (+0x37e). A mod that only clears bits of the finished mask cannot
re-enable these three; they follow the received rewards.

## Item Data

The descriptor table at 0x804ea2fc holds 23 pairs of a model name and an action name: entry 0 is Mystery
(`a2dIT21` / `AC_hatena`), and kind K sits at entry K + 1. Five action strings (`AC_ice`,
`AC_bomb`, `AC_mike`, `AC_meta`, `AC_piyo`) live in small data at 0x805db2c8 rather than beside the
others. Two kinds are the same Party Ball: 21 (`a2dIT20` / `AC_usiro`) is the real one and 12
(`a2dIT0b`) a visually identical twin.

Per-kind parameter blocks come from `TopRideItem_GetDataByIndex` (0x8034d204), a 22-case switch
into the array at 0x804ea548 (`TopRideItem_GetDataBase`, 0x8034d1f8). An out-of-range kind falls
through to returning the kind itself as a pointer, so only 0-21 are safe.

| Offset | Meaning |
|---|---|
| 0x00-0x10 | Weight columns; the timed spawn picks one by the player's rank, or column 3 in some modes |
| 0x10 | The weight the Party Ball's burst always reads |
| 0x14 | Earliest race time at which the kind can appear |

## Spawning

`TopRide_KirbyMgrUpdate` (0x802db74c) drives the flow each frame: `TopRide_KirbyPhysUpdate` per
kirby (0x802db8a4), `TopRideItem_SpawnTimed` (0x8034b8c8) twice (0x802db8e8, 0x802db914),
`TopRideItem_Update` (0x8034c130, the lifetime and render tick, at 0x802dc578) and
`TopRideItem_PartyBallUpdate` (0x80356dac, at 0x802dc5b8). The spawn picker and the update run
only while `round_state` is 2.

`TopRideItem_SpawnTimed` rolls a spawn chance that varies with time, player count and position;
walks the 22 kinds skipping any whose `enabled_mask` bit is clear or whose time threshold has not
passed; rolls a weighted pick from the active weight column; picks a position along the track by
spline; and calls `TopRideItem_Create` (0x8034ad08). `TopRideItem_SpawnAtPosition` (0x8034bf50)
spawns one item at a given point, for the per-item effect handlers (22 virtual overrides at
0x802b0b88-0x802b357c) and the Party Ball's burst.

**The Party Ball** runs on a frame counter: a camera shake in from 10, a bounce in from 80, a
wobble at 150, a flash at 191, opening at 255 with a burst of weighted random
`TopRideItem_SpawnAtPosition` calls, then shake out and reset by 395. The burst reads weights
through two `TopRideItem_GetDataByIndex` calls (0x803574a4, 0x803574d0) and does **not** check
`enabled_mask`, so a locked or out-of-range kind can come out of it unless those calls or the
spawn's entry are intercepted; an invalid kind makes `TopRideItem_Create` read past the descriptor
table.

## Pickup and Direct Apply

A spawned item flies along its initial vector and decays in `TopRideItem_Update`. On overlap with a
kirby's `Absorber` (kirby+0xD00) it writes the kind to `absorber+0x0E` and 3 (acquiring) to
`absorber+0x10`; that is only the visual handshake. The effect comes later in
`TopRide_KirbyPhysUpdate`'s tail: a helper at 0x8034ac84 (called at 0x802d6ba0) takes the kind -
-1 if the absorber's mode at +0x0C is nonzero, otherwise resetting it to 0xFFFF - and
`TopRide_KirbyApplyItem(kirby, kind)` (0x802d8cb4, called at 0x802d6bac) dispatches the effect.

To give an item straight to one kirby, with no projectile and no position check, call
`TopRide_KirbyApplyItem` directly: it applies the effect this frame and skips the absorb
animation. It needs the kirby's state handler, so only while `round_state` is 2; an out-of-range
kind does nothing.

## Held Powers

The four ability-themed items - Fire (11), Freeze Fan (9), Bomb (13), Walky (16) - are states, not
instant effects. `TopRide_KirbyApplyItem` moves the kirby's state handler into a per-item state
that lasts the power's duration: each setter resets the state object with
`TopRide_KirbyNormalSetter` (0x802df844), then overwrites its vtable.

| Item | Setter | State vtable |
|---|---|---|
| Fire | `TopRide_KirbyItemFireSetter` (0x80302d04) | 0x804db288 |
| Freeze Fan | `TopRide_KirbyItemFreezeFanSetter` (0x8030b398) | 0x804dc6e4 |
| Bomb | `TopRide_KirbyItemBombSetter` (0x80303a98) | 0x804db088 |
| Walky | `TopRide_KirbyItemWalkySetter` (0x8030668c) | 0x804dc150 |

A held power is detected by comparing the state handler's vtable with these (`TR_ITEMPOWER_VT_FIRE` and its siblings in hoshi's Top Ride header).
`active_item_kind` is not reset when a power runs out, so it is no "power active" test.

To drop a power, `TopRide_KirbyNormalMethod` (0x802da0f4, Kirby vtable slot 50) tears the current
state down - aura, model and effects - and installs `KirbyNormal`. It is the engine's own revert
when one power replaces another or a power times out.
