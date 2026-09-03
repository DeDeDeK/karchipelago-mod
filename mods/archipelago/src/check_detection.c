#include "game.h"
#include "audio.h"
#include "os.h"
#include "inline.h"
#include "code_patch/code_patch.h"
#include "hoshi/func.h"

#include "main.h"
#include "check_detection.h"
#include "checklist_rewards.h"
#include "ap_checklist.h"
#include "ap_check_detect.h"
#include "ap_announce.h"
#include "textbox_api.h"
#include "settings_menu.h"
#include "ap_patches.h"

// SFX cue the vanilla ClearChecker_SetNewUnlock plays on a first-this-frame
// transition, guarded by stc_clearchecker_sfx_last_frame (one-frame cooldown).
#define CHECKLIST_UNLOCK_SFX 0x10008

#define SENT_CHECK_BIT(r, k)  ((ap_save->sent_checks[(r)][(k) >> 6] >> ((k) & 63)) & 1ULL)

// Beat King Dedede goal: CT clear_kind 0x2F
#define KD_CLEAR_KIND 0x2F

// Hydra & Dragoon goal: CT clear_kind 0x77, the "complete both Dragoon and Hydra in
// one match!" cell - NOT the part-unlock cells 0x6D/0x6E (unrelated reward markers).
#define HYDRA_DRAGOON_CLEAR_KIND 0x77

void CheckDetection_EvaluateGoal(void);

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

static inline void ClearSentChecksForRow(int row)
{
    ap_save->sent_checks[row][0] = 0;
    ap_save->sent_checks[row][1] = 0;
    ap_data->sent_checks[row][0] = 0;
    ap_data->sent_checks[row][1] = 0;
}

static inline int PopcountRow(int row)
{
    return Popcount64(ap_save->sent_checks[row][0]) + Popcount64(ap_save->sent_checks[row][1]);
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
        OSReport("[CheckDetection] mode=%d clear_kind=%d type=%s (%d) recorded\n",
                 mode, clear_kind,
                 Reward_TypeName(rtype), rtype);
    }
    else
    {
        OSReport("[CheckDetection] mode=%d clear_kind=%d recorded (no local reward placement)\n",
                 mode, clear_kind);
    }
    if (APAnnounce_LocalEnabled(APLOCAL_CHECK))
        tb_api->EnqueueColoredNoun(NULL, "Check", tb_api->CheckColor, " recorded");

    CheckDetection_EvaluateGoal();
}

// Replacement for ClearChecker_SetNewUnlock (0x8004A054), the funnel most gameplay
// code uses to flag a completed objective. Detect the transition -> RecordCheck,
// then run the vanilla logic so the UI still works.
static void CheckDetection_SetNewUnlockReplacement(int mode, int clear_kind)
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
static void CheckDetection_SetNewUnlockSilentReplacement(int mode, int clear_kind)
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

// Per-row clear_kind of the vanilla "Fill in over 100 Checklist blocks!" cell.
// Returns 0xFF for the AP checklist row, which has no such cell.
static u8 Fill100ClearKind(int row)
{
    switch (row)
    {
    case GMMODE_AIRRIDE:   return AR_CLEAR_FILL_100_BLOCKS;
    case GMMODE_TOPRIDE:   return TR_CLEAR_FILL_100_BLOCKS;
    case GMMODE_CITYTRIAL: return CT_CLEAR_FILL_100_BLOCKS;
    default:               return 0xFF;
    }
}

// Returns 1 if satisfied (or NONE). `count` is the row's popcount, `n` the
// GOAL_N_CHECKLIST threshold.
static int goal_satisfied(APGoalKind goal, int row, int count, int n)
{
    switch (goal)
    {
    case GOAL_NONE:
        return 1;  // vacuously satisfied
    case GOAL_100_CHECKLIST:
    {
        // The "Fill in over 100 Checklist blocks!" cell, not a popcount -
        // GOAL_N_CHECKLIST below is the synthetic count goal.
        u8 k = Fill100ClearKind(row);
        return (k < CLEAR_KIND_NUM) && SENT_CHECK_BIT(row, k);
    }
    case GOAL_N_CHECKLIST:
        return count >= n;
    case GOAL_HYDRA_AND_DRAGOON:
        return SENT_CHECK_BIT(GMMODE_CITYTRIAL, HYDRA_DRAGOON_CLEAR_KIND);
    case GOAL_BEAT_KING_DEDEDE:
        return SENT_CHECK_BIT(GMMODE_CITYTRIAL, KD_CLEAR_KIND);
    case GOAL_CHECKLIST_LIST:
    {
        u64 *gc = ap_save->options.goal_checks[row];
        u64 *sc = ap_save->sent_checks[row];
        return ((sc[0] & gc[0]) == gc[0]) && ((sc[1] & gc[1]) == gc[1]);
    }
    case GOAL_MAX_STATS_CT:
        // Mode-independent sticky save bit, latched during a City Trial round.
        return ap_save->max_stats_ct_achieved;
    case GOAL_ASSEMBLE_AP_STAR:
        return SENT_CHECK_BIT(AP_CHECKLIST_ROW, APCK_ASSEMBLE_AP_STAR);
    case GOAL_ALL_LEGENDARIES_CT:
        return SENT_CHECK_BIT(AP_CHECKLIST_ROW, APCK_ASSEMBLE_ALL_LEGENDARY);
    }
    return 0;
}

// "<Mode> goal complete!", fired per mode as each is finished. Distinct from the
// aggregate "All Goals complete!".
static void AnnounceModeGoal(int row)
{
    static const char *const mode_names[GMMODE_NUM] = {
        [GMMODE_AIRRIDE]   = "Air Ride",
        [GMMODE_TOPRIDE]   = "Top Ride",
        [GMMODE_CITYTRIAL] = "City Trial",
    };
    const char *name;
    GXColor color;
    // ModeColors[] is sized GMMODE_NUM, so the AP row carries its own name and tint.
    if (row == AP_CHECKLIST_ROW)
    {
        static const GXColor ap_theme = {AP_THEME_R, AP_THEME_G, AP_THEME_B, 255};
        name = AP_CHECKLIST_NAME;
        color = ap_theme;
    }
    else
    {
        name = mode_names[row];
        color = tb_api->ModeColors[row];
    }
    TextSegment segs[2] = {
        { name,               color },
        { " goal complete!",  tb_api->GoalColor },
    };
    if (APAnnounce_LocalEnabled(APLOCAL_GOAL))
        tb_api->EnqueueSegments(segs, 2);
    OSReport("[CheckDetection] %s goal satisfied\n", name);
}

void CheckDetection_EvaluateGoal(void)
{
    APSlotOptions *opt = &ap_save->options;

    // Victory needs at least one non-NONE goal, all of them satisfied. If every
    // mode is GOAL_NONE it never fires.
    u8 satisfied_mask = 0;
    int any_real_goal = 0;
    int all_ok = 1;
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
    {
        APGoalKind goal = (APGoalKind)opt->goal[r];
        int sat = goal_satisfied(goal, r, PopcountRow(r), opt->checklist_amount[r]);
        if (goal != GOAL_NONE)
        {
            any_real_goal = 1;
            if (sat)
                satisfied_mask |= (u8)(1 << r);
        }
        if (!sat)
            all_ok = 0;
    }
    // Published before the sticky return so a save load repopulates it after victory.
    ap_data->goal_satisfied_mask = satisfied_mask;

    if (ap_save->goal_complete)
        return;  // sticky once set

    if (any_real_goal && all_ok)
    {
        ap_save->goal_complete = 1;
        ap_data->goal_complete = 1;
        // Suppress the per-mode message for the final mode, so it doesn't double
        // up with the aggregate "All Goals complete!".
        for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
            if (opt->goal[r] != GOAL_NONE)
                ap_save->goal_announced[r] = 1;
        OSReport("[CheckDetection] GOALS COMPLETE\n");
        if (APAnnounce_LocalEnabled(APLOCAL_GOAL))
            tb_api->EnqueueColoredNoun(NULL, "All Goals", tb_api->GoalColor, " complete!");
        return;
    }

    // Victory not reached yet: announce each mode goal that just became satisfied.
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
    {
        if (!(satisfied_mask & (1 << r)) || ap_save->goal_announced[r])
            continue;
        ap_save->goal_announced[r] = 1;
        AnnounceModeGoal(r);
    }
}

// Apply bits the client wrote into ap_data->client_backfill: sent_checks bit,
// clear[] is_unlocked/is_visible, optional has_reward, goal re-eval.
static void ProcessBackfill(void)
{
    int has_data = 0;
    for (int r = 0; r < CHECKLIST_MODE_NUM && !has_data; r++)
    {
        if (ap_data->client_backfill[r][0] | ap_data->client_backfill[r][1])
            has_data = 1;
    }
    if (!has_data)
        return;

    int processed_any = 0;
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
            GameClearData *cd = gmGetClearcheckerTypeP((GameMode)mode);

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

                processed_any = 1;
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

    if (processed_any)
    {
        OSReport("[CheckDetection] Backfill applied (%d new check(s))\n", backfilled);
        CheckDetection_EvaluateGoal();
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

META_UNLOCK_HANDLER(MetaUnlock_AirRide100,       GMMODE_AIRRIDE,   0x18)
META_UNLOCK_HANDLER(MetaUnlock_TopRide100,       GMMODE_TOPRIDE,   0x77)
META_UNLOCK_HANDLER(MetaUnlock_CityTrial100,     GMMODE_CITYTRIAL, 0x37)
META_UNLOCK_HANDLER(MetaUnlock_CityTrialDragoon, GMMODE_CITYTRIAL, 0x6D)
META_UNLOCK_HANDLER(MetaUnlock_CityTrialHydra,   GMMODE_CITYTRIAL, 0x6E)

// The clobbered `stb` stores 1 via a volatile reg (r4 / r0), which the epilogue
// re-materializes so the accept (return-0) auto-re-execute lands the 1 after `bl`.
// Reject (return-1) jumps to the function tail, bypassing the store and the
// display_state update.
#define META_SKIP_EXIT 0x8017f394

// 0x8017efc0: stb r4, 148(r30)   - AR: Complete 100 checkboxes (clear_kind 0x18)
CODEPATCH_HOOKCONDITIONALCREATE(0x8017efc0, "", MetaUnlock_AirRide100,       "li 4, 1\n\t", 0, META_SKIP_EXIT)

// 0x8017eff8: stb r4, 243(r30)   - TR: Complete 100 checkboxes (clear_kind 0x77)
CODEPATCH_HOOKCONDITIONALCREATE(0x8017eff8, "", MetaUnlock_TopRide100,       "li 4, 1\n\t", 0, META_SKIP_EXIT)

// 0x8017f030: stb r4, 179(r30)   - CT: Complete 100 checkboxes (clear_kind 0x37)
CODEPATCH_HOOKCONDITIONALCREATE(0x8017f030, "", MetaUnlock_CityTrial100,     "li 4, 1\n\t", 0, META_SKIP_EXIT)

// 0x8017f0ac: stb r0, 233(r30)   - CT: Unlock Dragoon Parts (clear_kind 0x6D), not the goal cell 0x77
CODEPATCH_HOOKCONDITIONALCREATE(0x8017f0ac, "", MetaUnlock_CityTrialDragoon, "li 0, 1\n\t", 0, META_SKIP_EXIT)

// 0x8017f120: stb r0, 234(r30)   - CT: Unlock Hydra Parts (clear_kind 0x6E), not the goal cell 0x77
CODEPATCH_HOOKCONDITIONALCREATE(0x8017f120, "", MetaUnlock_CityTrialHydra,   "li 0, 1\n\t", 0, META_SKIP_EXIT)

// Filler gate (Checklist_Think case 8), replacing vanilla's 3 hardcoded slot rejects:
// under reward shuffle those cells may hold legitimate rewards, so reject a filler
// only on a cell that would satisfy the active mode's goal without doing the objective.
// Returns 1 to reject (caller branches to errorNoise), 0 to accept. phys_slot is the
// cursor's grid position (row + col*12), which grid_mapping[] maps a clear_kind to.
static int FillerGate_IsRejected(u8 mode, u8 phys_slot)
{
    // Any custom tab other than the AP one has no AP goal to protect.
    int row = ChecklistModeRow(mode);
    if (row < 0)
        return 0;

    GameClearData *cd = gmGetClearcheckerTypeP((GameMode)mode);
    if (!cd)
        return 0;

    switch ((APGoalKind)ap_save->options.goal[row])
    {
    case GOAL_100_CHECKLIST:
    {
        // A filler on the "Fill in over 100 Checklist blocks!" cell would satisfy the
        // goal without filling 100 boxes. Nothing to protect on the AP row, which has
        // no such cell (AP 100-block goals are rejected at generation instead).
        u8 k = Fill100ClearKind(row);
        return (k < CLEAR_KIND_NUM) && cd->grid_mapping[k] == phys_slot;
    }
    case GOAL_HYDRA_AND_DRAGOON:
        if (row != GMMODE_CITYTRIAL)
            return 0;
        return cd->grid_mapping[HYDRA_DRAGOON_CLEAR_KIND] == phys_slot;
    case GOAL_BEAT_KING_DEDEDE:
        if (row != GMMODE_CITYTRIAL)
            return 0;
        return cd->grid_mapping[KD_CLEAR_KIND] == phys_slot;
    case GOAL_ASSEMBLE_AP_STAR:
        if (row != AP_CHECKLIST_ROW)
            return 0;
        return cd->grid_mapping[APCK_ASSEMBLE_AP_STAR] == phys_slot;
    case GOAL_ALL_LEGENDARIES_CT:
        if (row != AP_CHECKLIST_ROW)
            return 0;
        return cd->grid_mapping[APCK_ASSEMBLE_ALL_LEGENDARY] == phys_slot;
    case GOAL_CHECKLIST_LIST:
    {
        u64 *gc = ap_save->options.goal_checks[row];
        for (int w = 0; w < 2; w++)
        {
            u64 bits = gc[w];
            while (bits)
            {
                int bit = __builtin_ctzll(bits);
                bits &= bits - 1;
                if (cd->grid_mapping[(u8)(w * 64 + bit)] == phys_slot)
                    return 1;
            }
        }
        return 0;
    }
    default:
        return 0;  // count-based or NONE: no specific cell to protect
    }
}

// Filler-apply hook. The filler-apply path sets clear[k].is_filler directly without
// calling SetNewUnlock, so the REPLACEFUNC never sees a spent filler.
static void CheckDetection_OnFillerApplied(int mode, int clear_kind)
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
    CheckDetection_OnFillerApplied,
    "",
    0
)

// Hook site 0x80180A64 (vanilla's `lbz r3, 20(r31)` mode-load). The prologue replays
// vanilla's phys_slot computation into the non-volatile r18 where downstream code at
// 0x80180AA4 expects it. Accept (return 0): clobbered insn auto-re-execs (r3 = mode)
// and branches to 0x80180A9C, past vanilla's 3 hardcoded rejects. Reject (return 1):
// branches to 0x80180C24 (errorNoise).
CODEPATCH_HOOKCONDITIONALCREATE(
    0x80180a64,
    "lbz 3, 20(31)\n\t"    // r3 = mode (helper arg 1)
    "lbz 0, 24(31)\n\t"    // r0 = col
    "extsb 0, 0\n\t"
    "mulli 0, 0, 12\n\t"   // r0 = col * 12
    "lbz 4, 23(31)\n\t"    // r4 = row
    "extsb 4, 4\n\t"
    "add 18, 4, 0\n\t"     // r18 = row + col*12 = phys_slot (non-volatile)
    "mr 4, 18\n\t"         // r4 = phys_slot (helper arg 2)
    "clrlwi 4, 4, 24\n\t", // r4 = phys_slot & 0xFF
    FillerGate_IsRejected,
    "",
    0x80180a9c,            // accept: skip vanilla immediate rejects
    0x80180c24             // reject: errorNoise
)

void CheckDetection_OnFrameStart(void)
{
    ProcessBackfill();
}

void CheckDetection_OnSaveLoaded(void)
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
    CheckDetection_EvaluateGoal();

    OSReport("[CheckDetection] Loaded sent_checks AR=%d TR=%d CT=%d AP=%d goal=%d\n",
             PopcountRow(GMMODE_AIRRIDE),
             PopcountRow(GMMODE_TOPRIDE),
             PopcountRow(GMMODE_CITYTRIAL),
             PopcountRow(AP_CHECKLIST_ROW),
             ap_save->goal_complete);
}

void CheckDetection_OnBoot(void)
{
    CODEPATCH_REPLACEFUNC(ClearChecker_SetNewUnlock, CheckDetection_SetNewUnlockReplacement);

    // Top Ride checklist objectives commit through the "silent" variant, which
    // bypasses SetNewUnlock entirely - replace it too or every TR check is lost.
    CODEPATCH_REPLACEFUNC(ClearChecker_SetNewUnlockSilent, CheckDetection_SetNewUnlockSilentReplacement);

    // Meta auto-unlocks inside Checklist_ProcessUnlock.
    CODEPATCH_HOOKAPPLY(0x8017efc0);  // AR 100-checklist
    CODEPATCH_HOOKAPPLY(0x8017eff8);  // TR 100-checklist
    CODEPATCH_HOOKAPPLY(0x8017f030);  // CT 100-checklist
    CODEPATCH_HOOKAPPLY(0x8017f0ac);  // CT Dragoon assembly
    CODEPATCH_HOOKAPPLY(0x8017f120);  // CT Hydra assembly

    CODEPATCH_HOOKAPPLY(0x80180a64);  // Goal-aware filler gate
    CODEPATCH_HOOKAPPLY(0x80180dc4);  // Filler-apply: record the check
    OSReport("[CheckDetection] Hooks installed\n");
}

void CheckDetection_ResetAll(void)
{
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
    {
        ClearSentChecksForRow(r);
        ap_save->goal_announced[r] = 0;
    }
    ap_save->goal_complete = 0;
    ap_data->goal_complete = 0;
    ap_data->goal_satisfied_mask = 0;
    ap_save->max_stats_ct_achieved = 0;
    ApPatches_ResetAll();
}

void CheckDetection_DebugClearAll(void)
{
    CheckDetection_ResetAll();
    Hoshi_WriteSave();
    OSReport("[CheckDetection] Debug: cleared all sent_checks and goal_complete\n");
}

void CheckDetection_DebugForceMarkAll(void)
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
    CheckDetection_EvaluateGoal();  // republish goal_satisfied_mask over the forced checks
    ApPatches_DebugForceMarkAll();
    Hoshi_WriteSave();
    OSReport("[CheckDetection] Debug: force-marked all sent_checks and goal_complete\n");
}

void CheckDetection_DebugTriggerGoal(void)
{
    ap_save->goal_complete = 1;
    ap_data->goal_complete = 1;
    // goal_complete asserts every real goal is done, so the per-row mask has to agree -
    // sent_checks are untouched here, so nothing else would set it.
    u8 mask = 0;
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
        if (ap_save->options.goal[r] != GOAL_NONE)
            mask |= (u8)(1 << r);
    ap_data->goal_satisfied_mask = mask;
    Hoshi_WriteSave();
    OSReport("[CheckDetection] Debug: goal_complete forced\n");
}

