# Sound System

Everything the game plays that is not streamed music goes through one path: an
FGM id names a script, the script names a sample, and the sample is DSP-ADPCM
sitting in ARAM. This file covers that path, the three file formats behind it,
and the room each of them has left for a mod.

Streamed music is separate - `.hps` files played through `BGM_Play`
(`0x8005e1a8`) - and none of what follows applies to it.

## The four numbering schemes

Four different indices are all called "id" somewhere in the code, and mixing
them up is the usual way to get silence.

| Name | Range | What it identifies |
|---|---|---|
| FGM id | `(bank << 16) \| index` | a script, and so a sound with its envelope. This is what `SFX_Play` takes |
| SEM bank | 0-19 | a run of scripts. `fid >> 16` |
| global sound index | 0-614 | one sample, across every `.ssm` on the disc |
| SSM slot | 0-8 of 32 | one span of the ARAM sample arena, holding one or more loaded `.ssm` |

"Bank" means the SEM kind unless the sentence says SSM slot. The engine's own
asserts call the SSM slot a `bankID`.

A SEM bank and an `.ssm` file line up one for one, but nothing in the code
enforces that - a script carries an absolute global sound index, so the
interpreter never needs to know which file a sample came from. The vanilla
correspondence, with the sound indices each bank draws on:

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

Slots 4 and 5 hold two and eleven files respectively, one at a time; the rest
hold exactly one. Slot 5 is the stage slot, chosen per stage by
`FGM_LoadStageFGM` (`0x8005a474`) through a 21-row `{stage, ssm, variant}` table
at `0x80498ea4`.

## Playing a sound

`SFX_Play` (`0x800615f0`) is the full-volume wrapper; `SFX_PlayRaw`
(`0x80442a10`) takes volume, pan, audio track and sound generator. Both land in
`_SFX_Play` (`0x80442674`), which does nothing but validate and allocate:

```c
bank   = fid >> 16;                                       // rejected if >= stc_fgm_bank_num
script = (fid & 0xFFFF) + stc_fgm_bank_start_script[bank]; // rejected if >= stc_fgm_script_num
fgm->script_data = stc_fgm_script_data[script];
```

Positional sounds go through an `AudioEmitter` instead - `AudioEmitter_Alloc`
then `AudioEmitter_Play` - which owns distance attenuation and panning and calls
`SFX_PlayRaw` underneath.

The script runs a command per audio tick batch in `FGMinstance_UpdateScript`
(`0x80441760`). Opcode `0x01` stores its 16-bit operand into
`FGMInstanceData.sound_index` and raises the "start a voice" flag;
`FGMInstanceData_AllocPID` (`0x80440cd4`) then reaches `Audio_AllocPID`
(`0x80448f08`), which is where a sound index becomes an AX voice:

```c
node = stc_ssm_sound_hash[index & 0x1F];
for (; node; node = node->next)
    if (node->index == index) break;
if (node == NULL) return -1;              // unknown index: the sound silently drops
for (ch = 0; ch < node->channel_num; ch++) {
    AXSetVoiceAddr     (v, node + 0x10 + ch * 0x40);
    AXSetVoiceAdpcm    (v, node + 0x20 + ch * 0x40);
    AXSetVoiceAdpcmLoop(v, node + 0x48 + ch * 0x40);
}
vpb->pitch = node->sample_rate / 32000.0f;
```

There is no bounds check on the index anywhere - a miss returns -1 and the sound
never starts, which is what makes appending sound indices past 614 safe.

## `airride.sem`

The script map, `iso/files/audio/jp/airride.sem`, 34 KB. Five count-prefixed
`u32` tables back to back, which `FGM_InitSEM` (`0x80444208`) splits into four
r13 globals; tables 0, 1 and 4 are empty in the retail file.

```
u32 count0;       u32 table0[count0]
u32 count1;       u32 table1[count1]
u32 bank_num;     u32 bank_start_script[bank_num]     -> stc_fgm_bank_start_script, stc_fgm_bank_num
u32 script_num;   u32 script_offset[script_num]       -> stc_fgm_script_data, stc_fgm_script_num
u32 count4;       u32 table4[count4]
the script bodies
```

Entries in tables 1, 3 and 4 are file offsets, relocated in place by adding the
image's base address, so `FGM_InitSEM` may only be called once per image.
`FGM_LoadAirride.sem` (`0x8005c584`) reads the whole file into one audio-heap
block and installs it, and is reached again from `FGM_Init` (`0x8005a064`) on a
scene reset - anything a mod does to those four globals has to be redone after
each install.

A script is a run of one-word commands: an opcode byte then a 24-bit operand
whose delay/payload split depends on the opcode. Retail's longest is 70
commands, every script ends on `0x0E` or `0x0F`, and every script plays exactly
one sample.

| Opcodes | Delay field | Payload |
|---|---|---|
| `0x00`, `0x0E`, `0x0F` | bits 0-23 | none |
| `0x01`-`0x03`, `0x0C`, `0x0D`, `0x17`-`0x1A` | bits 16-23 | low 16 bits |
| `0x04`-`0x0B`, `0x10`-`0x16` | bits 8-23 | low 8 bits |
| `0x1B`, `0x1C` | none | low 8 bits |

| Op | Effect |
|---|---|
| `0x00` | delay only |
| `0x01` | play global sound index (low 16 bits) |
| `0x02` | loop counter; 0 means forever |
| `0x03` | jump back N commands, decrementing the counter |
| `0x04` / `0x05` | priority, absolute / relative |
| `0x06` / `0x07` | linear volume, absolute / relative |
| `0x08` / `0x09` | pan, absolute / relative |
| `0x0A` / `0x0B` | aux send, absolute / relative |
| `0x0C` / `0x0D` | pitch in cents, absolute / relative, clamped to -10800..2400 |
| `0x0E` | end |
| `0x0F` | end and release |
| `0x10`-`0x15` | 3D parameters |
| `0x16` | cap on concurrent instances of this fid |
| `0x17` / `0x18` | logarithmic volume, absolute / relative |
| `0x19` / `0x1A` | pan angle, 0-359 |
| `0x1B` / `0x1C` | two bytes the interpreter stores and nothing reads |

Dispatch is a 29-entry jump table at `0x80508aa0`; opcodes past `0x1C` are
no-ops. 730 of the 825 scripts open with `0x1B974000` or `0x1C974000`, whose
upper operand bits no consumer reads.

`iso/files/audio/adfgmnametable.dat` is an ordinary HSD archive whose single
public, `smSoundTestFGMGroupTable`, is a `-1`-terminated array of 0x18-byte rows
- `{bank, 1, ssm file index, name, script count, char *names[]}` - giving every
script a debug name like `SFX_engine_slick_lp`. It is reference data for the
sound test and nothing on the play path reads it.

## `.ssm`

A sound bank. Twenty of them live in `iso/files/audio/jp/`.

```
0x00 u32 table_size     bytes of sound records, from 0x10
0x04 u32 data_size      bytes of ADPCM, a multiple of 32
0x08 u32 sound_num
0x0c u32 sound_base     global sound index of this bank's sound 0
0x10      sound records
          the ADPCM data, starting at the next 32-byte boundary
```

`data_size` is the third read's size, and `File_Read` asserts that a read size
is a multiple of 32, so a bank whose data block is not padded out to that
panics in `devcom.c` as it loads. The file ends with the data block: the record
table's padding is not counted in either size field.

Each record is `{u32 channel_num; u32 sample_rate;}` then one 0x40-byte block
per channel, laid out as `AXPBADDR`, `AXPBADPCM`, `AXPBADPCMLOOP`:

```
0x00 u16 loop_flag       0 one shot, 1 looped
0x02 u16 format          0 = ADPCM
0x04 u32 loop_address    nibble offset of the loop point
0x08 u32 end_address     nibble offset of the last sample
0x0c u32 current_address nibble offset of the first sample
0x10 s16 coef[16]        the ADPCM book, Q11
0x30 u16 gain
0x32 u16 predictor_scale of the first frame
0x34 s16 yn1, yn2        history at the first sample
0x38 u16 loop_predictor_scale of the frame holding the loop point
0x3a s16 loop_yn1, loop_yn2   history at the loop point
0x3e u16 pad
```

The three addresses are nibble offsets into the bank's whole data block, not
into the sound's own samples, so a sound's first frame header sits at
`data[(current_address / 16) * 8]` and equals its stored predictor/scale. Two
records may point into the same range, which is how one sample serves several
sounds without a second copy. A one-shot sound sets its loop address equal to
its current address and zeroes the loop context.

The vanilla `sound_base` fields tile 0-614 with no gap, in bank order.

## The sample format

Nintendo DSP-ADPCM. Frames are 8 bytes: one predictor/scale byte then 14 4-bit
samples, so 14 samples cost 8 bytes and a nibble address is `frame * 16 + 2 +
sample_in_frame`. Decoding is

```
scale = 1 << (ps & 0xF);  c1 = coef[(ps >> 4) * 2];  c2 = coef[(ps >> 4) * 2 + 1]
out = clamp16(((nibble * scale << 11) + c1 * yn1 + c2 * yn2 + 1024) >> 11)
```

Every retail machine and stage sound is mono at 32000 Hz; the format allows any
rate and two channels, and a handful of sounds use both. The rate is stored per
sound and AX resamples with it, so retuning a sample is a matter of changing
that word.

Authoring source is 16-bit PCM. `scripts/audio/dsp.py` is the codec - decode,
encode, and coefficient-book generation by clustering per-frame least-squares
predictors. Given a sample's own book it reproduces retail's ADPCM bytes
exactly; with a generated book it lands 25-40 dB SNR depending on the material,
which is the same range Nintendo's encoder achieves on this content.

## Loading

`FGM_QueueLoad(path, slot, callback, arg)` (`0x8044809c`) is the only function
that reads a bank off disc. It takes a full FST path - callers such as
`FGM_LoadByEnum` (`0x8005c298`) build `"audio/" + name` themselves - pushes onto
a 32-deep DVD queue, and starts the transfer when the queue was empty.
`FGM_SychronousLoad(DoTasks)` (`0x80448220`) spins until the queue drains.

A bank arrives in three reads:

| Read | Source | Destination |
|---|---|---|
| 1 | file `0x00`, 0x20 bytes | `stc_ssm_load_header`, the staging buffer |
| 2 | file `0x20`, the record table | a fresh audio-heap block |
| 3 | the ADPCM block, `data_size` bytes | ARAM at the slot's write cursor |

`FGM_LoadBankCallback` (`0x80447ea4`) runs between 1 and 2. It refuses the load
if the slot has less room left than `data_size`, and sizes the heap block
`(sound_num * 8 + table_size + 0x37) & ~0x1F`.

`FGM_UnkCallback` (`0x80447a74`) runs after 3 and is where the bank becomes
usable. It builds an `SSMChunk` at the head of the heap block, pushes it onto
that slot's chunk list, then walks the records: each becomes an `SSMSound` whose
`index` is `sound_base + i`, whose three nibble addresses gain `aram_base * 2`,
and which is linked into `stc_ssm_sound_hash[index & 0x1F]`. Finally the slot's
ARAM cursor advances by `data_size`, so several banks can share a slot.

Slots come from `FGM_GetNextLargestSSMSizeIndex(size)` (`0x80448274`), which
carves the next span of the ARAM sample arena and returns its index, or -1 if
the arena or the 32 slots are exhausted. `FGM_IndexLargestSSMSize`
(`0x8005b8d8`) wraps it and also records the size in `Audio3D.largest_ssm_sizes`,
which has room for nine slots and no bound - past slot 8 that write lands in the
volume struct, so anything carving a tenth slot must call the allocator directly.

The arena is `ARAlloc(0xC00000)` at boot, `0x004500`-`0xC04500`. Retail fills it
to `0xA881C0`, leaving `0x17C340` - about 1.48 MiB - for anything added.

## What is capped

| Cap | Value | Enforced at |
|---|---|---|
| SSM slots | 32 | `FGM_GetNextLargestSSMSizeIndex` `0x80448274` |
| ARAM sample arena | 12 MiB, 1.48 MiB free | same, against the limit at `0x805de54c` |
| Per-slot ARAM | its own span | `FGM_LoadBankCallback` `0x80447ea4` |
| MRAM audio heap | 512 KB | `AudioHeap_Alloc` `0x804479e0` |
| Pending DVD loads | 32 | `FGM_QueueLoad` `0x8044809c` |
| Slot bookkeeping | 16 | `FGM_LoadBank` `0x8005bee4` |
| `.ssm` filename table | 22 rows | `SSM_GetFilename` `0x8005f82c` |
| Sound index | 16 bits | `FGMInstanceData.sound_index` |
| Concurrent FGM instances | 256 | pool at `0x805f8700` |
| Sound generators | 64 | `_SFX_Play` `0x80442674` |

Nothing caps the number of SEM banks, scripts or global sounds: the sound hash
is chained rather than open-addressed, the script map is described entirely by
`stc_fgm_bank_num`, `stc_fgm_script_num`, `stc_fgm_bank_start_script` and
`stc_fgm_script_data`, and an index no bank claims simply never resolves.

## Adding sounds

Everything above adds up to a four-step recipe that needs no file replacement:

1. Build a `.ssm` whose `sound_base` starts at 615, past the last index the
   vanilla banks claim.
2. `FGM_GetNextLargestSSMSizeIndex` for a slot big enough to hold it.
3. `FGM_QueueLoad` its FST path into that slot, then `FGM_SychronousLoad`.
4. Widen the script map: copy `bank_start_script` and `script_data` into larger
   mod-owned arrays, append a bank whose scripts play the new indices, and
   repoint all four globals. Every existing FGM id keeps its meaning, because
   the appended bank sits past them all.

Step 4 has to run again after each `FGM_InitSEM`, and its arrays have to outlive
a scene, since the game holds those pointers until something else replaces them.

The one thing a drop-in bank cannot decide for itself is its `sound_base`, once
there is more than one of them and each has to start past the last.
`FGM_LoadBankCallback` takes that number out of the staging buffer at
`stc_ssm_load_header`, which a hook can overwrite while the load is in flight.
What is resident is not a way to compute it: only nine banks are loaded at a
time, the stage slot changes per stage, and the `star` bank is still on disc at
the title screen. Two banks claiming one index is not an error anywhere - the
sound hash simply returns whichever of them it reaches first.

## Tooling

`scripts/audio/` reads and writes all three formats.

| Module | Contents |
|---|---|
| `dsp.py` | the DSP-ADPCM codec and coefficient-book generation |
| `ssm.py` | `.ssm` load and save, byte-exact on the retail banks |
| `sem.py` | `airride.sem` and the name table, with FGM id lookup by name |
| `bank.py` | a bank's sounds as PCM, and PCM back into a bank's channel plus data |
| `wav.py` | 16-bit PCM WAV, and the windowed-sinc resampler |
| `machine_audio.py` | the CLI that builds a drop-in machine bank |

```
uv run python scripts/audio/machine_audio.py info iso/files/audio/jp/star.ssm
uv run python scripts/audio/machine_audio.py dump iso/files/audio/jp/star.ssm /tmp/star
```
