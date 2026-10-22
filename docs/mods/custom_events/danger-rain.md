# Danger Rain

A custom City Trial event (`CUSTOM_EVKIND_DANGER_RAIN`, kind 26, "Danger Rain!") that rains one
hazard onto the city's roads for 30 seconds: small meteors, Gordos, sensor bombs, bombs or the
volcano's lava dragons. Each start rolls the kind uniformly from `DangerRainKind`. Every drop is
ownerless, so it threatens every rider, CPUs included, and credits no one. The event runs through
the `custom_events` framework with a siren intro, sky preset `CITYSKY_RED_VIGNETTE`, BGM file 0x31
(`event_meteo`), an 1800-frame duration and roll weight 20, and registers `start`, `active`,
`end2` and `abort`.

## Where It Rains

A drop lands on a road anywhere in the city. A spot is a random point between the two road edges
of a random CPU range pair, keeping 10% of the width clear at each edge, dropped onto the floor
under it with `Raycast_Ground` (0x800d1ac4). The spot is rejected when its floor triangle has
`GRCOLL_FLAG_MOVING` set, or when `Raycast_Any` (0x800d1a54) from 220 units above the floor down to
4 units above it hits anything, since a tunnel, an overhang or the garage has no sky to rain from.
Up to 8 picks are drawn per drop. The drop starts 220 units above its floor.

## Pacing

`active` spawns one drop every few frames while the kind's live count is under its cap and
`OSCheckHeap(HSD_GetHeapID())` leaves 512 KB free, because an allocation the heap cannot serve
crashes the game rather than failing the spawn.

| Kind | Every | Live cap | Counted as | Gone when |
|---|---|---|---|---|
| Meteor | 8 frames | 24 | meteor actors | its landing effect ends |
| Gordo | 6 frames | 48 | ownerless Gordos | its lifetime runs out, embedded |
| Sensor bomb | 6 frames | 64 | ownerless sensor bombs | a rider comes near or its fuse runs out |
| Bomb | 4 frames | 48 | ownerless bombs | it touches anything |
| Lava dragon | 10 frames | 10 | the event's dragons | its head reaches a floor |

Spawning stops with state 2. Meteors and dragons land long before `end2`, 300 frames later;
Gordos and sensor bombs outlast the event on their own timers.

## Weapons

The three weapon kinds are created with `Weapon_Create` (0x8021f428) at the drop point with a
random yaw and leave their HELD state 0 in the same call, since its first proc would snap them to
a missing owner's hand. Each starts falling at 1 unit per frame and adds stage gravity in its own
flying state.

- **Bomb.** `BOMB_STATE_THROWN` detonates on its first contact. A bomb never expires on its own,
  so its lifetime is set to 900 frames.
- **Sensor bomb.** `SENSOR_BOMB_STATE_ARMED_FLYING` falls until `SensorBomb_Land` (0x802290bc)
  embeds it. Its fuse is set to 480-780 frames from the spawn, so the field clears itself.
  `SensorBomb_EnforceMaxActive` (0x8022a270) fades every sensor bomb past a cap of two, whatever
  its owner, whenever a player throws one; the mod replaces its call in `SensorBomb_TryThrow`
  (at 0x80228fa8) with a wrapper that hides every ownerless sensor bomb from the count, so a
  player's throw leaves the rain's bombs alone.
- **Gordo.** `WeaponGObj_EnterGordoThrownState` (0x8022a544) seeds the spin, impulse and lifetime
  a thrown Gordo needs, and reads its spin axis from the owner's rider through `RiderGObj_GetUp`
  and `RiderGObj_GetForward`. The first live rider owns the Gordo for the create and the throw,
  and `owner_gobj` is cleared right after. The throw's state change credits that player with a
  Gordo attack through `Ply_RecordAttackUsed` (0x80231bdc), so their `attack_use_num` and
  `attack_num` are saved before and put back after. In flight the Gordo grows to full size; on a
  floor contact it drops its horizontal velocity and embeds until the lifetime the throw set
  runs out.

**The Gordo cap.** The thrown transition ends with `Gordo_EnforceMaxActive` (0x8022b45c), which
counts every thrown Gordo whatever its owner and sends each one the running count puts past its
cap to `GORDO_STATE_DESPAWN`. At boot the event replaces that call (at 0x8022a6b0) with a
wrapper. A rain Gordo's own throw skips the cap; any other throw hides every ownerless Gordo from
the count by giving it the kind `WPKIND_BOMB` for the length of the call.

## Meteors

A meteor is the event meteor (`ENEMYKIND_METEOR`) spawned the way its event does, with the
event's globals stood in for: for the length of `EventActor_Create` and `Meteor_BehaviorInit`,
`stc_meteor_data` is non-zero and `stc_meteor_event_data` points at a `MeteorEventData` with one
zone (height 220, fall speed 5) and one speed entry (angle 0). The actor is created at the floor
point with `variant` 0, so it falls straight down from 220 above it and throws no items. Then
the three ways creation hides a meteor are cleared.

The event's meteors are scale 5 to 33; a rain meteor is 2.5. Its model, particles, light and
landing effect follow `final_scale`, but two effects are sized for the event meteor:

- **Rumble.** `Meteor_Landing` (0x8021ea5c) rumbles every player within
  `MeteorParams.rumble_radius`, 500 units, which is the archive's block every meteor shares. A
  meteor rain sets it to 80 at the start and puts it back at `end2` or `abort`. No other meteor
  falls meanwhile, since events never overlap.
- **Quake.** `Meteor_HitTransition` (0x8021e7c4) starts an every-view quake at amplitude 0.2. The
  event replaces its `Quake_SetAmplitude` call (at 0x8021e918) with a wrapper that scales the
  amplitude by 0.25 while a meteor rain runs.

## Lava Dragons

The volcano's two lava dragons are `YAKUKIND_LAVADRAGON` (61) yakumono built from stage joints,
not models of their own: a dragon is a root joint and six bones its body is skinned to, animated
by a stage motion whose event script arms the hitbox by stage joint index. A rain dragon is a copy
of that body with a hitbox of its own.

**The model.** A wrapper `JOBJDesc` carrying the stage scale and the fall position, with the
vanilla dragon's root `JOBJDesc` from `GrObj.joint_table` as its child, loads through
`JObj_LoadJoint` into a self-contained copy: the root's `next` is NULL and every envelope binds
inside the subtree. It renders on GObj class 215 through `JObj_GX` on the map link at priority 2.
`JObj_AddAnimAll` binds the dragon's motion to the copy and `JObj_RemoveAnim` drops the root's own
track, the leap's arc and its scale collapse between leaps, so the bones keep their wiggle while
the body falls. The motion's AObjs do not loop, so the wiggle restarts every 220 frames. The body
is turned so its local +X, head first, points down, under a random yaw. A proc at priority 4
moves the wrapper down 3 units per frame and removes the dragon once a `Raycast_Ground` from its
head bone's previous position to its current one hits a floor.

**The hitbox.** The vanilla motion script cannot arm a copy's bones, so each dragon gets a
model-less `YAKUKIND_LAVADRAGON` yakumono for its HurtData. `GrYaku_Create` builds it from a
zeroed parameter block lent to `data_array[18]` for the length of the call: no model, no audio,
and state -1, so no motion script ever runs on it. At the start the event copies a vanilla
dragon's two attack regions as its script last armed them, with the body bone each follows, and
arms the same on the copy's bones with `HurtData_SetAttackRegion` (0x8018c8c4): 40 damage, radius
15 and 10, knockback 30 and 20. The priority-4 proc runs before `YakumonoGObj_Proc7` places the
regions. A hit also sets the "bumped a flaming dragon" flag, which only Magma Flows' checklist
reads.

At most 10 dragons fall at once. `end2` removes any still falling; `abort` only forgets them,
since the scene exit reclaims their GObjs.

## API

`DoDangerRain(rain)` in `CustomEventsAPI` starts Danger Rain raining the given `DangerRainKind`.
It returns 0 for a kind out of range and otherwise whatever `Do(CUSTOM_EVKIND_DANGER_RAIN)`
returns. When the start fails, or the scene exit cuts the event off before it starts, the next
start rolls at random again.
