#include "game.h"
#include "os.h"
#include "hoshi/mod.h"

#include "main.h"
#include "ap_checklist.h"
#include "ap_check_detect.h"
#include "custom_checklist_api.h"

// Imported custom_checklist API. Resolved in APChecklist_Register rather than at
// OnBoot, since the framework mod boots after us (alphabetical order).
static const CustomChecklistAPI *cc_api = NULL;

// Every cell answers with the same predicate - a pure read of latched state, keyed by the
// row's own clear_kind. All sampling happens in the detection hooks.
//
// clear_kind order is the wire contract with APLocation in the apworld (the AP location
// code is 361 + clear_kind), and each label is its location's name minus the leading
// "Archipelago: ", which the apworld restates by hand. The framework accepts glyphs only
// while under byte 157 (2 bytes per character, 1 per space or break) and silently
// truncates the rest; the longest label here spends 137 including its terminator. Each
// "\n" is a placed line break, kept under vanilla's widest rendered line of 37 characters
// - the framework's fallback split balances on width and would part a stadium name from
// its number.
static const CustomCheck ap_checks[] = {
    { APCK_CASTLE_FLOWER,   "City Trial: Visit the flower\non top of Castle Hall on foot!", APCheckDetect_IsSet },
    { APCK_BREAK_ALL_CORAL, "City Trial: Break all\nthe coral in one game!", APCheckDetect_IsSet },
    { APCK_OUT_OF_BOUNDS,   "City Trial: Go out of bounds!",                APCheckDetect_IsSet },

    { APCK_HP_PATCHES_10,   "City Trial: In one game,\nget 10 or more HP Patches!",  APCheckDetect_IsSet },
    { APCK_ALLUPS_5,        "City Trial: Collect\n5 All Ups in total!",     APCheckDetect_IsSet },

    { APCK_FOOD_ICECREAM,   "City Trial: In one game,\neat 3 or more Ice Creams!",   APCheckDetect_IsSet },
    { APCK_FOOD_RICEBALL,   "City Trial: In one game,\neat 3 or more Rice Balls!",   APCheckDetect_IsSet },
    { APCK_FOOD_CHICKEN,    "City Trial: In one game,\neat 3 or more Chickens!",     APCheckDetect_IsSet },
    { APCK_FOOD_CURRY,      "City Trial: In one game, eat\n3 or more plates of Curry!", APCheckDetect_IsSet },
    { APCK_FOOD_RAMEN,      "City Trial: In one game, eat\n3 or more bowls of Ramen!", APCheckDetect_IsSet },
    { APCK_FOOD_OMELET,     "City Trial: In one game,\neat 3 or more Omelets!",      APCheckDetect_IsSet },
    { APCK_FOOD_HAMBURGER,  "City Trial: In one game,\neat 3 or more Hamburgers!",   APCheckDetect_IsSet },
    { APCK_FOOD_APPLE,      "City Trial: In one game,\neat 3 or more Apples!",       APCheckDetect_IsSet },

    { APCK_SR1_FIRST + 0,   "Stadium: SINGLE RACE 1\nFinish in 1st place!", APCheckDetect_IsSet },
    { APCK_SR1_FIRST + 1,   "Stadium: SINGLE RACE 2\nFinish in 1st place!", APCheckDetect_IsSet },
    { APCK_SR1_FIRST + 2,   "Stadium: SINGLE RACE 3\nFinish in 1st place!", APCheckDetect_IsSet },
    { APCK_SR1_FIRST + 3,   "Stadium: SINGLE RACE 4\nFinish in 1st place!", APCheckDetect_IsSet },
    { APCK_SR1_FIRST + 4,   "Stadium: SINGLE RACE 5\nFinish in 1st place!", APCheckDetect_IsSet },
    { APCK_SR1_FIRST + 5,   "Stadium: SINGLE RACE 6\nFinish in 1st place!", APCheckDetect_IsSet },
    { APCK_SR1_FIRST + 6,   "Stadium: SINGLE RACE 7\nFinish in 1st place!", APCheckDetect_IsSet },
    { APCK_SR1_FIRST + 7,   "Stadium: SINGLE RACE 8\nFinish in 1st place!", APCheckDetect_IsSet },
    { APCK_SR1_FIRST + 8,   "Stadium: SINGLE RACE 9\nFinish in 1st place!", APCheckDetect_IsSet },

    { APCK_HIGHJUMP_1500,   "Stadium: HIGH JUMP\nJump higher than 1,500 feet!", APCheckDetect_IsSet },
    { APCK_AIRGLIDER_2000,  "Stadium: AIR GLIDER\nfly more than 2,000 feet!",   APCheckDetect_IsSet },
    { APCK_MELEE1_100,      "Stadium: KIRBY MELEE 1 In one game,\nKO over 100 enemies by yourself!", APCheckDetect_IsSet },
    { APCK_MELEE2_60,       "Stadium: KIRBY MELEE 2 In one game,\nKO over 60 enemies by yourself!",  APCheckDetect_IsSet },

    { APCK_SR1_BULK,        "Stadium: SINGLE RACE 1\nFinish in 1st place on Bulk Star!", APCheckDetect_IsSet },
    { APCK_SR1_PURPLE_3X,   "Stadium: SINGLE RACE 1 Finish in\n1st place 3 times as Purple Kirby!", APCheckDetect_IsSet },

    { APCK_DRAG_PHOTO,      "Stadium: In any DRAG RACE, have 2\nplayers finish within 0.10 seconds!", APCheckDetect_IsSet },
    { APCK_AIRRIDE_PHOTO,   "Air Ride: On any course, have 2\nplayers finish within 0.10 seconds!",   APCheckDetect_IsSet },

    { APCK_AIRRIDE_ALL_COLORS, "Air Ride: Finish a race\nas every Kirby color!",      APCheckDetect_IsSet },
    { APCK_MODEL_CITY,         "City Trial: Visit the\nmodel city on foot!",         APCheckDetect_IsSet },
    { APCK_VOLCANO_FLOWER,     "City Trial: Visit the flower on top\nof the volcanic cliffs on foot!", APCheckDetect_IsSet },
    { APCK_SKY_GARDEN_TOP,     "City Trial: Visit the top of\nthe garden in the sky on foot!", APCheckDetect_IsSet },
    { APCK_MAX_ALTITUDE,       "City Trial: Fly to the\nhighest point possible!",    APCheckDetect_IsSet },

    { APCK_AIRRIDE_1ST_METAKNIGHT, "Air Ride: Finish in 1st place\nas Meta Knight!", APCheckDetect_IsSet },
    { APCK_AIRRIDE_1ST_DEDEDE,     "Air Ride: Finish in 1st place\nas King Dedede!", APCheckDetect_IsSet },

    // Vanilla ships no cell for Nebula Belt at all, so these have no shape to match.
    // The course has no enemies, breakables, rails or animated props, which is why
    // they are all about racing and flying it.
    { APCK_NEBULA_1ST,         "Air Ride: NEBULA BELT\nFinish in 1st place!",        APCheckDetect_IsSet },
    { APCK_NEBULA_DIST_2MIN,   "Air Ride: NEBULA BELT\nRace over 5,500 feet in 2 minutes!", APCheckDetect_IsSet },
    { APCK_NEBULA_2LAP_TIME,   "Air Ride: NEBULA BELT\nFinish 2 laps in under 02:30:00!",   APCheckDetect_IsSet },
    { APCK_NEBULA_1ST_SCOOTER, "Air Ride: NEBULA BELT Finish in\n1st place on Wheelie Scooter!", APCheckDetect_IsSet },
    { APCK_NEBULA_AIRBORNE,    "Air Ride: NEBULA BELT Fly 10 seconds\non Dragoon, Flight or Winged Star!", APCheckDetect_IsSet },

    // The first restates the 10-KO cell DD 1/2/4/5 have and 3 does not, verbatim and
    // with vanilla's own line break.
    { APCK_DD3_KO_10,          "Stadium: DESTRUCTION DERBY 3\nIn one game, KO a rival 10 times or more!", APCheckDetect_IsSet },
    { APCK_DD_DEDEDE_KO_KIRBY, "Stadium: DESTRUCTION DERBY (All)\nAs King Dedede, KO 10 Kirbys in one game!", APCheckDetect_IsSet },

    // Mic is the only CopyKind vanilla never writes a cell for. The first restates
    // the Bomb and Sleep Copy Chance cells verbatim, break included; the second
    // takes the "(All)" heading vanilla gives a cell any stadium in a group
    // satisfies, and is scoped to the two melee stadiums.
    { APCK_MIC_COPY_CHANCE,    "City Trial: Get the Mic ability\nfrom the Copy Chance Wheel!", APCheckDetect_IsSet },
    { APCK_MIC_ENEMY_KOS,      "Stadium: KIRBY MELEE (All) In one game,\nKO 10 enemies as Mic Kirby!", APCheckDetect_IsSet },

    // Vanilla's two box cells count every color together over the whole save, so
    // these take the "In one game" shape of its other counting cells instead.
    { APCK_BOX_BLUE_20,        "City Trial: In one game,\nbreak 20 or more blue boxes!",  APCheckDetect_IsSet },
    { APCK_BOX_GREEN_10,       "City Trial: In one game,\nbreak 10 or more green boxes!", APCheckDetect_IsSet },
    { APCK_BOX_RED_10,         "City Trial: In one game,\nbreak 10 or more red boxes!",   APCheckDetect_IsSet },

    { APCK_MEADOWS_SHORTCUT,   "Air Ride: FANTASY MEADOWS\nTake the shortcut!",      APCheckDetect_IsSet },
};

#define AP_CHECK_NUM ((int)(sizeof(ap_checks) / sizeof(ap_checks[0])))

// A missing entry would leave an AP location with no way to complete it, stranding
// any progression item fill places on it.
_Static_assert(AP_CHECK_NUM == APCK_NUM, "ap_checks[] must cover every APCheckKind");

// Already recorded as sent this save? Out-of-range cells report "done".
static int APChecklist_IsRecorded(int clear_kind)
{
    if (clear_kind < 0 || clear_kind >= CLEAR_KIND_NUM)
        return 1;
    return (ap_save->sent_checks[AP_CHECKLIST_ROW][clear_kind >> 6] >> (clear_kind & 63)) & 1ULL;
}

// Record a completed AP check. The ClearChecker_SetNewUnlock REPLACEFUNC in
// check_detection intercepts ap_checklist_mode and sets the AP row's sent_checks
// bit, fires the "Check sent" textbox and re-evaluates goals. The framework seeds
// the cell's is_new/is_visible afterward, so the animation runs on the next entry.
static void APChecklist_RecordComplete(int clear_kind)
{
    ClearChecker_SetNewUnlock((GameMode)ap_checklist_mode, (u8)clear_kind);
}

// The framework's evaluator no-ops until this returns nonzero. game_ready is set at
// the end of OnSaveLoaded, once the textbox API has resolved.
static int APChecklist_IsReady(void)
{
    return ap_data && ap_data->game_ready;
}

// Tab art: an HSD archive staged to the FST root, exporting the banner watermark
// and tab-emblem image descriptors.
#define AP_TEX_FILE      "ApChecklistTex"
#define AP_BANNER_SYMBOL "apBannerImg"
#define AP_EMBLEM_SYMBOL "apEmblemImg"

static const CustomChecklistDesc ap_desc = {
    .name = AP_CHECKLIST_NAME,
    .theme_r = AP_THEME_R,
    .theme_g = AP_THEME_G,
    .theme_b = AP_THEME_B,
    .tex_file = AP_TEX_FILE,
    .banner_symbol = AP_BANNER_SYMBOL,
    .emblem_symbol = AP_EMBLEM_SYMBOL,
    .checks = ap_checks,
    .check_num = AP_CHECK_NUM,
    .is_recorded = APChecklist_IsRecorded,
    .record_complete = APChecklist_RecordComplete,
    .is_ready = APChecklist_IsReady,
};

void APChecklist_RevealAll(void)
{
    if (!cc_api)
        return;

    // Through the framework rather than by writing is_visible here: it latches the tab
    // open for the session, so a reveal that lands before the grid shuffle survives it.
    cc_api->RevealAll(ap_checklist_mode);
}

void APChecklist_Register(void)
{
    static int registered = 0;
    if (registered)
        return;
    registered = 1;

    cc_api = (const CustomChecklistAPI *)Hoshi_ImportMod(
        (char *)CUSTOM_CHECKLIST_MOD_NAME, CUSTOM_CHECKLIST_API_MAJOR, CUSTOM_CHECKLIST_API_MINOR);
    if (!cc_api)
    {
        OSReport("[APChecklist] custom_checklist API not available - AP tab disabled\n");
        return;
    }

    int mode = cc_api->Register(&ap_desc);
    if (mode < 0)
    {
        OSReport("[APChecklist] Registration failed\n");
        return;
    }
    // The framework appends to the next free slot; ChecklistModeRow maps whatever it
    // assigned to the fixed AP_CHECKLIST_ROW, so registration order does not matter.
    ap_checklist_mode = mode;

    OSReport("[APChecklist] Registered AP tab (mode %d, %d custom checks)\n", mode, AP_CHECK_NUM);
}
