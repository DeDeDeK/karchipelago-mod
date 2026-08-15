# Custom Machines

`mods/custom_machines/` adds air ride machines to the game that vanilla does not have. It is a registry in the `custom_*` family: it owns no machine of its own, it scans a drop-in folder on the disc and widens the engine tables so whatever it finds gets a real `MachineKind`, a real class slot, an audio parameter row and (optionally) a `CharacterKind` with a cell on the select grids. The one machine this repo ships through it, the Archipelago Star, is described near the end.

## Where a machine comes from

Every `.dat` under the FST folder `machines/` is a candidate. An archive qualifies when it exports two publics:

| Public | Contents |
|--------|----------|
| the machine's own `vcData` symbol | the ordinary engine machine archive, exactly as `VcStarSlick.dat` and friends ship it |
| `customMachine` | a `CustomMachineDesc` describing how to register the archive |

`CustomMachineDesc` is 0x28 bytes and is declared in `mods/custom_machines/include/custom_machines_api.h`:

| off | field | notes |
|---|---|---|
| 0x00 | `magic` | `0x434D4348`, big-endian ASCII `CMCH` |
| 0x04 | `version` | 2 |
| 0x08 | `name` | display name, e.g. `"Archipelago Star"` |
| 0x0c | `symbol` | the `vcData` public in this same archive |
| 0x10 | `is_bike` | machine class; only the star class (0) is supported |
| 0x14 | `wants_character` | also take a `CharacterKind` and a select-grid cell |
| 0x18 | `rider_kind` | `RiderKind` for the appended `CharacterDesc` row |
| 0x1c | `clone_kind` | star `MachineKind` whose per-kind engine rows it inherits |
| 0x20 | `spawn_weight` | City Trial loose-spawn weight; 0 never spawns |
| 0x24 | `description` | select-screen blurb, v2 and up; `\n` breaks the line |

A descriptor is rejected when its version is newer than the mod supports, so the field it does not know about is never read. A v1 descriptor ends where `description` starts and is still accepted; the machine gets an empty blurb.

Registration order is FST scan order, and it decides the appended kinds: the *n*-th machine found takes star slot `19 + n`, `MachineKind` `VCKIND_NUM + n` and, if it asked for one, `CharacterKind` `CKIND_NUM + n`. `CUSTOM_MACHINE_MAX` caps the registry at 4, because each extra character costs a select-screen icon anchor and those are authored art.

Discovery runs at `OnBoot` and cannot use `Archive_LoadFile`: that allocates from the per-scene heap, which does not exist yet. The mod reads each candidate with `DVDConvertPathToEntrynum` + `File_Read` into an `HSD_MemAlloc` buffer and calls `Archive_Init` on it, so the archives stay resident for the whole boot.

A machine may also drop a `.ssm` sound bank of the same basename next to its `.dat`, which gives it a voice of its own instead of its clone kind's.

## Building an archive

`scripts/hsd/clone_machine.py` produces one from a vanilla machine:

```
uv run python scripts/hsd/clone_machine.py iso/files/VcStarSlick.dat \
  mods/custom_machines/assets/machines/VcStarAp.dat vcDataStarAp \
  --name "Archipelago Star" \
  --description "A gift from another world.\nRides like a Slick Star." \
  --recolor ap
```

It renames the source archive's single public, appends a `CustomMachineDesc` under a second `customMachine` public (with a relocation per string pointer), and optionally recolors the model. Recoloring rewrites the two RGB565 endpoints of every DXT1 sub-block in each distinct CMPR texture, preserving the per-block index bytes and the `c0 > c1` relation that selects opaque versus 1-bit-alpha mode. It has to work on the textures rather than the materials: a machine's MObjs are `CONSTANT|TEX0` with a white diffuse color, so nothing about them is tinted.

`mods/*/assets/` is copied to the disc root by the ordinary asset step, so `mods/custom_machines/assets/machines/*.dat` lands at `machines/` on disc with no packaging change.

## What a machine archive holds

The `vcData` public is seven pointers:

| off | field | notes |
|---|---|---|
| 0x00 | attributes | 0x1F0 bytes; `Machine_AdjustAttributes` (`0x801c7278`) memcpys it to `MachineData+0x460`. Named fields include the rider sit bone index, base HP, ground and air top speed, the glide parameters and the quick-spin tornado size |
| 0x04 | model data | 0x2C: main model root, bone count, six LOD table slots (main/boost x high/mid/low), shadow root at `+0x28` |
| 0x08 | collision group | |
| 0x0c | collision attributes | 0x38, an analytic float table - there is no collision mesh anywhere in the archive |
| 0x10 | collision sphere | 0x18 |
| 0x14 | handling attributes | 0xF8 |
| 0x18 | animation bank | 0x7C: moving / unk1 / unk2 / boost / charge / stop FigaTree and MatAnim pairs, particle spawn ids and bones, boost and after-boost sound ids |

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

Export as glTF 2.0: its node TRS animation channels map almost one to one onto FigaTree `TRAX/Y/Z`, `ROTX/Y/Z` and `SCAX/Y/Z` tracks, which dodges the dropped-scale-keyframe problem the SMD path has. `scripts/hsd/fobj.py` is the packed-keyframe codec those tracks need, decode and encode; it round-trips all 21604 non-particle keyframe tracks in the 227 retail archives with no semantic differences, 154 of them one byte shorter than the retail exporter, which sometimes pads a run header with a redundant LEB128 continuation byte.

## The engine tables it widens

The engine addresses a machine as a `(is_bike, class slot)` pair. Stars are slots 0-18 and their slot equals their `MachineKind`; bikes are slots 0-6 with `MachineKind` `19 + slot`. A custom machine is therefore the only kind whose class slot and `MachineKind` differ, which is why consumers must resolve kinds through the API rather than through hoshi's `MachineKind_ClassIndex` / `MachineKind_FromClassIndex` (those describe vanilla and are still correct for it).

**Star class (`machine_registry.c`).** The class grows from 19 slots to `19 + CUSTOM_MACHINE_MAX`.

- The per-class `{filename, symbol}` name table is reached as `(*(char ***)0x805d6f90)[is_bike]`. The mod copies the 19 vanilla pairs into a wider table, appends each registered machine's path and symbol at its slot, and repoints the row. The class-shared archive's own pair lives in a separate table at `0x804b07e0`.
- `stc_vcDataLookup` (`0x8055a068`, `vcData *[2][19]`) is relocated into a mod-owned `[2][19 + N]`. Exactly three sites read it. Two are replaced outright - `vcData_InitLookup` (`0x801c6c68`) and `Vehile_LoadFile` (`0x801c6d74`), both small. The third, `Machine_StoreVcDataPtr` (`0x801c4f98`), is patched instruction by instruction instead: its `lis` at `0x801c4fd0`, its `addi` at `0x801c4fe8` and its row stride `mulli` at `0x801c5034`. A hook there would clobber caller-saved registers the surrounding code still needs.
- `MachineDesc_SetKindAndIsBikeFromMachineKind` (`0x801c857c`) is the one canonical `MachineKind -> (is_bike, slot)` split, with 14 callers including the City Trial spawn path. Replacing it teaches all of them about the appended kinds at once.
- City Trial preloads machines by walking a 26-byte enable table at `0x804b07f0` inside `Machine_PreloadAll` (`0x801c8cec`). A hook on the branch out of that loop (`0x801c8d8c`) preloads the registered archives too.
- Two 19-entry tables of machine-specific handlers, `0x804b15c0` and `0x804b160c`, sit back to back and end exactly where the star class's own descriptor begins. `Machine_Star_Init` (`0x801e7f3c`) and `Machine_Star_Think` (`0x801eacbc`) each end by loading their table at `MachineData.kind` and calling the result if it is non-NULL, with no bounds check - so slot 19 reads the descriptor's first word and branches into rodata. Only Hydra, Formula, Wagon and Turbo have handlers. Each table is read by exactly one site that does nothing but form the address, so both are relocated by rewriting the reader's `lis`/`addi` pair: `0x801e80dc`/`0x801e80e0` and `0x801eb524`/`0x801eb528`. A custom slot takes the handlers of its descriptor's `clone_kind`, which is NULL for the 15 stars that have none.

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

**Characters (`character_registry.c`).** Three DOL tables describe the roster and sit back to back with no slack: the select-screen linear strip (`0x804957ec`, 20 bytes), the select-screen grid (`0x80495800`, 2x10) and `CharacterDesc` (`0x80495814`, 20x3). Each is read by exactly one accessor that does nothing but form an address, so all three are relocated by rewriting the `lis`/`addi` pair inside the accessor - no hook, no register hazards:

| Accessor | `lis` | `addi` |
|---|---|---|
| `SelIcon_GetCKindLinear` | `0x8000b9a8` | `0x8000b9b0` |
| `Icon_GetCKind` | `0x8000b9c0` | `0x8000b9cc` |
| `Character_GetDesc` | `0x8000b9e0` | `0x8000b9e8` |

The grid gains a column per two appended characters, and its runtime row stride is written into `Icon_GetCKind`'s `mulli r5, r0, cols` at `0x8000b9c4`. Appended characters fill the new columns row 0 first, so a single addition gives an 11/10 grid rather than 10/11. At most one cell is left over; it holds a sentinel `CharacterKind` one past the last real one, which indexes a zeroed `CharacterDesc` row and which every availability predicate rejects, so it can never be packed into a select list. The appended `CharacterDesc` row is `{rider_kind, 0, star_slot}` - class-relative, as every vanilla row is.

A character is only appended while the select screens still have an icon for it, so `GetSelectIconMax()` is the real cap on appended characters even though `CUSTOM_MACHINE_MAX` machines can register. A machine past it still loads, drives, spawns in City Trial and can be handed out; it just is not on the grids.

**Select screens (`select_screen.c`).** This mod both widens the screens and packs them, because both screens build their icon lists by walking the character grid it widened. Vanilla's packing loops are a hard 10 columns into two 10-byte stack rows, so an 11th column has nowhere to land and an appended character stays invisible however well it is registered.

Four things cap a select screen at 20 icons, and all four are moved here:

- The packed icon list in `GameData` - a count at select base `+0x65` and one `CharacterKind` per icon from `+0x66` - is followed immediately by a live byte: Air Ride's row-layout flag at `+0x7a` and City Trial's debug-grid flag at the same offset in its own block. `+0x7d` is untouched on either screen and inside the block each screen's init memsets, so both flags move there by rewriting the displacement of every `lbz`/`stb` that reaches them: nine sites on Air Ride (`0x80020b04`, `0x80020b4c`, `0x80020b98`, `0x800214a4`, `0x80027f60`, `0x800285c8`, `0x80028818`, `0x80028970`, `0x80029c7c`) and six on City Trial (`0x8002e444`, `0x80038d00`, `0x8003a150`, `0x8003a15c`, `0x8003ac3c`, `0x8003ac48`). Code that rebuilds the Air Ride list has to write the flag through `SetAirRideRowSplit` rather than at the vanilla offset.
- The icon positions come from a strip of 20 anchor joints posed by an animation whose frame is the icon count, which has no key past 20. `AirRideSelect_LayoutIcons` and `CitySelect_LayoutMachineIcons` are replaced: they call the engine's pass, which is exact up to 20 icons, and past that redo the grid arithmetically - `spacing = 2H / max(ceil(N/2) - 1, floor(N/2) - 0.5)` about a fixed half-width `H` of 30.62 (Air Ride) or 25.20 (City Trial), with the shared icon scale multiplied by the spacing ratio so the tiles still fit their columns. The icon with no anchor is held in the mod, because its `Vec3` in the ipos userdata would land on the trailing scale and count; `AirRideSelect_GetIconPos` and `CitySelect_GetIconPos` are replaced to hand it out.
- The icon GObj array in `ScMenuCommon` holds 20 pointers and its writer indexes it unguarded, so the 21st overwrites the `JOBJSet` pointer that follows. Nothing reads the array, so the two icon creators are wrapped to put that pointer back as soon as the store lands.
- The packing itself. `AirRide_PopulateSelectIcons` (`0x80020a08`) is replaced outright. City Trial's lives inside `CitySelect_CreateMachineIcons` (`0x8002e3c4`) and is taken over with three hooks - the two counting passes at `0x8002e4d0` (Stadium) and `0x8002e5c0` (Free Run), both returning the count in r27 and exiting to `0x8002e670`, and the tail at `0x8002f0b8` that packs the list, lays it out and creates the icons. The two array-building passes in between have nothing left to build, so `0x8002e67c` and `0x8002e738` branch straight to the tail; that also skips the reorder between them, which assumes the vanilla grid's fixed positions for the legendary machines and duplicates icons on a packed list.

Two patches are not about widening:

- Vanilla's `AirRide_CheckCharacterAvailable` switches on a 20-entry jump table and falls through to the checklist query with an uninitialised reward index for anything past it, so its out-of-range branch at `0x80020924` is retargeted to the `return 1` arm. The packing above never asks it about an appended character, but other engine callers can.
- `CitySelect_Cursor1InputThink` splits cursor rows at `num >= 10` (`cmpwi r3, 9; ble` at `0x80031350`), while the grid renderer keeps up to 10 icons on one drawn row and only wraps at 11 - so at exactly 10 the cursor splits 5+5 across a single row. Vanilla City Trial only ever produces counts 15-20; a filtered roster can land on 10, so the compare becomes `cmpwi r3, 10`.

**Who fills the list.** Packing asks an availability predicate per `CharacterKind`. With no consumer attached that predicate is the engine's own: `AirRide_CheckCharacterAvailable` on Air Ride, and on City Trial the equivalent the engine only ever inlined into `CitySelect_CreateMachineIcons` - reward 30 for Dragoon, 34 for Hydra, 35 for King Dedede, 36 for Meta Knight, every other character unconditional, resolved through `Checklist_CheckCachedUnlock_CityTrial` or `ClearChecker_CheckUnlocked` depending on `Checklist_IsCacheValid`. Appended characters are unconditional, since no checklist reward stands behind a drop-in machine. So a drop-in machine is selectable with this mod alone - including in City Trial's Stadium mode, whose vanilla rule offers the 15 basic characters and hardcodes everything from ckind 15 up out. A filter is what takes that back.

A consumer that gates characters registers a `CustomMachineAvailabilityFilter` through `SetAvailabilityFilter` instead of replacing the packing. The filter receives the candidate and the engine's own answer for it, so it can narrow the roster (`return default && mine`) or ignore the engine entirely. Registering a filter is also what makes `GetGridSentinel`'s cell safe to ignore: the sentinel sits at or past `GetCharacterKindCeiling()`, which the predicate rejects before the filter ever sees it.

## Drop-in audio

A machine that wants a voice of its own ships a `.ssm` sound bank beside its `.dat`, same basename - `machines/VcStarAp.dat` and `machines/VcStarAp.ssm`. Nothing on the disc is replaced and the machine archive is untouched; a machine with no companion keeps its clone kind's sounds.

The bank is an ordinary HAL sound bank holding exactly one record per row slot, in the order above. A slot the author does not supply is a record with a sample rate of 0 and no samples, and that slot keeps the clone kind's id. Records may point into the same data, so a machine whose engine start is its engine loop - which is what every vanilla star does - pays for one copy.

Three things have to line up before one of those samples can play, and `machine_audio.c` sets all three up on the first `vcLoadCommon`, by which point the audio system is running and the DVD is available:

- **The samples have to be in ARAM.** `FGM_GetNextLargestSSMSizeIndex` (`0x80448274`) carves one SSM slot big enough for every companion bank, and each is loaded into it with `FGM_QueueLoad` (`0x8044809c`) plus `FGM_SychronousLoad` (`0x80448220`). Slots are carved for the run of the game, so this happens once; retail leaves about 1.48 MiB of the ARAM sample arena free.
- **They need global sound indices no vanilla bank claims.** A sample is addressed by an index that runs across every bank on the disc, and the vanilla banks tile 0-614 with no gap. A bank declares its own base in its header, which an author cannot choose safely once there is more than one companion bank, so the mod assigns it: the first starts at 615 and each one after it starts past the last. `FGM_LoadBankCallback` (`0x80447ea4`) takes that number out of a staging buffer between the header read and the rest of the load, and a hook at `0x80447eb8` - past the prologue, where the DVD callback's arguments are dead - overwrites it while the bank's own load is the only one in flight. The ceiling is the constant 615 and not a scan of what is loaded: banks come and go per scene, and the star bank is still on disc at the title screen, where the first machine registers. `Audio_AllocPID` (`0x80448f08`) resolves an index through a chained hash and drops the sound on a miss, so appended indices cost nothing, but an index two banks both claim resolves to whichever one the hash chain reaches first.
- **A script has to play them.** An FGM id names a script, not a sample; the script carries the sound's volume and pitch envelope. Each drop-in sound gets a copy of whichever script the clone kind's row names for that slot, with its one `0x01` command's operand rewritten to the new index, so a drop-in engine loop behaves exactly like the donor's. Those copies go into an appended SEM bank: the script map is described entirely by `stc_fgm_bank_num`, `stc_fgm_script_num`, `stc_fgm_bank_start_script` and `stc_fgm_script_data`, so widening it is copying both tables into larger mod-owned arrays, appending bank 20 and repointing all four. Every vanilla FGM id keeps its meaning because the appended bank sits past them all. `FGM_LoadAirride.sem` (`0x8005c584`) reinstalls the vanilla map on a scene reset, so a hook at `0x8005c654` puts the appended bank back after each one. Both tables and the script copies are statically sized arrays in the mod image, because the game holds those pointers for the run of the game and `HSD_MemAlloc` only returns memory that outlives the scene when it is called from a mod's boot callback - anything allocated later is reused the moment the scene changes, and the game reads the reused bytes as script pointers.

`scripts/audio/machine_audio.py` builds a bank, either from `.wav` files or by cloning a vanilla machine's:

```
uv run python scripts/audio/machine_audio.py clone slick \
  mods/custom_machines/assets/machines/VcStarAp.ssm \
  --roles engine,surface,engine-start,surface-start --pitch 0.82

uv run python scripts/audio/machine_audio.py build machines/VcMine.ssm \
  --engine engine_lp.wav --surface tires_lp.wav
```

Source audio is 16-bit PCM WAV at any rate; the tool encodes to the DSP-ADPCM the hardware wants, generates each sound's coefficient book, and writes the loop point and its decoder context. `--pitch` resamples, which lowers the pitch and lengthens the sound together, the way a bigger engine sounds. `roles` lists the slot names, `info` describes a built bank and `dump` writes its sounds back out as WAVs.

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

Every place the game draws a character - the select-screen portrait, name plate and machine picture, the four results screens, the time-attack board, the rule screen's machine name - reads its art out of a TexAnim whose animation frame *is* the CharacterKind. Sixteen such banks live across eight archives:

| Archive | TexAnim | Images | Frames | What |
|---|---|---|---|---|
| `MnSelplyAll.dat` | `0x36b98` | 20 | 64x64 CMPR | Air Ride select portrait |
| `MnSelplyAll.dat` | `0x42f3c` | 20 | 80x48 I4 | Air Ride select machine silhouette |
| `MnSelplyAll.dat` | `0x43394` | 34 | 80x48 C8 | Air Ride select machine picture |
| `MnSelplyctAll.dat` | `0x52298` | 20 | 64x64 CMPR | City Trial select portrait |
| `MnSelplyctAll.dat` | `0x5e62c` | 20 | 80x48 I4 | City Trial select machine silhouette |
| `MnSelplyctAll.dat` | `0x5ea84` | 34 | 80x48 C8 | City Trial select machine picture |
| `MnResultAll.dat` | `0x36948` | 20 | 80x48 I4 | results machine silhouette |
| `MnResultAll.dat` | `0x36da0` | 34 | 80x48 C8 | results machine picture |
| `MnResult2All.dat` | `0x33d0c` | 20 | 80x48 I4 | results machine silhouette |
| `MnResult2All.dat` | `0x34164` | 34 | 80x48 C8 | results machine picture |
| `MnResult4All.dat` | `0x379ec` | 20 | 80x48 I4 | results machine silhouette |
| `MnResult4All.dat` | `0x37e44` | 34 | 80x48 C8 | results machine picture |
| `MnResultCtAll.dat` | `0x6578c` | 20 | 80x48 I4 | results machine silhouette |
| `MnResultCtAll.dat` | `0x65be4` | 34 | 80x48 C8 | results machine picture |
| `MnBestrapAll.dat` | `0x2a7e8` | 20 | 40x40 C4 | time-attack board icon |
| `MnSelruleAll.dat` | `0x3ca94` | 20 | 96x24 I4 | rule-screen machine name |

Image dimensions vary frame to frame within a bank; the column is the one the appended frame is cloned from. The machine drawn beside a cursor is two layers of the same art at the same size, always posed together: a colored C8 picture over an I4 silhouette. Only the picture needs a variant per character color, which is why the two banks hold different numbers of images for the same 20 characters.

`scripts/hsd/add_ui_frame.py` grows all of them at once, writing the rewritten archives into `assets/` where the ordinary asset step drops them over the disc originals:

```
uv run --with pillow python scripts/hsd/add_ui_frame.py iso/files/Mn*.dat \
  --out-dir mods/custom_machines/assets --image mods/archipelago/assets/ap-icon.png
```

Each bank grows by an appended `ImageDesc` (plus a TLUT entry when a TLUT track indexes one), rebuilt image and TLUT pointer arrays, bumped counts in the TexAnim header at `+0x14`/`+0x16`, and a key on the AObj's image-index (track 1) and TLUT-index (track 10) ramps. The new frame's art is the given PNG re-encoded to suit the bank: RGB5A3 in a picture bank and I4 in a text bank. Format is per frame and `HSD_TObjSetup` (`0x803f7158`) reads the CI-ness of the one frame it is drawing, so an RGB5A3 frame in a CMPR, C4 or C8 bank leaves every other frame alone and needs neither a CMPR encoder nor a quantizer. Its TLUT slot is filled with the donor's and never consulted.

Two shapes of bank qualify, and the script has to tell them apart from the many other TexAnims in these archives. Most hold one image per character, so an image count of 20 identifies them. The picture banks hold one per character *color* and are wider than the roster, so they are matched on the shape the diverts leave on their ramp instead: a key on every frame `0` to `17`, none on King Dedede's kind 18 or Meta Knight's 19, and one on each of the frames they are diverted to. Matching only on frames 20 and 30 is not enough - `MnAll.dat`, `MnClCheckAll.dat` and `MnSelplym2dAll.dat` all key those frames for unrelated banks.

The portrait, board and rule banks index frame for frame. The silhouette and picture banks do not: the engine diverts King Dedede to frame `20 + color` and Meta Knight to `30 + color`, so frame 20 - where the 21st character lands - is already Dedede's. The rewritten banks put the appended entry on frame 20 and slide Dedede's run one frame later, one key on the silhouettes and a color apiece on the pictures. Meta Knight's run begins after the gap that slide runs into, so it stays put. `select_screen.c` patches the eight `addi rD, rS, 20` sites that form Dedede's frame to add 21 instead: `AirRideSelect_SetSIcon2Color` (`0x80151b08`), `AirRideSelect_SetSIcon2Character` (`0x80151bd4`), `CitySelect_SetSIcon2Color` (`0x8015c5c8`), `CitySelect_SetSIcon2Character` (`0x8015c694`), and one per results screen at `0x801672bc`, `0x8016b064`, `0x8016e9bc` and `0x80177b5c`. Those patches apply whether or not a drop-in machine was found, because the widened archives ship either way - and they are why every bank a patched site drives has to be grown together with the rest.

## Consuming the registry

`Hoshi_ImportMod(CUSTOM_MACHINES_MOD_NAME, ...)` returns a `CustomMachinesAPI`. Import it after `OnBoot` - mods boot alphabetically and most consumers boot first - and treat a NULL result as "no custom machine exists", falling back to the vanilla ceilings.

| Call | Use |
|---|---|
| `GetCount()` | machines registered this boot |
| `GetKindCeiling()` / `GetCharacterKindCeiling()` | one past the highest kind in use |
| `KindFromClassIndex(is_bike, slot)` / `ClassIndexFromKind(kind, &is_bike)` | the custom-aware conversions |
| `GetName(kind)` / `FindKindByName(name)` | display name, and the only handle that ties a specific drop-in `.dat` to feature code |
| `GetSpawnWeight(kind)` | City Trial loose-spawn weight |
| `GetGridCols()` / `GetGridSentinel()` | select-grid geometry, for code that walks the grid itself |
| `GetSelectIconMax()` | icons a select screen can lay out |
| `SetAirRideRowSplit(base, two_rows)` | Air Ride's row-layout flag, which this mod relocates |
| `SetAvailabilityFilter(fn)` | gate who gets a select-screen icon; NULL restores the engine's roster |

The API is exported even when the FST folder holds no machine at all, because this mod owns the select screens' packing either way and a consumer still needs the filter.

Two mods import it. `archipelago` resolves its machine by name and drives gating, spawn weights, the unlock mask and display names off the runtime ceilings. `archipelago_debug` grows its Machines page and its random-give pool by the registered kinds, so a drop-in machine can be locked, unlocked and handed out from the debug menu like any vanilla one.

## The Archipelago Star

The only machine registered today, and what the registry was built for. It is a genuinely new air ride machine rather than a model swap: six colored spheres in a horizontal ring around a central seat, shaped like the Archipelago logo. It is the reward for completing the Archipelago goal, and Kirby rides it on the title screen as a teaser for a machine the player has not earned yet.

| Thing | Value |
|---|---|
| Archive | `machines/VcStarAp.dat`, public `vcDataStarAp` |
| Sound bank | `machines/VcStarAp.ssm` |
| Display name | `"Archipelago Star"` - the handle the mod uses to find it |
| Class | star (`is_bike = 0`), first appended slot: star slot 19, `MachineKind` 26, `CharacterKind` 20 |
| AP item | 856 (`AP_MACHINE_UNLOCK_BASE + 26`), continuing the machine band past the 855 gap |
| Save bit | `machine_unlocked_mask` bit 26 |
| Logo colors | `#C97682` rose, `#75C275` green, `#CA94C2` violet, `#D9A07D` tan, `#767EBD` blue, `#EEE391` yellow |

None of those kind numbers is hardcoded anywhere. They are what a single registered machine happens to get, not constants anyone depends on: `mods/archipelago/src/main.h` holds the one link between the generic registry and the Archipelago feature wiring,

```c
#define AP_STAR_MACHINE_NAME "Archipelago Star"
```

and everything downstream resolves through `MachineKind_Num()`, `CharacterKind_Num()` and `MachineKind_Resolve()`, which fall back to the vanilla ceilings when this mod is not built. `AP_ResolveCustomMachines()` imports the API - deferred past `OnBoot`, because mods boot alphabetically and the registry boots after the archipelago mod, and retried on each call because the title screen asks for it before the first save load.

**Title screen.** The title scene's demo player is set up at `0x8000d300` from three `li r4` operands: `RiderKind` at `0x8000d340`, `is_bike` at `0x8000d34c`, class slot at `0x8000d358`. `main_menu.c` rewrites all three on each title entry, since the registry only resolves after every mod has booted, pointing them at Kirby on the Archipelago Star and falling back to King Dedede on the Wagon Star when no such machine is registered. The ride must stay star-class: the demo init uses hardcoded star-only state ids and a wheel-class machine crashes there. The Wagon Star's idle volume floor of 20.0 is why the title think and exit callbacks zero the floor for whichever kind the demo uses and put it back on the way out; a kind whose floor is already 0.0 passes through unchanged, and machine loops are only ever created at volume 0.0 and ramped up, so this never lets an audible frame through.

**The sound.** `VcStarAp.ssm` today is the Slick Star's engine loop and run noise resampled to 0.82, which drops them a little under three semitones and lengthens them to match, standing in until the real mix exists. Its engine start and surface start share those two samples, exactly as the Slick Star's do, so the bank is four filled slots over two samples and 81 KB of ARAM. It was built with

```
uv run python scripts/audio/machine_audio.py clone slick \
  mods/custom_machines/assets/machines/VcStarAp.ssm \
  --roles engine,surface,engine-start,surface-start --pitch 0.82
```

**The model.** `VcStarAp.dat` today is a recolored clone of `VcStarSlick.dat` standing in until the real model exists. It already proves the whole registration path: a new kind that loads, drives, animates, casts a shadow, has its own samples and its own select-screen art. The real model's joint layout, against the donor armature exported from `VcStarSlick.dat`:

| Joint | Role |
|---|---|
| 0-5 | donor chain; must carry no animation keys |
| 6 | body root |
| 7 | seat hub disc; the rider sit bone index is 7 |
| 8 | ring pivot - a single ROTY track drives the idle spin of all six spheres |
| 9-14 | the six spheres, children of joint 8, one per logo color |
| 15 | boost/exhaust particle joint |
| 16 | charge-glow billboard, needs `PBILLBOARD` in its JObj flags |

Bone count is 17. Budget is around 650 triangles - six spheres at ~80 each plus a ~60-triangle seat. The quick-spin deformation is a per-frame scale of the sphere joints' translation, driven from mod code because the animation bank has no slot for it. The model, its animations and the sound mix are the remaining art work; everything the engine needs around them is in place.

## Known gaps

These are the places a custom machine is still second-class. None of them corrupt state; each is a missing surface.

- **The select screens hold 21 icons.** That is one appended character, which is what freeing the byte after the packed list buys. Going further means relocating the byte after that too - Air Ride has 17 free bytes at the end of its block, City Trial only one, since its `+0x24b` is the start of something indexed. A machine whose character does not fit is still registered and still drives; the registry just does not give it a `CharacterKind`.
- **No City Trial radar blip.** `3DHud_LoadSomeJointPerPlayerView` (`0x801226e8`) builds the blip joints in a hard `< 0x1a` loop over `MachineKind`, so a custom kind gets no blip GObj. It reads its frame from a 26-entry table at `0x804a85f8` through the five-instruction leaf at `0x80122cbc` (which `GKYE01.map` misattributes to `usnd::CProgInstrs::fn_fs_poGetFn`); `0xFFFFFFFF` there means "no blip", and the highest frame in use is `0x11` against an 18-frame bank. Closing this needs a widened blip GObj array in the HUD's own data block, not just a table entry and a frame.
- **Per-machine stats share a bucket.** `PlayerStats.machine_change_count`, `deaths_by_machine` and `kills_by_machine` are `[0x1a]`. Their writers `Ply_AddDeath` (`0x8022f648`) and `Ply_IncrementGetOnMachineNum` (`0x8022f5bc`) compute the index themselves as `is_bike ? kind + 19 : kind` from class-relative inputs, so custom star slot 19 lands on index 19 and shares the Wheelie Bike's bucket. That is a miscount, never an overrun.
