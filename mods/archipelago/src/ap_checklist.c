#include "game.h"
#include "os.h"
#include "hoshi/mod.h"
#include "inline.h"

#include "main.h"
#include "ap_checklist.h"
#include "ap_check_detect.h"
#include "custom_checklist_api.h"

static const CustomChecklistAPI *cc_api = NULL;

// Every cell reads latched state through APCheckDetect_IsSet.
#define AP_CHECK(ck, label) { (ck), (label), APCheckDetect_IsSet }

// Each label is its AP location's name minus the leading "Archipelago: ".
static const CustomCheck ap_checks[] = {
    AP_CHECK(APCK_CASTLE_FLOWER,   "City Trial: Visit the flower\non top of Castle Hall on foot!"),
    AP_CHECK(APCK_BREAK_ALL_CORAL, "City Trial: Break all\nthe coral in one game!"),

    AP_CHECK(APCK_HP_PATCHES_10,   "City Trial: In one game,\nget 10 or more HP Patches!"),
    AP_CHECK(APCK_ALLUPS_5,        "City Trial: Collect\n5 All Ups in total!"),

    AP_CHECK(APCK_FOOD_ICECREAM,   "City Trial: In one game,\neat 3 or more Ice Creams!"),
    AP_CHECK(APCK_FOOD_RICEBALL,   "City Trial: In one game,\neat 3 or more Rice Balls!"),
    AP_CHECK(APCK_FOOD_CHICKEN,    "City Trial: In one game,\neat 3 or more Chickens!"),
    AP_CHECK(APCK_FOOD_CURRY,      "City Trial: In one game, eat\n3 or more plates of Curry!"),
    AP_CHECK(APCK_FOOD_RAMEN,      "City Trial: In one game, eat\n3 or more bowls of Ramen!"),
    AP_CHECK(APCK_FOOD_OMELET,     "City Trial: In one game,\neat 3 or more Omelets!"),
    AP_CHECK(APCK_FOOD_HAMBURGER,  "City Trial: In one game,\neat 3 or more Hamburgers!"),
    AP_CHECK(APCK_FOOD_APPLE,      "City Trial: In one game,\neat 3 or more Apples!"),

    AP_CHECK(APCK_SR1_FIRST + 0,   "Stadium: SINGLE RACE 1\nFinish in 1st place!"),
    AP_CHECK(APCK_SR1_FIRST + 1,   "Stadium: SINGLE RACE 2\nFinish in 1st place!"),
    AP_CHECK(APCK_SR1_FIRST + 2,   "Stadium: SINGLE RACE 3\nFinish in 1st place!"),
    AP_CHECK(APCK_SR1_FIRST + 3,   "Stadium: SINGLE RACE 4\nFinish in 1st place!"),
    AP_CHECK(APCK_SR1_FIRST + 4,   "Stadium: SINGLE RACE 5\nFinish in 1st place!"),
    AP_CHECK(APCK_SR1_FIRST + 5,   "Stadium: SINGLE RACE 6\nFinish in 1st place!"),
    AP_CHECK(APCK_SR1_FIRST + 6,   "Stadium: SINGLE RACE 7\nFinish in 1st place!"),
    AP_CHECK(APCK_SR1_FIRST + 7,   "Stadium: SINGLE RACE 8\nFinish in 1st place!"),
    AP_CHECK(APCK_SR1_FIRST + 8,   "Stadium: SINGLE RACE 9\nFinish in 1st place!"),

    AP_CHECK(APCK_HIGHJUMP_1500,   "Stadium: HIGH JUMP\nJump higher than 1,500 feet!"),
    AP_CHECK(APCK_AIRGLIDER_2000,  "Stadium: AIR GLIDER\nfly more than 2,000 feet!"),
    AP_CHECK(APCK_MELEE1_100,      "Stadium: KIRBY MELEE 1 In one game,\nKO over 100 enemies by yourself!"),
    AP_CHECK(APCK_MELEE2_60,       "Stadium: KIRBY MELEE 2 In one game,\nKO over 60 enemies by yourself!"),

    AP_CHECK(APCK_SR1_BULK,        "Stadium: SINGLE RACE 1\nFinish in 1st place on Bulk Star!"),
    AP_CHECK(APCK_SR1_PURPLE_3X,   "Stadium: SINGLE RACE 1 Finish in\n1st place 3 times as Purple Kirby!"),

    AP_CHECK(APCK_DRAG_PHOTO,      "Stadium: In any DRAG RACE, have 2\nplayers finish within 0.10 seconds!"),
    AP_CHECK(APCK_AIRRIDE_PHOTO,   "Air Ride: On any course, have 2\nplayers finish within 0.10 seconds!"),

    AP_CHECK(APCK_AIRRIDE_ALL_COLORS, "Air Ride: Finish a race\nas every Kirby color!"),
    AP_CHECK(APCK_MODEL_CITY,         "City Trial: Visit the\nmodel city on foot!"),
    AP_CHECK(APCK_VOLCANO_FLOWER,     "City Trial: Visit the flower on top\nof the volcanic cliffs on foot!"),
    AP_CHECK(APCK_SKY_GARDEN_TOP,     "City Trial: Visit the top of\nthe garden in the sky on foot!"),
    AP_CHECK(APCK_MAX_ALTITUDE,       "City Trial: Fly to the\nhighest point possible!"),

    AP_CHECK(APCK_AIRRIDE_1ST_METAKNIGHT, "Air Ride: Finish in 1st place\nas Meta Knight!"),
    AP_CHECK(APCK_AIRRIDE_1ST_DEDEDE,     "Air Ride: Finish in 1st place\nas King Dedede!"),

    AP_CHECK(APCK_NEBULA_1ST,         "Air Ride: NEBULA BELT\nFinish in 1st place!"),
    AP_CHECK(APCK_NEBULA_DIST_2MIN,   "Air Ride: NEBULA BELT\nRace over 5,500 feet in 2 minutes!"),
    AP_CHECK(APCK_NEBULA_2LAP_TIME,   "Air Ride: NEBULA BELT\nFinish 2 laps in under 02:30:00!"),
    AP_CHECK(APCK_NEBULA_1ST_SCOOTER, "Air Ride: NEBULA BELT Finish in\n1st place on Wheelie Scooter!"),
    AP_CHECK(APCK_NEBULA_AIRBORNE,    "Air Ride: NEBULA BELT Fly 10 seconds\non Dragoon, Flight or Winged Star!"),

    AP_CHECK(APCK_DD3_KO_10,          "Stadium: DESTRUCTION DERBY 3\nIn one game, KO a rival 10 times or more!"),
    AP_CHECK(APCK_DD_DEDEDE_KO_KIRBY, "Stadium: DESTRUCTION DERBY (All)\nAs King Dedede, KO 10 Kirbys in one game!"),

    AP_CHECK(APCK_MIC_COPY_CHANCE,    "City Trial: Get the Mic ability\nfrom the Copy Chance Wheel!"),
    AP_CHECK(APCK_MIC_ENEMY_KOS,      "Stadium: KIRBY MELEE (All) In one game,\nKO 10 enemies as Mic Kirby!"),

    AP_CHECK(APCK_BOX_BLUE_20,        "City Trial: In one game,\nbreak 20 or more blue boxes!"),
    AP_CHECK(APCK_BOX_GREEN_10,       "City Trial: In one game,\nbreak 10 or more green boxes!"),
    AP_CHECK(APCK_BOX_RED_10,         "City Trial: In one game,\nbreak 10 or more red boxes!"),

    AP_CHECK(APCK_MEADOWS_SHORTCUT,   "Air Ride: FANTASY MEADOWS\nTake the shortcut!"),

    AP_CHECK(APCK_ASSEMBLE_AP_STAR,   "City Trial: Collect all 6 spheres\nand assemble the Archipelago Star!"),
    AP_CHECK(APCK_ASSEMBLE_ALL_LEGENDARY, "City Trial: In one game, assemble\nDragoon, Hydra and Archipelago Star!"),

    AP_CHECK(APCK_OFFENSE_PATCHES_10, "City Trial: In one game,\nget 10 or more Offense Patches!"),

    AP_CHECK(APCK_NEBULA_2LAP_FAST,   "Air Ride: NEBULA BELT\nFinish 2 laps in under 02:06:00!"),
    AP_CHECK(APCK_NEBULA_TA,          "Time Attack: NEBULA BELT\nFinish in under 03:35:00!"),
    AP_CHECK(APCK_NEBULA_TA_FAST,     "Time Attack: NEBULA BELT\nFinish in under 03:10:00!"),
    AP_CHECK(APCK_NEBULA_TA_HYDRA,    "Time Attack: NEBULA BELT\nFinish in under 03:15:00 on Hydra!"),
    AP_CHECK(APCK_NEBULA_FR,          "Free Run: NEBULA BELT\nFinish 1 lap in under 01:15:00!"),
    AP_CHECK(APCK_NEBULA_FR_FAST,     "Free Run: NEBULA BELT\nFinish 1 lap in under 01:03:00!"),
    AP_CHECK(APCK_NEBULA_FR_WARP,     "Free Run: NEBULA BELT\nDo 1 lap under 01:10:00 on Warpstar!"),

    AP_CHECK(APCK_VALLEY_FR_AP_STAR,  "Free Run: CELESTIAL VALLEY Do 1 lap\nunder 01:00:00 on Archipelago Star!"),
    AP_CHECK(APCK_CHECKER_TA_FLIGHT,  "Time Attack: CHECKER KNIGHTS Finish\nin under 04:00:00 on Flight Warp Star!"),
    AP_CHECK(APCK_PASSAGE_FR_COMPACT, "Free Run: MACHINE PASSAGE Do 1 lap\nunder 01:05:00 on Compact Star!"),
    AP_CHECK(APCK_FROZEN_TA_WHEELIE,  "Time Attack: FROZEN HILLSIDE Finish\nin under 03:00:00 on Wheelie Bike!"),
    AP_CHECK(APCK_SANDS_TA_FLIGHT,    "Time Attack: SKY SANDS Finish in\nunder 02:50:00 on Flight Warp Star!"),

    AP_CHECK(APCK_EVENT_RUNAMOK_DIST,     "City Trial: While energy tanks run\namok, travel over 1,000 feet!"),
    AP_CHECK(APCK_EVENT_RAILFIRE_ALL,     "City Trial: Catch fire at all 5 rail\nstations while they burn!"),
    AP_CHECK(APCK_EVENT_SAMEITEM_20,      "City Trial: When the boxes all hold\nthe same item, get 20 of it!"),
    AP_CHECK(APCK_EVENT_LIGHTHOUSE_BOTH,  "City Trial: Go under both lights\nof the city lighthouse!"),
    AP_CHECK(APCK_EVENT_PREDICTION_WRONG, "City Trial: Get a Stadium Prediction\nthat turns out wrong!"),
    AP_CHECK(APCK_EVENT_UFO_ALLUP,        "City Trial: Get the All Up\non top of the UFO!"),
    AP_CHECK(APCK_EVENT_FORMATION_BUMP,   "City Trial: Bump into the Air Ride\nmachine formation!"),
    AP_CHECK(APCK_EVENT_BOUNCE_ITEMS,     "City Trial: While the items bounce,\nget over 10 items!"),
    AP_CHECK(APCK_EVENT_FOG_KO,           "City Trial: KO a rival while\na dense fog covers the city!"),
    AP_CHECK(APCK_EVENT_FAKE_NONE,        "City Trial: Get 5 power-ups while\nsome are fake, and touch no fakes!"),

    AP_CHECK(APCK_SAME_COPY_3X,       "City Trial: In one game, get the\nsame copy ability 3 times in a row!"),

    AP_CHECK(APCK_BUST_REX_ON_SCOOTER,    "City Trial: In the city, bust Rex\nWheelie while riding Wheelie Scooter!"),
    AP_CHECK(APCK_BUST_WINGED_ON_FLIGHT,  "City Trial: In the city, bust Winged\nStar while riding Flight Warp Star!"),
    AP_CHECK(APCK_BUST_SHADOW_ON_AP_STAR, "City Trial: In the city, bust Shadow\nStar while riding Archipelago Star!"),

    AP_CHECK(APCK_MAGMA_TA_METAKNIGHT, "Time Attack: MAGMA FLOWS Finish in\nunder 03:15:00 as Meta Knight!"),
    AP_CHECK(APCK_AIRRIDE_1ST_AP_STAR, "Air Ride: Finish in 1st place\non Archipelago Star!"),
    AP_CHECK(APCK_MELEE_DEDEDE_KO_30,  "Stadium: KIRBY MELEE (All) In one game,\nKO 30 enemies as King Dedede!"),
    AP_CHECK(APCK_VSKD_METAKNIGHT_KO,  "Stadium: VS. KING DEDEDE\nKO King Dedede as Meta Knight!"),

    AP_CHECK(APCK_TR_FREEZE_FAN_3,      "Top Ride: Freeze 3 or more rivals\nusing one Freeze Fan item!"),
    AP_CHECK(APCK_TR_FIRE_NO_BURN,      "Top Ride: FIRE Take 1st place\nwithout getting burned!"),
    AP_CHECK(APCK_TR_SAND_NO_ANTDOOM,   "Top Ride: SAND Take 1st place\nwithout dropping into Ant Doom!"),
    AP_CHECK(APCK_TR_1ST_VS_3_LV5,      "Top Ride: Finish 1st against\n3 CPUs set to level 5!"),
    AP_CHECK(APCK_TR_PHOTO,             "Top Ride: On any course, have 2\nplayers finish within 0.20 seconds!"),
    AP_CHECK(APCK_TR_ALL_COLORS,        "Top Ride: Finish a race\nas every Kirby color!"),
    AP_CHECK(APCK_TR_TA_GRASS_STEER,    "Time Attack: GRASS Finish in\nunder 00:33:00 on Steer Star!"),
    AP_CHECK(APCK_TR_TA_METAL_STEER,    "Time Attack: METAL Finish in\nunder 00:57:00 on Steer Star!"),
    AP_CHECK(APCK_TR_FR_SKY_STEER,      "Free Run: SKY Do 1 lap under\n00:11:00 on Steer Star!"),
    AP_CHECK(APCK_TR_ALL_COURSES_STEER, "Top Ride: Take 1st place on\nall courses on Steer Star!"),
    AP_CHECK(APCK_TR_NO_HIT,            "Top Ride: Take 1st place\nwithout getting hit once!"),
    AP_CHECK(APCK_TR_HIT_5_WIN,         "Top Ride: Take 1st after getting\nknocked around 5 times in one race!"),
    AP_CHECK(APCK_TR_ABILITY_ITEMS,     "Top Ride: In one race, use Fire,\nFreeze Fan, Bomb and Walky!"),
    AP_CHECK(APCK_TR_SPEEDDOWN_3_WIN,   "Top Ride: Get 3 Speed Down items in\none race and still finish 1st!"),
    AP_CHECK(APCK_TR_TA_EVEN_LAPS,      "Top Ride: In Time Attack, keep every\nlap within 1 second of each other!"),

    AP_CHECK(APCK_AP_STAR_SHOT_KO_CPU,  "City Trial: KO a CPU with a sphere\nshot from the Archipelago Star!"),

    AP_CHECK(APCK_RIDE_5_MACHINES,        "City Trial: In one game,\nride 5 different machines!"),
    AP_CHECK(APCK_AIRBORNE_20S,           "City Trial: In the city, stay\nairborne longer than 20 seconds!"),
    AP_CHECK(APCK_EVENT_DYNABLADE_ITEMS,  "City Trial: In one game, grab 5\nitems dropped by Dyna Blade!"),
    AP_CHECK(APCK_PANIC_SPIN_KO,          "City Trial: Use a Panic Spin\nto KO a rival!"),
    AP_CHECK(APCK_WHISPY_WOODS,           "City Trial: Talk to\nWhispy Woods on foot!"),
    AP_CHECK(APCK_LIGHTHOUSE_TOP,         "City Trial: Visit the top of\nthe lighthouse on foot!"),
    AP_CHECK(APCK_UNDER_WATERWHEEL,       "City Trial: Go underneath\nthe waterwheel!"),
    AP_CHECK(APCK_ALL_GRIND_RAILS,        "City Trial: In one game, use\nall the grind rails in the city!"),
    AP_CHECK(APCK_COPY_6_KINDS,           "City Trial: In one game, get 6\ndifferent copy abilities!"),
    AP_CHECK(APCK_EVENT_METEOR_NO_DAMAGE, "City Trial: Take no damage while\nthe meteor attacks the city!"),
    AP_CHECK(APCK_MAX_A_STAT,             "City Trial: In one game, max\nout a stat with patches!"),

    AP_CHECK(APCK_DD_KO_5_UNHURT,         "Stadium: DESTRUCTION DERBY (All) KO 5\nrivals without getting knocked out!"),
    AP_CHECK(APCK_VSKD_NO_DAMAGE,         "Stadium: VS. KING DEDEDE KO King\nDedede without taking any damage!"),
    AP_CHECK(APCK_DRAG_ALL_1ST,           "Stadium: Take 1st place\nin every DRAG RACE!"),

    AP_CHECK(APCK_AIRRIDE_ALL_COURSES_1ST, "Air Ride: Take 1st place\non every course!"),
    AP_CHECK(APCK_BEANSTALK_FERRIS_LAPS,   "Air Ride: BEANSTALK PARK Ride the\nFerris wheel every lap and take 1st!"),
    AP_CHECK(APCK_SANDS_NO_QUICKSAND,      "Air Ride: SKY SANDS Take 1st place\nwithout entering the quicksand!"),
    AP_CHECK(APCK_CHECKER_NO_SPIN_PANELS,  "Air Ride: CHECKER KNIGHTS Take 1st\nwithout using any spin panels!"),
    AP_CHECK(APCK_MAGMA_NO_BOOST_PANELS,   "Air Ride: MAGMA FLOWS Take 1st\nwithout using any Boost Panels!"),

    AP_CHECK(APCK_TR_WATER_NO_FALLS,      "Top Ride: WATER Take 1st place\nwithout entering the falls!"),
    AP_CHECK(APCK_TR_LIGHT_NO_RAIL,       "Top Ride: LIGHT Take 1st place\nwithout grinding the rail!"),
    AP_CHECK(APCK_TR_EVERY_ITEM,          "Top Ride: Use every kind\nof item at least once!"),
};

#define AP_CHECK_NUM ((int)GetElementsIn(ap_checks))

_Static_assert(AP_CHECK_NUM == APCK_NUM, "ap_checks[] must cover every APCheckKind");

// Called only with clear_kinds from ap_checks[], which Register validated.
static int APChecklist_IsRecorded(int clear_kind)
{
    return SENT_CHECK_BIT(AP_CHECKLIST_ROW, clear_kind);
}

// Routed through ClearChecker_SetNewUnlock, whose replacement records the AP row's sent
// bit.
static void APChecklist_RecordComplete(int clear_kind)
{
    ClearChecker_SetNewUnlock((GameMode)ap_checklist_mode, (u8)clear_kind);
}

static int APChecklist_IsReady(void)
{
    return ap_data->game_ready;
}

// Tab art: an archive exporting the banner watermark and tab-emblem images.
#define AP_TEX_FILE      "ApChecklistTex"
#define AP_BANNER_SYMBOL "apBannerImg"
#define AP_EMBLEM_SYMBOL "apEmblemImg"

static const CustomChecklistDesc ap_desc = {
    .name = AP_CHECKLIST_NAME,
    .theme = AP_THEME_COLOR,
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
    return cc_api ? cc_api->GetBuildMode() : -1;
}

// ap_checklist_mode's GMMODE_NUM default is also the mode the framework hands the first
// tab it registers, so it can't tell a live tab by itself.
static int ap_tab_registered = 0;

int APChecklist_IsRegistered(void)
{
    return ap_tab_registered;
}

void APChecklist_RevealAll(void)
{
    if (!ap_tab_registered)
        return;

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
        OSReport("[APChecklist] custom_checklist missing from this build, AP tab disabled\n");
        return;
    }

    int mode = cc_api->Register(&ap_desc);
    if (mode < 0)
    {
        OSReport("[APChecklist] Registration rejected (rc %d), AP tab disabled\n", mode);
        return;
    }
    ap_checklist_mode = mode;
    ap_tab_registered = 1;

    OSReport("[APChecklist] Registered AP tab (mode %d, %d custom checks)\n", mode, AP_CHECK_NUM);
}

const char *APChecklist_RowName(int row)
{
    static const char *const names[CHECKLIST_MODE_NUM] = {
        [GMMODE_AIRRIDE]   = "Air Ride",
        [GMMODE_TOPRIDE]   = "Top Ride",
        [GMMODE_CITYTRIAL] = "City Trial",
        [AP_CHECKLIST_ROW] = AP_CHECKLIST_NAME,
    };
    return (unsigned)row < CHECKLIST_MODE_NUM ? names[row] : "Checklist";
}

// ModeColors[] is sized GMMODE_NUM, so the AP row carries its own tint.
GXColor APChecklist_RowColor(int row)
{
    static const GXColor ap_theme = AP_THEME_COLOR;
    if (row == AP_CHECKLIST_ROW)
        return ap_theme;
    if ((unsigned)row < GMMODE_NUM)
        return tb_api->ModeColors[row];
    return tb_api->DefaultColor;
}
