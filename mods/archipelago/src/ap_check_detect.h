#ifndef ARCHIPELAGO_AP_CHECK_DETECT_H
#define ARCHIPELAGO_AP_CHECK_DETECT_H

// clear_kind of every Archipelago checklist objective. The numbering is a cross-repo
// wire contract - the AP location code is 361 + clear_kind - so an entry may only be
// renumbered together with APLocation in the apworld.
typedef enum APCheckKind
{
    APCK_CASTLE_FLOWER,     // 0
    APCK_BREAK_ALL_CORAL,   // 1
    APCK_OUT_OF_BOUNDS,     // 2

    APCK_HP_PATCHES_10,     // 3
    APCK_ALLUPS_5,          // 4

    APCK_FOOD_ICECREAM,     // 5
    APCK_FOOD_RICEBALL,     // 6
    APCK_FOOD_CHICKEN,      // 7
    APCK_FOOD_CURRY,        // 8
    APCK_FOOD_RAMEN,        // 9
    APCK_FOOD_OMELET,       // 10
    APCK_FOOD_HAMBURGER,    // 11
    APCK_FOOD_APPLE,        // 12

    APCK_SR1_FIRST,         // 13, SR2..SR9 follow in StadiumKind order
    APCK_SR9_FIRST = APCK_SR1_FIRST + 8, // 21

    APCK_HIGHJUMP_1500,     // 22
    APCK_AIRGLIDER_2000,    // 23
    APCK_MELEE1_100,        // 24
    APCK_MELEE2_60,         // 25

    APCK_SR1_BULK,          // 26
    APCK_SR1_PURPLE_3X,     // 27

    // Either photo finish counts on any course/stadium of its mode.
    APCK_DRAG_PHOTO,        // 28
    APCK_AIRRIDE_PHOTO,     // 29

    APCK_AIRRIDE_ALL_COLORS, // 30
    APCK_MODEL_CITY,         // 31
    APCK_VOLCANO_FLOWER,     // 32

    APCK_SKY_GARDEN_TOP,     // 33
    APCK_MAX_ALTITUDE,       // 34

    // Any Air Ride course.
    APCK_AIRRIDE_1ST_METAKNIGHT, // 35
    APCK_AIRRIDE_1ST_DEDEDE,     // 36

    // Nebula Belt only. Vanilla ships no cell for the course at all.
    APCK_NEBULA_1ST,          // 37
    APCK_NEBULA_DIST_2MIN,    // 38
    APCK_NEBULA_2LAP_TIME,    // 39
    APCK_NEBULA_1ST_SCOOTER,  // 40
    APCK_NEBULA_AIRBORNE,     // 41

    // Vanilla gives every other Destruction Derby stadium a 10-KO cell alongside its
    // 5-KO one; 3 has only the 5-KO one. The second is any DD stadium and counts
    // KO'd Kirbys specifically.
    APCK_DD3_KO_10,           // 42
    APCK_DD_DEDEDE_KO_KIRBY,  // 43

    // Mic is the one CopyKind with no vanilla cell of any kind. The first mirrors
    // the Copy Chance Wheel cells vanilla gives Bomb and Sleep; the second mirrors
    // the Tornado challenge, which is vanilla's only "KO with an ability" cell.
    APCK_MIC_COPY_CHANCE,     // 44
    APCK_MIC_ENEMY_KOS,       // 45

    // Vanilla counts boxes only as an all-colors lifetime total (500 / 1000), so a
    // per-color count is open. The thresholds are not equal because the colors are
    // not: GrCity1 spawns blue 63% of the time against red 20% and green 17%.
    APCK_BOX_BLUE_20,         // 46
    APCK_BOX_GREEN_10,        // 47
    APCK_BOX_RED_10,          // 48

    // The one course feature vanilla writes no cell about.
    APCK_MEADOWS_SHORTCUT,    // 49

    APCK_NUM,
} APCheckKind;

// Has this objective been achieved? Reads only latched state, so it is safe to
// poll every frame in any scene.
int APCheckDetect_IsSet(int ck);

// Installs the two KO recorder interceptions - the rival one the Destruction Derby
// objective needs, and the enemy one the Mic objective needs.
void APCheckDetect_OnBoot(void);

// Attaches the per-frame sampler to every human rider - the City Trial one for a
// Trial round, the shortcut one on Fantasy Meadows - and rebaselines the per-run
// counters.
void APCheckDetect_On3DLoadEnd(void);

// Samples the stadium results block, which Stadium_ExitMinor finishes latching
// immediately before this hook site.
void APCheckDetect_On3DExit(void);

#endif // ARCHIPELAGO_AP_CHECK_DETECT_H
