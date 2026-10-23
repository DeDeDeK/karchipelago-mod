# Custom Machines: Engine Tables

How the `custom_machines` mod makes room for a machine the engine was never built to hold. The
engine keeps a fixed 19 stars and 7 bikes in dozens of tables indexed by class slot or by
`MachineKind`, none of them bounds-checked; every one a registered machine can reach is relocated,
replaced or taught about the appended kinds here. A modder needs this to know which engine reads
are safe for a custom machine and which still answer for vanilla only.

## Kinds and class slots

The engine addresses a machine as an `(is_bike, class slot)` pair. Stars are slots 0-18 and their
slot equals their `MachineKind`; bikes are slots 0-6 with `MachineKind` `19 + slot`. A registered
machine takes the next appended slot of its class (star 19 and up, bike 7 and up) and the next
`MachineKind` past 25, so its `MachineKind` is not its slot plus either class base.

The engine's own fold, `MachineGObj_GetAbsoluteKind` (0x801c85bc,
`is_bike ? slot + 19 : slot`), lands an appended star slot in the bike range and an appended bike
slot on another custom kind. Every reader of it a custom machine can reach is replaced below.

## Machine classes

Each class grows by `CUSTOM_MACHINE_MAX` (13) slots.

- **Archive names.** `stc_vcNameTable[is_bike]` (`r13 - 0x6150`) is each class's
  `{filename, symbol}` pair table. Both rows are copied into wider tables with each registered
  machine's disc path and `vcData` symbol at its slot, and repointed. The class-shared archives'
  pairs live apart, in `stc_vcClassNameTable` (0x804b07e0).
- **Loaded `vcData`.** `stc_vcDataLookup` (0x8055a068, `vcData *[2][19]`) moves to a mod-owned
  table with both rows as wide as the widened star row. Of its three readers, `vcData_InitLookup`
  (0x801c6c68) and `Vehile_LoadFile` (0x801c6d74) are replaced outright; the third,
  `MachineGObj_StoreVcDataPtr` (0x801c4f98), is patched in place - its `lis` at 0x801c4fd0, `addi`
  at 0x801c4fe8 and row-stride `mulli` at 0x801c5034 - because a hook there would clobber
  registers the surrounding code still needs.
- **Kind split.** `MachineDesc_SetKindAndIsBikeFromMachineKind` (0x801c857c) is the one canonical
  `MachineKind -> (is_bike, slot)` split, shared by the City Trial spawn path and many other
  callers; replacing it teaches them all the appended kinds.
- **Kind fold.** `Machine_EncodeVehicleKind` (0x801c85a8) is the reverse, adding 19 to a bike's
  slot only. It sits behind `Ply_GetVehicleKind`, Free Run's machine counts and the checklist and
  finish-line compares against a vanilla kind, and is replaced to answer the widened `MachineKind`,
  which none of those compares match.
- **Preload.** City Trial preloads every machine by walking a 26-entry enable table in
  `Machine_PreloadAll` (0x801c8cec); a hook on the branch out of that loop (0x801c8d8c) queues
  every registered archive too.
- **Star handler tables.** `stc_machine_star_init_handler` (0x804b15c0) and
  `stc_machine_star_think_handler` (0x804b160c) are 19-entry tables of per-machine handlers
  (only Hydra, Formula, Wagon and Turbo have any) that `Machine_Star_Init` (0x801e7f3c) and
  `Machine_Star_Think` (0x801eacbc) index by class slot with no bound and call when non-NULL. They
  end where the star class descriptor begins, so slot 19 would branch into rodata. Each is formed
  by one `lis`/`addi` pair (0x801e80dc/0x801e80e0 and 0x801eb524/0x801eb528), which is rewritten
  to point at a widened copy whose custom slots start NULL. A consumer fills them through
  `SetInitHandler` / `SetThinkHandler`.
- **Bike handlers.** `Machine_Wheel_Init` (0x801f3b54) and `Machine_Wheel_Think` (0x801f5390)
  dispatch through nothing indexed by kind, so a bike's handlers run from hooks on their
  epilogues: 0x801f3c70 with the machine in `r31`, 0x801f597c with it in `r29`.
- **Anim handlers.** `MachineGObj_AnimThink` (0x801c618c) runs for both classes and ends by
  calling `Machine_ColAnimThink` at 0x801c6274, which reapplies the ColAnim overlays. That call is
  replaced by one that runs it and then the kind's `SetAnimHandler` handler, so a color written
  there is the one that draws.
- **Stuck check.** `Machine_CheckStuck` (0x801d2b04) exempts Dragoon, Hydra and Wing Meta Knight
  from City Trial's stuck check by comparing the class slot alone against 8, 4 and 17, whatever
  the class, and an appended bike can sit at 8 or 17. A hook on the first compare (0x801d2ba8)
  hands the compares `0xFF` for an appended slot. Vanilla's Wheelie Scooter, bike slot 4, still
  matches Hydra's 4 and still skips the check.

## Patch-stat scaling

`Machine_ApplyStarStatScaling` (0x801e81e4) scales a machine's attributes by its patch stats out
of `(low, high)` pairs in the class table `MachineData.stat_scale` points at, a block of
`VcStar.dat`. Most pairs are one per class, but six blocks keep one row per kind, 19 rows each:
Top Speed's at `+0x068` and `+0x100`, Weight's at `+0x328` and `+0x3c0`, Glide's at `+0x538` and
`+0x5d0`. Each is read as `lbz kind / slwi 3 / addi block / add` with no bound, so slot 19 reads
the next block's first row - for Weight and Glide, `air_recover_len` rows of hundreds of frames
landing on a 0.01 `air_impulse`, which throws the machine off any ledge once either stat moves.

The function is `0xe6c` bytes with some sixty scale sites, and only these six depend on the
machine, so the six reads are rewritten in place rather than the function replaced. Each becomes
a `bl` to a resolver taking the machine and the row - a vanilla slot gets exactly the pair the
engine would have read, a custom slot its descriptor's `stat_rows` - followed by `fmr f1, f31` to
put the stat ratio back for the scale call that follows. The ratio lives in `f31` and both table
bases in `r29` / `r31`, all preserved across the call. The rewrites start at 0x801e83c0 and
0x801e83e4 (Top Speed, through `Machine_ScaleFromRatio`) and at 0x801e8f38, 0x801e8f64, 0x801e8fc8
and 0x801e9008 (Weight and Glide, through `Machine_ScaleFromRatio2`, 0x801cab94), where the ratio
save and the `md->attr` reload that vanilla interleaves with the four index instructions move
ahead of the call.

`Machine_ApplyBikeStatScaling` (0x801f3d44) keeps ten such blocks of 7 rows in `VcWheel.dat`'s
table: Top Speed at `+0x060` and `+0x098`, then eight Turn blocks `0x38` apart from `+0x128`, each
scaling a field of the bike's own handling block, all through `Machine_ScaleFromRatio`. The air Top
Speed read (0x801f3fb4) and the eight Turn reads (`0x28` apart from 0x801f41d8) take the same
rewrite. The ground Top Speed read at 0x801f3f90 saves the ratio inside its own four
instructions, leaving no room to restore it, so that window calls a helper that returns the scaled
value itself and the engine's own scale call after it becomes a no-op.

## CPU rows

CPU riders read three per-machine DOL tables and switch on the machine's kind in a few more
places, all through `MachineGObj_GetAbsoluteKind`; left alone, a custom machine would be driven by
another machine's rows or by whatever lies past the tables. The descriptor authors a
`CustomMachineCpu` instead, and every reader is replaced with one that hands a vanilla machine its
engine row and a custom machine its authored fields:

| Engine data | Readers | Fields |
|---|---|---|
| `stc_cpu_machine_caps_star` (0x804b8854) and `_bike` (0x804b89d0), stride `0x14` | `MachineGObj_CPUGetSwapScore` (0x8027699c), `MachineGObj_CPUCanSwap`, `MachineGObj_CPUCanBrake`, `MachineGObj_CPUCanChargeHold`, `MachineGObj_CPUCanRamCharge`, `MachineGObj_CPUSkipsPassageBranch`, `MachineGObj_CPUGetChargeRelease` (0x80276bfc) | `swap_score`, `flags`, `charge_release` |
| `stc_cpu_machine_steer_star` (0x804b8f30), star rows only; bikes read the Warp Star's | `Rider_CPUGetMachineAlignCosNear` (0x80277538), `AlignCosFar`, `StuckAngle`, `TurnTolerance` | `align_cos_near`, `align_cos_far`, `stuck_angle`, `turn_tolerance` |
| Air Glider and High Jump `{pitch, min_len}` pairs (0x804b8a5c, 0x804b8b24), 25 each by absolute kind | `Rider_CPUGetAirGliderMachineParam` (0x80277024), `Rider_CPUGetHighJumpMachineParam` (0x80277094) | `air_glider_pitch`, `air_glider_min_len`, `high_jump_pitch`, `high_jump_min_len` |
| The charge-hold gate Bulk, Hydra, Rocket and Formula carry | `MachineGObj_CPUGetChargeHoldGate` (0x80276de4) | `has_charge_hold_gate`, `charge_hold_gate` |
| The release level Bulk and Hydra carry | `MachineGObj_CPUGetChargeReleaseOverride` (0x80276ea0) | `has_release_level`, `release_level` |
| Hydra's held-up stick, Winged and Jet Star's dive once moving | the kind cache `Rider_CPUEmitSteerStick` compares | `stick_pitch` |

Every accessor is reached only through a plain `bl`, so each is replaced outright. A vanilla
machine gets exactly the row the engine read: the caps and steer tables by `(is_bike, slot)`, and
the stadium pairs and the two kind switches through `MachineGObj_GetAbsoluteKind`, which is exact
for vanilla. The kind cache is the one site rewritten instead - the `bl` at 0x8026bc6c in
`Rider_ProcessCPUDistance` (0x8026bbe0): its only reader compares it against Hydra, Winged and Jet
Star, so a custom machine caches Hydra for a climbing pitch, Winged for a diving one, and its own
`MachineKind` otherwise. `MachineGObj_CPUIsKindSlotFilled` (0x80276db4) asks whether the City
Trial spawn slot numbered by the machine's absolute kind is filled - a vanilla mis-index kept for
vanilla machines - and a custom machine asks after its own `MachineData.city_spawn_slot`. The Rex
Wheelie special cases in `Rider_CPUEmitSteerStick` and `Rider_CPUArbitrateManeuver` test for bike
slot 3 and pass over a custom bike.

## Per-machine counters and the KO seam

`PlayerStats` carries three `int[0x1a]` arrays indexed by absolute kind -
`machine_mount_kind_num` (`+0x37c`), `kills_by_machine` (`+0x3e4`) and `deaths_by_machine`
(`+0x44c`) - and their writers fold the kind themselves with no bound. A custom machine's fold
lands far enough out to write past all three into the KO-by-cause counters, `vehicle_bust_mask`
and `item_collect`, every one a checklist condition. They run back to back inside `PlayerStats`,
so there is no slack to widen them in place.

So the two arrays anything reads are mirrored in a mod-owned `[5][2][26 + 13]` table keyed by the
widened kind, through four call replacements:

| Site | Call | Replacement |
|---|---|---|
| 0x801ba190 in `RiderState_GetOnStarEnter` | `Ply_IncrementGetOnMachineNum` | counts the mount in the mirror only |
| 0x801e1f74 in `Machine_GiveDamage` | `Ply_AddDeath` | counts the KO, runs the engine's call, then tells every death handler |
| 0x8004e6a8 in `CityTrial_CheckFreeRunObjectives` | `Ply_GetMachineChangeCount` | sums the mirror (the ten-machine-changes cell) |
| 0x80012470 in `Game_Think` | `Ply_GetKONum` | sums the mirror (the Destruction Derby score) |

Both readers sum their whole array, so every vanilla answer is unchanged. `Ply_AddDeath` still
runs because it also sets the KO-by-cause counters, `vehicle_bust_mask` and the King Dedede frame;
a kind it has no bucket for is swapped for `VCKIND_WHEELVSDEDEDE` on the way in - in range, never
on the field and named by no vehicle-bust entry, so its own array writes land somewhere unread.
This is the game's only call to `Ply_AddDeath`, which is why `AddDeathHandler` exists: no other
mod can replace the same call. The mirror is cleared at `On3DLoadStart`, just before
`Player_InitAll` clears the engine's copies.

## HUD and radar placement

**Field blip.** `CityBlip_Create` (0x801226e8) builds one template joint per `MachineKind` 0-25
from `IfAll2c.dat`, posed on that kind's frame of the blip TexAnim, into
`Game3dData.blip_template` - an array followed directly by the per-player instance joints.
`CityBlip_GXCallback` (0x80122380) instances the template at `MachineGObj_GetAbsoluteKind`, so a
custom machine would wear another kind's template or, past the array, instance the instance joints
into themselves until `JObj_DispAll` recurses without end. A hook past the template loop
(0x80122840) builds one template per registered machine the same way, posed on its appended frame
of the grown blip bank; the callback's `bl` at 0x80122414 becomes a call returning the template
itself, with the three index instructions after it rewritten to `mr r22, r3` and two no-ops; and a
hook on `CityBlip_Destroy`'s tail (0x80122b20) frees the custom templates. The array is cleared
before each build, and when the blip bank did not grow on this load no custom template is built,
so the machine goes without a blip rather than wearing another machine's frame.

**Blip height.** `CityBlip_GetMachineLift` (0x800096b8), called only from the per-view placement
at 0x80122c2c, copies a 19-entry star table (0x804894a0) and a 7-entry bike table (0x804894ec)
onto its stack and reads one by class slot, times 0.175. It is replaced: a vanilla slot gets the
engine's value, a custom machine its descriptor's `blip_height`.

**Stadium icon.** `Machine_GetIconFrame` (0x8011584c) picks the stadium HUD's machine icon frame
from the 17-byte `stc_machine_icon_frame` (0x804a7b84) at `is_bike * 12 + slot`, where an
appended star slot would land on the bikes. It is replaced: a custom machine gets its appended
frame of `IfAll3c.dat`'s icon bank.

**Stat radar.** `MnRadar_PlaceMachines` (0x80045e14) lowers each player's machine model on the
City Trial stat radar screen by a float from a star table (0x80489558) or a bike table
(0x804895a4), copied onto its stack and read at the player's saved `(is_bike, slot)` in
`GameData.city`. A hook on the scale load after the read (0x80045ffc), with the player in `r30`
and the value in `f2`, swaps in a custom machine's `radar_drop`.

## Particle generators

A machine's animation bank names its exhaust by id into the vehicle particle bank, which the
engine loads from `EfPtclVehicle.dat` as bank 0 and whose 52 generators are ids 0-51. A
generator is a `0x3c`-byte `PtclDesc` header - texture group, size, launch velocity, gravity,
emission cone half-angle at `+0x24`, rate at `+0x28` - followed by bytecode that `Ptcl_TickOne`
steps per particle per frame, color opcodes included. A particle's color is read out of its
generator's program on the frame it spawns.

A generator is bank data, shared by every machine naming its id, so a machine that wants a
program to itself brings its own (up to four) in its descriptor. The bank names a machine's k-th
own generator `52 + k`; each machine's generators are installed at a range of their own past
every earlier machine's, and discovery rewrites the bank's particle slots to those ids. The
`Vehile_LoadFile` replacement writes the resolved ids over every loaded copy of the animation
bank, whole rather than as an offset, so a copy that already took them is unchanged.

The install is a hook on the tail of `Ptcl_LoadEfPtclVehicle` (0x80235394) at 0x802354bc, where
`psInitDataBanks` has just pointed `stc_ps_generator_desc[0]` at the loaded bank and set its count.
The hook copies the bank's pointers into a mod-owned table that already holds the machines'
generators at their ids, and repoints the table and count. Both readers bound an id by the count
and return nothing for a NULL entry, so a dropped generator emits nothing. A vehicle bank holding
more than 52 generators would collide with the machines' ids, so it is then left as loaded and
the machines emit none of their own. The hook reruns on every load because the bank is rebuilt
each time; `Ptcl_Alloc` hands an emitter its program pointer once, at creation, so it cannot be
done lazily. Nothing inside a descriptor is a pointer, so the copies need no relocation beyond
the `PTCL_FLAGS_RELOC_MASK` rewrite `psRelocDataBanks` gives every bank descriptor, which each
copy takes at boot.

A cruise generator emits for as long as the machine moves, so it is authored tight and
short-lived; a boost generator is a wide, long-lived flare. One in the other's slot reads as a
permanent spray rather than a trail.

## City Trial field spawn

The field roll reads a chance row from `VcCommon.dat`
(`(*stc_vcDataCommon)->spawn_data->spawn_desc[]`, rows selected by match progress) with exactly
`VCKIND_NUM` columns, and its loops are no wider, so an appended kind has neither a weight nor an
index to be picked at. The selection in `CityMachineSpawn_DecideAndSpawn` (0x801defac) and in
`CityMachineSpawn_SpawnFormationStar` (0x801df408, the Machine Formation event) builds its
history-exclusion mask twice in separate registers and clobbers the random result between its two
passes, so it is replaced whole: hooks at 0x801df00c and 0x801df44c skip to 0x801df220 and
0x801df630, taking `MachineSpawnData*` in `r30` and the match progress in `f1`, and leave the
chosen kind in `r31` for the vanilla history write and `CityMachineSpawn_Create`. Machine
Formation's vanilla loop is 19 wide, so its replacement rolls stars only, vanilla and registered.

The replacement picks the chance row as vanilla does, including vanilla's special case that rolls
the first window at a progress of exactly 1.0, where the clock running out pins it. It seeds one
weight per kind - the chance row for vanilla, `spawn_weight` for a registered machine - passes
each through the spawn weight filter if one is set, excludes the last four spawns (fewer when
fewer kinds are spawnable, so the only candidate is never excluded by its own history), and rolls.
With no filter the result is the vanilla distribution plus whatever the descriptors ask for. The
Compact Star, Flight Warp Star and both legendaries are 0 in every vanilla window, so only a
filter can raise them onto the field. If the filter zeroes every kind, the first kind it permits
at a weight of 1.0 is spawned instead, and one that permits nothing gets the Compact Star.

**Free Run** is a separate placer. `CityMachineSpawn_PickFreeRunKind` (0x801de41c) keeps one of
each machine in fixed spots, chosen from per-kind counts in `MachineSpawnData.freerun_placed` over
a 26-wide loop, so it never places a custom machine. The counts are moved by
`CityMachineSpawnGObj_Init`, `CityMachineSpawn_TakeMachine` (0x801decbc) and
`CityMachineSpawn_ReleaseMachine` (0x801ded8c), keyed by `Machine_EncodeVehicleKind`, which now
answers a custom kind past the array. Hooks on each count (0x801de2ec, 0x801ded04, 0x801dedd4)
skip a kind at or above `VCKIND_NUM`.
