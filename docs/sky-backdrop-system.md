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
callback picks a random enabled entry, rebuilds that backdrop's subtree out of the retail
disc, and rewrites `grdata->model_section->backdrop` to point at it. The stock loader then
instantiates the foreign backdrop subtree as if it were native to this stage.

This is the simplest possible swap - no manual `HSD_JObjLoadJoint` / `HSD_JObjAddNext`, no
GX callback. The loader handles all of it; the mod just lies about which `JOBJDesc *` it
should use.

### Distance scaling

A second hook at `0x800dce84` - immediately after the backdrop branch stores the JObj at
`GrObj+0xF4` and stamps `grGetStageScale()` into the root joint scale at `JOBJ+0x2C/30/34`
- multiplies that stamped scale. At the hook `r30 = grobj` and `r29 = backdrop JObj` (both
non-volatile); the macro replays the clobbered `lwz r0, 20(r29)` so the classical-scaling
flag handling that follows sees the rescaled joint, and the change lands before the
per-frame matrix build.

Two factors ride on that one multiply:

- **Geometry normalization**, `backdrop_geom_scale`, set per round by the override hook
  from the manifest entry's `scale`. Donors are modelled anywhere from ~1300 units
  (Colosseum 5) to ~10000 (Check 2, Jump 3), and the loader discards each donor's own
  root scale, so untreated they render at radically different distances - small ones wrap
  in and obscure the map, large ones recede to nothing. The factor is
  `City_backdrop_radius / donor_radius`, which puts every backdrop at City Trial's own
  backdrop distance. Vanilla resets it to `1.0`.
- **Backdrop Distance**, the user-facing value option (`100% / 125% / 150% / 175% / 200%`,
  default 125% since the stamped distance reads as too close in City Trial). 100% leaves
  the normalized sky dome where it is; higher values push it out uniformly across all
  backdrops including Vanilla.

Scaling the root joint is equivalent to scaling every vertex, since the backdrop is a
rigid unskinned subtree under a uniform scale - and it is what lets the geometry stay
byte-identical to the disc.

### Payload lifetime

`Archive_LoadFile` and `Heap_Alloc(0, ...)` both allocate into a per-scene heap (despite
the `heap_id = 0`). The storage is zeroed in place when the 3D scene exits - caching a
pointer across CT entries reads back as `file_size=0, data=0, flags=0`.

So `custom_backdrops.c` does **not** cache. Each CT round reloads the manifest and rebuilds
the picked backdrop fresh. No `Archive_Free` or `HSD_Free` is called; scene teardown
reclaims both automatically. Cost is ~200 KB of I/O and heap for most donors, ~1.2 MB for
the heaviest two (Space2, Dedede1) - small relative to a full stage load.

## Rebuilding Donors From The Disc

The mod ships no backdrop data. Shipping a carved subtree would mean redistributing retail
geometry and textures (5.8 MB across 23 donors), and loading the donor `Gr*Model.dat`
whole at runtime is not an option either: the HSD heap `Archive_LoadFile` draws from is
10.2 MB total (`0x80A60BC0`-`0x81492B80`) and City Trial already holds
`GrCity1Model.dat` (2.79 MB) plus every machine, rider and item archive, so a second
0.5-2.8 MB stage archive does not fit - and `GrSimpleModel.dat` is 14.5 MB on its own.

So only a recipe ships. `scripts/authoring/make_backdrop_manifest.py` walks the JOBJDesc tree at
`<symbol>[1]`, computes exact byte sizes from each HSD struct's fields (image data from
`width x height x bpp` rounded to GX tile padding; palette from `n_entries x 2`; etc.), and
records **which byte ranges of the donor file the subtree occupies** rather than their
contents. At runtime the mod reads just those ranges off the disc, so it pays for the subtree
alone rather than for the stage archive around it.

Output: one `mods/custom_weather/assets/BackdropManifest.dat` (~15 KB) exporting public
`backdropManifest`, covering all 23 donors. Per entry it holds:

| Field | Meaning |
|---|---|
| `key` / `donor` | `grModel<X>` suffix, and the file to read (`GrCheck2Model.dat`) |
| `payload_size` | bytes to allocate |
| `scale` | `City_backdrop_radius / donor_radius`, applied to the root joint |
| `root_off` | the backdrop `JOBJDesc`, payload-relative |
| `ranges[]` | `(donor_off, dest_off, length)`, all multiples of 32 |
| `relocs[]` | `(dest_off, dest_val)` per pointer in those ranges |

Every range is expanded to 32-byte boundaries at both ends, which is what `File_Read`
requires of an offset, a length and a destination alike - and what GX requires for
textures, display lists and vertex arrays. In practice each donor's subtree is contiguous,
so every backdrop is a single read.

Relocations ship as value pairs, not just source offsets, because the bytes land raw off
the disc: each pointer still holds a donor offset, and translating one needs the whole byte
map, so the translated value is precomputed and the runtime only adds the payload base.

`RebuildBackdrop` in `custom_backdrops.c` is the whole runtime: resolve the donor with
`DVDConvertPathToEntrynum`, `Heap_Alloc` the payload 32-byte aligned, run each range
through `File_Read` (blocking via `*stc_file_read_done = 0` / `File_ReadDone` /
`while (File_Wait() == 0);`, the same pattern `File_LoadSync` uses), write the relocations,
and stamp the root pointer into word 0 of the leading `0x20`-byte pp slot - which mirrors a
vanilla stage's `grModel<X>[1]`, so it drops straight into `ModelSection.backdrop`.

The offsets are specific to GKYE01, which the mod already targets exclusively.

### Verification

`scripts/authoring/verify_backdrop_manifest.py` replays the runtime in Python - reads the ranges
out of the donor files, applies the relocations - and then walks the rebuilt payload with
the same type-aware walker that planned it, comparing the object graph type-for-type and
size-for-size against walking the donor's own subtree. The walker only follows a field
listed in the reloc set, so a pointer the manifest failed to record goes unfollowed and
shows up as a graph mismatch. All 23 backdrops reconstruct exactly.

### Size normalization

Because the loader stamps City Trial's `StageScale` (0.70) onto every grafted backdrop's
root joint regardless of donor, each backdrop's on-screen sphere radius is
`0.70 x (its own geometry radius)`. A donor's *native* `StageScale` does not predict this
(native on-screen radius ranges ~1260-16500), so the correction has to be measured.

`scripts/hsd/geom_bounds.py` walks the backdrop subtree, parses each POBJ's display list
for the positions actually drawn, accumulates the joint transforms, and computes the
geometry radius about the root origin (with the root joint's scale forced to 1, since the
loader overwrites it). `make_backdrop_manifest.py` measures City's backdrop radius (~2856)
once and ships `target / measured` per donor, which the distance hook multiplies into the
stamped scale. Because the subtree is rigid and unskinned and the scale is uniform, that
renders identically to scaling every vertex - and it leaves the geometry byte-identical to
the disc, which is what allows the ranges to be read verbatim.

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

`make_backdrop_manifest.py` plans 23 entries (one per `Gr*Model.dat` with a non-NULL
`ms[1]`). `backdrop_defs[]` in `custom_backdrops.c` references 21 of them by `key`, plus a
"Vanilla" no-op entry at index 0, for 22 menu options. Two entries are deliberately
unreferenced: `City1` (it would duplicate the Vanilla option) and `Simple` (a 4 KB
placeholder, almost certainly a dummy). The other 4 archives skipped during planning
(`GrSimple2`, `GrTest`, `GrTest6`, `GrTest7`) all have `ms[1] == NULL`.

The **Backdrops** menu (`backdrop_menu` in `custom_backdrops.c`) carries the Backdrop
Distance value, an Enable-All / Disable-All pair, and one Enabled/Disabled toggle per
entry; `CustomBackdrop_Override` picks uniformly among the enabled ones and falls back to
Vanilla when none are enabled. Settings persist via hoshi's keyed menu-save.
