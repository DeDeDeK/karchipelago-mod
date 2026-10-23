# Earthquake

The earthquake layer of `custom_weather`: a few times per City Trial round the ground rumbles,
every view rocks and the controllers rumble, and cracks tear open across the city's flat ground.
Anything on the surface - ridden and parked machines, riders on foot, items - falls through an
open crack, out of the bottom of the out-of-bounds box and into the normal fall death and respawn.
The cracks close again as the quake settles. It ships on the Earthquake preset and can be forced
onto any preset from its menu.

## Schedule and timeline

`EarthquakeDef` holds `count` (quakes per round, default 2; the Earthquake preset uses 3),
`duration` (default 720 frames, never under 280), `cracks` (per quake, default 3, at most 6),
`size` (crack length and width) and `intensity` (camera shake). Quakes are spread over the round
by match progress, one per equal slice with jitter inside it; the plan is drawn on the round's
first live frame and a later preset change ends a live quake without re-planning.

One quake runs on a fixed timeline:

| Frames | What happens |
|---|---|
| 0-45 | The rumble ramps in. One crack is grown per frame over the first 40, since each costs a few dozen ground casts. |
| 40-130 | The cracks tear open: each vertex reveals once the tear front, sweeping outward from the seed by arc length, reaches it, widening over 60 units of arc. |
| until 150 before the end | Held open. |
| last 150 | Every crack narrows shut along a smoothstep. |
| last 120 | The rumble ramps out. |

## Crack shape and placement

A crack is a polyline: a seed and two arms grown from it in opposite directions, 20-unit
segments, 9 segments per arm at size 1 (up to 24), each turning up to 0.45 rad off the last. The
half-width is 16 units times the size at the seed, tapering as `1 - x^1.5` to a point at each end,
jittered 0.75-1.15 per vertex. Up to two branches fork off interior vertices at 0.55-1.1 rad,
5 segments at size 1, starting at 65% of the fork's width.

Seeds are drawn within 75% of the out-of-bounds box. Every vertex must land on floor that is
near-flat (normal Y at least 0.85), not the sea (ground type 29), no more than 10 units up or
down from the previous vertex (so an arm ends at a kerb, wall or roof), and has nothing beneath it
on a ground cast from 8 units under the surface. That last test is what makes a fall end below the
city: City Trial's collision has stacked floors - tunnels under the northern mountain, raised roads,
the multi-storey block in the south, the sea running under the coastal rim - and a crack over one
would land bodies on hidden geometry. City Trial has no kind-0x19 death zones, so the only fall
death is the out-of-bounds box's floor at world Y -300; a machine reaches it about 2.7 s after
dropping in.

A crack is drawn as an earthen lip (dark brown, alpha 200, 1.3 times the hole plus 1.5 units)
and a black core over it, 2 units above the floor on the shared weather GX layer: one quad per
segment and an 8-sided fan per vertex, which closes the joints a bend opens. The fans and quads are
exactly the shape the collision tests.

## Removing the floor

Every floor query ends in one of two narrowphase tests, each hooked on its shared epilogue:

| Hook | Function | Registers at the hook |
|---|---|---|
| 0x800d9938 | `grColl_RayVsTri` (0x800d95dc) | r3 result, r27 the triangle's normal, r29 the hit point |
| 0x80244ea0 | `grColl_SweptSphereVsTri` (0x802448b0) | r3 result, r19 the triangle's normal, r29 the hit point |

On a hit against a floor triangle (`GrCollTri.kind` bit 0, at normal + 0x24) the hook tests the
hit point against the open cracks and turns a hit inside one into a miss. Callers only consume the outputs on a
non-zero return, so the veto is clean. A swept sphere's hit point is the point under its centre, so
a body drops once its centre crosses the edge rather than catching on the rim. Both tests are
one-sided, so ground cannot be hit from below and a falling body never snags on the underside,
and a crack that closes over a falling body does not catch it. The test only counts a hit within
14 units above or 40 below the crack's own surface, so another floor elsewhere is untouched.

Bodies at rest trust a cached ground triangle and never query again. Each re-checks it through
`ItemColl_GetGenericCollFlags` (0x800cee08, the triangle's collidable bit), and a 0 drops it the
way a retired prop's floor does. These sites are hooked to read 0 when the cached contact point
is in an open crack:

| Hook | Body | Cached point |
|---|---|---|
| 0x801ee7d0 | parked star (WaitStop state) | `MachineData.ground_probe_pos` |
| 0x801f8a68 | stopped bike (WaitStop state) | `MachineData.ground_pos` |
| 0x80254d94 | resting item (`Item_RestEnvColl`) | `ItemData.point_coll.land_pos` |
| 0x80255148 | landing item (`Item_FallEnvColl`) | same |
| 0x80255484 | `Item_GenericEnvColl` | same |
| 0x80255dfc | `ItemColl_HandleLand` | same |

The `ItemColl_HandleLand` hook is required: with no ground below, the item's raycast index stays
-1 and its landing test would read the triangle before the array and could snap it back onto the
crack. An item that falls out of the box is destroyed by `CityItem_LifetimeThink`.

A rider who falls on foot is eliminated in vanilla City Trial: `Respawner_Update` (0x8000ff78)
respawns a walking player only in the Derby (city kind 14). Once a crack has opened in a round,
the hook at 0x800100f4 hands that check the Derby's kind, so a rider who drops through on foot is
respawned instead.

## Shake, rumble, sound and dust

The camera shake is one global constant quake source (`Quake_AddConstantSource(NULL, 2, 0, 0)`)
whose offset is set every frame from the weather tick: 2.6 units times the intensity times the
envelope, mostly along camera right with a third of it vertical, the jitter from the frame count
rather than the game RNG. Priority 2 overrides the machine speed shake and small event quakes and
yields to a large one. A source is removed only while `Quake_SetConstantOffset` still accepts its
id, since removing an unknown id frees NULL; the 3D load's `Quake_Init` wipes the pool, so at a
scene change the id is dropped instead.

Controller rumble is `Enemy_RumblePlayer(ply, kind, 24)` for every player every 20 frames, the
LbRumble pattern stepping through kinds 8, 1, 10, 7, 10, 1, 8 over the quake (20%, 33%, 75% duty
and continuous), then kind 6, a decaying burst, as it settles. It runs through
`Rider_TriggerRumble`, so each player's own rumble option applies and CPUs never rumble.

A looping bed (`SFX_map_city_volcano_lp`, 0x120005) plays through `SFX_PlayRaw` at up to volume
200 following the envelope. It plays on sound group 1, which the match pause does not pause, so
the GX callback - which still runs under the pause - silences it there. Each crack seed plays
`SFX_map_city_big_stone_crash` (0x12000f) as the tear starts and `SFX_map_city_big_stone_hibi`
(0x12000e) as the close starts, both positional one-shots on a map audio emitter.

As the tear starts, each seed gets the pillar event's ground burst (particles 50007 and 50012, at
scale 3, 5 units under the surface) through `Effect_SpawnSync`'s world-position mode 103. Dust
puffs (particle 20067) rise from random open edges every 3 frames while tearing and every 8 while
closing.

## Settings

The Earthquake submenu carries the Earthquake toggle (Preset / Off / On), Quakes (Preset, 1, 2, 3,
5), Duration (Preset, Brief 0.5x, Long 1.6x, Sustained 2.5x), Cracks (Preset, One Big - a single
crack at twice the size - 1, 3, 6), Crack Size (Preset, Narrow 0.6x, Wide 1.5x, Chasm 2.2x),
Screen Shake (Preset, Off, Light 0.5x, Violent 1.8x) and Controller Rumble (Off / On, default On).
