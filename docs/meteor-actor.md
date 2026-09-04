# Meteor Event Actor (0x4E)

The meteor falls out of the sky and damages whatever it lands on. It is `ACTORID_METEOR`
(0x4E in `enemy.h`), data_index 0x15 (archive `EmMeteoData.dat`), and it is built by the same
universal factory as every other enemy and event actor, `EventActor_Create` (0x801fbb50).
What makes it unusual is that its fall parameters come from two globals owned by the City
Trial meteor event rather than from its own descriptor, so spawning one outside that event
means standing in for those globals. They are declared in `externals/hoshi/include/event.h`.

## The vanilla City Trial event

`EVKIND_METEOR` (2) behaves like any other City Trial event:

- `event_meteor_start` (0x80110b74) allocates a 0x218-byte state struct into
  `stc_meteor_data` (r13+0x650, 0x805dd730), resolves the event's data block into
  `stc_meteor_event_data` (r13+0x654), and marks the event active for the item system via
  `CityItem_SetMeteorEventFlag` (0x80254174).
- `event_meteor` (0x80110c0c) runs per frame, creating meteors at randomized XZ positions
  inside the zone bounds. The count comes from `stc_meteor_spawn_count` (r13+0x658) plus the
  event data, not a fixed cap.

`stc_meteor_data` doubles as the "event is running" guard: it is NULL outside the event, and
`Meteor_BehaviorInit` bails early if either global is NULL.

### Packed spawn parameters

The spawner packs zone, speed and approach angle into the descriptor's `x3C` field, which
`EventActor_InitFromDesc` copies into `EnemyData.tier_flags` (+0x30). `Meteor_BehaviorInit`
unpacks it:

| Bits | Field | Resolves through |
|------|-------|------------------|
| 0-7 | zone index | `stc_meteor_event_data->+0x0C`, stride 12: Y offset, fall speed, angle |
| 8-15 | speed index | `stc_meteor_event_data->+0x04`, stride 8: unused, angle in degrees |
| 22-31 | approach angle | horizontal approach angle in degrees; 0 = straight down |

## Creation

The meteor uses the standard per-type descriptor layout, at 0x804b4310: state table
0x804b42c0, init callback 0x8021dfc0, the two actor_data copiers, spawn-slot unregister, and
post-init callback 0x8021e0d4. Two of those slots do meteor-specific work:

- **Init callback** (0x8021dfc0) ground-snaps, disables rendering, points both hit-reaction
  callbacks at 0x8021e9b4, calls `EventActor_FinalizeInit` (0x802042fc), and nulls the two
  collision-sphere handles (`ed+0xB74`/`0xB78`). `EventActor_FinalizeInit` is what hides the
  model: it calls `HSD_JObjSetFlagsAll(root, JOBJ_HIDDEN)` on the model tree.
- **Post-init callback** (0x8021e0d4) runs at the tail of `EventActor_Create`, after all
  procs are registered. It zeroes velocity, hides the actor via `EventActor_Hide`
  (0x801fed40), sets `grounded_active`, disables rendering again, enters **state 14** via
  `EnemyStateChange(ed, 14, ...)`, saves `pos` into `initial_pos` (+0xB50) and clears
  `in_bounds_flag`.

## State machine

The per-type state table at 0x804b42c0 holds four 0x14-byte entries. `EnemyStateChange`
(0x801fc398) indexes it as `entry = table[state - 14]`, so the meteor's states are **14-17**.
Each entry is `{anim_idx, func1, func2, func3, func4}`; the four function pointers land in
`ed+0xAB8`-`0xAC4` and are dispatched by the GObj procs at priorities 1, 4, 5 and 6. The
meteor's func2 slot is NULL in every state, so it never runs pre-physics logic.

| State | anim | func1 (pri 1) | func3 (pri 5) | func4 (pri 6) | Role |
|-------|------|---------------|---------------|---------------|------|
| 14 | -1 | 0x8021e15c | - | - | spawn / hand-off |
| 15 | 0x0E | 0x8021e398 | 0x8021e3f8 | 0x8021e5e8 | falling |
| 16 | 0x0F | 0x8021e934 (`blr`) | 0x8021e938 | 0x8021e5e8 | impact |
| 17 | 0x10 | 0x8021ebfc | 0x8021ec84 | 0x8021e5e8 | landing, then destroy |

The shared func4 (0x8021e5e8) updates the shadow and the `ed+0xB74` collision sphere's
position and radius; it is the same function in states 15-17.

**State 14** is the entry state and does nothing on its own: its func1 (0x8021e15c) sets up
the damage query, calls `EventActor_SetVisibility`, clears `ed+0xB48` and immediately calls
`Meteor_BehaviorInit`, which leaves state 14 for state 15. Nothing else ever enters state 14.

**State 15** is the fall. Motion is pure physics - `Meteor_BehaviorInit` sets `vel.Y` and
`EnemyPhysicsProc` (priority 4) integrates it. func1 (0x8021e398) plays a one-shot falling
sound (0x13001a) the first time `pos.Y` drops below 400.0, latched by `camera_flag`
(+0xB4E). func3 (`Meteor_State14_BoundsAndHit`, 0x8021e3f8 - the map name is off by one
state) does two things: while `in_bounds_flag` (+0xB4C) is 0 it tests the position against an
XZ box (X in -300..100, Z in -200..200) plus a Y ceiling (250 inside the box, 500 outside),
and on entry rebuilds the collision, sets `kb_active` and calls
`EventActor_SetCollisionVisible` (0x80204b4c); once in bounds it updates the collision and
checks for a hit, calling `Meteor_HitTransition` on contact. It also fires two SFX projectiles
at `frame_counter` 2 and 6.

**State 16** is the impact. `Meteor_HitTransition` (0x8021e7c4) enters it: it normalizes the
current velocity, rescales it by the impact speed from actor_data, disables rendering, resets
the collision radii, creates the impact VFX and the `ed+0xB74` damage sphere, and fades the
audio. func1 is a bare `blr`. func3 (0x8021e938) counts `frame_counter` up to
`actor_data[1]->+0x08`, then removes the impact VFX, resets the color animation and calls
`Meteor_Landing`.

**State 17** is the landing and cleanup. `Meteor_Landing` (0x8021ea5c) enters it, zeroes
velocity, disables rendering, and creates the landing VFX plus the `ed+0xB74` damage sphere.
func1 (0x8021ebfc) keeps that sphere positioned and sized while `frame_counter` is below the
actor_data threshold. func3 (0x8021ec84) waits for the landing VFX handles to finish (via
0x802361a0), then destroys the collision sphere (`EventActor_CleanupCollisionSphere`
0x8021f1bc), the two VFX handles (`EventActor_CleanupVfxA3C` 0x8020c6e0 /
`EventActor_CleanupVfxA40` 0x8020c70c), the secondary sphere GOBJ, and finally the actor
itself via `EventActor_Destroy` (0x801fbf2c). **The meteor self-destructs at the end of state
17** - no external cleanup is required on any spawn path.

## Meteor_BehaviorInit (0x8021e1a0)

The function that turns a freshly created meteor into a falling one, and the only place the
two event globals are read:

1. Zeroes velocity and disables rendering.
2. `EnemyStateChange(ed, 15, ...)`.
3. Looks up the zone entry from `stc_meteor_event_data->+0x0C` by `tier_flags & 0xFF`,
   filling `zone_offset` (+0xB5C), fall speed and angle; then overrides the angle from the
   speed table at `+0x04` by `(tier_flags >> 8) & 0xFF`.
4. Sets forward to {0,0,1} and up to {0,1,0}.
5. Straight down (angle 0): `pos.Y = initial_pos.Y + zone_offset`, `vel.Y = -speed`.
   Angled: rotates the basis by the packed approach angle and distributes the speed across
   all three velocity components, offsetting `pos` along the rotated up axis.
6. Sets the collision sphere radii from actor_data, updates the model transform, inits audio.

## Visibility

Making a meteor visible means clearing three independent things, because the creation path
sets all three:

- **`render_flags` bit 4** (byte at `ed+0xB08`), "rendering disabled" - set by
  `EventActor_DisableRendering` (0x802041b0), cleared by `EventActor_EnableRendering`
  (0x80204198). Both take a GOBJ.
- **`render_flags` bit 7**, "invisible" - set by `EventActor_Hide` (0x801fed40). Its counterpart
  `EventActor_SetVisibility` (0x801fed74) clears bit 7 but then branches on actor ID: for
  IDs < 0x4C it enables rendering, for IDs >= 0x4C it **disables** it. The meteor is 0x4E, so
  calling `SetVisibility` leaves it render-disabled - clearing bit 7 by hand and calling
  `EnableRendering` separately is the only way to get both bits clear.
- **`JOBJ_HIDDEN` on the model tree** - set by `EventActor_FinalizeInit` during the init
  callback. Nothing on a standalone path clears it, so it has to be cleared explicitly with
  `JObj_ClearFlagsAll(root_jobj, JOBJ_HIDDEN)` on the root reached from `gobj->hsd_object`.

## Standalone spawn

`mods/custom_events/src/spawn_enemy.c` drops a meteor on every human player:
`SpawnEnemy_MeteorTrap` loops the player slots and calls `SpawnMeteorOnPlayer`. Nothing
invokes it - `SpawnEnemy_MeteorTrap`, `SpawnEnemy_Random` and `SpawnEnemy_OnBoot` are
scaffolding, so the trap and its two global patches are not live.

Constants: `METEOR_FALL_SPEED` 8.0, `METEOR_DROP_HEIGHT` 400.0, `METEOR_SCALE` 2.0,
`METEOR_LANDING_FRAMES` 210.

The spawn position is 400 units above the rider, with XZ lead-targeted by
`rider.self_vel * (DROP_HEIGHT / FALL_SPEED)` so the meteor lands on a moving player. The
descriptor uses `spawn_index = -1`, `spawn_slot = -1`, `bounds_flag = -1.0` - the standalone
sentinels that keep it out of the spawn-slot pool.

The sequence around `EventActor_Create` is what matters:

1. Save the real `stc_meteor_data` / `stc_meteor_event_data`.
2. Write `*stc_meteor_data = 1` and point `*stc_meteor_event_data` at a fake event-data
   struct (zone speed 8.0, all angles 0) laid out to match the two tables `Meteor_BehaviorInit`
   indexes.
3. `EventActor_Create` -> post-init callback -> state 14.
4. Call `Meteor_BehaviorInit(ed)` inline -> state 15, `vel.Y = -8.0`.
5. Restore the real globals immediately.
6. Clear all three visibility flags (see above).
7. Attach `MeteorDespawnProc` at priority 0x14.

**Why BehaviorInit is called by hand.** State 14's func1 would call it anyway, but not until
the priority-1 proc runs on the *next* frame - by which time the real globals are back. The
fake globals only exist for the few instructions between steps 2 and 5, so the call has to
happen inside that window.

**Why the save/restore matters.** These globals belong to the vanilla meteor event. During an
active event they point at live data, and `*stc_meteor_data = 1` is not a pointer - if vanilla
meteor code dereferenced it on the same frame it would crash. Restoring them before returning
keeps the window to a single straight-line stretch of code with no engine calls in between.

**MeteorDespawnProc** ticks `ed->lifetime_counter` every frame, records the frame the meteor
first reaches state 16 in `ed->spawn_index`, and after `METEOR_LANDING_FRAMES` runs the same
cleanup the vanilla state-17 func3 does and destroys the actor. It is a backstop rather than a
requirement: the vanilla chain (state 15 hit -> 16 -> `Meteor_Landing` -> 17 -> VFX complete ->
`EventActor_Destroy`) already destroys the meteor on this path, and whichever fires first
takes the GOBJ and its procs with it.

### Global patches

`SpawnEnemy_OnBoot` replaces two engine functions that assume the spawn-slot/event context:

- `EventActor_GetParentAnimRate` (0x802049b8) -> a null-checked version; standalone spawns have
  no parent GOBJ and the vanilla one dereferences it unconditionally.
- `splArcLengthPoint` (0x80415958) -> a null-checked version; standalone spawns have no
  spline assigned.

## Key addresses

| Symbol | Address | Notes |
|--------|---------|-------|
| `Meteor_BehaviorInit` | 0x8021e1a0 | state 14 -> 15, reads the event globals. Exported via `link.ld`; map row is still `zz_`. |
| `Meteor_HitTransition` | 0x8021e7c4 | state 15 hit -> 16, impact VFX + damage sphere |
| `Meteor_Landing` | 0x8021ea5c | state 16 timeout -> 17, landing VFX + damage sphere |
| `Meteor_State14_BoundsAndHit` | 0x8021e3f8 | state **15** func3: bounds gate then hit detection |
| Meteor per-type descriptor | 0x804b4310 | state table, init/post-init callbacks, actor_data copiers |
| Meteor state table | 0x804b42c0 | 4 entries of 0x14 bytes, states 14-17 |
| `stc_meteor_data` | 0x805dd730 (r13+0x650) | event state struct pointer; non-null = event active |
| `stc_meteor_event_data` | 0x805dd734 (r13+0x654) | zone table at +0x0C, speed table at +0x04 |
| `stc_meteor_spawn_count` | 0x805dd738 (r13+0x658) | spawn counter |
