#include "game.h"
#include "os.h"
#include "hoshi/mod.h"

#include "main.h"
#include "ap_checklist.h"
#include "ap_check_detect.h"
#include "custom_checklist_api.h"

// Imported custom_checklist API. Resolved in APChecklist_Register (called from
// OnSaveLoaded) via Hoshi_ImportMod, deferred past OnBoot since the framework mod
// boots after us (alphabetical order).
static const CustomChecklistAPI *cc_api = NULL;

// One zero-argument predicate per cell, as the framework's CustomCheck wants.
// Each is a pure read of latched state - all sampling is in ap_check_detect.c,
// because the framework polls these every frame in every scene.
#define AP_CHECK_PREDICATE(name) \
    static int Check_##name(void) { return APCheckDetect_IsSet(APCK_##name); }

AP_CHECK_PREDICATE(BOOT)
AP_CHECK_PREDICATE(RECEIVE_ITEM)
AP_CHECK_PREDICATE(RECEIVE_5_ITEMS)
AP_CHECK_PREDICATE(CASTLE_FLOWER)
AP_CHECK_PREDICATE(BREAK_ALL_CORAL)
AP_CHECK_PREDICATE(OUT_OF_BOUNDS)
AP_CHECK_PREDICATE(HP_PATCHES_10)
AP_CHECK_PREDICATE(ALLUPS_10)
AP_CHECK_PREDICATE(FOOD_ICECREAM)
AP_CHECK_PREDICATE(FOOD_RICEBALL)
AP_CHECK_PREDICATE(FOOD_CHICKEN)
AP_CHECK_PREDICATE(FOOD_CURRY)
AP_CHECK_PREDICATE(FOOD_RAMEN)
AP_CHECK_PREDICATE(FOOD_OMELET)
AP_CHECK_PREDICATE(FOOD_HAMBURGER)
AP_CHECK_PREDICATE(FOOD_APPLE)
AP_CHECK_PREDICATE(HIGHJUMP_1500)
AP_CHECK_PREDICATE(AIRGLIDER_2000)
AP_CHECK_PREDICATE(MELEE1_100)
AP_CHECK_PREDICATE(MELEE2_60)
AP_CHECK_PREDICATE(SR1_BULK)
AP_CHECK_PREDICATE(SR1_PURPLE_3X)
AP_CHECK_PREDICATE(AIRRIDE_PHOTO)

// The nine Single Race placements and four Drag Race photo finishes are one
// objective repeated per stadium, so their predicates are generated from the
// base clear_kind plus an offset rather than named individually.
#define AP_CHECK_PREDICATE_N(base, n) \
    static int Check_##base##_##n(void) { return APCheckDetect_IsSet(APCK_##base + (n)); }

AP_CHECK_PREDICATE_N(SR1_FIRST, 0)
AP_CHECK_PREDICATE_N(SR1_FIRST, 1)
AP_CHECK_PREDICATE_N(SR1_FIRST, 2)
AP_CHECK_PREDICATE_N(SR1_FIRST, 3)
AP_CHECK_PREDICATE_N(SR1_FIRST, 4)
AP_CHECK_PREDICATE_N(SR1_FIRST, 5)
AP_CHECK_PREDICATE_N(SR1_FIRST, 6)
AP_CHECK_PREDICATE_N(SR1_FIRST, 7)
AP_CHECK_PREDICATE_N(SR1_FIRST, 8)
AP_CHECK_PREDICATE_N(DRAG1_PHOTO, 0)
AP_CHECK_PREDICATE_N(DRAG1_PHOTO, 1)
AP_CHECK_PREDICATE_N(DRAG1_PHOTO, 2)
AP_CHECK_PREDICATE_N(DRAG1_PHOTO, 3)

// clear_kind order is the wire contract with APLocation in the apworld (the AP
// location code is 361 + clear_kind). Labels are the cell text and are
// independent of the apworld's location names - they are kept short because
// the framework composes them into a fixed 128-byte SIS entry.
static const CustomCheck ap_checks[] = {
    { APCK_BOOT,            "Boot the game",                    Check_BOOT },
    { APCK_RECEIVE_ITEM,    "Receive an item",                  Check_RECEIVE_ITEM },
    { APCK_RECEIVE_5_ITEMS, "Receive 5 items",                  Check_RECEIVE_5_ITEMS },

    { APCK_CASTLE_FLOWER,   "Visit the castle flower on foot",  Check_CASTLE_FLOWER },
    { APCK_BREAK_ALL_CORAL, "Break all the coral in one game",  Check_BREAK_ALL_CORAL },
    { APCK_OUT_OF_BOUNDS,   "Go out of bounds in City Trial",   Check_OUT_OF_BOUNDS },

    { APCK_HP_PATCHES_10,   "Get 10 HP Patches in one game",    Check_HP_PATCHES_10 },
    { APCK_ALLUPS_10,       "Collect 10 All Ups in total",      Check_ALLUPS_10 },

    { APCK_FOOD_ICECREAM,   "Eat 3 Ice Creams in one game",     Check_FOOD_ICECREAM },
    { APCK_FOOD_RICEBALL,   "Eat 3 Rice Balls in one game",     Check_FOOD_RICEBALL },
    { APCK_FOOD_CHICKEN,    "Eat 3 Chickens in one game",       Check_FOOD_CHICKEN },
    { APCK_FOOD_CURRY,      "Eat 3 Curries in one game",        Check_FOOD_CURRY },
    { APCK_FOOD_RAMEN,      "Eat 3 Ramens in one game",         Check_FOOD_RAMEN },
    { APCK_FOOD_OMELET,     "Eat 3 Omelets in one game",        Check_FOOD_OMELET },
    { APCK_FOOD_HAMBURGER,  "Eat 3 Hamburgers in one game",     Check_FOOD_HAMBURGER },
    { APCK_FOOD_APPLE,      "Eat 3 Apples in one game",         Check_FOOD_APPLE },

    { APCK_SR1_FIRST + 0,   "SINGLE RACE 1 Finish in 1st",      Check_SR1_FIRST_0 },
    { APCK_SR1_FIRST + 1,   "SINGLE RACE 2 Finish in 1st",      Check_SR1_FIRST_1 },
    { APCK_SR1_FIRST + 2,   "SINGLE RACE 3 Finish in 1st",      Check_SR1_FIRST_2 },
    { APCK_SR1_FIRST + 3,   "SINGLE RACE 4 Finish in 1st",      Check_SR1_FIRST_3 },
    { APCK_SR1_FIRST + 4,   "SINGLE RACE 5 Finish in 1st",      Check_SR1_FIRST_4 },
    { APCK_SR1_FIRST + 5,   "SINGLE RACE 6 Finish in 1st",      Check_SR1_FIRST_5 },
    { APCK_SR1_FIRST + 6,   "SINGLE RACE 7 Finish in 1st",      Check_SR1_FIRST_6 },
    { APCK_SR1_FIRST + 7,   "SINGLE RACE 8 Finish in 1st",      Check_SR1_FIRST_7 },
    { APCK_SR1_FIRST + 8,   "SINGLE RACE 9 Finish in 1st",      Check_SR1_FIRST_8 },

    { APCK_HIGHJUMP_1500,   "HIGH JUMP Jump over 1,500 feet",   Check_HIGHJUMP_1500 },
    { APCK_AIRGLIDER_2000,  "AIR GLIDER Fly over 2,000 feet",   Check_AIRGLIDER_2000 },
    { APCK_MELEE1_100,      "KIRBY MELEE 1 KO over 100 alone",  Check_MELEE1_100 },
    { APCK_MELEE2_60,       "KIRBY MELEE 2 KO over 60 alone",   Check_MELEE2_60 },

    { APCK_SR1_BULK,        "SINGLE RACE 1 1st on Bulk Star",   Check_SR1_BULK },
    { APCK_SR1_PURPLE_3X,   "SINGLE RACE 1 1st 3x as Purple",   Check_SR1_PURPLE_3X },

    { APCK_DRAG1_PHOTO + 0, "DRAG RACE 1 Photo finish",         Check_DRAG1_PHOTO_0 },
    { APCK_DRAG1_PHOTO + 1, "DRAG RACE 2 Photo finish",         Check_DRAG1_PHOTO_1 },
    { APCK_DRAG1_PHOTO + 2, "DRAG RACE 3 Photo finish",         Check_DRAG1_PHOTO_2 },
    { APCK_DRAG1_PHOTO + 3, "DRAG RACE 4 Photo finish",         Check_DRAG1_PHOTO_3 },
    { APCK_AIRRIDE_PHOTO,   "Air Ride Photo finish",            Check_AIRRIDE_PHOTO },
};

#define AP_CHECK_NUM ((int)(sizeof(ap_checks) / sizeof(ap_checks[0])))

// Every APCheckKind has a cell. A missing entry would leave an AP location with
// no way to complete it, which strands any progression item fill places on it.
_Static_assert(AP_CHECK_NUM == APCK_NUM, "ap_checks[] must cover every APCheckKind");

// Already recorded as sent this save? Out-of-range cells report "done". The AP
// checklist's recorded state is row AP_CHECKLIST_ROW of sent_checks[].
static int APChecklist_IsRecorded(int clear_kind)
{
    if (clear_kind < 0 || clear_kind >= CLEAR_KIND_NUM)
        return 1;
    return (ap_save->sent_checks[AP_CHECKLIST_ROW][clear_kind >> 6] >> (clear_kind & 63)) & 1ULL;
}

// Record a completed AP check. Routes through ClearChecker_SetNewUnlock with the
// framework-assigned ap_checklist_mode, which check_detection's REPLACEFUNC
// intercepts for that mode: it sets the AP row's sent_checks bit, fires the "Check sent"
// textbox, re-evaluates goals, and - when the unlock cache is invalid (mid-run) -
// plays the unlock SFX. The framework seeds the cell's is_new/is_visible after this
// returns, so the flip-and-sparkle runs on the next tab entry.
static void APChecklist_RecordComplete(int clear_kind)
{
    ClearChecker_SetNewUnlock((GameMode)ap_checklist_mode, (u8)clear_kind);
}

// The framework's evaluator no-ops until this returns nonzero. game_ready is set
// at the end of OnSaveLoaded - after the save loads and the textbox API resolves,
// so RecordComplete's textbox enqueue is safe.
static int APChecklist_IsReady(void)
{
    return ap_data && ap_data->game_ready;
}

// Tab art: an HSD archive staged to the FST root from this mod's assets/, exporting
// the banner watermark and tab-emblem image descriptors.
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
    // Adopt whatever mode the framework assigned (it appends to the next free slot).
    // RecordComplete and check_detection both read ap_checklist_mode, and
    // ChecklistModeRow maps it to the fixed AP_CHECKLIST_ROW - so any assigned slot
    // works and registration order across mods does not matter.
    ap_checklist_mode = mode;

    OSReport("[APChecklist] Registered AP tab (mode %d, %d custom checks)\n", mode, AP_CHECK_NUM);
}
