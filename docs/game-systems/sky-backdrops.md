# Sky Backdrops

The backdrop is the 3D mesh around the playable area - the distant skyline, mountains, sky dome or
starfield. It is a JObj subtree of its own, separate from the terrain: Air Ride courses and City
Trial each ship one, and a stage that ships none simply fogs out at the edge. Fog colour, the sky
tint and the fade overlay are a separate system driven by the stage's sky presets; the backdrop is
only geometry. Because every stage archive stores its backdrop the same way, one stage's backdrop
can be grafted into another.

## Where a stage keeps it

`GrData.model_section` (+0x0c) points at a `ModelSection`: `terrain` at +0x00, `backdrop` at +0x04
and two more model slots. A NULL slot is legal and suppresses that subtree.

The `grModel<X>` public of every `Gr<X>Model.dat` is this struct (HSDLib's `KAR_grModel`). Its
slots do not point straight at a `JOBJDesc`: `terrain` points at a main-model record and
`backdrop` at a skybox record, each of which *leads* with its root `JOBJDesc *` (the main model
then carries joint, DObj and POBJ counts and a bounding record; the skybox a model-motion joint).
Because the root pointer is the first field, the loader reads each slot as a `JOBJDesc **` and
never looks further.

## Loading

`CreateStageModel_3D` (0x800dcbf0) instantiates each populated slot with `JObj_LoadJoint`. The
terrain joint goes onto the stage GObj; the backdrop joint is parked in `GrObj.backdrop_jobj`
(+0xf4) with no GObj of its own, and `Gr_Think` (0x800ce618) animates it. A NULL backdrop slot
leaves `+0xf4` NULL and skips the load.

Both root joints get the current stage's scale - `grGetStageScale` (0x800d3058), which returns
`StageNode.scale` (+0x08) - written into all three scale components, overwriting whatever scale
the `JOBJDesc` carried. So a backdrop grafted from another stage never keeps its own scale: it is
drawn at the host stage's scale (0.70 on City Trial), and its on-screen size follows its raw
geometry, which differs between stages by nearly an order of magnitude. Scaling the root joint
after that write is equivalent to scaling every vertex, since a backdrop is a rigid, unskinned
subtree under a uniform scale.

## The stage file table

`grLoadStageArchive` (0x800ce7a0) loads a stage from the 28-row table at 0x804a2ffc, indexed by
GroundKind, with five strings per row: the stage file (`GrCity1.dat`), its `grData<X>` symbol, the
model file (`GrCity1Model.dat`), the `grModel<X>` symbol and the `grModelMotion<X>` symbol.

| GroundKind | Ground | GroundKind | Ground |
|---|---|---|---|
| 0 | `GrPlants1` | 14 | `GrPasture1` |
| 1 | `GrHeat2` | 15 | `GrColosseum1` |
| 2 | `GrDesert1` | 16 | `GrColosseum3` |
| 3 | `GrCheck2` | 17 | `GrColosseum5` |
| 4 | `GrValley2` | 18 | `GrJump1` |
| 5 | `GrMachine2` | 19 | `GrJump2` |
| 6 | `GrSpace2` | 20 | `GrJump3` |
| 7 | `GrSky2` | 21 | `GrDedede1` |
| 8 | `GrIce1` | 22 | empty row, special-cased by the loader |
| 9 | `GrCity1` (City Trial) | 23 | `GrTest`, no backdrop |
| 10 | `GrZeroyon1` | 24 | `GrTest6`, no backdrop |
| 11 | `GrZeroyon3` | 25 | `GrTest7`, no backdrop |
| 12 | `GrZeroyon4` | 26 | `GrSimple`, a 4 KB placeholder backdrop |
| 13 | `GrZeroyon5` | 27 | `GrSimple2`, no backdrop |

GroundKind is the physical ground and is what `GrObj.gr_kind` holds. StageKind is the menu and
mode index (`stGetCurrentStageKind`, 0x80261ecc), read from `Stage.dat`'s `stData` rows, whose
first word is the GroundKind. The two coincide only at 0-2 and City Trial (9), and several
StageKinds share a ground: Destruction Derby 4 and 5 run on `GrCity1`. Code that cares which
geometry is loaded should test `gr_kind`.

## Grafting a backdrop

Rewriting `ModelSection.backdrop` inside `CreateStageModel_3D`, before it reads the model section
(at 0x800dcc18 `r30` holds the GrObj), makes the stock loader instantiate a foreign subtree as if
it were native, with no manual `JObj_LoadJoint` and no GX callback. Two constraints shape how to
get the subtree:

- **Memory.** Loading a donor's whole `Gr<X>Model.dat` does not fit on top of a round: City
  Trial already holds `GrCity1Model.dat` (2.8 MB) plus every machine, rider and item archive, the
  donors run 0.5-2.6 MB, and `GrSimpleModel.dat` is 14.5 MB. Reading just the subtree's byte ranges
  off the disc with `File_Read` and relocating its pointers costs a few hundred KB for most
  backdrops and about 1.2 MB for the largest.
- **Lifetime.** `Archive_LoadFile` and `Heap_Alloc(0, ...)` both allocate from the per-scene heap,
  which is reclaimed when the 3D scene exits. A graft is rebuilt every round, and nothing needs
  freeing. The stage's own `Gr<X>Model.dat`, though, can be a resident preload
  (`Preload_LoadStageFiles`, 0x800ce964, heap ALLM) that the archive loader reuses as-is in the
  next round, so a rewritten `ModelSection.backdrop` slot outlives the heap it points into and
  has to be put back before the scene exits. It is still read after the backdrop is built:
  `Gr_SetBackdropAnim` (0x800dbc2c), right after `CreateStageModel_3D`, reads the record's word 1
  as the backdrop's motion array and binds entry 0.
