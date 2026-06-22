# Yakumono System

## Overview

"Yakumono" (役物) — Japanese for "stage prop" or "stage gimmick" — is the framework Kirby Air Ride uses for **interactive stage objects**. Yakumono are stage-bound (not enemy or item), often have hurt/hit collision, may animate, and may emit drops or spawn child objects. Examples in the shipped game span a wide range:

- Destructible scenery (volcano walls, houses, breakable rocks/coral/icicles, ice columns, fans)
- Activatable hazards (laser gates, push-out walls, rising cubes, gondolas, cannons, light tunnels)
- Damage/healing zones (catch zone, recovery zone, down-force zone)
- Boss-like fixed actors (WhispyWoods, Lighthouse)
- Pillars and event pillars

This system is distinct from the **enemy** system (Tac, Dyna Blade, Meteor — see `enemy-spawn-system.md`), the **event-actor** system (City Trial events — see `custom-events.md`), and **items** (`docs/event-source-drops.md`). All four register on different `p_link` values and have different lifecycles.

## High-level architecture

```
Stage data file (e.g. GrCity1.dat)
    └── GrData struct
        └── grdata->yakumono  (offset +0x40)  — YakumonoTable (per-stage manifest)
            ├── data_array[]    — per-instance param blocks (variable)
            └── entries[]       — kind-tagged spawn entries (kind 0..15)

grLoadStage  ──►  grInitYakumono                                   (priority init)
                      │
                      ├─► (1) if PTR_PTR_804a322c[gr_kind].fn_at_4 != NULL:
                      │         call hook(grobj)
                      │           e.g. grDataCity1_CreateYakumono
                      │             └─► <per-instance creator helper>(grobj, data_idx)
                      │                   └─► GrYaku_Create(desc_id, data_idx)
                      │                          ├─► GObj_Create(15, GAMEPLINK_YAKUMONO=8, 0)
                      │                          ├─► HSD_ObjAlloc(yakumono_class)
                      │                          ├─► GObj_AddUserData(gobj, 14, destroy_cb, ydata)
                      │                          ├─► GrYaku_InitData(gobj, desc_id, data_ptr)
                      │                          ├─► <init pipeline: model, hurt, audio, anim>
                      │                          └─► GObj_AddProc × 7 (priorities 1,4,5,6,7,9,10)
                      │
                      ├─► (2) HSD_MemAlloc the per-stage YakumonoData* index array
                      │
                      ├─► (3) for each entry in grdata->yakumono->entries[]:
                      │         grYakuFuncTable[entry.kind](grobj, entry.kind, entry.param)
                      │           └─► GrYaku_Create_Generic (GrYaku_Create variant
                      │                              reading from r13[0x5e4] = Yakumono.dat)
                      │                + per-pair bit-flag init + per-pair tail-init
                      │
                      └─► (4) GrYakuCommon_SelectRandomGroup  — common-group random subset selector
```

The per-grkind hook and the `entries[]` walker **both run** when both are present — they are not alternatives. `grInitYakumono` calls the hook unconditionally if non-NULL, then always allocates the index array and walks `entries[]`. City Trial happens to ship with an `entries[]` array containing the generic-kind entries that `grDataCity1_CreateYakumono` does *not* spawn (downforce/catch/recovery zones, lasergates, pillars, etc.), so both paths contribute. Two dispatch paths still exist functionally:

1. **Per-grkind explicit list** (e.g. `grDataCity1_CreateYakumono`) — one named helper per `data_idx`, each hardcodes a `desc_id` from the 70-entry table at `0x804a5be8` and supplies a kind-specific tail-init. Reads its param from `grdata->yakumono->data_array[]`.
2. **Generic kind-table dispatch** — `grInitYakumono` walks `entries[]` and calls `grYakuFuncTable[entry.kind]` (16-entry function table at `0x804a5ba8`). Wrappers call `GrYaku_Create_Generic`, a `GrYaku_Create` variant that reads its param array from `r13[0x5e4]` (the loaded `Yakumono.dat` archive), not from `grdata->yakumono->data_array[]`.

Both paths terminate in a `GrYaku_Create` shape and produce a fully-wired GObj. They use **different `desc_id` ranges** of the 70-entry descriptor table:

- The generic walker uses the **paired generic descriptors at indices 0..15** (eight kinds × two variants).
- Per-instance creators use the **per-instance descriptors at indices 16..69**.

## Source files (decompiled framework)

The framework lives in a family of `gryaku*.c` files identifiable from descriptor assert strings:

| File | Role |
|---|---|
| `gryaku.c` | Core framework: `GrYaku_Create`, `GrYaku_InitData`, the 7 procs, `GObj` wiring |
| `gryakuanim.c` | Animation helpers (`Gr_StateChange`, `Gr_AddAnim`, `Gr_RemoveAnim`) |
| `gryakueffect.c` | Effect spawning from anim event lists |
| `gryakuaudio.c` | Audio emitter / track allocation |
| `gryakulib.c` | Utility (`grYakuCheckGObjYakumono`, scaling helpers) |
| `gryakucommon.c` | Shared "common" group helpers (`GrYakuCommon_Group_Max`, random sets) |

Each per-kind file then implements one or more YakuKinds:

| File | Kinds (from asserts) |
|---|---|
| `gryakudownforcezone.c` | `Gr_YakuKind_DownForceZone` |
| `gryakucatchzone.c` | `Gr_YakuKind_CatchZone` |
| `gryakurecoveryzone.c` | `Gr_YakuKind_RecoveryZone` |
| `gryakurotjumphill.c` | (rotating jump hill) |
| `gryakuinvisibleball.c` | (invisible ball) |
| `gryakurisingcube.c` | `Gr_YakuKind_RisingCube`, `Gr_YakuKind_RisingCubeCtrl` |
| `gryakugondola.c` | (gondola — `gyp->lc.gondola.userGObj`) |
| `gryakucannon.c` | (cannon — `gyp->lc.cannon.userInfo[i].gobj`) |
| `gryakupushoutwall.c` | `Gr_YakuKind_PushOutWall`, `Gr_YakuKind_PushOutWallCtrl` |
| `gryakulighttunnel.c` | (light tunnel) |
| `gryakupillar.c` | `Gr_YakuKind_Pillar`, `Gr_YakuKind_PillarCtrl` |
| `gryakubreakrock.c` | (break rock — volcano walls, event pillars) |
| `gryakubreakhouse.c` | (break house) |
| `gryakuanimfloor.c` | (animated floor) |
| `gryakubreakcoral.c` | (break coral / "BigStar" = star pole) |
| `gryakubreakicicle.c` | (break icicle) |
| `gryakubreakcommon.c` | Shared break-helper (ring damage, range checks) |
| `gryakulasergate.c` | `Gr_YakuKind_LaserGate`, `Gr_YakuKind_LaserGateCtrl` |
| `gryakubreakfloor.c` | (break floor — multi-stage cracking) |
| `gryakubreakfan.c` | (break fan) |
| `gryakubreakcoll.c` | (break-collision shared base) |
| `gryakubreakhpcoll.c` | `Gr_YakuKind_BreakHpCollDoor` … `Gr_YakuKind_BreakHpCollHouse` (contiguous range — door/wall/.../house variants) |
| `gryakuwhispywoods.c` | WhispyWoods boss tree |
| (named separately) | `Lighthouse_Create` / `Lighthouse_Init` (lighthouse) |

Sentinel: `Gr_YakuKind_CommonTerminate` bounds the "common" range. Asserts use `kind < Gr_YakuKind_CommonTerminate` as a range check before passing to `grYakuFuncTable[]`.

## Per-stage manifest

`grdata->yakumono` (at GrData+0x40) is a pointer to:

```c
struct YakumonoTable
{
    void   **data_array;    // 0x00 — array of per-instance param-block pointers
    int      data_count;    // 0x04 — entries in data_array
    int      x08, x0c;      // 0x08, 0x0c — unknown
    struct
    {
        int   kind;         // 0x00 — yaku spawn-kind, 0..15 (small enum, distinct from YakuKind)
        void *param;        // 0x04 — kind-specific param (e.g. data_idx ref, position)
        int   x08;          // 0x08 — unknown
    } *entries;             // 0x10 — array used by generic dispatch
    int      entry_count;   // 0x14
};
```

- **`data_array[data_idx]`** — read by `GrYaku_Create` to get the per-instance param block (`data_ptr`).
- **`entries[]`** — read by the generic dispatcher `grInitYakumono`; each entry's `kind` is dispatched through `grYakuFuncTable`.

The two arrays are independent: stages may populate one, the other, or both. City Trial populates both but bypasses `entries[]`-based dispatch via a per-grkind hook (see below).

### Per-grkind hook table

`grInitYakumono` checks `(&PTR_PTR_804a322c)[grobj->gr_kind].fn_at_4` and calls it before iterating `entries[]`. For City Trial (GroundKind 9 / `GrCity1`) this hook is `grDataCity1_CreateYakumono` (0x8010f268), which manually invokes 31 per-instance creators (data_idx 0..30), each hardcoding its own descriptor id. This bypass keeps the City Trial spawn order deterministic and well-known, instead of relying on the data file's `entries[]` order.

The hook table at `0x804a322c` is indexed by `GroundKind`. Entries that are NULL fall through to the generic `entries[]` walk.

## Lifecycle: `GrYaku_Create`

```c
void GrYaku_Create(int desc_id, int data_idx);
```

Symbol: `0x800f446c`. Two arguments:

| Arg | Source | Use |
|---|---|---|
| `desc_id` | hardcoded by per-instance helper | index into 70-entry `stc_yaku_descs[]` (descriptor table at `0x804a5be8`) |
| `data_idx` | passed by stage iterator | index into `grdata->yakumono->data_array` (per-instance params) |

Asserts: `0 <= data_idx < data_count` and `data_array[data_idx] != NULL`.

Steps:
1. **Allocate GObj**: `GObj_Create(0x0F, GAMEPLINK_YAKUMONO=8, 0)`. Yakumono GObjs always have `gobj->kind = 15`.
2. **Allocate user data**: `HSD_ObjAlloc(yakumono_class @ 0x80557584)` → `YakumonoData` (size from class struct).
3. **Bind**: `GObj_AddUserData(gobj, GUDATA_YAKUMONO=14, destroy_cb=GrYaku_DestroyCallback, ydata)`.
4. **Increment counter**: `stc_grobj->yakumono_count++` (at `+0x6fc`).
5. **`GrYaku_InitData(gobj, desc_id, data_ptr)`** — see below.
6. **Init pipeline** (each in its own helper):
   - `GrYaku_AllocMiscTable` — alloc misc table (`ydata->[+0x10c]`)
   - `GrYaku_InitLighting` — sky/lighting hookup (calls `zz_800dcab8_` with stage's lights pointer)
   - `GrYaku_NoOp` — no-op stub (`blr` only)
   - `GrYaku_AllocJObj` — JObj allocator (sets `ydata->[+0x64]` JObj root); gated on `param+0x04` (the JObj data block)
   - `ydata->[+0x70] = 0` — null the `xform_jobj` pointer (set later only for movable yakumono; so `GrYaku_InitMatrix` is a no-op at Create and stays one for static/break props)
   - `GrYaku_InitMatrix` — JObj matrix setup (reads `+0x70`; no-op while it is NULL)
   - `GrYaku_AttachModel` — model attach (uses `grdata->lights` for lighting); gated on `param+0x0c` (the model data block)
   - `GrYaku_InitAudio` — fgm/audio init; gated on `param+0x14` (the audio descriptor); fills `ydata->[+0x118..0x124]`
   - `GrYaku_AttachAnim` — anim attach; re-reads `param+0x04` (no separate field) and gates on `jobj_data[0][+0x10]`
   - `GrYaku_InitHurtData` — HurtData create + region init (`HurtData_Create(gobj, 6, 2, regionCount, 0)` → `ydata->[+0xec]`)
7. **Add 7 procs** (see proc table below).
8. `GrYaku_FinalSetup` — final touches (zero a flag bit, init bbox at `+0xe0..+0xe8`).

## Lifecycle: `GrYaku_InitData`

```c
void GrYaku_InitData(GOBJ *gobj, int desc_id, void *data_ptr);
```

Symbol: `0x800f4d50`. Stores the args into `YakumonoData`, zeros a swath of fields, and looks up the kind descriptor.

Key writes:
- `ydata->[+0x00] = gobj`              (back-reference)
- `ydata->[+0x04] = desc_id`           (descriptor index)
- `ydata->[+0x08] = data_ptr`          (per-instance param block, "`gyp->param`" in source)
- Clears bits 7, 6, 4, 3 of `ydata->[+0x12c]` (`flags` byte at +300)
- `ydata->[+0xa4] = 1.0f`              (scale = `Gr_DefaultScale`)
- `ydata->[+0xb0] = 5`                 (a pre-set constant)
- Initializes orientation vectors at `+0x88` (right) and `+0x94` (up) from constant `(0,0,1)`
- Zeros all 7 per-type proc slots (+0xf0..+0x108)
- `ydata->[+0x74] = -1` (state)
- Looks up `stc_yaku_descs[desc_id]` (with `desc_id < 70` and non-NULL guard) and stores `desc->[+0x0]` (the descriptor's "back-pointer") into `ydata->[+0x84]` — this is the per-kind **state-table base**, indexed by state in `Gr_StateChange` (entry = base + state·16). See "How `yd->state_table` (+0x84) is used" below.

## YakumonoData layout

YakumonoData is the single user-data slot stored at `gobj->user_data[0]` (offset +0x2c on GObj). All per-frame procs read it as `ydata = *(int *)(gobj + 0x2c)`. Known offsets:

| Offset | Type | Meaning |
|---:|---|---|
| 0x00 | `GOBJ *` | Back-reference to owning GObj |
| 0x04 | `int` | `desc_id` — descriptor index (passed to Create) |
| 0x08 | `void *` | `data_ptr` — per-instance param block (from `data_array[data_idx]`); `gyp->param` in source |
| 0x10 | — | (unread in lifecycle) |
| 0x1c | `Vec3` | Position (`+0x1c`..`+0x28`) — single-instance world pos, written by `GrYaku_AttachModel`. **0,0,0 for the multi-instance break families** (their per-prop positions live in the `+0x130` child records, not here). |
| 0x40 | `Mat3x4?` | Local transform (used by `+0x4c` matrix copy) |
| 0x64 | `JObj *` | `model_jobj` — model JObj root, allocated by `GrYaku_AllocJObj`, positioned by `GrYaku_AttachModel`. **NULL for break families** (their geometry lives in the stage scene graph). |
| 0x70 | `JObj *` | `xform_jobj` — transform JObj that `GrYaku_InitMatrix` (`0x800f73fc`) transforms; its world matrix (`JObj+0x44`) is copied into the GObj render object. Zeroed at Create, set later only for **movable** yakumono (cannon/gondola/rising-cube/…). **Stays NULL for static + break-family props.** |
| 0x74 | `int` | **`state`** — current state-machine state (read by `GrYakumono_GetState`); `-1` initially |
| 0x78 | `int` | Previous anim id (?) |
| 0x7c | `int` | Previous joint id (?) |
| 0x80 | `int` | Anim flags (?) |
| 0x84 | `void *` | **State-table base** — `desc->[+0x0]` (the descriptor "back-pointer"). Array of 16-byte state entries; `Gr_StateChange` reads `base + state·16` and installs the state's handler into `proc1` (+0xF0). All-zero for passive kinds (zones); real handlers for active kinds (cannon: state0=`GrYakuCannon_State0`, state1=`GrYakuCannon_State1`) |
| 0x88 | `Vec3` | Right/forward axis (init `(0,0,1)`) |
| 0x94 | `Vec3` | Up axis (init `(0,0,1)`) |
| 0xA0 | `int` | (set 0 in InitData) |
| 0xA4 | `float` | **`scale`** — `Gr_DefaultScale = 1.0f`; asserted equal to default in many checks |
| 0xAC | `float` | (init `1.0f`) |
| 0xB0 | `int` | (init `5`) |
| 0xB4 | `int` | (init `0`) |
| 0xB8 | `int` | (init `0`) |
| 0xE0 | `Vec3` | BBox / center (init from constants in `GrYaku_FinalSetup`) |
| 0xEC | `HurtData *` | **HurtData pointer** — read by `GrYaku_GetHurtData` (`0x800f8248`); same offset as `EnemyData.xec`. |
| 0xF0 | `void(*)(GOBJ*)` | **proc1** callback — called from `GrYakumono_Think` (priority 1) |
| 0xF4 | `void(*)(GOBJ*)` | **proc2** callback — called from priority-4 proc |
| 0xF8 | `void(*)(GOBJ*)` | **proc3** callback — called from priority-5 proc |
| 0xFC | `void(*)(GOBJ*)` | **proc4** callback — called from priority-6 proc |
| 0x100 | `void(*)(GOBJ*, void*)` | **damage-on** callback — called from priority-10 proc when damage occurred this frame |
| 0x104 | `void(*)(GOBJ*)` | **damage-off** callback — called from priority-10 proc when no damage |
| 0x108 | `void(*)(GOBJ*)` | **proc5** callback — called from priority-7 proc, before `HurtData_UpdatePerFrame` |
| 0x10C | `int` | Misc table (alloc'd by `GrYaku_AllocMiscTable`) |
| 0x110 | `int` | (zeroed in init) |
| 0x114 | `int` | (zeroed in init) |
| 0x118 | `void *` | **`gyp->fgm.idData`** — FGM (field SFX/effect-id) data ptr. Filled by `GrYaku_InitAudio` from the param+0x14 audio descriptor `[+0x00]` |
| 0x11C | `int` | **`gyp->fgm.idDataNum`** — FGM id count (audio descriptor `[+0x04]`). The break families' `0 <= fgmId && fgmId < gyp->fgm.idDataNum` assert reads this offset |
| 0x120 | `AudioTrack` | Allocated audio track (`Map_AllocAudioTrack`, from audio descriptor `[+0x08]`) |
| 0x124 | `AudioEmitter` | Allocated audio emitter (`Map_AllocAudioEmitter(1)`) |
| 0x12C | `byte` | Flag byte (bits 3, 4, 6, 7 cleared on init) |
| 0x130 | `void *` | Break families: **per-prop child array** — `count*4` bytes of `0x98`-byte scene-instance record pointers (one per placed prop; each carries the prop's own JObj/world-matrix/hurt region; `record+0x90` = this parent GObj). BREAK-coll families overlay it as the per-region audio-handle array. (Also used by other post-init helpers, e.g. `zz_800fe60c_`.) |
| 0x134 | `int` | Break families: **child-array count** (trees/coral; the strong/house families keep the count at `+0x140` and use `+0x134/+0x138/+0x13c` for parallel arrays). |
| 0x138 | `int` | Set by `GrYaku_BaseKind0_TailInit` to `ydata->[+0x0]` (gobj backref into model node) |

Source-level field names (from asserts) map onto these:
- `gyp->kind` — the YakuKind enum; identification varies but typically derived from `desc_id` or stored in `data_ptr->[+0x0]` (which is the entry kind in `entries[].kind`).
- `gyp->scale` — `+0xA4` (compared to `Gr_DefaultScale`).
- `gyp->lc.gondola.userGObj`, `gyp->lc.cannon.userInfo[i].gobj`, `gyp->lc.breakFloor.currentAnim` — kind-specific union "local context" fields, located in the high half of YakumonoData (probably at the misc table `+0x10C` or in trailing class storage).
- `gyp->fgm.idDataNum` — Field/Game manager (FX+SFX id table) embedded near `+0x110`.

## The 7 procs

`GrYaku_Create` adds these procs to the GObj. Each priority handles one phase of the per-frame pipeline. Per-type behavior is plugged in via the `+0xF0..+0x108` callback slots — kind handlers populate them after `GrYaku_Create` returns.

| Pri | Symbol | Address | What it does |
|---:|---|---|---|
| 1 | `GrYakumono_Think` | `0x800f5284` | Reset HurtData damage flags; advance HurtData state; run state-machine + anim updater (`GrYakumono_StepStateAndAnim` → `GrYakumono_StepStateMachine` + `GrYakumono_StepAnimEvents`); call `proc1` (`+0xF0`) if non-NULL |
| 4 | `GrYakumono_Proc4` | `0x800f52e8` | Call `proc2` (`+0xF4`); if `flag bit 7` set, run `GrYaku_InitMatrix` (matrix recompute); call `zz_800f62c4_` |
| 5 | `GrYakumono_Proc5` | `0x800f5340` | Call `proc3` (`+0xF8`) if non-NULL — pure dispatch |
| 6 | `GrYakumono_Proc6` | `0x800f5374` | Call `proc4` (`+0xFC`) if non-NULL — pure dispatch |
| 7 | `GrYakumono_Proc7` | `0x800f53a8` | Call `proc5` (`+0x108`); then `HurtData_UpdatePerFrame(ydata->scale, hurtdata, NULL, 2, NULL)` |
| 9 | `GrYakumono_Proc9_HitColl` | `0x800f53fc` | **HitColl pipeline:** `HitColl_Init(hurtdata)` → 3 stage-side hooks (`zz_800f85a0_`, `zz_800f85fc_`, `zz_800f8658_`) → `HitColl_ActOnCollision(hurtdata)` → post-hook `zz_800f86b4_`. This is what `gate_*.c` files would have to hook to filter damage application. |
| 10 | `GrYakumono_Proc10` | `0x800f5454` | Damage-trigger dispatch. Reads two **float** damage values: primary at `hurtdata+0x24`, secondary at `+0x28`. **Gates on `hurtdata+0x24 != 0.0`**: if damage occurred, run audio (`zz_800f875c_`, passed the `+0x28` value) then call `damage-on` proc (`+0x100`, passed `hurtdata+0x1c`) and a post-hook `zz_800f8780_`. If `+0x24 == 0.0` (no damage) and `hurtdata+0x64 != 0`, call `damage-off` proc (`+0x104`). A trailing guard replays audio if either damage value is non-zero. |

The proc-priority numbers correspond to the standard yakumono pipeline phase:
- 1 = update / state machine
- 4 = pre-physics adjust
- 5, 6 = mid-pipeline kind hooks (kind-specific physics, anim ticks, etc.)
- 7 = HurtData advance
- 9 = HitColl resolution (stage-side handler — distinct from `priority 8` reserved for the empty `blr` stub seen in some other systems, and `priority 10` for damage-effect callbacks)
- 10 = post-damage callbacks

This places yakumono between enemy actors and items in the per-frame ordering. Priorities are uniform across all yakumono kinds — only the callbacks differ.

## Other framework helpers

| Symbol | Address | Purpose |
|---|---|---|
| `GrYakumono_GetState(GOBJ *)` | `0x800f7ab8` | Returns `ydata->state` (+0x74) iff `gobj->kind == 15`, else `-1` |
| `GrYaku_GetHurtData(GOBJ *)` | `0x800f8248` | Returns `ydata->[+0xEC]` |
| `Gr_StateChange(YakumonoData *, state, anim, joint, flags, start_frame, anim_rate, blend_rate)` | (linked) | Advances state + plays anim; commonly called from per-kind handlers and inside Create's bottom helpers |
| `Gr_AddAnim` / `Gr_RemoveAnim` | `0x800f5ce8` / `0x800f5f3c` | Animation chain helpers (gryakuanim.c) |
| `grGetYakumonoposNum` | `0x800d1434` | Returns count of position records in stage's yakumono-position block (`grdata->yakumono_pos` at **GrData+0x20** → `[+0x2C]` → `[+0x8]`); 0 if absent. Note: this is GrData+0x20, **not** the `pos_data` field at GrData+0x18. |
| `loadYakumonoLocations?` | `0x800d145c` | Loads position records from stage data |
| `Yakumono_Preload` | `0x800f82ec` | Per-stage GX preload (calls `stGetStageKind_3Cyakumono?` to read stage offset 0x3C, then preloads via `0x80072c90`) |
| `grLoadYakumono` | `0x800f440c` | Loads `YkCommon.dat` and `Yakumono.dat` archives if not already loaded |
| `grLoadYakumono_Common` / `_Main` | `0x800f8254` / `0x800f82a0` | Backing loaders — write into `r13[0x5e0]` / `r13[0x5e4]` |
| `grYakuFuncTable[]` | (in `gryaku.c`) | Per-kind function table; entries hold `coll_func`, `adhere_update_func`, `get_point_func` (referenced by asserts in `gryaku.c`) |

The dispatch table at `0x804a5ba8` (16 entries) is a separate "Create wrapper" table indexed by `entries[].kind` (small 0..15 enum). Each entry calls `GrYaku_Create_Generic` (a `GrYaku_Create` variant that reads its data array from `r13[0x5e4]` instead of `grdata->yakumono->data_array`) plus a kind-specific bit-flag init (`GrYakuFlags_SetBase`, `GrYakuFlags_SetCtrl`, etc.) and a shared post-init `GrYaku_BaseKind0_TailInit` that snaps to a position from `grdata`.

## Per-instance creators

There are ~45 per-instance creator functions (xrefs to `GrYaku_Create`), each a thin wrapper that:
1. Hardcodes `desc_id` in `r3` (literal `li r3, N`)
2. Forwards the caller's `data_idx` in `r4`
3. Calls `GrYaku_Create`
4. Calls a kind-specific tail-init that populates the `+0xF0..+0x108` callback slots, attaches per-kind state, and may issue a `Gr_StateChange` to enter the initial state.

Three of these are named or identified:

| Symbol | Address | desc_id | Notes |
|---|---|---:|---|
| `GrYakuCannon_Create` | `0x800fed20` | 48 (0x30) | Then calls `GrYakuCannon_TailInit` (`0x800fed48`) — sets up cannon state and zeros `userInfo[]`. Only called from two sites: gr_kind 5's hook (Machine Passage, `data_idx=1`) and `grDataSingleRace4_CreateYakumono` (`data_idx=18`). |
| `Lighthouse_Create` | `0x8010d228` | 68 (0x44) | Then calls `Lighthouse_Init` (`0x8010d240`) — the Lighthouse param uses the `lighthouse` arm of the `YakumonoParam` union |
| `whispyLogic` | `0x8010db64` | 69 (0x45) | WhispyWoods boss tree; the highest descriptor id (last in the 70-entry table) |

**Caveat on `GrYakuCannon_Create`'s calling convention.** The function's first instruction is `li r3, 48`, which overwrites whatever the caller passed in `r3` — so the cannon creator does **not** take `data_idx` in `r3`. It takes `data_idx` in **`r4`** (forwarded to `GrYaku_Create`). Both vanilla call sites use `mr r3, r31; li r4, N; bl GrYakuCannon_Create` where r31 holds a grobj (which `GrYaku_Create` ignores).

### City Trial spawn manifest

`grDataCity1_CreateYakumono` (`0x8010f268`) makes 31 calls (data_idx 0..30) and CT's `yakumono->entries[]` is empty (`entry_count = 0`), so the generic walker contributes nothing — CT creates exactly 31 yakumono **GObjs**. Note this is **31 GObjs, not 31 props**: the break families (trees/rocks/houses/holes/coral) are *multi-instance* — each creator call fans out to many placed sub-instances (one GObj manages a child array at `YakumonoData+0x130`), so the actual visible/collidable prop count is in the hundreds. See "Instance model per CT yakumono type" below for the per-type breakdown (which of the 14 descriptors are multi-instance, single-prop, or pool-backed singles), and "World positioning → Multi-instance break families" for the pool internals. **`data_array` actually has 33 slots, however** (`data_count = 33`), so slots **31 and 32 are spare** — they're allocated but no vanilla creator references them. Mod code can repoint them to a custom `YakumonoParam` block and spawn a 32nd or 33rd yakumono (the cannon, for example) without disturbing any vanilla slot. The hardcoded descriptor ids:

| data_idx | helper | desc_id | source file / YakuKind | object (see inventory below) |
|---:|---|---:|---|---|
| 0 | `0x800fa2a0` | 17 | `gryakucatchzone.c` (CatchZone) | catch zone (passive) |
| 1 | `0x800fa610` | 18 | RecoveryZone (passive) | recovery zone (passive) |
| 2..17 | `0x800fe5d4` | 46 | `gryakugondola.c` (Gondola) ×16 | gondola / cable-car loop (animated, not breakable) |
| 18..19 | `0x80109db4` | 61 | whispy-TU decorative ×2 | small decorative props (animated) |
| 20 | `Lighthouse_Create` | 68 | Lighthouse | Lighthouse |
| 21 | `whispyLogic` | 69 | `gryakuwhispywoods.c` (WhispyWoods) | WhispyWoods boss tree (Forest event) |
| 22..23 | `0x80106824` | 32 | `gryakubreakfloor.c` (BreakFloor) | **forest pitfall** (multi-stage cracking floor) |
| 24 | `0x80108f10` | 37 | break "strong" (`hitStrongObject`) | **volcano-base hole covers** |
| 25 | `0x80108ce8` | 36 | break "strong" (`hitStrongObject`) | breakable strong prop — candidate **volcano rock walls** (no checklist cell) |
| 26 | `0x80109138` | 38 | break "strong" (`hitStrongObject`) | **dilapidated city houses** |
| 27 | `0x80107bfc` | 33 | break "weak" (`hitWeakObject`) | breakable weak prop — candidate **coral** (no checklist cell) |
| 28 | `0x80107d64` | 34 | break "weak" (`hitWeakObject`) | **forest trees** |
| 29 | `0x80107ecc` | 35 | break "weak" (`hitWeakObject`) | **volcano + high-plains rocks** |
| 30 | `0x801043c8` | 29 | `gryakubreakcoral.c` (BreakCoral / BigStar) | **star pole** (BigStar) |

City Trial therefore uses 14 distinct descriptor ids: **17, 18, 29, 32, 33, 34, 35, 36, 37, 38, 46, 61, 68, 69**. Of the 70 descriptors, the rest are referenced by Air Ride / Top Ride stage creators (not yet enumerated). Note: the "huge pillars" the Forest event spawns are a *separate* BreakRock descriptor (≈39/40) created at event time, **not** part of this 31-instance manifest.

### City Trial breakable inventory — desc_id ↔ in-game object

Identity is anchored by the **break-count bucket array** (`checklist-stat-tracking.md`): `GrYaku_IncrementBreakCount` (`0x80105d80`) reads the broken prop's `desc_id` (`YakumonoData+0x04`) and bumps `PlayerData+0x62b+desc_id`, so the **stat-index equals the desc_id**, and each checklist cell's human-readable text pins a desc_id to a concrete object. (Family/handler comes from the descriptor's embedded source string + its `coll_func`; see "the break path" below.)

| desc_id | object | break family / `coll_func` | identity source | confidence |
|---:|---|---|---|---|
| 29 | **star pole (BigStar)** | `gryakubreakcoral.c` / `hitBigStar` (`0x80103eb8`) | break-count idx `0x1d`; "bust the star pole" | high |
| 32 | **forest pitfall** | `gryakubreakfloor.c` / `hitBreakableFloor` (`0x80106bd0`) | break-count idx `0x20`; "open the pitfall in the forest" | high |
| 34 | **forest trees** (53) | "weak" / `hitWeakObject` (`0x80107914`) | break-count idx `0x22`; "knock down all forest trees" | high |
| 35 | **volcano + high-plains rocks** (41) | "weak" / `hitWeakObject` | break-count idx `0x23`; "break all volcano + high-plains rocks" | high |
| 37 | **volcano-base hole covers** | "strong" / `hitStrongObject` (`0x801086d0`) | break-count idx `0x25`; "open all volcano-base holes" | high |
| 38 | **dilapidated houses** (30) | "strong" / `hitStrongObject` | break-count idx `0x26`; "destroy all dilapidated houses" | high |
| 33 | candidate **coral** | "weak" / `hitWeakObject` | no checklist cell (idx `0x21` unused) — weak/brittle family | candidate (family-inferred) |
| 36 | candidate **volcano rock walls** | "strong" / `hitStrongObject` | no checklist cell (idx `0x24` unused) — strong family | candidate (family-inferred) |

So in City Trial: **trees = 34, rocks (plains+volcano share one bucket) = 35, houses = 38, volcano-base holes = 37, forest pitfall = 32, star pole = 29.** The two remaining break instances (33 weak, 36 strong) have no dedicated checklist cell; by family they are the leading candidates for **coral** (33) and the **breakable volcano rock walls** (36). The "high-plains hole you fall into" is tracked by a *separate* counter (`PlayerData+0x834`), distinct from the `desc_id` break buckets.

### Instance model per CT yakumono type

Not every yakumono is the "one GObj → many props" fan-out. Each of the 31 creator calls in the manifest produces exactly **one GObj**, but where that GObj's geometry/transform lives differs by family. There are **four** binding models, from the per-instance creator/tail-init (`grDataCity1_CreateYakumono`'s callees). The discriminator that matters most for tooling is *which instance pool a prop lives in and where its owner back-reference is written* — because that is exactly what a pool-scanning mod (e.g. Hypernova) keys on.

| desc_id(s) | object(s) | instance model | binding mechanism (per-instance creator) |
|---|---|---|---|
| 34, 35, 33 (weak) · 38, 37, 36 (strong) | trees / rocks / coral · houses / holes / walls | **Multi-instance break family** — 1 GObj, **N props** | creator allocates a child-pointer array at `ydata+0x130`, count at `+0x134`; loops `grScene_FindInstanceByKey(grobj+0x54, key_i)` to claim **N `0x98` records** in the yakumono pool (`grobj+0x64`); sets each `record+0x90 = gobj`. *weak* adds a `0xFF`-seeded audio-handle overlay; *strong* adds parallel arrays at `+0x134/+0x138/+0x13c`. |
| 29 (BigStar), 32 (BreakFloor), 69 (WhispyWoods) | star pole · forest pitfall · WhispyWoods boss | **Single-prop break family** — 1 GObj, **1 prop** | same path as above but **one** `0x98` record stored directly at `ydata+0x130` with `record+0x90 = gobj` (pitfall may claim one optional linked second part). 69 uses this shape but is a boss, not a checklist break bucket. |
| 17 (CatchZone), 18 (RecoveryZone) | catch / recovery trigger volumes | **Single-instance, pool-backed (static)** | tail-init claims **one** `0x98` record (via `zz_800d79c0_`), stores it at `ydata+0x130`, and writes the owner back-ref at **`record+0x138`** — *not* `+0x90`. |
| 46 (Gondola) ×16 | cable-car loop (16 cars) | **Single-instance, pool-backed (movable)** | 16 separate GObjs; each tail-init claims one `0x98` record (pointer stored at `ydata+0x138`), sets `ydata+0x130 = 0`, and clears the matrix-dirty bit (`ydata+0x13c & 0x7f`) so it animates its own transform each frame. |
| 68 (Lighthouse) | lighthouse | **Multi-part structure, separate pool** | iterates its sub-parts in the **`0x140`-stride model-instance pool at `grobj+0x74`** (a different pool from the `0x98` yakumono pool); `ydata+0x130 = 0`. Not breakable, not in the `0x98` pool. |
| 61 (decorative) ×2 | small whispy-TU decorations | **Classic single-instance** | no scene-instance record at all; uses the standard `GrYaku_AttachModel` path (own model JObj at `ydata+0x64`, pos at `ydata+0x1c`); `ydata+0x130/+0x144` are anim scratch. |

**Takeaways:**
- "Many CT yakumono are one object with many children" is true **only for the break families** (34/35/33/38/37/36). Those, plus the single-prop break families (29/32/69), are the props that live in the `0x98` pool with `record+0x90 = gobj` — i.e. the set a `+0x90`-keyed pool scan reaches.
- The **zones and gondola are also in the `0x98` pool** but write their owner back-ref at `record+0x138`, leaving `+0x90` unset. A `+0x90` pointer-match therefore **excludes** them automatically — which is why Hypernova's vacuum can safely walk the whole pool. (The Hypernova breakable set is `29,32,33,34,35,36,37,38`; it deliberately omits 69 (boss) and the non-break families.)
- **Lighthouse** lives in a *separate* `0x140`-stride pool (`grobj+0x74`), and **decorative** props in no pool at all — neither is touched by a `0x98`-pool scan.
- The owner back-ref works because `YakumonoData[0]` (`+0x00`) is the GObj back-pointer, so `record+0x90 = *ydata = gobj`.
- **The weak families' props are `JOBJ_SKELETON` joints; the others are static.** Each prop's render JObj (`record+0x00`) has its flags at `JObj+0x14` — trees (34)/rocks (35)/coral (33) = `0x9` (`JOBJ_SKELETON | JOBJ_CLASSICAL_SCALING`), while houses (38)/walls (36)/holes (37)/floor (32)/BigStar (29)/whispy (69) = `0x40008` (`JOBJ_OPA | JOBJ_CLASSICAL_SCALING`, **no skeleton bit**). Consequence for **moving** a prop: a direct write to the skeleton joints' world matrix (`JObj+0x44`) is **rebuilt from the joint SRT every frame** by `HSD_JObjSetupMatrixSub` (`0x8040d6b4`) and so gets clobbered — to move them you must first set `JOBJ_USER_DEFINED_MTX` (`0x800000`) on the joint (via `JObj_SetFlags`, single-joint), which makes that setup early-return and honor your matrix. The static (non-skeleton) families need no such flag. See the MOVE recipe.

## Yaku-break families and item drops

Three of the yaku-break families have a drop hook that emits items via `City_SpawnMiscItems` → `event_source_drop[].chance_destructible`. See `event-source-drops.md` for the field-to-source mapping and the drop pipeline. Summary:

| Family | Drop helper | Drops? | Per-instance gate field |
|---|---|---|---|
| `gryakubreakrock.c` | `GrYakuBreakRock_DropItems` (`0x8010203c`) | yes | `param[0x24]` (NULL → no drop) |
| `gryakubreakhouse.c` | `GrYakuBreakHouse_DropItems` (`0x80102794`) | yes | `param[0x30]` |
| `gryakubreakcoral.c` | `GrYakuBreakCoral_DropItems` (`0x801040fc`) + `hitBigStar` (`0x80103eb8`) | yes | `param[0x28]` |
| `gryakubreakicicle.c` | — | no | (only breakable behavior; calls `destroyBigStar`) |
| `gryakubreakfloor.c` | — | no | |
| `gryakubreakfan.c` | — | no | |
| `gryakuanimfloor.c` | — | no | |
| `gryakubreakcommon.c` | — | shared helpers (ring-damage range checks) |
| `gryakubreakcoll.c` | — | shared collision base for the break families |
| `gryakubreakhpcoll.c` | — | HP-based variant: door/wall/.../house with per-region HP and crack states |

### Drop descriptor

The `param[+0x24/0x28/0x30]` slot holds an optional pointer to a "drop descriptor" passed to `City_SpawnMiscItems`. The descriptor's layout:

| Offset | Field | Meaning |
|---:|---|---|
| `+0x14..0x20` | `Vec3` velocity | Initial item velocity |
| `+0x1c` | `int drop_source` | Source enum 0..12 (3 = `chance_destructible`); -1 → fallback to `CityEvent_GetRandomItem` |
| `+0x20` | `int shape_flag` | 0 = `_City_SpawnMiscItems` (omnidirectional), 1 = `shootPowerUps_` (directed cone) |
| `+0x28..0x34` | `Vec3` position offsets | Where to spawn relative to origin |
| `+0x38` | `int count_or_flag` | Used by rock: switch case (0..7 valid) controls drop pattern |

If the per-instance pointer is NULL the destruction proceeds without any drop call — same code path, different stage-data wiring.

## How GObjs are bound

Every yakumono GObj has these wiring properties:

| Property | Value | Set by |
|---|---|---|
| `gobj->kind` | `15` | `GObj_Create(0xF, ...)` — used by `GrYakumono_GetState` and `grYakuCheckGObjYakumono` |
| `gobj->p_link` | `8` (`GAMEPLINK_YAKUMONO`) | `GObj_Create(..., 8, ...)` — pause-aware via the standard p_link gating |
| `gobj->gx_link` | `0` | `GObj_Create(..., ..., 0)` — yakumono use HSD's main render path, not a custom GX link |
| `user_data[0]` | `YakumonoData *` | `GObj_AddUserData(gobj, GUDATA=14, destroy_cb=GrYaku_DestroyCallback, ...)` |

To detect a GObj is a yakumono: `gobj->kind == 15` is the canonical test (used by `GrYakumono_GetState`). The framework helper `grYakuCheckGObjYakumono(gobj)` (`0x800f7a50`) does exactly this — reads `gobj->[+0x00]` and returns `1` iff it equals 15, else `0`. A sibling helper `GrYakumono_GetDescId` (`0x800f7a64`) asserts `kind == 15` then returns `ydata->desc_id` (`+0x04`); it is used by the yakumono-break counting path below.

## Loading and persistence

- **Common archive loaders**: `grLoadYakumono_Common` and `_Main` lazily load `YkCommon.dat` and `Yakumono.dat` into globals at `r13[0x5e0]` and `r13[0x5e4]`. These hold `ykDataCommon` and `ykDataAll` structures referenced from descriptor lookups.
- **Position table**: `grdata->yakumono_pos` (at **GrData+0x20**, declared `yakumono_pos` in `stage.h`) carries a sub-block at `[+0x2C]` whose `[+0x8]` is the position-record count. `grGetYakumonoposNum` reads this chain; `loadYakumonoLocations?` loads the records. This is category 8 of the unified 9-category placement system — see "World positioning → (A) The unified 9-category stage-placement table system" below. It is a distinct field from `pos_data` (GrData+0x18), which feeds the scene/collision instance pool that CT breakables actually bind to (placement system (B)).
- **Stage preload**: `Yakumono_Preload` calls `stGetStageKind_3Cyakumono?` (which dereferences the stage table at offset `0x3C`) and, if non-NULL, invokes the GX preloader at `0x80072c90` with shape parameters `(2, 4, 4, 0, 1, 8, 16)` — a small fixed-size preallocation for yakumono GX state.
- **Counter**: `stc_grobj->[+0x6fc]` is the live yakumono count (incremented on every `GrYaku_Create`).
- **Index array**: `stc_grobj->[+0x710]` is the per-stage array of YakumonoData pointers, allocated by `grInitYakumono` at `num*4` bytes and filled as each yakumono spawns. This lets the framework iterate "all currently-loaded yakumono" in O(num) without walking the GObj list. **`num` here is `entry_count` (length of `entries[]`), not `data_count`** — stages that use only the per-grkind hook path (City Trial: `entry_count = 0`) end up with this slot **NULL**, since `HSD_MemAlloc(0 * 4)` returns NULL. Per-frame procs do not depend on the array, so a NULL value here is harmless; only mod code that wants to enumerate live yakumono needs to handle that case.

## World positioning: where props are actually placed

> The visible/breakable City Trial props are authored at **local origin (0,0,0)** in the stage *model* archive (e.g. `GrCity1Model.dat`) — that is what HSDRaw shows. Their map placement is **data-driven and applied at load** from placement tables that live *outside* the model tree, which is why the model node itself reads 0,0,0. There are two independent placement representations, and CT breakables use the second one.

### (A) The unified 9-category stage-placement table system

KAR has a generic "where does object N of category C go" system. Every category has a `grGet<C>posNum` (count) and a `load<C>Locations` (record reader), and **every record is the same shape: `0x24` bytes = 9 floats = 3 `Vec3`s** (position + two more vectors — an orientation basis or rotation/scale; the loaders copy all 9 floats verbatim, so the meaning is set by the consumer). The categories (the `case` index in the resolver) are:

| C | Category | Count fn | addr | Loader | addr |
|--:|---|---|---|---|---|
| 0 | start positions | `grGetStartposNum` | `0x800d0b30` | `grGetStartPosition` | `0x800d0b7c` |
| 1 | enemy spawns | `grGetEnemyposNum` | `0x800d0c88` | `loadEnemy_spawnXYLocation?` | `0x800d0cd4` |
| 2 | items | `grGetItemposNum` | `0x800d1090` | `grLoadItemPosition` | `0x800d10dc` |
| 3 | item areas | `grGetItemAreaposNum` | `0x800d1550` | `loadItemAreaLocations?` | `0x800d15a8` |
| 4 | events | `grGetEventposNum` | `0x800d11d4` | `loadEventLocations` | `0x800d11fc` |
| 5 | vehicles | `grGetVehicleposNum` | `0x800d12f0` | `loadVehicleLocations?` | `0x800d133c` |
| 6 | vehicle areas | `grGetVehicleAreaposNum` | `0x800d16c4` | `loadVehicleAreaLocations?` | `0x800d171c` |
| 7 | global-dead | `grGetGlobalDeadPosNum` | `0x800e5318` | `loadGlobalDeadLocations?` | `0x800e5340` |
| **8** | **yakumono** | **`grGetYakumonoposNum`** | **`0x800d1434`** | **`loadYakumonoLocations?`** | **`0x800d145c`** |

`loadYakumonoLocations?(int index, Vec3 *out0, Vec3 *out1, Vec3 *out2)` copies record `index` from the per-stage record array (cached pointer at `stc_grobj+0x15c`; **count** read via `grdata->yakumono_pos` = GrData+0x20 → `[+0x2C]` → `[+0x8]`). Each category caches its array base in a different `stc_grobj` slot (start at `+0x134`, yakumono at `+0x15c`).

Only **two** call sites read the yakumono table:
- `grResolvePlacementRef` (`0x80088408`) — a small, **generic, stage-agnostic** per-descriptor placement resolver, **not** a City-Trial-init routine. Given a placement-reference object (`ref`), it reads the embedded placement-group record at `ref->[+0x2c]` and `switch`es on that record's category (`grp->[+0x14]`), dispatching to the matching per-category loader (`grGetStartPosition`/`loadEnemyLocations`/`grLoadItemPosition`/`loadEventLocations`/`loadVehicleLocations`/`loadGlobalDeadLocations`/`loadYakumonoLocations?`). Each loader is called with `count = grp->[+0x04]` and the group's vec/data fields (`grp->[+0x18/+0x24/+0x30]`). For category 8 (yakumono) it calls `loadYakumonoLocations?`. The whole body is just this dispatch — the heavy lifting is in the loaders. **It handles categories 0,1,2,4,5,7,8 only — there is no `case 3` (itemArea) or `case 6` (vehicleArea)**, so those two area categories are resolved by a different path. It has no direct xrefs (reached through a function pointer / callback slot); the actual CT placement work is `grDataCity1_CreateYakumono` plus the table-(B) pool build (`zz_800d6dcc_`), not this leaf.
- `dbPosition_Load` (`0x800869cc`) — a **debug position editor**. It destroys existing marker GObjs, calls the category's count fn, then loops `0..count` calling the category loader and `dbPosition_CreateGObj` to drop a visible marker at each record. It covers every category including yakumono. Its existence is direct evidence that these placement coordinates are designed to be enumerated and **moved**. (Companion: `dbPosition_Render` `0x80086d64`.)

### (B) The ground scene/collision instance pool — what CT breakables actually use

The breakable props bind to the **ground runtime object's instance pool**, not table (A). At stage load `zz_800d6dcc_` (the ground collision/scene-section allocator; asserts reference `grcoll.c` / `ground.h` / `zone_num`) allocates several parallel instance arrays inside `stc_grobj` and fills them from `grdata->pos_data` (GrData+0x18). The relevant one is the **`0x98`-byte placed-instance array at `stc_grobj+0x64`** (live count at `+0x68`); each record holds a positioned `JObj` at `[+0x00]` whose world matrix (`JObj+0x44`) is also cached into the record at `[+0x2C]` by `zz_800d6a70_`. Translation of that cached `3x4` row-major matrix lives at record `+0x38 / +0x48 / +0x58` (matrix col 3).

`grScene_FindInstanceByKey` (`zz_800d7954_`, `0x800d7954`) searches that pool for the record whose `[+0x00]` key matches, returning the record pointer.

Each `0x98` record also owns its **mpColl regions inline**: an array pointer at `record+0x0c`, a region count at `record+0x10`, `0x40`-byte stride per region. Each region's `+0x3c` byte holds the **collidable/intact** flag in bit 6 (`0x40`); `+0x38` is the back-key the colliding object stamps so the break path can map a hit → this record. The break finalizer toggles that bit on every region of a record: `grScene_SetInstanceColl` (`0x800d7ad0`, `(record, enabled)` — `0` = broken/non-collidable) sets/clears it, and `grScene_IsInstanceCollAll` (`0x800d7b0c`, `(record, state)`) returns 1 iff every region matches `state` (the break's "still whole?" guard). **These regions are NOT repositioned when the record's `JObj+0x44` world matrix is moved** — they stay baked at the prop's origin. So relocating a prop's model does not relocate its collision, and they can't be cheaply moved onto a puller (they're a fixed slice of the global region array at `*(stc_grobj+0x5c)`, the array the **ground scene-collision holder `stc_grobj+0x54`** exposes at its `+0x08`, found by spatial broadphase). To break a pulled/consumed prop with all genuine consequences, synthesize a `collideWithObject` call rather than trying to move or fake the regions — see "Driving yakumono → Option (b)" below.

These `+0x0c` regions ARE the prop's solid collision — there is **no separate static wall**. The records' regions are a contiguous slice of the 14040-region global array at `*(stc_grobj+0x5c)` (e.g. a house record's region array sits at global index ~9451). The rider's penetration response `mpResponse_DispatchSceneObjColl` (`0x80248bb4`) reads `region+0x3c` bit 6 and **drops any contact whose bit is clear**, so `grScene_SetInstanceColl(record, 0)` is exactly what makes a broken (or vacuumed) prop pass-through. Only break/init code writes that bit — **nothing re-arms it per frame** — so a clear sticks until something re-arms it. The holder is `stc_grobj+0x54` (`+0x08` region base, `+0x10` record pool = `*(stc_grobj+0x64)`, `+0x14` count); `stc_grobj+0x454` is an unrelated struct.

### Multi-instance break families — one GObj, many props

**This is the crux for "which position?":** the break-family creators do **not** make one yakumono per visible prop. Each makes **one parent yakumono `GObj`** (one `YakumonoData`, one `HurtData` with N regions) that manages **N placed sub-instances**:

1. `GrYaku_Create(desc, data_idx)` → the parent.
2. The creator reads a per-instance list from the param block, allocates a child array at **`YakumonoData+0x130`** (count at `+0x134`; the strong/house families spread extra arrays across `+0x134/+0x138/+0x13c` with the count at `+0x140`), then loops once per prop:
   - resolve a scene-instance key: `entries[i].posfield` → `grdata->[+0x104][key*8]`,
   - `grScene_FindInstanceByKey(stc_grobj+0x54, key)` → the `0x98` placed-instance record,
   - store it in the child array and set `record[+0x90] = parent_gobj` (back-ref).

So **"53 forest trees" = 1 yakumono GObj + 53 scene-instance records**, each with its own JObj/world-matrix/position and its own hurt region. The same holds for rocks (35), houses (38), volcano holes (37), etc. **Consequence:** the City Trial manifest's "31 instances" counts *creator calls / GObjs*, not props — the real visible/collidable prop count is in the hundreds. And the parent yakumono's own transform is unused for these: consistent with the header note that `pos` (+0x1c), `model_jobj` (+0x64) and `xform_jobj` (+0x70) are all NULL/zero for break-family props.

### Reading / moving a breakable prop's real position

- **Get** (per prop): walk the parent's child array `YakumonoData+0x130[0 .. +0x134]`; for each `0x98` record, the world position is the JObj translation `record[+0x00] → JObj+0x44` (floats 3/7/11), or the cached copy at `record[+0x38/+0x48/+0x58]`. Do **not** read the parent `YakumonoData+0x1c` for breakables — it is 0,0,0.
- **Move** (per prop, live): rewrite that sub-instance's JObj world-matrix translation (and the cached `record+0x2c` matrix to match). The hurt region tracks the JObj automatically each frame (`HurtData_UpdatePerFrame` in priority-7 proc). See the MOVE/SCALE recipes below — but apply them to the **sub-instance JObj**, not the parent ydata, for break families.
- **Move** (single-instance / active yakumono — cannon, gondola, rising cube, …): those *do* own a positioned `xform_jobj` (+0x70) and use the parent-ydata MOVE recipe directly.
- **Move** (permanent / data level): edit the placement records — the 9-float yakumono table (`grdata->yakumono_pos`) and/or the scene-instance transforms sourced from `grdata->pos_data` — to relocate props at load.

## Shared helpers and assertions

From the descriptor strings, every yakumono kind passes through these centralized invariants (in `gryaku.c`):

- `0 <= data_kind && data_kind < grGetYakuDataNum(gp)` — bounds check on data_idx
- `0 <= data_kind && data_kind < grGetYakuStaticDataNum(gp)` — same for static data
- `kind < Gr_YakuKind_CommonTerminate` — kind enum bound
- `gyp->scale == Gr_DefaultScale` — most code paths assume default scale
- `!yaku_data->localCollData` — local collision data must be NULL except at specific points
- `gp->yaku_num > 0` — at least one yakumono must exist for some helpers
- `grYakuFuncTable[gyp->kind] && grYakuFuncTable[gyp->kind]->{coll_func, adhere_update_func, get_point_func}` — per-kind callbacks must be wired

The `GrYakuCommon_Group_Max` and `GrYakuCommon_Random_Set_Num` constants govern the "common" group system in `gryakucommon.c` (a way to randomly pick subsets of yakumono — used by `GrYakuCommon_SelectRandomGroup`, called at the end of `grInitYakumono`, which selects a random subset of entries with `entries[].x8` group ids and disables the unselected ones via `zz_800f5744_(yd, 0)`).

## Driving yakumono from mod code (break / move / scale)

This is the lever set a mod (e.g. Hypernova) needs to manipulate City Trial props. The move/scale recipes below apply to the **single-instance / active** yakumono shape (one positioned JObj at `YakumonoData+0x70`); the **multi-instance break families** (CT trees/rocks/houses/holes/coral) work differently — one parent GObj with no positioned JObj managing N placed sub-instances — so for those, apply the recipes to each sub-instance JObj (child array at `+0x130`), not the parent. See "World positioning" above.

### The break path is collision-force-driven via the descriptor's `coll_func` — NOT the `+0x100` damage proc

The break families do **not** install a `+0x100` damage-on callback — `GrYaku_InitData` zero-inits `+0x100`/`+0x104` and none of the per-instance creators set them (the *only* exception is BigStar, which sets `+0x100` to its phase-2 handler `0x801040fc` **after** a first collision arms it). So `GrYakumono_Proc10` (`0x800f5454`) — even with a seeded lethal hit in HurtData (`+0x24` hit-gate ≠ 0, `+0x28` damage) — just accumulates into `+0xac` (via `0x800f875c`) and returns without breaking anything, because the `+0x100` slot it would call is NULL. **Seeding HurtData + calling Proc10 is a no-op for every CT break family except an already-armed BigStar.**

The real break is the **`coll_func`** in the prop's descriptor. `collideWithObject` (`0x800f5004`) does: `desc = YakumonoData+0x04` → `entry = stc_yaku_descs[desc]` (the 70-entry table at `0x804a5be8`) → `coll_func = entry[+0x04]` → asserts `coll_func != NULL` (the gryaku.c:518 assert) → calls `coll_func(yaku_gobj, otherCollData, collB, regionIdx, collC)`. (The "16-entry `grYakuFuncTable` at `0x804a5ba8`" elsewhere in this doc is the *generic-spawn* Create-wrapper table — a different table from this per-`desc_id` descriptor table whose `+0x04` holds the break handler.) The handler computes an impact **force** and compares it to the prop's **HP**:

- **`0x80104cd4` (one-shot threshold, non-subtractive)** — used by `hitWeakObject` and BigStar phase 1. `force = otherCollData.radius (CollData+0x344) × impactSpeed²`; breaks iff `force > HP[0]` (HP read from the prop's param block). Does **not** modify HP, so a too-weak hit leaves nothing behind — there is no accumulation.
- **`0x80104be0` (subtractive HP)** — used by `hitStrongObject` and the rock/house drop handlers. Same force, but `HP -= force` is written back; breaks only when remaining HP ≤ 0, so it accumulates across hits.

**`impactSpeed` is a normal projection, not `|delta|`.** `grScene_GetImpactSpeed` (`0x800d8edc`) projects `otherCollData.pos_delta (CollData+0x14)` onto the contacted region's outward normal (`region+0x0c`), **negates** it (the internal scale const at `0x805df634` = −1.0), and clamps a non-positive result to **0**. So the delta must point **into** the surface (against the outward normal); a delta whose dot with the normal is `≥ 0` yields `impactSpeed 0` → `force 0` → no break. This is the trap for a synthesized collider: an arbitrary delta direction can silently fail to break.

Per-family break trigger:

| family (desc) | handler | trigger |
|---|---|---|
| weak (33,34,35) | `hitWeakObject` | single hit with `force > HP` (one-shot threshold; weak hits do nothing) |
| strong (36,37,38) | `hitStrongObject` | per-region subtractive HP; may take several hits |
| floor / pitfall (32) | `hitBreakableFloor` | multi-stage crack — one stage per hit, final break after N stages (max from param) |
| BigStar / star pole (29) | `hitBigStar` | phase 1 needs `force ≥ HP`; if too weak it *arms* phase 2 (sets `+0x100 = 0x801040fc`), and any later damage event then destroys it |

**Implications for a no-contact "vacuum" break.** Because the break is gated on a real collider's mass×velocity, there is no clean "apply N damage" entry point that ignores collision. Options, cleanest first: (a) let the 2× Hypernova rider physically ram props (the natural collision drives the real break — drops, score, anim, teardown — for free); (b) **synthesize a `collideWithObject` call** with a fabricated high-force collider; (c) skip the break entirely and just move/scale. Driving the state machine straight to the broken state via `Gr_StateChange` would skip the drops/score/effects and is not recommended.

**Option (b) is what Hypernova uses, because (a) can't work for a *pulled* prop — the prop's regions stay baked at its origin so the rider never overlaps a model that's been drawn in.** The synthesis recipe (see `Hypernova_BreakInstanceNative`):

- `collideWithObject(yaku_gobj, coll, holder, region_idx, contact)`.
- **`yaku_gobj`** = the prop's parent GObj (`record+0x90`).
- **`holder`** = the ground scene-collision holder `stc_grobj+0x54`; `holder+0x08` is the base of the global mpColl region array (0x40 stride), `holder+0x10`/`+0x14` the placed-instance record pool/count. This is the holder the engine's own break-dispatch path (and `grScene_FindInstanceByKey`) uses. (`stc_grobj+0x454` is a *different* struct — passing it makes `(record+0x0c − *(holder+8))/0x40` negative, so the synthesize silently no-ops.)
- **`region_idx`** = `(record+0x0c − *(holder+0x08)) / 0x40` (a record's regions are a contiguous slice of the global array, so its first region's global index is this difference / stride).
- **`region_idx`** must point at a region with a **usable (non-degenerate) outward normal**, since the impact-speed calc projects the delta onto `region+0x0c`. Scan the record's regions (`record+0x0c` array, count at `record+0x10`) for the first with `|normal|² > 0` and use its global index.
- **`coll`** = a SYNTHETIC `CollData` (zeroed `0x400` buffer): `+0x344` radius is the reliable force lever (`force = radius × impactSpeed²`, computed via `grScene_GetImpactSpeed` `0x800d8edc` → `GrYaku_TestImpactBreak`/`GrYaku_ApplyImpactDamage`) — crank it astronomically for a guaranteed one-hit break even at a small impact speed. `+0x14` frame-delta = **`−normalize(region_normal) × M`** (any `M > 0`) so it projects to a positive impact speed (see the normal-projection note above). `+0x04` = the human rider GObj (read by `GrYakuBreak_GetAttackerPly` `0x80105cb0` for the break-count credit). `+0x44` = a zeroed `mpCollInfo` buffer with `+0x1d0 = −1` ("no BigStar region") — `destroyBigStar` (`0x800d7b8c`, called first by every `coll_func`) walks the collider's coll-info hit entries and would otherwise treat record-0 as a match; with `−1` it returns 0 and the break proceeds.
- Temporarily clear the target region's `+0x34` bit `0x20` for the call: when set, `grScene_GetImpactSpeed` takes a geometry-refined path (using `record+0x2c`/`+0x5c` and the contact point) that can zero a synthetic delta. Restore afterward (live collision data).
- **`contact`** = any valid Vec3 (the prop's current world pos works); only used by the geometry path, which we suppress.

`collideWithObject` then dispatches to the family `coll_func`, which runs the full genuine break tail (collision retire, mesh hide, debris + family drops, SFX, `GrYaku_IncrementBreakCount`, `Gr_StateChange`). The prop must be fully collidable when the call fires (`grScene_IsInstanceCollAll(record, 1)` must be true), so if collision was retired beforehand (e.g. Hypernova retires it during the pull so the moved model leaves no stranded wall) **re-arm it with `grScene_SetInstanceColl(record, 1)` right before the call** — and detect success by re-checking `grScene_IsInstanceCollAll(record, 1)` after (the tail clears it on a real break). The multi-stage floor (desc 32) advances one crack-stage per call.

### Destroying a prop directly is collision-safe

`GObj_Destroy` (`0x80428f64`) calls the GObj's registered user-data destructor `(*gobj+0x30)(gobj+0x2c)` synchronously — for a yakumono that is `GrYaku_DestroyCallback` (`0x800f4f0c`), which frees the HurtData (`+0xec` → `HurtData_Destroy 0x8018c2e4`), the effect group (`+0x10c`, an *Effect-module* eflib handle allocated by `0x800f666c→0x802364e0`, **not** a collision registry), the JObj tree (`+0x64`), and unlinks the prop. So a raw `GObj_Destroy` does **not** dangle any collision entry. The reason to prefer the break path is gameplay fidelity (item drops, break-count/score, break anim + SFX), **not** collision safety. (Always capture `gobj->next` before destroying while walking the p_link list.)

> **Multi-instance caveat:** for the break families this destroys the **whole group**, not one prop — `GObj_Destroy` tears down the single parent yakumono that manages all N props. And `+0x64` (the destructor's JObj target) is NULL for break families: the visible meshes are sub-instance JObjs owned by the **ground scene pool**, not the yakumono, so the destructor does not free them — they keep rendering after the parent is gone. To remove a single breakable prop, drive its break (collision) rather than destroying the GObj.

### MOVE recipe

> **Applies to single-instance / active yakumono** (cannon, gondola, rising cube, …) that own a positioned `xform_jobj` at `YakumonoData+0x70`. **The multi-instance break families (CT trees/rocks/houses/holes/coral) do NOT** — their parent `+0x70`/`+0x64`/`+0x1c` are all 0, and each prop is a separate `0x98` scene-instance record in the parent's child array (`YakumonoData+0x130`). To move one of those, apply the same matrix writes to the **sub-instance's** JObj (`record[+0x00] → JObj+0x44`) and its cached matrix (`record+0x2c`), not the parent ydata. See "World positioning → Multi-instance break families" above.

An instance's render matrix and its hurtbox both derive from its **transform JObj's world matrix** (`JObj+0x44`) — for a single-instance yakumono that JObj is `*(YakumonoData+0x70)`; for a break-family prop it is the **sub-instance's** JObj (`record[+0x00]`, see box above). There is no separate static solid-wall shape for the City Trial breakables (their only collision is the HurtData hurtbox, recomputed from the JObj each frame — see SCALE). To relocate a single-instance prop:

- write the new **local translation** on the root JObj (`JObj+0x10` Vec3), then set `YakumonoData+0x12c |= 0x80` (the matrix-dirty bit). The next frame, `GrYakumono_Proc4` (`0x800f52e8`) sees bit 7 set and calls `GrYaku_InitMatrix` (`0x800f73fc`), which rebuilds `JObj+0x44` from the local transform and copies it into the render object; or
- write the translation column of `JObj+0x44` directly (one-shot) — durable only while bit 7 stays clear (it normally is: a static prop builds its matrix once at spawn and Proc4 never rebuilds it).
- Also update `YakumonoData+0x1c` (the cached world-pos used by drops/SFX).

Bit 7 is **sticky** (nothing clears it), so a one-shot move = set it for one frame; continuous motion = keep it set and rewrite each frame. The hurtbox follows automatically (next point).

> **Skeleton-joint caveat (CT weak break families: trees 34, rocks 35, coral 33).** Those props' sub-instance JObjs carry `JOBJ_SKELETON` (flags `0x9` at `JObj+0x14`), so their world matrix (`JObj+0x44`) is rebuilt **from the joint SRT every frame by `HSD_JObjSetupMatrixSub`** (`0x8040d6b4`) — a path *independent* of `YakumonoData+0x12c` bit 7 / `GrYaku_InitMatrix`. A direct `JObj+0x44` write is therefore clobbered next frame. To drive these, first set `JOBJ_USER_DEFINED_MTX` (`0x800000`) on the joint — `JObj_SetFlags(jobj, JOBJ_USER_DEFINED_MTX)` (`0x8040bd64`, single-joint, does not recurse) — which makes `HSD_JObjSetupMatrixSub` early-return and honor your written matrix. The static (non-skeleton) families (houses 38 / walls 36 / holes 37 / floor 32 / BigStar 29) need no flag; setting it on them anyway is idempotent and harmless. (Used by Hypernova's vacuum so it can pull every breakable family, not just the static ones.)

### SCALE recipe

`YakumonoData+0xa4` (`Gr_DefaultScale = 1.0`) is the **hurtbox** scale: priority-7 proc `GrYakumono_Proc7` (`0x800f53a8`) calls `HurtData_UpdatePerFrame(scale = +0xa4, …)` every frame, which recomputes each region's world center from the JObj world matrix and its radius as `base × (+0xa4)`. So:

- **hurtbox / break collision:** write `+0xa4` — instant, every frame, no flag needed. A non-1.0 value does **not** trip an assert (the only `Gr_DefaultScale` assert is an init-time float↔int round-trip on the constant, not a check of the live field) and does not corrupt break logic.
- **visual model:** `+0xa4` does **not** scale the rendered JObj (`GrYaku_InitMatrix` ignores it). For the visual, scale the transform JObj's local scale (`JObj+0x24/+0x28/+0x2c`) and dirty the matrix (same pattern as the rider's `gmLanMenu_Scale3DObject`). For a coherent 2× prop, set both `+0xa4` (hurtbox) and the JObj local scale (model).

> **Multi-instance caveat:** for break families, `+0xa4` lives on the parent ydata, so writing it scales **every** sub-instance's hurtbox at once (each region's radius is `base × +0xa4`). But there is no parent JObj for the visual, so the model scale must be written on each **sub-instance** JObj (`record[+0x00]`) individually.

**The HurtData hurtbox tracks the model — the solid mpColl regions do NOT.** These are two different collision systems and they behave oppositely when you move a prop:
- The **HurtData hurtbox** (`ydata+0xec`, recomputed each frame by `GrYakumono_Proc7` from the JObj world matrix and `+0xa4`) follows a moved/scaled JObj for free.
- The **solid mpColl regions** (`record+0x0c`, the slice of the global array at `*(stc_grobj+0x5c)`) are **baked at the prop's origin and never repositioned** — moving the JObj leaves the solid collision behind. This holds for ALL break families including the "strong" ones (36/37/38): there is **no extra separate solid shape** — the `record+0x0c` regions are the whole solid collision, and they stay put.

The strong families do **not** carry a second baked solid shape. Their solid collision is exactly the `record+0x0c` regions: those regions slice into `*(stc_grobj+0x5c)`, the rider response reads their `+0x3c` bit 6, and clearing it via `grScene_SetInstanceColl(record,0)` makes the prop pass-through with nothing left behind. So a vacuum that *moves* a prop must **retire** its collision (clear bit 6) to avoid a stranded wall at the origin — moving the JObj alone is not enough.

## Mod implications

For KARchipelago, the yakumono system is relevant for:

1. **Drop gating** — already covered by `item_spawn_filter.c` filtering `event_source_drop[].chance_destructible`.
2. **Damage-source attribution** — when a player breaks a star pole, event pillar, or volcano wall, the credit goes through the HitColl pipeline at proc 9. To gate yakumono damage (e.g. for a "no breakables" challenge), hook `GrYakumono_Proc9_HitColl` (priority 9 stage HitColl) or the per-kind `damage-on` callback at `+0x100`.
3. **Counting yakumono interactions** — `stc_grobj->[+0x6fc]` is the live **GObj** count (incremented once per `GrYaku_Create`), *not* the visible prop count: a break-family GObj manages many props, so this is ~31 in CT while the prop count is in the hundreds. The per-kind `damage-on` callback fires once per damage event, which is the natural extension point for "X yakumono broken" location checks. The **vanilla** break-count path is already wired up: every break-family drop handler (`GrYakuBreakRock_DropItems`, `GrYakuBreakHouse_DropItems`, `GrYakuBreakCoral_DropItems`, `hitBigStar`, `hitWeakObject`) calls `GrYaku_IncrementBreakCount` (`0x80105d80`), which extracts the broken yakumono's `desc_id` via `GrYakumono_GetDescId` and bumps a per-`desc_id` byte counter in the player struct (`Ply_IncrementYakumonoBreakCount`, `0x8022fed8`, counters at PlayerData `+0x62b+desc_id`). This counter array is what the checklist's "break N of object X" cells read — see `checklist-stat-tracking.md` ("The yakumono-break bucket array (`+0x62b`)").
4. **Goal hooks** — the WhispyWoods kind at desc_id 69 is a candidate for a "defeat WhispyWoods" goal: hook its damage-on or its terminal state via `Gr_StateChange` instrumentation. The Lighthouse (desc_id 68) is similarly addressable.

The 70-descriptor table at `0x804a5be8` is read-only ROM data; modifying it would require a CODEPATCH pointing at a replacement table. For most mod purposes, hooking `GrYaku_Create` (to observe spawns) or the priority-9/10 procs (to observe damage) is sufficient and avoids touching the descriptor structure.

## Descriptor table (70 entries at `0x804a5be8`)

The descriptor table is **not homogeneous** — it has two sections with different layouts:

### Indices 0..15 — paired generic descriptors

These are referenced by the 16-entry `grYakuFuncTable[]` dispatch path. Eight unique 40-byte descriptor blocks, each shared by two consecutive table indices (the "non-ctrl" and "ctrl" variants):

| `desc_id` pair | Descriptor block | First field (back-ptr) | Function ptr at +0x1c |
|---|---|---|---|
| 0, 1 | `0x804a5da8` | `0x804a5d98` | `0x800f94b8` |
| 2, 3 | `0x804a5dd0` | `0x804a5dc0` | `0x800f968c` |
| 4, 5 | `0x804a5df8` | `0x804a5de8` | `0x800f9860` |
| 6, 7 | `0x804a5e20` | `0x804a5e10` | `0x800f9a24` |
| 8, 9 | `0x804a5e48` | `0x804a5e38` | `0x800f9ba4` |
| 10, 11 | `0x804a5e70` | `0x804a5e60` | (zeros at this offset) |
| 12, 13 | `0x804a5e98` | `0x804a5e88` | (zeros) |
| 14, 15 | `0x804a5ec0` | `0x804a5eb0` | (zeros) |

Each 40-byte block has the form:

| Offset | Type | Value |
|---:|---|---|
| +0x00 | `void *` | back-pointer (16 bytes before the block — usually points into zero padding) |
| +0x04..0x18 | — | zeros |
| +0x1c | `void(*)(...)` | per-kind function pointer (e.g. used by `coll_func` or `get_point_func`) |
| +0x20..0x24 | — | zeros |

The "back-pointer" appears to be a HSD-class artifact, not load-bearing for the framework — the targets are zero-padded space between blocks.

### Indices 16..69 — per-instance descriptors

These are referenced by the per-grkind hook path (e.g. `grDataCity1_CreateYakumono`). They have a **different, variable layout** from the paired descriptors and embed the kind's source filename + assertion strings (which is how each one is identified):

| Offset | Type | Value |
|---:|---|---|
| +0x00 | `void *` | **state-table back-pointer** — points to the per-kind state table just before this block (see "How `yd->state_table` (+0x84) is used") |
| +0x04 | — | zero |
| +0x08 | `void(*)(...)` | per-kind init/check fn ptr (present for some kinds — e.g. DownForceZone `0x800f9ed0` — but **zero for the cannon**, whose handlers live in its state table) |
| +0x0c..0x10 | — | zeros |
| +0x14 | `char[]` | source filename (e.g. `"gryakudownforcezone.c"`), followed by the assertion expression (`"gyp->kind == Gr_YakuKind_DownForceZone"`) |

Block size varies with the strings (40 to ~136 bytes). Some kinds (the cannon) also embed a trailing 4-entry function-pointer table after the strings. `grDataCity1_CreateYakumono` and the other named per-instance creators hardcode the `desc_id` literal that maps to one of these blocks.

**Kind identification.** Every per-instance descriptor names its YakuKind in an embedded source-file string. In address order the kinds run: DownForceZone, CatchZone, RecoveryZone, RotJumpHill, InvisibleBall, RisingCube(Ctrl), Gondola, Cannon, PushOutWall(Ctrl), LightTunnel, Pillar(Ctrl), BreakRock, BreakHouse, AnimFloor, BreakCoral, BreakIcicle (+ BreakCommon helper), LaserGate(Ctrl), BreakFloor, BreakFan, BreakColl, BreakHpColl, WhispyWoods. Anchored `desc_id` values (descriptor string maps cleanly to one block, matching a known creator):

| desc_id | YakuKind | Source file |
|---:|---|---|
| 16 | DownForceZone | `gryakudownforcezone.c` |
| 17 | CatchZone | `gryakucatchzone.c` |
| 20 | RecoveryZone | `gryakurecoveryzone.c` |
| 48 | Cannon | `gryakucannon.c` |
| 68 | Lighthouse | (own creator, `Lighthouse_Create`) |
| 69 | WhispyWoods | `gryakuwhispywoods.c` |

The remaining desc_ids interleave small helper/variant sub-descriptors between the primary kind blocks; each maps to a kind through its creator's hardcoded `desc_id` literal.

### How `yd->state_table` (+0x84) is used

`GrYaku_InitData` stores `*(stc_yaku_descs[desc_id])` — the descriptor's +0x00 "back-pointer" — into `yd[+0x84]`. It is the base of the kind's **state table** (16-byte entries), which sits in memory immediately before the descriptor block. `Gr_StateChange` indexes it as `base + state·16` and installs the active state's handler into `proc1` (+0xF0). For passive kinds (zones) the table is all-zero (a single null state); for active kinds it holds real handlers — e.g. the cannon's table at `0x804a6430` is `{state0 → GrYakuCannon_State0 (0x800fee40), state1 → GrYakuCannon_State1 (0x800ff010), …}`. The back-pointer's distance from the descriptor varies with the table size (0x10 for zones, 0x20 for the cannon).

## Generic dispatch table (16 entries at `0x804a5ba8`)

The table is organized as **8 pairs**, one pair per generic kind. Each pair shares a common tail-init function but differs in the bit-flag init it calls:

| `entry.kind` | Wrapper | Bit-flag init | Tail-init | Notes |
|---:|---|---|---|---|
| 0 | `0x800f9210` | `GrYakuFlags_SetBase` (clears bit 7, sets bit 6) | `GrYaku_BaseKind0_TailInit` | base kind 0 |
| 1 | `0x800f9258` | `GrYakuFlags_SetCtrl` (sets bit 7, sets bit 6) | `GrYaku_BaseKind0_TailInit` | base kind 0, "ctrl" variant |
| 2 | `0x800f9364` | `GrYakuFlags_SetBase` | `GrYaku_BaseKind1_TailInit` | base kind 1 |
| 3 | `0x800f93ac` | `GrYakuFlags_SetCtrl` | `GrYaku_BaseKind1_TailInit` | base kind 1, "ctrl" |
| 4 | `0x800f9538` | `GrYakuFlags_SetBase` | `GrYaku_BaseKind2_TailInit` | base kind 2 |
| 5 | `0x800f9580` | `GrYakuFlags_SetCtrl` | `GrYaku_BaseKind2_TailInit` | base kind 2, "ctrl" |
| 6 | `0x800f970c` | `GrYakuFlags_SetBase` | `GrYaku_BaseKind3_TailInit` | base kind 3 |
| 7 | `0x800f9754` | `GrYakuFlags_SetCtrl` | `GrYaku_BaseKind3_TailInit` | base kind 3, "ctrl" |
| 8 | `0x800f98e0` | `GrYakuFlags_SetBase` | `GrYaku_BaseKind4_TailInit` | base kind 4 (uses `zz_800d7a40_` collision alloc with extra param 10) |
| 9 | `0x800f9928` | `GrYakuFlags_SetCtrl` | `GrYaku_BaseKind4_TailInit` | base kind 4, "ctrl" |
| 10 | `0x800f9a60` | `GrYakuFlags_SetBase` | `GrYaku_BaseKind5_TailInit` | base kind 5 |
| 11 | `0x800f9aa8` | `GrYakuFlags_SetCtrl` | `GrYaku_BaseKind5_TailInit` | base kind 5, "ctrl" |
| 12 | `0x800f9be0` | `GrYakuFlags_SetBase` | `GrYaku_BaseKind6_TailInit` | base kind 6 (no model/collision; just `Gr_StateChange`) |
| 13 | `0x800f9c28` | `GrYakuFlags_SetCtrl` | `GrYaku_BaseKind6_TailInit` | base kind 6, "ctrl" |
| 14 | `0x800f9cb0` | `GrYakuFlags_SetBase` | `GrYaku_BaseKind7_TailInit` | base kind 7 (no model/collision) |
| 15 | `0x800f9cf8` | `GrYakuFlags_SetCtrl` | `GrYaku_BaseKind7_TailInit` | base kind 7, "ctrl" |

**Variant marker.** The "non-ctrl" vs "ctrl" flag is **bit 7 of `yd->flags` (+0x12c)**: `GrYakuFlags_SetBase` clears it, `GrYakuFlags_SetCtrl` sets it. Per-kind handlers branch on this byte (e.g. the descriptor-+0x1c function `GrYaku_BaseKind0_DescFunc` first checks `flags[+0x12c] < 0` — the high bit — before running its kind-specific work).

**Tail-init structure.** Pairs 0..3 use `zz_800d79c0_` to allocate kind-specific collision data. Pairs 4..5 use `zz_800d7a40_` (same role + extra param `10`). Pairs 6..7 skip the alloc and just transition state — these are the lightweight kinds (likely zones / non-collidable hazards). All eight pairs end with a `Gr_StateChange` whose initial-state floats are read from r2-relative SDA2 at `0x805df868..0x805df8a4` (one pair of floats per base kind, 8 bytes apart).

The generic `entries[].kind` value is therefore in 0..15 with bit 0 acting as the "ctrl" flag and bits 1..3 selecting the base kind. **This is a separate enum from `YakuKind`** — `YakuKind` covers all per-kind handlers (rising-cube, lasergate, pillar, …, the BREAK family, the HP-coll family, lighthouse, whispywoods), while the 0..15 generic-dispatch enum is just the eight base behaviors that the framework's stock walker can spawn from `Yakumono.dat`.

**The generic path is kind-agnostic in code.** None of the eight `GrYaku_BaseKindN_TailInit` or the five non-null `GrYaku_BaseKindN_DescFunc` reference a source-file or `Gr_YakuKind_*` — the wrappers/tail-inits bake in no specific YakuKind. The base kinds differ only structurally: collision-allocator variant (kinds 0–3 → `zz_800d79c0_`; kinds 4–5 → `zz_800d7a40_` with extra param `10`; kinds 6–7 → no collision, lightweight), `DescFunc` form (kinds 0–2 share the flag-aware 3-step form, kinds 3–4 a 2-step form, kinds 5–7 have no `DescFunc`), and the per-kind `Gr_StateChange` floats. Everything else — model, joint, collision shape, and thus the concrete kind — comes from the `Yakumono.dat` entry data at runtime. So the base-kind→YakuKind table is **not derivable from static code**; it is carried by the `Yakumono.dat` entry data at runtime. The four Ctrl-paired YakuKinds (RisingCube, PushOutWall, Pillar, LaserGate) are spawned through the *per-instance* descriptor path (range 16..69, with real handlers), not necessarily the generic walker; the lightweight generic kinds 6–7 (no collision, `Gr_StateChange`-only) line up with the non-collidable zone kinds (DownForceZone/CatchZone/RecoveryZone).

## Per-grkind hook table (`PTR_PTR_804a322c`)

The table is 28 entries, indexed by the physical `GroundKind` (`gr_kind` 0..27). Each entry is a pointer to a per-grkind block whose `+0x04` slot is the optional init hook called by `grInitYakumono`. Block size and layout vary; what matters for dispatch is whether `+0x04` is non-NULL.

Of the 28 entries:

- **15 GroundKinds have a real init hook** (do explicit per-instance spawns before the `entries[]` walker runs): 0, 1, 2, 3, 4, 5, 7, 8, **9 (= GrCity1)**, 10, 15, 19, 21, 26, 27.
- **1 GroundKind has a 4-byte stub hook** (`blr` only — same effect as NULL): 17 (= `GrColosseum5` / Kirby Melee 2, fn `0x8010faac`, size 4).
- **12 GroundKinds have NULL hooks** (rely entirely on the generic `entries[]` walker): 6, 11, 12, 13, 14, 16, 18, 20, 22, 23, 24, 25.

The table is indexed by the **physical GroundKind** (file order — the ground geometry file that loads), not by StageKind. Ground-file names below come from the stage-file table decoded in `docs/sky-backdrop-system.md`:

| `gr_kind` | Ground file | Init hook | Notes |
|---:|---|---|---|
| 0 | `GrPlants1` (Fantasy Meadows) | hook `0x8010e09c` (block 0x804a7528, size 0x10) | minimal block — embedded source strings live in the *next* block |
| 1 | `GrHeat2` (Magma Flows) | hook `0x8010e1c8` (block 0x804a7538, size 0x88) | source `grheat2.c` |
| 2 | `GrDesert1` (Sky Sands) | hook `0x8010e378` (block 0x804a75c0, size 0x40) | source `grdesert1.c` |
| 3 | `GrCheck2` (Checker Knights) | hook `0x8010e5dc` (block 0x804a7600, size 0x10) | minimal block |
| 4 | `GrValley2` (Celestial Valley) | hook `0x8010e87c` (block 0x804a7610, size 0x88) | source `grvalley2.c` |
| **5** | **`GrMachine2` (Machine Passage)** | **hook `0x8010ea40` (block 0x804a7698, size 0x88)** | **source `grmachine2.c` — calls `GrYakuCannon_Create` (data_idx=1) plus 4 more per-instance creators. The *only* Air Ride stage with a cannon yakumono.** |
| 6 | `GrSpace2` (Nebula Belt) | NULL hook (block 0x804a7720, size 0x10) | relies entirely on the generic `entries[]` walker |
| 7 | `GrSky2` (Beanstalk Park) | hook `0x8010ec90` (block 0x804a7730, size 0x40) | source `grsky2.c` |
| 8 | `GrIce1` (Frozen Hillside) | hook `0x8010edf8` (block 0x804a7770) | source `grice1.c` |
| 9 | `GrCity1` (City Trial) | `grDataCity1_CreateYakumono` (`0x8010f268`) | spawns 31 explicit per-instance yakumono; **does NOT call `GrYakuCannon_Create`** |
| 10 | `GrZeroyon1` | `0x8010f880` (real) | drag-race ground |
| 11..13 | `GrZeroyon3/4/5` | NULL | other drag-race grounds use only the generic walker |
| 14 | `GrPasture1` (Kirby Melee 1) | NULL | |
| 15 | `GrColosseum1` | `0x8010fa0c` (real) | |
| 16 | `GrColosseum3` | NULL | |
| 17 | `GrColosseum5` (Kirby Melee 2) | `0x8010faac` (4-byte stub) | functionally NULL |
| 18 | `GrJump1` (High Jump) | NULL | |
| 19 | `GrJump2` | real | |
| 20 | `GrJump3` | NULL | |
| 21 | `GrDedede1` | real | King Dedede arena |
| 22 | (no ground file) | NULL | `param_9 == 0x16` special-case in `grLoadStageArchive` |
| 23 | `GrTest` | NULL | debug |
| 24..25 | `GrTest6` / `GrTest7` | NULL | debug |
| 26 | `GrSimple` | `0x8010fbd4` (real) | system ground |
| 27 | `GrSimple2` | `0x8010ff08` (large, 0x1dc bytes) | hook is `grDataSingleRace4_CreateYakumono` (a single-race stadium that loads this ground); also calls `GrYakuCannon_Create` (data_idx=18) — second of the only two cannon callers in the game |

**`GroundKind` ≠ `AirRideCourse`.** The `AirRideCourse` enum in `stage.h` is the in-game course-selection index used in menus and `GameData` (`MACHINE_PASSAGE = 6`), but the internal `GroundKind` for that same stage is **5**. The two enums share the "0..8 = Air Ride courses" range but use different orderings. When indexing the per-grkind hook table at `0x804a322c`, always use `GroundKind`, never `AirRideCourse`.

There is no physical GroundKind **≥ 28** — the table's 28 entries (0..27) cover every ground file. The City Trial stadiums not represented by their own ground above (the remaining single races, Vs. King Dedede) are *StageKinds* that reuse one of these physical grounds rather than shipping a new ground file, so they dispatch through whichever GroundKind they load (or, like a boss arena, may skip `grLoadStage`'s yakumono path entirely).

## `gyp->fgm` substruct

`YakumonoData` includes a substruct `fgm` containing `int idDataNum` (count of FGM ids) plus an array of FGM ids. Two assertion strings name the field — `0x804a6fc4` and `0x804a7130`, both `"0 <= fgmId && fgmId < gyp->fgm.idDataNum"` (referenced from `gryakubreakcoll.c` and `gryakubreakhpcoll.c`). The substruct is only consumed by the BREAK and BREAK-HP-COLL families — other yakumono kinds never read `gyp->fgm`.

**`fgm.idDataNum` is at +0x11c, and the `fgm` substruct overlays the +0x118 audio block** — the same region `GrYaku_InitAudio` fills from the `param+0x14` audio descriptor (fitting, since "FGM" in this engine is the field SFX/effect-id manager). The substruct reads:

| Offset | `gyp->fgm` field |
|---:|---|
| +0x118 | `fgm.idData` — ptr to the FGM id-data (deref'd as `{ptr @0, byte @4}`) |
| +0x11c | `fgm.idDataNum` — id count |
| +0x120 | (audio track — `Map_AllocAudioTrack`) |
| +0x124 | (audio emitter — `Map_AllocAudioEmitter`) |

The full break-family tail (transient region handles, allocated from the FGM data) continues past it:

| Offset | Used as |
|---:|---|
| +0x130 | pointer to per-region audio handle array (one per HitColl region) |
| +0x134 | pointer to per-region damage/HP state array (BREAK-coll), or counter (BREAK-hp-coll) |
| +0x138, +0x13c | transient model+gobj handles for the current region (overwritten per iteration) |
| +0x140 | per-stage audio handle counter (BREAK-hp-coll only, decremented as handles are consumed) |
| +0x148, +0x14c | transient model+gobj handles (BREAK-hp-coll variant slots) |
| +0x150 | pointer to per-region 4-byte audio source allocations (BREAK-coll) |
| +0x160 | same idea for BREAK-hp-coll |

So `YakumonoData` extends to **at least +0x18c**: independently of the break families, `GrYakuCannon_TailInit` (`0x800fed48`) zeros a contiguous run of ydata words from `+0x130` through `+0x188` (the cannon's `userInfo[]`/local-context slots), so the struct cannot end before `+0x18c`. The `fgm` substruct begins at `+0x118` (= the audio block) with `idDataNum` at `+0x11c`; the trailing `+0x130..+0x160` slots above hold the per-region transient handles the break families allocate from it.

## Spawning yakumono in stages they don't normally appear in

> **Status: dormant scaffolding.** The cannon code below lives in `mods/custom_events/src/cannon_event.c`. It is **not** a registered custom event (it does not appear in the `CustomEventsAPI` event tables) and is gated off by default (`CANNON_LOAD_ENABLED = 0`; the spawn path runs only when `CANNON_SPAWN_ENABLED` is built in). No production code path spawns a cannon in City Trial.

Calling `GrYaku_Create(48, data_idx)` + `GrYakuCannon_TailInit(gobj)` from `On3DLoadEnd` in City Trial — well after `grInitYakumono` has finished — runs without asserting, even though City Trial's per-grkind hook never spawns a cannon. The yakumono counter (`(*stc_grobj)[+0x6fc]`) increments by 1 per call, so the GObj is fully wired and registered.

But "runs without asserting" is not the same as "fully spawned." The Create init pipeline silently skips graphical setup when the param block doesn't supply the data those steps need. With a zeroed param block, the post-spawn ydata looks like this:

| Slot | Status with zeroed param |
|---|---|
| `+0x000` gobj backref | populated |
| `+0x004` desc_id (48) | populated |
| `+0x008` data_ptr | populated (points at our block) |
| `+0x040..+0x06f` matrix | **all zero** (InitMatrix bailed) |
| `+0x064` JObj root | **NULL** (AllocJObj bailed) |
| `+0x074` state | 0 (tail-init's `Gr_StateChange` ran) |
| `+0x084` state_table | populated (descriptor back-pointer target — the per-kind state table) |
| `+0x088..+0x09c` axis vectors | populated (1.0 entries) |
| `+0x0a4` scale | 1.0 |
| `+0x0e8..` bbox | populated (FinalSetup ran) |
| `+0x0ec` hurt_data | **populated** (HurtData_Create ran) |
| `+0x0f0` proc1 | `0x800fee40` (auto-installed from the per-kind state table) |
| `+0x0f4..+0x108` proc2..proc5, on/off_damage | NULL (cannon doesn't use them) |
| audio_track / audio_source | NULL (no per-stage audio events) |

The cannon ends up as a **ghost yakumono**: collidable (HurtData), state-machine-driven (proc1 ticking every frame), but invisible (no JObj, no model) and immobile (no matrix). The 7 priority procs are added unconditionally and tick even on a model-less yakumono — `GrYakumono_Think` and the HitColl pipeline both read from `hurt_data`, not from the JObj.

### Param-block gating in the framework's init pipeline

Two helpers in the init pipeline branch on fields inside the `YakumonoParam` block (`ydata->data_ptr`, equal to `grdata->yakumono->data_array[data_idx]`):

| Helper | Address | Gate | If non-zero | If zero |
|---|---|---|---|---|
| `GrYaku_AllocJObj` | `0x800f7308` | `data_ptr->[+0x04]` | full alloc with model joint data (reads `jobj_data[0]` words `[0]/[4]/[8]/[0xc]`) → writes JObj to `ydata+0x64` | empty no-op alloc, leaves `+0x64` as 0 |
| `GrYaku_AttachModel` | `0x800f6274` | `data_ptr->[+0x0c]` | reads `grobj+0x54` / `grobj+0x0c` (lights & motion), attaches model to `ydata+0x64` | falls into the no-model branch, only sets default position bytes at `ydata+0x1c` |
| `GrYaku_AttachAnim` | `0x800f6394` | `data_ptr->[+0x04]` **then** `jobj_data[0][+0x10]` | attaches anim to `ydata+0x64` (`zz_800e5b20_`). **No separate param field** — anim data is bundled under the same JObj-data block the JObj alloc uses | no-anim init (`zz_800e5b0c_`) |
| `GrYaku_InitAudio` | `0x800f77dc` | `data_ptr->[+0x14]` | reads the audio descriptor `{idData @0x00, idDataNum @0x04, track_param @0x08}` → fills `ydata+0x118` (`fgm.idData`), `+0x11c` (`fgm.idDataNum`), `+0x120` (`Map_AllocAudioTrack`), `+0x124` (`Map_AllocAudioEmitter(1)`) | leaves the fgm/audio block zero |

These gates are real, but the JObj/model ones describe **whether the yakumono framework allocates its own attached JObj/model**, not whether the yakumono ends up visible. Many yakumono kinds — including the cannon — leave both gates zero and get their visible mesh from elsewhere. Always check the actual vanilla param block at runtime before assuming a kind needs framework-managed assets.

### Cannon: visible model lives in the stage's static scene graph

Vanilla Machine Passage's `data_array[1]` and the ydata of a vanilla cannon GObj:

- Vanilla cannon param `+0x04` = 0, `+0x0c` = 0. Both framework gates are intentionally NULL.
- Vanilla cannon ydata `+0x040..+0x06f` (matrix) = all zero. `+0x064` (JObj root) = NULL. Same as a zeroed-param ghost spawn.
- Vanilla cannon ydata `+0x0ec` (HurtData), `+0x0f0` (`proc1` = `0x800fee40`), and `+0x118..+0x124` (audio handles) — populated.

So the cannon yakumono only contributes **collision (HurtData), state machine (proc1), audio emitter (TailInit's `AudioEmitter_UpdatePosition` call), and eject physics**. The visible cannon mesh is part of `GrMachine2Model.dat` — loaded as part of the stage's main scene graph by `grLoadStage`, not by the yakumono framework.

The cannon's param block (sized at least `+0x80`) decodes as:

```
+0x00  metadata block ptr — small array {0x02, count=3, ?, 0x04, 0, packed_jointref, 0x02, ?}
+0x04  0      (framework jobj_data gate; intentionally zero)
+0x08  0
+0x0c  0      (framework model_data gate; intentionally zero)
+0x10  0
+0x14  trigger_desc 1   ┐
+0x18  physics block 1  ├ five repeating (trigger_desc, physics) pairs at 0x18 stride
+0x1c..+0x28  zeros     │  one per cannon barrel → 5 barrels
+0x2c  trigger_desc 2   │
+0x30  physics block 2  │
...                     │
+0x74  trigger_desc 5   │
+0x78  physics block 5  ┘
```

Each `trigger_desc` (0x20 bytes) holds `[self-back-ptr, 1, 0, 1, angle_float, 0, packed_joint_ref, 0]` where `packed_joint_ref = 0x000f00XX` with `XX` = stage-joint index for that barrel (5, 6, 8 in Machine Passage). Each `physics block` (0x20 bytes) holds `[count, kind, force, factor, scale, angle, factor, value]` — eject parameters per barrel.

The trigger_desc's joint reference is what binds the yakumono to the static stage geometry: the cannon framework reads joint positions from the live scene graph and overlays HitColl regions / launch impulse there. There is no separate "cannon model" asset to load — only the stage joints have to exist.

### What this means for cross-stage spawning of the cannon

The framework plumbing is stage-agnostic — `GrYaku_Create(48, idx)` + `GrYakuCannon_TailInit` runs cleanly in any stage and gives a fully-wired ghost cannon (collision + state machine + audio). To get a **visible, functional cannon in CT** the missing piece is the static geometry — the cannon mesh and its anchor joints — which currently lives only in `GrMachine2Model.dat`. Three approaches:

1. **Splice GrMachine2Model.dat's cannon subtree into CT's main scene graph at runtime.** Load the archive, find the cannon JObj subtree (the joints referenced by `0x000f00XX` packed values), graft it onto a CT scene-graph parent. Then spawn the yakumono with its param's joint refs pointing at the spliced subtree. Most efficient render-wise but invasive — needs HSD JObj-tree manipulation.
2. **Coexist: render GrMachine2's cannon-area geometry as a parallel scene graph.** Allocate a separate top-level GObj that draws GrMachine2's cannon JObjs, position it in CT's coordinate space. Yakumono points at GrMachine2's joints (which live in the parallel tree). Less invasive, but pays a second-pass render cost.
3. **Author a custom cannon mesh from scratch.** Build a JObjDesc tree in mod memory, attach via `param[+0x04]` so the framework's `GrYaku_AllocJObj` does the alloc. Yakumono mode "as documented" — the gate fields finally serve their nominal purpose. This bypasses GrMachine2.dat entirely but requires authoring HSD geometry by hand.

### Summary table

| Layer | Stage-coupled? | Status in CT with zeroed/cannon param | What needs to happen for visible CT cannon |
|---|---|---|---|
| Yakumono framework (Create, procs, state machine) | No | works | — already works |
| HurtData / collision regions | No | works | — already works |
| `proc1` callback (cannon's state machine) | No | works (installed by `Gr_StateChange` from the per-kind state table at `state_table`/+0x84) | — already works |
| Audio emitter (`AudioEmitter_UpdatePosition`) | Indirectly (via stage audio source) | works in MP, bare in CT | works in CT too once stage audio source is allocated |
| **Visible model (mesh + barrels)** | **Yes — lives in `GrMachine2Model.dat`** | **absent in CT** | **load+splice or author custom mesh** |
| **Anchor joints (referenced by `0x000f00XX`)** | **Yes — joints in stage scene graph** | **absent in CT** | **must exist for yakumono to position barrels correctly** |
| Position / matrix | Derived from anchor joints | unset (matrix all zero, but irrelevant — vanilla MP cannon also has unset matrix; positioning happens through joint refs) | works once anchor joints exist |

## Cross-stage cannon spawning: remaining work

- **Exact base-kind→YakuKind table for the generic dispatch path.** As established under "Generic dispatch table", the eight generic base kinds are kind-agnostic in code. The concrete YakuKind each `entries[].kind` produces is carried by the `Yakumono.dat` entry data at runtime, not recoverable from the static binary. (The per-instance descriptor path, by contrast, names every kind in an embedded string — see "Kind identification" above.)
- **Cannon `YakumonoParam` fields beyond `+0x80`.** Decoded through ~`+0x80`: a metadata-block ptr at `+0x00`, framework gates at `+0x04`/`+0x0c` (both intentionally zero — see "Cannon: visible model lives in the stage's static scene graph"), and five repeating `(trigger_desc, physics_block)` pairs at 0x18 stride covering five barrels. Each trigger_desc encodes a stage joint index in a `0x000f00XX` packed value, each physics block holds eject force/angle/scale params. The slots past `+0x80` are not yet decoded.
- **Cross-stage asset injection mechanism.** The yakumono framework is stage-agnostic; the visible model is stage geometry. Bringing the cannon's mesh into CT requires loading `GrMachine2Model.dat` and either splicing its cannon JObj subtree into CT's main scene graph, rendering it as a parallel scene graph, or authoring a substitute cannon mesh from scratch. This is an implementation task; the findings below scope it.

  **Heap-budget constraint.** Loading both full archives in CT — `Archive_LoadFile("GrMachine2.dat")` + `Archive_LoadFile("GrMachine2Model.dat")` from `On3DLoadEnd` — does load (and `Archive_GetPublicAddress` resolves `grDataMachine2` in stage.dat plus `grModelMachine2` / `grModelMotionMachine2` in model.dat), but leaves no headroom. Stage.dat is ~207KB and Model.dat is ~1.6MB; after the loads heap 1 has ~30 bytes free, so any subsequent allocation (e.g. `JObj_LoadSet_SetPri` on `grmodel[0]` needs ~1KB) trips `assertion "addr" failed in initialize.c on line 397`. The full Model archive is therefore too large to load in CT.

  **Archive layout.** `GrMachine2Model.dat` exposes 2 public symbols: `grModelMachine2` (the scene-models entry) and `grModelMotionMachine2` (the motion-anim entry). `GrMachine2.dat` exposes `grDataMachine2` plus 1 unresolved extern: `GrdMachine2_CannonSAN1_ACTION_Cannon1_animjoint` — the cannon's animated joint, which `Archive_LoadFile` does not auto-resolve against globally-loaded archives (so `*slot=0` even after model.dat is loaded; `grLoadStageArchive` does the post-parse extern resolution by walking `0x8041e434`/`0x8041e46c`).

  **`grModelMachine2` shape** (read at `*(JOBJSet **)grmodel`): an array of 3 JOBJSet pointers (terminated by NULL at index 3) plus extra fields at +0x10..+0x14 (anim_joint, matanim_joint pointers shared with `grModelMotionMachine2`). Each JOBJSet has the layout `(JObjDesc *jobj, int n_joints, int n_dobjs, int n_mobjs)` — NOT the typedef in `obj.h` (which has 4 pointers). The other fields are counts, not pointers, so `JObj_LoadSet_SetPri` must be called with `is_add_anim=0` to avoid dereferencing them. `grmodel[0]` = main 122-joint stage tree; `grmodel[1]` = smaller tree (likely cannon-bearing — root JObj has flags 0x10040008 with a DObj at +0x10); `grmodel[2]` = lights/cameras region.

  **Path forward.** Three options ordered by tractability:
  1. **Author a custom cannon mesh from scratch** — a few-joint HSD `JObjDesc` tree literal in mod memory (cylinders for barrels). Pass via `param[+0x04]` so the framework's `GrYaku_AllocJObj` does its job. Bypasses GrMachine2 entirely. Smallest memory cost. Placeholder visual.
  2. **Build a stripped `GrMachine2_CannonOnly.dat` offline** via HSDArc tooling (or equivalent). Load that ~50-100KB archive in CT instead of the full 1.6MB. Real cannon visual, fits in CT's heap.
  3. **Runtime extract + free** — load Model.dat, walk into `grmodel[1]`, copy the cannon JObj subtree to a smaller mod-owned buffer, then `Archive_Free` the originals. Recovers ~1.6MB. Hardest because we'd need to identify the cannon subtree's exact extent (joint range, DObj/MObj/PObj/material chains, texture references) and copy the entire reachability set. Production-quality version of (2), no offline tooling needed.
