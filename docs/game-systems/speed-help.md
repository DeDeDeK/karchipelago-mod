# Speed Help

Speed Help is the Air Ride rules menu's catch-up option (off, weak, strong). While it is on, every
player who trails the race leader is made faster and more forgiving of hits, in steps set by how
far behind they are, and CPU riders get extra per-level bonuses - including a pull toward the
best-placed human. It is the game's only race rubber band, so a mod that wants fairer or harder
CPUs, or a different catch-up curve, tunes it here.

## Race gaps

Two passes in `Game_Think` keep the inputs current every frame of a race:

- `AirRide_UpdateLapProgress` (0x80010478) reads each player's course position
  (`Ply_GetCourseProgress`, 0x8022ce94), stores the lap fraction (0-1) in
  `GameData.player_lap_fraction` (+0x890) and counts laps through `AirRide_OnLapComplete`.
- `AirRide_UpdateRaceGaps` (0x8001544c) fills two `float[5]` arrays from `player_rank` (+0x848,
  the live placement, 0 = 1st) and `player_race_distance` (+0xa4c, metres along the course):
  `player_leader_gap` (+0xa60), the leader's distance minus this player's (0 for the leader), and
  `cpu_human_gap` (+0xa74), how far a CPU trails the best-placed human (0 for humans and for CPUs
  ahead of every human).

## The pass

The rule lives in `GameData.speed_help` (+0xaa5 bits 0x06; 0 off, 1 weak, 2 strong), read by
`Gm_GetAirRideSpeedHelp` (0x8000a1d8). `AirRide_TickSpeedHelp` (0x800159ac) runs
`AirRide_ApplySpeedHelp` (0x800155d4) every `speed_help_interval` frames (60) while it is set. At
scene load `GameData.speed_help_gaps` (+0xabc) is pointed at the weak or strong threshold set.

For each player with a machine, the pass:

1. Finds the player's **band** (0-4): how many of the four gap thresholds its leader gap exceeds.
2. Applies the band's tunables: `MachineGObj_SetHitSpeedLossScale` (how much speed a hit takes
   off), `RiderGObj_SetHitStunScale` (how long a hit stuns), and `MachineGObj_SetKOBoost` (the
   frames and acceleration of the KO boost, the burst `Ply_StartKOBoost` gives for defeating an
   enemy; 3 frames at 0.15 without Speed Help).
3. Computes the **help** value. A player ranked behind the leader starts from its band plus its
   position between the band's two thresholds (0 to 4, smoothly), the leader from 0. A CPU behind
   whose machine is not flagged (`MachineData+0xc32` bit 0x02) adds the same reading of its
   `cpu_human_gap` over the catch-up bands, times `cpu_catchup_scale[level]`, where `level` is
   `Ply_GetCpuLevel`, the CSS level. Every player adds a per-player value at `GameData+0xaa8`; a CPU
   adds `cpu_speed_bonus[level]`, and in a lap or timed race `cpu_mid_race_bonus[level]` during
   the middle third of the race (the laps or frames from `GameData.cpu_mid_race_start` to
   `cpu_mid_race_end`, +0xac0 / +0xac4).
4. Writes the help value to the boost and top-speed entries of the player's `stat_aux` through
   `Ply_SetStatAux`, the same path stat patches use.

The CPU terms ride on the same pass, so with Speed Help off CPUs get none of them.

## Tables

All in `gmGameParams` (`gmDataAll->game_params`). Gaps are in metres.

| Field | Offset | Values |
|---|---|---|
| `speed_help_interval` | +0x20 | 60 frames |
| `speed_help_weak` | +0x24 | 114, 238, 369, 503 |
| `speed_help_strong` | +0x34 | 44, 85, 129, 168 |
| `speed_help_hit_speed_loss` | +0x44 | 1.2, 1, 0.75, 0.5, 0.2 (by band) |
| `speed_help_hit_stun` | +0x58 | 1.2, 1, 0.75, 0.5, 0.2 |
| `speed_help_ko_boost_frames` | +0x6c | 4, 6, 8, 10, 12 |
| `speed_help_ko_boost_accel` | +0x84 | 0.0324 in every band |
| `cpu_speed_bonus` | +0xd8 | -0.3, 0, 0.1, 0.35, 0.6, 0.9, 1.2, 1.6, 1.9 (by level 0-8) |
| `cpu_catchup_gaps` | +0xfc | 80, 160, 240, 320 |
| `cpu_catchup_scale` | +0x10c | 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.8, 1.0, 1.2 |
| `cpu_mid_race_bonus` | +0x130 | 0.1, 0.2, 0.3, 0.4, 0.6, 0.7, 0.8, 0.9, 1.0 |

Band 0 - the leader and anyone within the first threshold of it - is a handicap rather than help:
its hits cost 1.2 times the speed and stun.

## Notes for modders

- Editing the tables reshapes Speed Help for every race. Nothing but `AirRide_ApplySpeedHelp`
  reads the three CPU tables, so scaling them scales exactly the CPU-only terms, and zeroing them
  leaves humans' help untouched and removes the CPU rubber band.
- `AirRide_ApplySpeedHelp` overwrites the boost and top-speed `stat_aux` entries each pass, so
  with Speed Help on anything else written there lasts only until the next pass.
- Hooking `AirRide_TickSpeedHelp` controls the cadence; skipping it freezes each player's
  last-applied values.
