# Sky Backdrop System

## What the backdrop is

The 3D mesh that wraps around the playable area — distant city
skyline / mountains / sky dome / starfield. Lives in its own JObj
sub-tree separate from the playable terrain mesh. Air Ride courses
populate one; City Trial populates one (`grModelCity1[1]`, the city
horizon you see beyond the streets); some debug stages leave the slot
NULL and just fog out.

This is **not** the lighting system: fog colour, ambient sky tint,
fade overlay, and area light parameters all live in `SkyPresetEntry`
and are blended per-frame by `Sky_Update`. See `sky-lighting-system.md`.

## Mechanism

### `ModelSection` — `GrData + 0x0C`

Defined in `externals/hoshi/include/stage.h`:

```c
typedef struct ModelSection {
    JOBJDesc **terrain;  // 0x00 — main playable geometry
    JOBJDesc **backdrop; // 0x04 — secondary skybox/horizon mesh
    void *unk_8;         // 0x08
    void *unk_c;         // 0x0C
} ModelSection;
```

Each pointer is a *pointer-to-pointer* (HSD's "scene_data" array
style) — `ms[N]` is a slot, and the slot contains a `JOBJDesc *`. A
NULL slot is legal and just suppresses that subtree.

`grModel<X>` (the public symbol exported by every `Gr<X>Model.dat`
stage archive) **is** a `ModelSection`. So loading a foreign stage's
archive and taking `donor_ms.backdrop` gives you a backdrop you can
graft into any other stage that respects `ModelSection`.

### `3D_CreateStageModel` (0x800dcbf0) — the loader

Reads `grdata->model_section`, then for each populated slot:

```c
JOBJDesc *desc = *ms.terrain;            // (or *ms.backdrop)
JObj    *jobj = HSD_JObjLoadJoint(desc); // instantiate
// terrain: GObj_AddObject(grobj_gobj, kind, jobj)
// backdrop: grobj->backdrop_jobj_at_0xF4 = jobj
// both: stamp grGetStageScale() into JObj scale at +0x2C/30/34
```

If `ms.backdrop == NULL`, slot `GrObj+0xF4` is set to NULL and the
second `HSD_JObjLoadJoint` is skipped — no crash.

`grGetStageScale` (0x800d3058) returns `grdata->stage_node->StageScale`
(`StageNode+0x08`) of the **current** stage. The loader writes that one
float into all three of the instantiated root joint's scale components
(`JObj+0x2C/30/34`), uniformly **overwriting** whatever scale the loaded
`JOBJDesc` carried. So a grafted backdrop never keeps its donor's native
scale — it is forced to City Trial's `StageScale` (0.70). This is the
source of the size-normalization problem solved at carve time below.

### Override hook

`mods/custom_weather/src/custom_backdrops.c` patches one instruction
inside `3D_CreateStageModel` (at `0x800dcc18`, just after `r30 = grobj`
is loaded but before `r29 = ms` is read). The C callback rewrites
`grdata->model_section->backdrop` to point at a donor archive's
backdrop pp slot. The stock loader then instantiates the foreign
backdrop subtree as if it were native to this stage.

This is the simplest possible swap — no manual `HSD_JObjLoadJoint` /
`HSD_JObjAddNext`, no scale patching, no GX callback. The loader
handles all of it; we just lie about which `JOBJDesc *` it should use.

### Distance scaling

Because every carved backdrop is normalized to one geometry radius and
the loader stamps City Trial's `StageScale` (0.70) into the root joint,
all backdrops render at a single fixed distance. A second hook into
`3D_CreateStageModel` at `0x800dce84` — immediately after the backdrop
branch stores the JObj at `GrObj+0xF4` and stamps `grGetStageScale()`
into the root joint scale at `JOBJ+0x2C/30/34` — multiplies that scale by
a user-selected factor, moving the whole sky dome nearer or farther. At
the hook `r30 = grobj` and `r29 = backdrop JObj` (both non-volatile); the
macro replays the clobbered `lwz r0, 20(r29)` so the classical-scaling
flag handling that follows sees the rescaled joint, and the change lands
before the per-frame matrix build.

The factor is exposed as a "Backdrop Distance" value option in the
backdrop menu (`100% / 125% / 150% / 175% / 200%`, default 125% since the
stamped distance reads as too close in City Trial). A `gr_kind == GR_CITY1`
guard keeps the scaling out of every other stage's backdrop. The factor
multiplies the loader's stamped scale, so it composes with the carve-time
normalization rather than replacing it: 100% leaves the vanilla-distance
sky dome, higher values push it out uniformly across all backdrops
including Vanilla.

### Donor archive lifetime

`Archive_LoadFile` allocates into a per-scene heap (despite passing
`heap_id = 0`). The archive struct is **zeroed in place when 3D scene
exits** — caching the pointer across CT entries reads back as
`file_size=0, data=0, flags=0`.

So `custom_backdrops.c` does **not** cache. Each CT round reloads the
donor fresh. No `Archive_Free` is called; scene teardown reclaims the
storage automatically. Reload cost is ~200 KB I/O for most donors,
~1.2 MB for the heaviest two (Space2, Dedede1) — small relative to a
full stage load.

## Carving donors

Loading a full stage Model archive at runtime (1–2 MB) overflows the
heap, so the backdrop subtree is pre-extracted into a slim asset.

`scripts/hsd/carve_backdrop.py` walks the JOBJDesc tree starting at
`<symbol>[1]`, computes exact byte sizes from each HSD struct's
fields (image data sized from `width × height × bpp` rounded to GX
tile padding; palette from `n_entries × 2`; etc.), packs the kept
ranges with **32-byte alignment preserved** (GX requires cache-line
alignment for textures, display lists, and vertex arrays), and emits
a minimal HSD archive plus a synthesized `ModelSection`-shaped public
symbol.

Output convention: `Backdrop<X>.dat` exposing public `backdrop<X>`,
where `<X>` is the donor's `grModel<X>` suffix. Resident size after
carving ranges from ~5% to ~75% of the source archive (most cluster
~150–230 KB; the two outliers, Space2 at 61% / ~1.2 MB and Dedede1 at
75% / ~1.2 MB, carry far more backdrop geometry).

`scripts/hsd/carve_all_backdrops.py` batch-runs the carve over every
`Gr*Model.dat` that has a non-NULL `ms[1]`. `scripts/hsd/probe_backdrops.py`
reports which archives have backdrops; `scripts/hsd/verify_carved.py`
walks a carved file and confirms every reachable pointer lands inside
its data section.

### Size normalization

Because the loader stamps City Trial's `StageScale` (0.70) onto every
grafted backdrop's root joint regardless of donor, each backdrop's
on-screen sphere radius is `0.70 × (its own geometry radius)`. Donor
backdrops are modelled at wildly different raw sizes — from ~1300 units
(Colosseum 5) to ~10000 (Check 2, Jump 3) — so untreated they render at
radically different distances: small ones wrap in and obscure the map,
large ones recede to nothing. A donor's *native* `StageScale` does **not**
predict this (native on-screen radius ranges ~1260–16500), so there is no
runtime scalar that equalizes them.

The carve fixes it at the source. `scripts/hsd/geom_bounds.py` walks the
backdrop subtree, parses each POBJ's display list for the positions
actually drawn, accumulates the joint transforms, and computes the
geometry radius about the root origin (with the root joint's scale forced
to 1, since the loader overwrites it). `carve()` then uniformly scales the
subtree to a target radius — City Trial's own backdrop radius (~2856) — by
multiplying every joint translation and every drawn position vertex by
`target / measured` (an exact uniform scale of the hierarchy, since
rotations and per-node scales are dimensionless). `carve_all_backdrops.py`
measures City's radius once and normalizes all donors to it. Every carved
`Backdrop*.dat` therefore has the same geometry radius, so after the
loader stamps 0.70 they all render at City Trial's backdrop distance.

KAR backdrop positions are all `F32`, so the rescale is a plain float
multiply; integer position buffers would need re-quantization and the
carve raises instead. Re-measuring a carved asset with `geom_bounds.py`
reports the normalized radius as a built-in check.

## Stage table reference

### gr_kind 5-tuple table (Table A) — 0x804a2ffc, stride 0x14, 28 entries

Indexed by `gr_kind`; consumed by `grLoadStageArchive`. Per-row
layout:

| Offset | Field |
|------:|-------|
| `+0x00` | `gr_filename` (e.g. `"GrCity1.dat"`) |
| `+0x04` | `gr_data_symbol` (e.g. `"grDataCity1"`) |
| `+0x08` | `gr_model_filename` (e.g. `"GrCity1Model.dat"`) |
| `+0x0C` | `gr_model_symbol` (e.g. `"grModelCity1"`) |
| `+0x10` | `gr_motion_symbol` (e.g. `"grModelMotionCity1"`) |

Decoded entries:

| gr_kind | Filename / public | Notes |
|--------:|-------------------|-------|
|  0 | `GrPlants1`   | |
|  1 | `GrHeat2`     | |
|  2 | `GrDesert1`   | |
|  3 | `GrCheck2`    | |
|  4 | `GrValley2`   | |
|  5 | `GrMachine2`  | |
|  6 | `GrSpace2`    | space backdrop |
|  7 | `GrSky2`      | |
|  8 | `GrIce1`      | |
|  9 | `GrCity1`     | **City Trial** |
| 10 | `GrZeroyon1`  | |
| 11 | `GrZeroyon3`  | |
| 12 | `GrZeroyon4`  | |
| 13 | `GrZeroyon5`  | |
| 14 | `GrPasture1`  | |
| 15 | `GrColosseum1`| |
| 16 | `GrColosseum3`| |
| 17 | `GrColosseum5`| |
| 18 | `GrJump1`     | |
| 19 | `GrJump2`     | |
| 20 | `GrJump3`     | |
| 21 | `GrDedede1`   | King Dedede arena |
| 22 | (NULL — `param_9 == 0x16` special-case in `grLoadStageArchive`) |
| 23 | `GrTest`      | debug, no backdrop |
| 24 | `GrTest6`     | debug, no backdrop |
| 25 | `GrTest7`     | debug, no backdrop |
| 26 | `GrSimple`    | system archive (14 MB), backdrop is a 4 KB placeholder |
| 27 | `GrSimple2`   | no backdrop |

> **The physical ground indices above are `stage.h`'s `GroundKind` enum (`GR_*`
> members)** — the file-table index decoded here: `GR_CITY1 = 9`, `GR_PASTURE1 =
> 14`, `GR_COLOSSEUM5 = 17`, `GR_DEDEDE1 = 21`. Do **not** confuse this with
> `StageKind` (the menu/selection index): the two spaces coincide only at 0/1/2 and
> City Trial (9) and diverge elsewhere. `custom_backdrops` keys on
> `grobj->gr_kind == GR_CITY1`.

### StageKind table (Table B)

Separate 60-entry table at `*0x805dd8dc = 0x807ea0c8`, stride 0x58,
loaded at runtime from `Stage.dat` (public symbol `stData`). First
dword of each entry is a `gr_kind` that resolves into Table A. The
field at `+0x30` is the "is City" flag — `Gm_IsGrKindCity`
(0x80262574) bounds-checks the index against 60, then returns
`stData[stage_kind * 0x58 + 0x30]` verbatim.

`StageKind` is the finer-grained UI/mode-level identifier (per-mode
duplicates / variants); `gr_kind` is the underlying physical archive
identity. Most code wants `gr_kind`.

## Pool composition

`carve_all_backdrops.py` produces 23 `Backdrop*.dat` files (one per
`Gr*Model.dat` with a non-NULL `ms[1]`); the mod's `backdrop_defs[]`
references only 21 of them (the two starred below are carved but
intentionally not referenced). The runtime pool is those 21 donors
plus a "Vanilla" no-op entry, for 22 total options in the menu:

```
BackdropCheck2     BackdropDesert1    BackdropJump3      BackdropSpace2
BackdropCity1*     BackdropHeat2      BackdropMachine2   BackdropValley2
BackdropColosseum1 BackdropIce1       BackdropPasture1   BackdropZeroyon1
BackdropColosseum3 BackdropJump1      BackdropPlants1    BackdropZeroyon3
BackdropColosseum5 BackdropJump2      BackdropSky2       BackdropZeroyon4
BackdropDedede1                                          BackdropZeroyon5
                                      BackdropSimple*
```

The mod's pool excludes `City1` (would duplicate the Vanilla no-op
option) and `Simple` (4 KB placeholder, almost certainly a dummy).
The other 4 archives skipped during batch carving (`GrSimple2`,
`GrTest`, `GrTest6`, `GrTest7`) all have `ms[1] == NULL`.

## See also

- `mods/custom_weather/src/custom_backdrops.c` — the implementation
  (two `3D_CreateStageModel` hooks: the `ms.backdrop` override and the
  post-stamp distance scale, plus the backdrop menu).
- `mods/custom_weather/src/custom_weather.c` — sister system,
  preset extension. Same hoshi menu pattern.
- `scripts/hsd/carve_backdrop.py`, `scripts/hsd/carve_all_backdrops.py`,
  `scripts/hsd/probe_backdrops.py`, `scripts/hsd/verify_carved.py` — the
  asset extraction toolchain. All four share the `scripts/hsd/` HSD
  archive library (`Archive` / `Walker`); see
  `.claude/skills/dat-explore/SKILL.md` for the broader explorer.
- `docs/sky-lighting-system.md` — sky/fog/light preset system.
- `docs/scene-system.md` — scene/mode transitions; relevant to the
  per-scene heap behaviour.
