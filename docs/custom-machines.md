# Custom Machines

`mods/custom_machines/` adds air ride machines to the game that vanilla does not have. It is a registry in the `custom_*` family: it owns no machine of its own, it scans a drop-in folder on the disc and widens the engine tables so whatever it finds gets a real `MachineKind`, a real class slot, an audio parameter row and (optionally) a `CharacterKind` with a cell on the select grids. The one machine this repo ships through it, the Archipelago Star, is described near the end.

## Where a machine comes from

Every `.dat` under the FST folder `machines/` is a candidate. An archive qualifies when it exports two publics: its own `vcData` symbol, holding an ordinary engine machine archive exactly as `VcStarSlick.dat` and friends ship it, and `customMachine`, a `CustomMachineDesc` saying how to register it.

`CustomMachineDesc` is declared in `mods/custom_machines/include/custom_machines_api.h`, which carries the field-by-field notes. In outline: the display name and the `vcData` public to register; the machine class, of which only the star class is supported; whether the machine also wants a `CharacterKind` and a select-grid cell, and the `RiderKind` for that row; the star `MachineKind` whose per-kind engine rows it inherits (`clone_kind`); a City Trial loose-spawn weight; a select-screen blurb; a joint index and palette for the wall-clock material cycle; up to eight particle-generator byte offsets to tint with that same color and up to four generator slots to clone; and the two archives, their publics and the vanilla legendary index for the assembly cutscene.

`spawn_weight` is weighed against a vanilla table whose per-machine weights run 6-10 out of a ~111-119 total, so 1.0 is roughly 0.8% of field spawns.

A descriptor is rejected when its version is newer than the mod supports, so the field it does not know about is never read. A v1 descriptor ends where `description` starts and is still accepted; the machine gets an empty blurb, a v2 one gets no material cycle, a v3 or v4 one no trail tint - v4 laid that field out for a single generator and is read as asking for none - a v5 one no generator clones, and a v6 one no assembly cutscene.

Registration order is FST scan order, and it decides the appended kinds: the *n*-th machine found takes star slot `19 + n`, `MachineKind` `VCKIND_NUM + n` and, if it asked for one, `CharacterKind` `CKIND_NUM + n`. `CUSTOM_MACHINE_MAX` caps the registry at 13, which is what the select screens have room for: City Trial's packed icon list can grow to 33 entries before it reaches a byte `CitySelect_Think` reads, and 20 of those belong to the vanilla roster. The same 13 is the width the UI art banks are grown by and the width the per-machine counter arrays are relocated to, so all three move together.

Discovery runs at `OnBoot` and cannot use `Archive_LoadFile`: that allocates from the per-scene heap, which does not exist yet. The mod reads each candidate with `DVDConvertPathToEntrynum` + `File_Read` into an `HSD_MemAlloc` buffer and calls `Archive_Init` on it. Everything the registry keeps - the strings, the palette colors, the cutscene archive names - is copied out of the descriptor and the archive freed again through `HSD_Archive.top_ptr`, which is where `Archive_Init` leaves the file blob it was handed. A machine archive runs to six figures of bytes and the engine loads its own copy later, by filename, through the widened name table, so holding the boot copy would cost over a megabyte at the registry's cap for a few hundred bytes of descriptor.

A machine may drop two side-cars of the same basename next to its `.dat`: a `.ssm` sound bank, which gives it a voice of its own instead of its clone kind's, and a `.art` archive, which gives it its own picture everywhere the game draws a machine instead of the registry's placeholder.

## Building an archive

`scripts/hsd/clone_machine.py` produces one from a vanilla machine:

```
uv run python scripts/hsd/clone_machine.py iso/files/VcStarWing.dat \
  mods/mine_star/assets/machines/VcStarMine.dat vcDataStarMine \
  --name "Mine Star" \
  --description "Borrowed wings." \
  --clone-kind 2 \
  --spawn-weight 1.0 \
  --cinematic MineGlow.dat,mineGlow,mineCam,MineParts.dat,mineParts
```

It renames the source archive's single public and appends a `CustomMachineDesc` under a second `customMachine` public, with a relocation per string pointer. `--cinematic` is optional and `--cine-index` picks which vanilla legendary the cutscene borrows its rider pose, fanfare and sky from. Nothing inside the donor's own seven slots is touched, so the result is its donor under a new name; a machine that wants its own shape or paint edits the archive itself, with `scripts/hsd/builder.py` - the Archipelago Star's six-pod ring and repainted platform are `VcStarSlick.dat` edited that way.

`mods/*/assets/` is copied to the disc root by the ordinary asset step, so any mod's `assets/machines/*.dat` lands at `machines/` on disc with no packaging change. A machine therefore ships as its own mod folder - `mods/ap_star/assets/machines/VcStarAp.dat` is the Archipelago Star's, with `VcStarAp.art` beside it - and this mod discovers both without knowing the mod exists. Nothing but a machine descriptor may go in `assets/machines/` under a `.dat` extension: the scan probes every `.dat` there for a `customMachine` public and reports the ones that have none. Side-cars carry their own extensions and are found by basename, never scanned. A machine's other archives - a cutscene's models, for instance - belong at the FST root.

Assets carrying data taken out of the disc - `VcStarAp.dat`, `CmUiFrames.dat` - are `.gitignore`d rather than committed, so no vanilla data lives in the repo. Each is written by its own script out of an `iso/` extraction, and the ordinary asset step then picks it up like any other file in `assets/`.

## What a machine archive holds

The `vcData` public is the seven-pointer struct hoshi declares as `vcData` in `machine.h`: attributes, model data, collision group, collision attributes, collision sphere, handling attributes and animation bank. Three of those are worth knowing about here.

The attributes block is 0x1F0 bytes and `Machine_AdjustAttributes` (`0x801c7278`) memcpys it to `MachineData+0x460`, so its named fields - the rider sit bone index, base HP, ground and air top speed, the glide parameters, the quick-spin tornado size - are read live off the machine rather than off the archive. The model data holds the main model root, the bone count, six LOD table slots (main/boost x high/mid/low) and a shadow root. The collision attributes are a 0x38-byte analytic float table; there is no collision mesh anywhere in the archive.

The animation bank pairs a FigaTree and a MatAnim per machine state and also carries the particle spawn ids and bones and the boost and after-boost sound ids.

Animations live inside the machine's own archive as anonymous `AnimJoint` trees, at a **0x0C stride** for machines. Five of the bank's slots carry the machine's own motion:

| Slot | Content |
|---|---|
| Moving | idle and driving, played faster as speed rises, so give it generous frames |
| Charge | at least 100 frames; the charge gauge is a 0-100 meter and frames past 100 loop |
| Unk1 | the boost animation proper |
| Boost | the idle *during* boost |
| Stop | stop and brake |

Quick spin has no bank slot at all. A machine that wants to deform during one has to drive it from mod code off `Rider_QuickSpin_Enter` (`0x801b7ee4`), which also keeps it in sync with the spin's real duration.

## Authoring a model

A model authored against a donor armature inherits the donor's first six joints, and **those must carry no animation keys** - keys there break quick spin and vehicle rotation. Joint 6 is the body root, where every star machine puts its body, and the rider's seat is wherever the attributes block's sit bone index points. Bone count is the highest joint index plus one; a wrong count is the usual cause of parts detaching and the rider lagging on bad landings.

Colors can be flat material colors with one MObj per part, so no body texture is required. Delete any Blender color attributes - they make imported geometry render gray. The shadow is a separate flat top-down silhouette with normals up, solidified downward, imported into the shadow root with constant color on and diffuse shading off.

Export as glTF 2.0: its node TRS animation channels map almost one to one onto FigaTree `TRAX/Y/Z`, `ROTX/Y/Z` and `SCAX/Y/Z` tracks, which dodges the dropped-scale-keyframe problem the SMD path has. `scripts/hsd/fobj.py` is the packed-keyframe codec those tracks need, decode and encode.

## The engine tables it widens

The engine addresses a machine as a `(is_bike, class slot)` pair. Stars are slots 0-18 and their slot equals their `MachineKind`; bikes are slots 0-6 with `MachineKind` `19 + slot`. A custom machine is therefore the only kind whose class slot and `MachineKind` differ, which is why consumers must resolve kinds through the API rather than through hoshi's `MachineKind_ClassIndex` / `MachineKind_FromClassIndex` (those describe vanilla and are still correct for it).

**Star class (`machine_registry.c`).** The class grows from 19 slots to `19 + CUSTOM_MACHINE_MAX`.

- The per-class `{filename, symbol}` name table is reached as `(*(char ***)0x805d6f90)[is_bike]`. The mod copies the 19 vanilla pairs into a wider table, appends each registered machine's path and symbol at its slot, and repoints the row. The class-shared archive's own pair lives in a separate table at `0x804b07e0`.
- `stc_vcDataLookup` (`0x8055a068`, `vcData *[2][19]`) is relocated into a mod-owned `[2][19 + N]`. Exactly three sites read it. Two are replaced outright - `vcData_InitLookup` (`0x801c6c68`) and `Vehile_LoadFile` (`0x801c6d74`), both small. The third, `Machine_StoreVcDataPtr` (`0x801c4f98`), is patched instruction by instruction instead: its `lis` at `0x801c4fd0`, its `addi` at `0x801c4fe8` and its row stride `mulli` at `0x801c5034`. A hook there would clobber caller-saved registers the surrounding code still needs.
- `MachineDesc_SetKindAndIsBikeFromMachineKind` (`0x801c857c`) is the one canonical `MachineKind -> (is_bike, slot)` split, with 14 callers including the City Trial spawn path. Replacing it teaches all of them about the appended kinds at once.
- City Trial preloads machines by walking a 26-byte enable table at `0x804b07f0` inside `Machine_PreloadAll` (`0x801c8cec`). A hook on the branch out of that loop (`0x801c8d8c`) preloads the registered archives too.
- Two 19-entry tables of machine-specific handlers, `0x804b15c0` and `0x804b160c`, sit back to back and end exactly where the star class's own descriptor begins. `Machine_Star_Init` (`0x801e7f3c`) and `Machine_Star_Think` (`0x801eacbc`) each end by loading their table at `MachineData.kind` and calling the result if it is non-NULL, with no bounds check - so slot 19 reads the descriptor's first word and branches into rodata. Only Hydra, Formula, Wagon and Turbo have handlers. Each table is read by exactly one site that does nothing but form the address, so both are relocated by rewriting the reader's `lis`/`addi` pair: `0x801e80dc`/`0x801e80e0` and `0x801eb524`/`0x801eb528`. A custom slot takes the handlers of its descriptor's `clone_kind`, which is NULL for the 15 stars that have none. A consumer can layer its own handler onto a custom slot through `SetStarInitHandler` / `SetStarThinkHandler`: one shared dispatcher stands in the slot, recovers the row from `md->kind` - which is the star slot - and runs the inherited handler before the consumer's, so no per-slot trampoline is needed.

**Per-machine counters (`machine_stats.c`).** `PlayerStats` carries three `int[0x1a]` arrays - `machine_change_count` at `+0x37c`, `kills_by_machine` at `+0x3e4` and `deaths_by_machine` at `+0x44c` - and their writers fold `(is_bike, class slot)` into an absolute `MachineKind` themselves, with no bounds check. A custom machine is a star at class slot 19 and up, so it lands on the bike half of that range: the first seven share a bike's bucket and the eighth onward writes past all three arrays into `ko_cpu_machine_broken`, the three other KO-by-cause counters, `vehicle_bust_mask` and `item_collect[0]`, every one of which is a checklist condition.

There is no slack to widen them in place - the three run back to back and the five-player `stc_playerdata` block (`0x8055a9f0`, stride `0x90c`) ends 0x74 bytes short of `gEffectMgr` - so they are relocated into a mod-owned `int[5][3][26 + N]`. Every site that touches them is reached at a `bl`, so four call replacements cover the feature: `Ply_IncrementGetOnMachineNum` at `0x801ba190` in `AS_GetOnStar`, `Ply_AddDeath` at `0x801e1f74` in `Machine_GiveDamage`, and the two readers - `Ply_GetMachineChangeCount` at `0x8004e6a8` in `CityTrial_CheckFreeRunObjectives`, which unlocks a checklist cell at ten machine changes, and `Ply_GetKONum` at `0x80012470` in `Game_Think`, which publishes the Destruction Derby score. Both readers sum their whole array, so relocating them keeps every vanilla answer identical. The round reset is done from this mod's own `On3DLoadStart`, which runs just before `Player_InitAll` clears the engine's own copies.

`Ply_AddDeath` still runs, because it also sets the KO-by-cause counters, the vehicle-bust mask and the King Dedede frame; only the counting is taken over. A kind it has no bucket for is swapped for `VCKIND_WHEELVSDEDEDE` on the way in - in range, never on the field, and named by no vehicle-bust entry, so its two remaining array writes land somewhere unread and no bust achievement can fire by accident.

**City Trial blip (`machine_blip.c`).** The blip is the floating icon drawn over every machine on the field, one per machine per player view. The template builder at `0x801226e8` makes one joint per `MachineKind` out of `ScInfWarpstarct` in `IfAll2c.dat`, posing each on its own frame of an 18-frame TexAnim named by a 26-entry table at `0x804a85f8`; the per-frame GX callback picks the template up by machine kind and instances it. Six of the table's entries are `0xFFFFFFFF`, which means no blip.

The kind that callback indexes with comes from `Machine_GetAbsoluteKind` (`0x801c85bc`), the same `is_bike ? class slot + 19 : class slot` fold. A custom machine therefore lands on the bike half again: the first two slots hit the two wheelie-animal entries that are `0xFFFFFFFF` and get no blip, the next five would steal a real bike's icon, and the sixth onward reads past both the table and the template array into the live per-player instance joints - which the callback then instances into themselves, and `HSD_JObjDispAll` recurses without end. Replacing the `bl` at `0x80122414` is the whole fix, because every one of those follows from the one index. A custom machine borrows its `clone_kind`'s blip, the way it borrows that kind's audio row and handlers; all 18 frames of the bank are claimed and none is a duplicate that City Trial can reach, so a machine cannot have an icon of its own without growing the bank the way the UI art banks are grown.

**Audio (`machine_audio.c`).** `(*stc_machineAudioParams)->params[0]` is the star class's array of 0x94-byte `MachineAudioParams`, authored in `VcCommon.dat` and indexed by class slot. A hook on `vcLoadCommon`'s epilogue (`0x801c6d64`) copies the 19 vanilla rows into a wider mod-owned array, fills each custom slot from its descriptor's `clone_kind`, and repoints the pointer. It is idempotent, so re-entering a scene does not re-splice.

That row opens with thirteen FGM ids, and they are the whole of a machine's voice:

| # | Slot | Slick Star's |
|---|---|---|
| 0 | engine loop | `SFX_engine_slick_lp` |
| 1-3 | charge gauge, one loop per third | `SFX_wstar_charge{1,2,3}_lp` |
| 4-6 | boost release, loud / medium / quiet | all -1 |
| 7 | surface loop | `SFX_runnoize_slick_lp` |
| 8 | rumble loop | `SFX_rumble_s_lp` |
| 9 | quick spin | `SFX_spin_s` |
| 10 | engine start, one shot | `SFX_start_engine_slick` |
| 11 | surface start, one shot | `SFX_start_runnoize_slick` |
| 12 | overheat loop | `SFX_engine_overh1` |

A machine that wants nothing new can simply pick different ids out of that vocabulary; the vanilla samples are `iso/files/audio/jp/star.ssm` with routing in `airride.sem`, and `iso/files/audio/adfgmnametable.dat` names them. Keep the row's idle volume floor at 0.0: a nonzero floor clamps to full volume and makes the machine hum while parked, which is why the Wagon Star needs a workaround wherever it is posed rather than driven.

**Characters (`character_registry.c`).** Four DOL tables describe the roster and sit back to back with no slack: the select-screen linear strip (`0x804957ec`, 20 bytes), the select-screen grid (`0x80495800`, 2x10), `CharacterDesc` (`0x80495814`, 20x3) and the star class's slot-to-`CharacterKind` map (`0x80495850`, 19 bytes). Each is read by exactly one accessor that does nothing but form an address, so all four are relocated by rewriting the `lis`/`addi` pair inside the accessor - no hook, no register hazards:

| Accessor | `lis` | `addi` |
|---|---|---|
| `SelIcon_GetCKindLinear` | `0x8000b9a8` | `0x8000b9b0` |
| `Icon_GetCKind` | `0x8000b9c0` | `0x8000b9cc` |
| `Character_GetDesc` | `0x8000b9e0` | `0x8000b9e8` |
| `Machine_GetCKind` | `0x8000b9fc` | `0x8000ba04` |

`Machine_GetCKind` (`0x8000b9f4`) is the reverse of `CharacterDesc_GetMachineKind`: a `(is_bike, class slot)` pair back to the `CharacterKind` whose art to draw, over two byte tables, one per class - the star one above and a 7-byte bike one at `r13 - 0x7fbc`, which stays put because only the star class grows. Fifteen call sites go through it, all on the four results screens and the time-attack board; the select screens hold the `CharacterKind` already and never touch it. Vanilla parks its own art-less star slots - `FREE`, `STEER`, `WINGKIRBY`, `WINGMETAKNIGHT` - on the Warp Star, and an appended slot with no `CharacterKind` behind it goes to the same place. Without the relocation a custom slot reads the byte past the table's end, which is a zero, so every results screen and record draws the Compact Star.

The grid gains a column per two appended characters, and its runtime row stride is written into `Icon_GetCKind`'s `mulli r5, r0, cols` at `0x8000b9c4`. Appended characters fill the new columns row 0 first, so a single addition gives an 11/10 grid rather than 10/11. At most one cell is left over; it holds a sentinel `CharacterKind` one past the last real one, which indexes a zeroed `CharacterDesc` row and which every availability predicate rejects, so it can never be packed into a select list. The appended `CharacterDesc` row is `{rider_kind, 0, star_slot}` - class-relative, as every vanilla row is.

A character is only appended while the select screens still have an icon for it, so `GetSelectIconMax()` is the real cap on appended characters even though `CUSTOM_MACHINE_MAX` machines can register. A machine past it still loads, drives, spawns in City Trial and can be handed out; it just is not on the grids. Art is not part of that decision: every appended character resolves to the same UI frame, because the image-index ramp the side-car authors holds its last value past the frame it adds, so a machine is never refused a cell for want of a portrait.

**Select screens (`select_screen.c`).** This mod both widens the screens and packs them, because both screens build their icon lists by walking the character grid it widened. Vanilla's packing loops are a hard 10 columns into two 10-byte stack rows, so an 11th column has nowhere to land and an appended character stays invisible however well it is registered.

Four things cap a select screen at 20 icons, and all four are moved here. What they are moved to is 33, which is where City Trial's packed list runs out of room:

- The packed icon list in `GameData` - a count at select base `+0x65` and one `CharacterKind` per icon from `+0x66` - is followed immediately by live bytes: Air Ride's row-layout flag at `+0x7a` and its debug-grid flag at `+0x7b`, and City Trial's debug-grid flag at `+0x7a` in its own block. Each moves to the far end of its screen's free span by rewriting the displacement of every `lbz`/`stb` that reaches it - Air Ride's row-split to `+0x87` across nine sites (`0x80020b04`, `0x80020b4c`, `0x80020b98`, `0x800214a4`, `0x80027f60`, `0x800285c8`, `0x80028818`, `0x80028970`, `0x80029c7c`), its debug-grid to `+0x88` across six (`0x80020a88`, `0x8002881c`, `0x8002895c`, `0x80028968`, `0x80029c64`, `0x80029c70`), and City Trial's to `+0x87` across six (`0x8002e444`, `0x80038d00`, `0x8003a150`, `0x8003a15c`, `0x8003ac3c`, `0x8003ac48`). Code that rebuilds the Air Ride list has to write the flag through `SetAirRideRowSplit` rather than at the vanilla offset.

  The spans differ, and City Trial's is what sets the cap. Air Ride's block is `base+0x00` to `base+0x8c` - `CSS_airRide_InitSelectData` memsets `0x8d` bytes from `GameData+0x10a` - and nothing reads `base+0x7c` up, so its list would take 37 entries. City Trial has no block memset, and across every function that forms its base at `GameData+0x1d0` the only things read between its list and the Top Ride lobby data are the debug-grid flag and one byte at `base+0x88`, which `CitySelect_Think` tests. That leaves `base+0x66` through `base+0x86` for the list once the flag is out of the way: 33 entries, 20 of them the vanilla roster.
- The icon positions come from a strip of 20 anchor joints posed by an animation whose frame is the icon count, which has no key past 20. `AirRideSelect_LayoutIcons` and `CitySelect_LayoutMachineIcons` are replaced: they call the engine's pass, which is exact up to 20 icons, and past that redo the grid arithmetically - `spacing = 2H / max(ceil(N/2) - 1, floor(N/2) - 0.5)`, with the shared icon scale multiplied by the spacing ratio so the tiles still fit their columns. Icons with no anchor are held in the mod, because their `Vec3`s in the ipos userdata would land on the trailing scale and count; `AirRideSelect_GetIconPos` and `CitySelect_GetIconPos` are replaced to hand them out. The rows stay at two however many icons there are - the engine's cursor navigation is built for two - so the extra icons are absorbed by tightening the spacing rather than by adding a third row.

  The half-width `H`, the two row heights and the block's centre are measured off the strip on arrival rather than named as constants. The engine's pass fills the userdata array by calling `JOBJ_GetWorldPosition` on each anchor, and the two screens differ in what that returns: City Trial's anchors carry the layout animation's scale themselves, so a world position is the authored coordinate, while Air Ride's twenty hang under one parent joint the animation scales - 1.0 up to five icons, 0.82 from ten on - so its authored `-30.607` reaches the array as `-25.10`. Past 20 icons the animation holds its twenty-icon pose, so the strip always arrives holding that layout: `H` and the centre come from the first and last anchor, the row heights from the first of each row. Writing the authored numbers instead stretches the Air Ride grid by `1 / 0.82` and leaves City Trial's right, which is how the difference shows.
- The icon GObj array in `ScMenuCommon` holds 20 pointers and its writer indexes it unguarded, so an icon past the strip walks into the scene-model pointers that follow - by the seventh, the ipos GObj the layout above reads its positions out of. Both creators end in the same four instructions, an `extsb`/`slwi`/`add`/`stw` immediately before the epilogue, so the store itself is taken over: a hook at `0x8015181c` (Air Ride) and `0x8015c2dc` (City Trial) exits past it at `0x8015182c` / `0x8015c2ec`, keeping vanilla indices in the engine's array and sending appended ones to storage of the mod's own. Nothing reads either array.
- The packing itself. `AirRide_PopulateSelectIcons` (`0x80020a08`) is replaced outright. City Trial's lives inside `CitySelect_CreateMachineIcons` (`0x8002e3c4`) and is taken over with three hooks - the two counting passes at `0x8002e4d0` (Stadium) and `0x8002e5c0` (Free Run), both returning the count in r27 and exiting to `0x8002e670`, and the tail at `0x8002f0b8` that packs the list, lays it out and creates the icons. The two array-building passes in between have nothing left to build, so `0x8002e67c` and `0x8002e738` branch straight to the tail; that also skips the reorder between them, which assumes the vanilla grid's fixed positions for the legendary machines and duplicates icons on a packed list.

Two patches are not about widening:

- Vanilla's `AirRide_CheckCharacterAvailable` switches on a 20-entry jump table and falls through to the checklist query with an uninitialised reward index for anything past it, so its out-of-range branch at `0x80020924` is retargeted to the `return 1` arm. The packing above never asks it about an appended character, but other engine callers can.
- `CitySelect_Cursor1InputThink` splits cursor rows at `num >= 10` (`cmpwi r3, 9; ble` at `0x80031350`), while the grid renderer keeps up to 10 icons on one drawn row and only wraps at 11 - so at exactly 10 the cursor splits 5+5 across a single row. Vanilla City Trial only ever produces counts 15-20; a filtered roster can land on 10, so the compare becomes `cmpwi r3, 10`.

**Who fills the list.** Packing asks an availability predicate per `CharacterKind`. With no consumer attached that predicate is the engine's own: `AirRide_CheckCharacterAvailable` on Air Ride, and on City Trial the equivalent the engine only ever inlined into `CitySelect_CreateMachineIcons` - reward 30 for Dragoon, 34 for Hydra, 35 for King Dedede, 36 for Meta Knight, every other character unconditional, resolved through `Checklist_CheckCachedUnlock_CityTrial` or `ClearChecker_CheckUnlocked` depending on `Checklist_IsCacheValid`. Appended characters are unconditional, since no checklist reward stands behind a drop-in machine. So a drop-in machine is selectable with this mod alone - including in City Trial's Stadium mode, whose vanilla rule offers the 15 basic characters and hardcodes everything from ckind 15 up out. A filter is what takes that back.

A consumer that gates characters registers a `CustomMachineAvailabilityFilter` through `SetAvailabilityFilter` instead of replacing the packing. The filter receives the candidate and the engine's own answer for it, so it can narrow the roster (`return default && mine`) or ignore the engine entirely. Registering a filter is also what makes `GetGridSentinel`'s cell safe to ignore: the sentinel sits at or past `GetCharacterKindCeiling()`, which the predicate rejects before the filter ever sees it.

## Drop-in audio

A machine that wants a voice of its own ships a `.ssm` sound bank beside its `.dat`, same basename - `machines/VcMine.dat` and `machines/VcMine.ssm`. Nothing on the disc is replaced and the machine archive is untouched; a machine with no companion keeps its clone kind's sounds, which is what the Archipelago Star does.

The bank is an ordinary HAL sound bank holding exactly one record per row slot, in the order above. A slot the author does not supply is a record with a sample rate of 0 and no samples, and that slot keeps the clone kind's id. Records may point into the same data, so a machine whose engine start is its engine loop - which is what every vanilla star does - pays for one copy.

Three things have to line up before one of those samples can play, and `machine_audio.c` sets all three up on the first `vcLoadCommon`, by which point the audio system is running and the DVD is available:

- **The samples have to be in ARAM.** `FGM_GetNextLargestSSMSizeIndex` (`0x80448274`) carves one SSM slot big enough for every companion bank, and each is loaded into it with `FGM_QueueLoad` (`0x8044809c`) plus `FGM_SychronousLoad` (`0x80448220`). Slots are carved for the run of the game, so this happens once; retail leaves about 1.48 MiB of the ARAM sample arena free.
- **They need global sound indices no vanilla bank claims.** A sample is addressed by an index that runs across every bank on the disc, and the vanilla banks tile 0-614 with no gap. A bank declares its own base in its header, which an author cannot choose safely once there is more than one companion bank, so the mod assigns it: the first starts at 615 and each one after it starts past the last. `FGM_LoadBankCallback` (`0x80447ea4`) takes that number out of a staging buffer between the header read and the rest of the load, and a hook at `0x80447eb8` - past the prologue, where the DVD callback's arguments are dead - overwrites it while the bank's own load is the only one in flight. The ceiling is the constant 615 and not a scan of what is loaded: banks come and go per scene, and the star bank is still on disc at the title screen, where the first machine registers. `Audio_AllocPID` (`0x80448f08`) resolves an index through a chained hash and drops the sound on a miss, so appended indices cost nothing, but an index two banks both claim resolves to whichever one the hash chain reaches first.
- **A script has to play them.** An FGM id names a script, not a sample; the script carries the sound's volume and pitch envelope. Each drop-in sound gets a copy of whichever script the clone kind's row names for that slot, with its one `0x01` command's operand rewritten to the new index, so a drop-in engine loop behaves exactly like the donor's. Where the clone kind leaves that slot at -1 - the three boost tiers on all but four stars - the copy is taken from the first star row carrying one, so a drop-in can fill a slot its clone kind never had. Those copies go into an appended SEM bank: the script map is described entirely by `stc_fgm_bank_num`, `stc_fgm_script_num`, `stc_fgm_bank_start_script` and `stc_fgm_script_data`, so widening it is copying both tables into larger mod-owned arrays, appending bank 20 and repointing all four. Every vanilla FGM id keeps its meaning because the appended bank sits past them all. `FGM_LoadAirride.sem` (`0x8005c584`) reinstalls the vanilla map on a scene reset, so a hook at `0x8005c654` puts the appended bank back after each one. Both tables and the script copies are statically sized arrays in the mod image, because the game holds those pointers for the run of the game and `HSD_MemAlloc` only returns memory that outlives the scene when it is called from a mod's boot callback - anything allocated later is reused the moment the scene changes, and the game reads the reused bytes as script pointers.

`scripts/audio/machine_audio.py` builds a bank, either from `.wav` files or by cloning a vanilla machine's:

```
uv run python scripts/audio/machine_audio.py clone slick \
  mods/mymod/assets/machines/VcMine.ssm \
  --fallback warp --pitch 0.82

uv run python scripts/audio/machine_audio.py build machines/VcMine.ssm \
  --engine engine_lp.wav --surface tires_lp.wav
```

Source audio is 16-bit PCM WAV at any rate; the tool encodes to the DSP-ADPCM the hardware wants, generates each sound's coefficient book, and writes the loop point and its decoder context. `--pitch` resamples, which lowers the pitch and lengthens the sound together, the way a bigger engine sounds. `--fallback` fills the roles the donor star leaves at -1 from another star's row. `roles` lists the slot names, `donors` writes the sample behind each of a star's thirteen roles as a `.wav` to hand-edit and feed back through `build`, `info` describes a built bank and `dump` writes its sounds back out as WAVs.

## The machine name and description

Moving the cursor onto an icon draws the machine's name and its description underneath. Both screens get those from their own SIS file - `SisSelply.dat` on Air Ride, `SisSelplyCt.dat` on City Trial, each loaded into SIS slot 0 and each holding exactly 48 entries: two data pointers, six pieces of screen furniture, the 20 machine names at 8-27 and their 20 descriptions at 28-47. A `CharacterKind` becomes a pair of those indices through two 20-entry tables of words read as signed bytes, and each table is read by exactly one instruction pair:

| Screen | Reader | Name table | Description table |
|---|---|---|---|
| Air Ride | `AirRideSelect_SetMachineText` (`0x80153d2c`) | `0x804aa3d8` (`lis` `0x80153d58`, `addi` `0x80153d68`) | `0x804aa428` (`lis` `0x80153d5c`, `addi` `0x80153d6c`) |
| City Trial | `CitySelect_SetMachineText` (`0x8015e740`) | `0x804aa598` (`lis` `0x8015e76c`, `addi` `0x8015e77c`) | `0x804aa5e8` (`lis` `0x8015e770`, `addi` `0x8015e780`) |

Neither table nor either SIS file has a spare entry, so an appended `CharacterKind` reads a text index out of whatever data follows the table and then dereferences past the end of the entry pointer array - a hard crash inside `Text_StorePremadeText` (`0x8044f9d4`) the moment the cursor lands on the icon. `select_text.c` relocates all four tables widened, by the same `lis`/`addi` rewrite the character tables use.

The appended rows point at entries this mod adds to the SIS array itself. `Text_InitPremadeText` resolves an index as `stc_sis_data[sis_id][index]`, so a hook on the epilogue of each screen's loader - `AirRideSelect_LoadSisFile` (`0x8013bacc`, hooked at `0x8013baf0`) and `CitySelect_LoadSisFile` (`0x8013c4a8`, hooked at `0x8013c4cc`) - copies the loaded 48 pointers into a mod-owned array with the extra entries after them and repoints slot 0 at it. That runs per load, because the array it copies lives in the scene's heap.

Entries `48 + 2n` and `48 + 2n + 1` are the *n*-th appended character's name and description, composed from its descriptor in the same SIS opcodes the vanilla entries use - the name centered, kerned, black at scale 0.5 and upper-cased since every vanilla machine name is; the description left-aligned and gray at scale 0.55 in a box 20 units down, with `\n` in the descriptor string becoming a `LINEBREAK` opcode. Vanilla blurbs are two lines of about 24 characters and `FIT_ON` squeezes anything wider, so that is the budget. A machine with no description still gets an entry, composed from an empty string: both indices have to be valid, because the screen draws neither text when either is -1.

## The UI art banks

Every place the game draws a machine picture - the select-screen portrait and machine art, the four results screens, the stadium select, the time-attack board - reads it out of a TexAnim whose animation frame *is* the CharacterKind. Twenty such banks live across eight archives, and a twenty-first matches the search without being one of them:

| Archive | TexAnim | Images | Source frame | Role |
|---|---|---|---|---|
| `MnSelplyAll.dat` | `0x36b98` | 20 | 64x64 CMPR | portrait |
| `MnSelplyAll.dat` | `0x42f3c` | 20 | 80x48 I4 | silhouette |
| `MnSelplyAll.dat` | `0x43394` | 34 | 80x48 C8 | picture |
| `MnSelplyctAll.dat` | `0x52298` | 20 | 64x64 CMPR | portrait |
| `MnSelplyctAll.dat` | `0x5e62c` | 20 | 80x48 I4 | silhouette |
| `MnSelplyctAll.dat` | `0x5ea84` | 34 | 80x48 C8 | picture |
| `MnResultAll.dat` | `0x28e60` | 18 | 40x40 C4 | icon |
| `MnResultAll.dat` | `0x36948` | 20 | 80x48 I4 | silhouette |
| `MnResultAll.dat` | `0x36da0` | 34 | 80x48 C8 | picture |
| `MnResult2All.dat` | `0x23b80` | 18 | 40x40 C4 | icon |
| `MnResult2All.dat` | `0x33d0c` | 20 | 80x48 I4 | silhouette |
| `MnResult2All.dat` | `0x34164` | 34 | 80x48 C8 | picture |
| `MnResult4All.dat` | `0x28f60` | 18 | 40x40 C4 | icon |
| `MnResult4All.dat` | `0x379ec` | 20 | 80x48 I4 | silhouette |
| `MnResult4All.dat` | `0x37e44` | 34 | 80x48 C8 | picture |
| `MnResultCtAll.dat` | `0x8ba0` | 18 | 40x40 C4 | icon |
| `MnResultCtAll.dat` | `0x6578c` | 20 | 80x48 I4 | silhouette |
| `MnResultCtAll.dat` | `0x65be4` | 34 | 80x48 C8 | picture |
| `MnSelstadiumAll.dat` | `0x2b5d8` | 18 | 40x40 C4 | icon |
| `MnBestrapAll.dat` | `0x2a7e8` | 20 | 40x40 C4 | icon |
| `MnSelruleAll.dat` | `0x3ca94` | 20 | 96x24 I4 | not machine art |

Image dimensions vary frame to frame within a bank; the column is the frame the appended ones are cloned from, which is frame 4, the Slick Star. That geometry is also what says which role a bank plays, so four images cover all twenty:

- **portrait** - the character-select grid tile. Fully opaque, with the machine fitted inside the square rather than filling it: the vanilla Slick Star's tile spans the width and leaves half the height as backdrop, and only the longest machines overflow an edge. The backdrop is a flat warm gray `(106, 107, 102)` darkening to about `(79, 80, 84)` along the bottom edge, where a soft contact shadow pools under the machine.
- **picture** - the large art beside the select cursor and on all four results screens. A three-quarter hero view, alpha cutout, no backdrop.
- **silhouette** - drawn directly under the picture at the same size and position, as a soft white bloom. I4, so intensity is also alpha. It is the picture's own alpha under a Gaussian blur at sigma 1.5, which both matches the vanilla look and keeps the two layers registered.
- **icon** - the small machine icon: the time-attack board's tile, and the one a results or stadium-select row carries beside the rider's face. A top-down view, not the hero angle, with a hard black outline and a white halo outside it.

`MnSelruleAll.dat`'s `0x3ca94` holds the rule screen's 20 option labels - "Laps", "Time", "Recommended", "On", "Off" and so on, one per frame - and its animation frame is a rule value, not a CharacterKind. It matches the image-count test only because it happens to hold 20 images, and its 96x24 I4 geometry is no role, so it is skipped.

Each bank grows by appended `ImageDesc`s (plus a TLUT entry apiece when a TLUT track indexes them), rebuilt image and TLUT pointer arrays, bumped counts in the TexAnim header at `+0x14`/`+0x16`, and keys on the AObj's image-index (track 1) and TLUT-index (track 10) ramps.

**How many frames.** The banks grow by a fixed 13, not by however many machines happen to be registered: the ramps are authored keyframe data, so their width is decided when the side-car is built, and 13 is `CUSTOM_MACHINE_MAX`. Unclaimed slots hold the placeholder and no CharacterKind ever reaches them. The widest bank therefore goes 34 -> 47.

**Where the frames go.** The portrait, icon and rule banks index frame for frame. The silhouette and picture banks do not: the engine diverts King Dedede to frame `20 + color` and Meta Knight to `30 + color`, so the frames an appended CharacterKind needs - 20 through 32 - are already Dedede's and would run into Meta Knight's. Both runs move up by the appended count instead, to 33-40 and 43-50, and `ui_frames.c` rewrites the sixteen `addi` immediates that form them. Dedede's eight are `AirRideSelect_SetSIcon2Color` (`0x80151b08`), `AirRideSelect_SetSIcon2Character` (`0x80151bd4`), `CitySelect_SetSIcon2Color` (`0x8015c5c8`), `CitySelect_SetSIcon2Character` (`0x8015c694`) and one per results screen at `0x801672bc`, `0x8016b064`, `0x8016e9bc` and `0x80177b5c`; Meta Knight's are `0x14` past each of those, in the same if/else chain. Those patches apply whether or not a drop-in machine was found, because the splice runs either way - and they are why every bank a patched site drives has to be grown together with the rest.

A vanilla ramp is a `u8` over a `2^3` divisor, which tops out at 31.875 and cannot hold an image index past 31. The values are whole numbers, so the exponent is dropped as far as the new keys need and the format - and with it the buffer length - stays as it was. The fitted `value_flag` travels in the side-car alongside the buffer.

**The quad they are drawn on.** A picture bank's frames are not all the same shape. Frames `0` to `17` run 72x64, 80x48, 64x72 and 80x64; King Dedede's is 56x80 and Meta Knight's 104x52. Nothing in an `ImageDesc` carries an aspect, so the Sicon models size the quad per character instead, through a pair of `SCAX`/`SCAY` `AnimJoint` tracks on the same timeline as the image ramp - the value being the frame's pixel dimensions times about 0.104. Six of the eight donors carry four such tracks apiece, two joints for the picture and the silhouette drawn under it; `MnBestrapAll.dat` and `MnSelstadiumAll.dat` carry none, their icons all being 40x40.

Those tracks have to grow with the image ramps. Left alone, an appended frame draws at the shape of whatever frame it displaced - the appended run takes Dedede's tall 56x80 quad and Dedede, moved to frame 33, falls past the last key into Meta Knight's wide 104x52 one. They widen the same way, except that the new keys hold a constant rather than a running index: the source frame's scale, which is the shape every appended frame is encoded at. They are found by the same test that identifies a picture bank, applied to every `AnimJoint` ramp in the archive rather than to the `TexAnim`'s, and travel in the side-car as a per-donor list of `(FObjDesc offset, buffer, length, value_flag)` beside the banks. `ScMenSelplyIpos`'s anchor tracks key frames 18 and 19 and none at 30, so the test passes over them.

**Where the art comes from.** Growth is applied to the loaded archives rather than shipped as rewritten copies of them; rewriting on disc would mean carrying 3.9 MB of vanilla archive to deliver 86 KB of additions. Only two of the four edits are position-independent and can be baked: an `ImageDesc` with its texels, and a replacement keyframe buffer per ramp. The pointer arrays hold the donor's own descriptors, which move with every load, so they are rebuilt at runtime into a static pool with a fixed slot per bank.

`CmUiFrames.dat` carries one placeholder image per bank, the widened ramps and the widened quad-scale tracks. Twenty banks need only four distinct textures between them and most of their ramps encode to the same bytes, so both are interned and the side-car comes to 25 KB. A registered machine's own art overrides the placeholder in that machine's own slot, and travels beside the machine as `machines/<name>.art` - an archive exporting a `customMachineArt` public, magic `CMAR`, holding one image per bank geometry keyed by the source frame's `(width, height, format)`. A machine with no side-car shows the placeholder, so it still gets a cell.

```
uv run --with pillow python scripts/authoring/make_ui_frames.py

uv run --with pillow python scripts/hsd/make_machine_art.py \
  mods/ap_star/assets/machines/VcStarAp.art \
  --hero art/ap-star-hero.png --topdown art/ap-star-top.png --matte auto
```

Two renders are needed because the icon's angle is not the picture's: a three-quarter hero view from slightly above, and a straight top-down. Both want a generous margin and at least 4x the target resolution - 320x192 or better for the 80x48 slots. Each is cropped to its own alpha before anything is framed, so the margin costs nothing and a subject sitting small in a big canvas still fills its target. Art is then scaled to fit and padded, never stretched or cropped.

A render off a model viewer usually arrives opaque on the viewport's backdrop instead. `--matte RRGGBB` keys a flat one out, and `--matte auto` reads the colour off the four corners and fails if they disagree. Alpha comes from how far a pixel travels from that colour and the backdrop's share is divided back out, so a pod's additive glow keeps its falloff rather than ending on the hard edge a colour-equality key leaves. `--preview <path>` writes the four finished images magnified side by side, which is the only way to judge a 40x40 icon before it is on screen.

**Getting the renders.** A machine archive does not open in a model viewer as it ships. HSDraw types a root by its public name's suffix - `_joint` is a JObj, `_matanim_joint` a MatAnimJoint tree, `_figatree` a FigaTree - and a machine exports one public, a `vcData<Class><Stem>`, which names nothing the viewer can draw. `scripts/hsd/machine_preview.py` writes a viewer-only copy: the same data section and relocations under a public table naming the model root, the shadow model and each animation-bank slot the way the viewer expects. Every joint carries its three LOD meshes at once and a viewer draws all of them, so it also keeps only the DObjs one LOD table asks for - `--lod`, high by default - and a pod's always-drawn glow sprite is in every table and survives with whichever is chosen. The copy is not a disc asset: pruning leaves the LOD tables indexing a flat DObj order the tree no longer has, and nothing binds a machine by these names.

```
uv run python scripts/hsd/machine_preview.py mods/ap_star/assets/machines/VcStarAp.dat
```

It defaults to `out/preview/<stem>_preview.dat`, which `make clean` takes with the rest of `out/`.

To light the render the way City Trial does, `grDataCity1`'s primary light chain is a white ambient plus one INFINITE diffuse+specular light coloured `(255, 255, 217)` at `(-1000, 700, 1500)` with no interest point, so it aims at the origin - the light `stc_main_light` caches at stage init. The other two light groups swap that for a cooler `(216, 216, 255)` ambient under a plain white key at `(-1000, 1000, 1500)`. The platform disc's gloss is a sphere map rather than a specular highlight, so a viewer without sphere-map texgen renders it flatter than the game does.

Whatever the source, an image is re-encoded to suit the bank: RGB5A3 in a picture bank and I4 in a silhouette bank. Format is per frame and `HSD_TObjSetup` (`0x803f7158`) reads the CI-ness of the one frame it is drawing, so an RGB5A3 frame in a CMPR, C4 or C8 bank leaves every other frame alone and needs neither a CMPR encoder nor a quantizer. Its TLUT slot is filled with the donor's and never consulted.

**How the splice runs.** A bank names its donor by data-section offset, which is fixed for GKYE01, and the mod adds the loaded archive's `data` base to reach the live `TexAnim`. All eight archives load through `Gm_LoadGameFile` (`0x80059818`) and are rebuilt on every entry into their scene - the shared menu teardown at `0x80131928` frees them on the way out - so the splice hangs off two hooks in that loader rather than running once. The first, at `0x80059834`, stashes the basename while it is still in a register; the preload-hit and cold-read paths both clobber it before the tail. The second, at `0x800599f8` where those paths converge with the archive in `r30`, matches the name and patches. The engine passes the name with a trailing `.` and does not always match the disc's case (`MnSelplyCtAll` against `MnSelplyctAll.dat`, which `DVDConvertPathToEntrynum` accepts), so the match is case-insensitive and stops at the dot. The side-car and every machine's `.art` are held for the run of the game, because the banks are pointed straight at the images in them.

Three shapes of bank qualify, and the script has to tell them apart from the many other TexAnims in these archives. Most hold one image per character, so an image count of 20 identifies them. The picture banks hold one per character *color* and are wider than the roster, so they are matched on the shape the diverts leave on their ramp instead: a key on every frame `0` to `17`, none on King Dedede's kind 18 or Meta Knight's 19, and one on each of the frames they are diverted to. Matching only on frames 20 and 30 is not enough - `MnAll.dat`, `MnClCheckAll.dat` and `MnSelplym2dAll.dat` all key those frames for unrelated banks.

The small icon banks are the third shape and stop two short of the roster. Everything that draws one reaches the `CharacterKind` through `Machine_GetCKind` (`0x8000b9f4`), which never hands back King Dedede's kind or Meta Knight's - vanilla parks their star slots on the Warp Star - so the banks hold 18 images, one per kind with a machine of its own, under a ramp keying frame `N` to image `N`. An identity ramp on its own fits any strip animation, so the count is what tells them from `MnClCheckAll.dat`'s 39 checklist tiles or `MnSelstadiumAll.dat`'s 24 stadium thumbnails. Their appended frames still start at 20, which leaves 18 and 19 unkeyed and holding image 17 exactly as vanilla does. Ungrown, an appended `CharacterKind` runs past the last key onto that same image, and every results row and the stadium select draw the Flight Warp Star.

## The material cycle

A descriptor may name one joint and a palette, and `machine_palette.c` walks every material hanging off that joint through those colors on a wall clock.

Nothing in an archive can do this on its own. A `MatAnim`'s frame is the machine's state, not elapsed time: `vcAnimationStar` pairs each joint animation with the material animation played alongside it, the moving animation's rate rides on velocity, the charge animation's frame is the 0-100 charge gauge, and both restart when the state changes. The Slick Star's three material animations run 50, 104 and 3 frames. So the colors travel in the descriptor instead and are written into the live materials each frame.

The write goes to `HSD_Material.diffuse`, and it reaches a pixel only where the pipeline lets it. `MObjMakeTExp` (`0x803fa0b4`) builds the first TEV stage from the material's diffuse only when the MObj renders with `RENDER_CONSTANT` and without `RENDER_DIFFUSE`; every texture on the MObj then runs through `TObjMakeTExp` (`0x803f6860`) in lightmap groups - diffuse and ambient first, then specular, then ext - and a stage whose colormap is `REPLACE`, or `BLEND` with a `blending` of 1.0, discards whatever came before it. A material color under such a stage is invisible no matter what is written to it. `PASS` leaves it alone and `MODULATE` shades it, which is the shape a cycling joint's textures have to be in.

`MObjLoad` (`0x803f9f04`) gives every model instance its own `HSD_Material` copy, so each machine on the field is written separately rather than through the archive. The hook is `Machine_AnimThink`'s last call, `Machine_ColAnimThink` at `0x801c6274`, which runs once per machine per frame after the ColAnim overlays and before the draw. The descriptor's joint index is resolved against the archive's `JObjDesc` tree and the live joint found through the back-pointer `JObjLoad` (`0x8040add4`) leaves at `JOBJ+0x84`, so it does not depend on how the engine roots the instance. Phase advances on the time-base delta, so the period holds through slowdown; a gap of a whole cycle or more - no machine of that kind on the field, or the 32-bit tick counter wrapping at about 106 seconds - resumes where it left off instead of jumping.

### The exhaust trail

A machine that asks for a material cycle may also name the particle generators its exhaust comes from, and the same color is written over each of their color operands every frame.

A star archive's `vcAnimationStar` names its trail by index into the vehicle particle bank, which the engine loads from `EfPtclVehicle.dat` as bank 0: two slots at `+0x38` for cruising, three at `+0x40` for boosting, and up to three joints at `+0x4c` to emit from. The bank holds 52 generators. The first `0x3c` bytes of one are fixed fields - which of the bank's six texture groups to sample, size, fade rate, launch velocity, gravity, cone spread - and everything past that is bytecode, stepped once per frame per particle by `Ptcl_TickOne` (`0x8042cce8`). A byte below `0x80` is a wait of that many frames; `0x40 | wait` takes one more byte and sets the sprite image; `0xc0 | channels` and `0xd0 | channels` take a duration and then one byte per channel and ramp the particle's primary and secondary colors toward it; `0xff` ends.

So the color a particle wears is read out of the generator on the frame it spawns. Overwriting the operands of those color opcodes paints the particles born that frame and leaves the ones already in flight alone, which is why the trail comes out as the cycle stretched along it rather than the whole trail flashing at once. `Ptcl_Alloc` (`0x8043294c`) reaches a generator as `descTable[bank][id]` from the table of per-bank descriptor arrays at `0x8058c708`, bounds-checked against the per-bank counts at `0x8058c608`; both are rebuilt on every 3D load, so the descriptor is re-resolved each frame rather than cached.

The generator is bank data, not per-instance like `HSD_Material`, so every machine emitting that index is tinted together. A descriptor avoids that by claiming a slot instead: generators `3` and `8` are the only two of the 52 no machine reads, and `trail_clone` copies the generator the machine wants to look like into one of them, so its animation bank emits and its tint writes a descriptor nothing else on the field touches.

The copy is made by a hook at `0x802354bc`, the tail of `Ptcl_LoadEfPtclVehicle` where both of its install paths meet with the descriptor table in place, and it is remade on every load because the table is rebuilt each time. It cannot be done lazily on the frame path: `Ptcl_Alloc` hands a generator node the descriptor's program pointer once, at creation, and the node holds it for as long as the machine's emitter lives, so a slot swapped one frame late leaves that machine emitting the vanilla generator for the rest of the run. A clone takes a fixed 256-byte span rather than a measured one - descriptors sit back to back with no length in front of them, the largest in the bank is 192 bytes, and the copied program ends at its own `0xff` wherever that falls.

Cruise and boost want different generators. A cruise generator emits for as long as the machine is moving, so it is authored tight and short-lived; a boost generator is a wide, long-lived flare sized for the few seconds a boost lasts. The half-angle of the emission cone is the float at `+0x24` of the generator's fixed fields, in radians, and the emission rate is the float at `+0x28`. A boost generator in a cruise slot therefore reads as a permanent spray rather than a trail.

The color is pushed to full saturation before it is written. Particles blend additively, so wherever a trail overlaps itself the channels sum and clamp, and a pastel palette arrives at that sum as white with a thin colored fringe. Stretching each color to full saturation - `(c - min) * max / (max - min)` per channel - gives up the lightness the blend was going to destroy anyway and keeps the ratio between channels, which is the part that still reads as a color. A machine's seat and its trail therefore share a hue rather than an exact value.

## The City Trial field spawn

`machine_spawn.c` owns the roll that decides which machine appears next on the City Trial field, because a registered machine has no way into the vanilla one. The engine reads a chance row out of `VcCommon.dat` (`(*stc_vcDataCommon)->spawn_data->spawn_desc[]`, three entries selected by match progress) with exactly `VCKIND_NUM` columns, and its selection loop is that same width - so an appended kind has neither a weight it could carry nor an index it could be picked at.

The two spawn sites are `CityMachineSpawn_DecideAndSpawn` (`0x801defac`, normal spawns) and `cityTrialSpawnFormationStar` (`0x801df408`, the Machine Formation event). Both build a `u32` history-exclusion mask from `MachineSpawnData.prev_machine_kind[4]` and then roll in two passes, one summing the weights and one picking against `HSD_Randf`. Widening the mask instead of the roll does not work: the mask is built twice in separate registers, and the point between the two passes clobbers the `f1` holding the random result. So the whole selection is replaced - a hook at `0x801df00c` / `0x801df44c` skipping to `0x801df220` / `0x801df630`, with `r30` = `MachineSpawnData*` and `f1` = match progress on the way in and the chosen kind left in `r31` for the vanilla history write and `CityMachineSpawn_Create`.

The replacement seeds one weight per kind - the chance row for a vanilla kind, the descriptor's `spawn_weight` for a registered one - then keeps the two things the vanilla loop did that are worth keeping: the four-deep spawn history exclusion, shrunk to `min(spawnable - 1, 4)` so the only candidate cannot be excluded by its own history, and the weighted roll itself. With no consumer attached the result is the vanilla distribution plus whatever the descriptors ask for.

**Who gets weighed.** Every kind goes through a `CustomMachineSpawnWeightFilter` if a consumer registered one through `SetSpawnWeightFilter`, receiving the seeded weight and returning the one to use; 0 keeps a kind off the field. A consumer that gates machines returns 0 for a locked kind and can raise a kind the vanilla table gives 0 - the Compact Star, the Flight Warp Star and the two legendaries are all 0 in every window, so without that they could never appear whole on the field however they were unlocked. If a filter zeroes every kind there is nothing to roll, so the roll is skipped and the first kind the filter permits at an even weight of 1.0 is spawned instead; that way a consumer always gets one of its own back rather than a machine it refused.

Free Run is a separate placer and is not touched. `CityMachineSpawn_PickFreeRunKind` (`0x801de41c`) fills the city by drawing uniformly from the kinds not yet placed, over a 0-25 loop that cannot reach an appended `MachineKind` at all.

## The assembly cutscene

A machine whose descriptor names both cutscene archives can be put through the vanilla legendary assembly - the freeze, the scripted camera, the parts flying in on streaks of light, and the rider coming out mounted - with its own art. `machine_cinematic.c` owns it, and a consumer starts a run with `StartAssembly(kind, ply)`.

`VCKIND_DRAGOON` and `VCKIND_HYDRA` are accepted there too and fall through to the engine's own archives. Standing in at the seams below means this file already holds every condition a run has to satisfy, so a consumer handing out a whole legendary gets them for free rather than restating them; vanilla's own piece-collection path reaches `LegendaryMachine_StartAssembly` directly and is untouched either way.

Every machine-specific decision the engine's cutscene takes is reachable at a `bl`, so standing in at three is the whole of it:

| Site | Vanilla | Replacement |
|---|---|---|
| `0x80283914` in `LegendaryMachine_CreateAssembly` | `LegendaryMachine_LoadAssemblyArchive` picks `VsDragoon.dat` or `VsHydra.dat` off the machine index and returns its `vsData` | loads the machine's two archives and assembles the same three-pointer block - a glow-model triple, a parts-model triple, and a pointer to the camera-animation descriptor |
| `0x80283c98`, in phase 3 | `LegendaryMachine_FreeAssemblyArchive` | frees the two archives and clears the latch |
| `0x80283b70` | `Ply_EnterLegendaryAssembly` poses the rider and stages the `(is_bike, class slot)` pair the substate's motion script feeds to `Rider_RespawnFullRecreate` 150 frames later | calls it, then overwrites that pair with the machine's own, which is the whole of pointing the mount at a different machine |

The two archives carry one half each: the glow archive holds the streaks and the camera animation as two publics, the parts archive the parts themselves. That split is how `VsDragoon.dat` and `VsHydra.dat` pair theirs, so a machine's follow the same shape. `cine_machine_index` picks which of the two the run borrows its rider pose, fanfare and sky preset from - nothing else downstream of the archive load reads it.

One cutscene runs at a time, which is the engine's own limit: `GameData+0xa8c` holds a single controller GObj and a second run would tear down the first one's models. A latch names whichever registered machine started the run, and the vanilla pair plays untouched while it is clear. `On3DLoadStart` clears it, because a scene change tears the cutscene's GObjs down without reaching its own last phase.

`StartAssembly` returns 0, and the caller still owes whatever the cutscene would have done, when any of these does not hold:

- **The scene is City Trial.** The cutscene stages its models on the open CT map and drives that scene's sky and area lights; a stadium or an Air Ride race dereferences a null jobj or trips the area-light assert.
- **The rider is Kirby.** `Rider_EnterLegendaryAssembly` (`0x8019248c`) is Kirby-only and the mount rides on the state it enters, so anyone else would get the whole shot and no machine. They take the plain mount instead.
- **Nothing is already running**, by the latch or by `Gm_IsLegendaryAssembling` (`0x8000c934`).
- **A vanilla legendary has not already run this scene.** The engine frees `VsDragoon.dat` / `VsHydra.dat` when a run ends, so a second run in the same scene loads a joint out of the freed archive and crashes in `HSD_JObjLoadJoint`. A second latch, cleared with the others at `On3DLoadStart`, holds one bit per legendary. A registered machine's archives are reloaded per run and carry no such limit.

**Preloading (`machine_preload.c`).** The cutscene's archives are loaded synchronously the moment it starts, so they have to be resident already. `Preload_AllCityFiles` ends by calling `LegendaryMachine_PreloadAssemblyArchives`, which queues the vanilla pair; replacing that call at `0x80262be8` queues the vanilla pair plus everything registered. Each registered machine's two archives are added for it, and a consumer can add its own with `AddCityPreload`. City Trial is the only scene with a preload seam here, which is also the only scene the vanilla call queues for.

## Consuming the registry

`Hoshi_ImportMod(CUSTOM_MACHINES_MOD_NAME, ...)` returns a `CustomMachinesAPI`. Import it after `OnBoot`: hoshi installs mods in `/mods` FST order, which is alphabetical, so most consumers boot before this one and an `OnBoot` import answers NULL. Treat NULL as "no custom machine exists" and fall back to the vanilla ceilings - `custom_machines_api.h` publishes four inlines that do exactly that (`CustomMachines_KindNum`, `CustomMachines_CharacterKindNum`, `CustomMachines_ResolveKind`, `CustomMachines_ClassIndexOf`), each taking the imported pointer and answering with hoshi's own value when it is NULL.

That header documents every call. The ones carrying a design decision rather than a lookup:

- `FindKindByName(name)` is the only handle that ties a specific drop-in `.dat` to feature code; a display name is the link, since kinds are assigned in scan order and nothing may hardcode one.
- `SetAvailabilityFilter` and `SetSpawnWeightFilter` are how a consumer gates a machine. Each replaces one answer inside machinery this mod owns - the select screens' packing and the City Trial spawn roll - rather than the machinery itself, and each is asked per rebuild and per spawn, so a filter may be set from any scene.
- `SetStarInitHandler` / `SetStarThinkHandler` claim the engine's own per-kind slots on the star class, for per-machine work a consumer has no hook of its own for.
- `SetAirRideRowSplit` is mandatory for any code that rebuilds Air Ride's select list, because this mod moved that flag out of the widened list's way.
- `StartAssembly(kind, ply)` runs the legendary assembly cutscene on a registered machine, `VCKIND_DRAGOON` and `VCKIND_HYDRA` included. It returns 0 with the caller still owing whatever the cutscene would have done.
- `GetSelectIconMax()` is the real cap on appended characters, and `GetGridCols()` / `GetGridSentinel()` are what any code walking the character grid itself has to use instead of a literal 10.
- `AddCityPreload(path)` queues a file with City Trial's preload set. The path is not copied.

The API is exported even when the FST folder holds no machine at all, because this mod owns the select screens' packing either way and a consumer still needs the filter.

Three mods import it. `ap_star` owns the Archipelago Star's own behavior and resolves the machine by name to reach it. `archipelago` requires it: the unlock mask and every ceiling it reads come from here, and its gating is three filters - the select-screen availability filter, the spawn weight filter, and the legendary hand-out, which goes through `StartAssembly` rather than driving the cutscene itself. `archipelago_debug` grows its Machines page and its random-give pool by the registered kinds, so a drop-in machine can be locked, unlocked and handed out from the debug menu like any vanilla one.

## The Archipelago Star

The only machine registered today, and what the registry was built for. It is a genuinely new air ride machine rather than a model swap: six colored spheres in a horizontal ring around a central seat, shaped like the Archipelago logo. It is the reward for completing the Archipelago goal, and Kirby rides it on the title screen as a teaser for a machine the player has not earned yet.

| Thing | Value |
|---|---|
| Archive | `machines/VcStarAp.dat`, public `vcDataStarAp` |
| UI art | `machines/VcStarAp.art` |
| Cutscene | `ApStarGlow.dat` + `ApStarParts.dat`, run as Hydra |
| Display name | `"Archipelago Star"` - the handle the mod uses to find it |
| Class | star (`is_bike = 0`), first appended slot: star slot 19, `MachineKind` 26, `CharacterKind` 20 |
| AP item | 856 (`AP_MACHINE_UNLOCK_BASE + 26`), continuing the machine band past the 855 gap |
| Save bit | `machine_unlocked_mask` bit 26 |
| Logo colors | `#C97682` rose, `#75C275` green, `#CA94C2` violet, `#D9A07D` tan, `#767EBD` blue, `#EEE391` yellow |

Once the AP item has unlocked it, the machine is also assembled in City Trial the way the
two vanilla legendaries are: six colored spheres, one per logo color, dropped in the
forced-content red boxes across a round. Collecting the set is the `ap_star` mod's, not the
registry's; the cutscene that follows is the registry's, driven by the two archives the
star's descriptor names, and `ap_star` starts it through `StartAssembly`.

None of those kind numbers is hardcoded anywhere. They are what a single registered machine happens to get, not constants anyone depends on: the display name in `AP_STAR_MACHINE_NAME` is the one link between the generic registry and the Archipelago feature wiring, and everything downstream resolves through the archipelago mod's `MachineKind_Num()`, `CharacterKind_Num()` and `MachineKind_Resolve()`, which fall back to the vanilla ceilings when this mod is not built.

`AP_ResolveCustomMachines()` in `mods/archipelago/src/main.c` imports the API, deferred past `OnBoot` and retried on each call - the archipelago mod boots first and asks for the registry during its own `OnBoot`, while pointing the title demo at the star, so a failed import proves nothing. That mod only concludes the registry is absent in `OnSaveLoaded`, the first point past every mod's `OnBoot`. Concluding it earlier makes it patch the select screens underneath the registry's own patches, and since a hoshi hook stub ends with the instruction it displaced, the two chain and both packers run - the second wins, and the 10-column fallback cannot see an appended character in column 10.

**Title screen.** The title scene's demo player is set up at `0x8000d300` from three `li r4` operands: `RiderKind` at `0x8000d340`, `is_bike` at `0x8000d34c`, class slot at `0x8000d358`. `main_menu.c` rewrites all three on each title entry, since the registry only resolves after every mod has booted, pointing them at Kirby on the Archipelago Star and falling back to King Dedede on the Wagon Star when no such machine is registered. The ride must stay star-class: the demo init uses hardcoded star-only state ids and a wheel-class machine crashes there. The Wagon Star's idle volume floor of 20.0 is why the title think and exit callbacks zero the floor for whichever kind the demo uses and put it back on the way out; a kind whose floor is already 0.0 passes through unchanged, and machine loops are only ever created at volume 0.0 and ramped up, so this never lets an audible frame through.

**The sound.** No `.ssm` ships beside the archive, so the star keeps its clone kind's row whole and sounds like a Slick Star.

**The model.** `VcStarAp.dat` is `VcStarSlick.dat` with the Slick Star's three engine pods turned into six on an even ring, one per logo color, over a repainted platform. The ring sits at radius 2.20 against the donor's 2.45 and height 1.05 against 0.875, each pod scaled to 1.4375; a charging pod is held 10% of the way to the donor's white flash, and the platform rests at `#BFF5BF` shaded no darker than 0.75 of it, walking around the pod palette every 12 seconds.

Nothing is remodelled. The three new pods are new JObjs hung off the same ring pivot whose DObjs point at the donor's own POBJs through copied DObj/MObj/TObj records, so the archive carries 27 DObjs over 15 pieces of geometry and every donor animation still plays. The joint layout it produces:

| Joint | Role |
|---|---|
| 0-5 | donor chain, no animation keys |
| 6 | body root, the platform disc as three LOD DObjs |
| 7 | seat hub |
| 8 | ring pivot - the Moving FigaTree's only animated node, three ROT tracks driving the idle spin of all six pods |
| 9-14 | the six pods, children of joint 8, four DObjs each: three LODs plus an always-drawn XLU glow sprite |
| 15 | boost/exhaust particle joint |
| 16 | rider seat joint |

Bone count is 17. Four things outside the JObj tree are keyed to that numbering and the builder repatches all of them: `ModelData.BoneCount`; the three main LOD tables, whose bytes are flat DObj indices in JObj preorder; the Moving FigaTree's per-node track-count table; and the three MatAnimJoint trees, which are walked in lockstep with the JObj tree and so grow the same three nodes. Because the new pods precede the particle and seat joints in preorder, `VehicleAttributes+0x00` (the rider sit bone) and `AnimationBank+0x4c` (the particle spawn bone) shift up by three.

A pod's color is animated rather than static: each of the Moving, Charge and Stop MatAnims drives its material's DIFFUSE_R/G/B, black under the body texture and magenta on the glow sprite, ramping warm while charging. Recoloring therefore rewrites those keyframes as well as the material color and the pod's own tinted copy of the 64x64 body texture, keeping each key's intensity and swapping the hue. The Charge tracks are the only ones with desaturated keys - the donor flashes its pods white at 100 charge - and a key is allowed only 10% of the way back toward white, so a charging pod climbs to a slightly hotter version of its own color rather than to white. A track whose fixed-point exponent cannot hold the brighter value has its exponent lowered rather than its format changed, which keeps every keyframe buffer the same length.

The six colors and their ring order are the assembly pieces' own list in `scripts/authoring/make_ap_star_pieces.py`, imported rather than restated, so a pod and the sphere the player collects for it can never drift apart. Slot 0 sits on +Z, the machine's front, and the rest run clockwise seen from above. The same list is written into the archive as the descriptor's palette, which is what the platform cycles through.

The platform disc is repainted the other way around, so that its color can be animated at all. The donor drives it entirely from textures: a striped 256x256 map `REPLACE`s the color and contributes the 1-bit alpha that cuts the disc into a grate, and a magenta sphere map then `BLEND`s over that at full strength, so the material color never survives to a pixel. The builder cuts the first stage down to a single opaque texel with both channels set to `PASS` - which retires the Slick Star's stripes, and with them the cutout, leaving a solid disc - and turns the sphere map into gray with its luminance range stretched onto `[0.75, 1.0]`, `MODULATE`d over the material. The material renders with `RENDER_CONSTANT` and no lighting, so its diffuse is the disc's whole color and the gloss is all the shading it has. The 32 KB the stripes occupied holds the 32-byte replacement and is otherwise zeroed, so the retirement costs no file size.

The exhaust keeps the donor's own particles - the four-point sparkle at texture group 0 image 1, generator 20 while cruising and 51 on boost - and only changes color. They emit off the machine's axis and drift, which is what a machine that slides needs: a generator that emits down a tight cone pins its trail to where the machine is pointed, and a Slick Star spends much of its time not facing the way it is going.

Those are the Slick Star's own generators and the Archipelago Star is a Slick Star clone, so it does not emit them directly. The descriptor claims the bank's two spare slots - 20 is copied into 3 and 51 into 8 - the animation bank names the copies, and the eight `(generator, offset)` tint pairs address the copies too. The four offsets, the same in both because 51 is 20's program with padding after it, are `0x53`, `0x5a`, `0x60` and `0x89`: the two colors a particle spawns holding, the one it ramps up to over its first three frames, and the one it fades out through. Before writing them the builder checks each still sits behind a color opcode in `EfPtclVehicle.dat`, and that no `Vc*.dat` on the disc emits either claimed slot.

The quick-spin deformation is a per-frame scale of the pod joints' translation, driven from mod code because the animation bank has no slot for it. Modelled spheres and the real sound mix are the remaining art work; everything the engine needs around them is in place.

## Known gaps

These are the places a custom machine is still second-class. None of them corrupt state; each is a missing surface.

- **The select screens hold 33 icons.** That is 13 appended characters, which is what freeing the bytes past each screen's packed list buys - City Trial binds at 33, Air Ride would take 37. A machine whose character does not fit is still registered and still drives; the registry just does not give it a `CharacterKind`, and so no select-screen cell, no name and description, and no picture.
- **A machine cannot have its own City Trial blip.** It borrows its `clone_kind`'s. `ScInfWarpstarct`'s TexAnim in `IfAll2c.dat` is 18 frames and all 18 are claimed with no reachable duplicate, so a distinct icon means growing that bank the way the character-indexed banks are grown - a `UiFrameBank` at TexAnim `0xed8`, image and TLUT tracks at `0xc60` and `0xc74` - and widening both the 26-entry frame table at `0x804a85f8` and the 26-entry template array at `0x805581ec`, which has zero bytes of slack before the per-player instance joints.
- **Only the star class can be widened.** A descriptor with `is_bike` set is rejected at discovery. The bike class's tables are the same shape and would take the same treatment, but nothing has needed it.
