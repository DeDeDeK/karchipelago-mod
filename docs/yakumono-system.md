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

`grInitYakumono` checks `(&PTR_PTR_804a322c)[grobj->gr_kind].fn_at_4` and calls it before iterating `entries[]`. For City Trial (`GRKIND_CITY1`) this hook is `grDataCity1_CreateYakumono` (0x8010f268), which manually invokes 31 per-instance creators (data_idx 0..30), each hardcoding its own descriptor id. This bypass keeps the City Trial spawn order deterministic and well-known, instead of relying on the data file's `entries[]` order.

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
   - `GrYaku_AllocJObj` — JObj allocator (sets `ydata->[+0x64]` JObj root)
   - `ydata->[+0x70] = 0` — zero a status field
   - `GrYaku_InitMatrix` — JObj matrix setup
   - `GrYaku_AttachModel` — model attach (uses `grdata->lights` for lighting)
   - `GrYaku_InitAudio` — audio source/track alloc (from stage's `pos_data` block)
   - `GrYaku_AttachAnim` — anim attach (from stage's `motion` block)
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
- Looks up `stc_yaku_descs[desc_id]` (with `desc_id < 70` and non-NULL guard) and stores `desc->[+0x0]` into `ydata->[+0x84]` — this is the kind descriptor pointer used by per-frame helpers.

## YakumonoData layout

YakumonoData is the single user-data slot stored at `gobj->user_data[0]` (offset +0x2c on GObj). All per-frame procs read it as `ydata = *(int *)(gobj + 0x2c)`. Known offsets:

| Offset | Type | Meaning |
|---:|---|---|
| 0x00 | `GOBJ *` | Back-reference to owning GObj |
| 0x04 | `int` | `desc_id` — descriptor index (passed to Create) |
| 0x08 | `void *` | `data_ptr` — per-instance param block (from `data_array[data_idx]`); `gyp->param` in source |
| 0x10 | — | (unread in lifecycle) |
| 0x1c | `Vec3` | Position (`+0x1c`..`+0x28`) — 3 floats |
| 0x40 | `Mat3x4?` | Local transform (used by `+0x4c` matrix copy) |
| 0x64 | `JObj *` | Root JObj (model tree), set by init pipeline |
| 0x70 | `int` | Status flags (set to 0 in Create) |
| 0x74 | `int` | **`state`** — current state-machine state (read by `GrYakumono_GetState`); `-1` initially |
| 0x78 | `int` | Previous anim id (?) |
| 0x7c | `int` | Previous joint id (?) |
| 0x80 | `int` | Anim flags (?) |
| 0x84 | `void *` | Active kind descriptor pointer (`desc->[+0x0]`) |
| 0x88 | `Vec3` | Right/forward axis (init `(0,0,1)`) |
| 0x94 | `Vec3` | Up axis (init `(0,0,1)`) |
| 0xA0 | `int` | (set 0 in InitData) |
| 0xA4 | `float` | **`scale`** — `Gr_DefaultScale = 1.0f`; asserted equal to default in many checks |
| 0xAC | `float` | (init `1.0f`) |
| 0xB0 | `int` | (init `5`) |
| 0xB4 | `int` | (init `0`) |
| 0xB8 | `int` | (init `0`) |
| 0xE0 | `Vec3` | BBox / center (init from constants in `GrYaku_FinalSetup`) |
| 0xEC | `HurtData *` | **HurtData pointer** — read by `GrYaku_GetHurtData` (`0x800f8248`); same offset as `EnemyData.xec`. The map/Ghidra symbol at this address is now correctly `GrYaku_GetHurtData` (a prior `Enemy_GetHitColl` label was a misnomer — it reads `YakumonoData+0xEC`, not `EnemyData`). See `hurtdata-system.md`, which still carries the old `Enemy_GetHitColl` note. |
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
| 0x118 | `int` | Audio anim event handle |
| 0x11C | `int` | Audio loop count |
| 0x120 | `AudioTrack` | Allocated audio track |
| 0x124 | `AudioSource` | Allocated audio source |
| 0x12C | `byte` | Flag byte (bits 3, 4, 6, 7 cleared on init) |
| 0x130 | `int` | (used by post-init helpers, e.g. `zz_800fe60c_`) |
| 0x134 | `int` | (used by post-init helpers) |
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

`grDataCity1_CreateYakumono` (`0x8010f268`) makes 31 calls (data_idx 0..30) and CT's `yakumono->entries[]` is empty (`entry_count = 0`), so the generic walker contributes nothing — CT loads exactly 31 yakumono instances. **`data_array` actually has 33 slots, however** (`data_count = 33`), so slots **31 and 32 are spare** — they're allocated but no vanilla creator references them. Mod code can repoint them to a custom `YakumonoParam` block and spawn a 32nd or 33rd yakumono (the cannon, for example) without disturbing any vanilla slot. The hardcoded descriptor ids:

| data_idx | helper | desc_id | Notes |
|---:|---|---:|---|
| 0 | `0x800fa2a0` | 17 | |
| 1 | `0x800fa610` | 18 | |
| 2..17 | `0x800fe5d4` | 46 | 16 instances of the same descriptor (likely factory chimneys / stacks) |
| 18..19 | `0x80109db4` | 61 | |
| 20 | `Lighthouse_Create` | 68 | Lighthouse |
| 21 | `whispyLogic` | 69 | WhispyWoods (Forest event) |
| 22..23 | `0x80106824` | 32 | |
| 24 | `0x80108f10` | 37 | |
| 25 | `0x80108ce8` | 36 | |
| 26 | `0x80109138` | 38 | |
| 27 | `0x80107bfc` | 33 | |
| 28 | `0x80107d64` | 34 | |
| 29 | `0x80107ecc` | 35 | |
| 30 | `0x801043c8` | 29 | |

City Trial therefore uses 14 distinct descriptor ids: **17, 18, 29, 32, 33, 34, 35, 36, 37, 38, 46, 61, 68, 69**. Of the 70 descriptors, the rest are referenced by Air Ride / Top Ride stage creators (not yet enumerated).

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
- **Position table**: `grdata->yakumono_pos` (at **GrData+0x20**, declared `yakumono_pos` in `stage.h`) carries a sub-block at `[+0x2C]` whose `[+0x8]` is the position-record count. `grGetYakumonoposNum` reads this chain; `loadYakumonoLocations?` loads the records. (This is a distinct field from the `pos_data` member at GrData+0x18.)
- **Stage preload**: `Yakumono_Preload` calls `stGetStageKind_3Cyakumono?` (which dereferences the stage table at offset `0x3C`) and, if non-NULL, invokes the GX preloader at `0x80072c90` with shape parameters `(2, 4, 4, 0, 1, 8, 16)` — a small fixed-size preallocation for yakumono GX state.
- **Counter**: `stc_grobj->[+0x6fc]` is the live yakumono count (incremented on every `GrYaku_Create`).
- **Index array**: `stc_grobj->[+0x710]` is the per-stage array of YakumonoData pointers, allocated by `grInitYakumono` at `num*4` bytes and filled as each yakumono spawns. This lets the framework iterate "all currently-loaded yakumono" in O(num) without walking the GObj list. **`num` here is `entry_count` (length of `entries[]`), not `data_count`** — stages that use only the per-grkind hook path (City Trial: `entry_count = 0`) end up with this slot **NULL**, since `HSD_MemAlloc(0 * 4)` returns NULL. Per-frame procs do not depend on the array, so a NULL value here is harmless; only mod code that wants to enumerate live yakumono needs to handle that case.

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

## Mod implications

For KARchipelago, the yakumono system is relevant for:

1. **Drop gating** — already covered by `item_spawn_filter.c` filtering `event_source_drop[].chance_destructible`.
2. **Damage-source attribution** — when a player breaks a star pole, event pillar, or volcano wall, the credit goes through the HitColl pipeline at proc 9. To gate yakumono damage (e.g. for a "no breakables" challenge), hook `GrYakumono_Proc9_HitColl` (priority 9 stage HitColl) or the per-kind `damage-on` callback at `+0x100`.
3. **Counting yakumono interactions** — `stc_grobj->[+0x6fc]` tells you the live count. The per-kind `damage-on` callback fires once per damage event, which is the natural extension point for "X yakumono broken" location checks. The **vanilla** break-count path is already wired up: every break-family drop handler (`GrYakuBreakRock_DropItems`, `GrYakuBreakHouse_DropItems`, `GrYakuBreakCoral_DropItems`, `hitBigStar`, `hitWeakObject`) calls `GrYaku_IncrementBreakCount` (`0x80105d80`), which extracts the broken yakumono's `desc_id` via `GrYakumono_GetDescId` and bumps a per-`desc_id` byte counter in the player struct (`Ply_IncrementYakumonoBreakCount`, `0x8022fed8`, counters at PlayerData `+0x62b+desc_id`). This counter array is what the checklist's "break N of object X" cells read — see `checklist-stat-tracking.md` ("The yakumono-break bucket array (`+0x62b`)").
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

These are referenced by the per-grkind hook path (e.g. `grDataCity1_CreateYakumono`). They have a **different layout** with the function pointer at **+0x08** instead of +0x1c, and they embed source-file/assertion strings starting at +0x1c:

| Offset | Type | Value |
|---:|---|---|
| +0x00 | `void *` | back-pointer (vestigial, as above) |
| +0x04 | — | zero |
| +0x08 | `void(*)(...)` | per-kind init/check function pointer |
| +0x0c..0x18 | — | zeros |
| +0x1c | `char[]` | source filename (e.g. `"gryakudownforcezone.c"`, null-terminated) |
| (after) | `char[]` | assertion expression (e.g. `"gyp->kind == Gr_YakuKind_DownForceZone"`) |

Block size varies: the strings determine the size (40 to ~136 bytes per block). `grDataCity1_CreateYakumono` and the other named per-instance creators hardcode the `desc_id` literal that maps to one of these blocks.

### How `yd->desc_subblock` (+0x84) is used

`GrYaku_InitData` stores `*(stc_yaku_descs[desc_id])` — i.e. the back-pointer field — into `yd[+0x84]`. For paired descriptors this lands on a zero pad; for per-instance descriptors it lands on a kind-specific data block 16 bytes before the descriptor proper. Per-frame helpers reading `yd[+0x84]` therefore see different "context" depending on path. The full schema for what those preceding regions encode is still opaque — they are mostly zeros at rest and presumably populated by the per-kind init.

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

The generic `entries[].kind` value is therefore in 0..15 with bit 0 acting as the "ctrl" flag and bits 1..3 selecting the base kind. **This is a separate enum from `YakuKind`** — `YakuKind` covers all per-kind handlers (rising-cube, lasergate, pillar, …, the BREAK family, the HP-coll family, lighthouse, whispywoods), while the 0..15 generic-dispatch enum is just the eight base behaviors that the framework's stock walker can spawn from `Yakumono.dat`. The mapping from base-kind 0..7 to the YakuKind names of the four documented "with-Ctrl" pairs (RisingCube, PushOutWall, LaserGate, Pillar) is not yet pinned down by static data — the four extra base kinds presumably correspond to other paired or zone-style kinds.

## Per-grkind hook table (`PTR_PTR_804a322c`)

The table is 28 entries (`gr_kind` 0..27, indexing into `GroundKind`). Each entry is a pointer to a per-grkind block whose `+0x04` slot is the optional init hook called by `grInitYakumono`. Block size and layout vary; what matters for dispatch is whether `+0x04` is non-NULL.

Of the 28 entries:

- **15 grkinds have a real init hook** (do explicit per-instance spawns before the `entries[]` walker runs): 0, 1, 2, 3, 4, 5, 7, 8, **9 (= GRKIND_CITY1)**, 10, 15, 19, 21, 26, 27.
- **1 grkind has a 4-byte stub hook** (`blr` only — same effect as NULL): 17 (= `GRKIND_KIRBYMELEE1`, fn `0x8010faac`, size 4).
- **12 grkinds have NULL hooks** (rely entirely on the generic `entries[]` walker): 6, 11, 12, 13, 14, 16, 18, 20, 22, 23, 24, 25.

Translating the indices via `stage.h`'s GroundKind enum (which has `GRKIND_CITY1 = 9`):

| `gr_kind` | GroundKind | Init hook | Notes |
|---:|---|---|---|
| 0 | Air Ride course | hook `0x8010e09c` (block 0x804a7528, size 0x10) | minimal block — embedded source strings live in the *next* block |
| 1 | Air Ride course | hook `0x8010e1c8` (block 0x804a7538, size 0x88) | source `grheat2.c` (Magma Flows) |
| 2 | Air Ride course | hook `0x8010e378` (block 0x804a75c0, size 0x40) | source `grdesert1.c` (Sky Sands) |
| 3 | Air Ride course | hook `0x8010e5dc` (block 0x804a7600, size 0x10) | minimal block |
| 4 | Air Ride course | hook `0x8010e87c` (block 0x804a7610, size 0x88) | source `grvalley2.c` |
| **5** | **Air Ride course (Machine Passage)** | **hook `0x8010ea40` (block 0x804a7698, size 0x88)** | **source `grmachine2.c` — calls `GrYakuCannon_Create` (data_idx=1) plus 4 more per-instance creators. This is the *only* Air Ride stage with a cannon yakumono.** |
| 6 | Air Ride course | NULL hook (block 0x804a7720, size 0x10) | this stage relies entirely on the generic `entries[]` walker |
| 7 | Air Ride course | hook `0x8010ec90` (block 0x804a7730, size 0x40) | source `grsky2.c` |
| 8 | Air Ride course | hook `0x8010edf8` (block 0x804a7770) | source `grice1.c` |
| 9 | `GRKIND_CITY1` | `grDataCity1_CreateYakumono` (`0x8010f268`) | spawns 31 explicit per-instance yakumono; **does NOT call `GrYakuCannon_Create`** |
| 10 | `GRKIND_DRAG1` | `0x8010f880` (real) | Top Ride course 1 has explicit yakumono |
| 11..13 | `GRKIND_DRAG2..4` | NULL | other Top Ride courses use only the generic walker (or have empty entries) |
| 14 | `GRKIND_AIRGLIDER` | NULL | |
| 15 | `GRKIND_TARGETFLIGHT` | `0x8010fa0c` (real) | |
| 16 | `GRKIND_HIGHJUMP` | NULL | |
| 17 | `GRKIND_KIRBYMELEE1` | `0x8010faac` (4-byte stub) | functionally NULL |
| 18 | `GRKIND_KIRBYMELEE2` | NULL | |
| 19..23 | `GRKIND_DESTRUCTIONDERBY1..5` | DD1, DD3 real; DD2, DD4, DD5 NULL | |
| 24..25 | `GRKIND_SINGLERACE1..2` | NULL | |
| 26 | `GRKIND_SINGLERACE3` | `0x8010fbd4` (real) | |
| 27 | `GRKIND_SINGLERACE4` | `0x8010ff08` (large, 0x1dc bytes) | also calls `GrYakuCannon_Create` (data_idx=18) — second of the only two cannon callers in the game |

**`GroundKind` ≠ `AirRideCourse`.** The `AirRideCourse` enum in `stage.h` is the in-game course-selection index used in menus and `GameData` (`MACHINE_PASSAGE = 6`), but the internal `GroundKind` for that same stage is **5**. The two enums share the "0..8 = Air Ride courses" range but use different orderings. When indexing the per-grkind hook table at `0x804a322c`, always use `GroundKind`, never `AirRideCourse`.

GroundKinds **28..33** (`GRKIND_SINGLERACE5..9`, `GRKIND_VSKINGDEDEDE`) are **not in the table** — the table truncates at 28 entries while `GRKIND_NUM = 34`. This means `grInitYakumono` must not be called for those gr_kinds (or the lookup is bounded elsewhere). VSKINGDEDEDE is plausibly a special boss arena that doesn't go through `grLoadStage`'s yakumono path at all.

## `gyp->fgm` substruct

Source-level field name confirmed by two assertion strings in the binary:

- `0x804a6fc4`: `"0 <= fgmId && fgmId < gyp->fgm.idDataNum"` (referenced from `gryakubreakcoll.c`)
- `0x804a7130`: `"0 <= fgmId && fgmId < gyp->fgm.idDataNum"` (referenced from `gryakubreakhpcoll.c`)

So `YakumonoData` includes a substruct `fgm` containing at least `int idDataNum` (count of FGM ids) plus an array of FGM ids. The substruct is only consumed by the BREAK and BREAK-HP-COLL families — other yakumono kinds never read `gyp->fgm`.

The actual offset is **not directly visible** in the optimized binary because the bounds check has been dead-code-eliminated under `-O1` (the call to `__assert(..., "0 <= fgmId && fgmId < gyp->fgm.idDataNum")` is unreachable: the surrounding `cmpwi/ble/bgt` triplet always branches around it). What *is* visible from `hitWeakObject` (`0x80107914`, the relevant tail-access code sits around `+0x128`/`0x80107a3c`) and `zz_801080ec_` is the contiguous tail of YakumonoData used by the break families:

| Offset | Used as |
|---:|---|
| +0x118 | audio anim event handle (existing) |
| +0x11c | audio loop count (existing) |
| +0x120 | audio track (existing) |
| +0x124 | audio source (existing) |
| +0x130 | pointer to per-region audio handle array (one per HitColl region) |
| +0x134 | pointer to per-region damage/HP state array (BREAK-coll), or counter (BREAK-hp-coll) |
| +0x138, +0x13c | transient model+gobj handles for the current region (overwritten per iteration) |
| +0x140 | per-stage audio handle counter (BREAK-hp-coll only, decremented as handles are consumed) |
| +0x148, +0x14c | transient model+gobj handles (BREAK-hp-coll variant slots) |
| +0x150 | pointer to per-region 4-byte audio source allocations (BREAK-coll) |
| +0x160 | same idea for BREAK-hp-coll |

So `YakumonoData` extends to **at least +0x18c**: independently of the break families, `GrYakuCannon_TailInit` (`0x800fed48`) zeros a contiguous run of ydata words from `+0x130` through `+0x188` (the cannon's `userInfo[]`/local-context slots), so the struct cannot end before `+0x18c`. (The `+0x164` figure derived from the break-family reads is a smaller lower bound; the earlier header estimate of `sizeof = 0x13c` is well short.) The `fgm` substruct most likely lives in the +0x118..+0x14c region (mixed with the audio fields, since "FGM" in this engine means SFX). The exact `idDataNum` offset and the ID-array layout are still not pinned, but the substruct boundaries are now bracketed.

## Spawning yakumono in stages they don't normally appear in

> **Status: dormant RE scaffolding.** The cannon investigation below lives in `mods/custom_events/src/cannon_event.c`. It is **not** a registered custom event (it does not appear in the `CustomEventsAPI` event tables) and is gated off by default (`CANNON_LOAD_ENABLED = 0`; the spawn path runs only when `CANNON_SPAWN_ENABLED` is built in). Per CLAUDE.md, future cannon RE findings belong in this doc. The findings here are correct as recorded but no production code path spawns a cannon in City Trial.

Empirically confirmed (RE scaffolding in `mods/custom_events/src/cannon_event.c`, build-gated by `CANNON_SPAWN_ENABLED`; dormant by default — see status note above): you can call `GrYaku_Create(48, data_idx)` + `GrYakuCannon_TailInit(gobj)` from `On3DLoadEnd` in City Trial — well after `grInitYakumono` has finished — and the framework runs without asserting, even though City Trial's per-grkind hook never spawns a cannon. The yakumono counter (`(*stc_grobj)[+0x6fc]`) increments by 1 per call, so the GObj is fully wired and registered.

But "runs without asserting" is not the same as "fully spawned." The Create init pipeline silently skips graphical setup when the param block doesn't supply the data those steps need. With a zeroed param block, the post-spawn ydata looks like this:

| Slot | Status with zeroed param |
|---|---|
| `+0x000` gobj backref | populated |
| `+0x004` desc_id (48) | populated |
| `+0x008` data_ptr | populated (points at our block) |
| `+0x040..+0x06f` matrix | **all zero** (InitMatrix bailed) |
| `+0x064` JObj root | **NULL** (AllocJObj bailed) |
| `+0x074` state | 0 (tail-init's `Gr_StateChange` ran) |
| `+0x084` desc_subblock | populated (descriptor back-pointer target) |
| `+0x088..+0x09c` axis vectors | populated (1.0 entries) |
| `+0x0a4` scale | 1.0 |
| `+0x0e8..` bbox | populated (FinalSetup ran) |
| `+0x0ec` hurt_data | **populated** (HurtData_Create ran) |
| `+0x0f0` proc1 | `0x800fee40` (auto-installed from descriptor subblock) |
| `+0x0f4..+0x108` proc2..proc5, on/off_damage | NULL (cannon doesn't use them) |
| audio_track / audio_source | NULL (no per-stage audio events) |

The cannon ends up as a **ghost yakumono**: collidable (HurtData), state-machine-driven (proc1 ticking every frame), but invisible (no JObj, no model) and immobile (no matrix). The 7 priority procs are added unconditionally and tick even on a model-less yakumono — `GrYakumono_Think` and the HitColl pipeline both read from `hurt_data`, not from the JObj.

### Param-block gating in the framework's init pipeline

Two helpers in the init pipeline branch on fields inside the `YakumonoParam` block (`ydata->data_ptr`, equal to `grdata->yakumono->data_array[data_idx]`):

| Helper | Address | Gate | If non-zero | If zero |
|---|---|---|---|---|
| `GrYaku_AllocJObj` | `0x800f7308` | `data_ptr->[+0x04]` | full alloc with model joint data → writes JObj to `ydata+0x64` | empty no-op alloc, leaves `+0x64` as 0 |
| `GrYaku_AttachModel` | `0x800f6274` | `data_ptr->[+0x0c]` | reads `grobj+0x54` / `grobj+0x0c` (lights & motion), attaches model to `ydata+0x64` | falls into the no-model branch, only sets default position bytes at `ydata+0x1c` |

These gates are real, but they describe **whether the yakumono framework allocates its own attached JObj/model**, not whether the yakumono ends up visible. Many yakumono kinds — including the cannon — leave both gates zero and get their visible mesh from elsewhere. Always check the actual vanilla param block at runtime before assuming a kind needs framework-managed assets.

### Cannon: visible model lives in the stage's static scene graph

Confirmed by dumping vanilla Machine Passage's `data_array[1]` and the resulting ydata of both a vanilla cannon GObj and a duplicate spawn (RE scaffolding in `mods/custom_events/src/cannon_event.c`):

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

Each `trigger_desc` (0x20 bytes) holds `[self-back-ptr, 1, 0, 1, angle_float, 0, packed_joint_ref, 0]` where `packed_joint_ref = 0x000f00XX` with `XX` = stage-joint index for that barrel (observed: 5, 6, 8). Each `physics block` (0x20 bytes) holds `[count, kind, force, factor, scale, angle, factor, value]` — eject parameters per barrel.

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
| `proc1` callback (cannon's state machine) | No | works (auto-installed from descriptor) | — already works |
| Audio emitter (`AudioEmitter_UpdatePosition`) | Indirectly (via stage audio source) | works in MP, bare in CT | works in CT too once stage audio source is allocated |
| **Visible model (mesh + barrels)** | **Yes — lives in `GrMachine2Model.dat`** | **absent in CT** | **load+splice or author custom mesh** |
| **Anchor joints (referenced by `0x000f00XX`)** | **Yes — joints in stage scene graph** | **absent in CT** | **must exist for yakumono to position barrels correctly** |
| Position / matrix | Derived from anchor joints | unset (matrix all zero, but irrelevant — vanilla MP cannon also has unset matrix; positioning happens through joint refs) | works once anchor joints exist |

## Open questions (remaining)

- **Mapping the eight generic base kinds to YakuKind names**. We know there are 8 base kinds × 2 (ctrl) variants = 16 generic dispatch entries. RisingCube/PushOutWall/LaserGate/Pillar account for four of the paired YakuKinds — the other four base kinds need to be matched up, likely by tracing `Yakumono.dat`'s actual kind contents at runtime or by walking each tail-init's anim/state IDs.
- **`gyp->fgm` exact field offsets**. Bracketed to +0x118..+0x14c by the BREAK-family use; still need to confirm whether `fgm.idDataNum` lives at one of the existing audio offsets (treated as a substruct view onto +0x118..) or at a distinct field.
- **What lives in the back-pointer's preceding 16-byte slot** for the per-instance descriptors (16..69). At rest these are zeros; runtime population (if any) by the per-kind init code is unverified.
- **Cannon `YakumonoParam` full layout**. RE'd to ~`+0x80`: a metadata-block ptr at `+0x00`, framework gates at `+0x04`/`+0x0c` (both intentionally zero — see "Cannon: visible model lives in the stage's static scene graph"), and five repeating `(trigger_desc, physics_block)` pairs at 0x18 stride covering five barrels. Each trigger_desc encodes a stage joint index in a `0x000f00XX` packed value, each physics block holds eject force/angle/scale params. Cannon-specific fields beyond `+0x80` not yet inspected.
- **Anim-attach and audio gating fields**. `GrYaku_AttachAnim` and `GrYaku_InitAudio` follow the same "param-gate or skip" pattern as JObj/Model but the exact param offsets they read are not yet identified. Likely in the `+0x10..+0x20` region of `YakumonoParam`.
- **Cross-stage asset injection mechanism**. The yakumono framework is stage-agnostic; the visible model is stage geometry. Bringing the cannon's mesh into CT requires loading `GrMachine2Model.dat` and either splicing its cannon JObj subtree into CT's main scene graph, rendering it as a parallel scene graph, or authoring a substitute cannon mesh from scratch.

  **Heap-budget finding.** A naive `Archive_LoadFile("GrMachine2.dat")` + `Archive_LoadFile("GrMachine2Model.dat")` from CT's `On3DLoadEnd` succeeds — both archives load and `Archive_GetPublicAddress` resolves `grDataMachine2` (in stage.dat) and `grModelMachine2` / `grModelMotionMachine2` (in model.dat). **But there's no headroom left.** Stage.dat is ~207KB and Model.dat is ~1.6MB; after the loads heap 1 has ~30 bytes free. Any subsequent allocation (e.g. `JObj_LoadSet_SetPri` on `grmodel[0]` needs ~1KB) trips `assertion "addr" failed in initialize.c on line 397`. So loading the full Model archive in CT is non-viable.

  **Archive layout discovered.** `GrMachine2Model.dat` exposes 2 public symbols: `grModelMachine2` (the scene-models entry) and `grModelMotionMachine2` (the motion-anim entry). `GrMachine2.dat` exposes `grDataMachine2` plus 1 unresolved extern: `GrdMachine2_CannonSAN1_ACTION_Cannon1_animjoint` — the cannon's animated joint, which `Archive_LoadFile` does not auto-resolve against globally-loaded archives (so `*slot=0` even after model.dat is loaded; `grLoadStageArchive` does the post-parse extern resolution by walking `0x8041e434`/`0x8041e46c`).

  **`grModelMachine2` shape** (read at `*(JOBJSet **)grmodel`): an array of 3 JOBJSet pointers (terminated by NULL at index 3) plus extra fields at +0x10..+0x14 (anim_joint, matanim_joint pointers shared with `grModelMotionMachine2`). Each JOBJSet has the layout `(JObjDesc *jobj, int n_joints, int n_dobjs, int n_mobjs)` — NOT the typedef in `obj.h` (which has 4 pointers). The other fields are counts, not pointers, so `JObj_LoadSet_SetPri` must be called with `is_add_anim=0` to avoid dereferencing them. `grmodel[0]` = main 122-joint stage tree; `grmodel[1]` = smaller tree (likely cannon-bearing — root JObj has flags 0x10040008 with a DObj at +0x10); `grmodel[2]` = lights/cameras region.

  **Path forward.** Three options ordered by tractability:
  1. **Author a custom cannon mesh from scratch** — a few-joint HSD `JObjDesc` tree literal in mod memory (cylinders for barrels). Pass via `param[+0x04]` so the framework's `GrYaku_AllocJObj` does its job. Bypasses GrMachine2 entirely. Smallest memory cost. Placeholder visual.
  2. **Build a stripped `GrMachine2_CannonOnly.dat` offline** via HSDArc tooling (or equivalent). Load that ~50-100KB archive in CT instead of the full 1.6MB. Real cannon visual, fits in CT's heap.
  3. **Runtime extract + free** — load Model.dat, walk into `grmodel[1]`, copy the cannon JObj subtree to a smaller mod-owned buffer, then `Archive_Free` the originals. Recovers ~1.6MB. Hardest because we'd need to identify the cannon subtree's exact extent (joint range, DObj/MObj/PObj/material chains, texture references) and copy the entire reachability set. Production-quality version of (2), no offline tooling needed.
