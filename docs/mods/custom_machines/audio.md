# Custom Machines: Audio

How a registered machine sounds. Every machine's voice is a row of FGM ids in the engine's
per-kind audio table, and the `custom_machines` mod gives each registered machine a row of its own,
copied from a vanilla machine of its class (its descriptor's `audio_kind`). A machine that wants
sounds of its own ships a `.ssm` sound bank beside its archive, and the mod loads it, gives its
samples sound indices no vanilla bank claims, and writes scripts that play them into the row.

## The audio row

`(*stc_machineAudioParams)->params[is_bike]` is each class's array of 0x94-byte
`MachineAudioParams`, authored in `VcCommon.dat` - 19 star rows, 7 bike rows - and read by class
slot at many sites, none bounded. A hook on `vcLoadCommon`'s epilogue (0x801c6d64) copies both
classes' rows into wider mod-owned arrays, fills each registered machine's slot with its
`audio_kind`'s row, overlays the FGM ids of any drop-in sounds, and repoints both classes.
`VcCommon.dat` reloads per scene, so this runs on every scene entry.

The row opens with thirteen FGM ids, and they are the whole of a machine's voice; everything after
them is the per-frame volume and pitch envelope.

| # | Role | Plays |
|---|---|---|
| 0 | engine | loop, created at volume 0 and ramped with speed |
| 1-3 | charge gauge | one loop per third of the gauge, one at a time |
| 4-6 | boost release | loud, medium and quiet tiers by charge level; -1 where a kind has none |
| 7 | surface | the run-noise loop |
| 8 | rumble | loop |
| 9 | quick spin | one shot |
| 10 | engine start | as the machine is mounted |
| 11 | surface start | alongside the engine start |
| 12 | overheat | once, on auto-discharge after holding a full charge |

A machine that wants nothing new can pick different vanilla ids by choosing its `audio_kind`. The
row's `engine_idle_floor` is the volume the engine loop holds while parked; it is 0.0 on most
stars, and a machine copying a kind with a nonzero floor (Bulk, Wagon, Turbo, Jet, Formula) hums
while parked.

Each class also keeps a per-kind sound table outside the row, carried by the class archive's
`vcDataKindStar` at `+0x0c` and handed to every machine as `MachineData.x654` by the class's
attribute copy. The star's holds the air-noise loop's FGM id per slot from `+0x30`, which
`Machine_SoundEffectThink` (0x801ee588) starts; the bike's holds a `{loop, one-shot}` pair per
slot from `+0x1c` for its wheel sounds. Both are indexed with no bound, so the `Vehile_LoadFile`
replacement widens the table as the class archive loads: the vanilla entries are copied into a
mod-owned table, each custom slot takes its `audio_kind`'s entry, and the class's pointer is
repointed before any machine is created. No `.ssm` role reaches this table.

## Drop-in sound banks

A machine ships `machines/VcMine.ssm` beside `machines/VcMine.dat`. Nothing on the disc is
replaced and the machine archive is untouched; a machine with no bank keeps its `audio_kind`'s
sounds. The bank is an ordinary HAL sound bank holding exactly one record per role, in the order
above. A role the author does not supply is a record with a sample rate of 0 and no samples, and
keeps the `audio_kind`'s id. Records may point into the same sample data, so a machine whose engine
start is its engine loop pays for one copy.

Three things have to line up before a sample plays, and all three are set up on the first
`vcLoadCommon`, when the audio system is running and the DVD is available.

**The samples go into ARAM.** `FGM_AllocSSMSlot` (0x80448274) carves one SSM slot sized for every
drop-in bank together, and each bank is loaded into it with `FGM_QueueLoad` (0x8044809c) and
`FGM_SynchronousLoad` (0x80448220). A slot chains its banks in load order, so each bank's chunk is
the tail of `stc_ssm_slot_chunks[slot]` once its own load finishes. Slots are carved for the run of
the game, so this happens once.

**Each sample needs a global sound index no other bank claims.** Sound indices run across every
bank on the disc, and the vanilla banks tile 0-614 with no gap (`SSM_VANILLA_SOUND_NUM`). A bank's
header declares its own base, which an author cannot choose safely once there is more than one
drop-in bank, so the mod assigns bases from 615 up, each bank past the last. `FGM_LoadBankCallback`
(0x80447ea4) queues the read callback `FGM_UnkCallback` (0x80447a74), which takes the base out of
the staged file header (`stc_ssm_load_header`); a hook at 0x80447eb8, past
`FGM_LoadBankCallback`'s prologue, overwrites that base first. The stamp rewrites whatever header
is staged, so the mod drains the load queue before queuing its first bank and loads each bank
alone. `Sound_PlayIndex` (0x80448f08) resolves an index through a hash and drops the sound on a
miss, so an index two banks both claimed would resolve to whichever the hash chain reaches first.

**A script has to play them.** An FGM id names a script, not a sample; the script carries the
sound's volume and pitch envelope. Each drop-in sound gets a copy of the script its `audio_kind`'s
row names for that role, with its one sound command's operand rewritten to the new index, so a
drop-in engine loop keeps that script's envelope. Where the `audio_kind` has no id for a role - the
Slick Star, the Wagon Star and the two wing riders have no boost tiers - the copy is taken from the
first vanilla row of the same class that has one. The copies go into a script bank appended to the
SEM map, which four globals describe entirely (`stc_fgm_bank_num`, `stc_fgm_script_num`,
`stc_fgm_bank_start_script`, `stc_fgm_script_data`): both tables are copied into larger mod-owned
arrays, bank 20 is appended after the vanilla 20, and every vanilla FGM id keeps its meaning.
`FGM_LoadAirrideSem` (0x8005c584) reinstalls the vanilla map on a scene reset, so a hook at
0x8005c654 puts the appended bank back after each reload.

The map tables and the script copies are static arrays in the mod image, because the game holds
those pointers for the run of the game and `HSD_MemAlloc` only returns memory that outlives a scene
when called from a mod's boot callback. A map that would outgrow the reserved arrays is reported
once and the drop-in sounds stay off.

## Building a bank

The repo's machine-audio script builds a bank from 16-bit PCM WAV files at any rate, one option per
role: it encodes to the DSP-ADPCM the hardware plays, generates each sound's coefficient book, and
writes the loop point and its decoder context. It also lists the roles in order, describes a built
bank, and dumps a bank's sounds back out as WAVs.
