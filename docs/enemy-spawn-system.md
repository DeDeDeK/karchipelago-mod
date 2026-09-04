# Enemy and Event Actor Spawn System

Every enemy and event actor in Kirby Air Ride is created through one factory,
`EventActor_Create`, from one descriptor struct. Around it sits a spawn manager that owns
per-stage spawn positions, weighted enemy selection and respawn timers. This doc covers the
creation pipeline, archive loading, the spawn-slot system, which modes actually spawn enemies,
and how to bypass the manager and spawn actors standalone.

## Actor Types and Archives

79 actor IDs (0x00-0x4E), all sharing the table at 0x804b22b4 (stride 8,
`{int data_index, int flags}`). `data_index` selects the archive, `flags` selects a variant
inside it. The `ActorID` enum lives in `externals/hoshi/include/enemy.h`.

Tier 0 IDs 0x00-0x17, with the archive each maps to. There are 22 archives in all; the group
symbol is the file base with `Group` appended (`EmBroomData.dat` -> `emBroomDataGroup`).

| ID | Enemy | data_idx | Archive | Copy ability |
|----|-------|----------|---------|--------------|
| 0x00 | Broom Hatter | 0x00 | EmBroomData.dat | - |
| 0x01 | Broom Hatter B | 0x00 | | - |
| 0x02 | Bronto Burt | 0x01 | EmBrontData.dat | - |
| 0x03 | Bronto Burt B | 0x01 | | - |
| 0x04 | Scarfy | 0x02 | EmScarfyData.dat | - |
| 0x05 | Sword Knight | 0x03 | EmSwordData.dat | Sword |
| 0x06 | Cappy | 0x04 | EmCappyData.dat | - |
| 0x07 | Cappy B | 0x04 | | - |
| 0x08 | Wheelie | 0x05 | EmWheelieData.dat | Wheel |
| 0x09 | Phan Phan | 0x06 | EmElephantData.dat | Fire |
| 0x0A | Noddy | 0x07 | EmNoddyData.dat | Sleep |
| 0x0B | Chilly | 0x08 | EmChillyData.dat | Freeze |
| 0x0C | Flappy | 0x09 | EmFlappyData.dat | Wing |
| 0x0D | Plasma Wisp | 0x0A | EmPlasmaData.dat | Plasma |
| 0x0E | Gordo | 0x0B | EmGordoData.dat | - |
| 0x0F | Bomber | 0x0C | EmBombboneData.dat | Bomb |
| 0x10 | Pichikuri | 0x0D | EmPichicriData.dat | Needle |
| 0x11 | Pichikuri B | 0x0D | | Needle |
| 0x12 | Dayl | 0x0E | EmDaylData.dat | Fire |
| 0x13 | Dayl B | 0x0E | | Fire |
| 0x14 | Caller / Shaturn | 0x0F | EmShaturnData.dat | Tornado |
| 0x15 | Walky | 0x10 | EmWalkyData.dat | Mike |
| 0x16 | Waddle Dee Truck | 0x11 | EmDeeTruckData.dat | - |
| 0x17 | Waddle Dee | 0x12 | EmDeeData.dat | - |

### Tiers 1 and 2 (0x18-0x2F, 0x30-0x47)

Both tiers repeat the same 24 enemies in the same `data_index` order with `flags = 1` -
**identical flags**, so T1 and T2 select the same archive sub-entry and are functionally
identical. The only distinction is which spawn pool the City Trial timer draws from. Tier 1
Phan Phan (0x21) is "Heat Phan Phan", a visually distinct fire model; both it and T0 Phan Phan
have the Fire copy ability, because the ability comes from the archive at data_index 6, not
from the tier flags.

The tier flag is used **only at load time**, by `Enemy_GetActorData`, to pick a sub-entry from
the enemy's archive (each archive holds data sets at offsets 0, 4, 8, 12, 16 indexed by the
flags field). Once the result is in `EnemyData.actor_data` (+0x14), the tier flag is neither
stored nor checked again at runtime. What the sub-entry changes: base scale
(`actor_data->+0x00` -> `ed+0x2D0`), movement pattern type/radius/speed
(`actor_data->+0x00->+0x94/98/9C`), the knockback launch multiplier (`+0xA0`), the per-state
animation data (`actor_data->+0x0C + state*0x10`) and the model/textures.

Final visual scale is `mode_scale * spawn_scale * tier_base_scale * global_enemy_scale`, where
mode_scale is 1.0 in Air Ride, 1.1 in Top Ride, 1.2 in City Trial.

### Special and event actors (0x48-0x4E)

| ID | data_idx | flags | Archive | Actor |
|----|----------|-------|---------|-------|
| 0x48 | 0x00 | 2 | EmBroomData.dat | Broom Hatter (special/rider) |
| 0x49 | 0x03 | 2 | EmSwordData.dat | Sword Knight (special/rider) |
| 0x4A | 0x11 | 2 | EmDeeTruckData.dat | Waddle Dee Truck (special/driver) |
| 0x4B | 0x0B | 3 | EmGordoData.dat | Gordo (event) |
| 0x4C | 0x13 | 0 | EmTacData.dat | TAC |
| 0x4D | 0x14 | 0 | EmDynaData.dat | Dyna Blade |
| 0x4E | 0x15 | 0 | EmMeteoData.dat | Meteor |

### Loading

- `Enemy_CheckAndLoad(actor_id)` (0x801fd060) validates the ID and calls `Enemy_LoadFile`. It
  is idempotent - a no-op for an already-loaded archive.
- `Enemy_LoadFile(actor_id)` (0x801fd348) looks up `data_index`, checks the loaded flag at
  `0x8055a210[data_index]`, and loads via `lbLoadArchive` if needed.
- `Enemy_LoadStageEnemies()` (0x800f25b4) runs during `grLoadStage` and loads every archive in
  the stage's enemy ID list, event actors included. It is **skipped entirely in City Trial
  Free Run** (`major == MJRKIND_CITY && city_mode == CITYMODE_FREERUN`), so nothing is loaded
  there by default.
- `Enemy_GetActorData(actor_id)` (0x801fd498) is the runtime lookup: table at 0x804b22b4 for
  `{data_index, flags}`, archive root from `0x8055a228[data_index]`, then the flags-selected
  sub-entry. It returns 0 if the archive is not loaded, and `EventActor_Create` uses it
  internally - an actor whose archive is missing fails to create properly.

## EventActor_Create (0x801fbb50)

The universal factory. It takes a pointer to a 0x60-byte `EventActorDesc` (declared in
`externals/hoshi/include/enemy.h`) and accepts any actor ID 0x00-0x4E. Descriptors are built
by `Enemy_SpawnActor`, `Enemy_SpawnActorMode2` and the `event_dynablade` / `event_meteor` event
starters; `EventActor_InitFromDesc` (0x801fb53c) copies the fields into `EnemyData`.

The descriptor fields with non-obvious semantics:

- `spawn_index` (+0x2C) and `spawn_slot` (+0x30) are **-1 for standalone actors**. A non-(-1)
  `spawn_index` is what makes the `lifetime` field (+0x38) take effect at all.
- `bounds_flag` (+0x50) of -1.0 means "use the default collision bounds" (the all-zero block at
  0x804b1d40); any other value selects the `custom_bounds` vector at +0x44.
- `+0x3C` is the parent GOBJ for the special child actors 0x48-0x4A and doubles as their
  variant flag; it is 0 for everything else.
- `scale` (+0x28) feeds damage and size calculations, not just the model matrix.

### Creation flow

1. Create a GOBJ with entity class 21 (0x15) and plink 12 (0xC, `GAMEPLINK_ENEMY`).
2. Allocate the 0xBC0-byte `EnemyData` with `HSD_ObjAlloc` and memset it.
3. Attach it with `GObj_AddUserData` at priority 21, destructor
   `EventActor_GObjDestroyHandler` (0x801fcca0).
4. `EventActor_InitFromDesc` copies the descriptor in.
5. `Enemy_GetActorData(actor_id)` resolves the per-type data.
6. Load the JObj model from the archive.
7. Call the per-type init callback from `PTR_PTR_804b1d98[actor_id]`.
8. Attach the ten GOBJProcs below.
9. Register a GXLink with `Enemy_GX` (0x801fd158), priority 9, render pass 1.
10. Dispatch the descriptor's post-init callback, which ground/spline re-snaps the actor and
    puts it into its default state 0x0E.

### GOBJProc priorities

All ten are registered unconditionally on every actor, whatever its type.

| Priority | Function | Address | Purpose |
|----------|----------|---------|---------|
| 0 | `EventActor_ProcResetDamage` | 0x801fc670 | Zeros per-frame damage accumulators via `HurtData_ResetPerFrame` |
| 1 | `EventActor_ProcUpdate` | 0x801fc698 | HSD anim advance + animation-script machine + `state_func1` dispatch (calls `EventActor_AnimProcessor` 0x80200838 through `EventActor_UpdateState`) |
| 4 | `EnemyPhysicsProc` | 0x801fc6fc | `state_func2` dispatch, `vel += accel`, `pos += vel`, OOB floor kill (skipped for actor_id >= 0x4C) |
| 5 | `EventActor_ProcStateActive` | 0x801fc7c4 | `state_func3` dispatch - main per-state AI logic |
| 6 | `EventActor_ProcSharedModel` | 0x801fc7f8 | Shadow update, `state_func4` dispatch, position/direction into the model matrix |
| 7 | `EventActor_ProcPerType` | 0x801fc848 | `per_type_cb` dispatch, HurtData update, position snap |
| 8 | `EventActor_ProcHitCollInit` | 0x801fc8e8 | No-op stub (`blr`) |
| 9 | `EventActor_ProcHitColl` | 0x801fc8ec | HitColl setup; checks `damage_accum_1` (+0x994) against `param_hp_threshold` (+0x3B0) |
| 10 | `EventActor_ProcDamage` | 0x801fc9f0 | Reads HurtData output, calls `giveEnemyDamage`, dispatches `hit_reaction_cb2` |
| 21 | `EventActor_ProcFinal` | 0x801fcabc | `pos` -> `pos_prev`, ground-state flags, lifetime/despawn, OOB destroy |

### Parent/child actors

Some enemies are composite - a body plus a rider or driver. Broom Hatter (0x00/0x01) spawns
SP Broom Hatter (0x48), Sword Knight (0x05) spawns SP Sword Knight (0x49), Waddle Dee Truck
(0x16) spawns SP Waddle Dee Truck (0x4A).

`EventActor_SpawnChild` (0x801fcda0) creates the child and sets `child.parent_gobj` to the
parent. The child's state functions read it back: `EventActor_FollowParent` (0x80219eec) takes
the parent's animation rate then copies position data, `EventActor_CopyParentState`
(0x80219fd4) copies position and direction, and `EventActor_GetParentAnimRate` (0x802049b8)
reads `parent_gobj->userdata + 0x2B0` (`anim_rate`).

**`EventActor_GetParentAnimRate` crashes on a null parent** (DAR = 0x2C null deref). For a
standalone spawn of an actor whose states call it, `parent_gobj` (EnemyData+0x08) must be set
to a valid GOBJ after creation - pointing it at a player's machine GOBJ both avoids the crash
and makes the actor track that player.

### Standalone spawn

Spawning outside the spawn-slot system, in any mode:

```c
Enemy_CheckAndLoad(ACTORID_WADDLE_DEE); // idempotent; required in CT Free Run and Air Ride

EventActorDesc desc;
memset(&desc, 0, sizeof(desc));
desc.actor_id = ACTORID_WADDLE_DEE;
desc.position = spawn_pos;
desc.forward = (Vec3){ 0.0f, 0.0f, 1.0f };
desc.up = (Vec3){ 0.0f, 1.0f, 0.0f };
desc.scale = 1.0f;
desc.spawn_index = -1;
desc.spawn_slot = -1;
desc.bounds_flag = -1.0f;

GOBJ *actor = EventActor_Create(&desc);
if (actor)
    ((EnemyData *)actor->userdata)->parent_gobj = Ply_GetMachineGObj(0);
```

Constraints:

- **Memory.** Each archive occupies heap; loading all 22 at once may exceed what is available.
- **Position.** Regular enemies (0x00-0x47) have patrol AI that references their spawn
  position; event actors (0x48-0x4E) move autonomously.
- **Cleanup.** These are GOBJs on plink 0xC - destroy with `GObj_Destroy` or let the scene
  change collect them.
- **Collision.** Hurt/hit collision comes free with the GOBJProcs; the actor interacts with
  machines and riders through the standard collision system.

## Spawn Manager

Four globals hold the manager state, all r13-relative:

| Address | r13 offset | Name | Description |
|---------|-----------|------|-------------|
| 0x805DD708 | +0x628 | `stc_enemy_init_flag` | u16, 1 during init, 0 when done |
| 0x805DD70C | +0x62C | `stc_spawn_slots` | Array of 4 SpawnSlot structs |
| 0x805DD710 | +0x630 | `stc_enemy_spawn_data` | Per-stage spawn config (`EnemySpawnData`) |
| 0x805DD714 | +0x634 | `stc_enemy_mgr` | EnemyMgr struct |
| 0x805DE334 | +0x1254 | `stc_event_actor_list` | Global EventActor linked-list root |

### EnemyMgr (0x3C bytes)

| Offset | Type | Name | Description |
|--------|------|------|-------------|
| +0x00 | ptr | gobj | Manager GOBJ |
| +0x04 | ptr | spawn_slot_data | Per-position extended data array, stride 0x5C |
| +0x08 | ptr | spawn_group_pool | Spawn group data: enemy ID + weight pairs |
| +0x0C | u32 | frame_counter | Incremented every Think frame |
| +0x10 | u32 | total_spawns | Lifetime spawn count |
| +0x14 | u16 | active_count | Current alive enemy count |
| +0x16 | u16 | active_event_count | Alive "event" enemies (actor_id >= 0x18), CT mode 3 only |
| +0x18 | u16 | (reserved) | Zeroed at init, never read or written again |
| +0x1A | s16 | slots_initialized | Count of initialized spawn slots |
| +0x1C | s16 | last_spawn_slot | Last slot index used |
| +0x20 | u32[3] | ct_time | `City_GetMinSecMs` output |
| +0x2C | u32 | ct_duration_base | Base time in 60ths |
| +0x30 | u32 | ct_duration | Total match duration in 60ths |
| +0x34 | float | time_progress | current/total (0.0-1.0), drives CT difficulty scaling |
| +0x38 | u32 | ct_next_spawn_pos | CT rotating spawn position index |

### SpawnSlot (0x48 bytes, 4 of them)

One per player in Air Ride.

| Offset | Type | Name | Description |
|--------|------|------|-------------|
| +0x00 | ptr | gobj | Spawned EventActor GOBJ, null if empty |
| +0x04 | Vec3 | spawn_pos | Spawn position |
| +0x10 | Vec3 | world_pos | `spawn_pos + forward * -80.0` |
| +0x1C | Vec3 | forward | Forward direction |
| +0x28 | Vec3 | ground_normal | Ground normal |
| +0x34 | s16 | respawn_timer | Countdown, decremented each frame in City Trial |
| +0x40 | s16 | forward_sign | Sign of the forward direction |
| +0x42 | s16 | up_sign | |
| +0x44 | u8 | flags | Bit 7 is_spawned, bit 6 player_nearby |
| +0x46 | u16 | initialized | 1 = valid data |

### Per-position extended data (stride 0x5C)

The array at `EnemyMgr+0x04`, one entry per spawn position defined in the stage data. This is
the runtime working copy - it is not the stage-file entry array (stride 0x38), which is a
common confusion.

| Offset | Type | Description |
|--------|------|-------------|
| +0x00 / +0x0C / +0x18 | Vec3 | Position / direction / ground normal |
| +0x24 | s16 | spawn_slot (-1 = unassigned) |
| +0x26 | s16[4] | Enemy ID entries |
| +0x2E | s16[4] | Weight entries |
| +0x34 | s16 | respawn_timer |
| +0x4A | u8 | flags (bit 7 = occupied) |
| +0x4C | s16 | actor_id (-1 if empty) |
| +0x4E | s16 | spawn tracking counter |
| +0x50 | s16 | owning slot index |
| +0x58 | ptr | actor_gobj |

### Stage spawn data

`stc_enemy_spawn_data` points at an `EnemySpawnData` (declared in `enemy.h`): a spawn count, a
pointer to the `EnemySpawnEntry` table (stride 0x38, indexed by position index), a
`secondary_table` of meta-enemy sub-tables which may be NULL, and a config struct. The config
carries `max_respawn_delay` (+0x24), a random respawn range (+0x26) and the **mode** (+0x28),
which is 1 for Air Ride courses, 2 for Kirby Melee 1 and 3 for Kirby Melee 2.

`EnemySpawnEntry` is loaded verbatim from the stage `.dat` and only ever read. Its
`location_index` (+0x00) indexes the stage enemy-position table (`GrData+0x138`, stride 0x24 of
three Vec3s), resolved by `loadEnemy_spawnXYLocation` (0x800d0cd4) into the runtime per-position
extended data - not back into the entry. `scale` (+0x30) is negated if negative and defaults to
1.0 if zero; `variant` (+0x34) goes into the descriptor's parent/variant slot.

The id, weight and lifetime fields sit at **mode-dependent offsets** - the three modes pack them
differently, which is why `enemy.h` models them as a union keyed on `config.mode`:

| Mode | ids | weights | lifetime base/range |
|------|-----|---------|---------------------|
| 1 (Air Ride courses) | s16[4] @ +0x1E | s16[4] @ +0x26 (-1 terminates) | +0x1A / +0x1C |
| 2 (Kirby Melee 1) | s16 enemy_id @ +0x06 | s16 weight_columns[N] @ +0x08, one per meta-enemy category | +0x24 / +0x26 |
| 3 (Kirby Melee 2) | s16[5] @ +0x06 | s16[5] @ +0x10 (-1 terminates) | +0x1A / +0x1C |

`location_index`, `scale`, `lifetime` and `variant` are read by `Enemy_SpawnActor` (modes 1/3)
and `Enemy_SpawnActorMode2` (mode 2); the id/weight arrays are read by `Enemy_SpawnerDecide`
(modes 1/3) and `Enemy_SpawnerDecideMode2` (mode 2). Lifetime 0 or 1 means "no lifetime";
above 1 it gets plus/minus random jitter from the range field.

`stc_enemy_spawn_data` is NULL in the City Trial city map (timed or Free Run), in Top Ride, and
in stadiums that do not use stage-based spawning. The city's event actors (TAC, Dyna Blade,
Meteor) are loaded by `Enemy_LoadStageEnemies` but never populate it - they come in through the
event system rather than the spawn-position pool.

### Enemy_SpawnActor (0x800f13a8)

Takes `(spawn_slot_index, enemy_id_packed, position_index)`. If `enemy_id_packed` has bits in
the 0xFF00 mask, the variant is extracted as `(packed >> 8) - 1` with the base ID being
`packed & 0xFF`, and the extracted enemy is recorded into the group ring buffer at
`EnemyMgr+0x08` (12-byte entries, 5-slot mod-5 ring). The base ID (< 0x4F) then builds the
descriptor and calls `EventActor_Create`. Scale comes from the stage entry's +0x30 and lifetime
from the position data, absolute value, jittered when above 1.

Meta-enemy expansion (IDs 0x50-0x5E) does **not** happen here - `Enemy_SpawnerDecide` resolves a
meta ID to a concrete base ID before calling in.

### Enemy_SpawnerDecide (0x800f1a14)

Decides **what** to spawn at a position; mode-branched. The Air Ride path:

1. Check the flags byte at extended data +0x4A (occupied, player nearby, misc state).
2. Read the weight table from the extended data - up to 4 entries, -1 terminated.
3. Sum the weights and pick randomly.
4. **Meta-enemy expansion** for IDs in (0x4F, 0x5F), i.e. 0x50-0x5E: `group_index = id - 0x50`
   (`addi r5, r29, -320` at 0x800f1bd4 with `r29 = id * 4`) indexes `secondary_table`, and a
   weighted random pick is made from that sub-table of `{enemy_id, weight}` short pairs. The
   sub-table is **terminated by `weight == -1`, not by `enemy_id == -1`** - a leading
   `{-1, N}` pair is a legitimate "spawn nothing" outcome, and several vanilla groups open with
   one. The loop repeats until the resolved ID is no longer a meta-enemy. The separate
   `id - 0x4F` value is the +1-biased variant packed into the descriptor's high byte, not a
   table index.
5. Call `Enemy_SpawnActor` with the concrete base ID.

The City Trial path is the same flow with time-based difficulty scaling from
`EnemyMgr.time_progress` biasing toward higher-tier enemies as the match progresses. Mode 2 is
two-stage: `Enemy_SpawnerDecideMode2` (0x800f0efc) weighted-picks a meta-enemy category from
`secondary_table[0]` (biased by `time_progress`), then picks a concrete enemy from that
category's per-entry weight column.

### The two manager procs

Both update slot positions and vectors and scan the EventActor linked list for occupancy.

`Enemy_Think` (0x800f3904, Air Ride) keeps the 4 spawn slots tied to players, refreshing
position, direction and ground normal each frame and setting the occupancy bits. It makes no
spawn decisions - those live in a separate spawn loop.

`Enemy_CityTrialThink` (0x800f33c0) recomputes `time_progress` each frame, does the same
slot/occupancy update, then runs a spawn decision phase in one of three modes: sequential
(iterate from `last_spawn_slot` through all positions), City Trial proper (clear all position
weights/flags and iterate all positions, with a spawn cap of `slots_initialized * 2` indexed
into the config table - spawning stops once `active_count` reaches it), or Free Run (random
starting position, otherwise the sequential loop). Each position decrements its respawn timer,
then calls `Enemy_SpawnerDecide`.

### Despawn paths

`Enemy_UnregisterFromSpawnSlot` (0x800f3b28) runs when an enemy dies or despawns. It returns
immediately if `spawn_slot` is negative (a standalone actor), verifies the GOBJ matches the
slot entry, clears the slot (`actor_id = -1`, null GOBJ, flags cleared), decrements
`active_count` (City Trial Free Run tracks tier >= 0x18 separately in `active_event_count`), and
assigns a fresh respawn timer from the config's base (+0x24) and random range (+0x26).

`EventActor_Destroy` (0x801fbf2c) is **recursive depth-first**. The child chain (each node's
`child_gobj` at EnemyData+0x04) is walked with the first five levels manually unrolled and a
sixth level recursing back into `EventActor_Destroy`. Per node, deepest child first: destroy
the child and clear the parent's `child_gobj`; conditionally unregister from the spawn slot;
clean up the VFX/SFX handles (`EventActor_CleanupVfxA3C` 0x8020c6e0, `EventActor_CleanupVfxA40`
0x8020c70c); call `GObj_Destroy`.

That unregister is **not** `Enemy_UnregisterFromSpawnSlot` but a separate path
(`zz_80114524_` -> `zz_801218a4_`), and it is heavily gated: only when `kind < 0x48`, the actor
is not Tier 1/2, and `config.mode != 3`. In effect only Tier 0 enemies outside City Trial take
it; Tier 1/2 enemies and all City Trial enemies skip it.

## Which Modes Spawn Regular Enemies

Regular per-type AI enemies spawn **only** when the loaded stage's `.dat` ships a non-NULL
enemy-spawn array. The decision is data-driven, never derived from a stadium or mode constant:

1. `grLoadStage` -> `Enemy_InitPositionData` (0x800f2634) -> the enemies-enabled gate
   `zz_8000a228_` (bit 6 of GameData+0xAA6, effectively always on) -> the spawn-data provider
   `zz_800da4c4_`.
2. `zz_800da4c4_` reads `GrData+0x28`. **If that is NULL there are no enemies** - it returns
   NULL. Otherwise it indexes a per-stage `EnemySpawnData` array (stride 0x18) by the stage's
   EnemyposId and the result is stored to `stc_enemy_spawn_data`.
3. `Enemy_InitPositionData` then bails to the no-spawn path if `spawn_count == 0`,
   `config == 0` or `spawn_entries == 0`. Otherwise the **mode is read straight from
   `config->mode` (config+0x28)**, baked into the stage file, never computed from a stadium
   kind.

| Mode group | Stage file | Spawns regular enemies | mode |
|------------|-----------|------------------------|------|
| Air Ride courses 0-7 | GrPlants1 / Heat2 / Desert1 / Ice1 / Sky2 / Valley2 / Machine2 / Check2 | Yes | 1 |
| Air Ride, Nebula Belt | GrSpace2 | No (NULL spawn array) | - |
| City Trial, Kirby Melee 1 | GrPasture1 | Yes | 2 |
| City Trial, Kirby Melee 2 | GrColosseum5 | Yes | 3 |
| City Trial, other stadiums | GrZeroyon* / GrSimple* / GrJump* / GrColosseum1,3 / GrDedede1 | No (NULL spawn array) | - |
| City Trial, open city (timed) | GrCity1 | No - `GrData+0x28` is non-NULL but the selected entry is empty (spawn_count 0), so the spawner is inert | - |
| City Trial, Free Run | GrCity1 | No - `Enemy_LoadStageEnemies` is skipped entirely | - |

Kirby Melee 1 and 2 are the only City Trial events that spawn the regular AI enemy pool. Every
other City Trial context produces only event actors (TAC, Dyna Blade, Event Gordo, Meteor),
which come through the event system.

### StageKind vs GroundKind vs StadiumKind

Three different "kind" numbers get conflated here, and `stage.h` defines a distinct enum for
each of the two that mod code touches.

- **StageKind** (`stage.h`, `STAGEKIND_*`) is the 0-59 stage *selection* index, returned by
  `Gm_GetCurrentStageKind()` (GameData+0xA97) and `stGetCurrentStageKind()` (r13[0x7F8]
  cache). For Air Ride it equals the `AirRideCourse` value (0-8); City Trial stadiums occupy
  9-33, each stadium member being `STKIND_x + 10`. `STAGEKIND_KIRBYMELEE1 = 17`,
  `STAGEKIND_KIRBYMELEE2 = 18`.
- **GroundKind** (`stage.h`, `GR_*`) is which ground geometry file loads: an index into the
  stage-file table in `main.dol` at 0x804A2FFC. `0 = GrPlants1 ... 8 = GrIce1, 9 = GrCity1,
  10-13 = GrZeroyon1/3/4/5, 14 = GrPasture1, 15 = GrColosseum1, 16 = GrColosseum3,
  17 = GrColosseum5, 18-20 = GrJump1/2/3, 21 = GrDedede1, 23-25 = GrTest*,
  26-27 = GrSimple*`. It is `GrObj.gr_kind` (+0x04), returned by `Gr_GetCurrentGrKind()`. So
  Kirby Melee 1 is GroundKind 14 and Kirby Melee 2 is GroundKind 17.
- **StadiumKind** (`stadium.h`, `STKIND_*`) is the 0-based City Trial event index:
  `STKIND_MELEE1 = 7`, `STKIND_MELEE2 = 8`.

`Stage_GetGrKindFromStageKind` (0x80261ce8, exported to mod code as
`Gm_GetGrKindFromStageKind`; table `*(*(r13+0x7FC))`, stride 0x58, GroundKind at +0x00) maps
StageKind to GroundKind: 17 -> 14 (GrPasture1), 18 -> 17 (GrColosseum5), matching the `.dat`
evidence above. StageKind uses menu order and GroundKind uses file order, so the two spaces
coincide only at 0/1/2 and at City Trial (9) and diverge everywhere else - Machine Passage is
StageKind 6 but GroundKind 5. Because City Trial is 9 in both, a "current stage is City Trial"
check is space-agnostic; the distinction only bites once you touch a stadium or a re-ordered
Air Ride course. The yakumono per-grkind hook table is also indexed by physical GroundKind.

## Air Ride Per-Course Rosters

Which enemies each Air Ride course can spawn, from the vanilla stage `.dat` spawn tables
(mode 1: `ids[4]` at entry +0x1E, `weights[4]` at +0x26, stride 0x38; meta-enemy IDs 0x50-0x5E
expanded through `secondary_table`). An enemy is listed if it appears with a positive weight at
any spawn position on that course; T0/T1/T2 of an enemy share one copy ability and are
collapsed. Do not infer the stage file from the course name - the mapping is the
StageKind -> GroundKind one above. `GrPasture1` and `GrColosseum5` are the two Kirby Melee
stadiums, not Air Ride courses.

Copy-ability enemies are bold with their ability; the rest are ability-less "garbage" enemies,
which is what the "swallow garbage enemies" checklist cell accepts.

| StageKind | Course | Stage file | Copy-ability enemies | Garbage enemies |
|---|---|---|---|---|
| 0 | Fantasy Meadows | GrPlants1.dat | **Bomber** (Bomb), **Dayl** (Fire), **Noddy** (Sleep), **Phan Phan** (Fire), **Pichikuri** (Needle), **Pichikuri B** (Needle), **Sword Knight** (Sword), **Walky** (Mic) | Bronto Burt, Bronto Burt B, Broom Hatter, Cappy, Scarfy, Waddle Dee, Waddle Dee Truck |
| 1 | Magma Flows | GrHeat2.dat | **Bomber** (Bomb), **Dayl** (Fire), **Flappy** (Wing), **Noddy** (Sleep), **Phan Phan** (Fire), **Plasma Wisp** (Plasma), **Sword Knight** (Sword), **Walky** (Mic) | Bronto Burt, Broom Hatter |
| 2 | Sky Sands | GrDesert1.dat | **Bomber** (Bomb), **Caller/Shaturn** (Tornado), **Noddy** (Sleep), **Phan Phan** (Fire), **Pichikuri** (Needle), **Pichikuri B** (Needle), **Sword Knight** (Sword), **Walky** (Mic), **Wheelie** (Wheel) | Bronto Burt, Bronto Burt B, Broom Hatter, Cappy, Waddle Dee, Waddle Dee Truck |
| 3 | Frozen Hillside | GrIce1.dat | **Bomber** (Bomb), **Chilly** (Freeze), **Dayl** (Fire), **Noddy** (Sleep), **Phan Phan** (Fire), **Pichikuri** (Needle), **Pichikuri B** (Needle), **Sword Knight** (Sword) | Bronto Burt B, Broom Hatter, Scarfy, Waddle Dee Truck |
| 4 | Beanstalk Park | GrSky2.dat | **Bomber** (Bomb), **Caller/Shaturn** (Tornado), **Flappy** (Wing), **Noddy** (Sleep), **Phan Phan** (Fire), **Pichikuri** (Needle), **Pichikuri B** (Needle), **Walky** (Mic) | Bronto Burt, Broom Hatter, Cappy, Waddle Dee Truck |
| 5 | Celestial Valley | GrValley2.dat | **Bomber** (Bomb), **Caller/Shaturn** (Tornado), **Chilly** (Freeze), **Flappy** (Wing), **Pichikuri** (Needle), **Pichikuri B** (Needle), **Plasma Wisp** (Plasma), **Sword Knight** (Sword) | Bronto Burt, Broom Hatter, Cappy, Scarfy, Waddle Dee, Waddle Dee Truck |
| 6 | Machine Passage | GrMachine2.dat | **Bomber** (Bomb), **Dayl** (Fire), **Phan Phan** (Fire), **Pichikuri** (Needle), **Pichikuri B** (Needle), **Plasma Wisp** (Plasma), **Walky** (Mic), **Wheelie** (Wheel) | Bronto Burt B, Gordo, Waddle Dee, Waddle Dee Truck |
| 7 | Checker Knights | GrCheck2.dat | **Bomber** (Bomb), **Caller/Shaturn** (Tornado), **Chilly** (Freeze), **Flappy** (Wing), **Noddy** (Sleep), **Phan Phan** (Fire), **Plasma Wisp** (Plasma), **Sword Knight** (Sword), **Walky** (Mic), **Wheelie** (Wheel) | Bronto Burt, Broom Hatter, Waddle Dee, Waddle Dee Truck |
| 8 | Nebula Belt | GrSpace2.dat | - | - (no enemy spawn table) |

The reverse view. The Archipelago world gates its "swallow this enemy" checklist cells on
`HasAny` of the courses listed here when `air_ride_courses_gated` is on, using the Sword Knight,
Wheelie, Chilly and Plasma Wisp rows.

| Enemy | Ability | Spawns on |
|---|---|---|
| Sword Knight | Sword | Fantasy Meadows, Magma Flows, Sky Sands, Frozen Hillside, Celestial Valley, Checker Knights |
| Wheelie | Wheel | Sky Sands, Machine Passage, Checker Knights |
| Phan Phan | Fire | Fantasy Meadows, Magma Flows, Sky Sands, Frozen Hillside, Beanstalk Park, Machine Passage, Checker Knights |
| Noddy | Sleep | Fantasy Meadows, Magma Flows, Sky Sands, Frozen Hillside, Beanstalk Park, Checker Knights |
| Chilly | Freeze | Frozen Hillside, Celestial Valley, Checker Knights |
| Flappy | Wing | Magma Flows, Beanstalk Park, Celestial Valley, Checker Knights |
| Plasma Wisp | Plasma | Magma Flows, Celestial Valley, Machine Passage, Checker Knights |
| Bomber | Bomb | Fantasy Meadows, Magma Flows, Sky Sands, Frozen Hillside, Beanstalk Park, Celestial Valley, Machine Passage, Checker Knights |
| Pichikuri | Needle | Fantasy Meadows, Sky Sands, Frozen Hillside, Beanstalk Park, Celestial Valley, Machine Passage |
| Pichikuri B | Needle | Fantasy Meadows, Sky Sands, Frozen Hillside, Beanstalk Park, Celestial Valley, Machine Passage |
| Dayl | Fire | Fantasy Meadows, Magma Flows, Frozen Hillside, Machine Passage |
| Caller/Shaturn | Tornado | Sky Sands, Beanstalk Park, Celestial Valley, Checker Knights |
| Walky | Mic | Fantasy Meadows, Magma Flows, Sky Sands, Beanstalk Park, Machine Passage, Checker Knights |

## Damage and Knockback

Enemies have no traditional HP - death comes from per-hit knockback, not accumulated damage.

Incoming damage is first scaled by **0.4** (`Enemy_ScaleDamage` 0x8020b71c, reading param table
+0x04), then classified into a response tier 0-3 by `Enemy_ClassifyDamageTier` (0x8020b740)
against three float thresholds at the table's +0x08, +0x0C and +0x10 (10.0, 21.0, 32.0).
`Enemy_ApplyKnockback` (0x8020b784) indexes four per-tier arrays by that tier (`ed+0xA1C`) and
writes the results into `EnemyData`:

| Tier | Damage | Stun frames (ed+0xA18, +0x60) | Launch speed (ed+0x9D8, +0x50) | KB scale (ed+0x878, +0x40) | KB base magnitude (+0x30) |
|------|--------|-------------------------------|--------------------------------|----------------------------|---------------------------|
| 0 | < 10.0 | 2 | 2.0 | 1.0 | 20 |
| 1 | 10.0 - 20.9 | 4 | 3.0 | 0.8 | 30 |
| 2 | 21.0 - 31.9 | 6 | 4.0 | 0.6 | 40 |
| 3 | >= 32.0 | 8 | 5.0 | 0.5 | 50 |

These values are **global** - shared by every enemy type and tier - and static: they live in
`Enemy.dat` (public `emDataAll`), loaded by `Enemy_LoadCommonParams` (0x801fd580), which stores
the table pointer to `*0x805dd878`. The magnitude actually passed to the launch is
`int(KB_base_mag[tier] * actor_data->+0x00->+0xA0 * KB_scale[tier])`, clamped to at least 1, so
the per-tier archive launch multiplier (+0xA0) further scales how far a given enemy flies.
Higher-tier enemies can carry a lower multiplier, flying less far from the same hit and so
being harder to knock out of the arena.

The death sequence: a hit sets `stun_frames` (ed+0xA18) from the response tier;
`EnemyState_AnimExit` (0x8020c558, func3 for states 0x00-0x08) decrements it each frame during
knockback; at zero the enemy enters the death state; `death_timer` (ed+0xA28) then counts up and
the actor is destroyed after 30 frames.

`damage_accum_1` (ed+0x994) and `damage_accum_2` (ed+0x998) track total damage received, capped
at 9999. `giveEnemyDamage` (0x8020b680) writes them but nothing reads them for death logic -
they are cosmetic.

## Spline Path Following

Enemies can follow the splines embedded in stage data (`GrData->spline` at +0x14).

`EnemyPath_Init` (0x80206e2c) calls `Spline_FindNearest` (0x800cf07c) for the segment nearest
`ed->pos`. On a hit it stores the segment index to `ed->spline_segment` (+0x5DC), the arc
parameter to `ed->spline_arc_param` (+0x5FC), and picks the curve pointers
`spline_primary`/`spline_secondary` (+0x5D4/+0x5D8) according to `spline_direction` (+0x5F8).
On a miss it sets bit 2 of `ed+0xB0A`, the alternative-movement flag.

`EnemyPath_FollowUpdate` (0x80209ce4) runs each frame for enemies whose `path_active_flag`
(+0xA8C) is -1.0: it confirms the stage has splines via `Spline_GetCount` (0x800cf38c),
evaluates `splArcLengthPoint` on the enemy's spline to get a direction, and advances the
position along the path.

A standalone-spawned actor needs these set after `EventActor_Create` returns before it will
follow a path:

```c
ed->spline_path_ready = 1;    // +0x654
ed->spline_direction = 1;     // +0x5F8, 1 = forward
ed->path_active_flag = -1.0f; // +0xA8C, enables path following
EnemyPath_Init(ed);
```

`mods/custom_events/src/spawn_enemy.c` installs a `splArcLengthPoint` null-safety patch from
`SpawnEnemy_OnBoot` for actors whose init callbacks reach for splines before path setup;
nothing calls that boot function today, so the patch is not live.

## Key Functions

| Function | Address | Purpose |
|----------|---------|---------|
| `EventActor_Create` | 0x801fbb50 | Universal actor factory |
| `EventActor_Destroy` | 0x801fbf2c | Recursive actor destruction, children first |
| `EventActor_InitFromDesc` | 0x801fb53c | Copies descriptor fields into EnemyData |
| `EventActor_SpawnChild` | 0x801fcda0 | Spawns a child/rider actor and links `parent_gobj` |
| `EventActor_GObjDestroyHandler` | 0x801fcca0 | EnemyData userdata destructor, priority 21 |
| `EventActor_FollowParent` | 0x80219eec | Child state func: follow the parent's position/timing |
| `EventActor_CopyParentState` | 0x80219fd4 | Child state func: copy position/direction from parent |
| `EventActor_GetParentAnimRate` | 0x802049b8 | Reads `parent_gobj->userdata + 0x2B0` (`anim_rate`); crashes on null |
| `EventActor_AnimProcessor` | 0x80200838 | Advances the JObj animation, extracts the position delta into ed+0x550, zeros the JObj translate. Called through `EventActor_UpdateState`, not registered as a proc. |
| `Enemy_SpawnActor` | 0x800f13a8 | Spawn-slot wrapper for modes 1/3: variant extraction, descriptor build |
| `Enemy_SpawnActorMode2` | 0x800f16c0 | Mode-2 spawn helper |
| `Enemy_SpawnerDecide` | 0x800f1a14 | Weighted random enemy choice per position, mode-branched |
| `Enemy_SpawnerDecideMode2` | 0x800f0efc | Mode-2 two-stage picker: category then concrete enemy |
| `Enemy_UnregisterFromSpawnSlot` | 0x800f3b28 | Clear the slot, decrement the count, set the respawn timer |
| `Enemy_Think` | 0x800f3904 | Air Ride manager proc |
| `Enemy_CityTrialThink` | 0x800f33c0 | City Trial manager proc, includes the spawn decision phase |
| `Enemy_InitSpawner` | 0x800f2ee4 | Create the enemy manager GOBJ |
| `Enemy_InitPositionData` | 0x800f2634 | Load spawn positions from the stage and pick the mode |
| `Enemy_GetActorData` | 0x801fd498 | Archive data pointer by ActorID, tier-aware |
| `Enemy_CheckAndLoad` | 0x801fd060 | Load an actor's archive; idempotent |
| `Enemy_LoadFile` | 0x801fd348 | Low-level archive loader |
| `Enemy_LoadStageEnemies` | 0x800f25b4 | Stage batch loader; skipped in CT Free Run |
| `Enemy_GetStagesEnemies` | 0x80262808 | Enemy ID list for a stage |
| `Enemy_LoadCommonParams` | 0x801fd580 | Load `Enemy.dat` `emDataAll`, store the table pointer to `*0x805dd878` |
| `Gm_CheckEnemyEnabled` | 0x8000a348 | Bit 4 of GameData+0xAA7 - enemies enabled for the current stage/mode |
| `grGetEnemyposNum` | 0x800d0c88 | Number of enemy spawn positions for the stage |
| `loadEnemy_spawnXYLocation` | 0x800d0cd4 | Load enemy spawn locations from stage data |
| `loadEventLocations` | 0x800d11fc | Load event position data |
| `Enemy_GX` | 0x801fd158 | GXLink render callback, priority 9, render pass 1 |
| `gmLanMenu_Scale3DObject` | 0x80054414 | Sets a JObj world matrix from position, forward/up and scale |
| `giveEnemyDamage` | 0x8020b680 | Apply damage, write the accumulators |
| `Enemy_ScaleDamage` | 0x8020b71c | Scale incoming damage by table +0x04 (0.4) |
| `Enemy_ClassifyDamageTier` | 0x8020b740 | Damage -> response tier 0-3 via the 10/21/32 thresholds |
| `Enemy_ApplyKnockback` | 0x8020b784 | Full knockback transition: stun frames, velocity, state |
| `EnemyState_AnimExit` | 0x8020c558 | func3 for states 0x00-0x08: stun countdown, ground physics, spark VFX, death at 0 |
| `EnemyPath_Init` | 0x80206e2c | Find the nearest spline and assign path data |
| `EnemyPath_FollowUpdate` | 0x80209ce4 | Path-following movement update |
| `Spline_FindNearest` | 0x800cf07c | Spline segment nearest a position |
| `Spline_GetCount` | 0x800cf38c | Number of splines in the loaded stage |
| `splArcLengthPoint` | 0x80415958 | Evaluate a spline position (wrapper) |
| `splGetSplinePoint` | 0x80414fc0 | Evaluate a spline at a parameter |
| `splArcLengthGetParameter` | 0x80415758 | Arc-length parameter for a spline |
| `Stage_GetGrKindFromStageKind` | 0x80261ce8 | StageKind -> physical GroundKind (hoshi exports it as `Gm_GetGrKindFromStageKind`) |

## Data Addresses

| Data | Address | r13 offset | Description |
|------|---------|-----------|-------------|
| Actor data table | 0x804b22b4 | - | `{data_index, flags}` per actor ID, stride 8 |
| Archive loaded flags | 0x8055a210 | - | One byte per data_index, 1 = loaded |
| Archive root pointers | 0x8055a228 | - | Archive root pointer per data_index |
| Archive filename pointers | 0x804b2204 | - | Two pointers per data_index (dat, group) |
| Per-type descriptor table | 0x804b1d98 | - | One pointer per actor ID |
| Default collision bounds | 0x804b1d40 | - | All-zero bounds used when `bounds_flag == -1.0` |
| Enemy parameter table pointer | 0x805dd878 | - | Pointer to the `emDataAll` block, NULL until enemies load. Damage scale +0x04, tier thresholds +0x08/+0x0C/+0x10, per-tier KB magnitude/scale/launch/stun +0x30/+0x40/+0x50/+0x60, mode scale +0x70, detection range +0x80, retarget cooldown +0x94/+0x98 |
| Stage-file table | 0x804A2FFC | - | Stage-def pointers indexed by physical GroundKind |
| EnemyMgr pointer | 0x805DD714 | +0x634 | EnemyMgr struct (0x3C bytes) |
| SpawnSlot array | 0x805DD70C | +0x62C | Four SpawnSlot structs (0x48 each) |
| Enemy spawn data | 0x805DD710 | +0x630 | Per-stage spawn config pointer |
| Init flag | 0x805DD708 | +0x628 | 1 during init, 0 when done |
| EventActor list | 0x805DE334 | +0x1254 | Global EventActor linked-list root |
