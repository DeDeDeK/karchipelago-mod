# Sky Backdrop System

The backdrop is the 3D mesh that wraps around the playable area - distant city skyline,
mountains, sky dome, starfield. It lives in its own JObj sub-tree separate from the
playable terrain mesh: Air Ride courses populate one, City Trial populates one
(`grModelCity1[1]`, the city horizon beyond the streets), and some debug stages leave the
slot NULL and just fog out. Fog color, ambient sky tint, the fade overlay, and area-light
parameters are a *separate* system driven by `SkyPresetEntry` and `Sky_Update`
(`0x800dc640`); the backdrop is only geometry.

## Game System

### ModelSection - GrData + 0x0C

`ModelSection` (`externals/hoshi/include/stage.h`) is four dwords: `terrain` at `+0x00`,
`backdrop` at `+0x04`, and two unidentified model slots. A NULL slot is legal and just
suppresses that subtree.

`grModel<X>` (the public symbol exported by every `Gr<X>Model.dat` stage archive) **is**
this struct - HSD's `KAR_grModel`. Its slots don't point straight at `JOBJDesc`s:
`terrain` points at a `KAR_grMainModel` and `backdrop` at a `KAR_grSkyBoxModel`. Each of
those *leads with* its root `JOBJDesc *` (the main model then carries jobj/dobj/pobj
counts + a bounding record; the skybox carries a model-motion joint). Because the root
pointer is the first field, dereferencing a slot as a `JOBJDesc **` yields the root joint
- which is all `3D_CreateStageModel` ever reads, so the loader can treat the whole thing
as a `ModelSection` of `JOBJDesc **`s.

So loading a foreign stage's archive and taking `donor_ms.backdrop` gives you a backdrop
you can graft into any other stage that respects `ModelSection`.

### 3D_CreateStageModel (0x800dcbf0) - the loader

Reads `grdata->model_section` and instantiates each populated slot with
`HSD_JObjLoadJoint`. The terrain joint goes to `GObj_AddObject` on the ground GObj; the
backdrop joint is parked in `GrObj.backdrop_jobj` (`+0xF4`) with no GObj of its own. Both
get `grGetStageScale()` stamped into the root joint's scale at `JObj+0x2C/30/34`. If
`ms.backdrop == NULL`, `GrObj+0xF4` is set to NULL and the second `HSD_JObjLoadJoint` is
skipped - no crash.

`grGetStageScale` (0x800d3058) returns `grdata->stage_node->StageScale` (`StageNode+0x08`)
of the **current** stage. The loader writes that one float into all three of the
instantiated root joint's scale components (`JObj+0x2C/30/34`), uniformly overwriting
whatever scale the loaded `JOBJDesc` carried. So a grafted backdrop never keeps its
donor's native scale - it is forced to City Trial's `StageScale` (0.70). This is the
source of the size-normalization problem solved at carve time below.

## Implementation

`mods/custom_weather/src/custom_backdrops.c` owns the swap. It installs two hooks into
`3D_CreateStageModel` and a settings menu; both hooks are guarded on
`grobj->gr_kind == GR_CITY1` so no other stage's backdrop is touched.

### Override hook

`CustomBackdrop_Override` is hooked at `0x800dcc18`, just after `r30 = grobj` is loaded
but before `r29 = ms` is read (the macro replays the clobbered `lwz r3, 8(r30)`). The
callback picks a random enabled entry, loads that donor archive, and rewrites
`grdata->model_section->backdrop` to point at the donor's backdrop slot. The stock loader
then instantiates the foreign backdrop subtree as if it were native to this stage.

This is the simplest possible swap - no manual `HSD_JObjLoadJoint` / `HSD_JObjAddNext`, no
scale patching, no GX callback. The loader handles all of it; the mod just lies about
which `JOBJDesc *` it should use.

### Distance scaling

Because every carved backdrop is normalized to one geometry radius and the loader stamps
City Trial's `StageScale` (0.70) into the root joint, all backdrops would render at a
single fixed distance. A second hook at `0x800dce84` - immediately after the backdrop
branch stores the JObj at `GrObj+0xF4` and stamps `grGetStageScale()` into the root joint
scale at `JOBJ+0x2C/30/34` - multiplies that scale by a user-selected factor, moving the
whole sky dome nearer or farther. At the hook `r30 = grobj` and `r29 = backdrop JObj`
(both non-volatile); the macro replays the clobbered `lwz r0, 20(r29)` so the
classical-scaling flag handling that follows sees the rescaled joint, and the change lands
before the per-frame matrix build.

The factor is exposed as the **Backdrop Distance** value option (`100% / 125% / 150% /
175% / 200%`, default 125% since the stamped distance reads as too close in City Trial).
It multiplies the loader's stamped scale, so it composes with the carve-time normalization
rather than replacing it: 100% leaves the vanilla-distance sky dome, higher values push it
out uniformly across all backdrops including Vanilla.

### Donor archive lifetime

`Archive_LoadFile` allocates into a per-scene heap (despite passing `heap_id = 0`). The
archive struct is zeroed in place when the 3D scene exits - caching the pointer across CT
entries reads back as `file_size=0, data=0, flags=0`.

So `custom_backdrops.c` does **not** cache. Each CT round reloads the donor fresh. No
`Archive_Free` is called; scene teardown reclaims the storage automatically. Reload cost
is ~200 KB I/O for most donors, ~1.2 MB for the heaviest two (Space2, Dedede1) - small
relative to a full stage load.

## Carving Donors

Loading a full stage Model archive at runtime (1-2 MB) overflows the heap, so the backdrop
subtree is pre-extracted into a slim asset.

`scripts/hsd/carve_backdrop.py` walks the JOBJDesc tree starting at `<symbol>[1]`, computes
exact byte sizes from each HSD struct's fields (image data sized from
`width x height x bpp` rounded to GX tile padding; palette from `n_entries x 2`; etc.),
packs the kept ranges with **32-byte alignment preserved** (GX requires cache-line
alignment for textures, display lists, and vertex arrays), and emits a minimal HSD archive
plus a synthesized `ModelSection`-shaped public symbol.

Output convention: `Backdrop<X>.dat` exposing public `backdrop<X>`, where `<X>` is the
donor's `grModel<X>` suffix. Resident size after carving ranges from ~5% to ~75% of the
source archive (most cluster ~150-230 KB; the two outliers, Space2 at 61% / ~1.2 MB and
Dedede1 at 75% / ~1.2 MB, carry far more backdrop geometry).

`scripts/hsd/carve_all_backdrops.py` batch-runs the carve over every `Gr*Model.dat` that
has a non-NULL `ms[1]`; `--dry-run` reports which archives have backdrops without carving.
`scripts/hsd/verify_carved.py` walks a carved file and confirms every reachable pointer
lands inside its data section.

### Size normalization

Because the loader stamps City Trial's `StageScale` (0.70) onto every grafted backdrop's
root joint regardless of donor, each backdrop's on-screen sphere radius is
`0.70 x (its own geometry radius)`. Donor backdrops are modelled at wildly different raw
sizes - from ~1300 units (Colosseum 5) to ~10000 (Check 2, Jump 3) - so untreated they
render at radically different distances: small ones wrap in and obscure the map, large ones
recede to nothing. A donor's *native* `StageScale` does not predict this (native on-screen
radius ranges ~1260-16500), so there is no runtime scalar that equalizes them.

The carve fixes it at the source. `scripts/hsd/geom_bounds.py` walks the backdrop subtree,
parses each POBJ's display list for the positions actually drawn, accumulates the joint
transforms, and computes the geometry radius about the root origin (with the root joint's
scale forced to 1, since the loader overwrites it). `carve()` then uniformly scales the
subtree to a target radius - City Trial's own backdrop radius (~2856) - by multiplying
every joint translation and every drawn position vertex by `target / measured` (an exact
uniform scale of the hierarchy, since rotations and per-node scales are dimensionless).
`carve_all_backdrops.py` measures City's radius once and normalizes all donors to it. Every
carved `Backdrop*.dat` therefore has the same geometry radius, so after the loader stamps
0.70 they all render at City Trial's backdrop distance.

KAR backdrop positions are all `F32`, so the rescale is a plain float multiply; integer
position buffers would need re-quantization and the carve raises instead. Re-measuring a
carved asset with `geom_bounds.py` reports the normalized radius as a built-in check.

## Stage Tables

### gr_kind 5-tuple table (Table A) - 0x804a2ffc, stride 0x14, 28 entries

Indexed by `gr_kind`; consumed by `grLoadStageArchive` (`0x800ce7a0`). Per-row layout:

| Offset | Field |
|------:|-------|
| `+0x00` | `gr_filename` (e.g. `"GrCity1.dat"`) |
| `+0x04` | `gr_data_symbol` (e.g. `"grDataCity1"`) |
| `+0x08` | `gr_model_filename` (e.g. `"GrCity1Model.dat"`) |
| `+0x0C` | `gr_model_symbol` (e.g. `"grModelCity1"`) |
| `+0x10` | `gr_motion_symbol` (e.g. `"grModelMotionCity1"`) |

Decoded entries:

| gr_kind | Archive | gr_kind | Archive |
|--------:|---------|--------:|---------|
|  0 | `GrPlants1`    | 14 | `GrPasture1` |
|  1 | `GrHeat2`      | 15 | `GrColosseum1` |
|  2 | `GrDesert1`    | 16 | `GrColosseum3` |
|  3 | `GrCheck2`     | 17 | `GrColosseum5` |
|  4 | `GrValley2`    | 18 | `GrJump1` |
|  5 | `GrMachine2`   | 19 | `GrJump2` |
|  6 | `GrSpace2`     | 20 | `GrJump3` |
|  7 | `GrSky2`       | 21 | `GrDedede1` (King Dedede arena) |
| 8 | `GrIce1` | 22 | NULL - `param_9 == 0x16` special-case in `grLoadStageArchive` |
|  9 | `GrCity1` (**City Trial**) | 23 | `GrTest` (debug, no backdrop) |
| 10 | `GrZeroyon1`   | 24 | `GrTest6` (debug, no backdrop) |
| 11 | `GrZeroyon3`   | 25 | `GrTest7` (debug, no backdrop) |
| 12 | `GrZeroyon4`   | 26 | `GrSimple` (system archive, 14 MB; backdrop is a 4 KB placeholder) |
| 13 | `GrZeroyon5`   | 27 | `GrSimple2` (no backdrop) |

The indices above are `stage.h`'s `GroundKind` enum (`GR_*` members) - the file-table index
decoded here: `GR_CITY1 = 9`, `GR_PASTURE1 = 14`, `GR_COLOSSEUM5 = 17`, `GR_DEDEDE1 = 21`.
Do **not** confuse this with `StageKind` (the menu/selection index): the two spaces coincide
only at 0/1/2 and City Trial (9) and diverge elsewhere. `custom_backdrops` keys on
`grobj->gr_kind == GR_CITY1`.

### StageKind table (Table B)

Separate 60-entry table at `*0x805dd8dc = 0x807ea0c8`, stride 0x58, loaded at runtime from
`Stage.dat` (public symbol `stData`). First dword of each entry is a `gr_kind` that
resolves into Table A. The field at `+0x30` is the "is City" flag - `Gm_IsGrKindCity`
(0x80262574) asserts unless `0 <= stage_kind < 0x3b`, then returns the dword at
`stData[stage_kind * 0x58 + 0x30]` verbatim. Despite the name it is indexed by
`StageKind`, not `gr_kind`.

`StageKind` is the finer-grained UI/mode-level identifier (per-mode duplicates / variants);
`gr_kind` is the underlying physical archive identity. Most code wants `gr_kind`.

## Pool Composition

`carve_all_backdrops.py` produces 23 `Backdrop*.dat` files (one per `Gr*Model.dat` with a
non-NULL `ms[1]`), all staged in `mods/custom_weather/assets/`. `backdrop_defs[]` in
`custom_backdrops.c` references 21 of them, plus a "Vanilla" no-op entry at index 0, for 22
menu options. Two carved files are deliberately unreferenced: `City1` (it would duplicate
the Vanilla option) and `Simple` (a 4 KB placeholder, almost certainly a dummy). The other
4 archives skipped during batch carving (`GrSimple2`, `GrTest`, `GrTest6`, `GrTest7`) all
have `ms[1] == NULL`.

The **Backdrops** menu (`backdrop_menu` in `custom_backdrops.c`) carries the Backdrop
Distance value, an Enable-All / Disable-All pair, and one Enabled/Disabled toggle per
entry; `CustomBackdrop_Override` picks uniformly among the enabled ones and falls back to
Vanilla when none are enabled. Settings persist via hoshi's keyed menu-save.
