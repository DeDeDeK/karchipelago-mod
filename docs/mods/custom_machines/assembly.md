# Custom Machines: Assembly and Mounting

How the `custom_machines` mod puts a player onto a registered machine. A machine whose descriptor
names a cutscene archive can go through the vanilla legendary assembly - the world freeze, the
scripted camera, the parts flying in on streaks of light, the rider coming out mounted - with its
own art, through `StartAssembly(kind, ply)`. Where the cutscene cannot run, `MountMachine(kind,
ply)` puts the player straight on with no presentation. Both record the kind so a consumer's
respawn logic can keep the player on it.

## The vanilla cutscene in brief

`LegendaryMachine_StartAssembly` (0x80283cf0) runs the cutscene that plays when a player collects
the third Dragoon or Hydra piece. `LegendaryMachine_CreateAssembly` (0x802838a0) loads
`VsDragoon.dat` or `VsHydra.dat` by machine index and builds a controller GObj, and
`LegendaryMachine_AssemblyThink` (0x802839b8) runs it: a 28-frame lead-in, a 150-frame animation,
and a 3-frame close that frees the archive. At the start of the animation
`Ply_EnterLegendaryAssembly` poses the rider and stages an `(is_bike, class slot)` pair in
`RiderData.respawn_is_bike` / `respawn_class_slot`; the pose's motion script later fires
`Rider_RespawnFullRecreate` (0x80193900) on that pair, which is the mount. Every decision that
depends on which machine is assembling is reachable at a `bl`.

## The seams

| Site | Vanilla call | Replacement |
|---|---|---|
| 0x80262be8, the tail of `Preload_AllCityFiles` (0x80262ba4) | `LegendaryMachine_PreloadAssemblyArchives` queues the two vanilla archives | calls it, then on `GR_CITY1` queues every registered machine's cutscene archive with the same `Preload_CreateEntry` arguments |
| 0x80283914 in `LegendaryMachine_CreateAssembly` | `LegendaryMachine_LoadAssemblyArchive` returns the vanilla `vsData` | for a registered machine's run, loads its archive with `lbLoadArchive` and returns its public |
| 0x80283b70 in `LegendaryMachine_AssemblyThink` | `Ply_EnterLegendaryAssembly` | calls it, then overwrites the staged pair with the machine's own - either class - and records the kind as the player's respawn kind |
| 0x80283c98 in `LegendaryMachine_AssemblyThink`, phase 3 | `LegendaryMachine_FreeAssemblyArchive` | for a registered machine's run, frees its archive and clears the latch |

A latch names the registered machine whose run is up; while it is clear every replacement defers
to vanilla, so piece-collected Dragoon and Hydra runs are untouched. The latch clears at the free
rather than at the mount, so every seam downstream of the load still sees the run. The load
replacement is always installed (it also marks vanilla runs, below); the other three only when at
least one machine has a working cutscene.

## The archive

A machine's cutscene archive has the shape of `VsDragoon.dat` and `VsHydra.dat`: one public, a
`LegendaryAssemblyData` of three pointers - a glow-model triple `{JOBJDesc*, FigaTree*,
MatAnimJoint*}` (the streaks the parts ride in on, which anchors the camera and whose FigaTree sets
the 150-frame length), a parts-model triple of the same shape, and a pointer to a word holding the
camera-animation descriptor. Everything the run draws comes out of that archive, so how it is
authored is the machine's own affair. `cine_machine_index` picks which vanilla legendary the run
borrows its rider pose, motion script, fanfare and sky preset from; nothing else reads it.

`lbLoadArchive` (0x80059a20) asserts on a public it cannot find, and a run loads its archive
synchronously mid-round, so the archive has to be sound and resident before any run. At boot each
machine's archive is checked: `DVDConvertPathToEntrynum` confirms it is on the disc, and
`Archive_LoadFile` loads it inside an arena mark and release while `Archive_GetPublicAddress`
looks for `cine_symbol`. A machine that fails has its cutscene disabled and is reported. The
preload seam is what makes a sound archive resident; City Trial is the only ground the vanilla
call queues for, and the only scene a run is allowed in.

## When StartAssembly refuses

`StartAssembly` returns 0, leaving what the player gets instead to the caller, unless all of these
hold:

- **The kind has a cutscene.** A registered machine with one that passed the boot check, or
  `VCKIND_DRAGOON` / `VCKIND_HYDRA`, which run the engine's own archives with every condition
  below applied.
- **The scene is the open City Trial map, and not the title demo.** The cutscene stages its models
  there and drives that scene's sky and area lights; a stadium or race dereferences a NULL joint or
  trips the area-light assert. The attract demo is a real City Trial round inside `MJRKIND_TITLE`,
  where a cutscene would take over the title loop (`Gm_IsAutoDemo`).
- **The player has a machine and a rider, and the rider is Kirby.**
  `RiderGObj_EnterLegendaryAssembly` (0x8019248c) is Kirby-only and the mount rides on the state it
  enters, so any other rider would get the whole shot and no machine.
- **Nothing is running**, by the latch or by `Gm_IsLegendaryAssembling` (0x8000c934). The engine
  holds a single controller GObj, and a second run would tear down the first one's models.
- **The same cutscene has not run this scene.** The engine frees the archive when a run ends but
  `Preload_GetArchiveByFilename` keeps handing back the freed block, so a second run would load a
  joint out of freed memory and crash in `JObj_LoadJoint`. A bit per cutscene - one per vanilla
  legendary, one per registered machine - is set when a run starts. The load replacement sets a
  vanilla legendary's bit on every run, so one assembled from collected pieces also refuses a
  later `StartAssembly` of the same machine; the reverse order, `StartAssembly` then the same
  legendary's pieces, is the engine's own path and is not refused.

The latch, the archive handle and the bits clear at `On3DLoadStart`, since a scene change tears
the cutscene down without its last phase. A run started for a registered machine also clears the
machine's ColAnims first, as the vanilla pickup does, so no flash survives into the freeze.

## The plain mount

`MountMachine(kind, ply)` takes any kind under the ceiling, vanilla included, and mounts through
the same `Rider_RespawnFullRecreate` the cutscene ends in. The recreate destroys the rider's
current machine GObj, and a caller inside `Machine_OnTouchItem` or a collision callback is still
running on that machine, so the mount cannot happen where it is asked for. The kind is recorded per
player and a one-shot proc is added to the rider's GObj at `RDPRI_ANIM`; it runs the recreate on
the rider's next update and frees itself with `GObj_FreeProc` (0x8042898c), which defers freeing a
running proc until it returns. That is where the engine runs its own:
`RiderState_LegendaryAssemblyAnimThink` (0x801bdb2c) fires the recreate from the rider's anim
proc. The proc is on the rider's p_link, so a pause or hitstop that freezes the rider holds the
mount until it runs. A second mount before the proc runs replaces the kind instead of adding a
proc. A player with no rider is refused at the call, and a mount still owed at a scene change is
dropped with the rider GObj its proc sat on.

## The respawn kind

`GetRespawnKind(ply)` answers the kind of the player's last plain mount or registered machine's
cutscene mount this scene, or -1; every scene change clears it. The engine keeps no such record:
a respawn goes through `Rider_ResetStartingMachine` (0x80195288), which hardcodes the Compact Star,
and `RiderData.machine_kind` (`+0x10`) is the class-relative slot written at spawn and never
refreshed. A consumer that replaces the respawn assignment reads this first so a mounted machine
survives a KO.
