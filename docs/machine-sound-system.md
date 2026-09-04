# Machine Sound System

A machine's whole voice is thirteen FGM ids in one `MachineAudioParams` row, plus
the per-frame envelope that pitches and fades them.

## The row

`(*stc_machineAudioParams)->params[is_bike]` is a class's array of 0x94-byte
`MachineAudioParams`, authored in `VcCommon.dat` and indexed by class slot -
stars are slots 0-18, and for a star the slot equals its `MachineKind`. The row
opens with the thirteen ids and the rest is float envelope data.

| # | Field | Slick Star's | Bank |
|---|---|---|---|
| 0 | `engine_loop_sfx` | `SFX_engine_slick_lp` | 3 `star` |
| 1-3 | `charge_loop_sfx[3]` | `SFX_wstar_charge{1,2,3}_lp` | 0 `pinfo` |
| 4 | `boost_sfx_l` | -1 | 2 `main` |
| 5 | `boost_sfx_m` | -1 | 2 `main` |
| 6 | `boost_sfx_s` | -1 | 2 `main` |
| 7 | `surface_loop_sfx` | `SFX_runnoize_slick_lp` | 3 `star` |
| 8 | `rumble_loop_sfx` | `SFX_rumble_s_lp` | 3 `star` |
| 9 | `quick_spin_sfx` | `SFX_spin_s` | 3 `star` |
| 10 | `engine_start_sfx` | `SFX_start_engine_slick` | 3 `star` |
| 11 | `surface_start_sfx` | `SFX_start_runnoize_slick` | 3 `star` |
| 12 | `overheat_loop_sfx` | `SFX_engine_overh1` | 0 `pinfo` |

Six of the thirteen are what makes a machine sound like itself: engine, surface,
rumble, spin and the two starts all come out of the `star` bank and every star
kind names a different one. The charge loops, the boost releases and the
overheat are shared - seventeen of the nineteen stars name the same
`SFX_wstar_charge*_lp` trio, and only the Wagon Star has its own
(`SFX_rocket_charge*_lp`). Boost is the emptiest slot in vanilla: eleven stars
name the `SFX_dash_l/m/s` trio, four fill it partly, and Slick, Jet and the two
wing riders leave all three at -1 and release silently.

The star slots and the sample family each names:

| Slot | Machine | Engine sample |
|---|---|---|
| 0 | Warp | `star` |
| 1 | Compact | `light` |
| 2 | Winged | `wing` |
| 3 | Shadow | `devil` |
| 4 | Hydra | `hydra` |
| 5 | Bulk | `heavy` |
| 6 | Slick | `slick` |
| 7 | Formula | `formula` |
| 8 | Dragoon | `dragon` |
| 9 | Wagon | `wagon` |
| 10 | Rocket | `rocket` |
| 11 | Swerve | `ufo` |
| 12 | Turbo | `turbo` |
| 13 | Jet | `jet` |
| 14-16 | Flight, Free, Steer | `star` |
| 17-18 | Wing Kirby, Wing Meta Knight | none; engine is -1 |

## What plays them

`Machine_UpdateAudioEmitter` (`0x801dce60`) is the machine's voice for one
frame. It runs the surface loop, then the engine loop, then the charge loop, the
ground rumble and the wind loop, then `Machine_GroundSFXThink` and
`AudioEmitter_SetPosition`. Everything positional goes through the machine's own
`AudioEmitter` at `+0x844`; `Machine_PlaySFX` (`0x801dd17c`) is the wrapper that
starts one script on it and returns an instance handle.

Six loop handles live in `MachineData`, each `-1` when silent:

| Handle | Loop |
|---|---|
| `+0x860` | surface |
| `+0x87c` | engine |
| `+0x888` | charge, with the id it was started from at `+0x88c` |
| `+0x890` | ground rumble, with its id at `+0x894` |
| `+0x898` | wind |
| `+0x8a0` | machine rumble |

`Machine_PlaySpawnSound` (`0x801dccec`) clears them as a machine spawns and
starts the surface and engine loops at volume 0.0.

### Engine loop

`Machine_UpdateEngineLoop` (`0x801dcb18`) restarts the loop whenever `+0x87c` is
-1, so it is effectively always alive, and stops it while `md->xc39` bit 0 is
set. Its input is the engine load `Machine_Star_Think` maintains at `+0x870`:

```
pitch_target  = min(md->x870 * engine_pitch_coef, 1.0)
pitch_target  = engine_pitch_min + pitch_target * (engine_pitch_max - engine_pitch_min)
md->x880     += clamp(pitch_target - md->x880, -engine_pitch_slew, +engine_pitch_slew)
md->x880      = min(md->x880, 2400)          // the interpreter's own cent ceiling

vol_target    = md->x870 * engine_volume_coef + engine_idle_floor
vol_target   *= engine_volume_air   if md->x754 == 1
vol_target   *= engine_volume_x6c   if md->xc34 bit 4
md->x884     += clamp(vol_target - md->x884, -engine_volume_slew, +engine_volume_slew)
md->x884      = min(md->x884, 1.0)
```

Pitch is in cents, so the Slick Star's `-1200 .. +400` is an octave down at rest
rising to a third above the sample's own tuning at full load.

**Keep `engine_idle_floor` at 0.0.** It is the volume the loop holds while
parked, and a nonzero floor makes the machine hum wherever it is posed rather
than driven. Only Bulk (40), Wagon (20), Turbo (18), Jet (12) and Formula (10)
have one.

### Surface loop

`Machine_UpdateSurfaceLoop` (`0x801dc80c`) is the same shape driven by
`|md->x36c|` clamped to `surface_speed_max`, with `surface_pitch_coef` /
`_base` / `_slew` and `surface_volume_coef` / `_floor` / `_slew`, plus three
extra multipliers: `surface_volume_gnd` while the rail loop supplies the id,
`surface_volume_air` while `md->x754 == 1`, and `surface_volume_x58` on
`md->xc34` bit 4.

The id is not always the row's, but the exception is narrow. While the machine
is riding a rail - `md->xc3b` bit 5 clear and `md->xc33` bit 1 set - the id
comes instead from the class-shared audio table `vcDataCommon->x8`, looked up
through `grRail_GetGroundType` (`0x800d416c`) on the rail handle and spline
position at `md+0x1b48`. Every one of that table's sixteen ground rows names
`SFX_rail_runnoize_lp` there, and only the water row differs
(`SFX_rail_water_runnoize_lp`), so in practice the override is a single rail
sound. Everywhere else - all normal ground and all air - the loop is the row's
own `surface_loop_sfx`.

That table is cached at `r13 + 0x760` by `vcLoadCommon`. Its head holds the wind
loop's speed range (`+0x08`, `+0x0c`) and envelope (`+0x14`..`+0x24`); the
sixteen ground rows are the 0x10-byte records from `+0x48`, and their first word
is the ground rumble id that `+0x890` plays.

The same table's next word per row is the per-material impact sound, and that
one really does vary: `SFX_tyak_soil`, `_gravel`, `_sand`, `_grass`, `_asphalt`,
`_ice`, `_snow`, `_stone`, `_tree`, `_rock`, `_fire`, `_leaf`, `_crystal`,
`_iron`. Those are shared by every machine and are not part of the row.

When the chosen id differs from the one at `+0x86c` the old instance is stopped
and a new one started, so stepping on or off a rail restarts the loop rather
than crossfading.

Every star kind's `surface_volume_floor` is 0.03, below the level that produces
a voice, so a parked machine is silent here too.

### Charge loops

The charge loop is the only one gated on charge state. `Machine_UpdateAudioEmitter`
plays it while **both** `md->xc32` bit 6 and bit 5 are set - the pair
`Machine_IncrementCharge` (`0x801cc480`) sets each frame the meter actually
accumulates, and `Machine_ClearChargeState` (`0x801ca294`) clears. So the loop is
alive exactly while the player is holding A on the ground and the meter is
moving, and stops the instant charging is interrupted, released or cleared.

Which of the three plays is a step function of the meter, not a crossfade:

```
if      (charge_value <  charge_loop_split[0])  charge_loop_sfx[0]
else if (charge_value <  charge_loop_split[1])  charge_loop_sfx[1]
else                                            charge_loop_sfx[2]
```

`charge_loop_split` is `{0.33, 0.66}` on every star but the Wagon Star, which
steps early at `{0.10, 0.30}`. The chosen id is remembered at `+0x88c`; crossing
a split stops the running instance and starts the next one from its head, which
is why the three vanilla samples are written to hand off - each is a rising
sweep that ends where the next begins. All three start at full volume and
nothing modulates them afterwards, so a charge sample's own shape is the whole
effect. Their loop points are late in the sample (`SFX_wstar_charge1_lp` runs
22318 samples and loops at 16187), so each sweeps once and then holds a tail
until the meter steps or the player lets go.

The meter filling to 1.0 does **not** play a fourth sound; it starts the
auto-discharge timer and the `0x1D` glow. Holding until that timer expires is
what plays `overheat_loop_sfx` - `Machine_ChargeUpdate` (`0x801ca4c0`) fires it
once as a one shot alongside the `0x1F` discharge animation, so despite the
field name it is the overcharge penalty cue, not a loop.

### Boost release

`Machine_PlayBoostSFX` (`0x801dd3ec`) runs on release and picks a tier from the
meter at the moment it is let go, on track2 (`+0x84c`) at full volume:

```
if (charge_value <  boost_thresh_min)  nothing              // 0.05
else if (charge_value >= boost_thresh_l) boost_sfx_l        // 0.80, 0.90 on Warp and Hydra
else if (charge_value >= boost_thresh_m) boost_sfx_m        // 0.60
else                                     boost_sfx_s
```

A kind whose tier is -1 releases silently at that tier, which is all three on
Slick and Jet.

### Rumble, wind and the one shots

`Machine_UpdateRumbleLoop` (`0x801dd578`) holds `rumble_loop_sfx` at `+0x8a0`
alive at a volume scaled from `md->xb84` times a per-class constant, stopping it
when the product reaches zero; `Machine_SetRumbleLoopLevel` (`0x801dd48c`) is
the same with the level passed in. This is the machine's own rumble and is
separate from the ground rumble at `+0x890`, whose id comes from the ground-type
table rather than the row.

`engine_start_sfx` and `surface_start_sfx` fire together as the machine is
mounted, from the state enter at `0x801e1908`, and are skipped when its mode
argument is 3. They are not separate samples: every vanilla star points them at
the *same* sound indices as its engine and surface loops, and what makes them a
start is the script. `SFX_start_engine_slick` is 34 commands of swell - priority,
pitch, a run of logarithmic volume ramps under loop counters - ending on `0x0F`,
end and release, which is what stops the voice. So a start needs a **looped**
sample record even though it plays once, because its envelope outruns the sample.

That is the general rule for these thirteen. A loop's script ends on `0x0E`,
which ends the script but leaves the voice running for the code to stop by
handle; a one shot ends on `0x0F`, which releases it. Engine, the three charges,
surface, rumble and the two starts want looped records; the three boosts, quick
spin and overheat want one shots.

`quick_spin_sfx` is one shot from `Machine_PlayQuickSpinSFX` (`0x801e383c`) and
one other site.

## Giving a machine its own samples

The ids above name scripts, and a script names a sample by a global sound index
that runs across every `.ssm` on the disc. A drop-in machine ships a companion
`.ssm` next to its `.dat` with the same basename - `machines/VcMine.dat` and
`machines/VcMine.ssm` - holding exactly one record per row slot in the order
above. A slot the author does not supply is a record with a sample rate of 0,
and that slot keeps the clone kind's id. Records may point into the same data,
so a machine whose engine start is its engine loop pays for one copy.

`machine_audio.c` in `mods/custom_machines/` loads those banks into one SSM slot
carved out of the ARAM sample arena, assigns each bank global sound indices past
the 615 vanilla claims, and appends one SEM bank of scripts that play them. Each
script is a copy of whichever vanilla script the clone kind's row names for that
slot with its sound index rewritten, so a drop-in engine loop keeps the donor's
volume and pitch envelope. Where the clone kind leaves a slot at -1 - the boost
tiers, usually - the copy is taken from the first star row that has one, so a
drop-in can fill a slot its clone kind never had.

Retail leaves about 1.48 MiB of the ARAM sample arena free, which is the real
budget on how much of a voice a drop-in machine can carry.

## Authoring

`scripts/audio/machine_audio.py` reads and writes these banks, over the
subcommands `roles`, `clone`, `donors`, `build`, `info` and `dump`. Source audio
is 16-bit PCM WAV at any rate; the tool encodes to the DSP-ADPCM the hardware
wants, generates each sound's coefficient book, and writes the loop point and its
decoder context.

`clone <star>` copies a vanilla machine's thirteen roles into a new bank and is
the fastest way to a working voice. `donors <star> <dir>` writes the sample behind
each role out as a `.wav` named for the slot it fills, which is the starting point
for hand-editing: edit the files and feed them back through `build`. `--fallback`
supplies the roles a star leaves at -1, so `slick --fallback warp` is a complete
thirteen. `--pitch` resamples, which lowers the pitch and lengthens the sound
together, the way a bigger engine sounds; a loop point is scaled with it.

`build` defaults each role to the loop flag its slot wants and loops from sample
0. `--loop ROLE=SAMPLE` sets a real loop point, which matters for the charge
sweeps and the rumble, whose vanilla loops start well into the sample, and
`--loop ROLE=-1` forces a one shot. The vanilla loop points a `clone` of the
Slick Star carries are `--loop charge1=19740 --loop charge2=28867 --loop
charge3=25296 --loop rumble=16734`, scaled by whatever `--pitch` asks for.

Pass all thirteen roles every time. `build` writes an absent record for any role
it is not given, and an absent role falls back to the clone kind's sound rather
than keeping what the previous bank had.

No machine ships a companion bank. The Archipelago Star, the only drop-in
registered, takes the Slick Star's row whole and sounds like one, so the loading
and index-assignment path above runs with nothing to load and every drop-in sound
resolves to its clone kind's.
