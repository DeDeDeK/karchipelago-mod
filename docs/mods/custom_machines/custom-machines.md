# Custom Machines

The `custom_machines` mod adds Air Ride machines - stars or bikes - that vanilla does not have.
It owns no machine of its own: it scans a drop-in folder on the disc at boot, and each archive it
finds there becomes a real machine with its own `MachineKind`, its own slot in its machine
class, audio rows, CPU rows and, if it asks, a `CharacterKind` with a cell on the select screens.
Every machine is authored whole in its own archive; nothing is copied from a vanilla machine to
fill one in. The one thing a machine borrows is the voice of a vanilla machine of its class,
where it ships no sound of its own.

## Finding the machines

Every `.dat` in the disc's `machines/` folder is a candidate. Any mod's `assets/machines/` lands
there through the ordinary asset staging, so a machine ships as part of whatever mod wants it,
or as a bare archive in any mod's assets, and this mod finds it without knowing that mod exists.
Nothing but a machine archive may sit in that folder with a `.dat` extension: the scan probes
every one for a `customMachine` public and reports those without one. A machine's other archives
(a cutscene's models, for instance) belong at the disc root.

Discovery runs in `OnBoot`, while hoshi routes `HSD_MemAlloc` and `Archive_LoadFile` to a
persistent bump arena. Each candidate is loaded with the ordinary `Archive_LoadFile` between an
`HSD_ArenaMark` and an `HSD_ArenaRelease`: everything the registry keeps - strings, stat and CPU
rows, particle programs, cutscene names - is copied out of the descriptor, and the arena is
rewound. The engine loads its own copy of the archive later, by filename, so the boot copy would
only cost heap.

Registration order is FST scan order, and it decides the appended numbers:

- The n-th machine registered takes `MachineKind` `VCKIND_NUM + n` (26 and up), whatever its
  class.
- It takes the next free appended slot of its own class: star slot `19 + s` for the s-th star,
  bike slot `7 + b` for the b-th bike.
- A machine that asks for a character takes the next `CharacterKind` from `CKIND_NUM` (20) up,
  counted only over machines that ask, so character kinds do not follow registry order.

`CUSTOM_MACHINE_MAX` (13) caps the registry across both classes. Every per-machine table is
sized from it; candidates past it are skipped and counted in one boot message.

## The archive

A machine archive exports two publics:

- its own `vcData` - the engine's seven-pointer machine struct, exactly as `VcStarSlick.dat` or
  `VcWheelWheelie.dat` ship theirs: attributes, model data, collision group, collision
  attributes, collision sphere, handling attributes and animation bank;
- `customMachine`, a `CustomMachineDesc` saying how to register it.

The attribute block is `0x1f0` bytes in either class and `Machine_AdjustAttributes`
(0x801c7278) copies it to `MachineData+0x460`, so its fields - the rider sit bone, base HP, the
two top speeds, the glide terms - are read live off the machine. The model data holds the main
model root, the bone count (highest joint index plus one; a wrong count detaches parts at
runtime), six LOD table sets (main and boost, at high, mid and low) whose bytes are flat DObj
indices in joint preorder, and a flat shadow root. The collision attributes are an analytic
float table; there is no collision mesh in a machine archive.

The animation bank is the class's own. A star's `vcAnimationStar` pairs a FigaTree with a
MatAnimJoint per state, names its particle generators and the joints they emit from, and carries
the boost sound ids:

| Slot | Plays |
|---|---|
| Moving | idle and driving, sped up with velocity, so it wants generous frames |
| Charge | driven by the 0-100 charge gauge, so it wants at least 100 frames |
| Unk1 | on boost |
| Boost | the idle held for the duration of a boost |
| Stop | stopping and braking |

A bike's `vcAnimationWheel` holds four anim pairs, a cruise and a boost generator id, four
emitter joints and the cruise emitter's timers. Quick spin has no slot in either bank; a machine
that deforms during one drives it from code off `RiderState_QuickSpinEnter` (0x801b7ee4).

Star models follow the vanilla layout: joints 0-5 are an unanimated armature chain, joint 6 is
the body root, and the rider sits on whatever joint the attribute block's `rider_sit_bone_idx`
names.

## The descriptor

`CustomMachineDesc` is an on-disc format with a magic of `CMCH` and a version stamp. The `.dat`
files and the mod are separate Riivolution files, so a descriptor whose `version` is not exactly
`CUSTOM_MACHINE_DESC_VERSION` is rejected rather than read against the wrong layout.

| Field | Meaning |
|---|---|
| `name` | display name, and the handle consumers find the machine by; the filename if NULL |
| `symbol` | the `vcData` public in the same archive |
| `is_bike` | class: 0 star, 1 bike |
| `wants_character`, `rider_kind` | take a `CharacterKind` and a select cell, ridden by this `RiderKind` |
| `audio_kind` | vanilla `MachineKind` of the same class whose sounds fill what the machine lacks |
| `spawn_weight` | City Trial loose-spawn weight; 0 never spawns loose |
| `description` | select-screen blurb, `\n` breaks the line; held to 63 characters |
| `generators` | up to four vehicle particle generators of its own |
| `cpu` | a `CustomMachineCpu`: how CPU riders treat the machine |
| `cine_file`, `cine_symbol`, `cine_machine_index` | the assembly cutscene archive, its public, and which vanilla legendary (0 Dragoon, 1 Hydra) it runs under |
| `stat_rows` | the class's per-machine patch-stat scaling pairs |
| `blip_height`, `radar_drop` | the City Trial field blip's height and the stat radar screen's model drop |

`spawn_weight` is weighed against the vanilla chance row, whose per-machine entries run 6-10 out
of a total near 115, so 1.0 is roughly 0.8% of field spawns. `blip_height` and `radar_drop` are
in the units of the engine's own per-kind tables, which it scales by 0.175: the blip rides 1.87
over most stars, 2.2 over the Slick Star, 8.4 over the Wagon Star and 7.2 over the Wheelie Bike,
and the radar lowers most stars by 5.87 and the Wheelie Bike by 8.

**Stat rows.** The engine turns each patch stat into a ratio in `[-1, 1]` against
`Patch_GetMaxValue`, and scales some attributes by a `(low, high)` pair it keeps one row per
machine: `low` is weighed when the ratio is negative, `high` when it is positive, and at exactly
zero - every Air Ride machine, which carries no patches - the row does nothing. A star lists
six rows:

- `top_speed_ground`, `top_speed_air` - multipliers, `1 + |ratio| * (pair - 1)`;
- `weight_air_impulse`, `glide_air_impulse` - added to `vcHandlingAttr.air_impulse`,
  `|ratio| * pair`. That field is the scale of the kick `Machine_Star_ApplyAirImpulse`
  (0x801ebe88) adds in flight, 0.01 on the Slick Star; a row an order of magnitude off
  launches the machine the moment it leaves the ground;
- `weight_air_recover`, `glide_air_recover` - added to `air_recover_len`, the frames
  `Machine_Star_ApplyAirThrust` blends airborne thrust over (600 on the Slick Star).

A bike lists ten, all multipliers: the two top speeds, then eight Turn rows, `turn_074` through
`turn_0a0`, each named for the `MachineData.attr` offset it scales - fields of the bike's own
handling block, which starts at `attr + 0x1c`.

The HSD toolchain's machine-descriptor script prints a descriptor - particle slots included -
and edits its stat rows, CPU fields, `audio_kind`, `spawn_weight` and the two heights in place,
rebuilding the descriptor's tail of strings, rows and programs while leaving the machine's own
data where it is. A script that builds a machine archive can append a descriptor to any data
section through the same module.

## What discovery checks

A candidate that breaks any of these is reported and not registered:

- the `customMachine` public, its magic, its version, and an `is_bike` of 0 or 1;
- a disc path under 64 characters, and a name and `symbol` under 32 - a string cut short would
  name a different file or symbol, or a name no consumer could look up;
- a name no earlier machine took;
- a `vcData` public under `symbol` with all seven pointers set and a model root;
- an `audio_kind` that is a vanilla `MachineKind` of the machine's own class (0-18 for a star,
  19-25 for a bike);
- `stat_rows` and `cpu` both present, since there is no engine row to fall back on;
- a valid `RiderKind` when the machine wants a character.

These drop only the offending part, with a report:

- a `spawn_weight` outside 0-1000 becomes 0;
- a cutscene needs both `cine_file` and `cine_symbol`, each under 32 characters; naming one, or
  one too long, gets no cutscene;
- more than four generators are clamped to four, and a generator without a descriptor, no
  longer than the `0x3c`-byte `PtclDesc` header, over 256 bytes or not ending on `0xfe` or `0xff`
  keeps its id but emits nothing;
- each animation-bank particle slot must be -1, one of the vehicle bank's own ids below 52, or
  `52 + k` for the machine's own k-th generator; anything else becomes -1.

## Side-cars

Two optional files sit beside the machine's `.dat`, same basename, found by name rather than
scanned:

- a `.ssm` sound bank, which gives the machine a voice of its own in place of its `audio_kind`'s;
- a `.art` archive (`customMachineArt` public, magic `CMAR`), which gives the machine its own
  picture everywhere the game draws one, in place of the registry's placeholder.

A machine archive is the source of truth and is edited in place. Asset folders are not
committed, so archives carrying retail-derived data stay out of the repository.

## Consuming the registry

`Hoshi_ImportMod(CUSTOM_MACHINES_MOD_NAME, ...)` returns a `CustomMachinesAPI`. Mods boot in
alphabetical order, so a consumer that sorts before `custom_machines` gets NULL from an `OnBoot`
import; import at `OnSaveLoaded`, the first callback past every `OnBoot`. NULL then means the
mod is absent, and the header's inlines (`CustomMachines_KindNum`,
`CustomMachines_CharacterKindNum`, `CustomMachines_ResolveKind`, `CustomMachines_ClassIndexOf`,
`CustomMachines_GetRespawnKind`) answer with hoshi's vanilla values for a NULL pointer, so a
consumer writes one code path for both builds.

Kinds are handed out in scan order, so nothing may hardcode one. A registered machine's
`MachineKind` is not its class slot plus a class base either, so hoshi's
`MachineKind_ClassIndex` / `MachineKind_FromClassIndex` are right only for vanilla kinds; use
`ClassIndexFromKind` / `KindFromClassIndex`, which cover both.

| Call | Contract |
|---|---|
| `GetCount`, `GetKindCeiling`, `GetCharacterKindCeiling` | registered machines, and one past the highest `MachineKind` / `CharacterKind` in use |
| `KindFromClassIndex`, `ClassIndexFromKind` | `(is_bike, class slot)` to `MachineKind` and back, custom slots included |
| `FindKindByName` | the one handle tying a drop-in archive to feature code; names are unique, so the answer is never ambiguous |
| `SetAvailabilityFilter` | narrows who gets a select-screen icon; asked per screen rebuild |
| `SetSpawnWeightFilter` | reweighs each kind in the City Trial field spawn roll; asked per spawn |
| `AddDeathHandler` | told about every KO, after the engine's bookkeeping, with the victim, its `DmgLog` and the widened kind it rode; up to four |
| `SetInitHandler`, `SetThinkHandler`, `SetAnimHandler` | claim a registered machine's per-kind handler slots |
| `GetMachineJoint` | the live joint for a depth-first index into the archive's own joint tree |
| `GetGenerator` | the registry's copy of one of the machine's own particle generators |
| `StartAssembly`, `MountMachine`, `GetRespawnKind` | the legendary assembly cutscene, the plain mount, and the kind a player was last mounted on this scene |

Each filter holds one function, since one mod owns that policy, and either may be set from any
scene. The availability filter receives the engine's own answer and may narrow it
(`return default && mine`) or ignore it. The spawn filter receives the seeded weight and returns
the one to use; 0 keeps a kind off the field.

The handler slots are for per-machine work a consumer has no hook of its own for. `Init` runs
once as a machine of the kind is created and `Think` once per frame per machine, each at the end
of its class's own; `Anim` runs once per frame per machine at the end of `MachineGObj_AnimThink`,
after the ColAnim overlays are reapplied, so a material color written there is the one that
draws. `GetMachineJoint` walks the archive's `JOBJDesc` tree to the index and finds the live joint
by the descriptor back-pointer `JObjLoad` leaves at `JOBJ+0x84`, so it does not depend on how the
engine roots the instance. `GetGenerator`'s copy is what the vehicle particle bank points at, so a
write to it recolors every particle that generator spawns afterward and nothing else on the field.

The API is exported even when `machines/` holds nothing, and the select-screen packing, the spawn
roll, the KO seam and the cutscene archive load are installed regardless, since a consumer's
filters, death handlers and `StartAssembly` reach them; the UI art splice also runs regardless,
since it moves frames vanilla characters use. Everything else is installed only when a machine
is registered, and patches only one class reaches only when that class has a machine.

## Limitations

- The registry holds 13 machines; a 14th archive is skipped and reported.
- A select list longer than 22 icons on Air Ride or 21 on City Trial runs onto the
  controller-claim state that follows it in `GameData`. Offline nothing reads those bytes after
  their initialization, so only multi-console (LAN) play with that many icons loses its
  controller claiming.
- A machine still borrows a vanilla voice: its `audio_kind` supplies every sound its `.ssm`
  leaves empty, the script envelope each drop-in sound plays through, and its class sound table
  entry, which no `.ssm` role reaches.
- The assembly cutscene runs under a vanilla legendary's rider pose, fanfare and sky preset.
- Free Run never places a custom machine: its placer counts and spots are `VCKIND_NUM` wide.
