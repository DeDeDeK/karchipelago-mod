---
name: dat-explore
description: >
    Use the dat-explore skill to inspect Kirby Air Ride HSD archive (.dat) files
    under `iso/files/`. Activate when the user wants to:
    - List the publics/externs and header of a .dat (`ls`)
    - Walk the JObj tree of a stage/model archive with decoded
      JObj/MObj/TObj/Material/PE flags + colors (`tree`)
    - Decode a `grData<X>` public's fields and sub-nodes (`grdata`)
    - Grep public AND extern symbols across many .dat files (`find`)
    - Carve a stage backdrop subtree into a Backdrop*.dat mod asset
    - Carve an Item.dat model into a customItem .dat mod asset
    Wraps `scripts/hsd/explore.py` (general explorer), the carve/author
    scripts (`carve_backdrop.py`, `carve_all_backdrops.py`,
    `carve_custom_item.py`, `make_checklist_textures.py`), and supporting
    tools (`probe_backdrops.py`, `verify_carved.py`, `dump_lights.py`,
    `geom_bounds.py`, all under `scripts/hsd/`). Reads files directly from
    disc - does not require Dolphin to be running.
---

# dat-explore Skill

CLI-driven inspection and authoring of HSD archive (`.dat`) files using the
in-repo Python library at `scripts/hsd/`. The library is a focused Python
port of the bits of [HSDLib](https://github.com/Ploaj/HSDLib) we actually
need: archive header parsing and (re)serialization, reloc/public/extern
tables, JObj/DObj/MObj/TObj/POBJ tree walking with exact size computation,
subtree range-carving, GX texture-format tables/encoders, and AirRide
public-symbol classification.

Library modules:
- `archive` - `Archive` (reader), `build_archive` (writer), `u16`/`u32`/`f32`/`cstr`.
- `walker` - `Walker` (typed reachability walk), `merge_intervals`, `carve_ranges`.
- `gx` - `FORMAT_BLOCK`/`FORMAT_NAME`, `image_size`, `align32`, RGB5A3 / I4 encoders.
- `symbols` - `classify_symbol` (public-name → HSDLib root type).

**Always invoke via `uv run python`** (see project rule in CLAUDE.md).

## When to reach for this

Anything about the contents of an `iso/files/*.dat` - listing symbols,
walking model/scene trees, decoding a stage's `grData`, grepping
publics/externs across archives, or carving a backdrop asset.

For runtime memory inspection (no .dat involved) use `dolphin-memory`
instead - that talks to a running game.

## Commands

### `ls <file.dat>` - header + symbol table

```bash
uv run python scripts/hsd/explore.py ls iso/files/GrSpace2Model.dat
```

Prints:
- File size, data size, reloc/public/extern counts, version
- Every public symbol with its inferred HSDLib root type (e.g.
  `grModelSpace2 [KAR_grModel]`, `enem01DataGroup [KAR_emData]`).
  Unclassified symbols print `[?]`.
- Every extern symbol (references into other archives).

### `tree <file.dat> [<symbol>]` - JObj tree walk

```bash
uv run python scripts/hsd/explore.py tree iso/files/GrSpace2Model.dat grModelSpace2
```

If `<symbol>` is a `grModel*` (`KAR_grModel`), walks its MainModel
(slot 0) and SkyboxModel (slot 1) JOBJ roots - each roots a full JObj
tree, and the MainModel also carries a (leaf) bounding record. Otherwise
the root type is picked from `classify_symbol(symbol)`:

| Classify result | Root type used |
| --- | --- |
| `HSD_SOBJ` | `SOBJ` (Scene object: model / camera / light arrays + fog) |
| `HSD_MOBJ` | `MObjDesc` |
| `HSD_IOBJ` | `IOBJDesc` (standalone image object) |
| `HSD_Camera` (suffix `_camera`) | `Camera` |
| `HSD_JOBJ` (suffix `_joint`) | `JOBJDesc` |
| `HSD_JOBJDesc` (suffix `_model_set`) | `ModelGroup` (the 0x10 wrapper) |
| `HSD_ModelGroup` (suffix `_model_group`) | `ModelGroup` |
| `HSD_FogDesc` (suffix `_fog`) | `FogDesc` |
| `HSD_ParticleGroup` (suffix `_ptcl`) | `ParticleGroup` |
| `HSDNullPointerArrayAccessor<HSD_Light>` (`_scene_lights`, `map_plit`) | walks NULL-terminated array of `Light` entries inline |
| `HSDNullPointerArrayAccessor<HSD_JOBJDesc>` (`_scene_models`) | walks NULL-terminated array of `ModelGroup` entries inline |
| anything else | `JOBJDesc` (fallback) |

Use `--root-type` to override. Per-node line shows offset, type, struct
size, and decoded fields. Coverage:

- **Render / geometry:** `JOBJDesc`, `DObjDesc`, `MObjDesc`, `POBJDesc`,
  `TObjDesc`, `ImageDesc`, `TlutDesc`, plus the leaf children
  `MaterialDesc`, `PEDesc`, `TexLODDesc`, `TObjTev`, `Spline`,
  `ParticleJoint`. Flag fields are decoded with HSDLib enum names
  (JOBJ_FLAG / RENDER_MODE / TOBJ_FLAGS / PE flags + blend mode).
- **Scene:** `SOBJ` (walks the three null-ptr arrays inline + inline
  FogAnim), `Camera`, `LightGroup` / `LightNode` / `Light`,
  `LObjDesc`, `WObjDesc`, `FogDesc`, `IOBJDesc`, `ParticleGroup`,
  `ModelGroup`, `RObjDesc` (REFTYPE-routed).
- **Animation (full):** `AOBJ` / `AOBJDesc` / `FOBJDesc` / `FOBJ`
  with keyframe buffer sized from `FOBJDesc.dataLength`, plus the
  joint trees they hang off - `AnimJoint`, `MatAnimJoint` (→`MatAnim`
  →`TexAnim`), `ShapeAnimJoint` (→`ShapeAnim`), and the
  `ROBJAnimJoint` / `WOBJAnim` / `LightAnimPointer` chains. Reached
  from `ModelGroup`'s three anim arrays, `KAR_grModelMotion`, and
  `Light+0x04`. `TexAnim` frame images/palettes (`ImageDesc` /
  `TlutDesc` count-arrays) are followed. `FigaTree` containers are
  walked too. `track` byte is printed raw - its enum is context-
  dependent (see `HSD_FOBJ.cs` for the six `*TrackType`s).
- **Flag-tagged unions** route automatically: `POBJ+0x14`
  (SHAPEANIM→ShapeSet, ENVELOPE→Envelope[], else→JOBJDesc),
  `JOBJ+0x10` (SPLINE→Spline, PTCL→ParticleJoint, else→DObjDesc),
  `RObj+0x08` follows when REFTYPE == JOBJ.

Flags:
- `--max-depth N` - cap recursion depth.
- `--root-type TYPE` - override root type (default JOBJDesc).
- `--no-summary` - skip the type/size footer.

The summary footer (when the tree reaches any `ImageDesc`) lists the
full reachable byte budget per type - same numbers `carve_backdrop`
uses to size the carved archive.

### `grdata <file.dat> [<public>]` - decode a `KAR_grData` public

```bash
uv run python scripts/hsd/explore.py grdata iso/files/GrCity1.dat
uv run python scripts/hsd/explore.py grdata iso/files/GrSpace2.dat grDataSpace2
```

If `<public>` is omitted, the first `KAR_grData` / `KAR_grDataCommon`
symbol in the file is used. Prints every field at its known offset
(NULL / non-reloc / runtime-only slots flagged), then inlines
`StageNode`, `LightGroup` (full chain walk - matches the heads
`dump_lights.py` uses, offset by `+ HSD_HEADER`), `FogNode → FogDesc`,
`PositionNode` (non-NULL slots only), and `SubAnimNode` (the six
`grSubAnim` slots - SuperJump / Rail / ... - each listing its
`HSD_AnimJoint` array). Field offsets and types are ported from
HSDLib's `KAR_grData.cs`.

### `find <pattern> [<glob>...]` - grep publics and externs across .dats

```bash
uv run python scripts/hsd/explore.py find 'grModel[^M]' 'iso/files/*.dat'
uv run python scripts/hsd/explore.py find ^itData iso/files/A2Item.dat
uv run python scripts/hsd/explore.py find --externs-only EmyCodayl
```

Pattern is a Python regex matched against each symbol name. Default
glob is `iso/files/*.dat`. Output is one
`file  pub|ext  offset  symbol  [class]` line per hit - the
`pub`/`ext` tag distinguishes public symbol defs from extern
references. Useful both for "which files define X" and "which archives
*import* X".

Flags:
- `--publics-only` - skip extern matches.
- `--externs-only` - skip public matches.

### Carving / authoring assets

Carving extracts a reachable subtree from a game archive into a minimal
standalone `.dat` with its own public(s). Both carve tools are thin
wrappers over the same pipeline: `Walker.walk` (collect reachable
offsets + sizes) → `carve_ranges` (concatenate + realign the kept byte
ranges, rebuild the reloc table into carved coordinates) → `build_archive`
(serialize). They live inside `scripts/hsd/` and share the library:

- `scripts/hsd/carve_backdrop.py` - extract a stage's skybox model
  (`grModel` slot 1 -> SkyboxModel, whose JOBJ root is the backdrop) into
  a Backdrop*.dat, normalizing its geometry radius to City Trial's.
  Single-backdrop CLI + `carve(input_path, src_symbol, slot, output_path,
  new_symbol)` API (slot 0 = MainModel, 1 = SkyboxModel):
  ```bash
  uv run python scripts/hsd/carve_backdrop.py \
      iso/files/GrSpace2Model.dat grModelSpace2 1 \
      mods/custom_weather/assets/BackdropSpace.dat backdropSpace
  ```
- `scripts/hsd/carve_all_backdrops.py` - bulk run over `iso/files/Gr*Model.dat`.
- `scripts/hsd/carve_custom_item.py` - carve an `Item.dat` model (by
  ItemKind) into a `customItem` .dat for the custom_items mod, optionally
  re-encoding a PNG into one texture slot:
  ```bash
  uv run python scripts/hsd/carve_custom_item.py iso/files/Item.dat 55 \
      mods/custom_items/assets/items/MegaHydra.dat "Mega Hydra" \
      --base-kind 3 --scale 1.2 --weight-blue 40 --ev-destructible 80
  ```

See `docs/sky-backdrop-system.md` (backdrop consumption via the
`3D_CreateStageModel` hook) and `docs/custom-items.md` (custom item
discovery + `CustomItemDesc`) for how the carved files are used at runtime.

Sizing is delicate - a misclassified blob silently corrupts the output -
so always `verify_carved.py` a new carve. To add a new carve target, write
a thin tool like the two above: walk the subtree, hand the visited map to
`carve_ranges` with a `prefix` holding your descriptor / pointer slots,
then `build_archive`. `make_checklist_textures.py` authors an archive the
same way but from GX textures rather than a carved subtree - it builds
ImageDescs + texel blobs with the `gx` encoders, then `build_archive`.

### Supporting tools

- `scripts/hsd/probe_backdrops.py` - read-only survey of every
  `iso/files/Gr*Model.dat`, reports whether the skybox model (`grModel`
  slot 1) is non-NULL (i.e. has a carveable backdrop subtree). Faster
  than running the full carve when you just want the inventory.
- `scripts/hsd/verify_carved.py <carved.dat> [<symbol>]` - sanity-check
  a carved archive: bounds-validates every reloc source/target, then
  BFS-walks the JObj tree to confirm no pointer escapes the data
  section. Returns nonzero if any bad reloc or pointer is found. Use
  after authoring a new carve target.
- `scripts/hsd/dump_lights.py [<file.dat>]` - dumps the
  LightGroup/LObjDesc chains at the hardcoded City Trial layout (see
  `docs/sky-lighting-system.md`). Adjust the chain head offsets in
  `GRCITY1_CHAINS` for other stages.
- `scripts/hsd/geom_bounds.py <Model.dat> <grModelX> [slot]` - measures a
  backdrop subtree's bounding box / radius about the root origin (root
  scale forced to 1, since `3D_CreateStageModel` overwrites it). Parses
  each POBJ's display list for drawn positions and accumulates joint
  transforms. Exposes `measure_root(arc, root)` and `scale_geometry(arc,
  root, f)` (uniform rescale by multiplying every translation + position
  vertex); the carve uses these to normalize each backdrop to City's
  radius. Re-run on a carved asset to confirm the normalized size.

## Library entry points (`scripts/hsd/`)

For one-off Python scripts, import from the library directly:

```python
import sys, os
sys.path.insert(0, os.path.join(os.getcwd(), 'scripts'))
from hsd import Archive, Walker, build_archive, carve_ranges, classify_symbol

arc = Archive('iso/files/GrSpace2Model.dat')
print(arc.publics)  # OrderedDict[name -> data offset]
print(arc.externs)  # [(offset, name), ...]

# Walk a JObj subtree with exact sizes.
w = Walker(arc)
ms_off = arc.publics['grModelSpace2']
pp = arc.deref(ms_off + 4)             # slot 1 -> SkyboxModel
jobj = arc.deref(pp)                    # SkyboxModel's root JOBJ
visited = w.walk(jobj, 'JOBJDesc')     # OrderedDict[off -> (type, size)]

# Carve those bytes into a new archive:
res = carve_ranges(arc, visited, bytearray(prefix_len))  # realign + rebuild relocs
out = build_archive(res.data, res.relocs, [(new_symbol, 0)], arc.version)
```

The walker handles cycles (RObj backrefs to ancestors), sizes
ImageDesc/TlutDesc blobs exactly, and falls back to a
next-reachable-start heuristic for display lists / vertex arrays.
Animation is fully walked: the AOBJ / FOBJDesc keyframe buffers plus
the joint trees they hang off (`HSD_AnimJoint`, `HSD_MatAnimJoint`
→`MatAnim`→`TexAnim`, `HSD_ShapeAnimJoint`, and the ROBJAnimJoint /
WOBJAnim / LightAnimPointer chains), reached from `ModelGroup`,
`KAR_grModelMotion`, and `HSD_Light`. Texture-animation frames are
followed through to their `ImageDesc` / `TlutDesc` buffers, so a
carve rooted at a wrapper keeps the animated textures. The MainModel's
`KAR_grModelBounding` spatial metadata (view-region / bounding-box
containers plus their per-record index arrays, including the first
region's indices at data-offset 0) and POBJ `ShapeSet` morph tables
are walked too. The only reachable pointers left unfollowed are
`KAR_grModel`'s two trailing model slots, which HSDLib leaves
unidentified.

## What this skill does NOT do

- Runtime memory access - use `dolphin-memory`.
- Disassembly of code segments in a .dat - use `scripts/disasm.sh`.
- Editing/rewriting archives in place. The carve workflow rebuilds a
  minimal archive from scratch; for arbitrary edits, consult HSDLib
  directly (the C# `HSDRawFile` writer is what would need porting).

## When to extend

- New AirRide root types we hit but the classifier reports `[?]` - add
  the prefix to `_PREFIX_TABLE` (or suffix to `_SUFFIX_TABLE`) in
  `scripts/hsd/symbols.py`, mirroring what HSDLib's `HSDRawFile.cs`
  does. Suffix rules win over prefix rules.
- New auto-routable root → walker mapping - add to `CLASS_TO_ROOT` (or
  `ARRAY_ROOTS` if the public's data IS the array, e.g. `_scene_*`)
  in `scripts/hsd/explore.py`.
- New HSD struct fields the walker doesn't follow - add a `visit_*`
  handler in `scripts/hsd/walker.py`. Keep the size-known table in
  sync, and add the field to `TREE_FIELDS` / `FIELD_LABEL` in
  `scripts/hsd/explore.py` (and `TYPE_CHILDREN` in
  `scripts/hsd/verify_carved.py`) so the tree printer and verifier
  follow it.
- Flag-tagged unions like POBJ+0x14 / JObj+0x10 / RObj+0x08 - handle in
  both `Walker.visit_*` (so reachability is correct) and the
  `_tree_children` dispatcher in `explore.py` (so the tree print shows
  the right child type). Use the `(offset, type, 'array')` form in
  `TREE_FIELDS` for NULL-terminated pointer arrays.
- New carve/author target - don't hand-roll the byte copy or the
  reloc/header serialization; walk with `Walker`, splice with
  `carve_ranges`, emit with `build_archive` (`carve_custom_item.py` is
  the reference). New GX texture format - add it to `FORMAT_BLOCK` /
  `FORMAT_NAME` in `scripts/hsd/gx.py`.
