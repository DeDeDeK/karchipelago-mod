# Truck Storm

A custom City Trial event (`CUSTOM_EVKIND_TRUCK_STORM`, kind 23, "Waddle Dee trucks incoming!")
that fills the roads around the human players with Waddle Dee Trucks. The trucks drive the city's
road splines with their vanilla AI at twice their normal speed, hit with four times their normal
knockback, and cannot be knocked out or inhaled. It runs through the `custom_events` framework
with a siren intro, sky preset `CITYSKY_DUSKY_CLOUDS`, BGM file 0x2e (`event_gordo`), an
1800-frame duration and roll weight 20, and registers `start`, `active`, `end` and `end2`, which
also serves as `abort`.

## The Truck

The Waddle Dee Truck is two actors from one archive, `EmDeeTruckData.dat` (95 KB): the driver,
`ENEMYKIND_WADDLE_DEE_TRUCK` (0x16), and the cart,
`ENEMYKIND_SP_WADDLE_DEE_TRUCK` (0x4A), which `DeeTruck_Init` (0x802186e4) spawns as the driver's
child and which copies the driver's position and facing every frame. Each has its own `HurtData`
with two attack regions. The driver's archive parameters give a base scale of 1.6, `param_speed`
0.8 and `param_hp` 1.

Driving is `EventActor_AIPhysicsTick` (0x802081ec) on the stage's range splines - the CPU road
splines `Spline_GetCount` (0x800cf38c) counts, 802 of them in the city. Driver states:

| State | Role |
|---|---|
| 0x0E | Spawn: `DeeTruck_SpawnAnim` (0x8021887c) shows the truck and enters drive |
| 0x0F | Drive; at each animation end a 1-in-12 chance of each look-back |
| 0x10 | Airborne, until it lands back on the road or takes a lean-turn |
| 0x11, 0x12 | The two look-backs |
| 0x13, 0x14 | The two lean-turns |

## Spawning

`start` loads the archive with `Enemy_CheckAndLoad` (0x801fd060) unless it is resident. Every 10
frames `active` frees slots whose truck vanilla destroyed and spawns one truck into a free slot of
24. Slots are kept only while their GObj is still on the `GAMEPLINK_ENEMY` list as a truck, which
also catches the allocator handing a freed GObj to the next actor.

The spawn point is a range-spline midpoint at most 300 units from a random human player and at
least 80 from every rider, drawn uniformly by reservoir sampling over all of them. The 3D distance
keeps an underground player's trucks underground. `EventActor_Create` (0x801fbb50) gets the
descriptor `Enemy_SpawnActor` (0x800f13a8) builds, without the spawn pool: the kind, the point,
forward (0, 0, 1), up and ground normal (0, 1, 0), scale 1.0, `is_airborne` 1, `spawn_index` and
`spawn_slot` -1 and `leash_radius` -1.0. No leash makes `EventActor_Create` attach the truck to
the nearest range spline, driving forward, with `EventActor_PathInit` (0x80206e2c).

## Speed

Path speed accelerates. `EventActor_InitGroundMove` (0x80207dec) starts `movement_speed` at
`param_speed`, and every frame `EventActor_UpdatePathSpeed` (0x80208010) adds `param_accel` and
clamps it between `param_speed` and `param_speed_max`, all three divided by the current spline's
length, since `movement_speed` is in spline parameter per frame. The parameters are per-actor
copies of the archive's, so the event doubles all three on each driver right after
`EventActor_Create`; the next frame's clamp lifts the speed it started with to the new floor. The
drive animation keeps its vanilla rate.

## The Heap Limit

Every allocation behind a truck - the `EnemyData`, its model, `HurtData` and collision - comes from
HSD object pools with no count limit, which grow from the HSD heap. Nothing caps the count but the
heap, and an allocation the heap cannot serve crashes the game rather than failing the spawn. So
each spawn first checks `OSCheckHeap(HSD_GetHeapID())` and spawns only while 512 KB stays free for
the rest of the round; `start` also skips the storm when the heap cannot take the archive on top
of that reserve.

The first time spawning stops in a storm, at the 24-truck cap or the reserve, the event logs which
limit it hit, the truck count, the free heap and the heap spent per truck since the archive
loaded. The per-truck figure includes whatever else the round allocated meanwhile, so it is an
upper bound.

## Knockback

A machine hit by an attack region turns the hit into a push: `Machine_SetupHitReaction`
(0x801e2324) sets `MachineData.knockback_vel` to the knockback direction times the hit's
knockback times `MachineData.knockback_scale`, and the machine adds it to its position every frame
while `knockback_decay` wears it down. The knockback is `base_knockback` plus `kb_distance_factor`
times the relative speed, so scaling both scales the push.

The truck's animation script arms its attack regions at priority 1 and re-arms them with the
archive's values whenever it fires again. The machine and on-foot rider hit passes,
`Machine_UpdateHitColl` (0x801c67a0) and `Rider_UpdateHitColl` (0x8018f95c), read them at priority
9. A proc the event adds to each driver at priority 8 multiplies `base_knockback` and
`kb_distance_factor` of both actors' regions by 4.0. A region that no longer holds the values last
written was re-armed and is scaled again, so a region is never scaled twice. Damage and the hit
reaction stay vanilla.

## Invulnerability

- **Hits.** `EventActorGObj_ProcHitColl` (0x801fc8ec), the actor's own hit pass, runs only while
  `damage_accum_1` is below `param_hp`. Setting the driver's `param_hp` to 0 at spawn turns it
  off, so nothing can knock a truck out; the cart's kind already skips the pass.
- **Inhale.** `EventActorGObj_IsInhalable` (0x802041c8) passes every kind below 0x48 except Gordo
  that has no captor and is being rendered, trucks included. `CODEPATCH_REPLACECALL` swaps its
  three calls - at 0x8019c5e0 in `Rider_TryStartInhale` (0x8019c5ac), 0x8019c6a4 in
  `Rider_InhaleCaptureScan` (0x8019c63c) and 0x8026d0b8 in `Rider_CPUScanInhaleTargets`
  (0x8026cfec) - for a wrapper that returns 0 for a tracked truck while a storm runs and defers
  to the original otherwise.

## Recycling and the End

A truck that gets more than 450 units from every human shrinks away and frees its slot, so the
storm follows the players. Shrinking scales `final_scale` of both actors from its spawn value to 0
over 30 frames, with every attack region switched off, then destroys the truck with
`EventActorGObj_Destroy` (0x801fbf2c).

`end` runs every frame of state 3 and starts every remaining truck shrinking after a random delay
of up to 90 frames, well inside the 300-frame cleanup. `end2` only clears the event's active flag,
because vanilla may already have destroyed a tracked truck; each truck's proc sees the flag and
destroys its own truck. `abort` clears the flag too, so the inhale wrapper never matches a
pointer left over from a round the scene exit cut off; `start` clears the slots.
