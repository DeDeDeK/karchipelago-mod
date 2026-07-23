#ifndef ARCHIPELAGO_AP_CHECK_DETECT_H
#define ARCHIPELAGO_AP_CHECK_DETECT_H

// clear_kind of every Archipelago checklist objective. The numbering is a
// cross-repo wire contract - the AP location code is 361 + clear_kind - so an
// entry may only be renumbered together with APLocation in the apworld.
typedef enum APCheckKind
{
    APCK_BOOT,              // 0
    APCK_RECEIVE_ITEM,      // 1
    APCK_RECEIVE_5_ITEMS,   // 2

    APCK_CASTLE_FLOWER,     // 3
    APCK_BREAK_ALL_CORAL,   // 4
    APCK_OUT_OF_BOUNDS,     // 5

    APCK_HP_PATCHES_10,     // 6
    APCK_ALLUPS_10,         // 7

    APCK_FOOD_ICECREAM,     // 8
    APCK_FOOD_RICEBALL,     // 9
    APCK_FOOD_CHICKEN,      // 10
    APCK_FOOD_CURRY,        // 11
    APCK_FOOD_RAMEN,        // 12
    APCK_FOOD_OMELET,       // 13
    APCK_FOOD_HAMBURGER,    // 14
    APCK_FOOD_APPLE,        // 15

    APCK_SR1_FIRST,         // 16, SR2..SR9 follow in StadiumKind order
    APCK_SR9_FIRST = APCK_SR1_FIRST + 8, // 24

    APCK_HIGHJUMP_1500,     // 25
    APCK_AIRGLIDER_2000,    // 26
    APCK_MELEE1_100,        // 27
    APCK_MELEE2_60,         // 28

    APCK_SR1_BULK,          // 29
    APCK_SR1_PURPLE_3X,     // 30

    APCK_DRAG1_PHOTO,       // 31, DRAG2..DRAG4 follow in StadiumKind order
    APCK_DRAG4_PHOTO = APCK_DRAG1_PHOTO + 3, // 34
    APCK_AIRRIDE_PHOTO,     // 35

    APCK_NUM,
} APCheckKind;

// Has this objective been achieved? The AP checklist's predicates are one call
// to this each; it reads only latched state, so it is safe to poll every frame
// in any scene.
int APCheckDetect_IsSet(int ck);

// Attaches the per-frame sampler to every human rider (City Trial Trial rounds
// only) and rebaselines the per-run counters.
void APCheckDetect_On3DLoadEnd(void);

// Samples the stadium results block, which Stadium_ExitMinor finishes latching
// immediately before this hook site.
void APCheckDetect_On3DExit(void);

#endif // ARCHIPELAGO_AP_CHECK_DETECT_H
