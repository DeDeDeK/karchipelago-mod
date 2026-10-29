# Mini UFOs Event

A custom City Trial event (`CUSTOM_EVKIND_MINI_UFOS`, kind 29, "Mini UFOs have appeared! Grab the
items!") that sends four small copies of the UFO event's saucer around the city for a minute. Each
one hovers along the stage's course loop, drops a stat patch about every five seconds and fires
shrunken full-charge Plasma balls at whoever comes near, so chasing the drops is a risk. It runs
through the `custom_events` framework with the UFO event's own sky preset (`CITYSKY_BLUE_SKY`) and
BGM file 0x33 (`event_stationfire`), a siren intro, a 3600-frame duration and roll weight 20, and
registers `can_start`, `start`, `end`, `end2` and `abort`.

## The Saucer

The vanilla UFO is `YAKUKIND_UFO` (67), built from a parameter block in `GrCity1Event.dat` that
the City Trial load installs in `YakumonoTable.spawn_data_array`; the UFO event's own data
(`bgm_sky[EVKIND_UFO].event_data`) holds its index. The archive lists the block once, because
`grColl_Alloc` reserves its roof collision once per listing, so a second UFO yakumono cannot be
spawned. The event spawns none. It loads the block's model - a two-joint tree whose child carries
the saucer's seven translucent meshes, the tractor-beam cone among them - into GObjs of its own
with `JObj_LoadJoint`, drawn by `JObj_GX` on the map link after the stage. The block's first motion
spins the child and animates its materials, and loops once `JObj_SetAllAOBJLoopByFlags` is set.

At scale 1 the saucer is 643 units across, and its beam reaches 212 below its center. A mini UFO is
drawn at 0.155, 100 units across with the beam 33 deep. The copies have no collision and no attack
regions, like the original's, which nothing ever arms. They also skip its two area lights and its
shadow.

## The Flight

The track is City Trial's course spline: `GrData.spline_node->course->pieces`, one linear closed
loop of 18 points and 3152 units through downtown, under the garden in the sky and past the
lighthouse. The load scales every `GrData` spline by the stage scale, so it is in world units.
Authored at a constant height of 110, it runs into hillsides in places, so the event flies a path
of its own laid over it.

At every City Trial load with events on, the event samples the loop every 10 units with
`splArcLengthPoint` and raises each sample to 40 units over the highest floor under the saucer.
It finds that floor with `Raycast_Ground` from 300 units up, just under the garden, at the center
and 25 units out on four sides, so a saucer passing a building rises over its roof. A sample with
no floor keeps the loop's own height. It then raises every sample to within a slope of 0.5 of its
neighbours in both directions, twice round so the rise carries across the loop's seam. The saucer climbs ahead of a
rooftop rather than into it, and hovers 40 to 180 units over the ground below it, 60 at the
median.

`start` spaces the four UFOs a quarter of the loop apart from a random point. Each moves 0.9 units
per frame along the path, about one lap per event, bobbing 2 units on a 120-frame cycle. It
arrives by dropping 150 units onto the path over 90 frames as it grows from nothing. State 3 sends
it back up the same way, shrinking, and it destroys itself well within the 300-frame cleanup.
`end2` destroys any UFO still up, and `abort` forgets the slots for the scene exit to reclaim.

## Drops

Once it has arrived a UFO drops one item every 240 to 360 frames, the four staggered 75 frames
apart, with `CityItem_Throw` from 8 units under the saucer: tossed outward in a random direction
and pitched 0.5 radians down, tagged `ITSPAWN_UFO`, unpickable for its first 20 frames.

The kind comes from the stage's event-drop table, the UFO column that `CityItem_GetEventItem
(EVDROP_UFO)` weighs, kept to the stat-up kinds (`Item_IsStatUpKind`: the nine up patches and All
Up): every patch at 20 except turn and glide at 10, and All Up at 10, so Charge Max never drops.
A mod that locks an item kind zeroes its row, so a locked kind never drops and a round with every
kind locked drops nothing.

## Plasma Shots

A UFO fires at the nearest rider within 150 units, CPUs included, every 150 to 210 frames, and
fires at once when someone comes into range after a quiet spell. The shot is `WPKIND_PLASMA_D`, the
Plasma ability's full-charge ball, made with `Weapon_Create` 4 units under the saucer and then
reshaped:

- **Owner.** `PlasmaD_Init` and `PlasmaD_PostInit` read the owner's player through
  `RiderGObj_GetPly`, so a NULL owner crashes the create. The ball is created with a static
  stand-in GObj whose `RiderData.ply` is `PLY_NUM`, no player: `Ply_PlayFGM` skips the firing
  sound for it and `Ply_RecordAttackUsed` ignores a GObj that is neither a rider nor a machine.
  Both owner fields are cleared right after, so the ball threatens every rider and credits no one.
- **Size.** `PlasmaD_PostInit` ignores `desc.scale` and seeds a ramp in the kind's scratch that
  grows `scale` from 0.05 to 0.7 over 30 frames; the event halves the ramp and the scale, which
  drives the model and the hitbox (radius 18 times `scale`) alike.
- **Damage.** `desc.charge` becomes the HurtData's `dmg_scale`; 0.5 halves the ball's 72 damage.
  The ball still pierces: `PlasmaD_OnHit` never ends it.
- **Flight.** Its velocity starts at 2.4 units per frame toward the target's position plus 3, and
  its `phys_callback` is replaced: each frame it blends 6% of the direction to the nearest rider
  within range into its heading at a constant speed, and flies straight with nobody near.
- **Ending.** `lifetime` is set to 0, so the vanilla timer never ends it and `PlasmaD_OnHit`, which
  halves a nonzero lifetime on each hit, never shortens it. The replaced `envcoll_callback` runs
  `Weapon_UpdateEnvColl`, which sweeps walls and ceilings as well as floors, and bursts the ball
  on any contact; the vanilla pass sweeps floors only. After 150 frames it bursts in the air.

The burst is `PlasmaD_Burst`, which moves the ball to its 10-frame shrink and reseeds the ramp from
the inner joint's animated scale - the size of a full ball, not `scale` - so the event halves the
reseeded ramp too and stops the ball where it burst. Only the burst's end frees the ball's point
light, so a ball is never destroyed any other way.
