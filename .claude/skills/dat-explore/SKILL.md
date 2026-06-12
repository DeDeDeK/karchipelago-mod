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
    Wraps `scripts/hsd/explore.py` (general explorer), the carve scripts
    (`scripts/hsd/carve_backdrop.py`, `scripts/hsd/carve_all_backdrops.py`),
    and supporting tools (`probe_backdrops.py`, `verify_carved.py`,
    `dump_lights.py`, all under `scripts/hsd/`). Reads files directly from
    disc - does not require Dolphin to be running.
---

# dat-explore Skill

CLI-driven inspection of HSD archive (`.dat`) files using the in-repo Python
library at `scripts/hsd/`. The library is a focused Python port of the bits
of [HSDLib](https://github.com/Ploaj/HSDLib) we actually need: archive
header parsing, reloc/public/extern tables, JObj/DObj/MObj/TObj/POBJ
tree walking with exact size computation, and AirRide public-symbol
classification.

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

If `<symbol>` is a `grModel*` (ModelSection), walks each of the four
JOBJDesc**[4] slots independently. Otherwise the root type is picked
from `classify_symbol(symbol)`:

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
- **Animation (shallow):** `AOBJ`, `FOBJDesc`, `FOBJ` with keyframe
  buffer sized from `FOBJDesc.dataLength`. `track` byte is printed
  raw - its enum is context-dependent (see `HSD_FOBJ.cs` for the six
  `*TrackType`s). AnimJoint / MatAnimJoint / ShapeAnimJoint trees
  are intentionally not walked.
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
and `PositionNode` (non-NULL slots only). Field offsets and types are
ported from HSDLib's `KAR_grData.cs`.

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

### Carving a backdrop

The carve scripts are backdrop-specific (extract `grModel<X>[1]`, the
backdrop JOBJDesc**, into a standalone Backdrop*.dat with a public of
the same name). They live inside `scripts/hsd/` alongside the rest of
the HSD library and share its `Archive` / `Walker`:

- `scripts/hsd/carve_backdrop.py` - single-backdrop CLI + `carve(input_path, src_symbol, slot, output_path, new_symbol)` API:
  ```bash
  uv run python scripts/hsd/carve_backdrop.py \
      iso/files/GrSpace2Model.dat grModelSpace2 1 \
      mods/custom_weather/assets/BackdropSpace.dat backdropSpace
  ```
- `scripts/hsd/carve_all_backdrops.py` - bulk run over `iso/files/Gr*Model.dat`:
  ```bash
  uv run python scripts/hsd/carve_all_backdrops.py
  ```

See `docs/sky-backdrop-system.md` for how the carved files are consumed
at runtime (the `3D_CreateStageModel` override hook in custom_weather).

There is intentionally **no** general subtree carve API - sizing is
delicate (a misclassified blob silently corrupts the output), and we
only need backdrops today. If a new carve target comes up, copy
`carve_backdrop.py` and adjust rather than adding speculative knobs.

### Supporting tools

- `scripts/hsd/probe_backdrops.py` - read-only survey of every
  `iso/files/Gr*Model.dat`, reports whether `grModel<X>[1]` is non-NULL
  (i.e. has a carveable backdrop subtree). Faster than running the full
  carve when you just want the inventory.
- `scripts/hsd/verify_carved.py <carved.dat> [<symbol>]` - sanity-check
  a carved archive: bounds-validates every reloc source/target, then
  BFS-walks the JObj tree to confirm no pointer escapes the data
  section. Returns nonzero if any bad reloc or pointer is found. Use
  after authoring a new carve target.
- `scripts/hsd/dump_lights.py [<file.dat>]` - dumps the
  LightGroup/LObjDesc chains at the hardcoded City Trial layout (see
  `docs/sky-lighting-system.md`). Adjust the chain head offsets in
  `GRCITY1_CHAINS` for other stages.

## Library entry points (`scripts/hsd/`)

For one-off Python scripts, import from the library directly:

```python
import sys, os
sys.path.insert(0, os.path.join(os.getcwd(), 'scripts'))
from hsd import Archive, Walker, classify_symbol, image_size

arc = Archive('iso/files/GrSpace2Model.dat')
print(arc.publics)  # OrderedDict[name -> data offset]
print(arc.externs)  # [(offset, name), ...]

# Walk a JObj subtree with exact sizes.
w = Walker(arc)
ms_off = arc.publics['grModelSpace2']
pp = arc.deref(ms_off + 4)             # ms[1]
jobj = arc.deref(pp)
visited = w.walk(jobj, 'JOBJDesc')     # OrderedDict[off -> (type, size)]
```

The walker handles cycles (RObj backrefs to ancestors), sizes
ImageDesc/TlutDesc blobs exactly, and falls back to a
next-reachable-start heuristic for display lists / vertex arrays.
Animation support is shallow: AOBJ / FOBJDesc / FOBJ are walked
(keyframe buffer sized from `FOBJDesc.dataLength`), wired in today
only at `FogAnim+0x04`. The deeper anim-joint trees (`HSD_AnimJoint`
/ `HSD_MatAnimJoint` / `HSD_ShapeAnimJoint`, plus LightAnimPointer
and WOBJAnim chains) are intentionally not walked.

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
