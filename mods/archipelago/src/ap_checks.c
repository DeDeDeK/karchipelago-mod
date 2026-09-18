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

// SFX cue the vanilla ClearChecker_SetNewUnlock plays on a first-this-frame
// transition, guarded by stc_clearchecker_sfx_last_frame (one-frame cooldown).
#define CHECKLIST_UNLOCK_SFX 0x10008

// Set the sent_checks bit in both save and the shared-memory mirror. Returns 1 if
// newly set. `row` is a ChecklistModeRow() result.
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

// Record a check: set the save bit, mirror to shared memory, re-evaluate goal.
// Idempotent. Deliberately does not write the card - Hoshi_WriteSave rewrites the
// whole file synchronously and stalls the frame, and checks are recorded mid-run.
// The bits live in ap_save until the game's own save point flushes them.
static void RecordCheck(int mode, int clear_kind)
{
    int row = ChecklistModeRow(mode);
    if (row < 0 || (unsigned)clear_kind >= CLEAR_KIND_NUM)
        return;
    // Only the AP tab's first APCK_NUM cells back an AP location, and the filler
    // cursor can reach the blank ones. Recording those would send a location code
    // the multiworld has never heard of.
    if (row == AP_CHECKLIST_ROW && clear_kind >= APCK_NUM)
        return;
    if (!SetSentCheck(row, (u8)clear_kind))
        return;

    u8 src_mode, src_ri;
    if (ChecklistRewards_ResolveCell(mode, clear_kind, &src_mode, &src_ri))
    {
        u8 rtype = stc_reward_table_ptrs[src_mode][src_ri].reward_type;
        OSReport("[APChecks] mode=%d clear_kind=%d type=%s (%d) recorded\n",
                 mode, clear_kind,
                 Reward_TypeName(rtype), rtype);
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

// Replacement for ClearChecker_SetNewUnlock (0x8004A054), the funnel most gameplay
// code uses to flag a completed objective. Detect the transition -> RecordCheck,
// then run the vanilla logic so the UI still works.
static void APChecks_SetNewUnlockReplacement(int mode, int clear_kind)
{
    // ChecklistModeRow accepts the AP-checklist mode too, not just the 3 real modes:
    // the AP-checklist evaluator drives completions through here.
    if (ChecklistModeRow(mode) < 0 || (unsigned)clear_kind >= CLEAR_KIND_NUM)
        return;
    GameClearData *cd = gmGetClearcheckerTypeP(mode);
    if (!cd)
        return;

    int fresh = !cd->clear[clear_kind].is_new && !cd->clear[clear_kind].is_unlocked;

    // Transition detection runs regardless of cache state so AP never misses a check.
    if (fresh)
        RecordCheck(mode, clear_kind);

    // Vanilla short-circuit: when the unlock cache is valid the rest is a no-op.
    if (Checklist_IsCacheValid() != 0)
        return;

    // Vanilla plays the unlock SFX at most once per frame.
    if (fresh)
    {
        int frame = ClearChecker_GetFrameIndex();
        if (*stc_clearchecker_sfx_last_frame != frame)
        {
            SFX_PlayFullVolume(CHECKLIST_UNLOCK_SFX);
            *stc_clearchecker_sfx_last_frame = frame;
        }
    }

    cd->clear[clear_kind].is_new = 1;
}

// Replacement for ClearChecker_SetNewUnlockSilent (0x80049FCC). Top Ride commits
// every check through this "silent" variant, not SetNewUnlock, so without this every
// TR check is dropped. The SFX is omitted because the caller already played it.
static void APChecks_SetNewUnlockSilentReplacement(int mode, int clear_kind)
{
    if ((unsigned)mode >= GMMODE_NUM || (unsigned)clear_kind >= CLEAR_KIND_NUM)
        return;
    GameClearData *cd = gmGetClearcheckerTypeP(mode);
    if (!cd)
        return;

    int fresh = !cd->clear[clear_kind].is_new && !cd->clear[clear_kind].is_unlocked;

    // Transition detection runs regardless of cache state so AP never misses a check.
    if (fresh)
        RecordCheck(mode, clear_kind);

    // Vanilla short-circuit: when the unlock cache is valid the store is skipped.
    if (Checklist_IsCacheValid() != 0)
        return;

    cd->clear[clear_kind].is_new = 1;
}

// Apply bits the client wrote into ap_data->client_backfill: sent_checks bit,
// clear[] is_unlocked/is_visible, optional has_reward, goal re-eval. Called only under
// ap_data->backfill_valid, so the arrays are whole.
void APChecks_ApplyBackfill(void)
{
    int backfilled = 0;
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
    {
        for (int word = 0; word < 2; word++)
        {
            u64 incoming = ap_data->client_backfill[r][word];
            u64 already  = ap_save->sent_checks[r][word];
            u64 new_bits = incoming & ~already;
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
                if (r == AP_CHECKLIST_ROW && clear_kind >= APCK_NUM)
                    continue; // blank AP cells back no location, same as RecordCheck

                SetSentCheck(r, clear_kind);

                // is_visible is what the grid renders as revealed.
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

    // Single-writer protocol: the mod consumes, then zeroes.
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

// Meta auto-unlock handlers (Checklist_ProcessUnlock 0x8017e490): five cells whose
// clear[] byte vanilla sets via direct `stb`, bypassing SetNewUnlock. Return 0 lets
// the `stb` run, 1 skips it - skip iff the cell is already is_filler, since the store
// would wipe a filler byte no other path re-sets. The skip must set is_unlocked
// itself: the store site's `!is_unlocked` guard otherwise stays true forever, and
// Checklist_Think re-enters ProcessUnlock every frame with the screen taking no input.
#define META_UNLOCK_HANDLER(name, mode, kind)                            \
    static int name(void)                                                \
    {                                                                    \
        RecordCheck((mode), (kind));                                     \
        GameClearData *cd = gmGetClearcheckerTypeP((mode));              \
        if (!cd || !cd->clear[(kind)].is_filler)                          \
            return 0;                                                    \
        cd->clear[(kind)].is_unlocked = 1;                               \
        return 1;                                                        \
    }

META_UNLOCK_HANDLER(MetaUnlock_AirRide100,       GMMODE_AIRRIDE,   AR_CLEAR_FILL_100_BLOCKS)
META_UNLOCK_HANDLER(MetaUnlock_TopRide100,       GMMODE_TOPRIDE,   TR_CLEAR_FILL_100_BLOCKS)
META_UNLOCK_HANDLER(MetaUnlock_CityTrial100,     GMMODE_CITYTRIAL, CT_CLEAR_FILL_100_BLOCKS)
META_UNLOCK_HANDLER(MetaUnlock_CityTrialDragoon, GMMODE_CITYTRIAL, CT_CLEAR_UNLOCK_DRAGOON)
META_UNLOCK_HANDLER(MetaUnlock_CityTrialHydra,   GMMODE_CITYTRIAL, CT_CLEAR_UNLOCK_HYDRA)

// Each site is the `li r3, 1` that carries ProcessUnlock's return value, one
// instruction ahead of the `stb` that sets the clear[] byte. Hooking the `li` rather
// than the `stb` is what keeps the return value alive: the bl destroys r3, and only
// the relocated `li` puts it back. The epilogue re-materializes the volatile register
// the following `stb` stores through (r4 / r0), which the bl also destroys.
// Accept (return 0): epilogue, relocated `li r3, 1`, then on to the `stb`.
// Reject (return 1): straight to the function tail, skipping both, returning the
// handler's own 1 in r3.
#define META_SKIP_EXIT 0x8017f394

// AR: Complete 100 checkboxes (clear_kind 0x18). Next insn: stb r4, 148(r30)
CODEPATCH_HOOKCONDITIONALCREATE(0x8017efbc, "", MetaUnlock_AirRide100,       "li 4, 1\n\t", 0, META_SKIP_EXIT)

// TR: Complete 100 checkboxes (clear_kind 0x77). Next insn: stb r4, 243(r30)
CODEPATCH_HOOKCONDITIONALCREATE(0x8017eff4, "", MetaUnlock_TopRide100,       "li 4, 1\n\t", 0, META_SKIP_EXIT)

// CT: Complete 100 checkboxes (clear_kind 0x37). Next insn: stb r4, 179(r30)
CODEPATCH_HOOKCONDITIONALCREATE(0x8017f02c, "", MetaUnlock_CityTrial100,     "li 4, 1\n\t", 0, META_SKIP_EXIT)

// CT: Unlock Dragoon Parts (clear_kind 0x6D), not the goal cell 0x77. Next insn: stb r0, 233(r30)
CODEPATCH_HOOKCONDITIONALCREATE(0x8017f0a8, "", MetaUnlock_CityTrialDragoon, "li 0, 1\n\t", 0, META_SKIP_EXIT)

// CT: Unlock Hydra Parts (clear_kind 0x6E), not the goal cell 0x77. Next insn: stb r0, 234(r30)
CODEPATCH_HOOKCONDITIONALCREATE(0x8017f11c, "", MetaUnlock_CityTrialHydra,   "li 0, 1\n\t", 0, META_SKIP_EXIT)

// Filler-apply hook. The filler-apply path sets clear[k].is_filler directly without
// calling SetNewUnlock, so the REPLACEFUNC never sees a spent filler.
static void APChecks_OnFillerApplied(int mode, int clear_kind)
{
    RecordCheck(mode, clear_kind);
}

// Hook site: 0x80180dc4 in Checklist_Think, where r31 = UI state (mode at +0x14) and
// r18 = clear_kind. Clobbered instruction is `lbz r3, 2(r29)` (start of the
// checkbox_filler_num decrement); auto re-execution reloads r3 from the non-volatile
// r29, so no epilogue is needed.
CODEPATCH_HOOKCREATE(
    0x80180dc4,
    "lbz 3, 20(31)\n\t"   // r3 = mode
    "mr 4, 18\n\t",        // r4 = clear_kind
    APChecks_OnFillerApplied,
    "",
    0
)

void APChecks_OnSaveLoaded(void)
{
    // Mirror into shared memory for the client to read.
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
    {
        ap_data->sent_checks[r][0] = ap_save->sent_checks[r][0];
        ap_data->sent_checks[r][1] = ap_save->sent_checks[r][1];
    }
    ap_data->goal_complete = ap_save->goal_complete;

    // Covers options changing since last boot, or saved checks already satisfying
    // the active goal.
    APGoal_Evaluate();

    OSReport("[APChecks] Loaded sent_checks AR=%d TR=%d CT=%d AP=%d goal=%d\n",
             APChecks_PopcountRow(GMMODE_AIRRIDE),
             APChecks_PopcountRow(GMMODE_TOPRIDE),
             APChecks_PopcountRow(GMMODE_CITYTRIAL),
             APChecks_PopcountRow(AP_CHECKLIST_ROW),
             ap_save->goal_complete);
}

void APChecks_OnBoot(void)
{
    CODEPATCH_REPLACEFUNC(ClearChecker_SetNewUnlock, APChecks_SetNewUnlockReplacement);

    // Top Ride checklist objectives commit through the "silent" variant, which
    // bypasses SetNewUnlock entirely - replace it too or every TR check is lost.
    CODEPATCH_REPLACEFUNC(ClearChecker_SetNewUnlockSilent, APChecks_SetNewUnlockSilentReplacement);

    // Meta auto-unlocks inside Checklist_ProcessUnlock.
    CODEPATCH_HOOKAPPLY(0x8017efbc);  // AR 100-checklist
    CODEPATCH_HOOKAPPLY(0x8017eff4);  // TR 100-checklist
    CODEPATCH_HOOKAPPLY(0x8017f02c);  // CT 100-checklist
    CODEPATCH_HOOKAPPLY(0x8017f0a8);  // CT Dragoon assembly
    CODEPATCH_HOOKAPPLY(0x8017f11c);  // CT Hydra assembly

    CODEPATCH_HOOKAPPLY(0x80180dc4);  // Filler-apply: record the check
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
    ApPatches_ResetAll();
}

void APChecks_DebugClearAll(void)
{
    APChecks_ResetAll();
    Hoshi_WriteSave();
    OSReport("[APChecks] Debug: cleared all sent_checks and goal_complete\n");
}

void APChecks_DebugForceMarkAll(void)
{
    _Static_assert(CLEAR_KIND_NUM > 64 && CLEAR_KIND_NUM <= 128,
                   "clear-kind packing assumes 2 u64 words");
    _Static_assert(APCK_NUM <= 64, "the AP row's mask below assumes one word");
    const u64 lo_mask = ~0ULL;
    const u64 hi_mask = (CLEAR_KIND_NUM == 128) ? ~0ULL
                                                 : ((1ULL << (CLEAR_KIND_NUM - 64)) - 1);
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
    {
        // Only the AP tab's first APCK_NUM cells back a location, and its blank
        // ones decode into the AP Patch code block, so they must stay clear.
        int ap = (r == AP_CHECKLIST_ROW);
        ap_save->sent_checks[r][0] = ap ? (1ULL << APCK_NUM) - 1 : lo_mask;
        ap_save->sent_checks[r][1] = ap ? 0 : hi_mask;
        ap_data->sent_checks[r][0] = ap_save->sent_checks[r][0];
        ap_data->sent_checks[r][1] = ap_save->sent_checks[r][1];
        ap_save->goal_announced[r] = 1;
    }
    ap_save->goal_complete = 1;
    ap_data->goal_complete = 1;
    ap_save->max_stats_ct_achieved = 1;
    APGoal_Evaluate();  // republish goal_satisfied_mask over the forced checks
    ApPatches_DebugForceMarkAll();
    Hoshi_WriteSave();
    OSReport("[APChecks] Debug: force-marked all sent_checks and goal_complete\n");
}
