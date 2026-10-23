# Planet Event

A custom City Trial event (`CUSTOM_EVKIND_PLANET`, kind 20) announced as "A planet has appeared!".
It runs through the `custom_events` framework with a siren intro, sky preset 7 (Blue Sky), BGM file
0x31 (`event_meteo`), a 3600-frame duration and roll weight 20, and uses the `start`, `end`,
`end2` and `abort` callbacks.

A planet grows in the sky over the central tower. Gravity near it points at its center, so a
machine that jumps or flies anywhere close falls onto it and can drive all the way around it.
While it is up it also lifts resting items and parked machines out of the city and drops them on
its surface. When the event ends the planet vanishes and everything on it falls back to the city.

The planet is built from three things the engine already understands, so almost nothing needs
special casing: **stage collision** appended to City Trial's own pool, which every sweep, raycast
and shadow already queries; **gravity** through the one function every body asks for its down
vector; and an **immediate-mode mesh** drawn from a GX callback, so no asset is needed.

## Engine Seams

| Site | Patch | Does |
|---|---|---|
| Call at 0x800d6e4c in `grColl_Alloc` (0x800d6dcc) | Call replaced | Counts the terrain, then adds the planet's counts to `GrObj.coll_max` |
| Call at 0x800d7044 in `grColl_Alloc` | Call replaced | Builds the terrain, then appends the planet's collision |
| `Gr_GetDownVector` (0x800ceb18) | Function replaced | The gravity field, for every body and camera |
| `Machine_RefreshGravity` (0x801c98c4) | Function replaced | The long-range pull on airborne ridden machines |

All four are installed at boot. The collision hooks act only when `Gr_GetCurrentGrKind()` is
`GR_CITY1` and City Trial events are on (`Gm_IsEventsEnabled`), and both replacements run the
vanilla body and change nothing while the planet is not solid.

## Shape and Placement

The planet is centered at (-60, 710, 0) with radius 180. Its bottom (Y 530) rests on the three
prongs of the sky garden atop the central tower, and its top (Y 890) stays well under the city's
invisible lid at Y 1040.9.

The mesh is an icosahedron with every face cut into 8 x 8 triangles and projected onto the unit
sphere: 1280 triangles over 20 faces, facets about 8 degrees apart. It is built once at boot and
feeds both the collision, scaled and placed at load, and the drawing.

## Collision

`grColl_Alloc` sizes the stage's collision arrays exactly to the counts in `GrObj.coll_max`
(+0x0C) and allocates them once per load, so the planet's room is reserved before that:

- **Reserving.** The hooked `grColl_CountArrays` call adds the planet's vertices, triangles,
  records and `moving_record` entries to `coll_max`.
- **Appending.** The function's last call builds the terrain with `grColl_Build`. The hook then
  writes the planet at the fill position the `GrObj.coll` (+0x54) counts mark and advances those
  counts. That runs before any prop builds its own collision window, so the props land after the
  planet's entries.

Each icosahedron face becomes one record, added to `coll.moving_record`. Every query brute-forces
that list, testing each record's bounding box before its triangles, so the box culls most of the
sphere for any one query. The records have no joint, identity `world` and `prev_inv` matrices and
no `yaku_gobj`. They sit outside every rebake window, so nothing re-transforms them, and record
walks that look for breakable props skip them.

Every triangle is `GRCOLL_KIND_UNDER` (ground) with ground type 5, the island's main ground, so
traction, sounds and dust match the city. Its normal faces outward under the engine's convention,
`normalize((v2 - v0) x (v1 - v0))`. Nothing in the contact path compares a triangle normal against
world up, and machines, riders and items all probe the ground along their own up or down vector,
so a triangle facing sideways or down is drivable for any body whose gravity holds it there.

Between events the triangles stay in the pool, switched off. Their state is
`GRCOLL_STATE_SURFACE_PARAM` without `GRCOLL_STATE_COLLIDABLE`, and each record's box is parked far
below the out-of-bounds floor with zero size, which also keeps the load-time stage raycasts off
the invisible planet. Switching on and off only flips those fields. The counts and list entries
never shrink, so later prop windows and cached triangle ids stay valid.

## Gravity

The replacement `Gr_GetDownVector` runs the vanilla body - point zones, spline zones, then the
stage's `gravity_dir` and `gravity_strength` - and while the planet is solid overrides the
direction near it:

| Distance from center | Down |
|---|---|
| 280 and beyond | The stage's |
| 230 to 280 | Turned from the stage's toward the planet's by distance |
| 177 to 230 | Toward the center |
| Under 177 | Away from the center |

The strength is always the stage's, read live, so anything else that changes it still applies.
The blend band exists for the camera: it takes its up from the down vector at its target, and
blends that per component without renormalizing, so a down vector that flipped in one frame would
pass it through a short up vector. The turn goes through the halfway direction, so it stays
defined even straight under the planet, where the two downs are opposite.

**The long-range pull.** A machine copies its down vector from `Gr_GetDownVector` in
`Machine_RefreshGravity`, every frame from its priority-6 matrix proc. The replacement does the
same, and for a ridden machine that is airborne, off a rail and alive it then turns that down
toward the planet out to 600 units from the center, blended over the outer 100 units the same way.
A jump or flight anywhere near the planet therefore falls onto it, at the stage's strength. Only
the machine is overridden: grounded machines, riders on foot, items and every camera keep the
stage's gravity until they reach the field itself, so the city stays drivable underneath and the
view rolls only as the machine arrives.

**The core.** It starts 3 units inside the true radius because flat facets sag up to about half a
unit below it, and inside it gravity points outward. The triangles are one-sided, so nothing else
would push out a body that ends up inside: each frame the planet is solid, every machine, rider on
foot and item in the core is moved out to 8 units above the surface. Machines move through
`ext_disp`, riders through `platform_disp` and items directly. Machines and riders keep their
velocity; items are dropped into a fresh fall.

## Bodies on the Planet

- **Ridden machines** drive around the whole sphere. The ground code aligns them to the floor
  normal and the air code to the gravity up, and `Machine_Star_GroundSnap` (0x801ede30) holds them
  to the curve.
- **Riders on foot** land anywhere, but `Rider_WalkTurn` (0x801a2168) forces their up to world Y
  while the stick is held, so they can walk only on the top cap.
- **Enemies and weapons** follow the planet's direction inside the field.

## Pulling Things In

Every 10 frames the planet claims one target at random, up to 16 at a time. A target is a resting
item (`is_airborne` 0, `ITEM_X35A_PICKUP_BUSY` clear) or a riderless machine in WaitStop or WaitRun,
within 600 units horizontally of the planet's axis and outside the field. Boxes are claimed like
power-ups. A claim rises straight at the center, accelerating by 0.04 a frame to 4 units a frame,
and is checked against its live p_link list every frame, so a collected, despawned or boarded
target drops out.

- **Items** are moved by writing `pos`, with `vel` zeroed and `is_airborne` set to -1 so their
  env-collision skips its ground raycast and snap. They tumble about the horizontal axis across
  their travel.
- **Machines** are woken into WaitFly (stars) or WaitJump (bikes), since WaitStop sleeps and skips
  its gravity refresh. Their velocity is zeroed and the rise is added to `ext_disp` from a
  priority-2 proc, after the priority-0 clear and before the priority-4 physics.

Inside 230 units the claim is released into the planet's gravity. A machine simply falls the rest
of the way. An item needs more, because both fall entries - `ItemGObj_PowerUpEnterFall`
(0x80258698) and `ItemGObj_EnterFall` (0x802578c8) for boxes - set `accel` once from the item's
cached `fall_dir`. So `CityItem_UpdateGravity` (0x80254c50) re-aims `fall_dir` first, the item gets
`coll_kind = ITEM_COLL_POINT` with an invalid raycast index so it re-casts along it, is turned
upright against it, and enters its fall. A claim that has not arrived after 600 frames is released
where it is.

## Drawing

The planet's GObj carries a priority-2 proc and a GX callback on the world camera's link 0. The
callback draws only in pass 1 (`GXPASS_TEXEDGE`), which runs after every link's opaque pass;
drawn in pass 0 on link 0, it is painted over. It draws opaque, unlit and depth-writing with no
culling, as `GX_TRIANGLES` with a position matrix of the camera's view times the planet's scale
and center, so every split-screen view draws it. Its colors are baked per vertex at boot: noise
bands of ocean, shallows, beach, grass and forest, ice caps, and a fixed sun.

## Lifecycle

| Phase | Frames | What happens |
|---|---|---|
| Grow | 90, from `start` | Drawn scaling up with a slight overshoot; no collision, no gravity |
| Solid | To the end of state 2 | Collision and gravity on; core ejection and suction each frame |
| Shrink | 30, from `end` | Collision and gravity off, claims dropped where they are; the drawing shrinks away |

The GObj sits on `GAMEPLINK_1`, which the match pause, the match end and the explosion hitstop
freeze. When the planet switches off, whatever stood on it falls through the engine's own
retired-ground paths: a ridden machine's next ground probe misses and it flies, and a parked
machine or a resting item finds its cached triangle no longer collidable and drops, re-reading
gravity, which is the stage's again.

`end2` destroys the GObj. `abort`, run when the round ends mid-event, only resets the event's
state: the collision lives in the scene heap and is rebuilt, switched off, by the next City Trial
load.

## Limitations

- **Every City Trial load with events on pays for the planet.** The reservation is about 104 KB
  of the scene heap (24 bytes a vertex, 64 a triangle, 152 a record), whether or not the event
  rolls.
- **Fast machines lift off.** A grounded machine stays on the curve while `speed^2 / (2 x 180)` is
  under the `snap_max` that `Machine_Star_GroundSnap` pulls by each frame, so one far over its top
  speed leaves the surface and falls back.
- **Riders on foot walk only the top cap**, as above.
- **Items already falling** when the planet turns solid keep the direction they started falling in
  until they land or bounce.
- **Both replacements are whole functions.** Another mod that replaces `Gr_GetDownVector` or
  `Machine_RefreshGravity` conflicts with this event.
