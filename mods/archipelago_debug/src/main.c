#include "os.h"
#include "game.h"
#include "hsd.h"
#include "hoshi/mod.h"
#include "hoshi/func.h"

#include "archipelago_api.h"
#include "debug_menu.h"

const ArchipelagoAPI *ap_api = 0;

static void TryImportApi(void)
{
    if (!ap_api)
        ap_api = (const ArchipelagoAPI *)Hoshi_ImportMod(
            (char *)ARCHIPELAGO_MOD_NAME,
            ARCHIPELAGO_API_MAJOR, ARCHIPELAGO_API_MINOR);
}

static void OnSaveLoaded(void)
{
    TryImportApi();
    if (!ap_api)
    {
        OSReport("[ApDebug] failed to import KARchipelago API\n");
        return;
    }
    // Reflect archipelago's masks into our local toggle state so the menu
    // matches reality after AP grants from save load.
    DebugMenu_RefreshStateFromMasks();
    OSReport("[ApDebug] API imported, debug menu ready\n");
}

static void OnFrameStart(void)
{
    if (!ap_api)
        return;

    HSD_Pad *pad = &stc_engine_pads[0];

    if (pad->down & PAD_BUTTON_DPAD_LEFT)
    {
        // Build a pool of every remote-placed reward (shuffled == 0xFFFF),
        // pick one at random, grant it.
        u16 pool[256];
        int n = 0;
        for (int m = 0; m < GMMODE_NUM; m++)
        {
            int count = ap_api->GetRewardCount((GameMode)m);
            for (int ri = 0; ri < count && n < (int)(sizeof(pool) / sizeof(pool[0])); ri++)
            {
                if (ap_api->GetShuffledReward((GameMode)m, (u8)ri) == 0xFFFF)
                    pool[n++] = (u16)((m << 8) | ri);
            }
        }
        if (n == 0)
        {
            OSReport("[ApDebug] no remote-placed rewards available\n");
        }
        else
        {
            u16 picked = pool[HSD_Randi(n)];
            ap_api->GrantReward((GameMode)(picked >> 8), (u8)(picked & 0xFF));
            OSReport("[ApDebug] granted random remote-placed reward\n");
        }
    }

    if (pad->down & PAD_BUTTON_DPAD_DOWN)
    {
        ap_api->DebugTriggerDeathlinkReceive();
        OSReport("[ApDebug] triggered deathlink_receive\n");
    }

    if (pad->down & PAD_BUTTON_DPAD_UP)
    {
        ap_api->DebugTriggerTraplinkReceive();
        OSReport("[ApDebug] triggered traplink_receive\n");
    }

    if (pad->down & PAD_TRIGGER_Z)
    {
        // In a checklist menu, Z unlocks the currently hovered cell as if the
        // objective had been completed in-game. Z is unused by vanilla
        // checklist navigation. ClearChecker_SetNewUnlock is REPLACEFUNC'd by
        // archipelago's check_detection, so the AP check fires and goal
        // evaluation runs.
        u8 mode, k;
        if (!ap_api->GetHoveredCell(&mode, &k))
        {
            OSReport("[ApDebug] Z pressed but no cell hovered yet (move cursor first)\n");
            return;
        }

        u8 src_mode, src_ri;
        int has_placement = ap_api->ResolveCell(mode, k, &src_mode, &src_ri);
        if (has_placement)
        {
            u8 rtype = stc_reward_table_ptrs[src_mode][src_ri].reward_type;
            OSReport("[ApDebug] Z unlock mode=%d clear_kind=%d -> %s ri=%d type=%s (0x%02x)\n",
                     mode, k,
                     (src_mode == mode) ? "same-mode" : "cross-mode",
                     src_ri, Reward_TypeName(rtype), rtype);
        }
        else
        {
            OSReport("[ApDebug] Z unlock mode=%d clear_kind=%d (no local reward placement)\n",
                     mode, k);
        }

        ClearChecker_SetNewUnlock(mode, k);
        // SetNewUnlock bails when Checklist_IsCacheValid (always true in
        // menus), so RecordCheck ran but no clear[] bits got written. Set the
        // end-state bits directly.
        GameClearData *cd = gmGetClearcheckerTypeP(mode);
        cd->clear[k].is_new = 1;
        cd->clear[k].is_unlocked = 1;
        cd->clear[k].is_visible = 1;

        if (has_placement && DebugMenu_ShouldAutoGrantOnUnlock())
        {
            OSReport("[ApDebug] auto-granting reward mode=%d ri=%d\n",
                     src_mode, src_ri);
            ap_api->GrantReward(src_mode, src_ri);
            Hoshi_WriteSave();
        }
    }
}

ModDesc mod_desc = {
    .name = "KARchipelago Debug",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .option_desc = &DebugMod_RootOption,
    .OnSaveLoaded = OnSaveLoaded,
    .OnFrameStart = OnFrameStart,
};
