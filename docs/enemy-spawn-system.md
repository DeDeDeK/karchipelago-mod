# Enemy & Event Actor Spawn System

Documentation of the enemy/event actor spawning system in Kirby Air Ride (GKYE01), focused on enabling standalone actor spawning in City Trial.

## Architecture Overview

All enemies and event actors share a single creation pipeline:

```
Enemy_SpawnActor()  ──builds descriptor──►  EventActor_Create(descriptor)
                                                    │
event_dynablade_start() ──builds descriptor─────────┘
event_meteor_start()    ──builds descriptor─────────┘
```

`EventActor_Create` (0x801fbb50) is the universal actor factory. It accepts any actor type ID from 0x00 to 0x4E (79 total). `Enemy_SpawnActor` is a wrapper that builds a descriptor from spawn-slot data and calls `EventActor_Create` internally.

## Actor Type IDs

There are 79 actor type IDs organized into tiers. All share the same data table at `0x804b22b4` (stride 8 per entry: `{int data_index, int flags}`). The `data_index` selects which archive to load; `flags` selects the variant within that archive.

### Tier 0 — Base Enemies (IDs 0x00-0x17)

| ID | Hex | data_idx | Enemy | Copy Ability |
|----|-----|----------|-------|-------------|
| 0 | 0x00 | 0x00 | Broom Hatter | None |
| 1 | 0x01 | 0x00 | Broom Hatter B | None |
| 2 | 0x02 | 0x01 | Bronto Burt | None |
| 3 | 0x03 | 0x01 | Bronto Burt B | None |
| 4 | 0x04 | 0x02 | Scarfy | None |
| 5 | 0x05 | 0x03 | Sword Knight | Sword |
| 6 | 0x06 | 0x04 | Cappy | None |
| 7 | 0x07 | 0x04 | Cappy (flags=4) | None |
| 8 | 0x08 | 0x05 | Wheelie | Wheel |
| 9 | 0x09 | 0x06 | Phan Phan | Fire |
| 10 | 0x0A | 0x07 | Noddy | Sleep |
| 11 | 0x0B | 0x08 | Chilly | Freeze |
| 12 | 0x0C | 0x09 | Flappy | Wing |
| 13 | 0x0D | 0x0A | Plasma Wisp | Plasma |
| 14 | 0x0E | 0x0B | Gordo | None |
| 15 | 0x0F | 0x0C | Bomber | Bomb |
| 16 | 0x10 | 0x0D | Pichikuri | Needle |
| 17 | 0x11 | 0x0D | Pichikuri B | Needle |
| 18 | 0x12 | 0x0E | Dayl | Fire |
| 19 | 0x13 | 0x0E | Dayl (flags=4) | Fire |
| 20 | 0x14 | 0x0F | Caller / Shaturn | Tornado |
| 21 | 0x15 | 0x10 | Walky | Mike |
| 22 | 0x16 | 0x11 | Waddle Dee Truck | None |
| 23 | 0x17 | 0x12 | Waddle Dee | None |

### Tier 1 — Enhanced Variants (IDs 0x18-0x2F)

Same 24 enemies as Tier 0 with `flags=1` (enhanced variant). Same `data_index` sequence. Tier 1 Phan Phan (ID 0x21) is "Heat Phan Phan" — visually distinct fire model, but both T0 and T1 Phan Phan share the Fire copy ability (determined by the archive at data_index 6, not the tier flags).

### Tier 2 — Further Enhanced (IDs 0x30-0x47)

Same 24 enemies with `flags=1` — **identical flags to Tier 1**. Both T1 and T2 select the same sub-entry from the archive, meaning they are functionally identical. The distinction is only in which spawn pool the City Trial timer draws from.

### Tier Differences (T0 vs T1/T2)

The tier flag is used **only at load time** by `Enemy_GetActorData` to select a sub-entry from the enemy's archive. Each archive contains multiple data sets (at offsets 0, 4, 8, 12, 16), and the flags field indexes into them. Once loaded into `EnemyData.actor_data` (+0x14), the tier flag itself is not stored or checked at runtime.

What varies per tier sub-entry:

| Property | Source | Effect |
|----------|--------|--------|
| Base scale | `actor_data->+0x00` -> `EnemyData+0x2D0` | Different size and hitbox |
| Movement pattern | `actor_data->+0x00->+0x94/98/9C` | Pattern type, radius, speed |
| Knockback launch mult | `actor_data->+0x00->+0xA0` | How far they fly when hit |
| Animation data | `actor_data->+0x0C + state*0x10` | Per-state animation timing |
| Model/textures | Archive sub-entry | Visual variants (e.g. Heat Phan Phan) |

The final visual scale is computed as: `mode_scale * spawn_scale * tier_base_scale * global_enemy_scale` where mode_scale is 1.0 (Air Ride), 1.1 (Top Ride), or 1.2 (City Trial).

### Special / Event Actors (IDs 0x48-0x4E)

| ID | Hex | data_idx | flags | Archive | Actor |
|----|-----|----------|-------|---------|-------|
| 72 | 0x48 | 0x00 | 2 | EmBroomData | Broom Hatter (special) |
| 73 | 0x49 | 0x03 | 2 | EmSwordData | Sword Knight (special) |
| 74 | 0x4A | 0x11 | 2 | EmDeeTruckData | Waddle Dee Truck (special) |
| 75 | 0x4B | 0x0B | 3 | EmGordoData | Gordo (event) |
| 76 | 0x4C | 0x13 | 0 | EmTacData | TAC |
| 77 | 0x4D | 0x14 | 0 | EmDynaData | Dyna Blade |
| 78 | 0x4E | 0x15 | 0 | EmMeteoData | Meteor |

## Archive System

### 22 Unique Archives (data_index 0x00-0x15)

| data_idx | Archive File | Group Name |
|----------|-------------|------------|
| 0x00 | EmBroomData.dat | emBroomDataGroup |
| 0x01 | EmBrontData.dat | emBrontDataGroup |
| 0x02 | EmScarfyData.dat | emScarfyDataGroup |
| 0x03 | EmSwordData.dat | emSwordDataGroup |
| 0x04 | EmCappyData.dat | emCappyDataGroup |
| 0x05 | EmWheelieData.dat | emWheelieDataGroup |
| 0x06 | EmElephantData.dat | emElephantDataGroup |
| 0x07 | EmNoddyData.dat | emNoddyDataGroup |
| 0x08 | EmChillyData.dat | emChillyDataGroup |
| 0x09 | EmFlappyData.dat | emFlappyDataGroup |
| 0x0A | EmPlasmaData.dat | emPlasmaDataGroup |
| 0x0B | EmGordoData.dat | emGordoDataGroup |
| 0x0C | EmBombboneData.dat | emBombboneDataGroup |
| 0x0D | EmPichicriData.dat | emPichicriDataGroup |
| 0x0E | EmDaylData.dat | emDaylDataGroup |
| 0x0F | EmShaturnData.dat | emShaturnDataGroup |
| 0x10 | EmWalkyData.dat | emWalkyDataGroup |
| 0x11 | EmDeeTruckData.dat | emDeeTruckDataGroup |
| 0x12 | EmDeeData.dat | emDeeDataGroup |
| 0x13 | EmTacData.dat | emTacDataGroup |
| 0x14 | EmDynaData.dat | emDynaDataGroup |
| 0x15 | EmMeteoData.dat | emMeteoDataGroup |

### Loading

- `Enemy_CheckAndLoad(actor_id)` (0x801fd060) — validates ID, calls `Enemy_LoadFile`.
- `Enemy_LoadFile(actor_id)` (0x801fd348) — looks up `data_index` from the table, checks loaded flag at `0x8055a210[data_index]`, loads archive via `lbLoadArchive` if not already loaded.
- `Enemy_LoadStageEnemies()` (0x800f25b4) — called during `grLoadStage`. Skips in City Trial Free Run (major == MJRKIND_CITY && city_mode == CITYMODE_FREERUN). Runs normally in City Trial mode, iterating the stage's enemy ID list (which includes event actors: TAC 0x4C, Dyna Blade 0x4D, Meteor 0x4E) and calling `Enemy_CheckAndLoad` for each.
- Loading is idempotent — calling `Enemy_CheckAndLoad` for an already-loaded archive is a no-op.

### Data Lookup at Runtime

`Enemy_GetActorData(actor_id)` (0x801fd498) looks up the actor's data pointer. It indexes the actor data table at `0x804b22b4` by ActorID to get `{data_index, flags}`, then indexes the archive root-pointer array at `0x8055a228` by `data_index` and reads the tier sub-entry (offsets +0x00/+0x04/+0x08/+0x0C, selected by `flags`). Returns 0 if the archive is not loaded. `EventActor_Create` uses this internally — if the data isn't loaded, the actor will fail to create properly.

## EventActor_Create

**Address:** 0x801fbb50 (size 0x3dc)

### Descriptor Struct (EventActorDesc)

`EventActor_Create` takes a pointer to a descriptor struct (0x60 bytes, 24 fields). `EventActor_InitFromDesc` (0x801fb53c) is the setup function that copies descriptor -> EnemyData; the other callers are `Enemy_SpawnActor` and `event_dynablade`/`event_meteor`.

```c
typedef struct EventActorDesc
{
    int actor_id;           // 0x00: ActorID (0x00-0x4E)
    Vec3 position;          // 0x04: spawn world position
    Vec3 forward;           // 0x10: forward direction (unit vector)
    Vec3 up;                // 0x1C: up direction (unit vector)
    float scale;            // 0x28: model scale (typically 1.0, or abs of spawn-data value)
    int spawn_index;        // 0x2C: spawn tracking counter. -1 for standalone (skips lifetime)
    int spawn_slot;         // 0x30: spawn slot index. -1 for standalone
    int x34;                // 0x34: 0 in all observed callers
    int lifetime;           // 0x38: lifetime in frames. Only applies if spawn_index != -1
    int x3C;                // 0x3C: parent GOBJ (child spawner sets child EnemyData.parent_gobj). Also variant flag for special actors 0x48-0x4A.
    int x40;                // 0x40: 1 from Enemy_SpawnActor, varies in events
    Vec3 custom_bounds;     // 0x44: custom collision bounds (only if bounds_flag != -1.0)
    float bounds_flag;      // 0x50: -1.0 = use default bounds. Other = use custom_bounds
    Vec3 ground_normal;     // 0x54: ground/surface normal at spawn point
} EventActorDesc; // 0x60 bytes
```

#### Descriptor -> EnemyData Field Mapping

`EventActor_InitFromDesc` (0x801fb53c) copies descriptor fields into the EnemyData userdata:

| Desc Field | Offset | -> EnemyData[N] | ED Offset | Purpose |
|------------|--------|-----------------|-----------|---------|
| actor_id | 0x00 | [3] | 0x0C | Actor type / kind |
| position | 0x04 | [0xC1-0xC3] | 0x304 | World position (also copied to [0xBE-0xC0] and [0xC4-0xC6]) |
| forward | 0x10 | [0xD3-0xD5] | 0x34C | Forward direction (also to [0x2AA-0x2AC]) |
| up | 0x1C | [0xD6-0xD8] | 0x358 | Up direction |
| scale | 0x28 | [0xB3] | 0x2CC | Base scale factor (feeds into damage/size calculations) |
| spawn_index | 0x2C | [10] | 0x28 | Checked against -1; if not -1, lifetime field is written |
| spawn_slot | 0x30 | [8] | 0x20 | Spawn slot tracking |
| x34 | 0x34 | [9] | 0x24 | -- |
| lifetime | 0x38 | [0xB] | 0x2C | Lifetime frames (only if spawn_index != -1) |
| x3C | 0x3C | [2], [0xC] | 0x08, 0x30 | -> EnemyData.parent_gobj for special actors (0x48-0x4A); 0 otherwise. See Parent/Child System below. |
| x40 | 0x40 | [0x242] | 0x908 | -- |
| custom_bounds | 0x44 | [0x2A0-0x2A2] | 0xA80 | Custom collision bounds (only if bounds_flag != -1.0) |
| bounds_flag | 0x50 | [0x2A3] | 0xA8C | -1.0 triggers default bounds (all zeros from 0x804b1d40) |
| ground_normal | 0x54 | [0x23F-0x241] | 0x8FC | Surface normal at spawn point |

#### Parent/Child Actor System

Some enemies are **composite** — they consist of a main body and a rider/attached entity:
- Broom Hatter (0x00/0x01) -> spawns SP Broom Hatter (0x48) as rider
- Sword Knight (0x05) -> spawns SP Sword Knight (0x49) as rider
- Waddle Dee Truck (0x16) -> spawns SP Waddle Dee Truck (0x4A) as driver

`EventActor_SpawnChild` (0x801fcda0) creates the child actor and sets `child.parent_gobj = parent_gobj`. The child's state functions use this reference:

- `EventActor_FollowParent` (0x80219eec) — reads `parent_gobj` to get parent's scale and timing, then copies position data
- `EventActor_CopyParentState` (0x80219fd4) — copies position/direction from parent's EnemyData into child
- `EventActor_GetParentScale` (0x802049b8) — reads `parent_gobj->userdata + 0x2B0` (parent's scale). **Crashes if parent_gobj is null** (DAR=0x2C null deref).

**EnemyData layout for references:**
```c
EnemyData[0] (0x00) = GOBJ *gobj;         // this actor's own GOBJ
EnemyData[1] (0x04) = GOBJ *child_gobj;   // child/rider GOBJ (0 if none)
EnemyData[2] (0x08) = GOBJ *parent_gobj;  // parent/target GOBJ (null crashes some states)
```

**For standalone spawns**: `parent_gobj` must be set to a valid GOBJ after creation. Setting it to the player's machine GOBJ makes enemies track/follow the player. If left null, actors whose state functions call `EventActor_FollowParent` will crash.

#### Standalone Spawn Recipe

For spawning an actor without the spawn-slot system (e.g., in City Trial):

```c
Enemy_CheckAndLoad(ACTORID_WADDLE_DEE); // ensure archive loaded

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
{
    EnemyData *ed = actor->userdata;
    // Set parent to player's machine so the actor tracks the player
    // and doesn't crash in states that dereference parent_gobj.
    ed->parent_gobj = Ply_GetMachineGObj(0);
}
```

### Creation Flow

1. Creates GOBJ with entity_class=21 (0x15), plink=12 (0xC, GAMEPLINK_ENEMY)
2. Allocates EnemyData userdata (3008 bytes / 0xBC0) via `HSD_ObjAlloc` + `memset`
3. Attaches EnemyData with `GObj_AddUserData` at priority 21, destructor `EventActor_GObjDestroyHandler` (0x801fcca0)
4. Copies descriptor fields into EnemyData via `EventActor_InitFromDesc`
5. Looks up per-type data via `Enemy_GetActorData(actor_id)`
6. Loads JObj model from archive
7. Calls per-type init callback from table at `PTR_PTR_804b1d98[actor_id]`
8. Attaches 10 GOBJProc callbacks for AI, physics, collision, rendering, etc.
9. Registers GXLink with function 0x801fd158, priority 9, render pass 1

### GOBJProc Priorities

EventActor_Create registers 10 procs unconditionally for each actor:

| Priority | Function | Address | Purpose |
|----------|----------|---------|---------|
| 0 | `EventActor_ProcResetDamage` | 0x801fc670 | Zeros per-frame damage accumulators via HurtData_ResetPerFrame |
| 1 | `EventActor_ProcUpdate` | 0x801fc698 | HSD anim advance + state machine + state_func1 dispatch (invokes `EventActor_AnimProcessor` 0x80200838 via `EventActor_UpdateState`) |
| 4 | `EnemyPhysicsProc` | 0x801fc6fc | state_func2 dispatch + `vel += accel`, `pos += vel`, OOB floor-kill check (skipped for actor_id >= 0x4C) |
| 5 | `EventActor_ProcStateActive` | 0x801fc7c4 | state_func3 dispatch (main per-state AI logic) |
| 6 | `EventActor_ProcSharedModel` | 0x801fc7f8 | Shadow update + state_func4 dispatch + position/direction to model matrix |
| 7 | `EventActor_ProcPerType` | 0x801fc848 | per_type_cb dispatch + HurtData update + position snap |
| 8 | `EventActor_ProcHitCollInit` | 0x801fc8e8 | No-op stub (`blr`) — does nothing |
| 9 | `EventActor_ProcHitColl` | 0x801fc8ec | HitColl collision setup; checks `damage_accum_1` (+0x994) against `param_hp_threshold` (+0x3b0) |
| 10 | `EventActor_ProcDamage` | 0x801fc9f0 | Reads HurtData output, calls `giveEnemyDamage`, dispatches `hit_reaction_cb2` |
| 21 | `EventActor_ProcFinal` | 0x801fcabc | pos → pos_prev, ground-state flags, lifetime/despawn, OOB destroy |

Note: Priority 8 (`EventActor_ProcHitCollInit`, 0x801fc8e8) is a single `blr` no-op stub registered unconditionally. All 10 procs are always created regardless of actor type. The GXLink render callback (`zz_801fd158_`, priority 9, render pass 1) is registered separately via `GObj_AddGXLink`.

## Spawn Manager System

### Global Variables

| Address | r13 offset | Type | Name | Description |
|---------|-----------|------|------|-------------|
| 0x805DD708 | +0x628 | u16 | stc_enemy_init_flag | 1 during init, 0 when done |
| 0x805DD70C | +0x62C | ptr | stc_spawn_slots | Array of 4 SpawnSlot structs |
| 0x805DD710 | +0x630 | ptr | stc_enemy_spawn_data | Per-stage spawn config |
| 0x805DD714 | +0x634 | ptr | stc_enemy_mgr | EnemyMgr struct pointer |
| 0x805DE334 | +0x1254 | ptr | stc_event_actor_list | Global EventActor linked list root |

### EnemyMgr Struct (0x3C bytes)

Pointed to by r13+0x634 (0x805DD714). Manages all spawning state for the current stage.

| Offset | Type | Name | Description |
|--------|------|------|-------------|
| +0x00 | ptr | gobj | Manager GOBJ |
| +0x04 | ptr | spawn_slot_data | Per-position extended data array (stride 0x5C) |
| +0x08 | ptr | spawn_group_pool | Spawn group data (enemy ID + weight pairs) |
| +0x0C | u32 | frame_counter | Incremented every Think frame |
| +0x10 | u32 | total_spawns | Total enemies spawned (lifetime) |
| +0x14 | u16 | active_count | Current alive enemy count |
| +0x16 | u16 | active_event_count | Alive "event" enemies (actor_id >= 0x18). CT mode 3 only. |
| +0x18 | u16 | (reserved) | Unused — memset-0 at manager init (`Enemy_InitPositionData`), never read or written afterward by any spawn/think/unregister code. The live near-neighbor counters are +0x14/+0x16/+0x1A/+0x1C. |
| +0x1A | s16 | slots_initialized | Count of initialized spawn slots |
| +0x1C | s16 | last_spawn_slot | Last spawn slot index used |
| +0x20 | u32[3] | ct_time | City_GetMinSecMs output (min, sec, ms) |
| +0x2C | u32 | ct_duration_base | Base time in 60ths |
| +0x30 | u32 | ct_duration | Total match duration in 60ths |
| +0x34 | float | time_progress | current_time / total_time (0.0-1.0) for CT difficulty |
| +0x38 | u32 | ct_next_spawn_pos | CT rotating spawn position index |

### SpawnSlot Struct (0x48 bytes, 4 slots)

Array at r13+0x62C (0x805DD70C). Four spawn slots, one per player in Air Ride.

| Offset | Type | Name | Description |
|--------|------|------|-------------|
| +0x00 | ptr | gobj | Spawned EventActor GOBJ (null if empty) |
| +0x04 | Vec3 | spawn_pos | Spawn position |
| +0x10 | Vec3 | world_pos | Calculated: spawn_pos + forward*(-80.0) |
| +0x1C | Vec3 | forward | Forward direction |
| +0x28 | Vec3 | ground_normal | Ground normal |
| +0x34 | s16 | respawn_timer | Countdown. Decremented each frame in CT. |
| +0x40 | s16 | forward_sign | Sign of forward direction |
| +0x42 | s16 | up_sign | |
| +0x44 | u8 | flags | Bit 7: is_spawned, bit 6: player_nearby |
| +0x46 | u16 | initialized | 1 = valid data |

### Per-Slot Extended Data (stride 0x5C)

Array at EnemyMgr+0x04. One entry per spawn position defined in the stage data.

| Offset | Type | Description |
|--------|------|-------------|
| +0x00 | Vec3 | Position |
| +0x0C | Vec3 | Direction |
| +0x18 | Vec3 | Ground normal |
| +0x24 | s16 | spawn_slot (-1 = unassigned) |
| +0x26 | s16[4] | Enemy ID entries |
| +0x2E | s16[4] | Weight entries |
| +0x34 | s16 | respawn_timer |
| +0x4A | u8 | flags (bit 7 = occupied) |
| +0x4C | s16 | actor_id (-1 if empty) |
| +0x4E | s16 | spawn_tracking_counter |
| +0x50 | s16 | owning_slot_index |
| +0x58 | ptr | actor_gobj |

### Spawn Data Structure

From stage .dat file, pointed to by r13+0x630 (0x805DD710). Defined as
`EnemySpawnData` in `enemy.h`.

| Offset | Type | Description |
|--------|------|-------------|
| +0x00 | s16 | spawn_count |
| +0x04 | ptr | spawn_entries — table of `EnemySpawnEntry` (stride **0x38** per entry). Indexed by `position_index`. |
| +0x08 | int | (mode-dependent) |
| +0x0C | ptr | secondary_table — pointer-array of meta-enemy sub-tables (0x50-0x5E). May be NULL. Read by `Enemy_SpawnerDecide`. |
| +0x10 | ptr | config struct |

Note: the **0x5C stride** is the per-position *extended data* array at `EnemyMgr+0x04` (runtime), not the stage-file `spawn_entries` (0x38).

### Spawn Entry Struct (EnemySpawnEntry, 0x38 bytes)

One entry per spawn position, loaded verbatim from the stage `.dat` (the engine
only reads it). Defined as `EnemySpawnEntry` in `enemy.h`. The id/weight arrays
live at **mode-dependent offsets** (the three modes pack them differently), so
the struct models them as a union keyed on `config.mode`.

Common fields:

| Offset | Type | Name | Description |
|--------|------|------|-------------|
| +0x00 | s16 | location_index | Index into the stage enemy-position table (`GrData+0x138`, stride 0x24 of three Vec3s). Resolved by `loadEnemy_spawnXYLocation`; the position/direction/ground-normal go into the runtime per-position extended data, not back into this entry. |
| +0x02 | s16 | pad02 | — |
| +0x04 | s16 | entry_type | Sub-kind selector (modes 2/3): e.g. point vs spline spawn. |
| +0x30 | f32 | scale | Per-spawn model scale (negated if negative, 1.0 if zero). |
| +0x34 | s32 | variant | Copied into the actor descriptor's parent/variant slot. |

Mode-dependent id/weight/lifetime fields (the union at +0x06..+0x2F):

| Mode | ids | weights | lifetime base/range |
|------|-----|---------|---------------------|
| 1 (Air Ride courses) | s16[4] @ +0x1E | s16[4] @ +0x26 (-1 terminates) | +0x1A / +0x1C |
| 2 (STKIND_MELEE1) | s16 enemy_id @ +0x06 | s16 weight_columns[N] @ +0x08 (one per meta-enemy category) | +0x24 / +0x26 |
| 3 (STKIND_MELEE2) | s16[5] @ +0x06 | s16[5] @ +0x10 (-1 terminates) | +0x1A / +0x1C |

Field readers: `location_index`, `scale`, `lifetime`, `variant` are read by
`Enemy_SpawnActor` (modes 1/3) and `Enemy_SpawnActorMode2` (mode 2). The
`ids`/`weights` arrays are read by `Enemy_SpawnerDecide` (modes 1/3) and
`Enemy_SpawnerDecideMode2` (mode 2). Lifetime values 0 or 1 mean "no lifetime";
values > 1 get +/- random jitter from the range field.

Mode 2 is two-stage: `Enemy_SpawnerDecideMode2` weighted-picks a meta-enemy
category from `secondary_table[0]` (biased by `EnemyMgr.time_progress`), then
picks a concrete enemy from that category's per-entry `weight_columns` and
returns the entry's `enemy_id`.

Config struct at spawn_data+0x10:

| Offset | Type | Description |
|--------|------|-------------|
| +0x24 | s16 | max_respawn_delay |
| +0x26 | s16 | random respawn range |
| +0x28 | s16 | mode (1=Air Ride courses, 2=STKIND_MELEE1, 3=STKIND_MELEE2) |

`stc_enemy_spawn_data` is NULL in the City Trial city map (whether
timed or Free Run), Top Ride, and stadiums that don't use stage-based enemy
spawning (Air Glider, Destruction Derby, Single Race, etc.). The CT city's
event actors (TAC, Dyna Blade, Meteor) are loaded by `Enemy_LoadStageEnemies`
but do not populate `stc_enemy_spawn_data` — they spawn through the event
system, not the spawn-position pool.

## Air Ride Per-Course Enemy Rosters

Which enemies each Air Ride course can spawn, from the vanilla stage `.dat`
spawn tables (mode 1: `ids[4]` at entry `+0x1E`, `weights[4]` at `+0x26`, stride `0x38`; meta-enemy
IDs `0x50-0x5E` expanded through `secondary_table`). An enemy is listed if it appears with a positive
weight at any spawn position on that course. Tiers T0/T1/T2 of an enemy share one copy ability and
are collapsed.

**StageKind → GroundKind → stage file** — do NOT infer the file from the course
name. The mapping comes from `Stage_GetGrKindFromStageKind` (0x80261ce8: global stage table at
`*(*(r13+0x7FC))`, stride `0x58`, GroundKind at `+0x00`) plus the stage-def filename table in
`main.dol`. Note `GrPasture1` is **Kirby Melee 1** (spawn mode 2) and `GrColosseum5` is **Kirby Melee
2** (mode 3) — neither is an Air Ride course.

### Per-course enemy roster

Copy-ability enemies are **bold** with their ability; the rest are ability-less "garbage" enemies
(the "swallow garbage enemies" checklist cell takes any of these).

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
| 8 | Nebula Belt | GrSpace2.dat | — | — (no enemy spawn table) |

### Copy-ability enemy → courses

The reverse view. The Archipelago world uses the Sword Knight / Wheelie / Chilly / Plasma Wisp rows
to gate the "swallow this enemy" checklist cells on `HasAny` of those courses when
`air_ride_courses_gated` is on (`_SWALLOW_ENEMY_COURSE_RULES` in `worlds/kirby_air_ride/KARRules.py`).

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

## Which modes spawn regular enemies

Regular per-type AI enemies spawn **only** when the loaded stage's `.dat` ships a non-NULL enemy-spawn array. The decision is **data-driven, not derived from a stadium/mode constant**:

1. `grLoadStage` → `Enemy_InitPositionData` (0x800f2634) → gate `0x8000a228` (GameData+0xAA6 bit 4, "enemies enabled", effectively always on) → spawn-data provider `0x800da4c4`.
2. `0x800da4c4` reads `GrData+0x28`. **If it is NULL, no enemies** (returns NULL). Otherwise it indexes a per-stage `EnemySpawnData` array (stride 0x18) by the stage's EnemyposId and stores the result at `stc_enemy_spawn_data` (0x805DD710).
3. `Enemy_InitPositionData` then bails to the no-spawn path if `spawn_count == 0` / `config == 0` / `spawn_entries == 0`. Otherwise the **`mode` is read straight from `config->mode` (config+0x28)** — baked into the stage file, never computed from a stadium kind.

So spawning regular enemies requires (a) the always-on enemies-enabled bit and (b) a stage file that ships a populated enemy-spawn array.

| Mode group | Stage file | Spawns regular enemies? | mode |
|------------|-----------|-------------------------|------|
| Air Ride courses 0-7 | GrPlants1 / Heat2 / Desert1 / Ice1 / Sky2 / Valley2 / Machine2 / Check2 | **Yes** | 1 |
| Air Ride — Nebula Belt | GrSpace2 | No (NULL spawn array) | — |
| City Trial — **Kirby Melee 1** | GrPasture1 | **Yes** | 2 |
| City Trial — **Kirby Melee 2** | GrColosseum5 | **Yes** | 3 |
| City Trial — Drag Race / Air Glider / Target Flight / High Jump / Destruction Derby / Single Race / Vs. Dedede | GrZeroyon* / GrSimple* / GrJump* / GrColosseum1,3 / GrDedede1 | No (NULL spawn array) | — |
| City Trial — open city (timed) | GrCity1 | No — `GrData+0x28` base is non-NULL but the selected `EnemySpawnData` entry is empty (spawn_count 0), so the spawner is inert | — |
| City Trial — Free Run | GrCity1 | No (`Enemy_LoadStageEnemies` skipped entirely) | — |

**Kirby Melee 1 and Kirby Melee 2 are the only City Trial events that spawn the regular AI enemy pool.** Every other City Trial context (open city, Free Run, all other stadiums) produces only **event actors** (TAC, Dyna Blade, Event Gordo, Meteor), which spawn through the event system, not this pool.

> **The three index spaces — StadiumKind vs StageKind vs physical GroundKind.** Three different "kind" numbers get conflated here. `stage.h` defines a distinct enum for each of the two that mod code touches:
>
> - **StageKind** (`stage.h` `enum StageKind`, members `STAGEKIND_*`): the 0–59 stage *selection* index. This is what `Gm_GetCurrentStageKind()` (GameData+0xA97) and `stGetCurrentStageKind()` (r13[0x7F8] cache) return. For Air Ride it equals `AirRideCourse` (0–8); the City Trial stadiums occupy 9–33. `STAGEKIND_KIRBYMELEE1 = 17`, `STAGEKIND_KIRBYMELEE2 = 18`. (Each stadium member equals `STKIND_x + 10` — `STAGEKIND_KIRBYMELEE1=17 = STKIND_MELEE1+10`.)
> - **Physical GroundKind** (`stage.h` `enum GroundKind`, members `GR_*`): which ground geometry file loads — an index into the stage-file table in `main.dol` at `0x804A2FFC` (stage-def pointers indexed from 0): `0=GrPlants1 … 8=GrIce1, 9=GrCity1, 10–13=GrZeroyon1/3/4/5, 14=GrPasture1, 15=GrColosseum1, 16=GrColosseum3, 17=GrColosseum5, 18–20=GrJump1/2/3, 21=GrDedede1, 23–25=GrTest*, 26–27=GrSimple*`. This is `GrObj.gr_kind` (+0x04), returned by `Gr_GetCurrentGrKind()`. So **Kirby Melee 1 = GroundKind 14 (GrPasture1)** and **Kirby Melee 2 = GroundKind 17 (GrColosseum5)**.
> - **StadiumKind** (`stadium.h` `STKIND_*`, the City Trial event index, 0-based): `STKIND_MELEE1 = 7`, `STKIND_MELEE2 = 8`.
>
> `Gm_GetGrKindFromStageKind()` (`0x80261ce8`, table `*(*(r13+0x7FC))`, stride `0x58`, GroundKind at `+0x00`) maps StageKind→GroundKind. Live read (Dolphin): **StageKind 17 → GroundKind 14 (GrPasture1)**, **StageKind 18 → GroundKind 17 (GrColosseum5)** — matching the `.dat` evidence above (GrPasture1 = mode-2 enemy data, GrColosseum5 = mode-3). StageKind uses menu order and GroundKind uses file order, so the two spaces coincide only at 0/1/2 and City Trial (9) and diverge everywhere else (e.g. Machine Passage is StageKind 6 but GroundKind 5).
>
> Because City Trial is 9 in both the StageKind and GroundKind spaces, a "current stage is City Trial" check (`== STAGEKIND_CITY1`, or `grobj->gr_kind == GR_CITY1`) is space-agnostic; the spaces only matter once you touch a stadium or a re-ordered Air Ride course. The physical GroundKind→file-name table is decoded in `docs/sky-backdrop-system.md`. Note the `yakumono.h` per-grkind hook table is *also* indexed by physical GroundKind (so Machine Passage = 5 there too).

## Spawning custom actors in City Trial

### Archive loading

In City Trial Free Run, `Enemy_LoadStageEnemies` is skipped, so no enemy archives are loaded by default. In timed City Trial and the Melee stadiums, `Enemy_LoadStageEnemies` runs normally and loads the stage's enemy archives (including event actors TAC, Dyna Blade, Meteor).

### Spawning custom actors

To spawn actors in modes where their archive isn't already loaded (Free Run, Air Ride), call `Enemy_CheckAndLoad(actor_id)` first — it's idempotent and no-ops if already loaded. Then call `EventActor_Create` with a descriptor struct.

```c
// Example: spawn Dyna Blade near a player
Enemy_CheckAndLoad(0x4D); // ensure EmDynaData.dat is loaded

EventActorDesc desc;
memset(&desc, 0, sizeof(desc));
desc.actor_id = 0x4D;
desc.position = player_position;
// ... fill in direction, duration, etc.

GOBJ *actor = EventActor_Create(&desc);
```

### Considerations

- **Memory**: Each archive uses heap memory. Loading all 22 archives simultaneously may exceed available memory. Load only what's needed.
- **Position**: Regular enemies (IDs 0x00-0x47) have patrol AI that references their spawn position. Event actors (0x48-0x4E) have more autonomous movement (Dyna Blade swoops, Meteor falls, TAC chases).
- **Cleanup**: Actors created via `EventActor_Create` are GOBJs on plink 0xC. They can be destroyed via `GObj_Destroy` or will be cleaned up on scene change.
- **Collision**: Event actors have built-in hurt/hit collision via their GOBJProc callbacks. They interact with machines and riders using the standard collision system.

## Enemy_SpawnActor

**Address:** 0x800f13a8 (size 0x318)

Takes `(spawn_slot_index, enemy_id_packed, position_index)`.

### Variant Extraction

If `enemy_id_packed` has bits set in the 0xFF00 mask, the variant is extracted:
- `variant = (packed >> 8) - 1`
- Base enemy ID = `packed & 0xFF`

The extracted enemy is recorded into the group ring buffer at `EnemyMgr+0x08` (`spawn_group_pool`, a 12-byte entry per group with a 5-slot mod-5 ring). The base ID (< 0x4F) is then used to build the descriptor and call `EventActor_Create`.

Meta-enemy *expansion* (IDs 0x50-0x5E → weighted random pick) is **not** done here — it happens in `Enemy_SpawnerDecide`, which resolves a meta-enemy ID before passing the concrete base ID to `Enemy_SpawnActor`. See the [Meta-Enemy Expansion](#enemy_spawnerdecide) note under `Enemy_SpawnerDecide`.

### Scale and Lifetime

- **Scale**: Read from `spawn_entries[position_index] + 0x30` (stride 0x38). Negated if negative, defaults to 1.0 if zero.
- **Lifetime**: Read from position data, absolute value. If > 1, random jitter is added.

## Enemy_SpawnerDecide

**Address:** 0x800F1A14 (size 0xA54)

Large function that decides **what** enemy to spawn at a given position. Mode-branched.

### Air Ride Path

1. Check flags byte at extended data +0x4A (8 bits: occupied, player nearby, various state)
2. Read weight table from extended data (up to 4 entries, -1 terminated)
3. Sum weights, random select
4. **Meta-enemy expansion** for IDs in `(0x4F, 0x5F)` (i.e. 0x50-0x5E): `group_index = id - 0x4F`, index `secondary_table` at `stc_enemy_spawn_data+0x0C`, then weighted-random select from that sub-table of `{enemy_id, weight}` short pairs (loops until the resolved ID is no longer a meta-enemy)
5. Call `Enemy_SpawnActor` with the concrete base ID

### City Trial Path

Similar flow but uses time-based difficulty scaling via `EnemyMgr.time_progress` to bias toward higher-tier enemies as the match progresses.

## Enemy_Think vs Enemy_CityTrialThink

Both update slot positions/vectors and scan the EventActor linked list for occupancy. Key differences:

### Air Ride (Enemy_Think, 0x800f3904)

- 4 spawn slots tied to players
- Updates player positions, direction, ground normal each frame
- Scans linked list to set occupancy bits
- No spawn decision logic in the think function (handled by separate spawn loop)

### City Trial (Enemy_CityTrialThink, 0x800f33c0)

- **Time progress update**: recomputes `time_progress = current / total` each frame
- Same slot/occupancy update as Air Ride
- **Spawn decision phase** with 3 modes:
  - **Mode 1 (sequential)**: Iterates from `last_spawn_slot` through all positions
  - **Mode 2 (CT proper)**: Clears all position weights/flags, iterates all positions. **Spawn cap**: `slots_initialized * 2` indexed into config table. If `active_count >= cap`, stops spawning.
  - **Mode 3 (CT Free Run)**: Random starting position, same loop as mode 1
- Each position: decrements respawn timer, calls spawn decision, calls `Enemy_SpawnerDecide` for weighted random enemy selection

## Enemy_UnregisterFromSpawnSlot

**Address:** 0x800F3B28 (size 0xBC)

Called when an enemy dies or despawns:

1. Gets `spawn_slot` from EnemyData. If < 0, returns (standalone actor).
2. Verifies GOBJ matches slot entry
3. Clears slot state: `actor_id = -1`, nulls GOBJ pointer, clears flags
4. Decrements `active_count`. For CT Free Run: separate tracking for tier >= 0x18 (`event_count` vs `active_count`)
5. Assigns new respawn timer from random range (config +0x24 base, +0x26 random range)

## EventActor_Destroy

**Address:** 0x801fbf2c (size 0x368)

**Recursive depth-first** destruction. The child chain (each node's `child_gobj` at EnemyData+0x04) is descended with the first 5 levels manually unrolled; a deeper sixth level recurses back into `EventActor_Destroy`.

Per node (deepest child first):
1. Destroy child first (recursive — clears the parent's `child_gobj` after)
2. Conditionally unregister from spawn slot — only when `kind < 0x48` (non-special) **and** the actor is not Tier 1/2 (`0x18 <= kind < 0x48` is false) **and** the mode is not City Trial (`config.mode != 3`). The effective condition is **Tier 0 enemies (kind < 0x18) outside City Trial**.
3. Cleanup VFX/SFX handles (`EventActor_CleanupVfxA3C` 0x8020c6e0, `EventActor_CleanupVfxA40` 0x8020c70c)
4. Call `GObj_Destroy`

Spawn-slot logic: only Tier 0 enemies in non-CT modes call the Air-Ride spawn-slot unregister (`zz_80114524_` → `zz_801218a4_`). Tier 1/2 enemies and all City Trial enemies simply skip the unregister call here. (This is a different path from `Enemy_UnregisterFromSpawnSlot` at 0x800f3b28, which is called from the death/respawn flow, not from `EventActor_Destroy`.)

## Damage & Knockback System

Enemies do **not** have traditional HP. Death is determined by per-hit knockback, not accumulated damage.

### Per-Hit Response

Incoming damage is first scaled ×**0.4** (`Enemy_ScaleDamage` 0x8020b71c, reads table `+0x04`), then classified into a response tier (0-3) by `Enemy_ClassifyDamageTier` (0x8020b740). The classifier compares against three float thresholds at the param table's `+0x08`, `+0x0C`, `+0x10`:

```
if (dmg < thr[+0x08]) return 0;   // 10.0
if (dmg < thr[+0x0C]) return 1;   // 21.0
if (dmg < thr[+0x10]) return 2;   // 32.0
return 3;
```

`Enemy_ApplyKnockback` (0x8020b784) then indexes four per-tier arrays in the table by the tier (`ed+0xA1C`) and writes the results into EnemyData:

| Tier | Damage | Stun frames (ed+0xA18, +0x60) | Launch speed (ed+0x9D8, +0x50) | KB scale (ed+0x878, +0x40) | KB base mag (+0x30) |
|------|--------|-------------------------------|--------------------------------|----------------------------|---------------------|
| 0 | < 10.0 | 2 | 2.0 | 1.0 | 20 |
| 1 | 10.0 - 20.9 | 4 | 3.0 | 0.8 | 30 |
| 2 | 21.0 - 31.9 | 6 | 4.0 | 0.6 | 40 |
| 3 | >= 32.0 | 8 | 5.0 | 0.5 | 50 |

These values are **global** (shared across all enemy types/tiers) and **static** — they live in `Enemy.dat` (public `emDataAll`), loaded into RAM by `Enemy_LoadCommonParams` (0x801fd580) which stores the table pointer to `*0x805dd878`. They are NOT runtime-computed; the main-menu `mem1.raw` simply shows `0x805dd878` NULL because no stage with enemies has loaded yet. The knockback magnitude passed to the launch is `int(KB_base_mag[tier] × actor_data->+0x00->+0xA0 × KB_scale[tier])` (clamped ≥ 1), so the per-tier `actor_data` launch multiplier (`+0xA0`) further scales how far that enemy flies.

### Death Sequence

1. A hit sets `EnemyData.stun_frames` (+0xA18) based on the response tier
2. `EnemyState_AnimExit` (0x8020c558, func3 for states 0-8) decrements `stun_frames` by 1 each frame during the knockback state
3. When `stun_frames` reaches 0, the enemy enters the death state
4. `EnemyData.death_timer` (+0xA28) counts up; enemy is destroyed after 30 frames

### Damage Accumulators

`EnemyData.damage_accum_1` (+0x994) and `damage_accum_2` (+0x998) track total damage received (capped at 9999). These are written by `giveEnemyDamage` (0x8020b680) but **nothing reads them for death logic** — they appear to be cosmetic/diagnostic only.

### Per-Tier Knockback Differences

The tier-specific data at `actor_data->+0x00->+0xA0` provides a **launch multiplier** that scales the knockback velocity when the enemy is hit. Higher-tier enemies may have a lower multiplier, meaning they fly less far from the same hit — making them harder to knock out of the arena.

## Gm_CheckEnemyEnabled

**Address:** 0x8000a348 (size 0x28)

Reads `GameData+0x0AA7`, extracts bit 4. Returns 1 if enemies are enabled for the current stage/mode, 0 if disabled. This is a per-stage/mode flag set during stage configuration.

## Spline / Path-Following System

Enemies can follow predefined spline paths embedded in stage data (`GrData->spline` at +0x14).

### Path Initialization

`EnemyPath_Init` (0x80206e2c) takes an `EnemyData*` and:
1. Calls `Spline_FindNearest` (0x800cf07c) to find the nearest spline segment to the enemy's position (`ed->pos` at +0x2F8)
2. If found: stores segment index to `ed->spline_segment` (+0x5DC), arc parameter to `ed->spline_arc_param` (+0x5FC), and assigns spline curve pointers to `ed->spline_primary` (+0x5D4) / `ed->spline_secondary` (+0x5D8) based on `ed->spline_direction` (+0x5F8)
3. If not found: sets bit 2 of `ed+0xB0A` (alternative movement mode)

### Path-Following Update

`EnemyPath_FollowUpdate` (0x80209ce4) runs each frame for enemies with `ed->path_active_flag` (+0xA8C) == -1.0:
1. Checks stage has splines via `Spline_GetCount` (0x800cf38c)
2. Evaluates `splArcLengthPoint` on the enemy's spline pointer to compute direction
3. Updates enemy position along the path

### Standalone Spawn Path Setup

For standalone-spawned actors to follow paths, set these fields after `EventActor_Create` returns:
```c
ed->spline_path_ready = 1;    // +0x654
ed->spline_direction = 1;     // +0x5F8 (1=forward)
ed->path_active_flag = -1.0f; // +0xA8C (enables path following)
EnemyPath_Init(ed);           // finds nearest spline
```

The `splArcLengthPoint_Safe` patch (installed by `SpawnEnemy_OnBoot`) provides null-safety for actors whose init callbacks try to use splines before path setup.

## Key Functions Reference

For meteor-specific documentation (state machine, standalone spawn, patches), see [meteor-actor.md](meteor-actor.md).

| Function | Address | Size | Purpose |
|----------|---------|------|---------|
| EventActor_Create | 0x801fbb50 | 0x3dc | Universal actor factory |
| EventActor_Destroy | 0x801fbf2c | 0x368 | Recursive actor destruction (handles children, clears references) |
| EventActor_InitFromDesc | 0x801fb53c | 0x4fc | Copies descriptor fields into EnemyData |
| EventActor_SpawnChild | 0x801fcda0 | 0x264 | Spawns child/rider actor, sets parent_gobj link |
| EventActor_GObjDestroyHandler | 0x801fcca0 | 0x100 | GObj userdata destructor for EnemyData (priority 21) |
| EventActor_FollowParent | 0x80219eec | 0x7c | State function: follow parent GOBJ position/timing |
| EventActor_CopyParentState | 0x80219fd4 | 0x9c | State function: copy position/direction from parent |
| EventActor_GetParentScale | 0x802049b8 | 0xc | Reads parent_gobj->userdata + 0x2B0 (scale). Crashes if null. |
| EnemyPath_Init | 0x80206e2c | 0xd4 | Find nearest spline and assign path data to EnemyData |
| Enemy_SpawnActor | 0x800f13a8 | 0x318 | Spawn-slot wrapper (modes 1/3): variant extraction, meta-enemy expansion, calls EventActor_Create |
| Enemy_SpawnActorMode2 | 0x800f16c0 | 0x354 | Mode-2 spawn helper: builds descriptor from a spawn entry, calls EventActor_Create |
| Enemy_SpawnerDecide | 0x800f1a14 | 0xa54 | Decides what enemy to spawn at a position (weighted random, mode-branched) |
| Enemy_SpawnerDecideMode2 | 0x800f0efc | 0x1f8 | Mode-2 two-stage picker: meta-enemy category from secondary[0], then concrete enemy from its weight column |
| Enemy_UnregisterFromSpawnSlot | 0x800f3b28 | 0xbc | Remove actor from spawn-slot tracking, set respawn timer |
| Enemy_GetActorData | 0x801fd498 | 0xe8 | Look up loaded archive data pointer by ActorID (tier-aware) |
| Enemy_CheckAndLoad | 0x801fd060 | 0x5c | Load archive for actor ID |
| Enemy_LoadFile | 0x801fd348 | 0x150 | Low-level archive loader |
| Enemy_LoadStageEnemies | 0x800f25b4 | 0x80 | Stage-based batch loader (skips in CT Free Run) |
| Enemy_InitSpawner | 0x800f2ee4 | 0x4dc | Create enemy manager GOBJ |
| Enemy_InitPositionData | 0x800f2634 | 0x8b0 | Load spawn positions from stage |
| Enemy_Think | 0x800f3904 | 0x1bc | Air Ride enemy manager proc |
| Enemy_CityTrialThink | 0x800f33c0 | 0x544 | City Trial enemy manager proc |
| Enemy_GetStagesEnemies | 0x80262808 | 0x84 | Get enemy ID list for stage |
| Gm_CheckEnemyEnabled | 0x8000a348 | 0x28 | Check if enemies are enabled for current stage/mode |
| grGetEnemyposNum | 0x800d0c88 | 0x4c | Get number of enemy spawn positions for stage |
| loadEnemy_spawnXYLocation | 0x800d0cd4 | 0xf8 | Load enemy spawn XY locations from stage data |
| loadEventLocations | 0x800d11fc | 0xf4 | Load event position data |
| EnemyPhysicsProc | 0x801fc6fc | 0xc8 | Priority 4 GObj proc: `vel += accel`, `pos += vel`, floor-kill check (skipped for actor_id >= 0x4C) |
| EventActor_ProcUpdate | 0x801fc698 | 0x64 | Priority 1 GObj proc: HSD anim advance + state machine + state_func1 dispatch |
| EventActor_AnimProcessor | 0x80200838 | 0x36c | Anim subroutine invoked via `EventActor_UpdateState`: advances JObj animation, extracts position delta to ed+0x550, zeros JObj translate. (Named in the map; NOT the registered proc.) |
| gmLanMenu_Scale3DObject | 0x80054414 | 0x168 | Sets JObj world matrix from position + forward/up vectors + scale |
| EnemyActor_DistToPlayer | 0x801fffa4 | 0x60 | Returns distance from enemy to player by index (named in the map, link.ld + enemy.h) |
| EnemyActor_RumblePlayer | 0x801ff80c | 0x58 | `(player_idx, rumble_type, intensity)` -- triggers controller rumble for player |
| giveEnemyDamage | 0x8020b680 | 0x9c | Apply damage to enemy (writes accumulators) |
| Enemy_ScaleDamage | 0x8020b71c | -- | Scale incoming damage by the global multiplier (table +0x04 = 0.4) |
| Enemy_ClassifyDamageTier | 0x8020b740 | 0x44 | Classify hit damage into response tier (0-3) via 3 thresholds (10/21/32) |
| Enemy_LoadCommonParams | 0x801fd580 | -- | Load `Enemy.dat` `emDataAll`; store the param-table pointer to `*0x805dd878` (was `fn_emLoadCommon`) |
| Enemy_ApplyKnockback | 0x8020b784 | 0x554 | Full knockback state transition: sets stun_frames, computes velocity, enters knockback state |
| EnemyState_AnimExit | 0x8020c558 | 0xdc | func3 for states 0-8: decrement stun_frames each frame, ground physics, stun spark VFX; triggers death at 0 |
| EnemyPath_FollowUpdate | 0x80209ce4 | 0x35c | Enemy path-following movement update |
| splArcLengthPoint | 0x80415958 | 0x48 | Evaluate spline position (wrapper) |
| splGetSplinePoint | 0x80414fc0 | 0x490 | Evaluate spline at parameter |
| splArcLengthGetParameter | 0x80415758 | 0x200 | Get arc-length parameter for spline |

## Data Addresses

| Data | Address | r13 offset | Description |
|------|---------|-----------|-------------|
| Actor data table | 0x804b22b4 | -- | {data_index, flags} per actor ID, stride 8 |
| Archive loaded flags | 0x8055a210 | -- | byte per data_index, 1 = loaded |
| Archive filename ptrs | 0x804b2204 | -- | 2 pointers per data_index (dat, group) |
| Per-type callback table | 0x804b1d98 | -- | pointer per actor ID |
| Enemy parameter table pointer | 0x805dd878 | -- | Holds a pointer to the param table (from `Enemy.dat` `emDataAll`; set by `Enemy_LoadCommonParams`, NULL until enemies load). Fields: damage scale +0x04, thresholds +0x08/+0x0C/+0x10, per-tier KB mag/scale/launch/stun +0x30/+0x40/+0x50/+0x60, mode scale +0x70, detection range +0x80, retarget cooldown +0x94/+0x98 |
| EnemyMgr pointer | 0x805DD714 | +0x634 | EnemyMgr struct (0x3C bytes) |
| SpawnSlot array | 0x805DD70C | +0x62C | Array of 4 SpawnSlot structs (0x48 each) |
| Enemy spawn data | 0x805DD710 | +0x630 | Per-stage spawn config pointer |
| Init flag | 0x805DD708 | +0x628 | 1 during init, 0 when done |
| EventActor list | 0x805DE334 | +0x1254 | Global EventActor linked list root |
