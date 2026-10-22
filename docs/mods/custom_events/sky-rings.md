# Sky Rings

A custom City Trial event (`CUSTOM_EVKIND_SKY_RINGS`, kind 34, "Rings in the sky have appeared!
Get the items!") that hangs 15 extra dash rings in the sky for 60 seconds, each holding a
floating patch. Flying through a ring gives the vanilla ring boost and collects its patch, and a
new patch appears in the ring 5 seconds after one is taken. It runs through the `custom_events`
framework with a siren intro, sky preset `CITYSKY_BLUE_SKY`, BGM file 0x34 (`event_supercharge`),
a 3600-frame duration and roll weight 20, and registers `can_start`, `start`, `active`, `end` and
`end2`.

## The Ring Model

City Trial's nine sky rings have no joints of their own. All nine are baked into one terrain
POBJ - the second POBJ of the terrain root's DObj 55 in `GrCity1Model.dat` - together with about
150 other vertex-colored strips. Each ring is an octagonal hoop of 32 vertices drawn as a few
triangle strips. The material is unlit and opaque (`RENDER_VERTEX | ALPHA_MAT`, no texture), and the
two-tone yellow is the vertex colors. Nothing animates it. The hoop's inner corners sit 9.26
world units from its center, the outer 10.14, and it is about 1 unit deep.

The event copies one ring out of that display list: the axis-aligned ring at
(76.8, 56.0, -96.2), whose position indices are 3351-3382 and which opens along Z. Each vertex in the
list is 4 bytes, a u16 position index and an RGBA4 color, so a strip is kept when every index it
draws falls in that range. The kept strips are copied into a 32-byte-aligned buffer, zero-padded
and flushed with `DCFlushRange` (0x803d3f24). A new `POBJDesc` points at that buffer and at the
source's own vertex descriptors and position array. A `DObjDesc` pairs it with the source's
material, and a mesh joint offsets it by minus the ring's center. The source is fingerprinted
before use: 398 display blocks, position as a `GX_INDEX16` F32 index and color direct.
`can_start` fails on any other stage, so the event never rolls there.

The rings hang under one root on one GObj, drawn by `JObj_GX` on `GAMEGX_MAP` at priority 2, after
`Map_GX` (0x800d81e4), whose backdrop draws with no depth test. Each ring joint carries its
position, a yaw that turns its local Z onto the ring's axis, and a scale of the stage scale times
1.5, so an event ring is half again the size of a vanilla one. The archive's vertex data lives
only as long as the round, so the descriptors are rebuilt at every start.

## Placement

Each candidate is a random point over a road: a random point between the two road edges of a random
CPU range pair, dropped onto the floor. It gets a height drawn uniformly between 56.0, the lowest
vanilla ring, and 463.8, the roof of the garden in the sky, and a random yaw. The ring always
stands vertical, like the vanilla ones. A candidate is rejected when:

- it is less than 25 units above the road it was drawn over;
- it is within 80 units of another event ring or 50 of a vanilla ring;
- `Raycast_Ground` (0x800d1ac4) from y 600 down to the top of the hoop hits a floor, so a ring is
  never under the garden, a bridge or an overhang, or inside a hill;
- any of five lines along the axis is blocked within 50 units on either side of the opening.
  The lines run through the center and four points on the rim. Rays are one-sided, so each line is
  cast both ways with `Raycast_Any` (0x800d1a54).

Up to 300 candidates are drawn. Against the baked City Trial collision, 15 rings place in about 20.

## Passing Through

A vanilla ring is two collision boxes of zone kind 4 (`GrCZK_DashRing`) with opposite +Z
axes. It fires when the machine's sweep crosses the box's flagged face within the zone's radius
of the face center. `Machine_CheckDashZone` (0x801cf60c) handles the zone. Stars call it through
`Machine_Star_CheckDashZone` (0x801edcc8) and bikes call it directly, from their run, fly and
landing states, and never from a rail. It reads the ring's row from `grGetDashGateZoneParam`
(0x800d21f8) - in City Trial accel 0.12, impulse 0.05, 40 frames - and then:

- writes `dash_dir` (the zone's +Z), `dash_accel` and `dash_frames`, and adds `dir * impulse` to
  `velocity`; a star also adds the impulse to `star_dash_accum`;
- plays the zone's stage sound, FGM 0x120019;
- calls `Ply_IncrementDashGateCount` (0x80230e78), which counts `dash_gate_hits` and, for a
  ring, `sky_rings`, the stat behind the checklist's "fly through the rings in the sky" cell;
- calls `Ply_PlaySkyRingSFX` (0x8027a2d0), the jingle.

The boost itself is `Machine_ApplyDashAccel` (0x801d96ac), which every star and bike physics proc
runs while `dash_frames` is non-zero. It adds the acceleration, fires the boost jets and speed
trail, and puts the rider in its accel pose. So writing the three fields is a full boost.

Event rings have no zones, so `active` tests each player's machine itself. It keeps every
machine's position from the previous frame and checks whether the segment from there to the
current position crosses a ring's plane inside its opening, 9.26 x 1.5 units from the center. A
crossing does what `Machine_CheckDashZone` does for a vanilla ring, with the ring's axis pointed the
way the machine flew. The whoosh plays through `Machine_PlaySFX` (0x801dd17c), and the event
increments `dash_gate_hits` and `sky_rings` directly, so event rings count toward the checklist cell.

A machine is skipped when it has no rider, is on a rail, is held by a gondola or cannon, or is
dead or fall-dead. A move longer than 60 units in one frame is a respawn and is not tested. A
machine that just passed a ring cannot pass it again for 30 frames, so jitter along the plane
does not fire it twice. Riders on foot never touch a dash zone and are never tested.

## The Items

Each ring holds one item from the nine up patches and All Up, picked uniformly from the kinds
the round offers. A kind is offered when one of `grBoxGeneObj`'s three box pools lists it with a
non-zero chance, or when the stage's `event_source_drop` table weighs it. A mod that locks an
item kind takes it out of both. With nothing offered the rings still boost but hold nothing.

An item is created at the ring's center with scale 1.5 through `Item_InitDesc` (0x802509a0) and
`CityItem_Create` (0x8024eef4), with `is_airborne` -1 and `ITEM_COLL_REST`. With `is_airborne`
-1, creation runs no ground raycast and keeps the position as given. The spawn state hands the
item to rest with velocity and accel zeroed, and with `is_airborne` -1 the rest state's
`Item_RestEnvColl` (0x80254d4c) returns without testing a floor. So the item hangs in place with
no per-frame pin. Its lifetime is set to 4800 frames, past the end of the event.

An item is collected by touching it, as anywhere else: `Machine_CheckPatchColl` (0x801dba74) has no
ground or airborne requirement. A ring pass also collects it, through `Machine_OnTouchItem`
(0x801db34c), while the item is at rest, `ItemGObj_CanCollect` (0x80252df0) passes and the
machine's `xc31` lacks `MACHINE_XC31_NO_PICKUP`. That is the bit `Machine_CheckPatchColl` skips
on. Either way the patch tosses toward the rider as usual.

Items carry `spawn_type` 0x534b, a tag no vanilla source uses. Item GObjs are reused, so a ring
treats its item as still waiting only while that GObj is on the item list with the tag, its own
`spawn_serial`, and the spawn or rest state. Once it is not, the ring waits 300 frames and spawns
a new one. A spawn waits while `CityItem_CanSpawnNMore(12)` (0x80252d40) fails or the heap has
less than 512 KB free.

## Ending

On the first frame of state 3 the event runs `ItemGObj_EnterExpire` (0x8025611c) on every item
still waiting in a ring and destroys the ring GObj, as the sky turns back. `end2` logs the passes
and the items taken. A run that the scene exit cuts off needs no `abort`: the GObjs go with the
scene, and `start` resets every static.
