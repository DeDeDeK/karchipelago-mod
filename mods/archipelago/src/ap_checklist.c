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
// Each is a pure read of latched state - all sampling happens in the detection
// hooks, because the framework polls these every frame in every scene.
#define AP_CHECK_PREDICATE(name) \
    static int Check_##name(void) { return APCheckDetect_IsSet(APCK_##name); }

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
// independent of the apworld's location names.
//
// They follow the vanilla checklist's wording: a title-case category prefix
// ("City Trial: ", "Stadium: ", "Air Ride: "), the stadium name in caps with no
// colon of its own, and a trailing "!". Length is bounded - the framework composes
// each into a fixed 128-byte SIS entry whose glyph budget is 124 bytes, 2 per
// character and 1 per space, and silently truncates past it.
//
// The "\n" is the line break, placed rather than left to the framework's automatic
// midpoint split: vanilla breaks after the whole stadium designation, which a
// width-only rule parts from its trailing number ("SINGLE RACE / 8 Finish in 1st!").
// A label short enough for one line carries none.
static const CustomCheck ap_checks[] = {
    { APCK_CASTLE_FLOWER,   "City Trial: Visit the\ncastle flower on foot!", Check_CASTLE_FLOWER },
    { APCK_BREAK_ALL_CORAL, "City Trial: Break all\nthe coral in one game!", Check_BREAK_ALL_CORAL },
    { APCK_OUT_OF_BOUNDS,   "City Trial: Go out of bounds!",                Check_OUT_OF_BOUNDS },

    { APCK_HP_PATCHES_10,   "City Trial: Get 10\nHP Patches in one game!",  Check_HP_PATCHES_10 },
    { APCK_ALLUPS_10,       "City Trial: Collect\n10 All Ups in total!",    Check_ALLUPS_10 },

    { APCK_FOOD_ICECREAM,   "City Trial: Eat 3\nIce Creams in one game!",   Check_FOOD_ICECREAM },
    { APCK_FOOD_RICEBALL,   "City Trial: Eat 3\nRice Balls in one game!",   Check_FOOD_RICEBALL },
    { APCK_FOOD_CHICKEN,    "City Trial: Eat 3\nChickens in one game!",     Check_FOOD_CHICKEN },
    { APCK_FOOD_CURRY,      "City Trial: Eat 3\nCurries in one game!",      Check_FOOD_CURRY },
    { APCK_FOOD_RAMEN,      "City Trial: Eat 3\nRamens in one game!",       Check_FOOD_RAMEN },
    { APCK_FOOD_OMELET,     "City Trial: Eat 3\nOmelets in one game!",      Check_FOOD_OMELET },
    { APCK_FOOD_HAMBURGER,  "City Trial: Eat 3\nHamburgers in one game!",   Check_FOOD_HAMBURGER },
    { APCK_FOOD_APPLE,      "City Trial: Eat 3\nApples in one game!",       Check_FOOD_APPLE },

    { APCK_SR1_FIRST + 0,   "Stadium: SINGLE RACE 1\nFinish in 1st!",       Check_SR1_FIRST_0 },
    { APCK_SR1_FIRST + 1,   "Stadium: SINGLE RACE 2\nFinish in 1st!",       Check_SR1_FIRST_1 },
    { APCK_SR1_FIRST + 2,   "Stadium: SINGLE RACE 3\nFinish in 1st!",       Check_SR1_FIRST_2 },
    { APCK_SR1_FIRST + 3,   "Stadium: SINGLE RACE 4\nFinish in 1st!",       Check_SR1_FIRST_3 },
    { APCK_SR1_FIRST + 4,   "Stadium: SINGLE RACE 5\nFinish in 1st!",       Check_SR1_FIRST_4 },
    { APCK_SR1_FIRST + 5,   "Stadium: SINGLE RACE 6\nFinish in 1st!",       Check_SR1_FIRST_5 },
    { APCK_SR1_FIRST + 6,   "Stadium: SINGLE RACE 7\nFinish in 1st!",       Check_SR1_FIRST_6 },
    { APCK_SR1_FIRST + 7,   "Stadium: SINGLE RACE 8\nFinish in 1st!",       Check_SR1_FIRST_7 },
    { APCK_SR1_FIRST + 8,   "Stadium: SINGLE RACE 9\nFinish in 1st!",       Check_SR1_FIRST_8 },

    { APCK_HIGHJUMP_1500,   "Stadium: HIGH JUMP\nJump over 1,500 feet!",    Check_HIGHJUMP_1500 },
    { APCK_AIRGLIDER_2000,  "Stadium: AIR GLIDER\nFly over 2,000 feet!",    Check_AIRGLIDER_2000 },
    { APCK_MELEE1_100,      "Stadium: KIRBY MELEE 1\nKO over 100 alone!",   Check_MELEE1_100 },
    { APCK_MELEE2_60,       "Stadium: KIRBY MELEE 2\nKO over 60 alone!",    Check_MELEE2_60 },

    { APCK_SR1_BULK,        "Stadium: SINGLE RACE 1\n1st on Bulk Star!",    Check_SR1_BULK },
    { APCK_SR1_PURPLE_3X,   "Stadium: SINGLE RACE 1\n1st 3x as Purple!",    Check_SR1_PURPLE_3X },

    { APCK_DRAG1_PHOTO + 0, "Stadium: DRAG RACE 1\nPhoto finish!",          Check_DRAG1_PHOTO_0 },
    { APCK_DRAG1_PHOTO + 1, "Stadium: DRAG RACE 2\nPhoto finish!",          Check_DRAG1_PHOTO_1 },
    { APCK_DRAG1_PHOTO + 2, "Stadium: DRAG RACE 3\nPhoto finish!",          Check_DRAG1_PHOTO_2 },
    { APCK_DRAG1_PHOTO + 3, "Stadium: DRAG RACE 4\nPhoto finish!",          Check_DRAG1_PHOTO_3 },
    { APCK_AIRRIDE_PHOTO,   "Air Ride: Photo finish!",                      Check_AIRRIDE_PHOTO },
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

void APChecklist_RevealAll(void)
{
    GameClearData *cd = gmGetClearcheckerTypeP((GameMode)ap_checklist_mode);
    if (!cd)
        return;

    for (int i = 0; i < AP_CHECK_NUM; i++)
        cd->clear[ap_checks[i].clear_kind].is_visible = 1;

    OSReport("[APChecklist] Revealed %d cells\n", AP_CHECK_NUM);
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
    // Adopt whatever mode the framework assigned (it appends to the next free slot).
    // RecordComplete and check_detection both read ap_checklist_mode, and
    // ChecklistModeRow maps it to the fixed AP_CHECKLIST_ROW - so any assigned slot
    // works and registration order across mods does not matter.
    ap_checklist_mode = mode;

    OSReport("[APChecklist] Registered AP tab (mode %d, %d custom checks)\n", mode, AP_CHECK_NUM);
}
