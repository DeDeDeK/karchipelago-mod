# Enemy Spawns

Where enemies come from: the actor kinds and the archives behind them, the one factory every
enemy and event actor goes through (`EventActor_Create`), the spawn manager that places regular
enemies on a course, and which stages spawn them at all. A modder needs this to spawn an enemy
from code, to change what a course spawns, or to know why a mode has no enemies.

## Kinds and archives

`EnemyKind` has 79 values. 0x00-0x17 are the tier-0 enemies, 0x18-0x2F and 0x30-0x47 repeat the
same 24 as tiers 1 and 2, and 0x48-0x4E are special actors: the rider parts of composite enemies
(SP Broom Hatter 0x48, SP Sword Knight 0x49, the Waddle Dee Truck's cart 0x4A), Event Gordo
0x4B, TAC 0x4C, Dyna Blade 0x4D and Meteor 0x4E.

The table at 0x804b22b4 gives each kind `{data_index, flags}`. `data_index` picks one of 22
archives (`EmBroomData.dat` ... `EmMeteoData.dat`, names at 0x804b2204), and `flags` picks one
of five sub-entries inside it. Tier 0 uses sub-entry 0 and tiers 1 and 2 both use sub-entry 1,
so a T1 and a T2 enemy load identical data; the `B` variants of Cappy and Dayl use sub-entry 4
in every tier, and the special actors use 2 or 3. The sub-entry sets base scale, movement
parameters, animations and model. Tier 1 Phan Phan is the visibly different Heat Phan Phan. The
kind also selects the AI descriptor, which tiers 1 and 2 share and tier 0 does not.

Final scale is `mode_scale * spawn scale * base scale * global scale`, with `mode_scale` 1.0 in
Air Ride, 1.1 in Top Ride and 1.2 in City Trial.

| Function | Address | Role |
|---|---|---|
| `Enemy_CheckAndLoad` | 0x801fd060 | Load a kind's archive if needed; idempotent |
| `Enemy_LoadFile` | 0x801fd348 | Low-level loader; loaded flags at 0x8055a210, roots at 0x8055a228 |
| `Enemy_LoadStageEnemies` | 0x800f25b4 | During stage load, loads every archive on the stage's enemy list, event actors included; skipped entirely in City Trial Free Run |
| `Enemy_GetActorData` | 0x801fd498 | The kind's sub-entry, or 0 when its archive is not loaded |

An actor whose archive is not loaded fails to create, so spawning a kind the stage did not load
needs `Enemy_CheckAndLoad` first. Every archive costs heap; loading many at once can run out.

## EventActor_Create (0x801fbb50)

The single factory, for any kind 0x00-0x4E. It takes a 0x60-byte `EventActorDesc`, which
`EventActorGObj_InitFromDesc` (0x801fb53c) copies into the new `EnemyData`. It creates a GObj of
class 21 on p_link 12 with a GX link (`Enemy_GX`, 0x801fd158), allocates and zeroes the
0xBC0-byte `EnemyData` (destructor `EventActor_Destructor`, 0x801fcca0), resolves the actor
data, loads the model, runs the kind's init callback, adds the ten per-frame procs and finally
runs the kind's post-create callback, which snaps the actor to the ground or its spline and
enters state 0x0E.

The descriptor fields with non-obvious meaning:

- `spawn_index` (+0x2C) and `spawn_slot` (+0x30) are -1 for an actor outside the spawn pool;
  `lifetime` (+0x38) only applies when `spawn_index` is not -1.
- `leash_radius` (+0x50) of -1.0 means no leash and enables path following; any other value
  clamps the actor to that horizontal radius around `leash_center` (+0x44).
- `variant` (+0x3C) is copied to `EnemyData.tier_flags`. For the rider parts 0x48-0x4A it
  carries the parent GObj instead; the meteor packs its fall parameters into it.
- `is_airborne` (+0x40) seeds `EnemyData.is_airborne`.

Composite enemies spawn their part in their init callback with `EventActor_SpawnChild`
(0x801fcda0), which links `child.parent_gobj`. The parts read their parent every frame, so they
cannot be spawned on their own.

`EventActorGObj_Destroy` (0x801fbf2c) destroys an actor and its child chain, deepest first,
killing their effect groups; destroying the GObj any other way skips that. Every actor is also
freed at the next scene change.

### Spawning one from mod code

```c
Enemy_CheckAndLoad(ENEMYKIND_WADDLE_DEE);

EventActorDesc desc;
memset(&desc, 0, sizeof(desc));
desc.kind = ENEMYKIND_WADDLE_DEE;
desc.position = pos;
desc.forward.Z = 1.0f;
desc.up.Y = 1.0f;
desc.scale = 1.0f;
desc.spawn_index = -1;
desc.spawn_slot = -1;
desc.leash_radius = -1.0f;

GOBJ *actor = EventActor_Create(&desc);
```

The actor runs its normal AI from wherever it is placed and interacts with machines and riders
through the usual hit collision. To make it follow a spline, set `spline_path_ready`
(`ed+0x654`) to 1 and `spline_direction` (`ed+0x5F8`) to 1 and call `EventActor_PathInit`.

## The spawn manager

Regular enemies on a course are placed by a manager rather than by stage code. Its state is in
four r13 globals: an init flag (r13+0x628), the four spawn slots (r13+0x62C, one per player in
Air Ride), `stc_enemy_spawn_data` (r13+0x630, the stage's spawn table) and the `EnemyMgr`
(r13+0x634: counters, the active-enemy count and City Trial's `time_progress`).

`stc_enemy_spawn_data` points at an `EnemySpawnData`: a count, the `EnemySpawnEntry` table (0x38
bytes per spawn position, loaded verbatim from the stage file), a `secondary_table` of
meta-enemy groups, and a config whose `mode` (+0x28) the stage file fixes: 1 for Air Ride
courses, 2 for Kirby Melee 1, 3 for Kirby Melee 2. Each entry names a position in the stage's
enemy-position table (`grGetEnemyPosition`, 0x800d0cd4), a scale, a lifetime and up to four or
five `{enemy id, weight}` pairs at mode-dependent offsets, which the `EnemySpawnEntry` union
models:

| Mode | Enemy ids | Weights | Lifetime base / jitter |
|---|---|---|---|
| 1 (Air Ride) | `s16[4]` at +0x1E | `s16[4]` at +0x26, -1 ends the list | +0x1A / +0x1C |
| 2 (Kirby Melee 1) | one `s16` at +0x06 | one `s16` per meta category from +0x08, by `id - 0x50` | +0x24 / +0x26 |
| 3 (Kirby Melee 2) | `s16[5]` at +0x06 | `s16[5]` at +0x10, -1 ends the list | +0x1A / +0x1C |

`location_index` (+0x00), `scale` (+0x30, negated if negative, 1.0 if 0) and `variant` (+0x34)
are common to all three. A lifetime of 0 or 1 means none; above 1 it gets random jitter.

- `Enemy_SpawnerDecide` (0x800f1a14) picks what spawns at a position: a weighted draw over the
  entry's ids. Ids 0x50-0x5E are meta-enemies: `id - 0x50` indexes `secondary_table`, and a
  second weighted draw picks from that group's `{id, weight}` pairs (terminated by a weight of
  -1; a leading id of -1 is a legitimate "spawn nothing"), repeating until a concrete kind comes
  out. Mode 2 instead draws a category, biased by `time_progress`, then an enemy from that
  category's weight column (`Enemy_SpawnerDecideMode2`, 0x800f0efc).
- `Enemy_SpawnActor` (0x800f13a8) builds the descriptor and calls `EventActor_Create`
  (`Enemy_SpawnActorMode2`, 0x800f16c0, for mode 2).
- `Enemy_Think` (0x800f3904, Air Ride) and `Enemy_CityTrialThink` (0x800f33c0) are the per-frame
  manager procs. They keep the spawn slots on the players and track occupancy; the City Trial
  one also counts down respawn timers and calls the spawner.
- `Enemy_UnregisterFromSpawnSlot` (0x800f3b28) frees an actor's slot, decrements the active
  count and arms a respawn timer; it returns at once for an actor with no slot.

## Which stages spawn enemies

The decision is data, never a mode check. During stage load `Enemy_InitPositionData`
(0x800f2634) asks the spawn-data provider for the stage's entry: NULL if `GrData+0x28` is NULL,
else the `EnemySpawnData` the stage table's enemy-position id selects. It then gives up when the
count, config or entry table is empty, and otherwise takes the mode from the config.

| Stage | Ground file | Regular enemies |
|---|---|---|
| Air Ride courses except Nebula Belt | GrPlants1, GrHeat2, GrDesert1, GrIce1, GrSky2, GrValley2, GrMachine2, GrCheck2 | Yes, mode 1 |
| Nebula Belt | GrSpace2 | No - NULL spawn table |
| Kirby Melee 1 | GrPasture1 | Yes, mode 2 |
| Kirby Melee 2 | GrColosseum5 | Yes, mode 3 |
| Other stadiums, test grounds | GrZeroyon*, GrJump*, GrColosseum1/3, GrDedede1, GrSimple* | No - NULL spawn table |
| City Trial city | GrCity1 | No - the table holds one all-zero entry, so the spawner is inert |

Air Ride courses draw only tier-0 kinds. The city's only enemy-system actors are the event
actors (TAC, Dyna Blade, Meteor), which its events create directly.

The stage file is the ground file of a `GroundKind`, which is not the menu's `StageKind`;
`Gm_GetGrKindFromStageKind` (0x80261ce8) maps one to the other. They coincide for the first
three courses and City Trial (9) but not for most others: Machine Passage is StageKind 6 but
GroundKind 5 (`GrMachine2`), and Kirby Melee 1 and 2 are StageKind 17 and 18 but GroundKind 14
and 17.
