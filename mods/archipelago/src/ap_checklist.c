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
// truncates the rest; the longest label here spends 138 including its terminator. Each
// "\n" is a placed line break, kept under vanilla's widest rendered line of 37 characters
// - the framework's fallback split balances on width and would part a stadium name from
// its number.
static const CustomCheck ap_checks[] = {
    { APCK_CASTLE_FLOWER,   "City Trial: Visit the flower\non top of Castle Hall on foot!", APCheckDetect_IsSet },
    { APCK_BREAK_ALL_CORAL, "City Trial: Break all\nthe coral in one game!", APCheckDetect_IsSet },

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

    { APCK_ASSEMBLE_AP_STAR,   "City Trial: Collect all 6 spheres\nand assemble the Archipelago Star!", APCheckDetect_IsSet },
    { APCK_ASSEMBLE_ALL_LEGENDARY, "City Trial: In one game, assemble\nDragoon, Hydra and Archipelago Star!", APCheckDetect_IsSet },

    // Restates vanilla's per-stat patch cell for the one stat it never counts.
    { APCK_OFFENSE_PATCHES_10, "City Trial: In one game,\nget 10 or more Offense Patches!", APCheckDetect_IsSet },

    // Vanilla's own Time Attack and Free Run wording, prefixes included.
    { APCK_NEBULA_2LAP_FAST,   "Air Ride: NEBULA BELT\nFinish 2 laps in under 02:06:00!",     APCheckDetect_IsSet },
    { APCK_NEBULA_TA,          "Time Attack: NEBULA BELT\nFinish in under 03:35:00!",         APCheckDetect_IsSet },
    { APCK_NEBULA_TA_FAST,     "Time Attack: NEBULA BELT\nFinish in under 03:10:00!",         APCheckDetect_IsSet },
    { APCK_NEBULA_TA_HYDRA,    "Time Attack: NEBULA BELT\nFinish in under 03:15:00 on Hydra!", APCheckDetect_IsSet },
    { APCK_NEBULA_FR,          "Free Run: NEBULA BELT\nFinish 1 lap in under 01:15:00!",      APCheckDetect_IsSet },
    { APCK_NEBULA_FR_FAST,     "Free Run: NEBULA BELT\nFinish 1 lap in under 01:03:00!",      APCheckDetect_IsSet },
    { APCK_NEBULA_FR_WARP,     "Free Run: NEBULA BELT\nDo 1 lap under 01:10:00 on Warpstar!", APCheckDetect_IsSet },

    { APCK_VALLEY_FR_AP_STAR,  "Free Run: CELESTIAL VALLEY Do 1 lap\nunder 01:00:00 on Archipelago Star!", APCheckDetect_IsSet },
    { APCK_CHECKER_TA_FLIGHT,  "Time Attack: CHECKER KNIGHTS Finish\nin under 04:00:00 on Flight Warp Star!", APCheckDetect_IsSet },
    { APCK_PASSAGE_FR_COMPACT, "Free Run: MACHINE PASSAGE Do 1 lap\nunder 01:05:00 on Compact Star!", APCheckDetect_IsSet },
    { APCK_FROZEN_TA_WHEELIE,  "Time Attack: FROZEN HILLSIDE Finish\nin under 03:00:00 on Wheelie Bike!", APCheckDetect_IsSet },
    { APCK_SANDS_TA_FLIGHT,    "Time Attack: SKY SANDS Finish in\nunder 02:50:00 on Flight Warp Star!", APCheckDetect_IsSet },

    // Each worded after the event's own announcement.
    { APCK_EVENT_RUNAMOK_DIST,     "City Trial: While energy tanks run\namok, travel over 1,000 feet!",   APCheckDetect_IsSet },
    { APCK_EVENT_RAILFIRE_ALL,     "City Trial: Catch fire at all 5 rail\nstations while they burn!",     APCheckDetect_IsSet },
    { APCK_EVENT_SAMEITEM_20,      "City Trial: When the boxes all hold\nthe same item, get 20 of it!",   APCheckDetect_IsSet },
    { APCK_EVENT_LIGHTHOUSE_BOTH,  "City Trial: Go under both lights\nof the city lighthouse!",           APCheckDetect_IsSet },
    { APCK_EVENT_PREDICTION_WRONG, "City Trial: Get a Stadium Prediction\nthat turns out wrong!",         APCheckDetect_IsSet },
    { APCK_EVENT_UFO_ALLUP,        "City Trial: Get the All Up\non top of the UFO!",                      APCheckDetect_IsSet },
    { APCK_EVENT_FORMATION_BUMP,   "City Trial: Bump into the Air Ride\nmachine formation!",              APCheckDetect_IsSet },
    { APCK_EVENT_BOUNCE_ITEMS,     "City Trial: While the items bounce,\nget over 10 items!",             APCheckDetect_IsSet },
    { APCK_EVENT_FOG_KO,           "City Trial: KO a rival while\na dense fog covers the city!",          APCheckDetect_IsSet },
    { APCK_EVENT_FAKE_NONE,        "City Trial: Get 5 power-ups while\nsome are fake, and touch no fakes!", APCheckDetect_IsSet },

    { APCK_SAME_COPY_3X,       "City Trial: In one game, get the\nsame copy ability 3 times in a row!", APCheckDetect_IsSet },

    { APCK_BUST_REX_ON_SCOOTER,    "City Trial: In the city, bust Rex\nWheelie while riding Wheelie Scooter!", APCheckDetect_IsSet },
    { APCK_BUST_WINGED_ON_FLIGHT,  "City Trial: In the city, bust Winged\nStar while riding Flight Warp Star!",  APCheckDetect_IsSet },
    { APCK_BUST_SHADOW_ON_AP_STAR, "City Trial: In the city, bust Shadow\nStar while riding Archipelago Star!", APCheckDetect_IsSet },

    { APCK_MAGMA_TA_METAKNIGHT, "Time Attack: MAGMA FLOWS Finish in\nunder 03:15:00 as Meta Knight!",           APCheckDetect_IsSet },
    { APCK_AIRRIDE_1ST_AP_STAR, "Air Ride: Finish in 1st place\non Archipelago Star!",                           APCheckDetect_IsSet },
    { APCK_MELEE_DEDEDE_KO_30,  "Stadium: KIRBY MELEE (All) In one game,\nKO 30 enemies as King Dedede!",        APCheckDetect_IsSet },
    { APCK_VSKD_METAKNIGHT_KO,  "Stadium: VS. KING DEDEDE\nKO King Dedede as Meta Knight!",                      APCheckDetect_IsSet },

    // Vanilla's Top Ride cells carry no mode prefix, so these add one. The two Time
    // Attack rows and the Free Run row keep vanilla's own prefix, which the course
    // names already set apart from Air Ride's.
    { APCK_TR_FREEZE_FAN_3,      "Top Ride: Freeze 3 or more rivals\nusing one Freeze Fan item!",          APCheckDetect_IsSet },
    { APCK_TR_FIRE_NO_BURN,      "Top Ride: FIRE Take 1st place\nwithout getting burned!",                 APCheckDetect_IsSet },
    { APCK_TR_SAND_NO_ANTDOOM,   "Top Ride: SAND Take 1st place\nwithout dropping into Ant Doom!",         APCheckDetect_IsSet },
    { APCK_TR_1ST_VS_3_LV5,      "Top Ride: Finish 1st against\n3 CPUs set to level 5!",                  APCheckDetect_IsSet },
    { APCK_TR_PHOTO,             "Top Ride: On any course, have 2\nplayers finish within 0.20 seconds!",  APCheckDetect_IsSet },
    { APCK_TR_ALL_COLORS,        "Top Ride: Finish a race\nas every Kirby color!",                         APCheckDetect_IsSet },
    { APCK_TR_TA_GRASS_STEER,    "Time Attack: GRASS Finish in\nunder 00:33:00 on Steer Star!",            APCheckDetect_IsSet },
    { APCK_TR_TA_METAL_STEER,    "Time Attack: METAL Finish in\nunder 00:57:00 on Steer Star!",            APCheckDetect_IsSet },
    { APCK_TR_FR_SKY_STEER,      "Free Run: SKY Do 1 lap under\n00:11:00 on Steer Star!",                  APCheckDetect_IsSet },
    { APCK_TR_ALL_COURSES_STEER, "Top Ride: Take 1st place on\nall courses on Steer Star!",                APCheckDetect_IsSet },
    { APCK_TR_NO_HIT,            "Top Ride: Take 1st place\nwithout getting hit once!",                    APCheckDetect_IsSet },
    { APCK_TR_HIT_5_WIN,         "Top Ride: Take 1st after getting\nknocked around 5 times in one race!",  APCheckDetect_IsSet },
    { APCK_TR_ABILITY_ITEMS,     "Top Ride: In one race, use Fire,\nFreeze Fan, Bomb and Walky!",          APCheckDetect_IsSet },
    { APCK_TR_SPEEDDOWN_3_WIN,   "Top Ride: Get 3 Speed Down items in\none race and still finish 1st!",   APCheckDetect_IsSet },
    { APCK_TR_TA_EVEN_LAPS,      "Top Ride: In Time Attack, keep every\nlap within 1 second of each other!", APCheckDetect_IsSet },

    { APCK_AP_STAR_SHOT_KO_CPU,  "City Trial: KO a CPU with a sphere\nshot from the Archipelago Star!",    APCheckDetect_IsSet },
};

#define AP_CHECK_NUM ((int)(sizeof(ap_checks) / sizeof(ap_checks[0])))

// A missing entry would leave an AP location with no way to complete it, stranding
// any progression item fill places on it.
_Static_assert(AP_CHECK_NUM == APCK_NUM, "ap_checks[] must cover every APCheckKind");

// Already recorded as sent this save? The framework range-checks clear_kind first.
static int APChecklist_IsRecorded(int clear_kind)
{
    return (ap_save->sent_checks[AP_CHECKLIST_ROW][clear_kind >> 6] >> (clear_kind & 63)) & 1ULL;
}

// Record a completed AP check. The ClearChecker_SetNewUnlock REPLACEFUNC in
// ap_checks intercepts ap_checklist_mode and sets the AP row's sent_checks
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

int APChecklist_GetBuildMode(void)
{
    return cc_api && cc_api->GetBuildMode ? cc_api->GetBuildMode() : -1;
}

// Set only once custom_checklist has accepted the tab, so callers can tell a live tab
// from ap_checklist_mode's GMMODE_NUM default - the framework hands out that same mode
// when the AP tab registers first.
static int ap_tab_registered = 0;

int APChecklist_IsRegistered(void)
{
    return ap_tab_registered;
}

void APChecklist_RevealAll(void)
{
    if (!ap_tab_registered)
        return;

    // Through the framework rather than by writing is_visible here: it latches the tab
    // open for the session, so a reveal that lands before the grid shuffle survives it.
    cc_api->RevealAll(ap_checklist_mode);
}

void APChecklist_Register(void)
{
    if (cc_api)
        return;

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
        OSReport("[APChecklist] Registration rejected (rc %d) - AP tab disabled\n", mode);
        return;
    }
    // The framework appends to the next free slot; ChecklistModeRow maps whatever it
    // assigned to the fixed AP_CHECKLIST_ROW, so registration order does not matter.
    ap_checklist_mode = mode;
    ap_tab_registered = 1;

    OSReport("[APChecklist] Registered AP tab (mode %d, %d custom checks)\n", mode, AP_CHECK_NUM);
}
