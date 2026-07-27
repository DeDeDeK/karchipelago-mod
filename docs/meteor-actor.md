# Meteor Event Actor (0x4E)

The meteor is a special event actor that falls from the sky and damages players on impact. Actor type ID `ACTORID_METEOR = 0x4E` (`enemy.h`), data_index `0x15`. It is created through the same universal factory as every other enemy/event actor, but its behavior is unusual: the vanilla City Trial event drives it with a JObj animation, while a standalone spawn must switch it to a physics-driven fall. Meteor globals are declared in `externals/hoshi/include/event.h`.

## Normal Event Flow

During the City Trial meteor event (`EVKIND_METEOR` = 2 in `event.h`):

1. `event_meteor_start` (0x80110b74) allocates a 0x218-byte state struct -> `r13+0x650` (`stc_meteor_data`), looks up event data -> `r13+0x654` (`stc_meteor_event_data`), and calls `CityItem_SetMeteorEventFlag` (0x80254174).
2. `event_meteor` (0x80110c0c) runs per-frame, creating meteors (`li r0,0x4E`) with randomized XZ positions within zone bounds. Spawn count is driven by `stc_meteor_spawn_count` plus event data, not a fixed cap.
3. Each meteor is created via `EventActor_Create(ACTORID_METEOR)` with packed spawn params in `desc.x3C`.

## EventActor_Create Lifecycle (0x801fbb50)

`EventActor_Create` is the universal actor factory. For meteors:

1. Allocates GOBJ and 0xBC0-byte EnemyData (zeroed).
2. Calls `EventActor_InitFromDesc` (0x801fb53c) — copies desc fields to EnemyData (position, scale, etc.).
3. Spline/path init if available.
4. **Init callback** (`0x8021dfc0`): sets render flags, disables rendering, stores state callback pointers at `ed->xACC`/`ed->xAD0`, and **nulls `ed->xB74` and `ed->xB78`** (collision sphere handles).
5. Creates temporary collision at `ed->x594`.
6. Registers 10+ GObj procs at various priorities.
7. Zeros velocity.
8. Render/audio init.
9. **Post-init callback** (`0x8021e0d4`): destroys the temporary `x594` collision, zeros velocity again, hides the actor via `EventActor_Hide` (0x801fed40 — sets bit 7 of render_flags + disables rendering), disables rendering via `EventActor_DisableRendering`, enters state 14 via `EnemyStateChange(ed, 14, 0)` (0x801fc398), copies `pos` -> `initial_pos` (`xB50`-`xB58`), and clears `in_bounds_flag` (`xB4C = 0`).
10. Returns GOBJ.

## Actor Function Table (at 0x804b4310)

| Index | Offset | Address | Purpose |
|-------|--------|---------|---------|
| 0 | +0x00 | 0x804b42c0 | State function table pointer |
| 1 | +0x04 | NULL | — |
| 2 | +0x08 | 0x8021dfc0 | Init callback |
| 3 | +0x0C | 0x8021e01c | Copy actor_data to ed->x40C (21 words) |
| 4 | +0x10 | 0x8021e058 | Copy actor_data to ed->x40C (duplicate of [3]) |
| 5 | +0x14 | 0x8021e094 | Wrapper for 0x801fed1c |
| 6 | +0x18 | 0x8021e0d4 | Post-init callback |
| 7 | +0x1C | NULL | — |

## Descriptor Field x3C (Packed Spawn Params)

The event spawner packs zone/speed/angle into `desc.x3C` (copied to `ed->x30`). `Meteor_BehaviorInit` reads it:

| Bits | Field | Purpose |
|------|-------|---------|
| 0-7 | Zone index | Indexes `event_data+0x0C` (position table, stride 12: Y offset, speed, angle) |
| 8-15 | Speed index | Indexes `event_data+0x04` (speed table, stride 8: ?, angle_degrees) |
| 22-31 | Approach angle | Horizontal approach angle in degrees (0 = straight down) |

## State Machine (table at 0x804b42c0)

Entries are stride `0x14` — 5 words each: `state_id, func1, func2, func3, func4`.

- func1 (-> ed+0xAB8): called each frame from the priority-1 proc
- func2 (-> ed+0xABC): called each frame from the priority-4 proc (before physics)
- func3 (-> ed+0xAC0): called each frame from the priority-5 proc
- func4 (-> ed+0xAC4): called each frame from the priority-6 proc

The meteor's per-type table has exactly **four** entries — states `-1`, `14`, `15`, `16`. There is **no state 17 entry**: `Meteor_Landing` enters state 17, which has no per-type funcs.

| State | func1 | func2 | func3 | func4 | Purpose |
|-------|-------|-------|-------|-------|---------|
| -1 (default) | 0x8021e15c | NULL | NULL | NULL | func1 calls `EventActor_SetVisibility` then `Meteor_BehaviorInit`. Entered when the JObj animation ends (ProcUpdate detects end -> transitions to default state). |
| 14 | 0x8021e398 | NULL | 0x8021e3f8 (`Meteor_State14_BoundsAndHit`) | 0x8021e5e8 | Boundary detection + hit detection. Entry state from `EventActor_Create`. |
| 15 | 0x8021e934 (blr) | NULL | 0x8021e938 | 0x8021e5e8 | Falling (from BehaviorInit). Physics-driven via vel.Y. func3 times out -> `Meteor_Landing` -> **state 17**. |
| 16 | 0x8021ebfc | NULL | 0x8021ec84 | 0x8021e5e8 | Impact (from state 14 hit detection via `Meteor_HitTransition`). Impact VFX + damage sphere. |

### State 14: boundary detection + hit detection

- **func1** (0x8021e398): one-shot camera effect when `pos.Y < 400.0` and `camera_flag == 0`.
- **func3** (0x8021e3f8, `Meteor_State14_BoundsAndHit`): while `in_bounds_flag == 0`, checks whether pos is within map bounds (XZ range + Y threshold); on pass it recreates the `x594` collision, enables collision rendering via `EventActor_SetCollisionVisible(ed, 1)` (0x80204b4c), and sets `in_bounds_flag = 1`. While `in_bounds_flag == 1` it updates the x594 collision and checks hit results; on a hit it calls `Meteor_HitTransition`.
- **func4** (0x8021e5e8): shared across states 14/15/16 — updates the shadow sphere and the xB74 collision sphere position/radius.

State 14 uses **animation-driven motion** (HSD JObj animation keyframes), not physics velocity. That only works with the event system's specific animation setup and produces no meaningful falling motion for standalone spawns.

### State 15: falling (physics-driven)

Entered by `Meteor_BehaviorInit`. Uses physics velocity (`vel.Y = -speed`) integrated each frame by `EnemyPhysicsProc`.

- **func1**: `blr` (no-op).
- **func3** (0x8021e938): increments `frame_counter` (xB4A). When it exceeds the per-actor max (`actor_data+0x08`) it calls `Meteor_Landing` (0x8021ea5c), which enters **state 17** (`EnemyStateChange(ed, 17, 0)`), zeros velocity, disables rendering, and creates the landing VFX + xB74 damage sphere. State 17 has no per-type func entry, so the physics-driven path lands in state 17, not state 16.
- **func4** (0x8021e5e8): same shared function as state 14.

### State 16: impact (state-14 hit-detection path only)

Entered **only** from state 14 via `Meteor_HitTransition` (ground contact during the animation-driven event path).

- **func1** (0x8021ebfc): updates the xB74 damage sphere while the timer is below threshold.
- **func3** (0x8021ec84): increments the timer and monitors VFX handles (via 0x802361a0). When the VFX finish it cleans up (`EventActor_CleanupCollisionSphere` 0x8021f1bc, `EventActor_CleanupVfxA40` 0x8020c70c, `EventActor_CleanupVfxA3C` 0x8020c6e0) and calls `EventActor_Destroy` (0x801fbf2c). State 16 therefore self-destroys; state 17 does not.

## Meteor_BehaviorInit (0x8021e1a0)

Enter function of default state -1. Called during transitions **to** state -1 (e.g. animation end), not during initial creation.

1. Disables rendering via `EventActor_DisableRendering(gobj)` (sets bit 4 of render_flags) and sets `JOBJ_HIDDEN`.
2. Zeros velocity.
3. Enters state 15 via `EnemyStateChange(ed, 15, 0)`.
4. Reads the zone position offset from `stc_meteor_event_data->+0x0C` (NULL returns early).
5. Reads fall speed from `stc_meteor_event_data->+0x04` (NULL returns early).
6. Sets forward `{0,0,1}` and up `{0,1,0}`.
7. If angle == 0 (straight down): `pos.Y = initial_pos.Y + zone_offset`, `vel.Y = -speed`.
8. If angle != 0 (angled): rotates the direction and applies velocity in all components.
9. Sets collision sphere radii from actor_data.
10. Calls the model transform update and audio init.

## Visibility System

Meteor visibility involves two independent mechanisms, and both must be addressed to make a standalone meteor visible.

**EnemyData render_flags byte** (+0xB08):

- Bit 4 "rendering disabled" — set by `EventActor_DisableRendering(gobj)`, cleared by `EventActor_EnableRendering(gobj)`.
- Bit 7 "invisible" — set by `EventActor_Hide(ed)` (0x801fed40), cleared by `EventActor_SetVisibility(ed)`.
- `EventActor_SetVisibility` clears bit 7, then calls EnableRendering for actor_id < 0x4C or **DisableRendering** for actor_id >= 0x4C. The meteor is 0x4E, so SetVisibility actually leaves it render-disabled.

**JObj JOBJ_HIDDEN flag** (+0x14 of JObj, bit 4) controls whether the HSD render system draws the model. `Meteor_BehaviorInit` sets it via its `EventActor_DisableRendering` call path, so a standalone spawn must clear it with `JObj_ClearFlagsAll(root_jobj, JOBJ_HIDDEN)`.

## Meteor_HitTransition (0x8021e7c4)

Called by state 14 func3 when ground contact is detected:

1. Normalizes current velocity.
2. Computes impact speed from actor_data.
3. Sets velocity to the normalized direction * speed.
4. Enters **state 16** (`EnemyStateChange(ed, 16, 0)`).
5. Disables rendering.
6. Resets collision radii.
7. Creates impact VFX.
8. Creates the xB74 collision sphere (impact damage sphere).
9. Audio fade.

## Standalone Meteor Spawn (Trap)

Implemented in `mods/custom_events/src/spawn_enemy.c` (`SpawnEnemy_MeteorTrap` -> `SpawnMeteorOnPlayer`): a meteor above each human player that falls, impacts with VFX + damage, and self-destructs. `SpawnEnemy_MeteorTrap`, `SpawnEnemy_Random` and `SpawnEnemy_OnBoot` are defined but not called from anywhere — this is scaffolding, not a live trap.

Constants: `METEOR_FALL_SPEED = 8.0f`, `METEOR_DROP_HEIGHT = 400.0f`, `METEOR_SCALE = 2.0f`, `METEOR_LANDING_FRAMES = 210` (3.5s).

### Approach

1. Spawn position is computed per human player: `pos.Y = rider.pos.Y + 400`, and XZ is lead-targeted — offset by `rider.self_vel.{X,Z} * (DROP_HEIGHT / FALL_SPEED)` so the meteor lands on a moving player. `desc.scale = METEOR_SCALE` (2.0).
2. Save the real `stc_meteor_data` / `stc_meteor_event_data` globals.
3. Set fake globals: `*stc_meteor_data = 1`, `*stc_meteor_event_data = &s_fake_event_data` (zone_speed = 8.0, all angles 0).
4. `EventActor_Create` -> post-init callback -> state 14.
5. `Meteor_BehaviorInit(ed)` — reads the fake globals, transitions to state 15 with `vel.Y = -8.0`.
6. Restore the real globals immediately.
7. Fix visibility: `EventActor_EnableRendering(meteor)` (clears bit 4), clear bit 7 manually (`render_flags &= ~0x80`), and `JObj_ClearFlagsAll(root_jobj, JOBJ_HIDDEN)` on the model tree (root from `meteor->hsd_object`).
8. `MeteorDespawnProc` (priority 0x14) increments `ed->lifetime_counter` each frame. When `ed->state == 16` it records the impact frame in `ed->spawn_index`, waits `METEOR_LANDING_FRAMES`, then runs cleanup (`EventActor_CleanupCollisionSphere`, `EventActor_CleanupVfxA3C`, `EventActor_CleanupVfxA40`) and `EventActor_Destroy`.

**Known defect:** `MeteorDespawnProc` waits for `ed->state == 16`, but the physics-driven path `Meteor_BehaviorInit` puts standalone meteors into lands in **state 17** via `Meteor_Landing`, never state 16 (state 16 is reachable only from state 14's hit detection). The despawn timer therefore never fires for these spawns and they leak. The check should test `state == 17` (or `state >= 16`). Vanilla state 17 / `Meteor_Landing` has no func3 self-destruct, which is why `MeteorDespawnProc` exists at all.

### Why BehaviorInit is required

State 14 uses animation-driven motion that depends on the event system's animation setup; without it a standalone meteor hangs in the air. BehaviorInit enters state 15, which uses physics velocity (`vel.Y = -speed`) and falls reliably with no event-system involvement.

### Why save/restore of globals is critical

`Meteor_BehaviorInit` reads `stc_meteor_data` and `stc_meteor_event_data` to compute fall velocity and position offset. These globals are owned by the vanilla meteor event: during an active event they point at live event data, and outside it they may be NULL or stale. Without save/restore, writing fake values corrupts the running event — `*stc_meteor_data = 1` is a non-pointer value that crashes if vanilla meteor code dereferences it on the same frame.

### Global patches (in SpawnEnemy_OnBoot)

- `EventActor_GetParentScale` -> `EventActor_GetParentScale_Safe`: null-checks the parent GOBJ (standalone spawns have no parent).
- `splArcLengthPoint` -> `splArcLengthPoint_Safe`: null-checks the spline pointer (standalone spawns have no path).

### Lifecycle

1. Meteor spawns 400 units above the lead-targeted XZ of a player at scale 2.0; BehaviorInit runs -> state 15, `vel.Y = -8.0`.
2. Vanilla `EnemyPhysicsProc` integrates velocity each frame.
3. State 15 func3 counts frames; on timeout `Meteor_Landing` runs -> **state 17** (impact VFX + xB74 damage sphere). State 16 is not reached on this path.
4. `MeteorDespawnProc` waits 210 frames after impact, then cleans up and destroys — subject to the state-16 defect above.

## Key EnemyData Fields (Meteor)

All offsets are defined in `externals/hoshi/include/enemy.h` (`EnemyData`).

| Offset | Type | Field | Purpose |
|--------|------|-------|---------|
| +0x24 | int | spawn_index | -1 for standalone. Repurposed by `MeteorDespawnProc` to store the impact-frame timestamp. |
| +0x2C | int | lifetime_counter | Generic per-frame counter; `MeteorDespawnProc` uses it as its own frame tick. |
| +0x34 | int | state | Current state ID (written by `EnemyStateChange`) |
| +0x2EC | Vec3 | vel | Added to position each frame by the physics proc |
| +0x2F8 | Vec3 | pos | Current world position |
| +0xB08 | int | render_flags | Byte-accessed. Bit 4: rendering disabled. Bit 7: invisible. |
| +0xB4A | s16 | frame_counter | Counts frames in the current state |
| +0xB4C | s16 | in_bounds_flag | Set to 1 when the meteor enters the map area (state 14) |
| +0xB4E | s16 | camera_flag | Set to 1 once the state-14 camera effect fires |
| +0xB50 | Vec3 | initial_pos | Saved by the post-init callback |
| +0xB5C | float | zone_offset | Height offset from the zone table (read by BehaviorInit) |
| +0xB68 | Vec3 | collision_radii | Base collision sphere radii (from actor_data) |
| +0xB74 | ptr | collision_sphere | Collision sphere handle (nulled by init; created by `Meteor_HitTransition` / `Meteor_Landing`) |

## Key Functions

`Meteor_InitCallback` and `Meteor_PostInitCallback` are descriptive labels used only in this doc — those two are still `zz_XXXXXXXX_` in `GKYE01.map` and are not exported from `link.ld`. Every other name below resolves through `GKYE01.map` or `link.ld`.

| Function | Address | Purpose |
|----------|---------|---------|
| Meteor_BehaviorInit | 0x8021e1a0 | Sets velocity, enters state 15. Reads zone/speed from event globals. Disables rendering. |
| Meteor_HitTransition | 0x8021e7c4 | Ground collision (state-14 path) -> state 16, impact VFX + damage sphere |
| Meteor_Landing | 0x8021ea5c | State-15 timeout -> **state 17**, landing VFX + xB74 damage sphere. Called by state 15 func3. |
| Meteor_State14_BoundsAndHit | 0x8021e3f8 | State 14 func3: bounds check, then hit detection -> `Meteor_HitTransition` |
| Meteor_InitCallback (`zz_8021dfc0_`) | 0x8021dfc0 | Init callback (table[2]): render flags, null xB74/xB78, store hit reaction callbacks |
| Meteor_PostInitCallback (`zz_8021e0d4_`) | 0x8021e0d4 | Post-init callback (table[6]): destroy x594, zero velocity, hide actor, enter state 14 |
| EventActor_Create | 0x801fbb50 | Universal actor factory |
| EventActor_Destroy | 0x801fbf2c | Destroys actor GOBJ + EnemyData |
| EventActor_InitFromDesc | 0x801fb53c | Copies EventActorDesc fields into EnemyData |
| EnemyStateChange | 0x801fc398 | Writes the new state to ed+0x34, resolves per-type func slots |
| EventActor_Hide | 0x801fed40 | Sets bit 7 (invisible) of render_flags, then DisableRendering. Takes EnemyData. |
| EventActor_SetVisibility | 0x801fed74 | Clears bit 7 of render_flags. Calls Enable/DisableRendering based on actor_id (>= 0x4C -> Disable). Takes EnemyData. |
| EventActor_EnableRendering | 0x80204198 | Clears bit 4 of render_flags (+0xB08). Takes GOBJ. |
| EventActor_DisableRendering | 0x802041b0 | Sets bit 4 of render_flags (+0xB08). Takes GOBJ. |
| EventActor_SetCollisionVisible | 0x80204b4c | Enables/disables collision rendering (state 14 bounds entry) |
| EventActor_CleanupCollisionSphere | 0x8021f1bc | Destroys + nulls the xB74 collision sphere |
| EventActor_CleanupVfxA3C / ...VfxA40 | 0x8020c6e0 / 0x8020c70c | VFX handle cleanup |
| event_meteor_start | 0x80110b74 | City Trial meteor event start (allocs the 0x218 state struct -> stc_meteor_data) |
| event_meteor | 0x80110c0c | City Trial meteor event per-frame spawn logic |
| CityItem_SetMeteorEventFlag | 0x80254174 | Marks the meteor event active for the item system |

## Data Addresses

| Data | Address | Description |
|------|---------|-------------|
| stc_meteor_data | r13+0x650 (0x805dd730) | Meteor event state struct pointer. Non-null = event active. |
| stc_meteor_event_data | r13+0x654 (0x805dd734) | Meteor event data pointer (zone table at +0x0C, speed table at +0x04). |
| stc_meteor_spawn_count | r13+0x658 (0x805dd738) | Meteor spawn counter. |
| Meteor actor function table | 0x804b4310 | 8 slots: state table ptr, init/post-init callbacks, actor_data copiers. |
| Meteor state function table | 0x804b42c0 | 4 entries of stride 0x14 (states -1, 14, 15, 16). |
