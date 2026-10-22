# Custom Backdrops

The backdrop half of `custom_weather`: each City Trial round can swap the city's horizon for the
backdrop of another stage - a desert, a starfield, a colosseum - rebuilt from the retail disc,
and every backdrop's distance can be pushed out. The mod ships no geometry or textures of its
own, only a recipe for reading each donor backdrop's bytes off the disc.

## How the engine loads a backdrop

`CreateStageModel_3D` (0x800dcbf0) reads `GrData.model_section`, instantiates the `backdrop`
slot with `JObj_LoadJoint`, parks the joint in `GrObj.backdrop_jobj` (+0xf4), and writes the
stage's scale (`grGetStageScale`, 0.70 on City Trial) over all three of the root joint's scale
components. Every stage's `grModel<X>` public stores its backdrop the same way, a record leading
with the root `JOBJDesc *`, so a slot pointing at another stage's backdrop record loads as if it
were native. Right after it, `Gr_SetBackdropAnim` (0x800dbc2c) reads the record's word 1 as the
backdrop's motion array and binds entry 0.

## The hooks

Both code hooks are inside `CreateStageModel_3D` and act only when `gr_kind == GR_CITY1` - City
Trial and the Destruction Derby 4 and 5 stadiums that share its ground.

| Site | Registers | Does |
|---|---|---|
| 0x800dcc18 | `r30` = GrObj | before the loader reads the model section: roll a backdrop, rebuild it, point `model_section->backdrop` at it |
| 0x800dce84 | `r30` = GrObj, `r29` = backdrop JObj | after the loader stamps the scale: multiply it by the geometry factor and the Backdrop Distance |
| `On3DExit` | - | put the original `model_section->backdrop` back |

Both code hooks replay the instruction they overwrite (`lwz r3, 8(r30)` and `lwz r0, 20(r29)`),
so the loader's own flow, including the `JOBJ_MTS_INDEPEND_SRT` test and matrix-dirty mark that
follow the second, sees the change.

**Selection** rolls `Gm_Roll` over the enabled flags of the pool - Vanilla plus one entry per
donor - so it picks uniformly among enabled entries and falls back to Vanilla when none is. Vanilla
rebuilds nothing.

**Scale.** The loader discards each donor's own root scale, and donors are modelled anywhere from
about 1300 units (Colosseum 5) to about 10000 (Check 2, Jump 3), so untreated they would sit at
wildly different distances - small ones closing in over the map, large ones receding to nothing.
Each manifest entry carries `scale = City's backdrop radius / donor radius`, measured from the
geometry actually drawn, which puts every backdrop at City Trial's own distance. The Backdrop
Distance setting (100-200%, default 125%) then multiplies every backdrop, Vanilla included; the
mod's sky layers read the same factor to keep the moon and stars inside the dome. Since a
backdrop is a rigid, unskinned subtree, scaling its root is the same as scaling every vertex, and
the bytes stay identical to the disc.

**The slot restore.** `GrCity1Model.dat` is preloaded into a heap that can outlive the round, and
the archive loader reuses a resident preload as-is, so the rewritten `model_section->backdrop`
would still point into the previous round's reclaimed heap. The override records the slot it
changed, and `On3DExit` writes the original back while that scene's heap still exists.

## The manifest

A donor's whole `Gr<X>Model.dat` cannot be loaded during a round: City Trial already holds
`GrCity1Model.dat` (2.8 MB) and every machine, rider and item archive, and the donors run up to
2.6 MB. Shipping carved copies would mean redistributing retail data. So the mod ships
`BackdropManifest.dat`, about 9 KB, whose public `backdropManifest` describes, per donor, which
byte ranges of the donor file hold its backdrop subtree and how to relocate them.

```c
typedef struct BackdropManifest {
    u32 magic;                             // 'BDMF'
    u32 version;                           // 1
    u32 entry_num;
    const BackdropManifestEntry *entries;
} BackdropManifest;

typedef struct BackdropManifestEntry {
    const char *key;                       // grModel<X> suffix, "Check2"
    const char *donor;                     // "GrCheck2Model.dat"
    u32 payload_size;                      // bytes to allocate
    float scale;                           // City's backdrop radius / donor radius
    u32 root_off;                          // the backdrop JOBJDesc, payload-relative
    const BackdropRange *ranges;           // {donor_off, dest_off, length}
    u32 range_num;
    const BackdropReloc *relocs;           // {dest_off, dest_val}
    u32 reloc_num;
} BackdropManifestEntry;
```

Every range offset and length is a multiple of 32, which is what `File_Read` requires of an
offset, a length and a destination alike - and what GX wants of textures and display lists. In
practice each donor's subtree is contiguous, so a backdrop is one read. Relocations ship as value
pairs, not just source offsets: the bytes land raw off the disc with every pointer still a donor
file offset, and translating one needs the whole byte map, so the translated payload offset is
precomputed and the runtime only adds the payload base.

The manifest covers 14 donors, one per distinct look among every `Gr<X>Model.dat` with a
backdrop. Several stages share a backdrop byte for byte once rebuilt - Heat 2 is Colosseum 3,
Valley 2 is Colosseum 1, Space 2 is Dedede 1, Sky 2 and Zeroyon 4 are Jump 2, Zeroyon 3 and 5 are
Zeroyon 1 - and only the first of each is kept. City1 (it would duplicate Vanilla) and Simple (a
4 KB placeholder) are left out.

## Rebuilding at runtime

Per round, if a donor is picked:

1. Check the manifest is on the disc (`Archive_LoadFile` asserts on a missing file), load it,
   and check its magic and version.
2. Resolve the donor with `DVDConvertPathToEntrynum` and `Heap_Alloc` the payload; the heap's
   blocks are already 32-byte aligned.
3. Zero the leading 0x20-byte slot, then `File_Read` each range into place, blocking on
   `File_PollReadDone` the way `File_LoadSync` does.
4. Write each relocation as `base + dest_val`, and the root as `base + root_off` into word 0 of the
   leading slot, which then reads like a vanilla stage's skybox record.
5. Point `model_section->backdrop` at the payload.

Both the manifest and the payload live in the per-scene heap, which is reclaimed when the 3D scene
exits, so the payload is rebuilt every round and nothing is freed. The cost is about 200 KB of I/O
and heap for most donors and about 1.2 MB for the heaviest (Dedede 1). The offsets are specific to
GKYE01.

A missing manifest or donor file leaves City Trial's own backdrop in place and logs once. The heap
asserts rather than returning NULL when it runs out, so an allocation failure is not a fallback.

## Settings

The Backdrops submenu carries Backdrop Distance, Enable All / Disable All, and one
Enabled/Disabled toggle per pool entry. They persist through hoshi's keyed menu save.
