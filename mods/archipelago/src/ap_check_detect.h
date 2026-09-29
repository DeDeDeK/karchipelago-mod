#ifndef ARCHIPELAGO_AP_CHECK_DETECT_H
#define ARCHIPELAGO_AP_CHECK_DETECT_H

// clear_kind of every Archipelago checklist objective. The numbering is a cross-repo
// wire contract - the AP location code is 361 + clear_kind - so an entry may only be
// renumbered together with APLocation in the apworld.
typedef enum APCheckKind
{
    APCK_CASTLE_FLOWER,     // 0
    APCK_BREAK_ALL_CORAL,   // 1

    APCK_HP_PATCHES_10,     // 2
    APCK_ALLUPS_5,          // 3

    APCK_FOOD_ICECREAM,     // 4
    APCK_FOOD_RICEBALL,     // 5
    APCK_FOOD_CHICKEN,      // 6
    APCK_FOOD_CURRY,        // 7
    APCK_FOOD_RAMEN,        // 8
    APCK_FOOD_OMELET,       // 9
    APCK_FOOD_HAMBURGER,    // 10
    APCK_FOOD_APPLE,        // 11

    APCK_SR1_FIRST,         // 12, SR2..SR9 follow in StadiumKind order
    APCK_SR9_FIRST = APCK_SR1_FIRST + 8, // 20

    APCK_HIGHJUMP_1500,     // 21
    APCK_AIRGLIDER_2000,    // 22
    APCK_MELEE1_100,        // 23
    APCK_MELEE2_60,         // 24

    APCK_SR1_BULK,          // 25
    APCK_SR1_PURPLE_3X,     // 26

    // Either photo finish counts on any course/stadium of its mode.
    APCK_DRAG_PHOTO,        // 27
    APCK_AIRRIDE_PHOTO,     // 28

    APCK_AIRRIDE_ALL_COLORS, // 29
    APCK_MODEL_CITY,         // 30
    APCK_VOLCANO_FLOWER,     // 31

    APCK_SKY_GARDEN_TOP,     // 32
    APCK_MAX_ALTITUDE,       // 33

    // Any Air Ride course.
    APCK_AIRRIDE_1ST_METAKNIGHT, // 34
    APCK_AIRRIDE_1ST_DEDEDE,     // 35

    // Nebula Belt only. Vanilla ships no cell for the course at all.
    APCK_NEBULA_1ST,          // 36
    APCK_NEBULA_DIST_2MIN,    // 37
    APCK_NEBULA_2LAP_TIME,    // 38
    APCK_NEBULA_1ST_SCOOTER,  // 39
    APCK_NEBULA_AIRBORNE,     // 40

    // Vanilla gives every other Destruction Derby stadium a 10-KO cell alongside its
    // 5-KO one; 3 has only the 5-KO one. The second is any DD stadium and counts
    // KO'd Kirbys specifically.
    APCK_DD3_KO_10,           // 41
    APCK_DD_DEDEDE_KO_KIRBY,  // 42

    // Mic is the one CopyKind with no vanilla cell of any kind. The first mirrors
    // the Copy Chance Wheel cells vanilla gives Bomb and Sleep; the second mirrors
    // the Tornado challenge, which is vanilla's only "KO with an ability" cell.
    APCK_MIC_COPY_CHANCE,     // 43
    APCK_MIC_ENEMY_KOS,       // 44

    // Vanilla counts boxes only as an all-colors lifetime total (500 / 1000), so a
    // per-color count is open. The thresholds are not equal because the colors are
    // not: GrCity1 spawns blue 63% of the time against red 20% and green 17%.
    APCK_BOX_BLUE_20,         // 45
    APCK_BOX_GREEN_10,        // 46
    APCK_BOX_RED_10,          // 47

    // The one course feature vanilla writes no cell about.
    APCK_MEADOWS_SHORTCUT,    // 48

    // Collect all six Archipelago spheres in one City Trial round. Needs all six
    // sphere items, which are what put the spheres in the pool.
    APCK_ASSEMBLE_AP_STAR,    // 49

    // Assemble Dragoon, Hydra and the Archipelago Star in one City Trial round -
    // the three-machine version of vanilla's City Trial cell 0x77.
    APCK_ASSEMBLE_ALL_LEGENDARY, // 50

    // Vanilla counts ten of every stat patch but HP and Offense. APCK_HP_PATCHES_10
    // covers the first; this covers the last one with no cell anywhere.
    APCK_OFFENSE_PATCHES_10,     // 51

    // The Nebula Belt mode cells every other course has: the faster race lap tier,
    // and Time Attack and Free Run's two open tiers plus one machine tier each.
    APCK_NEBULA_2LAP_FAST,       // 52
    APCK_NEBULA_TA,              // 53
    APCK_NEBULA_TA_FAST,         // 54
    APCK_NEBULA_TA_HYDRA,        // 55
    APCK_NEBULA_FR,              // 56
    APCK_NEBULA_FR_FAST,         // 57
    APCK_NEBULA_FR_WARP,         // 58

    // Machine tiers for the machines no vanilla Air Ride cell names.
    APCK_VALLEY_FR_AP_STAR,      // 59
    APCK_CHECKER_TA_FLIGHT,      // 60
    APCK_PASSAGE_FR_COMPACT,     // 61
    APCK_FROZEN_TA_WHEELIE,      // 62
    APCK_SANDS_TA_FLIGHT,        // 63

    // One per City Trial event vanilla writes no cell about, each earned while that
    // event is running.
    APCK_EVENT_RUNAMOK_DIST,     // 64
    APCK_EVENT_RAILFIRE_ALL,     // 65
    APCK_EVENT_SAMEITEM_20,      // 66
    APCK_EVENT_LIGHTHOUSE_BOTH,  // 67
    APCK_EVENT_PREDICTION_WRONG, // 68
    APCK_EVENT_UFO_ALLUP,        // 69
    APCK_EVENT_FORMATION_BUMP,   // 70
    APCK_EVENT_BOUNCE_ITEMS,     // 71
    APCK_EVENT_FOG_KO,           // 72
    APCK_EVENT_FAKE_NONE,        // 73

    // One copy ability three grants running, from any mix of sources.
    APCK_SAME_COPY_3X,           // 74

    // Bust one machine while riding another in the city, the shape of vanilla's eight
    // bust cells, for machines none of them names.
    APCK_BUST_REX_ON_SCOOTER,    // 75
    APCK_BUST_WINGED_ON_FLIGHT,  // 76
    APCK_BUST_SHADOW_ON_AP_STAR, // 77

    // The two characters and the Archipelago Star, where no cell names them yet.
    APCK_MAGMA_TA_METAKNIGHT,    // 78
    APCK_AIRRIDE_1ST_AP_STAR,    // 79
    APCK_MELEE_DEDEDE_KO_30,     // 80
    APCK_VSKD_METAKNIGHT_KO,     // 81

    // Top Ride. Vanilla's 120 cells never name a machine or a Kirby color, only Fire,
    // Buzz Saw and Bomb hit a rival, and only SKY and METAL ask a course hazard to be
    // avoided.
    APCK_TR_FREEZE_FAN_3,        // 82
    APCK_TR_FIRE_NO_BURN,        // 83
    APCK_TR_SAND_NO_ANTDOOM,     // 84
    APCK_TR_1ST_VS_3_LV5,        // 85
    APCK_TR_PHOTO,               // 86
    APCK_TR_ALL_COLORS,          // 87
    APCK_TR_TA_GRASS_STEER,      // 88
    APCK_TR_TA_METAL_STEER,      // 89
    APCK_TR_FR_SKY_STEER,        // 90
    APCK_TR_ALL_COURSES_STEER,   // 91
    APCK_TR_NO_HIT,              // 92
    APCK_TR_HIT_5_WIN,           // 93
    APCK_TR_ABILITY_ITEMS,       // 94
    APCK_TR_SPEEDDOWN_3_WIN,     // 95
    APCK_TR_TA_EVEN_LAPS,        // 96

    // The Archipelago Star's charge-release shot lands the killing hit on a CPU, in
    // the city.
    APCK_AP_STAR_SHOT_KO_CPU,    // 97

    APCK_NUM,
} APCheckKind;

// Has this objective been achieved? Reads only latched state, so it is safe to
// poll every frame in any scene.
int APCheckDetect_IsSet(int ck);

// Latch an objective, here or elsewhere in the mod. Idempotent.
void APCheckDetect_Observe(int ck);

// The cross-session progress counters, and a debug override of one. Setting a
// counter to one below its target lets the next real event complete the check.
int APCheckDetect_GetProgress(APCheckProgressKind which);
void APCheckDetect_DebugSetProgress(APCheckProgressKind which, int value);

// Handed to custom_machines' KO seam once the registry resolves. Counts the Kirbys
// a human King Dedede has KO'd in Destruction Derby, and latches the bust, sphere
// shot and VS. King Dedede objectives.
void APCheckDetect_AddDeath(int victim, struct DmgLog *dmg_log, int machine_kind);

// Installs the enemy-defeat and yakumono-break interceptions the Mic and coral
// objectives read from, the Time Attack / Free Run dispatch wrappers, the City Trial
// event seams, the machine projectile-hit hook and the Top Ride per-kirby sampler.
// The rival KO arrives through custom_machines instead.
void APCheckDetect_OnBoot(void);

// Attaches the per-frame sampler to every human rider - the City Trial one for a
// Trial round, the shortcut one on Fantasy Meadows - and rebaselines the per-run
// counters.
void APCheckDetect_On3DLoadEnd(void);

// Clears the per-round Top Ride counters. Top Ride loads through its own minor, so
// On3DLoadEnd never runs for it.
void APCheckDetect_OnTopRideLoadEnd(void);

// Clears the frame's sphere-shot hits and polls the three-legendary objective. The
// poll is not part of the per-rider sampler because assembly ends in
// Rider_RespawnFullRecreate, which tears the rider's machine down under it.
void APCheckDetect_OnFrameStart(void);

// Samples the stadium results block, which Stadium_ExitMinor finishes latching
// immediately before this hook site.
void APCheckDetect_On3DExit(void);

#endif // ARCHIPELAGO_AP_CHECK_DETECT_H
