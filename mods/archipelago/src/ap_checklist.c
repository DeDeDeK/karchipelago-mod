#include "game.h"
#include "os.h"
#include "hoshi/mod.h"

#include "main.h"
#include "ap_checklist.h"
#include "custom_checklist_api.h"

// The AP checklist is a custom checklist tab (the synthetic 4th checklist mode)
// built on the custom_checklist framework: the framework owns the tab's
// presentation (minor scene, grid build, theme recolor, banner/emblem swap) and
// per-frame check evaluation, while this file supplies the AP-specific pieces -
// the objective set, the blue theme, the tab art, and where a completion is
// recorded (AP's sent_checks_ap, routed through check_detection).

// Imported custom_checklist API. Resolved in APChecklist_Register (called from
// OnSaveLoaded) via Hoshi_ImportMod, deferred past OnBoot since the framework mod
// boots after us (alphabetical order).
static const CustomChecklistAPI *cc_api = NULL;

// Phase 1 stubs: observable, deterministic placeholders that exercise the pipeline
// end to end. Replaced by the real custom objectives later.
static int Check_Booted(void)            { return ap_save->boot_num >= 1; }
static int Check_ReceivedAnyItem(void)   { return ap_save->item_received_count >= 1; }
static int Check_ReceivedFiveItems(void) { return ap_save->item_received_count >= 5; }

static const CustomCheck ap_checks[] = {
    { 0, "Boot the game",   Check_Booted },
    { 1, "Receive an item", Check_ReceivedAnyItem },
    { 2, "Receive 5 items", Check_ReceivedFiveItems },
};

#define AP_CHECK_NUM ((int)(sizeof(ap_checks) / sizeof(ap_checks[0])))

// Already recorded as sent this save? Out-of-range cells report "done". The AP
// checklist's recorded state is the appended sent_checks_ap bitmask (the 4th-mode
// parallel of sent_checks[]).
static int APChecklist_IsRecorded(int clear_kind)
{
    if (clear_kind < 0 || clear_kind >= CLEAR_KIND_NUM)
        return 1;
    return (ap_save->sent_checks_ap[clear_kind >> 6] >> (clear_kind & 63)) & 1ULL;
}

// Record a completed AP check. Routes through ClearChecker_SetNewUnlock with the
// framework-assigned ap_checklist_mode, which check_detection's REPLACEFUNC
// intercepts for that mode: it sets the sent_checks_ap bit, fires the "Check sent"
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

// AP blue. The framework retints City Trial's green tab tint onto this hue (it
// borrows CT's grid template). Tunable - the dominant channel sets the hue.
#define AP_THEME_R 40
#define AP_THEME_G 120
#define AP_THEME_B 230

// Tab art: an HSD archive staged to the FST root from this mod's assets/, exporting
// the banner watermark and tab-emblem image descriptors.
#define AP_TEX_FILE      "ApChecklistTex"
#define AP_BANNER_SYMBOL "apBannerImg"
#define AP_EMBLEM_SYMBOL "apEmblemImg"

static const CustomChecklistDesc ap_desc = {
    .name = "Archipelago",
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
    // RecordComplete and check_detection both read ap_checklist_mode, and the AP
    // sent_checks_ap routing keys on "mode >= GMMODE_NUM" - so any assigned slot
    // works and registration order across mods does not matter.
    ap_checklist_mode = mode;

    OSReport("[APChecklist] Registered AP tab (mode %d, %d custom checks)\n", mode, AP_CHECK_NUM);
}
