# Bomb TACs

A custom City Trial event (`CUSTOM_EVKIND_BOMB_TACS`, kind 31, "TACs are dropping bombs!") that
lets six TACs loose across the city for a minute. They throw no items. Each drops a small Bomb
ability bomb about every ten seconds instead, and a TAC that is hit leaves and is replaced
somewhere else. It runs through the `custom_events` framework with the TAC event's own sky preset
(`CITYSKY_DARK_VIGNETTE`) and BGM file 0x35 (`event_syoukinkubi`), a siren intro, a 3600-frame
duration and roll weight 20, and registers `start`, `active`, `end`, `end2` and `abort`.

## The TACs

A TAC is the vanilla event actor (`ENEMYKIND_TAC`, 0x4C) with its vanilla AI. It never follows a
spline. It idles or circles its spawn point until a rider comes within 30 units in front of it,
then runs about the city at random for the rest of its stay. It has no attack region, so touching
one does nothing. `start` calls `Enemy_CheckAndLoad`, then spawns the six.

**Placement.** A spawn spot comes from the CPU road ranges, the left and right road edges CPUs
steer between, all over the city, tunnels and the garage included. A random point between a
random pair's edges, keeping 15% of the width clear at each edge, is dropped onto the floor under
it with `Raycast_Ground` (0x800d1ac4). The floor must hold still, have a normal Y of at least 0.8,
and not be a rail, the sea or the barrier (ground types 26-30). The spot must also be 150 units
from every TAC on the map and 80 from every rider. Up to 40 picks are drawn per spawn.

**The descriptor.** `EventActor_Create` (0x801fbb50) gets the floor point, a random yaw,
`spawn_index` and `spawn_slot` -1, `leash_radius` -1.0 and `variant` -1. TAC reads `variant`
(`EnemyData.variant`) as its stay and compares its grounded frame count against it unsigned at
each animation end, so -1 keeps it on the map until something else sends it off.

**No items.** TAC's three throws - one now and then while it runs, a few while it flees, three when
hit - all call `Tac_ScatterItems` (0x8021c8ec). At boot the event replaces those calls, at
0x8021bccc in `Tac_RunAnim` (0x8021bb78), 0x8021c5a8 in `Tac_FleePri6` (0x8021c504) and 0x8021c67c
in `Tac_OnHit` (0x8021c62c), with a wrapper that skips the event's TACs and passes any other TAC
through, so the vanilla TAC event is untouched.

## Leaving and Respawning

A hit with knockback runs TAC's own reaction, `Tac_OnHit` (0x8021c62c): it turns intangible and
plays its hit animation. The event sees the hit state, stops the TAC's drops and sets its stay to
0, so the animation's end sends it fleeing (`Tac_EnterFlee`, 0x8021c3bc) rather than running on.
A fleeing TAC rises and fades out once above 1000 units; the event starts the fade at once
instead, with `EventActor_ArmFadeOut` (0x801fedd4) over 20 frames and `EventActor_StartFadeOut`
(0x801ff7b0), and the flee state destroys the TAC when the fade is done.

Twelve slots hold the six TACs on the map and the ones leaving it. Event actors skip the floor
kill, so a TAC that runs off the edge of the map would fall for good; one off the ground for more
than 180 frames is destroyed. While fewer than six are on the map, a new TAC spawns every 120
frames.

`end` sends every TAC off the same way on the first frame of state 3, and destroys outright any
TAC still fading in, whose fade-in proc would fight the fade-out over its alpha. `end2` destroys
any TAC still around, and `abort` only forgets the slots for the scene exit to reclaim.

## The Bombs

Each TAC drops its first bomb 60 to 600 frames after it spawns, then one every 540 to 660 frames
while it is on the ground, past its fade-in and in an idle, circle or run state. Each drop first
checks `OSCheckHeap(HSD_GetHeapID())` and skips when less than 512 KB would stay free, because an
allocation the heap cannot serve crashes the game rather than failing the spawn.

A bomb is `WPKIND_BOMB`, the Bomb ability's bomb, made with `Weapon_Create` (0x8021f428) 3 units
behind the TAC and 3 above its feet, with no owner: it hits every rider, CPUs included, and credits
no one. It enters `BOMB_STATE_THROWN` in the same call, since its HELD state would snap it to the
missing owner's hand. Then it is reshaped:

- **Fuse.** The thrown state's `envcoll_callback` sets the bomb off on its first contact, so it is
  replaced. The replacement runs `Weapon_UpdateEnvColl` (0x80221fd4) and zeroes the velocity on a
  contact, so the bomb settles where it lands. It calls `Bomb_DetonationTrigger` (0x80225c8c) once
  the bomb is 120 frames old (`WeaponData.frame_counter`), or as soon as a rider comes within 8
  units. The vanilla contact quake, flash, 14-frame pause and blast follow.
- **Size.** The model is drawn at half size through `WeaponData.model_scale`. The bomb's own
  `BombParams` copy has `blast_scale_start`, `blast_scale_end` and `quake_amplitude` cut to a
  third, so the hitbox regions, the fireball, the wind and the quake are a third of a player's
  bomb. `scale` stays 1.0, since it would shrink the hitbox on top.
- **Sparing TACs.** An ownerless blast hits event actors, which would send a TAC off with its own
  bomb. A proc at priority 8 clears `HITFLAG_VICTIM(HURTKIND_EVENTACTOR)` from both attack regions'
  `hit_flags` every frame of the blast, after the blast script arms them at priority 1 and before
  the hit scans at priority 9. It dies with the bomb.

The blast still breaks boxes, hits items and pushes machines like any bomb's. Bombs dropped near
the end go off on their own fuses during state 3.
