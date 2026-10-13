# Custom AI

Behavior presets for CPU riders and enemies, and a scale on City Trial's free CPU stat growth.
Each preset layers decisions over what the vanilla brain already computes rather than replacing
it, so CPUs keep their stage's path-finding and stuck recovery and still play at their CSS
level. The mod keeps no state beyond its settings and acts only in City Trial and Air Ride.

## Settings

The Custom AI page has a City Trial submenu (CPU AI, Enemy AI, CPU Stat Pool) and an Air Ride
submenu (CPU AI, Enemy AI). Each AI option picks a preset or Random, which rolls one of that
menu's non-Default presets - per CPU rider for CPU AI, per scene load for Enemy AI.

## CPU presets

The game picks a CPU's profile (`CpuData.ai_state`) once per stage - Navigate in the City Trial
city, Cruise+ on an Air Ride course - and the mod never changes it, nor `difficulty_level`.
Every roll or range a preset adds scales with `Rider_CPUDifficultyScale` (CPU level / 8), so a
higher-level CPU plays its preset better.

| Preset | Menus | Behavior | Level 0 -> 8 |
|---|---|---|---|
| Default | both | vanilla | - |
| Cautious | both | vetoes RamCharge and PursueLOS; a second roll at the hazard Wiggle | dodge chance 0.05 -> 0.35 a frame |
| Reckless | both | vetoes DodgeWeapon and the hazard Wiggle; a second roll at RamCharge | ram chance 0.02 -> 0.22 a frame |
| Aggressive | City Trial | steers at the `Rider_CPURivalSelect` rival while it is in range, ahead of items and machines; a second roll at RamCharge | chase range 150 -> 600 units; ram as Reckless |
| Hoarder | City Trial | scores boxes, stat patches, All Up, the Max boosts and legendary parts 100, keeps food's HP-scaled score, ignores abilities, weapons and Candy; rescans the whole city for a better item; vetoes RamCharge and PursueLOS | rescan every 300 -> 30 frames |

Aggressive and Hoarder act through Navigate's targeting, so Air Ride does not offer them: on a
course, a steer target off the racing line would stop the CPU lapping.

A preset acts through three seams:

- **Assignment** - a hook in the `Rider_CPUInit` epilogue (0x80262fbc, `r31` the `RiderData*`)
  records the rider's preset in a table indexed by `RiderData.ply`, rolling Random there. It
  records Default unless `Ply_GetPKind` says CPU - the autopilot `Game_Think` gives a human slot
  allocates a `CpuData` before the slot is marked CPU - and unless the scene is Air Ride or the
  City Trial city (`CityTrial_IsInStadium` false), so stadium events keep vanilla CPUs. Every
  init overwrites its slot, so no scene inherits the last one's presets.
- **The committed maneuver** - a hook at the exit of `Rider_CPUArbitrateManeuver` (0x80275bc4,
  `r31` the `RiderData*`). A veto sends the maneuver back to `base_maneuver`, which holds for as
  long as the cascade keeps choosing it. The second Wiggle roll reads the hazard list
  (`stc_cpu_hazards`) as the cascade does - an imminent, non-empty entry, with the no-dodge
  desire clear - and never replaces Coast, AvoidObstacle, the nav-steer maneuvers 0xe-0x10 or
  the cascade's own TapOnce and Wiggle. The second RamCharge roll takes the nearest `side == 1`
  entry of `stc_cpu_forward`, copies it to `CpuData+0xd0` / `ramcharge_target_pos` as the
  cascade does, and only replaces cruising (`base_maneuver`), PursueLOS or ChargeHold.
- **Navigate's targets** - a hook in `Rider_CPUDecideNavigate` at 0x80272258 (`r28` the
  `RiderData*`) runs after the handler picks `nav_target_pos` and before it becomes the steer
  target and route goal; Aggressive overwrites it there. Hoarder replaces two calls:
  `Rider_CPUScanItems`'s per-item `Rider_CPUGetItemScore` call at 0x80263d00 and Navigate's
  rescan of its current item target at 0x80272144, which on the interval first scans the whole
  map around the rider.

Top Ride is not hooked: `Rider_CPUInit` never runs there, so neither menu covers it.

## Enemy presets

Every enemy reads one global table, `EnemyParamTable` (`Enemy.dat`'s `emDataAll`), so a preset
is three multipliers on it:

| Dial | Fields | Effect |
|---|---|---|
| range | `detect_range`, `leash_range` | How far enemies notice and pursue riders |
| retarget | `retarget_min`, `retarget_max` (rounded) | Below 1 switches targets more often, above 1 locks on |
| knockback | `kb_launch[4]` | Below 1 flies less far when hit |

| Preset | range | retarget | knockback |
|---|---|---|---|
| Default | 1.0 | 1.0 | 1.0 |
| Aggressive | 1.75 | 0.6 | 1.0 |
| Relentless | 2.5 | 2.0 | 0.65 |
| Docile | 0.4 | 1.3 | 1.0 |
| Erratic | 1.15 | 0.3 | 1.0 |
| Tanky | 1.0 | 1.0 | 0.4 |

The hook sits in the epilogue of `Enemy_LoadCommonParams` (0x801fd664), which runs on every 3D
scene load after the table pointer is stored at `stc_enemy_param_table`. `lbLoadArchive` reuses
a preloaded `Enemy.dat` in place, so the table can arrive already scaled; the mod copies the
first table it ever sees, necessarily vanilla, and always writes `vanilla * multiplier`.
`Scene_GetCurrentMajor()` picks the Air Ride or City Trial selection; any other major (the title
demo) gets Default back.

In City Trial the preset reaches the Kirby Melee stadiums' enemies and, in the city, any actor
that uses the default knockback; TAC and Dyna Blade install their own hit reactions and read
none of these fields.

## CPU stat pool

At City Trial load the game seeds each CPU slot a pool of free machine stats
(`GameData.city.cpu_stat_budget[slot]`, sized by CPU level) that it drains onto the machine over
the round. A hook on the seed loop's slot increment in `SceneLoad_3D` (0x80014ad4, `r25` the
slot) multiplies the freshly stored pool by the CPU Stat Pool setting:

| Setting | Off | Low | Default | High | Max |
|---|---|---|---|---|---|
| Multiplier | 0 | 0.5 | 1 | 1.5 | 2 |

Off leaves the per-tick drainer nothing to hand out. Humans are seeded 0, so scaling them does
nothing.
