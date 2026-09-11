#include "os.h"
#include "game.h"
#include "hsd.h"
#include "hoshi/mod.h"
#include "hoshi/func.h"

#include "archipelago_api.h"
#include "custom_events_api.h"
#include "custom_machines_api.h"
#include "debug_menu.h"

const ArchipelagoAPI *ap_api = 0;
static const CustomEventsAPI *ce_api = 0;

// Which Archipelago Star sphere the next R + D-Pad Down drops. Per round.
static int next_sphere;

static void TryImportApi(void)
{
    if (!ap_api)
        ap_api = (const ArchipelagoAPI *)Hoshi_ImportMod(
            (char *)ARCHIPELAGO_MOD_NAME,
            ARCHIPELAGO_API_MAJOR, ARCHIPELAGO_API_MINOR);
    if (!ce_api)
        ce_api = (const CustomEventsAPI *)Hoshi_ImportMod(
            (char *)CUSTOM_EVENTS_MOD_NAME,
            CUSTOM_EVENTS_API_MAJOR, CUSTOM_EVENTS_API_MINOR);
    DebugMenu_BindCustomMachines((const CustomMachinesAPI *)Hoshi_ImportMod(
        (char *)CUSTOM_MACHINES_MOD_NAME,
        CUSTOM_MACHINES_API_MAJOR, CUSTOM_MACHINES_API_MINOR));
}

static void OnSaveLoaded(void)
{
    TryImportApi();
    if (!ap_api)
    {
        OSReport("[ApDebug] Failed to import KARchipelago API\n");
        return;
    }
    if (!ce_api)
        OSReport("[ApDebug] custom_events API unavailable, Scale Change trigger disabled\n");
    DebugMenu_RefreshState();
    OSReport("[ApDebug] API imported, debug menu ready\n");
}

// The toggle rows read their category mask through a cached array, so re-derive it
// before a scene that can put the settings menu on screen.
static void OnSceneChange(void)
{
    DebugMenu_RefreshState();
}

static void On3DLoadEnd(void)
{
    next_sphere = 0;
}

// One reward the seed placed remotely (shuffled == 0xFFFF), picked uniformly by
// reservoir so no candidate list has to be built on the stack.
static void GrantRandomRemoteReward(void)
{
    int n = 0;
    u8 pick_mode = 0, pick_ri = 0;

    for (int m = 0; m < GMMODE_NUM; m++)
    {
        int count = ap_api->GetRewardCount((GameMode)m);
        for (int ri = 0; ri < count; ri++)
        {
            if (ap_api->GetShuffledReward((GameMode)m, (u8)ri) != 0xFFFF)
                continue;
            if (HSD_Randi(++n) == 0)
            {
                pick_mode = (u8)m;
                pick_ri = (u8)ri;
            }
        }
    }

    if (n == 0)
    {
        OSReport("[ApDebug] No remote-placed rewards available\n");
        return;
    }
    ap_api->GrantReward((GameMode)pick_mode, pick_ri);
    OSReport("[ApDebug] Granted remote-placed reward mode=%d ri=%d\n", pick_mode, pick_ri);
}

// A locked sphere has no ItemKind and is skipped rather than stalling the cycle.
static void SpawnNextApStarPiece(void)
{
    int spawned = ap_api->DebugSpawnApStarPiece(next_sphere, DebugMenu_TargetPlayer());
    OSReport("[ApDebug] AP Star sphere %d %s\n", next_sphere,
             spawned ? "spawned" : "not available");
    next_sphere = (next_sphere + 1) % AP_STAR_PIECE_NUM;
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
    // SetNewUnlock bails when Checklist_IsCacheValid (always true in menus), so
    // RecordCheck ran but no clear[] bits were written; set the end state here.
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

// Both the hoshi settings menu and the checklist grid navigate on the D-Pad, so the
// round bindings below have to stay out of every menu scene. The major test also
// keeps them out of the title screen's attract demo, which runs a real round under
// MJRKIND_TITLE. Top Ride gameplay is minor 19, Air Ride and City Trial minor 18.
static int InRound(void)
{
    MinorKind minor = Scene_GetCurrentMinor();
    if (minor != MNRKIND_3D && minor != MNRKIND_19)
        return 0;

    MajorKind major = Scene_GetCurrentMajor();
    return major == MJRKIND_CITY || major == MJRKIND_AIR || major == MJRKIND_TOP;
}

// Game_Think ends each mode's round on an unsigned seconds_passed >= time_seconds
// (Game_Think+0xa48), so zeroing the configured limit runs the engine's own timeout
// path: results, stadium selection and every round-end check fire as they normally do.
// A lap-limited Air Ride race ends on its lap count instead and is unaffected.
static void EndRoundNow(void)
{
    Gm_GetGameData()->time_seconds = 0;
    OSReport("[ApDebug] Zeroed the round time limit\n");
}

static void OnFrameStart(void)
{
    if (!ap_api)
        return;

    HSD_Pad *pad = &stc_engine_pads[0];

    if (pad->down & PAD_TRIGGER_Z)
        UnlockHoveredCell();

    if (!InRound())
        return;

    if (pad->down & PAD_BUTTON_DPAD_LEFT)
    {
        if (pad->held & PAD_TRIGGER_L)
            EndRoundNow();
        else
            GrantRandomRemoteReward();
    }

    if (pad->down & PAD_BUTTON_DPAD_RIGHT)
        DebugMenu_GiveRandomUnlock();

    if (pad->down & PAD_BUTTON_DPAD_DOWN)
    {
        if (pad->held & PAD_TRIGGER_L)
        {
            // Held rather than consumed until a round state that can apply it.
            ap_api->DebugTriggerDeathlinkReceive();
            OSReport("[ApDebug] Armed deathlink_receive\n");
        }
        else if (pad->held & PAD_TRIGGER_R)
        {
            SpawnNextApStarPiece();
        }
        else
        {
            // The drop-ins are held out of the registry while ap_patches is 0, and an
            // unregistered item has no ItemKind until the round reloads with it set.
            if (ap_api->DebugSpawnApBox(DebugMenu_TargetPlayer()))
                OSReport("[ApDebug] AP Box spawned\n");
            else
                OSReport("[ApDebug] No AP Box registered this round (ap_patches = %d)\n",
                         ap_api->GetApPatchCount());
        }
    }

    if (pad->down & PAD_BUTTON_DPAD_UP)
    {
        if (pad->held & PAD_TRIGGER_L)
        {
            DebugMenu_GiveRandomModeItem(Scene_GetCurrentMajor());
        }
        else if (pad->held & PAD_TRIGGER_R)
        {
            // The receive proc only exists while Trap Link was on as the scene loaded;
            // otherwise the flag sits armed until a round that has one.
            ap_api->DebugTriggerTraplinkReceive();
            OSReport("[ApDebug] Armed traplink_receive\n");
        }
        else
        {
            // Do() returns 0 with no eventcheck GObj (outside City Trial) or when an
            // event is already running.
            if (ce_api && ce_api->Do(CUSTOM_EVKIND_SCALE_CHANGE))
                OSReport("[ApDebug] Triggered Scale Change event\n");
            else
                OSReport("[ApDebug] Scale Change trigger unavailable, needs City Trial with no active event\n");
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
    .OnSceneChange = OnSceneChange,
    .On3DLoadEnd = On3DLoadEnd,
    .OnFrameStart = OnFrameStart,
};
