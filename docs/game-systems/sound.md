# Sound

Every sound effect the game plays goes through one path: an FGM id names a script, the script
names a sample, and the sample is DSP-ADPCM sitting in ARAM. Streamed music is separate - `.hps`
files played through `BGM_Play` (0x8005e1a8) - and none of this applies to it. This doc covers the
numbering, how a sound plays, the two file formats (`airride.sem` and `.ssm`), how banks load,
what is capped, and how to add sounds without replacing any file on disc.

## The four numbering schemes

Four different indices are all called "id" somewhere in the code, and mixing them up is the usual
way to get silence.

| Name | Range | What it identifies |
|---|---|---|
| FGM id | `(bank << 16) \| index` | a script, and so a sound with its envelope. What `SFX_Play` takes |
| SEM bank | 0-19 | a run of scripts: `fid >> 16` |
| global sound index | 0-614 | one sample, across every `.ssm` on the disc |
| SSM slot | 0-8 of 32 | one span of the ARAM sample arena, holding one or more loaded `.ssm` |

"Bank" below means the SEM bank unless it says SSM slot; the engine's own asserts call the SSM
slot a `bankID`. A SEM bank and an `.ssm` line up one for one in vanilla, but nothing enforces it:
a script carries an absolute global sound index, so the interpreter never needs to know which file
a sample came from.

| Bank | Scripts | Sounds | `.ssm` | SSM slot |
|---|---|---|---|---|
| 0 | 0-59 | 0-46 | `pinfo` | 3 |
| 1 | 60-75 | 47-61 | `menu` | 0 |
| 2 | 76-248 | 62-190 | `main` | 1 |
| 3 | 249-380 | 191-251 | `star` | 7 |
| 4 | 381-432 | 252-278 | `ground` | 6 |
| 5 | 433-470 | 279-306 | `zako` | 8 |
| 6 | 471-533 | 307-366 | `2d_main` | 2 |
| 7 | 534-580 | 367-405 | `2d_item` | 4 |
| 8 | 581-642 | 406-454 | `2d_stage` | 5 |
| 9 | 643-647 | 455-459 | `map_plants` | 5 |
| 10 | 648-662 | 460-472 | `map_heat` | 5 |
| 11 | 663-674 | 473-483 | `map_desert` | 5 |
| 12 | 675-686 | 484-494 | `map_chk2` | 5 |
| 13 | 687-698 | 495-505 | `map_valley` | 5 |
| 14 | 699-714 | 506-515 | `map_ice` | 5 |
| 15 | 715-723 | 516-524 | `map_machine` | 5 |
| 16 | 724-726 | 525-527 | `map_space` | 5 |
| 17 | 727-735 | 528-536 | `map_sky` | 5 |
| 18 | 736-785 | 537-580 | `map_city1` | 5 |
| 19 | 786-824 | 581-614 | `city` | 4 |

Slots 4 and 5 hold two and eleven files respectively, one at a time. Slot 5 is the stage slot,
chosen per stage by `Gm_LoadGroundFGMBank` (0x8005a474) through a `{stage, ssm, variant}` table at
0x80498ea4. Only nine banks are resident at once, so which sounds can play depends on the scene.

## Playing a sound

`SFX_Play` (0x800615f0) is the full-volume wrapper; `SFX_PlayRaw` (0x80442a10) takes volume, pan,
audio track and sound generator. Both land in `_SFX_Play` (0x80442674), which only validates and
allocates:

```c
bank   = fid >> 16;                                        // rejected if >= stc_fgm_bank_num
script = (fid & 0xFFFF) + stc_fgm_bank_start_script[bank]; // rejected if >= stc_fgm_script_num
                                                           // or >= bank_start_script[bank + 1]
fgm->script_data = stc_fgm_script_data[script];
```

So an fid whose index runs past its own bank is refused rather than reaching a neighbour's script.
The sound generator must be 0-63 and the track must not be in `stc_audio_track_blacklist`.

The script runs in `FGMInstance_UpdateScript` (0x80441760). Opcode `0x01` stores its operand into
`FGMInstanceData.sound_index` and flags a voice start; `FGMInstanceData_AllocPID` (0x80440cd4)
then reaches `Sound_PlayIndex` (0x80448f08), where a sound index becomes an AX voice. It walks the
chain at `stc_ssm_sound_hash[index & 0x1F]` for a node with that index, feeds each channel's
parameter blocks to `AXSetVoiceAddr` / `AXSetVoiceAdpcm` / `AXSetVoiceAdpcmLoop`, and sets the
pitch to `sample_rate / 32000`. Nothing bounds the index: a miss returns -1 and the sound silently
never starts, which is what makes indices past 614 safe to append.

## Generators, emitters and voices

Three numbers follow a sound down to the hardware.

- **`sg`, the sound generator.** 64 of them, the ceiling on concurrent sounds. Status lives in
  `Audio3D.sg_status` (indexed `sg - 4`; 0-3 are reserved), and `FGMInstanceData`,
  `AudioEmitterData` and the VPB all carry it. The per-`sg` volume is `UserVolume` at 0x8059a178.
- **`pid`**, held in `FGMInstanceData+0x10`, ties an instance to its AX voice.
- **`fgm_instance`**, built from the fid and a counter that increments with every sound.

A positional sound goes through an `AudioEmitter`: `AudioEmitter_Alloc` (0x8005d864) ->
`AudioEmitter_Play` -> `AudioEmitterData_GetSoundGenerator` (0x8005d6dc) -> `SFX_Play`. The
emitter owns distance attenuation and panning; `AudioEmitter_Update` writes the result into the
per-`sg` volume, and `AudioEmitter_SetVoiceParams` stops the voice when volume falls below 10 and
restarts it when it rises above. Each audio tick a loop over the VPB list pushes every `sg`'s
volume to the hardware.

`AudioEmitterData_GetSoundGenerator` scans `sg_status` for a free generator and otherwise reclaims
one whose sounds have all finished (`Audio_GetFGMNumUsingSoundGenerator`, 0x80443d8c, counts the
live instances on it). `AudioEmitter_Alloc` is likewise the collector for finished emitters: it
prefers an emitter in state 1 whose `sg` has no live sound, then a free one in state 0, and
otherwise fails with -1. An emitter destroyed while holding a voice goes to state 1, which puts
it first in line.

## `airride.sem`

The script map, `audio/jp/airride.sem`: five count-prefixed `u32` tables back to back, which
`FGM_InitSEM` (0x80444208) splits into four r13 globals. Tables 0, 1 and 4 are empty in retail.

```
u32 count0;      u32 table0[count0]
u32 count1;      u32 table1[count1]
u32 bank_num;    u32 bank_start_script[bank_num]   -> stc_fgm_bank_start_script, stc_fgm_bank_num
u32 script_num;  u32 script_offset[script_num]     -> stc_fgm_script_data, stc_fgm_script_num
u32 count4;      u32 table4[count4]
the script bodies
```

Entries in tables 1, 3 and 4 are file offsets relocated in place, so `FGM_InitSEM` may run only
once per image. `FGM_LoadAirrideSem` (0x8005c584) reads the file into one audio-heap block and
installs it, and runs again from `FGM_Init` (0x8005a064) on a scene reset - anything a mod does
to the four globals has to be redone after each install.

A script is a run of one-word commands: an opcode byte, then a 24-bit operand whose split between
delay and payload depends on the opcode. Every retail script plays exactly one sample and ends on
`0x0E` or `0x0F`.

| Opcodes | Delay field | Payload |
|---|---|---|
| `0x00`, `0x0E`, `0x0F` | bits 0-23 | none |
| `0x01`-`0x03`, `0x0C`, `0x0D`, `0x17`-`0x1A` | bits 16-23 | low 16 bits |
| `0x04`-`0x0B`, `0x10`-`0x16` | bits 8-23 | low 8 bits |
| `0x1B`, `0x1C` | none | low 8 bits |

| Op | Effect |
|---|---|
| `0x00` | delay only |
| `0x01` | play a global sound index |
| `0x02` / `0x03` | loop counter (0 = forever) / jump back N commands, decrementing it |
| `0x04` / `0x05` | priority, absolute / relative |
| `0x06` / `0x07` | linear volume, absolute / relative |
| `0x08` / `0x09` | pan, absolute / relative |
| `0x0A` / `0x0B` | aux send, absolute / relative |
| `0x0C` / `0x0D` | pitch in cents, absolute / relative, clamped to -10800..2400 |
| `0x0E` | end; a playing voice keeps running until code stops it by handle |
| `0x0F` | end and release the voice |
| `0x10`-`0x15` | 3D parameters |
| `0x16` | cap on concurrent instances of this fid |
| `0x17` / `0x18` | logarithmic volume, absolute / relative |
| `0x19` / `0x1A` | pan angle, 0-359 |
| `0x1B` / `0x1C` | stored, never read |

Dispatch is a 29-entry jump table at 0x80508aa0; opcodes past `0x1C` are no-ops.

`audio/adfgmnametable.dat` is an HSD archive whose public `smSoundTestFGMGroupTable` gives every
script a debug name such as `SFX_engine_slick_lp`. Only the sound test reads it.

## `.ssm`

A sample bank. The 0x10-byte header is hoshi's `SSMHeader`: table size, data size, sound count
and the bank's `sound_base`. The record table follows at 0x10 and the ADPCM data at the next
32-byte boundary; the file ends with the data block. `File_Read` asserts that every read size is
a multiple of 32, so a bank whose data block is not padded to that panics in `devcom.c` as it
loads.

Each record is `{u32 channel_num; u32 sample_rate;}` and then one 0x40-byte block per channel,
laid out as `AXPBADDR`, `AXPBADPCM`, `AXPBADPCMLOOP`:

```
0x00 u16 loop_flag              0 one shot, 1 looped
0x02 u16 format                 0 = ADPCM
0x04 u32 loop_address           nibble offset of the loop point
0x08 u32 end_address            nibble offset of the last sample
0x0c u32 current_address        nibble offset of the first sample
0x10 s16 coef[16]               the ADPCM book, Q11
0x30 u16 gain
0x32 u16 predictor_scale        of the first frame
0x34 s16 yn1, yn2               history at the first sample
0x38 u16 loop_predictor_scale   of the frame holding the loop point
0x3a s16 loop_yn1, loop_yn2     history at the loop point
0x3e u16 pad
```

The addresses are nibble offsets into the bank's whole data block, so two records may share one
range - one sample serving several sounds. A one-shot record sets its loop address to its current
address and zeroes the loop context. The vanilla `sound_base` fields tile 0-614 with no gap.

The samples are Nintendo DSP-ADPCM: 8-byte frames of one predictor/scale byte and 14 4-bit
samples, so a nibble address is `frame * 16 + 2 + sample_in_frame`, decoded as

```
scale = 1 << (ps & 0xF);  c1 = coef[(ps >> 4) * 2];  c2 = coef[(ps >> 4) * 2 + 1]
out = clamp16(((nibble * scale << 11) + c1 * yn1 + c2 * yn2 + 1024) >> 11)
```

Every retail machine and stage sound is mono at 32000 Hz. The rate is stored per sound and AX
resamples with it, so retuning a sample means changing that word.

## Loading

`FGM_QueueLoad(path, slot, callback, arg)` (0x8044809c) is the only function that reads a bank off
disc. It takes a full FST path (`FGM_LoadByEnum`, 0x8005c298, builds `"audio/" + name`), pushes
onto a 32-deep DVD queue, and starts the transfer if the queue was empty. `FGM_SynchronousLoad`
(0x80448220) spins until the queue drains. A bank arrives in three reads:

| Read | Source | Destination |
|---|---|---|
| 1 | file 0x00, 0x20 bytes | `stc_ssm_load_header`, the staging buffer |
| 2 | file 0x20, the record table | a fresh audio-heap block |
| 3 | the ADPCM block | ARAM at the slot's write cursor |

`FGM_LoadBankCallback` (0x80447ea4) runs between reads 1 and 2 and refuses the load if the slot
has less room left than the data size. `FGM_UnkCallback` (0x80447a74) runs after read 3: it turns
each record into an `SSMSound` whose `index` is `sound_base + i`, rebases its nibble addresses to
the bank's ARAM, links it into `stc_ssm_sound_hash`, and advances the slot's ARAM cursor, so
several banks can share one slot.

`FGM_AllocSSMSlot(size)` (0x80448274) carves the next span of the ARAM sample arena and returns
its slot, or -1 when the arena or the 32 slots are spent. `FGM_IndexLargestSSMSize` (0x8005b8d8)
wraps it and also records the size in `Audio3D.largest_ssm_sizes`, which has room for nine slots
and no bound check: past slot 8 that write lands in the volume struct, so anything carving a
tenth slot must call `FGM_AllocSSMSlot` directly. The arena is `ARAlloc(0xC00000)` at boot;
retail fills it to 0xA881C0, leaving about 1.48 MiB free.

## What is capped

| Cap | Value | Enforced at |
|---|---|---|
| SSM slots | 32 | `FGM_AllocSSMSlot` |
| ARAM sample arena | 12 MiB, about 1.48 MiB free | `FGM_AllocSSMSlot`, against the limit at 0x805de54c |
| Per-slot ARAM | its own span | `FGM_LoadBankCallback` |
| MRAM audio heap | 512 KB | `AudioHeap_Alloc` (0x804479e0) |
| Pending DVD loads | 32 | `FGM_QueueLoad` |
| Slot bookkeeping | 16 | `FGM_LoadBank` (0x8005bee4) |
| `.ssm` filename table | 22 rows | `SSM_GetFilename` (0x8005f82c) |
| Sound index | 16 bits | `FGMInstanceData.sound_index` |
| Concurrent FGM instances | 256 | pool at 0x805f8700 |
| Sound generators | 64 | `_SFX_Play` |

Nothing caps the number of SEM banks, scripts or global sounds. The sound hash is chained, the
script map is described entirely by the four `stc_fgm_*` globals, and an index no bank claims
simply never resolves.

## Adding sounds

All of this adds up to a way to add sounds that replaces no file on disc:

1. Build an `.ssm` whose `sound_base` starts past the last claimed index (615 in vanilla).
2. Take a slot big enough for it from `FGM_AllocSSMSlot`.
3. `FGM_QueueLoad` its FST path into that slot and `FGM_SynchronousLoad`.
4. Widen the script map: copy `stc_fgm_bank_start_script` and `stc_fgm_script_data` into larger
   mod-owned arrays, append a bank whose scripts play the new indices, and repoint all four globals.

Every existing FGM id keeps its meaning because the appended bank sits past them all. The map has
to be widened again after each `FGM_InitSEM`, and the arrays must outlive the scene, since the
game holds the pointers until something replaces them.

A copied vanilla script with its `0x01` operand rewritten plays a new sample through the vanilla
envelope, which is the easiest way to give a sound the shape the game expects of it.

When several banks are added, each must start past the last, and a drop-in bank cannot know where
that is: only nine banks are resident at a time and the stage slot changes per stage.
`FGM_LoadBankCallback` takes `sound_base` from the staging buffer at `stc_ssm_load_header`, which a
hook can overwrite while the load is in flight. Two banks claiming one index is not an error
anywhere - the hash returns whichever it reaches first.
