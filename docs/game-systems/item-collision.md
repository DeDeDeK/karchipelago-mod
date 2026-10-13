# Item Collision

How a City Trial item finds and stays on the ground, and what to pass when spawning one from mod
code. Items do not all carry a full collision body the way machines and riders do: most track
the ground with a single downward raycast, and the one byte that picks the strategy,
`coll_kind`, is also the parameter most likely to be wrong in a hand-built spawn.

## coll_kind

`coll_kind` is a 3-bit field in `ItemData+0x359` (mask 0x1C). `CityItem_AllocCollData`
(0x80254318) writes it at spawn and creates a `CollData` (`ItemData.coll_data`, `+0x1A4`) only
when it is 1. Each frame `CityItem_EnvColl` (0x8024f814) calls the item's env-collision
callback, usually `Item_GenericEnvColl` (0x80255438), which dispatches on it:

| Value | CollData | Behavior |
|---|---|---|
| 0 | none | Resting. While `is_airborne != -1` it re-checks the collidable bit of the ground triangle it landed on; once that triangle is retired (a broken prop under it), it switches to 3 and raycasts down for new ground |
| 1 | required | Full sphere collision. Updates the `CollData` and reads its contacts: a wall or ceiling contact zeroes velocity, a floor contact runs `ItemColl_BounceLand` (0x802557bc) |
| 2 and up | optional | Point collision through `ItemColl_HandleLand` (0x80255aa4); a `CollData`, if present, is still updated |

When a bouncing (1) or point-collision (2+) item lands, it clears `is_airborne`, destroys any
`CollData` and drops to 0. A box spawned with 1 therefore bounces with full collision and then
settles into the cheap resting state.

Kind 1 reads its contacts through `CityItem_GetGroundInfo` (0x80254464), which dereferences
`coll_data` unconditionally, so an item must never reach kind 1 without one.

## Ground state

Items on the raycast path keep their ground in `ItemData` rather than a `CollData`:
`point_coll.raycast_idx` (`+0x1A8`, the triangle under the item, -1 for none),
`point_coll.land_pos` (`+0x1AC`), `fall_dir` (`+0x1C8`, the direction the ground raycast is cast
along) and `is_airborne` (`+0x1D4`).

`is_airborne` is the only reliable "is it on the ground" test: 1 airborne, 0 resting, -1
airborne with the ground raycast suppressed. The `0x10` bit of `ItemData+0x35A`, set by
`Item_SetGroundedFlag` (0x802557a8), only means a ground reference was acquired - a sky drop
sets it while still hundreds of units up - and it is never cleared.

The raycast helpers are `Item_Raycast` (0x802546e4, steps `Raycast_Ground` along a direction),
`CityItem_FindGroundBelow` (0x802547cc, raycasts along `fall_dir` into `point_coll`) and
`CityItem_ValidatePointCollID` (0x80254444, a wrapper over the triangle-id range check). A
cached `raycast_idx` keeps pointing at its triangle after that triangle is retired, until the
next raycast.

## Spawning an item

`Item_InitDesc` (0x802509a0) fills an `ItemDesc` and `CityItem_Create` (0x8024eef4) builds the
item from it, returning NULL when the spawn fails. `Item_InitDesc` takes 13 arguments: eight in
GPRs (r3-r10), the scale in f1, and four on the stack - `is_airborne`, `coll_kind`, `spawn_area`
and `spawn_coll_kind`. The GameCube ABI does not shadow floats in GPRs, so the last four really
are stack arguments, and a call that omits them hands the item stack garbage for `coll_kind`.
`spawn_area` and `spawn_coll_kind` are -1 in ordinary spawns.

```c
ItemDesc desc;
Item_InitDesc(&desc, kind, 1.0f, 0, &pos, &up, &forward, -1, -1,
              1,    // is_airborne: do the initial ground raycast
              3,    // coll_kind: point collision, no CollData
              -1, -1);
GOBJ *item = CityItem_Create(&desc);
```

| Wanted | is_airborne | coll_kind |
|---|---|---|
| Drop at a position and settle on the ground below | 1 | 3 |
| Bounce on landing, like a box | 1 | 1 |

An `is_airborne` of -1 skips the spawn raycast. The engine's own sky spawner,
`PowerUp_SpawnFromSky` (0x800ecdf4), picks the pair from its spawn style: (0, 2), (0, 1), (1,
2), (1, 1) or (1, 3) for styles 0-4, and (-1, 0) otherwise. With `coll_kind` 1 the `CollData`
cleanup is automatic: the landing destroys it, and an item destroyed early frees it in its
destructor.
