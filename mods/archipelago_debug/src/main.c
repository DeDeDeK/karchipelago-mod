#include "os.h"
#include "game.h"
#include "hsd.h"
#include "hoshi/mod.h"
#include "hoshi/func.h"

#include "archipelago_api.h"
#include "debug_menu.h"

const ArchipelagoAPI *ap_api = 0;
const TextBoxAPI *tb_api = 0;

static void TryImportApi(void)
{
    if (!ap_api)
        ap_api = (const ArchipelagoAPI *)Hoshi_ImportMod(
            (char *)ARCHIPELAGO_MOD_NAME,
            ARCHIPELAGO_API_MAJOR, ARCHIPELAGO_API_MINOR);
    if (!tb_api)
        tb_api = (const TextBoxAPI *)Hoshi_ImportMod(
            (char *)TEXTBOX_MOD_NAME, TEXTBOX_API_MAJOR, TEXTBOX_API_MINOR);
}

static void OnSaveLoaded(void)
{
    TryImportApi();
    if (!ap_api)
    {
        OSReport("[ApDebug] Failed to import KARchipelago API\n");
        return;
    }
    if (!tb_api)
        OSReport("[ApDebug] textbox API unavailable, debug actions post no messages\n");
    DebugMenu_RefreshState();
    OSReport("[ApDebug] API imported, debug menu ready\n");
}

// Z is unused by vanilla checklist navigation. ClearChecker_SetNewUnlock is
// REPLACEFUNC'd, so the AP check fires and goal evaluation runs.
static void UnlockHoveredCell(void)
{
    u8 mode, k;
    if (!ap_api->GetHoveredCell(&mode, &k))
        return;

    u8 src_mode, src_ri;
    int has_placement = ap_api->ResolveCell(mode, k, &src_mode, &src_ri);
    if (has_placement)
    {
        u8 rtype = stc_reward_table_ptrs[src_mode][src_ri].reward_type;
        OSReport("[ApDebug] Z unlock mode=%d clear_kind=%d -> %s ri=%d type=%s (0x%02x)\n",
                 mode, k, (src_mode == mode) ? "same-mode" : "cross-mode",
                 src_ri, Reward_TypeName(rtype), rtype);
    }
    else
    {
        OSReport("[ApDebug] Z unlock mode=%d clear_kind=%d, no local reward placement\n",
                 mode, k);
    }

    ClearChecker_SetNewUnlock(mode, k);
    // SetNewUnlock raises only is_new (and nothing during a LAN session); a debug
    // unlock also shows the cell as unlocked.
    GameClearData *cd = gmGetClearcheckerTypeP(mode);
    if (!cd)
        return;
    cd->clear[k].is_new = 1;
    cd->clear[k].is_unlocked = 1;
    cd->clear[k].is_visible = 1;

    if (has_placement && DebugMenu_ShouldAutoGrantOnUnlock())
    {
        OSReport("[ApDebug] Auto-granted reward mode=%d ri=%d\n", src_mode, src_ri);
        ap_api->GrantReward(src_mode, src_ri);
        Hoshi_WriteSave();
    }
}

static void OnFrameStart(void)
{
    if (!ap_api)
        return;

    HSD_Pad *pad = &stc_engine_pads[0];

    if (pad->down & PAD_TRIGGER_Z)
        UnlockHoveredCell();
}

ModDesc mod_desc = {
    .name = "KARchipelago Debug",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .affects_gameplay = 1,
    .option_desc = &DebugMod_RootOption,
    .OnSaveLoaded = OnSaveLoaded,
    // The toggle rows read cached masks, so re-derive them before a scene can show the menu.
    .OnSceneChange = DebugMenu_RefreshState,
    .OnFrameStart = OnFrameStart,
};
