# Archipelago Checklist Objectives

The Archipelago tab's 120 cells (`APCheckKind`, `clear_kind` 0-119) are objectives the vanilla
game never tracks: Nebula Belt's missing course cells, on-foot visits around the city, City
Trial events, Top Ride feats, the Archipelago Star. Every one is an in-game achievement - none
completes by booting or by receiving an item, since a location that fills itself is one the
seed's fill could hide progression behind. And every one must be able to fire: a location whose
check never comes still counts as reachable to the multiworld's logic.

This doc covers how the mod detects them: the seams it samples, the rules every objective
shares, and the mechanics behind the less obvious ones.

## Predicates read, samplers latch

The framework polls every predicate every frame in every scene, so a predicate never samples
game state. `APCheckDetect_IsSet(ck)` only reads one of two latches:

- **`ap_observed`**, a two-word bitmask held for the rest of the boot. Samplers set a bit with
  `APCheckDetect_Observe(ck)` the moment they see the objective met; the framework then records
  the check, which persists in `sent_checks`.
- **`APSave.checks`** (`APCheckProgress`), for the objectives that accumulate over many rounds:

| Field | Cell | Target |
|---|---|---|
| `allup_collect_total` | 3 | 5 All Ups |
| `purple_sr1_wins` | 26 | 3 wins |
| `race_color_mask` | 29 | bit per `KirbyColor`, all 8 |
| `tr_color_mask` | 87 | bit per `KirbyColor`, all 8 |
| `tr_steer_win_mask` | 91 | bit per `TopRideCourse`, all 7 |
| `drag_win_mask` | 111 | bit per DRAG RACE, all 4 |
| `ar_course_win_mask` | 112 | bit per Air Ride `GroundKind` `GR_PLANTS1`-`GR_ICE1`, all 9 |
| `tr_item_mask` | 119 | bit per `TopRideItemKind` 0-20 |

The targets are defined beside the predicates, so the counter and its test share one value. A
new counting objective adds a field here rather than a loose scalar in `APSave`.

## Seams

| Seam | Site | Samples |
|---|---|---|
| results | `On3DExit` | `GameData.stadium_results` after a stadium or an Air Ride race |
| city proc | rider proc, City Trial trial rounds | Position, `PlayerStats` counters, events |
| meadows proc | rider proc on `GR_PLANTS1` | The Fantasy Meadows shortcut |
| derby proc | rider proc in DESTRUCTION DERBY 1-5 | Falls (`Ply_CheckIfFallDead`) |
| timed-run wrappers | `bl`s at 0x80010d68 (`race3D_isFinished`) and 0x80010418 (`AirRide_OnLapComplete`) | Air Ride finish and best-lap times |
| KO handler | `custom_machines`' death-handler seam over `Ply_AddDeath` | Rider KOs: killer, victim, machine, attack |
| enemy-defeat wrapper | `bl Ply_RecordEnemyDefeat` at 0x802022ec | Enemy defeats |
| break wrapper | `bl Ply_IncrementYakumonoBreakCount` at 0x80105da0 | Prop breaks, any breaker |
| item-collect wrapper | `bl Ply_IncrementItemCollectNum` at 0x801db928 | Every pickup with its kind and spawn type |
| rail-fire wrapper | `bl Ply_PlayRailFireHitSFX` at 0x801d741c and 0x80196668 | A Rail Fire burn |
| damage wrapper | both `bl Machine_GiveDamage` in `Machine_DmgApply` (0x801c68c0, 0x801c6990) | Damage to a human's machine |
| bump hook | hook at 0x801dacb4 in `Machine_CheckMachineBumpCollision` | Machine bumps |
| prediction wrapper | `bl HSD_Randi` at 0x801279a0 in `stadiumPrediction` | A random stadium prediction |
| top ride wrapper | `bl TopRide_CheckPerCourseObjectives` at 0x8029cb74 and 0x802acd4c | Every kirby, every frame |
| ap_star handler | `ap_star`'s assemble handler | Archipelago Star assemblies |

The wrappers call the original function first and then look; each repointed `bl` is the only
call site of its function, so nothing reaches the counter around them. The rider procs are
attached at `On3DLoadEnd` to every human rider's GObj at `RDPRI_HITCOLL + 1`; the rider GObj
survives a machine swap or assembly, which recreates only the machine. `On3DLoadEnd` also resets
every per-round counter and latches what the samplers need about the round: whether it is a City
Trial trial round, a KIRBY MELEE round, and the loaded Air Ride ground. Top Ride never reaches
`On3DLoadEnd`; `OnTopRideLoadEnd` resets its state instead.

The title screen's attract demo is a real round with a CPU in every slot. `On3DLoadEnd` and
`OnTopRideLoadEnd` arm nothing when `Gm_IsAutoDemo()`, since the coral count, for one, credits
any breaker.

## Rules every objective shares

- **Humans only.** The achieving slot is `PKIND_HMN`; CPUs count only as opponents.
- **A win** is `ply_finished[p] && ply_placement[p] == 0` with at least one other human or CPU
  slot the rankers counted. The rankers place slots that never crossed the line too -
  `Stadium_ComputeRankByTime` (0x800108b0) falls back to race distance - so placement alone is
  not a finish, and a race started with no CPUs would otherwise hand out 1st for free.
- **Stale slots.** `Stadium_ExitMinor` (0x80014d5c) latches `stadium_results` in the copy loop
  that `On3DExit`'s hook follows, but skips the latch for a replay or the attract demo, leaving
  the last round's values; results are not read after a replay. A slot whose
  `rank_skip[p]` is nonzero has a stale placement and time and is ignored.
- **Times.** `ply_race_time == 0` is a did-not-finish and never pairs into a photo finish. In the
  time-ranked stadiums the game projects a time for a CPU that did not finish, which then counts
  as a finisher, as on the results screen.
- **"In one game"** is one 3D scene: `SceneLoad_3D` zeroes `PlayerStats` on every load, so a
  stadium visited mid-trial restarts the city counters, the same scope vanilla's cells use.
- **Stadiums** are recognized with `CityTrial_IsInStadium` under `MJRKIND_CITY`, not by
  `CityMode`: a trial's closing stadium still reads `CITYMODE_TRIAL`.
- **Air Ride courses** are the loaded ground, `Gr_GetCurrentGrKind()`, not
  `GameData.stage_kind`, which is the menu's selection and goes stale outside a race.
- **Machines** are identified by `CustomMachines_ResolveKind(cm_api, is_bike, kind)`, never by
  `Ply_GetMachineKind` alone. `PlayerData.machine_kind` counts stars and bikes from 0 separately,
  so compared raw a bike reads as some star; hoshi's `Ply_GetMachineKindAbs` fixes bikes but folds
  a custom star onto the bike range. The Archipelago Star's kind is assigned at boot and read
  through `GateApStar_MachineKind()`.

## Results objectives

`On3DExit` reads the latched results for Air Ride races and every stadium. Several cells mirror
gates vanilla puts on its own: the Nebula Belt distance cell (37) needs a timed race whose limit
is set to exactly 120 seconds, and its two lap-time cells (38, 52) a race configured to 2 laps,
so a longer race cannot pay out the 2-lap time. The Nebula Belt cells (36-40, 52-58) give that
course the set of cells every other course has and vanilla's evaluators skip - its ground has no
case in any of them - with targets scaled from the ratios between tiers the other courses
share.

The four course-avoidance cells (113-116) reverse a vanilla cell on the same course: ride the
Ferris wheel every lap, and avoid the quicksand, the spin panels or the Boost Panels. They read
the same `PlayerStats` counters vanilla does. The quicksand and panel counters keep running under
the autopilot that carries a finished racer, so the race-finish wrapper judges each human at the
line and stores the result; the cell pays out at the results only if that human also won.
`laps_no_ferris` is the longest run of laps without the wheel, so 0 means every lap rode it. A
spin panel is "used" only when crossed while charging: a star runs `Machine_CheckSpinZone` only
from its charging state. The Boost Panels are Magma Flows' grounded boost zones, which
`GrMagma_GetBoostPanelRefMask` (0x800d5740) returns in `zone_bits`' layout.

`max_time_spent_airborne` is the longest single airborne stretch, not a total, and only a
machine's airtime feeds it.

## City Trial rounds

The city proc runs only in a City Trial trial round. The per-round item cells read
`item_collect` with no baseline. Two sources besides the player's own pickups feed it, both
deliberately:
Archipelago items spawned onto a player are collected through `Machine_OnTouchItem` like any
pickup, so a patch from another world counts; and permanent patches credit their round-start
rise straight into the array on the City Trial map. Box kinds 0-2 count breaks, which is how the
per-color box cells work; vanilla only counts boxes as an all-colors lifetime total.

The visit cells test the rider's position against a sphere, on foot - a machine flying through
does not count. The spheres fit the spot: 2 units for the tiny flower platform on Castle Hall, 10
for the open model city. The lighthouse top tests standing within reach of the rotating head
above the tower's tapering wall; the waterwheel cell (on foot or not) tests the ledge and ramp
under the axle between the wheel's rims. "Highest point" is a height of 1000: a climb in the city
stops at 1040.3 against geometry, far above the sky garden, the highest place reachable without
flying. The grind-rail cell (105) needs one id of each of the five station links, which use a
pair of ids each, and ids 10-13; the river's ids 14-20 are not a rail to the player.

## City Trial events

One cell per event vanilla has no cell for (64-73), plus Dyna Blade (100) and Meteor (107).
"During" is `CityEvent_GetActiveKind()` (0x800ee8c4), which reports the event only in its running
state, for a natural event and one an Archipelago trigger forces alike; `custom_events`' kinds are
`>= EVKIND_NUM` and never match. Event GObj pointers survive into the next scene, so every read is
gated on a City Trial round.

Progress is per human in an `EventRun`, cleared whenever the active kind changes, so one run of an
event is one attempt, and a repeat of the same kind re-arms. No event starts over a running one,
so a change away from Fake Powerups or Meteor means it ran its course, and that is where those two
are judged; a round that ends mid-event never judges them.

- **Run Amok** compares distance travelled against the value when it began, in feet at vanilla's
  mileage factor.
- **Rail Fire** stations are told apart by the burner's nearest station centre; every fire is
  within 52 units of its own centre and the nearest two centres are 556 apart.
- **Lighthouse** repeats the game's own beam test (`CityLighthouse_InBeam`) per light, since the
  game's caller stops at the first light and never says which.
- **Prediction** names the real stadium four times in five; on the fifth it rolls a random
  `StadiumKind`. The wrapper remembers that roll, and the trial's stadium load judges it, so a
  random pick that happens to be right does not count.
- **UFO**: the first item of each ring the UFO drops is an All Up, spawn type `ITSPAWN_UFO`.
- **Machine Formation** machines hold their slot in `MachineData.formation_slot` until anything
  takes them out of formation. The bump hook sits before the first hit reaction, while both slots
  still hold; an attack or a boarding does not count.
- **Fake Powerups** wants five good patches and no fake; `ITSPAWN_DIRECT` pickups (Archipelago
  items) neither fail nor pad the run.
- **Meteor** counts damage to the human's machine (the damage wrapper) and any rise in
  `onfoot_dmg_total`, and needs at least 60 frames of event, which keeps a meteor plan that fails
  on its first frame from paying out.
- **Dyna Blade** counts pickups whose spawn type is `ITSPAWN_DYNABLADE`, in any event state, since
  a drop keeps its spawn type after she leaves.

## KOs, defeats and breaks

`Ply_AddDeath` (0x8022f648) is the only place a KO names both riders, reached from one `bl`
(0x801e1f74) in `Machine_GiveDamage`. `custom_machines` owns that call and passes each KO to its
death handlers with the victim's widened `MachineKind`; this mod registers
`APCheckDetect_AddDeath` once the registry resolves. The killer is the damage log's
`attacker_ply`, and `credited_attack` names the attack the KO is credited to. Busts compare the
victim's kind and the killer's ridden kind with the killer on its machine. The Panic Spin's hit
comes from the spinning rider, whose attack cause is `ATK_PANICSPIN`. The sphere shot carries an
attack cause no vanilla attack uses, `AP_STAR_SHOT_ATTACK_CAUSE`, so `GateApStar_IsShotAttack`
can test it with no import of `ap_star`.

DESTRUCTION DERBY's "without getting knocked out" (109) has three KO paths to watch: a hit to 0
HP (the KO handler, which marks the victim before vetting the killer), a fall (the derby proc,
since a fall never reaches `Ply_AddDeath`) and a received DeathLink (the DeathLink receiver calls
`APCheckDetect_OnKnockedOut` itself). VS. KING DEDEDE cells KO slot 4, the boss.

Rider kinds matter in stadiums: a stadium CPU picks its character from the gated select grid,
so once King Dedede or Meta Knight is unlocked a rival may be one, which the Kirby-only cell 42
has to test.

The Mic has no attack-method index of its own - its blast installs no hitbox - so cell 44 counts
defeats credited while the human holds Mic in `RDSTATE_MIKESING` or `RDSTATE_MIKEEND`. A ram
landing inside the blast counts too, the same direction of error the item cells accept. Only the
two KIRBY MELEE stadiums spawn ordinary enemies, which is what scopes the cell to them.

The coral cell (1) counts breaks at the credit path rather than reading one player's
`yakumono_break`, so it means "the coral is gone": a break with no clear attacker is credited to
the first occupied slot, and with CPUs in the city no single counter may ever reach the total.
The total comes from the stage, `Gr_GetYakumonoSpawnTotal`, as vanilla's Sky Sands coral cell
does for its own coral kind.

## Top Ride

Top Ride builds none of the 3D player, rider or machine objects, so none of the seams above reach
it. Its seam is vanilla's own per-kirby evaluator: the wrapper samples each kirby and then calls
`TopRide_CheckPerCourseObjectives`, once per occupied slot per frame, after the kirbys have moved
and been ranked. Sampling there matters for the item cells: `active_item_kind` holds the item
`TopRide_KirbyApplyItem` applied since the last frame until that evaluator resets it, so it sees
natural pickups and Archipelago item gives alike.

Everything requires the race to be running (`round_state == 2`). A kirby's state is its
state-handler vtable, and entering a state is a change from the last sample. A win is `standing`
0 on the frame `finished` is first seen, with another occupied slot, in a Race. Hit cells count
entries into the damage reactions, leaving out Ant Doom and the WATER falls, which vanilla asks
the player to use, and Speed Down. The Freeze Fan freezes through effectors that exist only in its
power state, so a kirby newly frozen while a human holds the fan is credited to that human. Lap
times come from `lap_progress` rising, and targets are frames at 60 per second, as in vanilla's
course table.

## The Archipelago Star

Cell 49 (location 410) is assembling the Archipelago Star. `ap_star` runs its assemble handlers
for every assembly - six spheres collected in the city, the star given outright, or a sphere give
that completes the set - and this mod's handler latches the cell for a human. Cell 50 (location
411) is Dragoon, Hydra and the Archipelago Star assembled by one player in one round: the city
proc reads `flags_84d`'s two legendary bits, which `Ply_MarkLegendaryMachineAssembled`
(0x80231198) sets and the scene load clears, and `GateApStar_AssembledThisRound(ply)`, cleared at
the same point. Cell 97 is a CPU KO'd by a sphere shot. Cells 49 and 50 also back the
`GOAL_ASSEMBLE_AP_STAR` and `GOAL_ALL_LEGENDARIES_CT` goals.

## In the apworld

`APLocation` restates every label and the `clear_kind` order by hand; nothing catches a desync.
Each box sits in the region where it is played, so it inherits that region's entrance rules
(stadium and course unlocks, the stadium prerequisite chains) instead of restating them. Only
item-spawn dependencies are written per box: the Mic for the Mic cells, a City Trial machine for
boxes that need something to ride, the named machine of a machine cell (plus Charge for Hydra and
the Archipelago Star, which cannot be steered without it), the event unlock for event cells, all
eight colors for the color cells. The three bust boxes (75-77) take vanilla's bust rules: both
machine unlocks, Charge for the Archipelago Star, and a damage source - except box 77, whose
sphere shot is one. King Dedede and Meta Knight are no damage source there, since a bust names
the machine ridden. The assembly boxes need all six sphere unlocks while City Trial items are
gated or the spheres are goal-forced, and the Red Box while boxes are gated, since spheres arrive
in red boxes.
