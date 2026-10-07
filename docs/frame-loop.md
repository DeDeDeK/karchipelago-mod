# Frame Loop

What runs every frame, in what order, and which pauses stop it. `updateFunction` (0x800067a4)
is the body of the game's main loop (`loop`, 0x80006b58): one call runs one or more logic ticks
and then renders once. hoshi's two frame hooks sit inside the tick loop, and the pause system
decides tick by tick which GObj procs run. Where a mod puts its per-frame work therefore decides
which scenes it runs in and which pauses freeze it.

## One tick

The tick loop runs from 0x80006828 to the `blt` at 0x80006a88. Each tick, in order:

1. Pad input is consumed (`PadAlarm_ConsumePad`), and the debug hotkeys are checked when the
   debug level is 3 or more.
2. **`OnFrameStart`** - hoshi's hook at 0x80006844. It runs on every tick, in every scene,
   through every pause.
3. While `HSD_Update.is_running` is set and the engine-speed gate (`zz_80062a68`) lets the tick
   through: `EngineSpeed_Apply`, then the minor's `cb_ThinkPreGObjProc` (called at
   0x800068bc).
4. The pause bookkeeping turns `HSD_Update.pause_kind` into `plink_blacklist_req`, the OR of
   `stc_pause_plink_blacklists[kind]` (0x80494f68) over every set pause kind. `is_running` is
   cleared while `PAUSEKIND_SYS` is set without a frame advance, and the blacklist is then all
   ones. A tick the engine-speed gate drops also blacklists everything.
5. **`GObj_UpdateAll`** (called at 0x80006a10) runs every GObj proc whose p_link is not
   blacklisted, by priority from 0 to 23.
6. While `is_running` is set and the engine-speed gate passed: **`OnFrameEnd`** - hoshi's hook
   at 0x80006a30 - then `HSD_Update.engine_frames` increments and the minor's
   `cb_ThinkPostGObjProc` runs.
7. The minor's `cb_ThinkPostGObjProc2`, every tick.
8. `Debug_RecordRngSeed` (0x80098988), every tick in every scene.

After the last tick: `GXInvalidateVtxCache` and `GXInvalidateTexAll` (called at 0x80006a94 and
0x80006a98), the minor's `cb_ThinkPreRender`, the render (`HSD_StartRender`,
`HSD_UpdateAllCObjs`), and `cb_ThinkPostRender`.

The cache invalidation runs every frame between the logic and the render. Code that re-points a
texture or rewrites vertex data during the ticks needs no flush of its own.

## hoshi's frame hooks

| Hook | Site | Scenes | Runs through |
|---|---|---|---|
| `OnFrameStart` | 0x80006844 | Every scene, boot to shutdown | Everything, including the debug pause |
| `OnFrameEnd` | 0x80006a30 | Every scene, boot to shutdown | Everything but the debug pause and dropped ticks |

Neither hook knows the mode, the scene or the options. A handler on one runs in the menus, the
title screen's attract demo, every mode and every pause it is not excluded from, so it must
either need that reach or test for where it is.

`OnFrameStart` is the only point that runs in every scene before any game logic of the tick,
which is why it is where a mod reads what the AP client writes into memory: no game function
runs at the moment the client writes, and the client writes in menus too. It is also the place
for work that must happen outside every GObj proc.

`stc_hsd_update->engine_frames` advances once per tick that ran `OnFrameEnd`. Comparing it to a
stored value tells a proc whether a new tick has started, with no flag passed between a proc
and a hook.

## Pause kinds and p_links

`HSD_Update.pause_kind` is a bitmask of `1 << PauseKind`. Each kind freezes the p_links set in
its `stc_pause_plink_blacklists` entry (bit `1 << p_link`):

| Kind | Set by | Frozen p_links |
|---|---|---|
| `PAUSEKIND_SYS` | Debug pause (Z frame-advances) | All - `is_running` is cleared |
| `PAUSEKIND_GAME` | Match pause | 1-17, 22, 24-26, 29-32 |
| `PAUSEKIND_2` | - | 1-10, 12-17, 22, 24, 26, 29-32 |
| `PAUSEKIND_MATCHEND` | Match end | 1-17, 22, 24, 26, 29-32 |
| `PAUSEKIND_EXPLODE` | Machine explosion, legendary piece pickup hitstop | 1-17, 22, 24-26 |
| 5-8 | - | None |

So a proc's p_link decides which pauses freeze it:

- **p_link 0** (`GAMEPLINK_SYS`) is in no entry. It runs through every pause but the debug one,
  which suits HUD that must keep updating while the match is paused.
- **p_link 1** (`GAMEPLINK_1`) is in every non-empty entry. It freezes with the match pause,
  match end and the hitstops, along with items, machines, riders and the stage. Anything that
  moves the world belongs there, or it keeps moving while the rest of the game stands still.

A proc on p_link 1 needs no pause check of its own - testing `PAUSEKIND_GAME` by hand only
repeats what the blacklist already does.

## Proc priority

`GObj_UpdateAll` runs procs by priority, 0 first. `stc_gobj_init_data->proc_pri_max` is 23, the
last priority it runs. The engine's own procs that a mod most often races:

| Priority | Proc |
|---|---|
| 0 | City Trial event procs |
| 4 | `Machine_PhysicsThink` (0x801c6368), `CityItem_PhysicsThink` |
| 5 | `Machine_EnvCollThink` and its `mpColl_Update` (0x80245f70), the item ground snap |
| 6 | Model-matrix appliers: `Machine_ApplyModelMatrix` (0x801c9074), `Rider_ModelMatrixThink` (0x8018f79c), the item matrix proc (0x8024f848) |
| 13 | `PlyCamGObj_Think` |

A write that must win over physics and collision - a forced position, a clamped velocity - goes
in a proc at priority 23, after all of them, rather than in a hook on each physics routine.

## GObj lifetime

Every GObj is freed with the scene's heap at the next scene change, without its destructor
running. A GObj created at `On3DLoadEnd` (or `OnTopRideLoadEnd`) therefore exists for exactly
one round: it needs no stale-pointer check, no reset when the mode changes, and no teardown -
unless it holds something outside the heap, such as a playing sound, which has to be released
before the scene exits. Creating it only in the mode, and with the options, that use it keeps
the per-frame cost and the enabled checks out of every other scene.

A proc may destroy its own GObj. `GObj_UpdateAll` records the GObj it is running at 0x805de324,
and `GObj_Destroy` (0x80428f64) only flags that one; the free happens once its proc returns. A
GObj that should exist only while there is work - a queue that empties - can create itself on
demand and destroy itself when idle, as long as the mod clears its pointer at the scene change.

## One-shot procs

Work that only has to run off the caller's stack - a machine swap asked for from inside the
machine's own collision - needs no frame hook. A proc added to a GObj that outlives the work
(the rider, for a machine swap) runs on that GObj's next update, after the current proc returns,
and frees itself with `GObj_FreeProc(*stc_gobjproc_cur)` (0x8042898c): `GObj_UpdateAll` defers
freeing the running proc until its callback returns. It inherits the GObj's p_link, so it waits
out the pauses that freeze the GObj, and a proc still pending at a scene change is freed with the
GObj; only state the mod keeps beside it needs clearing there.
