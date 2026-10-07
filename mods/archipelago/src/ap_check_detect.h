#ifndef ARCHIPELAGO_AP_CHECK_DETECT_H
#define ARCHIPELAGO_AP_CHECK_DETECT_H

// clear_kind of each Archipelago objective. AP location code = 361 + clear_kind, so
// renumbering changes the apworld's APLocation too.
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

    APCK_DRAG_PHOTO,        // 27
    APCK_AIRRIDE_PHOTO,     // 28

    APCK_AIRRIDE_ALL_COLORS, // 29
    APCK_MODEL_CITY,         // 30
    APCK_VOLCANO_FLOWER,     // 31

    APCK_SKY_GARDEN_TOP,     // 32
    APCK_MAX_ALTITUDE,       // 33

    APCK_AIRRIDE_1ST_METAKNIGHT, // 34
    APCK_AIRRIDE_1ST_DEDEDE,     // 35

    APCK_NEBULA_1ST,          // 36
    APCK_NEBULA_DIST_2MIN,    // 37
    APCK_NEBULA_2LAP_TIME,    // 38
    APCK_NEBULA_1ST_SCOOTER,  // 39
    APCK_NEBULA_AIRBORNE,     // 40

    APCK_DD3_KO_10,           // 41
    APCK_DD_DEDEDE_KO_KIRBY,  // 42

    APCK_MIC_COPY_CHANCE,     // 43
    APCK_MIC_ENEMY_KOS,       // 44

    APCK_BOX_BLUE_20,         // 45
    APCK_BOX_GREEN_10,        // 46
    APCK_BOX_RED_10,          // 47

    APCK_MEADOWS_SHORTCUT,    // 48

    APCK_ASSEMBLE_AP_STAR,    // 49

    APCK_ASSEMBLE_ALL_LEGENDARY, // 50

    APCK_OFFENSE_PATCHES_10,     // 51

    APCK_NEBULA_2LAP_FAST,       // 52
    APCK_NEBULA_TA,              // 53
    APCK_NEBULA_TA_FAST,         // 54
    APCK_NEBULA_TA_HYDRA,        // 55
    APCK_NEBULA_FR,              // 56
    APCK_NEBULA_FR_FAST,         // 57
    APCK_NEBULA_FR_WARP,         // 58

    APCK_VALLEY_FR_AP_STAR,      // 59
    APCK_CHECKER_TA_FLIGHT,      // 60
    APCK_PASSAGE_FR_COMPACT,     // 61
    APCK_FROZEN_TA_WHEELIE,      // 62
    APCK_SANDS_TA_FLIGHT,        // 63

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

    APCK_SAME_COPY_3X,           // 74

    APCK_BUST_REX_ON_SCOOTER,    // 75
    APCK_BUST_WINGED_ON_FLIGHT,  // 76
    APCK_BUST_SHADOW_ON_AP_STAR, // 77

    APCK_MAGMA_TA_METAKNIGHT,    // 78
    APCK_AIRRIDE_1ST_AP_STAR,    // 79
    APCK_MELEE_DEDEDE_KO_30,     // 80
    APCK_VSKD_METAKNIGHT_KO,     // 81

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

    APCK_AP_STAR_SHOT_KO_CPU,    // 97

    APCK_RIDE_5_MACHINES,        // 98
    APCK_AIRBORNE_20S,           // 99
    APCK_EVENT_DYNABLADE_ITEMS,  // 100
    APCK_PANIC_SPIN_KO,          // 101
    APCK_WHISPY_WOODS,           // 102
    APCK_LIGHTHOUSE_TOP,         // 103
    APCK_UNDER_WATERWHEEL,       // 104
    APCK_ALL_GRIND_RAILS,        // 105
    APCK_COPY_6_KINDS,           // 106
    APCK_EVENT_METEOR_NO_DAMAGE, // 107
    APCK_MAX_A_STAT,             // 108

    APCK_DD_KO_5_UNHURT,         // 109
    APCK_VSKD_NO_DAMAGE,         // 110
    APCK_DRAG_ALL_1ST,           // 111

    APCK_AIRRIDE_ALL_COURSES_1ST, // 112
    APCK_BEANSTALK_FERRIS_LAPS,   // 113
    APCK_SANDS_NO_QUICKSAND,      // 114
    APCK_CHECKER_NO_SPIN_PANELS,  // 115
    APCK_MAGMA_NO_BOOST_PANELS,   // 116

    APCK_TR_WATER_NO_FALLS,      // 117
    APCK_TR_LIGHT_NO_RAIL,       // 118
    APCK_TR_EVERY_ITEM,          // 119
    APCK_NUM,
} APCheckKind;

// Reads only latched state, so it is safe to poll every frame.
int APCheckDetect_IsSet(int ck);

// Latches an objective for the rest of the boot. Idempotent.
void APCheckDetect_Observe(int ck);

// Cross-boot progress counters, a debug override of one, and a log of them all.
int APCheckDetect_GetProgress(APCheckProgressKind which);
void APCheckDetect_DebugSetProgress(APCheckProgressKind which, int value);
void APCheckDetect_ReportProgress(void);

// Debug: forget every latch and cross-boot counter.
void APCheckDetect_ResetProgress(void);

// custom_machines' KO callback, for every KO through Machine_GiveDamage's bl
// Ply_AddDeath.
void APCheckDetect_AddDeath(int victim, struct DmgLog *dmg_log, int machine_kind);

// Marks a human KO'd this round by a path the KO callback misses.
void APCheckDetect_OnKnockedOut(int ply);

void APCheckDetect_OnBoot(void);

// Resets per-round state and attaches the loaded mode's per-rider sampler.
void APCheckDetect_On3DLoadEnd(void);

// Top Ride loads through its own minor, which never reaches On3DLoadEnd.
void APCheckDetect_OnTopRideLoadEnd(void);

// Stadium_ExitMinor has latched the stadium results block by this point.
void APCheckDetect_On3DExit(void);

#endif // ARCHIPELAGO_AP_CHECK_DETECT_H
