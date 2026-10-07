#include <string.h>

#include "game.h"
#include "audio.h"
#include "os.h"
#include "inline.h"
#include "code_patch/code_patch.h"
#include "hoshi/func.h"

#include "main.h"
#include "ap_checks.h"
#include "ap_goal.h"
#include "checklist_rewards.h"
#include "ap_checklist.h"
#include "ap_check_detect.h"
#include "ap_announce.h"
#include "textbox_api.h"
#include "ap_colors.h"
#include "ap_patches.h"

// Every AP tab cell backs an AP location, so the AP row fills the grid.
_Static_assert(APCK_NUM == CLEAR_KIND_NUM, "the AP tab must fill its grid");

// Returns 1 if newly set. row is a ChecklistModeRow() result.
static inline int SetSentCheck(int row, u8 clear_kind)
{
    u64 bit = 1ULL << (clear_kind & 63);
    int word = clear_kind >> 6;
    if (ap_save->sent_checks[row][word] & bit)
        return 0;
    ap_save->sent_checks[row][word] |= bit;
    ap_data->sent_checks[row][word] |= bit;
    return 1;
}

// Doesn't write the card: Hoshi_WriteSave stalls the frame, so the bits wait in ap_save
// for the game's own save point.
static void RecordCheck(int mode, int clear_kind)
{
    int row = ChecklistModeRow(mode);
    if (row < 0 || (unsigned)clear_kind >= CLEAR_KIND_NUM)
        return;
    if (!SetSentCheck(row, (u8)clear_kind))
        return;

    u8 src_mode, src_ri;
    if (ChecklistRewards_ResolveCell(mode, clear_kind, &src_mode, &src_ri))
    {
        u8 rtype = stc_reward_table_ptrs[src_mode][src_ri].reward_type;
        OSReport("[APChecks] mode=%d clear_kind=%d type=%s (%d) recorded\n",
                 mode, clear_kind, Reward_TypeName(rtype), rtype);
    }
    else
    {
        OSReport("[APChecks] mode=%d clear_kind=%d recorded (no local reward placement)\n",
                 mode, clear_kind);
    }
    if (APAnnounce_LocalEnabled(APLOCAL_CHECK))
        tb_api->EnqueueColoredNoun(NULL, "Check", APColor_Check, " recorded");

    APGoal_Evaluate();
}

// The shared body of both SetNewUnlock replacements: records a first completion, then
// stores is_new unless a LAN session is up. Returns 1 when the vanilla store ran on a
// fresh cell, which is when the loud variant plays its SFX. The AP tab's evaluator
// records through the loud variant, so ChecklistModeRow admits its mode.
static int MarkNewUnlock(GameMode mode, u8 clear_kind)
{
    if (ChecklistModeRow(mode) < 0 || clear_kind >= CLEAR_KIND_NUM)
        return 0;
    GameClearData *cd = gmGetClearcheckerTypeP(mode);
    if (!cd)
        return 0;

    // Recorded before the LAN short-circuit so no check is missed.
    int fresh = !cd->clear[clear_kind].is_new && !cd->clear[clear_kind].is_unlocked;
    if (fresh)
        RecordCheck(mode, clear_kind);

    if (Net_IsSessionActive() != 0)
        return 0;

    cd->clear[clear_kind].is_new = 1;
    return fresh;
}

// Replaces ClearChecker_SetNewUnlock (0x8004a054), the funnel most objectives complete
// through, and reimplements its body. The SFX plays at most once per frame.
static void APChecks_SetNewUnlock(GameMode mode, u8 clear_kind)
{
    if (!MarkNewUnlock(mode, clear_kind))
        return;

    int frame = Gm_GetEngineFrames();
    if (*stc_clearchecker_sfx_last_frame != frame)
    {
        SFX_PlayFullVolume(CLEARCHECKER_UNLOCK_SFX);
        *stc_clearchecker_sfx_last_frame = frame;
    }
}

// Replaces ClearChecker_SetNewUnlockSilent (0x80049fcc), which Top Ride commits every
// check through; its caller has already played the SFX.
static void APChecks_SetNewUnlockSilent(GameMode mode, u8 clear_kind)
{
    MarkNewUnlock(mode, clear_kind);
}

// Sets the sent_checks bit, reveals and unlocks the cell, and badges a received reward.
// Called only under ap_data->backfill_valid, so the arrays are whole.
void APChecks_ApplyBackfill(void)
{
    int backfilled = 0;
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
    {
        for (int word = 0; word < 2; word++)
        {
            u64 new_bits = ap_data->client_backfill[r][word] & ~ap_save->sent_checks[r][word];
            if (!new_bits)
                continue;

            int mode = ChecklistRowMode(r);
            GameClearData *cd = (r == AP_CHECKLIST_ROW && !APChecklist_IsRegistered())
                ? NULL
                : gmGetClearcheckerTypeP((GameMode)mode);

            while (new_bits)
            {
                int bit = __builtin_ctzll(new_bits);
                new_bits &= new_bits - 1;
                u8 clear_kind = (u8)(word * 64 + bit);
                if (clear_kind >= CLEAR_KIND_NUM)
                    continue;

                SetSentCheck(r, clear_kind);

                if (cd)
                {
                    cd->clear[clear_kind].is_unlocked = 1;
                    cd->clear[clear_kind].is_visible = 1;
                    if (ChecklistRewards_CellHasReceivedReward(mode, clear_kind))
                        cd->clear[clear_kind].has_reward = 1;
                }

                backfilled++;
            }
        }
    }

    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
    {
        ap_data->client_backfill[r][0] = 0;
        ap_data->client_backfill[r][1] = 0;
    }

    if (backfilled)
    {
        OSReport("[APChecks] Backfill applied (%d new check(s))\n", backfilled);
        APGoal_Evaluate();
    }
}

// Checklist_ProcessUnlock (0x8017e490) sets these five cells by a direct stb, bypassing
// SetNewUnlock. Returning 1 skips the store on a filler cell, whose byte it would wipe;
// the skip sets is_unlocked itself, or ProcessUnlock re-enters every frame with the
// screen taking no input.
#define META_UNLOCK_HANDLER(name, mode, kind)                            \
    static int name(void)                                                \
    {                                                                    \
        RecordCheck((mode), (kind));                                     \
        GameClearData *cd = gmGetClearcheckerTypeP((mode));              \
        if (!cd || !cd->clear[(kind)].is_filler)                         \
            return 0;                                                    \
        cd->clear[(kind)].is_unlocked = 1;                               \
        return 1;                                                        \
    }

META_UNLOCK_HANDLER(MetaUnlock_AirRide100,       GMMODE_AIRRIDE,   AR_CLEAR_FILL_100_BLOCKS)
META_UNLOCK_HANDLER(MetaUnlock_TopRide100,       GMMODE_TOPRIDE,   TR_CLEAR_FILL_100_BLOCKS)
META_UNLOCK_HANDLER(MetaUnlock_CityTrial100,     GMMODE_CITYTRIAL, CT_CLEAR_FILL_100_BLOCKS)
META_UNLOCK_HANDLER(MetaUnlock_CityTrialDragoon, GMMODE_CITYTRIAL, CT_CLEAR_UNLOCK_DRAGOON)
META_UNLOCK_HANDLER(MetaUnlock_CityTrialHydra,   GMMODE_CITYTRIAL, CT_CLEAR_UNLOCK_HYDRA)

// Each hook replaces the li r3, 1 that carries ProcessUnlock's return value, one ahead of
// the stb, so the relocated li restores what the bl destroys; the epilogue restores the
// stb's source (r4 / r0). A skip exits at the function tail with the handler's 1 in r3.
#define META_SKIP_EXIT 0x8017f394

// AR 100 checkboxes (0x18), then stb r4, 148(r30).
CODEPATCH_HOOKCONDITIONALCREATE(0x8017efbc, "", MetaUnlock_AirRide100,       "li 4, 1\n\t", 0, META_SKIP_EXIT)

// TR 100 checkboxes (0x77), then stb r4, 243(r30).
CODEPATCH_HOOKCONDITIONALCREATE(0x8017eff4, "", MetaUnlock_TopRide100,       "li 4, 1\n\t", 0, META_SKIP_EXIT)

// CT 100 checkboxes (0x37), then stb r4, 179(r30).
CODEPATCH_HOOKCONDITIONALCREATE(0x8017f02c, "", MetaUnlock_CityTrial100,     "li 4, 1\n\t", 0, META_SKIP_EXIT)

// CT Dragoon parts (0x6D), then stb r0, 233(r30).
CODEPATCH_HOOKCONDITIONALCREATE(0x8017f0a8, "", MetaUnlock_CityTrialDragoon, "li 0, 1\n\t", 0, META_SKIP_EXIT)

// CT Hydra parts (0x6E), then stb r0, 234(r30).
CODEPATCH_HOOKCONDITIONALCREATE(0x8017f11c, "", MetaUnlock_CityTrialHydra,   "li 0, 1\n\t", 0, META_SKIP_EXIT)

// The filler-apply path sets is_filler directly, bypassing SetNewUnlock. Hook at 0x80180dc4
// in Checklist_Think (0x8017f3bc): r31 = UI state (mode at +0x14), r18 = clear_kind. The
// clobbered lbz r3, 2(r29) starts the checkbox_filler_num decrement and reloads from r29.
CODEPATCH_HOOKCREATE(
    0x80180dc4,
    "lbz 3, 20(31)\n\t"
    "mr 4, 18\n\t",
    RecordCheck,
    "",
    0
)

void APChecks_OnSaveLoaded(void)
{
    memcpy(ap_data->sent_checks, ap_save->sent_checks, sizeof(ap_data->sent_checks));

    // Publishes the goal state, and latches a goal saved checks already meet.
    APGoal_Evaluate();

    OSReport("[APChecks] Loaded sent_checks AR=%d TR=%d CT=%d AP=%d goal=%d\n",
             APChecks_PopcountRow(GMMODE_AIRRIDE),
             APChecks_PopcountRow(GMMODE_TOPRIDE),
             APChecks_PopcountRow(GMMODE_CITYTRIAL),
             APChecks_PopcountRow(AP_CHECKLIST_ROW),
             ap_data->goal_complete);
}

void APChecks_OnBoot(void)
{
    CODEPATCH_REPLACEFUNC(ClearChecker_SetNewUnlock, APChecks_SetNewUnlock);
    CODEPATCH_REPLACEFUNC(ClearChecker_SetNewUnlockSilent, APChecks_SetNewUnlockSilent);

    CODEPATCH_HOOKAPPLY(0x8017efbc);
    CODEPATCH_HOOKAPPLY(0x8017eff4);
    CODEPATCH_HOOKAPPLY(0x8017f02c);
    CODEPATCH_HOOKAPPLY(0x8017f0a8);
    CODEPATCH_HOOKAPPLY(0x8017f11c);
    CODEPATCH_HOOKAPPLY(0x80180dc4);
    OSReport("[APChecks] Hooks installed\n");
}

void APChecks_ResetAll(void)
{
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
    {
        ap_save->sent_checks[r][0] = 0;
        ap_save->sent_checks[r][1] = 0;
        ap_data->sent_checks[r][0] = 0;
        ap_data->sent_checks[r][1] = 0;
    }
    APGoal_Reset();
}

void APChecks_DebugClearAll(void)
{
    APChecks_ResetAll();
    APPatches_ResetAll();
    Hoshi_WriteSave();
    OSReport("[APChecks] Debug: cleared every sent check, AP Patch and goal\n");
}

void APChecks_DebugForceMarkAll(void)
{
    _Static_assert(CLEAR_KIND_NUM > 64 && CLEAR_KIND_NUM < 128,
                   "clear-kind packing assumes 2 u64 words");
    const u64 hi_mask = (1ULL << (CLEAR_KIND_NUM - 64)) - 1;
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
    {
        ap_save->sent_checks[r][0] = ~0ULL;
        ap_save->sent_checks[r][1] = hi_mask;
        ap_data->sent_checks[r][0] = ap_save->sent_checks[r][0];
        ap_data->sent_checks[r][1] = ap_save->sent_checks[r][1];
    }
    ap_save->max_stats_ct_achieved = 1;
    APPatches_DebugForceMarkAll();
    APGoal_DebugComplete();
    OSReport("[APChecks] Debug: force-marked every check, AP Patch and goal\n");
}
