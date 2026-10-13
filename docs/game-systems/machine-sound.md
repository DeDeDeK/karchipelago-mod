# Machine Sound

A machine's whole voice is thirteen FGM ids in one `MachineAudioParams` row, plus the per-frame
envelope that pitches and fades them. This doc covers the row, the code that plays each id, which
of them make a machine sound like itself, and what a replacement sample has to look like. FGM ids,
scripts and samples are the general sound system's: an id names a script in `airride.sem`, and
the script plays one sample by global sound index.

## The row

`(*stc_machineAudioParams)->params[is_bike]` is a class's array of 0x94-byte
`MachineAudioParams`, authored in `VcCommon.dat` and indexed by class slot. Stars are slots 0-18,
and for a star the slot equals its `MachineKind`. The row opens with the thirteen ids; the rest is
float envelope data.

| # | Field | Slick Star's | Bank |
|---|---|---|---|
| 0 | `engine_loop_sfx` | `SFX_engine_slick_lp` | 3 `star` |
| 1-3 | `charge_loop_sfx[3]` | `SFX_wstar_charge{1,2,3}_lp` | 0 `pinfo` |
| 4-6 | `boost_sfx_l` / `_m` / `_s` | -1 | 2 `main` |
| 7 | `surface_loop_sfx` | `SFX_runnoize_slick_lp` | 3 `star` |
| 8 | `rumble_loop_sfx` | `SFX_rumble_s_lp` | 3 `star` |
| 9 | `quick_spin_sfx` | `SFX_spin_s` | 3 `star` |
| 10 | `engine_start_sfx` | `SFX_start_engine_slick` | 3 `star` |
| 11 | `surface_start_sfx` | `SFX_start_runnoize_slick` | 3 `star` |
| 12 | `overheat_loop_sfx` | `SFX_engine_overh1` | 0 `pinfo` |

Six of the thirteen make a machine sound like itself: engine, surface, rumble, spin and the two
starts all come from the `star` bank, and every star kind names its own. The rest are shared.
Seventeen of the nineteen stars name the same `SFX_wstar_charge*_lp` trio; the Rocket Star has
its own, and the Wagon Star leaves all three at -1. Boost is the emptiest slot: twelve stars name
a full trio, Bulk names one tier, Hydra and Swerve two, and Slick, Wagon and the two wing riders
release silently.

| Slot | Machine | Engine sample family |
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

`Machine_UpdateAudioEmitter` (0x801dce60) is a machine's voice for one frame: the surface loop,
the engine loop, the charge loop, the ground rumble and the wind loop, then
`Machine_UpdateSkidLoop` and `AudioEmitter_SetPosition`. Everything positional goes through the
machine's own `AudioEmitter` at `+0x844`; `Machine_PlaySFX` (0x801dd17c) starts one script on it
and returns an instance handle. `Machine_InitAudioLoops` (0x801dccec) clears the loop handles as a
machine spawns and starts the surface and engine loops at volume 0.

| Handle | Loop |
|---|---|
| `+0x860` `surface_loop_fgm` | surface |
| `+0x87c` `engine_loop_fgm` | engine |
| `+0x888` | charge, with the id it was started from at `+0x88c` |
| `+0x890` | ground rumble, with its id at `+0x894` |
| `+0x898` | wind |
| `+0x8a0` | machine rumble |

Each handle is -1 while silent.

### Engine loop

`Machine_UpdateEngineLoop` (0x801dcb18) restarts the loop whenever its handle is -1, so it is
always alive, and stops it while `md->xc39` bit 0 is set. Its input is the engine load that
`Machine_Star_Think` keeps at `+0x870`:

```
pitch_target  = min(x870 * engine_pitch_coef, 1.0)
pitch_target  = engine_pitch_min + pitch_target * (engine_pitch_max - engine_pitch_min)
x880         += clamp(pitch_target - x880, -engine_pitch_slew, +engine_pitch_slew)
x880          = min(x880, 2400)                  // the interpreter's cent ceiling

vol_target    = x870 * engine_volume_coef + engine_idle_floor
vol_target   *= engine_volume_air                if x754 == 1 (airborne)
vol_target   *= engine_volume_x6c                if xc34 bit 4
x884         += clamp(vol_target - x884, -engine_volume_slew, +engine_volume_slew)
x884          = min(x884, 1.0)
```

`x880` and `x884` are the loop's live pitch and volume. Pitch is in cents, so the Slick Star's
`-1200 .. +400` runs from an octave down at rest to a third above the sample's own tuning at full
load. `engine_idle_floor` is the volume the loop holds while
parked; only Bulk, Wagon, Turbo, Jet and Formula have one, and a nonzero floor makes a machine hum
wherever it sits.

### Surface loop

`Machine_UpdateSurfaceLoop` (0x801dc80c) has the same shape, driven by `|md->x36c|` clamped to
`surface_speed_max`, with extra volume multipliers for a rail-supplied id, for airborne, and for
`xc34` bit 4. Every star's `surface_volume_floor` is below the level that produces a voice, so a
parked machine is silent here too.

The id is the row's except while riding a rail (`xc3b` bit 5 clear and `xc33` bit 1 set). Then it
comes from the class-shared ground table `vcDataCommon->x8` (cached at `r13 + 0x760`), looked up
through `grRail_GetGroundType` (0x800d416c); every row there names `SFX_rail_runnoize_lp` except
the water row. When the id changes, the old instance stops and a new one starts, so stepping on
or off a rail restarts the loop. The same table's rows also carry the ground rumble id that
`+0x890` plays and the per-material impact sounds (`SFX_tyak_soil`, `_gravel`, `_ice` and so on),
which every machine shares.

### Charge loops

The charge loop plays while both bit 6 and bit 5 of `md->xc32` are set - the pair
`Machine_IncrementCharge` (0x801cc480) sets each frame the meter actually fills, and
`Machine_ClearChargeState` (0x801ca294) clears. Which of the three plays is a step function of the
meter:

```
charge_value <  charge_loop_split[0]  ->  charge_loop_sfx[0]
charge_value <  charge_loop_split[1]  ->  charge_loop_sfx[1]
otherwise                             ->  charge_loop_sfx[2]
```

The splits are `{0.33, 0.66}` on every star but the Rocket Star (`{0.10, 0.30}`). Crossing one
stops the running instance and starts the next from its head, so the vanilla samples are written
to hand off: each is a rising sweep ending where the next begins, played at full volume with
nothing modulating it. Their loop points sit late in the sample, so each sweeps once and then
holds a tail.

A full meter plays no fourth sound. Holding past the auto-discharge timer plays
`overheat_loop_sfx` once from `Machine_ChargeUpdate` (0x801ca4c0), alongside the discharge
animation; despite the name it is a one shot.

### Boost release

`Machine_PlayBoostSFX` (0x801dd3ec) picks a tier from the meter at release:

```
charge_value <  boost_thresh_min  ->  nothing           // 0.05
charge_value >= boost_thresh_l    ->  boost_sfx_l       // 0.80; 0.90 on Warp and Flight, 1.0 on Rocket
charge_value >= boost_thresh_m    ->  boost_sfx_m       // 0.60; 0.68 on Hydra, 0.90 on Rocket
otherwise                         ->  boost_sfx_s
```

A tier left at -1 releases silently.

### Rumble, starts and spin

`Machine_UpdateRumbleLoop` (0x801dd578) keeps `rumble_loop_sfx` alive at a volume scaled from
`md->xb84`, stopping it when that reaches zero; `Machine_SetRumbleLoopLevel` (0x801dd48c) is the
same with the level passed in. This is separate from the ground rumble at `+0x890`.

`engine_start_sfx` and `surface_start_sfx` fire together as the machine is mounted (from the state
enter at 0x801e1908, skipped when its mode argument is 3). Every vanilla star points them at the
same samples as its engine and surface loops; what makes them a start is the script.
`SFX_start_engine_slick` is 34 commands of swell under loop counters, ending on `0x0F` to release
the voice. `quick_spin_sfx` is a one shot from `Machine_PlayQuickSpinSFX` (0x801e383c).

### The class sound table

Each class's shared archive carries one more table, `vcDataKindStar[is_bike]->sound` (+0x0c),
which the class's attribute copy hands every machine as `MachineData.x654` and nothing else reads.
The star's entries (from +0x30, one per slot) are the air-noise loop - `SFX_airnoize_s/m/l_lp` -
which `Machine_SoundEffectThink` (0x801ee588) plays at the volume `Machine_Star_ApplyGrip` leaves
in `+0xc44`. The bike's (from +0x1c) are `{loop, one-shot}` pairs: the loop is driven by
`Machine_Wheel_Think` (0x801f5390) while moving, the one shot plays from the jump states.

## Replacement samples

Because the ids name scripts, a machine gets new samples through new scripts: append a SEM bank
whose scripts play the new global sound indices, and point the row's ids at it. Copying the
vanilla script a slot already names and rewriting only its `0x01` operand keeps that slot's
envelope, so a new engine loop is pitched and faded like the old one.

What decides whether a sample works is the record's loop flag, set by how the script ends. A
loop's script ends on `0x0E`, which leaves the voice running for the code to stop by handle; a one
shot ends on `0x0F`, which releases it. So:

| Wants a looped record | Wants a one-shot record |
|---|---|
| engine, the three charges, surface, rumble, both starts | the three boosts, quick spin, overheat |

The starts need a looped record even though they play once, because their swell outruns the
sample. The charge sweeps and the rumble loop from well into the sample, not from its start.

The free ARAM in a retail session, about 1.48 MiB, is the real budget on how much new sound a
machine can carry.
