# Yakumono System

"Yakumono" is the framework Kirby Air Ride uses for **interactive stage objects**: stage-bound props that usually carry hurt/hit collision, may animate, and may emit drops or spawn child objects. Shipped kinds cover destructible scenery (volcano walls, houses, rocks, coral, icicles, ice columns, fans), activatable hazards (laser gates, push-out walls, rising cubes, gondolas, cannons, light tunnels), damage/healing trigger volumes (catch zone, recovery zone, down-force zone), pillars, and boss-like fixed actors (WhispyWoods, Lighthouse).

Every yakumono is a GObj with entity class 15 on `GAMEPLINK_YAKUMONO` (p_link 8), carrying a `YakumonoData` user-data block, created through one factory (`GrYaku_Create`, `0x800f446c`) parameterised by a `YakuKind` (`YakumonoData.kind`) into a 70-entry (`YAKUKIND_NUM`) descriptor table at `0x804a5be8`. Structures and enums are declared in `externals/hoshi/include/yakumono.h`; the collision types the break families bind to are in `collision.h` and `stage.h`.

This is a different system from the enemy / event actors (Tac, Dyna Blade, Meteor) on `GAMEPLINK_ENEMY`, from the City Trial event engine that triggers those actors, and from items on `GAMEPLINK_ITEM`. Each has its own p_link and lifecycle.

## Spawn Dispatch

`grInitYakumono` (`0x800f425c`) runs during `grLoadStage`, and both of its dispatch paths run when both are present - they are not alternatives:

1. **Per-grkind hook.** `grInitYakumono` reads a 28-entry table at `0x804a322c` indexed by the physical `GroundKind` and calls the entry's `+0x04` slot if non-NULL. For City Trial that hook is `grDataCity1_CreateYakumono` (`0x8010f268`), which calls 31 named per-instance creators by hand, each hardcoding its own kind (range 16..69) and passing a `data_idx`. Hardcoding keeps the spawn order deterministic instead of depending on the data file's entry order.
2. **Generic entry walk.** `grInitYakumono` then allocates the per-stage `YakumonoData *` index array and walks `grdata->yakumono->entries[]`, dispatching each entry through the 16-entry common-kind create table at `0x804a5ba8` (`stc_yaku_common_create`). Those wrappers call `GrYaku_Create_Generic` (`0x800f4b20`), a `GrYaku_Create` variant that reads its param array from the loaded `Yakumono.dat` archive (`r13[0x5e4]`) rather than from `grdata->yakumono->data_array[]`. The generic path uses only the paired generic descriptors at kinds 0..15.

`entries[]` sits at YakumonoNode+0x10 with its count at +0x14, independent of the `data_array[]` pair at +0x00/+0x04, and each entry is 0x0c bytes of `{kind, data_idx, common_group}`. Entry kind 12 (`GrYaku_DispatchEntry12`, `0x800f9be0`) is the ground copy panel: it contributes a kind-15 `GrCZK_RandomAbility` collision zone past the terrain model's own, which is how Nebula Belt (four entries) and Celestial Valley (one, on top of the tree) get copy panels no other Air Ride course has.

Finally `GrYakuCommon_SelectRandomGroup` (`0x800f3f9c`) picks a random subset of the entries tagged with a common-group id (`YakumonoEntry.x08`, -1 = none) and disables the rest via `zz_800f5744_(yd, 0)`. `GRYAKU_COMMON_GROUP_MAX` is 20 and `GRYAKU_COMMON_RANDOM_SET_NUM` is 10.

City Trial ships `entry_count = 0`, so only the hook path contributes there.

## Source Files

The framework decomposes into files identifiable from the assert strings embedded in the descriptor blocks:

| File | Role |
|---|---|
| `gryaku.c` | Core: `GrYaku_Create`, `YakumonoGObj_InitData`, the 7 procs, GObj wiring |
| `gryakuanim.c` | `Gr_StateChange`, `YakumonoGObj_AddAnim`, `YakumonoGObj_RemoveAnim` |
| `gryakueffect.c` | Effect spawning from anim event lists |
| `gryakuaudio.c` | Audio emitter / track allocation |
| `gryakulib.c` | `grYakuCheckGObjYakumono`, scaling helpers |
| `gryakucommon.c` | Common-group helpers and the random-subset selector |
| `gryakubreakcommon.c` | Shared break helpers (ring damage, range checks) |
| `gryakubreakcoll.c` / `gryakubreakhpcoll.c` | Shared collision base, and the HP-per-region variant that backs the "strong" City Trial families |

Beyond those, one file per kind (`gryakucannon.c`, `gryakugondola.c`, `gryakubreakrock.c`, ...) implements one or two kinds each. `YakuKind` in `yakumono.h` carries the game's own kind values and names the confirmed ones. `YAKUKIND_COMMONTERMINATE` (16) is the bound `grInitYakumono` asserts (`common_data->kind < Gr_YakuKind_CommonTerminate`) before indexing the common-kind create table.

## Per-Stage Manifest

`grdata->yakumono` (GrData+0x40) is a `YakumonoTable`: a `data_array[]` of per-instance param-block pointers indexed by `data_idx`, and an `entries[]` array of kind-tagged spawn entries. The two arrays are independent - a stage may populate one, the other, or both. `GrYaku_Create` asserts `0 <= data_idx < data_count` and that the slot is non-NULL.

The param block a slot points at is a `YakumonoParam` union whose layout is kind-specific. Four fields in it gate the Create init pipeline, and a zero field takes the "skip" branch of its consumer rather than failing:

| Gate field | Consumer | Non-zero | Zero |
|---|---|---|---|
| `+0x04` jobj_data | `GrYaku_AllocJObj` (`0x800f7308`) | allocates the model JObj into `ydata->model_jobj` | empty alloc, `model_jobj` stays NULL |
| `+0x04` then `jobj_data[0][+0x10]` | `GrYaku_AttachAnim` (`0x800f6394`) | attaches the anim (anim data is bundled under the same block; there is no separate param field) | no-anim init |
| `+0x0c` model_data | `GrYaku_AttachModel` (`0x800f6274`) | attaches the model and positions it | only writes the default position into `ydata->pos` |
| `+0x14` audio_desc | `GrYaku_InitAudio` (`0x800f77dc`) | fills the fgm/audio block from `{idData, idDataNum, track_param}` | leaves the audio block zero |

These gates decide **whether the framework allocates its own attached JObj/model**, not whether the yakumono ends up visible. Several kinds - the cannon and every break family - leave both zero and take their visible mesh from the stage's own scene graph instead.

## Lifecycle

`GrYaku_Create(kind, data_idx)` returns the new GObj so the per-instance creator can run its tail-init on it immediately. It:

1. `GObj_Create(15, GAMEPLINK_YAKUMONO, 0)` - gx_link 0, so yakumono render through HSD's main path rather than a custom GX link.
2. `HSD_ObjAlloc` the `YakumonoData` from the class at `0x80557584`, bound with `GObj_AddUserData(gobj, GUDATA_YAKUMONO, GrYaku_DestroyCallback, ydata)`.
3. `grobj->yaku_num++` (GrObj+0x6fc).
4. `YakumonoGObj_InitData` (`0x800f4d50`) - stores gobj/kind/param, clears flag bits 3/4/6/7, seeds `scale = 1.0`, `forward = (0,0,1)` and `up = (0,1,0)`, `state = -1`, all seven callback slots to NULL, and looks up `stc_yaku_descs[kind]->state_table` into `ydata->state_table`.
5. The init pipeline, each step a separate helper: `GrYaku_AllocEffectGroup` (`0x800f666c`), `GrYaku_InitLighting` (`0x800f72cc`), `GrYaku_NoOp` (`0x800f5798`, a bare `blr`), `GrYaku_AllocJObj` (`0x800f7308`), then `xform_jobj = NULL` followed by `GrYaku_InitMatrix` (`0x800f73fc`) - which reads `xform_jobj` and is therefore a no-op at create and stays one for static and break-family props - then `GrYaku_AttachModel` (`0x800f6274`), `GrYaku_InitAudio` (`0x800f77dc`), `GrYaku_AttachAnim` (`0x800f6394`), and `GrYaku_InitHurtData` (`0x800f8484`), which calls `HurtData_Create(gobj, 6, 2, regionCount, 0)`.
6. Adds the seven procs below.
7. `GrYaku_FinalSetup` (`0x800f4ea0`) - clears a flag bit and inits the bbox.

`GrYaku_DestroyCallback` (`0x800f4f0c`) is the user-data destructor: it frees the HurtData, the effect group, the model JObj tree, and unlinks the prop.

### The 7 procs

Priorities are uniform across every yakumono kind; only the per-type callbacks in `YakumonoData` differ, and those are populated by the per-instance tail-init after `GrYaku_Create` returns and by `Gr_StateChange`.

| Pri | Symbol | Address | What it does |
|---:|---|---|---|
| 1 | `YakumonoGObj_Think` | `0x800f5284` | Reset HurtData damage flags, advance HurtData, run the state machine and anim updater (`GrYakumono_StepStateAndAnim` `0x800f5944` -> `GrYakumono_StepStateMachine` `0x800f5ac4` + `GrYakumono_StepAnimEvents` `0x800f9030`), then call `anim_callback` |
| 4 | `YakumonoGObj_Proc4` | `0x800f52e8` | Call `phys_callback`; if flag bit 7 is set, rebuild the matrix via `GrYaku_InitMatrix`; then `zz_800f62c4_` |
| 5 | `YakumonoGObj_Proc5` | `0x800f5340` | Call `envcoll_callback` - pure dispatch |
| 6 | `YakumonoGObj_Proc6` | `0x800f5374` | Call `post_envcoll_callback` - pure dispatch |
| 7 | `YakumonoGObj_Proc7` | `0x800f53a8` | Call `trigger_callback`, then `HurtData_UpdatePerFrame(ydata->scale, hurtdata, NULL, 2, NULL)` |
| 9 | `YakumonoGObj_Proc9_HitColl` | `0x800f53fc` | `HitColl_Init` -> three stage-side hooks (`zz_800f85a0_`, `zz_800f85fc_`, `zz_800f8658_`) -> `HitColl_ActOnCollision` -> `zz_800f86b4_`. The hook point for filtering damage application |
| 10 | `YakumonoGObj_Proc10` | `0x800f5454` | Damage dispatch. Gates on `hurt_data->kb_mag` (+0x24) being non-zero; accumulates into `ydata->dmg` via `GrYakumono_AccumulateDamage` (`0x800f875c`, capped at 9999) then calls `on_damage_callback` with `&hurt_data->hitcoll_log_idx`. If that float is zero and `hurtdata+0x64` is set, calls `off_damage_callback` |

Phase meaning: 1 update/state machine, 4 pre-physics adjust, 5 and 6 mid-pipeline kind hooks, 7 HurtData advance, 9 HitColl resolution, 10 post-damage callbacks.

### Other framework helpers

| Symbol | Address | Purpose |
|---|---|---|
| `YakumonoGObj_GetState` | `0x800f7ab8` | `ydata->state`, or -1 if the GObj is not a yakumono |
| `YakumonoGObj_GetKind` | `0x800f7a64` | Asserts entity class 15, returns `ydata->kind` |
| `grYakuCheckGObjYakumono` | `0x800f7a50` | 1 iff entity class is 15 - the canonical yakumono test |
| `YakumonoGObj_GetHurtData` | `0x800f8248` | `ydata->hurt_data` |
| `Gr_StateChange` | `0x800f5548` | Advance state and play the state's anim; called from per-kind handlers and every tail-init |
| `YakumonoGObj_AddAnim` / `YakumonoGObj_RemoveAnim` | `0x800f5ce8` / `0x800f5f3c` | Animation chain helpers |
| `grLoadYakumono` | `0x800f440c` | Loads `YkCommon.dat` and `Yakumono.dat` if not already loaded, into `r13[0x5e0]` / `r13[0x5e4]` (`grLoadYakumono_Common` `0x800f8254`, `_Main` `0x800f82a0`) |
| `Yakumono_Preload` | `0x800f82ec` | Reads the stage table at offset 0x3C and, if non-NULL, calls `Preload_CreateEntry` (`0x80072c90`) with shape params `(2, 4, 4, 0, 1, 8, 16)` - a small fixed preallocation of yakumono GX state |
| `Gr_GetYakumonoSpawnTotal` | `0x800f7db0` | Props the current stage places for a kind. Only `GR_CITY1` and `GR_DESERT1` (Sky Sands) ship a spawn-count table; everything else returns 0 |

Two counters live on `GrObj`: `yaku_num` (+0x6fc) is the live **GObj** count, incremented once per `GrYaku_Create`; `yaku` (+0x710) is the spawn-ordered `YakumonoData *` array. **`yaku` is sized from `entry_count`, not `data_count`**, so a stage that only uses the per-grkind hook path leaves it NULL (`HSD_MemAlloc(0)` returns NULL). City Trial is exactly that case. Nothing in the per-frame procs reads the array, so NULL is harmless in vanilla - but mod code enumerating live yakumono must walk the `GAMEPLINK_YAKUMONO` list instead.

The framework's centralized asserts are worth knowing because they bound what mod code may do: `data_idx` within `grGetYakuDataNum` / `grGetYakuStaticDataNum`, `kind < YAKUKIND_COMMONTERMINATE`, `gyp->scale == Gr_DefaultScale`, `!yaku_data->localCollData`, `gp->yaku_num > 0`, and `grYakuFuncTable[kind]` plus its `coll_func` / `adhere_update_func` / `get_point_func` slots being non-NULL.

## City Trial Manifest

`grDataCity1_CreateYakumono` makes 31 calls (`data_idx` 0..30) and City Trial's `entries[]` is empty, so CT creates exactly 31 yakumono **GObjs**. That is 31 GObjs, not 31 props: the break families are multi-instance, so each of those creator calls fans out to many placed props and the visible/collidable count is in the hundreds.

`data_array` has 33 slots, so slots **31 and 32 are spare** - allocated but referenced by no vanilla creator. Mod code can repoint them at a custom param block and spawn a 32nd or 33rd yakumono without disturbing a vanilla slot.

| data_idx | creator | kind | object |
|---:|---|---:|---|
| 0 | `0x800fa2a0` | 17 | catch zone (passive) |
| 1 | `0x800fa610` | 18 | unnamed trigger zone (passive) |
| 2..17 | `0x800fe5d4` x16 | 46 | gondola / cable-car loop, one GObj per car |
| 18..19 | `0x80109db4` x2 | 61 | small animated decorative props |
| 20 | `Lighthouse_Create` | 68 | Lighthouse |
| 21 | `whispyLogic` | 69 | WhispyWoods boss tree (Forest event) |
| 22..23 | `0x80106824` | 32 | forest pitfall (multi-stage cracking floor) |
| 24 | `0x80108f10` | 37 | volcano-base hole covers |
| 25 | `0x80108ce8` | 36 | volcano rock walls |
| 26 | `0x80109138` | 38 | dilapidated city houses |
| 27 | `0x80107bfc` | 33 | coral |
| 28 | `0x80107d64` | 34 | forest trees |
| 29 | `0x80107ecc` | 35 | volcano + high-plains rocks |
| 30 | `0x801043c8` | 29 | star pole (BigStar) |

City Trial therefore uses 14 kinds: 17, 18, 29, 32, 33, 34, 35, 36, 37, 38, 46, 61, 68, 69. Events add more at event time, outside this manifest: the Pillar event's huge pillars (40, `YAKUKIND_EVENTPILLAR`, `event_pillar_start`), the Restoration Area event's recovery zones (20), Rail Fire (65), the Secret Chamber (66) and the UFO (67).

### Breakable inventory

The identity anchor is the per-kind break counter: `YakumonoGObj_IncrementBreakCount` (`0x80105d80`) reads the broken prop's `kind` and calls `Ply_IncrementYakumonoBreakCount` (`0x8022fed8`), which bumps `PlayerStats.yakumono_break[kind - 0x15]` (byte `PlayerStats+0x62b+kind`, valid range kinds 0x15..0x28). Each checklist cell's human-readable text therefore pins a kind to a concrete object. Family comes from the descriptor's `coll_func`.

| kind | object | family / `coll_func` | props placed in CT |
|---:|---|---|---:|
| 29 | star pole (BigStar) | `gryakubreakcoral.c` / `hitBigStar` (`0x80103eb8`) | 1 |
| 32 | forest pitfall | `gryakubreakfloor.c` / `hitBreakableFloor` (`0x80106bd0`) | 1 (+ an optional linked second part) |
| 33 | coral | weak / `hitWeakObject` (`0x80107914`) | 10 |
| 34 | forest trees | weak / `hitWeakObject` | 53 |
| 35 | volcano + high-plains rocks | weak / `hitWeakObject` | 41 |
| 36 | volcano rock walls | strong / `hitStrongObject` (`0x801086d0`) | - |
| 37 | volcano-base hole covers | strong / `hitStrongObject` | - |
| 38 | dilapidated houses | strong / `hitStrongObject` | 30 |

Counts come from `Gr_GetYakumonoSpawnTotal`. Kinds 33 and 36 have no checklist cell (`yakumono_break[12]` and `[15]` are unread by the checklist), so their object identity rests on family plus placement rather than on cell text. Kind 33 does have a vanilla consumer: `zz_8027a6a0_`, which the incrementer calls in City Trial, plays a player FGM once the present players' combined breaks of kind 33, 34 or 38 reach the stage total.

City Trial coral (33) is not the kind `gryakubreakcoral.c` is named for. That file's kinds (24..29) all use `hitBigStar`; its namesake is Sky Sands' coral, kind 24 (`YAKUKIND_BREAKCORAL`, 13 placed by the `GrDesert1` hook), which the Air Ride "break all coral" cell reads through `Ply_GetAllCoralBrokenFlag` (`0x8022fd48`: `yakumono_break[3]` against `Gr_GetYakumonoSpawnTotal(24)`). The "high-plains hole you fall into" is a separate counter (`PlayerStats.highplains_hole_entries`, +0x834), not a kind break bucket.

### Instance models

Each of the 31 creator calls produces exactly one GObj, but where that GObj's geometry and transform live differs by family. What matters for tooling is which pool a prop occupies and where its owner back-reference is written, because that is what a pool-scanning mod keys on.

| kind(s) | instance model | binding |
|---|---|---|
| 33/34/35 (weak), 36/37/38 (strong) | **Multi-instance break family** - 1 GObj, N props | The creator allocates a `GrCollRecord *` array at `ydata->region_audio_arr` (+0x130) and loops `grScene_FindInstanceByKey(&grobj->coll, grobj->joint_table[entry.node_id].jobj)` once per placed prop, storing each record and setting `record->yaku_gobj = gobj`. Weak keeps the count at +0x134 and adds an `0xFF`-seeded audio-source array at +0x150; strong allocates three parallel per-prop arrays (HP at +0x134, two byte-state arrays at +0x138/+0x13c) with the count at +0x140 |
| 29, 32, 69 | **Single-prop break family** - 1 GObj, 1 prop | Same path, one record stored directly at +0x130 with `record->yaku_gobj = gobj`. 69 uses this shape but is a boss, not a checklist break bucket |
| 17, 18 | **Trigger volume** | Claims a **collision zone**, not a record: `zz_800d79c0_(&grobj->coll, joint_jobj, NULL)` scans `coll.zone` (0x140 stride, count `coll.zone_num`), stores the zone pointer at +0x130 and writes the owner GObj at **zone+0x138** |
| 46 (gondola) x16 | **Movable, zone-backed** | Same zone lookup, but keeps the zone *index* at +0x138, sets +0x130 to 0, and clears the matrix-dirty bit so it animates its own transform each frame |
| 68 (Lighthouse) | **Multi-part, zone-backed** | `Lighthouse_Init` (`0x8010d260`) walks its sub-part list clearing bit 7 of `zone+0x13c` for each; +0x130 stays 0. Not breakable, no `GrCollRecord` |
| 61 (decorative) x2 | **Classic single-instance** | No pool entry at all - the standard `GrYaku_AttachModel` path, with its own model JObj and `ydata->pos`; +0x130 and +0x144 are anim scratch |

Consequences:

- "One GObj with many children" holds only for the break families. Those, plus the single-prop break families, are the props reachable by scanning `Gr_GetCollRecords()` for a non-NULL `yaku_gobj`. Terrain records leave it NULL.
- Zones, gondola and Lighthouse live in `coll.zone`, a different array with a different stride, and write their owner at zone+0x138. A `GrCollRecord.yaku_gobj` scan cannot reach them at all, which is why Hypernova's vacuum can safely walk the whole record pool; its breakable set is `29,32,33,34,35,36,37,38`, deliberately omitting 69 (boss).
- **Weak-family props are `JOBJ_SKELETON` joints; the others are static.** Each prop's `record->jobj` flags are `0x9` (`JOBJ_SKELETON | JOBJ_CLASSICAL_SCALING`) for trees/rocks/coral, and `0x40008` (`JOBJ_OPA | JOBJ_CLASSICAL_SCALING`, no skeleton bit) for houses/walls/holes/floor/BigStar/whispy. That changes how you move them.

## Break Path

The City Trial break families do **not** install an `on_damage_callback` - `YakumonoGObj_InitData` zeroes it, every `Gr_StateChange` clears it, and their creators leave it unset. The only exception is the BreakCoral family (the star pole in City Trial): its creators install `GrYakuBreakCoral_DropItems` (`0x801040fc`) right after their initial state change, and `hitBigStar` re-installs it whenever it changes state. So `YakumonoGObj_Proc10`, even with a seeded lethal hit in HurtData, just accumulates into `ydata->dmg` and returns. **Seeding HurtData is a no-op for every CT break family except the star pole.**

The real break is the descriptor's `coll_func`. `collideWithObject` (`0x800f5004`) resolves `stc_yaku_descs[ydata->kind]->coll_func`, asserts it is non-NULL, and calls it as `coll_func(yaku_gobj, other_colldata, gcp, tri_idx, contact)`, returning its result: 1 if the prop broke. (Its assert, `grYakuFuncTable[gyp->kind] && grYakuFuncTable[gyp->kind]->coll_func`, is what names `stc_yaku_descs` as the game's `grYakuFuncTable`; the 16-entry common-kind create table is a different table.) The handler computes an impact force and compares it to the prop's HP:

- **`GrYaku_TestImpactBreak` (`0x80104cd4`)** - one-shot threshold, non-subtractive. `force = other->radius (CollData+0x344) * impactSpeed^2`; breaks iff `force >= param[0]` (`YakuBreakPlacement.hp[0]`). It does not write HP back, so a too-weak hit leaves nothing behind and nothing accumulates. Used by `hitWeakObject` and BigStar phase 1.
- **`GrYaku_ApplyImpactDamage` (`0x80104be0`)** - subtractive. Same force, but `*hp -= force` is written back to the prop's own HP and the break fires when it reaches 0, so hits accumulate; `param[0]` is unused. Used by `hitStrongObject` and the rock/house drop handlers.

Both return 1 on a break and then cap the collider's speed: `other->yaku_break_speed_cap = param[2] * (1 - overshoot / param[1])`, clamped at 0, with `req_yaku_break_effect` set.

**`impactSpeed` is a normal projection, not `|delta|`.** `grScene_GetImpactSpeed` (`0x800d8edc`) projects `other->pos_delta` (CollData+0x14) onto the contacted triangle's outward normal, negates it, and clamps a non-positive result to 0. The delta must point **into** the surface; a delta whose dot with the normal is `>= 0` gives impact speed 0, force 0, and no break. This is the trap for a synthesized collider.

Per-family trigger:

| family | handler | trigger |
|---|---|---|
| weak (33, 34, 35) | `hitWeakObject` (`0x80107914`) | Single hit with `force >= HP`. Spawns debris effects and credits the break, but does not hide the original mesh inline - it `Gr_StateChange`s the prop into a broken-state model that renders at the prop's baked spot |
| strong (36, 37, 38) | `hitStrongObject` (`0x801086d0`) | Per-region subtractive HP; may take several hits. Does the full visible break inline at the passed contact point, so a synthesized break renders correctly wherever the contact is |
| floor (32) | `hitBreakableFloor` (`0x80106bd0`) | Multi-stage crack, one stage per call, final break after N stages (max from the param block) |
| BigStar (24..29) | `hitBigStar` (`0x80103eb8`) | Phase 1 needs `force >= HP`; a weaker hit (when the param has an arm anim) *arms* phase 2 by setting bit 7 of +0x150, after which the next collision breaks it with no force test |

### Synthesizing a break

Because the break is gated on a real collider's mass and velocity, there is no "apply N damage" entry point that ignores collision. In order of cleanliness: let a physically larger rider ram props so the natural collision drives the real break; synthesize a `collideWithObject` call with a fabricated high-force collider; or skip the break and just move/scale the prop. Driving the state machine straight to the broken state via `Gr_StateChange` skips drops, score and effects and is not worth it.

Synthesis is what Hypernova's vacuum uses (`Hypernova_BreakInstanceNative` in `mods/hypernova/src/hypernova_vacuum.c`), because ramming cannot work for a *pulled* prop: its triangles stay baked at its origin, so the rider never overlaps a mesh that has been drawn in. The constraints that make the call land:

- `gcp` must be `&grobj->coll` (`Gr_GetCollParam()`), the same `GrCollParam` the engine's own break dispatch and `grScene_FindInstanceByKey` use. Any other base makes the derived triangle index wrong and the call silently no-ops.
- `tri_idx` is a **global** index into `coll.tri`, computed as `record->tri_begin - Gr_GetCollTris()`, and it must name a triangle with a non-degenerate normal, since the impact-speed calc projects onto it. Scan `record->tri_begin[0 .. tri_num)` for the first with a usable normal.
- The synthetic `CollData` needs `radius` (+0x344) cranked far above any HP - it is the force lever - `pos_delta` set to `-normalize(normal) * M` so it projects positive, `g` (+0x04) set to the rider GObj so `GrYakuBreak_GetAttackerPly` (`0x80105cb0`) credits the break, and `coll_info` pointing at a zeroed `mpCollInfo` with `contact_tri_id = -1`. That last one matters: `destroyBigStar` (`0x800d7b8c`) runs first in every `coll_func` and would otherwise read triangle 0 as a BigStar contact.
- Clear `GRCOLL_FLAG_MOVING` (0x20) on the target triangle for the duration of the call and restore it after. When set, `grScene_GetImpactSpeed` takes a geometry-refined path through the record's `prev_inv`/`world` that can rewrite a synthetic delta to zero.
- The prop must be fully collidable when the call fires (`grScene_IsInstanceCollAll(record, 1)`), so re-arm with `grScene_SetInstanceColl(record, 1)` if collision was retired beforehand. Detect success by re-checking afterwards - the break tail clears it.
- Weak families anchor their debris to a separate `grobj->joint_table` node at the prop's baked spot (`YakuBreakEntry.node_id`), so a pulled prop's debris appears in the wrong place unless that node's matrix is relocated onto the contact point for the call, with `JOBJ_USER_DEFINED_MTX` set so the write is honored.

`collideWithObject` then runs the genuine break tail: collision retire, mesh hide or debris, family drops, SFX, `YakumonoGObj_IncrementBreakCount`, and the state change. The multi-stage floor advances one crack stage per call.

### Destroying a prop directly

`GObj_Destroy` (`0x80428f64`) calls the registered user-data destructor synchronously, so `GrYaku_DestroyCallback` frees the HurtData, effect group and JObj tree and unlinks the prop - a raw destroy leaves no dangling collision entry. The reason to prefer the break path is gameplay fidelity (drops, break credit, anim, SFX), not safety. Capture `gobj->next` before destroying while walking the p_link list.

For a break family this destroys the **whole group**, since one parent GObj manages all N props - and `model_jobj` is NULL for those families, so the destructor frees nothing visible and the sub-instance meshes keep rendering after the parent is gone. To remove a single breakable prop, drive its break instead.

## Collision, Position and Movement

Break props are authored at local origin `(0,0,0)` in the stage model archive; their placement is data-driven and applied at load. Two independent representations exist, and City Trial breakables use the second.

### Unified stage-placement tables

KAR has a generic "where does object N of category C go" system. Every category has a count function and a record loader, and every record is 0x24 bytes = 9 floats = 3 `Vec3`s (position plus two more vectors; the loaders copy all nine verbatim, so the meaning is set by the consumer).

| C | Category | Count fn | Loader |
|--:|---|---|---|
| 0 | start positions | `grGetStartposNum` `0x800d0b30` | `grGetStartPosition` `0x800d0b7c` |
| 1 | enemy spawns | `grGetEnemyposNum` `0x800d0c88` | `0x800d0cd4` |
| 2 | items | `grGetItemposNum` `0x800d1090` | `grLoadItemPosition` `0x800d10dc` |
| 3 | item areas | `grGetItemAreaposNum` `0x800d1550` | `0x800d15a8` |
| 4 | events | `grGetEventposNum` `0x800d11d4` | `loadEventLocations` `0x800d11fc` |
| 5 | vehicles | `grGetVehicleposNum` `0x800d12f0` | `0x800d133c` |
| 6 | vehicle areas | `grGetVehicleAreaposNum` `0x800d16c4` | `0x800d171c` |
| 7 | global-dead | `grGetGlobalDeadPosNum` `0x800e5318` | `0x800e5340` |
| 8 | yakumono | `grGetYakumonoposNum` `0x800d1434` | `loadYakumonoLocations` `0x800d145c` |

The yakumono count comes from `grdata->pos_node` (GrData+0x20) `->[+0x2C]->[+0x8]`, a distinct field from `coll_node` (GrData+0x18) which feeds the collision instance pool. Each category caches its record-array base in a different `GrObj` slot (start at +0x134, yakumono at +0x15c).

Only two call sites read the yakumono table. `grResolvePlacementRef` (`0x80088408`) is a stage-agnostic per-descriptor resolver: it reads the placement-group record at `ref->[+0x2c]`, switches on the group's category, and dispatches to the matching loader. It handles categories 0, 1, 2, 4, 5, 7, 8 - there is no case for item areas or vehicle areas, which are resolved elsewhere - and it has no direct xrefs, being reached through a function-pointer slot. `dbPosition_Load` (`0x800869cc`) is the debug position editor: it destroys existing marker GObjs, then loops every record of a category dropping a visible marker via `dbPosition_CreateGObj` (companion renderer `dbPosition_Render` `0x80086d64`). It covers yakumono, which is direct evidence these coordinates are meant to be enumerated and moved.

### The collision instance pool

Breakables bind to the stage's collision pool, not to the placement table. `grColl_Alloc` (`0x800d6dcc`) allocates the runtime arrays into `grobj->coll` (a `GrCollParam` at GrObj+0x54), sized exactly from the capacity mirror `coll_max`, and the fill pass populates them from `grdata->coll_node`. Two arrays matter:

- `coll.record` - `GrCollRecord`, 0x98 bytes, one per placed instance. `record->jobj` is the positioned joint; `record->world` caches that joint's world matrix as of the last bake; `record->tri_begin`/`tri_num` name the instance's contiguous slice of the global triangle array; `record->yaku_gobj` is the owning yakumono GObj (NULL for terrain). `grScene_FindInstanceByKey` (`0x800d7954`) searches this array by `jobj`.
- `coll.tri` - `GrCollTri`, 0x40 bytes. Each carries the outward `normal`, the surface `kind` bits, `flags` (including `GRCOLL_FLAG_MOVING`), a back-pointer to its owning record, and a `state` byte whose `GRCOLL_STATE_COLLIDABLE` (0x40) bit is the intact/broken flag.

**These triangles ARE the prop's solid collision** - there is no separate static wall behind them. `grScene_SetInstanceColl` (`0x800d7ad0`) toggles the collidable bit across every triangle of a record (0 = broken), and `grScene_IsInstanceCollAll` (`0x800d7b0c`) is the break path's "still whole?" guard. The rider's penetration response `mpResponse_DispatchSceneObjColl` (`0x80248bb4`) drops any contact whose collidable bit is clear, so clearing it is exactly what makes a broken or vacuumed prop pass-through. Only break and init code writes that bit - nothing re-arms it per frame - so a clear sticks.

**The triangles are not repositioned when the record's joint moves.** They stay baked where the prop was placed. So relocating a prop's model does not relocate its collision, and a vacuum that moves a prop must retire that prop's collision to avoid leaving a stranded invisible wall at the origin.

### Reading and moving a prop

- **Read a break-family prop's position**: walk the parent's record array at `ydata->region_audio_arr`, and take the translation of `record->world` (or of `record->jobj`'s world matrix). Do **not** read the parent `ydata->pos` for break families - it is `(0,0,0)`, and `model_jobj` / `xform_jobj` are NULL, because the parent's own transform is unused.
- **Move a single-instance or active yakumono** (cannon, gondola, rising cube): those own a positioned `xform_jobj`. Write the new local translation on the root JObj (`JObj+0x10`), then set flag bit 7 (`ydata->flags |= 0x80`); the next frame `YakumonoGObj_Proc4` sees it and calls `GrYaku_InitMatrix` to rebuild the world matrix and copy it into the render object. Alternatively write the translation column of `JObj+0x44` directly, which is durable only while bit 7 stays clear - which for a static prop it normally does, since it builds its matrix once at spawn and Proc4 never rebuilds it. Bit 7 is sticky, so a one-shot move means setting it for one frame; continuous motion means keeping it set and rewriting each frame. Also update `ydata->pos`, the cached world position drops and SFX read.
- **Move a break-family prop**: apply the same idea to the sub-instance's `record->jobj`, not the parent. **Skeleton caveat**: the weak families' joints carry `JOBJ_SKELETON`, so their world matrix is rebuilt from the joint SRT every frame by `JObj_SetupMtxSub` (`0x8040d6b4`), a path independent of the yakumono matrix-dirty bit. A direct write to `JObj+0x44` is clobbered next frame unless `JOBJ_USER_DEFINED_MTX` is set first via `JObj_SetFlags` (`0x8040bd64`, single joint, does not recurse), which makes the setup routine early-return and honor your matrix. The static families need no flag; setting it anyway is harmless.
- **Move permanently**: edit the placement data - the 9-float yakumono table, and/or the scene-instance transforms sourced from `grdata->coll_node`.

### Scale

`ydata->scale` (+0xa4, `GR_DEFAULT_SCALE = 1.0`) is the **hurtbox** scale: `YakumonoGObj_Proc7` passes it to `HurtData_UpdatePerFrame` every frame, which recomputes each region's world center from the JObj world matrix and its radius as `base * scale`. Writing it takes effect immediately with no flag, does not trip an assert (the only `Gr_DefaultScale` assert is an init-time float round-trip on the constant, not a check of the live field), and does not corrupt break logic.

It does **not** scale the rendered model - `GrYaku_InitMatrix` ignores it. For the visual, scale the transform JObj's local scale (`JObj+0x24..0x2c`) and dirty the matrix. For break families the scale field lives on the parent, so writing it scales every sub-instance's hurtbox at once, while the visual scale must be written on each `record->jobj` individually.

The two collision systems behave oppositely when a prop moves: the HurtData hurtbox follows a moved or scaled JObj for free, while the solid `GrCollTri` slice never moves.

## Item Drops

Three break families emit items through `City_SpawnMiscItems`, drawing from the `chance_destructible` column of the drop table (drop-source enum 3):

| Family | Drop helper | Per-instance gate |
|---|---|---|
| `gryakubreakrock.c` | `GrYakuBreakRock_DropItems` (`0x8010203c`) | `param[+0x24]` |
| `gryakubreakhouse.c` | `GrYakuBreakHouse_DropItems` (`0x80102794`) | `param[+0x30]` |
| `gryakubreakcoral.c` | `GrYakuBreakCoral_DropItems` (`0x801040fc`) plus `hitBigStar` | `param[+0x28]` |

If the gate pointer is NULL the destruction proceeds with no drop call - same code path, different stage-data wiring. The remaining break files (icicle, floor, fan, animfloor) have no drop helper at all.

The gate points at a drop descriptor:

| Offset | Field | Meaning |
|---:|---|---|
| +0x14 | `Vec3` | Initial item velocity |
| +0x1c | `int` | Drop source enum 0..12 (3 = destructible); -1 falls back to `CityEvent_GetRandomItem` |
| +0x20 | `int` | Shape: 0 = `City_SpawnMiscItemsRing` (`0x80104e10`, omnidirectional), 1 = `shootPowerUps` (`0x801058c0`, directed cone) |
| +0x28 | `Vec3` | Spawn position offsets |
| +0x38 | `int` | Rock family only: switch case 0..7 selecting the drop pattern |

## Descriptor Tables

### stc_yaku_descs (70 entries at 0x804a5be8)

Not homogeneous - two sections with different layouts.

`collideWithObject`'s assert calls this table `grYakuFuncTable`.

**Indices 0..15** are the paired generic descriptors the common-kind create path uses: eight unique 40-byte blocks, each shared by two consecutive indices (the "non-ctrl" and "ctrl" variant of one base kind). Each block holds a state-table base at +0x00 and a per-kind `DescFunc` at +0x1c; the rest is zeros. Because the blocks sit contiguously at 0x28 stride, each block's state-table base lands in the tail of the previous block. Only five of the eight pairs have a `DescFunc` (`GrYaku_BaseKind0_DescFunc` `0x800f94b8` through `GrYaku_BaseKind4_DescFunc` `0x800f9ba4`); pairs 10/11, 12/13 and 14/15 have zeros there, and pair 0/1's state-table base is genuinely all-zero, giving those kinds a single null state.

**Indices 16..69** are the per-instance descriptors the per-grkind hooks hardcode. Each is a 0x14-byte `YakuDesc` - state-table base at +0x00, `coll_func` at +0x04, an optional per-kind init/check pointer at +0x08 (present for e.g. DownForceZone `0x800f9ed0`, zero for the cannon whose handlers live in its state table), `adhere_update_func` at +0x0c and `get_point_func` at +0x10 - with its state table just before it. Both sit in their source file's data next to that file's assert strings (the `gryaku*.c` filename, `Gr_YakuKind_*` names), which is what identifies most kinds. Where a file implements several kinds, the descriptors can fall before or after its strings, and kinds of one family share a state table and `coll_func` (22/23, 24..29, 33..35, 36..38).

A kind's id is the literal its creator passes to `GrYaku_Create` (or `GrYaku_CreateSpawn` for event-time kinds). Named kinds, with the file and creator that pin them:

| kind | `YAKUKIND_` | file | creator | placed by |
|---:|---|---|---|---|
| 16 | `DOWNFORCEZONE` | `gryakudownforcezone.c` (kind assert) | `0x800f9d80` | Sky Sands x7, Celestial Valley x2 |
| 17 | `CATCHZONE` | `gryakucatchzone.c` (kind assert) | `0x800fa2a0` | City Trial, Machine Passage, Beanstalk Park |
| 20 | `RECOVERYZONE` | `gryakurecoveryzone.c` (kind assert) | `0x800faa98` (spawn), `0x800fb378` | Restoration Area event |
| 22 | `BREAKHOUSE` | `gryakubreakhouse.c` | `0x8010250c` | `GrSimple2` only |
| 24 | `BREAKCORAL` | `gryakubreakcoral.c` | `0x80103c58` | Sky Sands x13 |
| 29 | `STARPOLE` | `gryakubreakcoral.c` | `0x801043c8` | City Trial |
| 30 | `BREAKFAN` | `gryakubreakfan.c` | `0x80107120` | Machine Passage x18 |
| 31 | `BREAKICICLE` | `gryakubreakicicle.c` | `0x801044cc` | Frozen Hillside x6 |
| 32 | `BREAKFLOOR` | `gryakubreakfloor.c` | `0x80106824` | City Trial x2 (forest pitfall), Frozen Hillside x6 (ice platforms) |
| 33..35 | `CORAL`, `TREE`, `ROCK` | `gryakubreakcoll.c` | `0x80107bfc`, `0x80107d64`, `0x80107ecc` | City Trial |
| 36..38 | `BREAKHPCOLLDOOR`, `HOLE`, `HOUSE` | `gryakubreakhpcoll.c` (range assert) | `0x80108ce8`, `0x80108f10`, `0x80109138` | City Trial |
| 40 | `EVENTPILLAR` | `gryakubreakrock.c` | `0x8010181c` (spawn) | Pillar event (`event_pillar_start`) |
| 41, 42 | `ROTJUMPHILL`, `ROTJUMPHILLCTRL` | `gryakurotjumphill.c` | `0x800fb5d0`, `0x800fbf60` | Checker Knights x18 + 1 |
| 43 | `INVISIBLEBALL` | `gryakuinvisibleball.c` (its tail-init asserts there) | `0x800fc848` | Sky Sands |
| 44, 45 | `RISINGCUBE`, `RISINGCUBECTRL` | `gryakurisingcube.c` (Ctrl assert in 45's tail-init) | `0x800fcd44`, `0x800fdf6c` | Checker Knights, `GrZeroyon1`, `GrDedede1` |
| 46, 47 | `GONDOLA`, `GONDOLACTRL` | `gryakugondola.c` | `0x800fe5d4`, `0x800feb20` | City Trial x16, Beanstalk Park x8 + 1 |
| 48 | `CANNON` | `gryakucannon.c` | `GrYakuCannon_Create` | Machine Passage |
| 49, 50 | `PUSHOUTWALL`, `PUSHOUTWALLCTRL` | `gryakupushoutwall.c` (Ctrl assert in 50's tail-init) | `0x800ff50c`, `0x800ffd04` | Frozen Hillside x6 + 1 |
| 51 | `LIGHTTUNNEL` | `gryakulighttunnel.c` | `0x80100224` | Frozen Hillside |
| 52, 53 | `PILLAR`, `PILLARCTRL` | `gryakupillar.c` (kind asserts) | `0x801007fc`, `0x80100fb4` | Sky Sands x7 + 1 |
| 54 | `ANIMFLOOR` | `gryakuanimfloor.c` | `0x80102c40` | Magma Flows, Frozen Hillside x3 |
| 57, 58 | `LASERGATE`, `LASERGATECTRL` | `gryakulasergate.c` (kind asserts) | `0x80106144`, `0x801063bc` | Frozen Hillside x4 + 1 |
| 65 | `RAILFIRE` | - | `0x8010a5dc` | Rail Fire event |
| 66 | `SECRETCHAMBER` | - | `0x8010a8d4` | Secret Chamber event |
| 67 | `UFO` | - | `0x8010ab34` (spawn) | UFO event |
| 68 | `LIGHTHOUSE` | - | `Lighthouse_Create` | City Trial |
| 69 | `WHISPYWOODS` | `gryakuwhispywoods.c` | `whispyLogic` | City Trial |

The Ctrl kinds are the single instance that drives the rest of its file's kind through a `targetNum` list; RotJumpHill and Gondola follow that shape without a `...Ctrl` string of their own.

Unnamed kinds, and what is known of each:

| kind | known |
|---:|---|
| 18, 19 | String-less zone kinds between the catch-zone and recovery-zone code. 18: City Trial (`data_idx` 1), Beanstalk Park. 19: Fantasy Meadows x3, Magma Flows, Sky Sands, Checker Knights x2, Frozen Hillside x10 |
| 21 | Break kind with its own `coll_func` (`0x800fc474`), code between the RotJumpHill and InvisibleBall files. Checker Knights x2 |
| 23 | BreakHouse family (22's state table and `coll_func`). The Destruction Derby 1 rocks: `GrColosseum1` x2, read by CT cell 0x28 |
| 25..27 | BreakCoral descriptors no creator uses |
| 28 | BreakCoral family, same creator shape as the star pole. Celestial Valley x2 |
| 39 | BreakRock family. Frozen Hillside x4 |
| 55 | String-less, code after the AnimFloor file. Machine Passage x3, Beanstalk Park |
| 56 | `coll_func` `0x80106094`, code after `gryakubreakcommon.c`. Machine Passage x5 |
| 59, 60 | Descriptors between the BreakFloor and BreakFan files; no creator |
| 61 | Classic single-instance prop. City Trial x2 (`data_idx` 18..19), Magma Flows x3 |
| 62, 63 | `adhere_update_func` / `get_point_func` only; no `GrYaku_Create` caller |
| 64 | The Air Glider stadium's one yakumono, created by `grairglider.c` (`0x800eee70` -> `0x8010a450`) at a given translation |

**State-table lookup.** `YakumonoGObj_InitData` stores the descriptor's +0x00 into `ydata->state_table`, which points at an array of 16-byte state entries sitting immediately before the descriptor block. `Gr_StateChange` resolves the active entry from `common_state_table` when `state < common_state_num`, else as `state_table + (state - common_state_num)*16`; `InitData` leaves `common_state_num` at 0 and `common_state_table` NULL, so in practice it is `state_table + state*16`. The entry's four words are installed into `anim_callback`, `phys_callback`, `envcoll_callback` and `post_envcoll_callback`, and `on_damage_callback`, `off_damage_callback` and `trigger_callback` are cleared. Passive kinds (zones) have an all-zero table; active kinds hold real handlers - the cannon's table at `0x804a6430` is `{GrYakuCannon_State0 0x800fee40, GrYakuCannon_State1 0x800ff010, ...}`. Table size varies (0x10 for zones, 0x20 for the cannon), so the gap between block and table is not fixed.

### Common-kind create table (16 entries at 0x804a5ba8)

Declared in `yakumono.h` as `stc_yaku_common_create`. Each wrapper takes `(grobj, kind, entry->param)` and returns the new GObj, whose user data `grInitYakumono` stores into `GrObj.yaku`.

Eight pairs, one per generic base kind, wrappers at `0x800f9210` through `0x800f9cf8`. Within a pair the even entry calls `GrYakuFlags_SetBase` (`0x800f9188`, clears flag bit 7) and the odd calls `GrYakuFlags_SetCtrl` (`0x800f91c8`, sets it); both share a tail-init (`GrYaku_BaseKind0_TailInit` through `..KindN..`) and both call `GrYaku_Create_Generic`. So `entries[].kind` is a common kind 0..15, below `YAKUKIND_COMMONTERMINATE`, with bit 0 acting as the "ctrl" flag and bits 1..3 selecting the base kind.

Tail-inits differ structurally only: pairs 0..3 allocate kind-specific collision data through the zone finder `zz_800d79c0_`, pairs 4..5 through `zz_800d7a40_` (same role plus an extra `10`), pairs 6..7 skip the alloc entirely and just transition state. All eight end with a `Gr_StateChange` whose initial-state floats come from SDA2 at `0x805df868..0x805df8a4`, one pair per base kind.

**The generic path is kind-agnostic in code.** No `GrYaku_BaseKindN_TailInit` or `DescFunc` references a source file or a `YakuKind`. Model, joint and collision shape - and thus the concrete kind - come from the `Yakumono.dat` entry data at runtime, so which object each common kind becomes is not derivable from the binary. The Ctrl-paired kinds (RotJumpHill, RisingCube, Gondola, PushOutWall, Pillar, LaserGate) are spawned through the per-instance descriptor path instead, where real handlers exist.

The "ctrl" marker is bit 7 of `ydata->flags`, which per-kind handlers branch on directly (`GrYaku_BaseKind0_DescFunc` tests the sign of the flag byte before doing its kind-specific work). The same bit doubles as the per-frame matrix-rebuild gate in `YakumonoGObj_Proc4`.

### Per-grkind hook table (0x804a322c)

28 entries indexed by the physical `GroundKind` - the ground geometry file that loads, not the `StageKind` and not the `AirRideCourse` menu index. The three enums share the 0..8 range with different orderings: Machine Passage is `AIRRIDE_MACHINE_PASSAGE = 6` but `GR_MACHINE2 = 5`. Each entry points at a per-grkind block whose +0x04 slot is the optional init hook; block size and layout vary and only that slot matters for dispatch.

15 GroundKinds have a real hook (0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 15, 19, 21, 26, 27), one has a 4-byte `blr` stub that is functionally NULL (17, `GrColosseum5` / Kirby Melee 2), and 12 are NULL and rely entirely on the generic walker (6, 11, 12, 13, 14, 16, 18, 20, 22, 23, 24, 25). There is no GroundKind >= 28; City Trial stadiums without their own ground reuse one of these physical grounds, or skip the yakumono path entirely.

The hooks worth naming:

| gr_kind | Ground | Hook | Notes |
|---:|---|---|---|
| 0 | `GrPlants1` (Fantasy Meadows) | `0x8010e09c` | |
| 1 | `GrHeat2` (Magma Flows) | `0x8010e1c8` | source `grheat2.c` |
| 2 | `GrDesert1` (Sky Sands) | `0x8010e378` | source `grdesert1.c` |
| 3 | `GrCheck2` (Checker Knights) | `0x8010e5dc` | |
| 4 | `GrValley2` (Celestial Valley) | `0x8010e87c` | source `grvalley2.c` |
| 5 | `GrMachine2` (Machine Passage) | `0x8010ea40` | source `grmachine2.c`; calls `GrYakuCannon_Create` with `data_idx = 1` plus four other creators. One of only two cannon call sites in the game |
| 7 | `GrSky2` (Beanstalk Park) | `0x8010ec90` | source `grsky2.c` |
| 8 | `GrIce1` (Frozen Hillside) | `0x8010edf8` | source `grice1.c` |
| 9 | `GrCity1` (City Trial) | `grDataCity1_CreateYakumono` `0x8010f268` | 31 explicit creators; no cannon |
| 10 | `GrZeroyon1` | `0x8010f880` | drag-race ground; 11..13 are NULL |
| 15 | `GrColosseum1` | `0x8010fa0c` | |
| 19 | `GrJump2` | real | |
| 21 | `GrDedede1` | real | King Dedede arena |
| 26 | `GrSimple` | `0x8010fbd4` | system ground |
| 27 | `GrSimple2` | `grDataSingleRace4_CreateYakumono` `0x8010ff08` | 0x1dc bytes; the second cannon call site, `data_idx = 18` |

## Audio Block

`YakumonoData` carries an `fgm` (field SFX/effect-id manager) substruct overlaying +0x118..+0x124 - the region `GrYaku_InitAudio` fills from the `param+0x14` audio descriptor: `fgm.idData`, `fgm.idDataNum`, the `Map_AllocAudioTrack` handle and the `Map_AllocAudioEmitter(1)` handle. Only the BREAK and BREAK-HP-COLL families read `fgm`; their `0 <= fgmId && fgmId < gyp->fgm.idDataNum` asserts (strings at `0x804a6fc4` and `0x804a7130`) are what names the field.

Past +0x130 the struct is a per-kind overlay - the break families' per-prop arrays and audio handles, or a kind's local context union (`gyp->lc.gondola.userGObj`, `gyp->lc.cannon.userInfo[i].gobj`, `gyp->lc.breakFloor.currentAnim`). The struct is 0x190 bytes; independently of the break overlays, `GrYakuCannon_TailInit` (`0x800fed48`) zeros a contiguous run of words from +0x130 through +0x188.

## Mod Hook Points

- **Observing spawns**: hook `GrYaku_Create`. The 70-descriptor table is read-only ROM data, so replacing it would need a CODEPATCH pointing at a copy - rarely worth it.
- **Observing or gating damage**: hook `YakumonoGObj_Proc9_HitColl` (the whole HitColl pipeline) or a kind's `on_damage_callback`. A "no breakables" rule goes here.
- **Counting breaks**: the vanilla path is already wired. Every break-family drop handler and `hitWeakObject` calls `YakumonoGObj_IncrementBreakCount`, which bumps `PlayerStats.yakumono_break[kind - 0x15]` - the array the checklist's "break N of object X" cells read. `Ply_GetYakumonoBreakCount` (`0x8022fccc`) reads it back and returns 0 outside kinds 0x15..0x28.
- **Goal hooks**: WhispyWoods (`YAKUKIND_WHISPYWOODS`, 69) and the Lighthouse (`YAKUKIND_LIGHTHOUSE`, 68) are both addressable through their `on_damage_callback` or their terminal state.
- **Enumerating live props**: walk the `GAMEPLINK_YAKUMONO` list, or walk `Gr_GetCollRecords()` and filter on a non-NULL `yaku_gobj` when you want individual props rather than parent GObjs. `GrObj.yaku` is NULL in City Trial and `GrObj.yaku_num` counts GObjs (~31), not props (hundreds).

## Cross-Stage Spawning: the Cannon

Spawning a yakumono kind in a stage that never ships it works at the framework level but not at the asset level.

`GrYaku_Create(YAKUKIND_CANNON, data_idx)` plus `GrYakuCannon_TailInit(gobj)` from `On3DLoadEnd` in City Trial - long after `grInitYakumono` finished - runs without asserting and increments `GrObj.yaku_num`, so the GObj is fully wired and registered. But with a zeroed param block the pipeline silently skips graphical setup: no JObj, an all-zero matrix, no audio handles. HurtData is created, `FinalSetup` runs, the scale and axis vectors are seeded, and the state callbacks are auto-installed from the per-kind state table. The result is a **ghost yakumono**: collidable and state-machine-driven, but invisible and immobile. The seven procs are added unconditionally and tick regardless - `YakumonoGObj_Think` and the HitColl pipeline read HurtData, not the JObj.

That is not actually a broken spawn, because **the vanilla cannon looks the same**. Machine Passage's cannon param has both framework gates at zero, and a vanilla cannon's ydata also has a zero matrix and a NULL model JObj; only HurtData, `anim_callback` and the audio handles are populated. The cannon yakumono contributes collision, state machine, audio emitter and eject physics - the visible mesh is part of `GrMachine2Model.dat`, loaded with the stage's main scene graph.

The binding is by joint reference. The cannon's param block (at least 0x80 bytes) is a metadata pointer at +0x00, the two intentionally-zero framework gates, then five repeating `(trigger_desc, physics)` pairs at 0x18 stride - one per barrel. Each 0x20-byte `trigger_desc` holds `[self-back-ptr, 1, 0, 1, angle, 0, packed_joint_ref, 0]` where `packed_joint_ref = 0x000f00XX` and `XX` is the stage-joint index of that barrel (5, 6 and 8 in Machine Passage); each 0x20-byte physics block holds `[count, kind, force, factor, scale, angle, factor, value]`. The cannon framework reads joint positions from the live scene graph and overlays HitColl regions and launch impulse there, so there is no separate cannon model asset - only the anchor joints have to exist.

What is missing in City Trial is therefore exactly two things: the visible mesh and the anchor joints. Everything else (Create, procs, state machine, HurtData, `anim_callback`) is stage-independent and already works.

**The full archives do not fit.** `Archive_LoadFile` on `GrMachine2Model.dat` (~1.6MB) plus `GrMachine2.dat` (~207KB) does load and resolve their publics, but leaves heap 1 with roughly 30 bytes free, so the next allocation asserts in `initialize.c`. `GrMachine2Model.dat` exposes `grModelMachine2` and `grModelMotionMachine2`; `GrMachine2.dat` exposes `grDataMachine2` plus one extern, `GrdMachine2_CannonSAN1_ACTION_Cannon1_animjoint`, which `Archive_LoadFile` does not resolve against globally-loaded archives (`grLoadStageArchive` does that post-parse resolution itself). `grModelMachine2` reads as an array of 3 JOBJSet pointers, NULL-terminated at index 3, with anim/matanim pointers at +0x10..+0x14; each JOBJSet is `(JObjDesc *jobj, int n_joints, int n_dobjs, int n_mobjs)` - **not** the 4-pointer typedef in `obj.h`, so `JObj_LoadSet_SetPri` must be called with `is_add_anim = 0` to avoid dereferencing the counts. Index 0 is the 122-joint main stage tree, index 1 a smaller tree (likely the cannon-bearing one), index 2 the lights/cameras region.

Getting a visible cannon into City Trial therefore means either authoring a small custom mesh in mod memory and passing it via `param[+0x04]` so `GrYaku_AllocJObj` does the work, or building a stripped cannon-only archive offline that fits the heap. Extracting the subtree at runtime and freeing the originals would recover the most memory but requires pinning the cannon subtree's exact reachability set.
