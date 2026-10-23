# Custom AI

Behavior presets, bug fixes, perception and difficulty controls for CPU riders in all three
modes, and presets, damage and behavior for enemies. The mod layers decisions over what the
vanilla brain already computes rather than replacing it: CPUs keep the stage's path-finding,
steering and stuck recovery, and still play at their CSS level. It exports `CustomAIAPI` for
other mods: hunts, frenzies and a level offset.

## Settings

| Page | Option | Values |
|---|---|---|
| City Trial AI | CPU AI | Default, Cautious, Reckless, Aggressive, Hoarder, Strategist, Random |
| | Enemy AI | an enemy preset (below) |
| | CPU Stat Pool | Off, Low, Default, High, Max, Adaptive |
| Air Ride AI | CPU AI | Default, Cautious, Reckless, Racer, Random |
| | Enemy AI | an enemy preset |
| | CPU Catch-up | Off, Default, Strong, Skill |
| | Racing Line | Off, Racers (default), All CPUs |
| Top Ride AI | CPU AI | Default, Cautious, Aggressive, Hoarder, Expert, Random |
| General | CPU Fixes | On by default |
| | Stadium CPUs | On by default |
| | CPU Targeting | Default, Humans, Leader, Lead Human |
| | CPU Teamwork | On by default |
| | Adaptive CPUs | Off by default |
| | Enemy Damage | None, x0.5, Default, x2, x3 |
| | Enemy Behavior | Default, Pursue, Skittish |
| | AI Overlay | Off, Text, World, Both |

Random rolls one of the menu's non-Default presets per CPU (per scene load for Enemy AI).

## How the CPU brain is hooked

The vanilla brain picks a profile (`CpuData.ai_state`) once per stage - Navigate in the City
Trial city, Cruise+ on a course - and the mod never changes it. Every roll a preset adds scales
with `Rider_CPUDifficultyScale` (CPU level / 8). The main seams:

| Site | Address | Register | Used for |
|---|---|---|---|
| `RiderGObj_CPUThink`'s `_Rider_UpdateCPU` call | 0x8018fc80 | call | The shared per-tick view, self perception, pad overrides, debug capture |
| `Rider_CPUInit` epilogue | 0x80262fbc | r31 RiderData | Preset roll, level offset |
| `Rider_CPUDecideNavigate`, after `nav_target_pos` is picked | 0x80272258 | r28 RiderData | City goals: chases, Strategist, route fix |
| `Rider_CPUArbitrateManeuver` exit | 0x80275bc4 | r31 RiderData | Maneuver overrides, every profile |
| `Rider_CPUScanItems`, before the per-item score | 0x80263cf8 | r19 ItemData | Lets the score see the item |
| The per-item `Rider_CPUGetItemScore` call | 0x80263d00 | call | Preset and custom-item scores, claims, reachability |
| Navigate's item rescan | 0x80272144 | call | Whole-city rescans |
| `Rider_CPUDifficultyScale`, `Rider_CPUInvDifficultyScale`, `Rider_CPUGetSteerEnvelope` | 0x80276f00, 0x80276f7c, 0x80276650 | replaced | Racer skill, Skill catch-up |

Everything every CPU shares - the view of each rider and of the world, the teamwork claims, the
hunt and frenzy timers - is refreshed by the first CPU update of each tick. It runs only where
CPUs think, and freezes with the riders through the match pause and the hitstops.

Presets are recorded per `RiderData.ply` at every init, so no scene inherits the last one's. A
human slot's autopilot (`Game_Think` gives it a `CpuData` before marking it CPU), stadium events
and the title demo keep the Default preset; the demo also keeps every CPU's CSS level, the
vanilla stat pool, catch-up and enemy damage, while the fixes and targeting still run.

**Reachability.** `Rider_CPUFindGoalRange` matches the nearest CPU range by height alone, and a
goal it finds no range for gets no plan, so a goal on a rooftop or a ledge leaves the CPU
stalled on the road beneath or beside it. The mod accepts a goal only when FindGoalRange finds
a range and one of the range's three lines passes within 20 units vertically and 60
horizontally. Answers are cached per object for two seconds in a 256-entry table hashed by
address, cleared at each 3D load.

**Custom items.** A custom item's `ItemData.kind` holds the vanilla kind it was cloned from, so
vanilla scores it as that kind. With custom_items loaded, the mod asks `CustomItemsAPI.GetItemKind`
for the real kind and scores a custom item only by what it visibly grants: a bad or fake one -1,
one with stat grants (`Item_GetEffectInfo`) as a patch (50), anything else 0 - its effect lives
in its mod's code, which a CPU cannot see.

## CPU presets

| Preset | Modes | Behavior |
|---|---|---|
| Cautious | both | A second roll at the dodge Wiggle (0.05 -> 0.35 a frame by level) when an enemy, projectile or attacking rider is about to hit; in the city, heads for a Recovery Zone under 45% HP and keeps 300 units from any rider it cannot hurt (Candy, invincible) within 150 |
| Reckless | both | Never Wiggles; a second roll at the dash-zone maneuver (0.02 -> 0.22) |
| Aggressive | City Trial | Chases the nearest vulnerable rider (stunned, on foot, under 35% HP) within 150 -> 600 units, else its rival, never one it cannot hurt; quick-spins the one it chases within 25 units ahead; the dash-zone roll |
| Hoarder | City Trial | Scores patches, boxes, timed boosts and legendary parts 100, keeps food's score, ignores abilities, weapons and Candy, and rescans the whole city every 300 -> 30 frames |
| Strategist | City Trial | Builds for the round's stadium and pursues goals (below) |
| Racer | Air Ride | Launch-charges before GO on any machine that can brake, steers with level 8's envelope and alignment, adds 0.25 to its skill, rolls 0.25 a frame at the first dash zone ahead (so takes nearly all of them), and drives the racing line |

The dash-zone maneuvers are the cascade's ZonePress (3) and ZonePass (4): `stc_cpu_forward`
holds the dash panels, dash gates and copy panels ahead, and the CPU steers through one. A
second roll copies the nearest `side == 1` entry to `zone_target_id` / `zone_target_pos` as the
cascade does. A Wiggle is a random stick shake, which is a quick spin: it dodges and attacks.

The skill bonuses reach every roll that goes through `Rider_CPUDifficultyScale` and the steering
alignment. The junction pick, the LaunchCharge gate and the ability press timing read the raw
level and stay at the CSS level.

Chases aim where the target will be: its position plus its velocity times the frames the CPU
needs to cover the distance, capped at 30.

Aggressive, Hoarder and Strategist act through Navigate's targeting, so Air Ride does not offer
them: on a course a target off the racing line would stop the CPU lapping.

### Strategist

The stadium is decided in `CityTrial_Init`, so `Gm_GetCurrentStadiumGroup` is known from the
city's first frame. Each stat patch scores 6 times how much the group wants that stat (0-10; a
glide stadium wants no Weight, a combat stadium Offense, Defense and HP), All Up 70, blue boxes
40, timed boosts 5, legendary parts 60 plus 15 per piece of that machine already held; combat
stadiums add abilities and Candy. A stat the stadium does not want scores 0 and is left on the
ground. Like a Hoarder it rescans the whole city every 300 -> 30 frames.

Every 20 frames (staggered per rider) it scores goals against plain item collection (35):

| Goal | Utility | Target |
|---|---|---|
| Flee | 100 | 500 units out of a falling meteor's predicted blast (1.5x its radius, landing within 240 frames) |
| Heal | 70+ | The nearest Recovery Zone (`GrYakuRecoveryZone_FindNearest`) under 45% HP |
| Visit | 60 / 45 | A Secret Chamber; the Lighthouse beam spot (`Lighthouse_FindBeamSpot`) |
| Strike | 55 / 50 | TAC within 900 (it drops what it stole); the Pillar event's pillar (`GrYakuEventPillar_GetPos`) |
| Machine | 30+ | An empty field machine that suits the stadium 3+ points better than the ridden one, within 1200, not claimed by a closer CPU; +20 in the last quarter of the round |

A machine no suitability table rates (a custom one) scores 5 of 10. A goal holds for 90 frames
unless Flee preempts it, and is dropped - its target ignored for 900 frames - when it gets no 20
units closer in 300. Navigate's hook writes the goal into `nav_target_pos`. At the arbitration
exit, a Machine goal claims the machine as `city_object` within 100 units and commits Dismount
(0xd) within 30, after which vanilla boards it by contact; a Strike goal quick-spins within 25.
While a hunt or frenzy holds Navigate, the goal is left alone. Hydra and Dragoon are never
left, as in vanilla.

## Targeting, teamwork, hunts and frenzies

CPU Targeting rewrites `rival_ply` at every arbitration exit: Humans picks the nearest human,
Leader the best-ranked other rider and Lead Human the best-ranked human (race rank on a course,
stat total in the city). The Attack and Patrol profiles steer at the rival; in the city it is
what Aggressive falls back to chasing and what the Aggressive, hunt and frenzy spins aim at. The
Vs. King Dedede fight (`CITYKIND_VSDEDEDE`) keeps its own rivals: Dedede for the challengers,
and none for him.

CPU Teamwork: each tick every city CPU claims its item target and its machine or goal target
(the Strategist's, or vanilla's `city_object`); another CPU farther from a claimed item scores
it a third, and a Strategist skips a machine a closer CPU claimed.

`CustomAIAPI.Hunt(ply, frames)` sets every CPU on one player: city CPUs chase it across the map,
and every CPU quick-spins it when close. `Frenzy(frames)` raises every CPU to level 8 from the
next CPU tick, restoring each one's level afterwards, and sends them after their rivals. Both
count down only while CPUs think and end at the next 3D load.

## Difficulty

- **Adaptive CPUs.** After an Air Ride race a human finished, or a City Trial stadium, the
  best-placed human in 1st raises a level offset by one and in last lowers it, within +/-4.
  Every CPU init adds it (plus `SetLevelOffset`'s base offset) to `difficulty_level`, clamped to
  0-8. It moves the brain only: Speed Help's per-level CPU bonuses read the CSS level
  (`Ply_GetCpuLevel`).
- **CPU Catch-up.** The Speed Help rule's `AirRide_ApplySpeedHelp` gives CPUs three extras on top
  of the band every racer gets, from three per-level tables only it reads: catch-up toward the
  best human (`cpu_catchup_scale`), a flat `cpu_speed_bonus` and `cpu_mid_race_bonus` inside the
  middle third of the race. At each 3D load the mod writes the three tables from their shipped
  values times 0 (Off, Skill), 1 or 2 (Strong). Without Speed Help nobody gets speed catch-up.
  Skill instead moves each CPU's skill by its gap to the leading human along the course
  (`player_race_distance`): up to +0.5 when 150 m behind, down to -0.25 when as far ahead.
- **CPU Stat Pool.** City Trial seeds each CPU slot a pool of free stats
  (`GameData.city.cpu_stat_budget`) that `CityTrial_GrowCpuStats` drains onto its machine. A hook
  on the seed loop in `SceneLoad_3D` (0x80014ad4, r25 the slot) scales it by 0, 0.5, 1, 1.5 or 2.
  Adaptive seeds 0, and a hook inside `CityTrial_GrowCpuStats` once its interval passes
  (0x80015a50) sets each CPU's pool to how far its stat total trails the best human's before the
  drain, so it catches up at its level's drain rate and stops once level.

## Top Ride

Top Ride's CPU is a separate brain (`TopRide_CpuInputThink`, 0x802eee90) reached through the
reader vtable's slot 3 (`stc_topride_cpu_reader_vtable`), which the mod points at a wrapper.
Presets are rolled per slot at each Top Ride load. A preset scales the reader's level row of the
item, rival and ram aggression tables (0-100) for the call and restores it: Cautious zeroes rival
and ram, Aggressive triples them and halves items, Hoarder triples items and halves the rest.
The ram table also feeds `TopRide_CpuDetectObstacle`, so those presets change how readily a CPU
reacts to walls and air currents too. Expert sets `reader->difficulty` to 4.

## Enemies

Every enemy reads one global table, `EnemyParamTable` (`Enemy.dat`'s `emDataAll`). A preset is
three multipliers on it, written from a saved vanilla copy in the `Enemy_LoadCommonParams`
epilogue (0x801fd664), since a preloaded `Enemy.dat` is reused in place:

| Preset | range (`detect_range`, `leash_range`) | retarget (`retarget_min/max`) | knockback (`kb_launch`) |
|---|---|---|---|
| Aggressive | 1.75 | 0.6 | 1.0 |
| Relentless | 2.5 | 2.0 | 0.65 |
| Docile | 0.4 | 1.3 | 1.0 |
| Erratic | 1.15 | 0.3 | 1.0 |
| Tanky | 1.0 | 1.0 | 0.4 |

The City Trial city has no regular enemies, so its preset reaches the Kirby Melee stadiums and
any event actor that uses the default knockback.

**Enemy Damage** replaces `EnemyAnimCmd_SetHitDesc`'s `EventActor_EmitHitDesc` call
(0x8020111c): the decoded region (`HurtParams`) is on the stack, so its `base_damage` and
`base_knockback` are scaled before the vanilla call arms it.

**Enemy Behavior** moves the flyers - Bronto Burt, Scarfy and Bomber, which hover around
`pos_initial` - from a `GAMEPLINK_1` proc created at each 3D load while the option is not
Default, so it freezes with the enemies: Pursue drifts the anchor 0.6 units a frame toward the
nearest rider within `leash_range`, Skittish away. Actors in a common state (below
`ENEMYSTATE_PERTYPE`: hit, inhaled, launched) are left alone.

## AI overlay

Created at each 3D load while the option is on. Text puts a screen text in the top left,
refreshed every 6 frames from a p_link 0 proc, so it keeps up through the pause: a header with
the level offset, the running City Trial event and whether the racing line is built, then one
line per CPU with level, preset, profile, committed maneuver, goal, rival, riding or on foot,
speed and charge. World draws, through walls, each CPU's route waypoints, its steer target and
its goal, the racing line ahead of the first CPU, every hazard sphere and each falling meteor's
predicted impact, on a GX layer in the 3D scene. Both shows the two.
