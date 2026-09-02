---
name: dat-explore
description: >
    Use the dat-explore skill to inspect and author Kirby Air Ride HSD
    archive (.dat) files under `iso/files/`. Activate when the user wants to:
    - List the publics/externs and header of a .dat (`ls`)
    - Walk the typed object tree of a model, scene or stage archive with
      decoded JObj/MObj/TObj/Material/PE flags + colors (`tree`)
    - Decode a stage's `grData<X>` public - physics, lighting, collision +
      zones, splines, spawn positions, items, rails, audio, yakumono,
      partition tree (`grdata`)
    - Grep public AND extern symbols across many .dat files (`find`)
    - Carve an Item.dat model into a customItem .dat mod asset
    - Plan the manifest custom_weather rebuilds a stage backdrop from
    - Author a texture or model .dat from PNGs
    Wraps the general `.dat` toolchain in `scripts/hsd/` (`explore.py`,
    `carve_custom_item.py`, `clone_machine.py`, `machine_preview.py`,
    `make_machine_art.py`, `verify_carved.py`, `geom_bounds.py`) and the
    per-mod asset authors in `scripts/authoring/`. Reads files directly from
    disc - does not require Dolphin to be running.
---

# dat-explore Skill

CLI-driven inspection and authoring of HSD archive (`.dat`) files using the
in-repo Python library at `scripts/hsd/`. The library is a focused Python
port of the bits of [HSDLib](https://github.com/Ploaj/HSDLib) we actually
need: archive header parsing and (re)serialization, reloc/public/extern
tables, a declarative layout table for the HSD/KAR struct types, typed tree
walking with exact size computation, subtree range-carving, GX texture-format
tables/encoders, and AirRide public-symbol classification.

Library modules:
- `archive` - `Archive` (reader), `build_archive` / `Blob` (writer), `u16`/`s16`/`u32`/`s32`/`f32`/`cstr`.
- `builder` - `Builder` (in-place edits over a parsed archive: read/repoint a
  pointer field, append or duplicate a record) plus the joint / MatAnimJoint
  preorder walks and DObj chain helpers an edit needs. The walks also run over
  a read-only `Archive`.
- `quad_model` - the textured-quad leaf a HUD or menu image is drawn as
  (`reserve_quad` / `write_quad` / `write_jobj`), with the HSD struct sizes and
  the vanilla quad's GX render config.
- `ui_banks` - finding the character-indexed TexAnim banks in a menu archive
  and extending their image / TLUT / quad-scale ramps.
- `schema` - `SCHEMA` (type -> size + pointer fields), `resolved_fields`, `root_for`.
- `walker` - `Walker` (typed reachability walk), `merge_intervals`, `carve_ranges`.
- `format` - GX/HSD flag+enum tables and `describe(arc, type, off)` one-liners.
- `grdata` - the `KAR_grData` stage-data decoder.
- `gx` - `FORMAT_BLOCK`/`FORMAT_NAME`, `image_size`, `align32`, RGB5A3 / RGBA8 / I4 encoders.
- `fobj` - packed keyframe streams (`decode` / `encode` / `Key`) and `JOINT_TRACK`.
- `symbols` - `classify_symbol` (public name -> HSDLib root type).

**Always invoke via `uv run python`** (see project rule in CLAUDE.md).

## When to reach for this

Anything about the contents of an `iso/files/*.dat` - listing symbols,
walking model/scene/stage trees, decoding a stage's `grData`, grepping
publics/externs across archives, or carving/authoring a mod asset.

For runtime memory inspection (no .dat involved) use `dolphin-memory`
instead - that talks to a running game.

## Commands

### `ls <file.dat>` - header + symbol table

```bash
uv run python scripts/hsd/explore.py ls iso/files/GrSpace2Model.dat
```

Prints the header (sizes, reloc/public/extern counts, version), then every
public with its inferred root type (e.g. `grModelSpace2 [KAR_grModel]`,
`enem01DataGroup [KAR_emData]`) and every extern (references into other
archives). Unclassified symbols print `[?]`.

Files that aren't HSDRawFile archives (`A2*.dat` menu/audio packs) are
rejected with a clear header error rather than yielding garbage symbols.

### `tree <file.dat> [<symbol>]` - typed object-tree walk

```bash
uv run python scripts/hsd/explore.py tree iso/files/GrSpace2Model.dat grModelSpace2
uv run python scripts/hsd/explore.py tree iso/files/GrCity1.dat grDataCity1 --max-depth 3
```

The root type is picked from `classify_symbol(symbol)` via `schema.root_for`
(override with `--root-type`; the help text lists every valid type). Publics
whose storage *is* a NULL-terminated pointer array (`_scene_models`,
`_scene_lights`, `map_plit`) are walked entry by entry.

Each line shows the field label, type, offset, struct size and decoded
fields. Coverage:

- **Render / geometry:** `JOBJDesc`, `DObjDesc`, `MObjDesc`, `POBJDesc`,
  `TObjDesc`, `ImageDesc`, `TlutDesc`, `IOBJDesc`, plus the leaf children
  `MaterialDesc`, `PEDesc`, `TexLODDesc`, `TObjTev`, `Spline`, `ShapeSet`,
  `Envelope`, `ParticleJoint`, `ParticleGroup`. Flag fields decode with
  HSDLib enum names (JOBJ_FLAG / RENDER_MODE / TOBJ_FLAGS / POBJ_FLAG /
  PE flags + blend mode); textures print their GX format name.
- **Scene / lighting:** `SOBJ` (three NULL-ptr arrays + inline FogAnim),
  `Camera`, `LightGroup` / `LightNode` / `Light`, `LObjDesc` (including the
  point/spot/raw attenuation block), `WObjDesc`, `FogDesc`, `ModelGroup`,
  `RObjDesc` (REFTYPE-routed).
- **Animation (full):** `AOBJ` / `AOBJDesc` / `FOBJDesc` / `FOBJ` with
  keyframe buffers sized from `FOBJDesc.dataLength`, plus the joint trees
  they hang off - `AnimJoint`, `MatAnimJoint` (->`MatAnim`->`TexAnim`),
  `ShapeAnimJoint`, and the `ROBJAnimJoint` / `WOBJAnim` /
  `LightAnimPointer` chains. Reached from `ModelGroup`'s three anim arrays,
  `KAR_grModelMotion`, `Light+0x04` and `grSubAnimNode`. `TexAnim` frame
  images/palettes are followed. `FigaTree` containers (KAR names these
  publics `*_cmpatree`) are walked too. The `track` byte prints raw - its
  enum is context-dependent (see `HSD_FOBJ.cs` for the six `*TrackType`s).
- **KAR stage model:** `grModel` -> `MainModel` (+ `ModelBounding`
  view-region / bounding-box containers and their u16 index arrays) and
  `SkyBoxModel` (+ `ModelMotion`).
- **KAR stage data:** `grData` and its whole sub-node web - stage params,
  collision + zone joints, the partition tree and its buckets, splines
  (course / CPU range / conveyor / rail / heavy), position and area lists,
  item tables, fog types, rails, FGM audio triggers, yakumono descs,
  respawn indices. On City Trial this reaches ~99% of the archive's bytes.
- **Flag-tagged unions** route automatically: `POBJ+0x14`
  (SHAPEANIM->ShapeSet, ENVELOPE->Envelope[], else->JOBJDesc), `JOBJ+0x10`
  (SPLINE->Spline, PTCL->ParticleJoint, else->DObjDesc), `RObj+0x08`
  (follows only when REFTYPE == JOBJ), `grFGMNodeEntry+0x14` (Spline only
  when Type == 2).

Flags: `--max-depth N`, `--root-type TYPE`, `--no-summary`.

The summary footer lists the full reachable byte budget per type plus a
total - the same numbers a carve or a manifest plan sizes itself by.
Stage trees are large; pair with `--max-depth`.

### `grdata <file.dat> [<public>] [--expand SECTION]` - decode stage data

```bash
uv run python scripts/hsd/explore.py grdata iso/files/GrCity1.dat
uv run python scripts/hsd/explore.py grdata iso/files/GrCity1.dat --expand collision
uv run python scripts/hsd/explore.py grdata iso/files/GrSpace2.dat grDataSpace2 --expand all
```

If `<public>` is omitted, the first `KAR_grData` / `KAR_grDataCommon` symbol
is used. The default output is the root field table (offsets and HSDLib
types from `KAR_grData.cs`, with NULL / non-reloc / runtime-only slots
flagged), each pointer annotated with a one-line summary of what it points
at - stage scale/gravity, collision and zone counts, spline counts, position
totals, sub-animation counts, rail/FGM/yakumono counts, partition size.

`--expand` (repeatable, or `all`) prints a section in full:

| Section | Contents |
| --- | --- |
| `stage` | every `KAR_grStageNode` field: physics, restitution tables, minimap, audio flags, boost pad/gate/ring params, OoB box |
| `lights` | the three `LightNode` slots, their `HSD_Light`/`LObjDesc` chains, colors, attenuation, position/interest |
| `collision` | every collision joint (bone, kind, vertex/face ranges, conveyor force) and every zone joint (bone, zone kind name, flags, link, param) |
| `splines` | course setup + key groups, CPU range splines, conveyor / rail / heavy spline lists with per-spline type, point count and length |
| `positions` | each of the 11 position lists + 2 area lists, count and joint-indexed vs inline storage |
| `subanim` | the six `grSubAnim` slots (SuperJump / Leap / Rail / x0C / x10 / EventAnim) and their `HSD_AnimJoint` arrays |
| `items` | timing / City Trial / Air Ride / Coliseum spawn tables and their entry counts |
| `fog` | the `HSD_FogDesc` plus every `KAR_TypeDataEntry` color set |
| `rails` | each `KAR_grRailColl` (spline indices, sub-anim, next/prev links) and its param block |
| `fgm` | positional and triggered audio entries with sound count, type and distance |
| `yakumono` | each `KAR_YakumonoDesc` with its model/anim pointers, collision, hurtbox and audio summary |
| `partition` | bucket count, leaf count, depth histogram, bit-table size |
| `respawn` | the `GlobalDeadPos` index list |

### `find <pattern> [<glob>...]` - grep publics and externs across .dats

```bash
uv run python scripts/hsd/explore.py find 'grModel[^M]' 'iso/files/*.dat'
uv run python scripts/hsd/explore.py find ^itData iso/files/A2Item.dat
uv run python scripts/hsd/explore.py find --externs-only EmyCodayl
```

Pattern is a Python regex matched against each symbol name. Default glob is
`iso/files/*.dat`. Output is one `file  pub|ext  offset  symbol  [class]`
line per hit - the `pub`/`ext` tag distinguishes public definitions from
extern references, so this answers both "which files define X" and "which
archives import X". Flags: `--publics-only`, `--externs-only`.

## Carving / authoring assets

Carving extracts a reachable subtree from a game archive into a minimal
standalone `.dat` with its own public(s), through one pipeline: `Walker.walk`
(collect reachable offsets + sizes) -> `carve_ranges` (concatenate + realign the
kept byte ranges, rebuild the reloc table into carved coordinates) ->
`build_archive` (serialize). A manifest plans the same walk without carrying the
bytes, listing the donor ranges for the mod to read at runtime.

- `scripts/authoring/make_backdrop_manifest.py` - plan every backdrop in
  `iso/files/Gr*Model.dat` into `BackdropManifest.dat`: the donor file, the
  byte ranges its subtree occupies, the relocations, and the radius
  normalization factor. `--dry-run` reports the plan without writing.
  `verify_backdrop_manifest.py` replays it and compares the rebuilt object
  graph against the donor's.
- `scripts/hsd/carve_custom_item.py` - carve an `Item.dat` model (by
  ItemKind) into a `customItem` .dat for the custom_items mod, optionally
  re-encoding a PNG into one texture slot:
  ```bash
  uv run python scripts/hsd/carve_custom_item.py iso/files/Item.dat 55 \
      mods/custom_items/assets/items/MegaHydra.dat "Mega Hydra" \
      --base-kind 3 --scale 1.2 --weight-blue 40 --ev-destructible 80
  ```
- `scripts/authoring/make_ap_star_pieces.py` - author the Archipelago Star's six
  sphere items (generated UV-sphere `customItem` archives, one per logo color)
  plus `ApPieceIcons.dat`, their HUD tracker art.
- `scripts/authoring/make_checklist_textures.py` - author `ApChecklistTex.dat`
  (banner RGB5A3 + emblem I4) from `art/ap-icon.png`.
- `scripts/authoring/make_menu_logo.py` - author `MnTitleKarchi.dat`, a
  `_scene_models` model archive of textured quads for the title screen.

See `docs/sky-backdrop-system.md` (backdrop consumption via the
`3D_CreateStageModel` hook), `docs/custom-items.md` (custom item discovery +
`CustomItemDesc`), `docs/ap-checklist.md` and `docs/custom-menu.md` for how
each authored file is used at runtime.

Sizing is delicate - a misclassified blob silently corrupts the output - so
always `verify_carved.py` a new carve. To add a new carve target, write a thin
tool like `carve_custom_item.py`: walk the subtree, hand the visited map to
`carve_ranges` with a `prefix` holding your descriptor / pointer slots, then
`build_archive`. The `make_*` scripts author archives the same way but build
their structs from scratch instead of carving.

Anything that writes one mod's shipped asset to a fixed path under
`mods/<mod>/assets/` belongs in `scripts/authoring/`, not here; `scripts/hsd/`
holds only what works on any archive. An author reaches the library through the
same `sys.path` insert (both directories sit one level under `scripts/`).

## Supporting tools

- `scripts/hsd/verify_carved.py <file.dat> [<public>]` - sanity-check an
  authored archive: bounds-validates every reloc source/target, then walks
  the typed tree to confirm no pointer escapes the data section. Returns
  nonzero on any bad reloc or pointer. The root is auto-detected
  (`customItem` descriptor, backdrop ModelSection, or the public's
  classified type); `--root`/`--root-type` override it.
- `scripts/hsd/geom_bounds.py <Model.dat> <grModelX> [slot]` - measure a
  model subtree's bounding box / radius about the root origin (root scale
  forced to 1, since `3D_CreateStageModel` overwrites it). Parses each
  POBJ's display list for drawn positions and accumulates joint transforms.
  Exposes `measure_root(arc, root)`, `scale_geometry(arc, root, f)` (uniform
  rescale) and `joint_world_positions(arc, off, world)`; the carve uses
  these to normalize each backdrop to City's radius.
- `scripts/hsd/machine_preview.py <Vc*.dat> [out.dat]` - re-export a machine
  archive under the public names a model viewer types (`_joint`,
  `_matanim_joint`, `_figatree`); a `vcData<Class><Stem>` names nothing one
  can draw. Keeps only the DObjs one LOD table asks for (`--lod`, high by
  default). Viewer-only - the LOD tables no longer match the pruned tree.

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
jobj = arc.deref(pp)                   # SkyboxModel's root JOBJ
visited = w.walk(jobj, 'JOBJDesc')     # OrderedDict[off -> (type, size)]

# Carve those bytes into a new archive:
res = carve_ranges(arc, visited, bytearray(prefix_len))  # realign + rebuild relocs
out = build_archive(res.data, res.relocs, [(new_symbol, 0)], arc.version)
```

The walker handles cycles (RObj backrefs to ancestors), sizes
ImageDesc/TlutDesc blobs exactly, and falls back to a next-reachable-start
heuristic for display lists, vertex arrays and unmapped (`opaque`) regions.

## Extending it

Everything about a type's layout lives in one place: `scripts/hsd/schema.py`.
`SCHEMA[type] = TypeSpec(size, fields)`, and the walker, the `tree` printer
and `verify_carved.py` all read it, so a new type or field is added once.

Field kinds:

| Kind | Meaning |
| --- | --- |
| `ptr` | one relocated pointer |
| `array` | pointer to a NULL-terminated pointer array |
| `count` | pointer array whose length is in a sibling field |
| `run` | pointer run delimited by the reloc table (no stored count) |
| `records` | pointer to N embedded fixed-size records of a schema type |
| `buffer` | pointer to a raw blob of `count * stride` bytes |

A field with `label=None` is walked but not printed (class-name strings,
bind matrices, raw blobs).

- **New public prefix/suffix reporting `[?]`** - add a rule to `_RULES` in
  `scripts/hsd/symbols.py`, in the same position HSDLib's `HSDRawFile.cs`
  lambda chain has it (the list is an ordered line-for-line port; first
  match wins). KAR families HSDLib doesn't know go in `_KAR_RULES`.
- **New root type for `tree`** - add the HSDLib class -> schema type
  mapping to `CLASS_TO_ROOT` (or `ARRAY_ROOTS` if the public's data IS the
  array) in `scripts/hsd/schema.py`.
- **New struct or field** - add it to `SCHEMA`. Only add a `visit_*` method
  on `Walker` when the size or child set cannot be expressed declaratively
  (a size read out of the record, an inline array, an embedded container).
- **New flag-tagged union** - add a router to `_UNIONS` in `schema.py`;
  every consumer picks it up.
- **A one-line summary for a type** - add a `_d_*` function and register it
  in `_DESCRIBERS` in `scripts/hsd/format.py`.
- **New GX texture format** - add it to `FORMAT_BLOCK` / `FORMAT_NAME` in
  `scripts/hsd/gx.py`.

## What this skill does NOT do

- Runtime memory access - use `dolphin-memory`.
- Disassembly of code segments in a .dat - use `uv run python scripts/kar.py disasm`.
- Full round-trip rewriting of an arbitrary archive. `builder` edits a parsed
  data section in place and `build_archive` serializes it, which covers
  repointing, appending and duplicating records; anything beyond that (layout
  compaction, unsupported types) means consulting HSDLib directly.
