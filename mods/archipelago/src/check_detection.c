#include "game.h"
#include "audio.h"
#include "os.h"
#include "inline.h"
#include "code_patch/code_patch.h"
#include "hoshi/func.h"

#include "main.h"
#include "check_detection.h"
#include "checklist_rewards.h"
#include "textbox_api.h"

// SFX cue played by the vanilla ClearChecker_SetNewUnlock on a first-this-frame
// transition. Guarded by stc_clearchecker_sfx_last_frame (one-frame cooldown).
#define CHECKLIST_UNLOCK_SFX 0x10008

// Meta auto-unlock checkboxes bypass ClearChecker_SetNewUnlock entirely — the
// vanilla game sets their clear[] bytes via direct stores inside
// Checklist_ProcessUnlock (0x8017e490), reached from Checklist_Think case 1
// when the checklist is entered with a qualifying condition already true.
// We hook each of the 5 store sites directly instead of polling per-frame.
//
// Layout: r30 = GameClearData* (from gmGetClearcheckerTypeP), the store's
// immediate (e.g. 148) is (0x7C + clear_kind). Each store is a single 1-byte
// `stb` of a register holding 1 — after `bl` clobbers the volatile reg we
// restore it in the epilogue so the trampoline's auto-re-execute writes 1.

// Bit accessor for sent_checks[mode][2] u64 packing. Mode m, clear_kind k in
// [0..CLEAR_KIND_NUM-1]: word index = k / 64, bit index = k % 64.
#define SENT_CHECK_BIT(m, k)  ((ap_save->sent_checks[(m)][(k) >> 6] >> ((k) & 63)) & 1ULL)

// Beat King Dedede goal: CT clear_kind 0x2F
#define KD_CLEAR_KIND 0x2F

// Hydra & Dragoon goal: CT clear_kind 0x77 — the single "In one match, complete both Dragoon and
// Hydra!" gameplay checkbox. This is NOT the two "Unlock <machine> Parts on the Checklist!" cells
// (0x6D/0x6E), which auto-complete from receiving the part rewards and are unrelated to this goal.
#define HYDRA_DRAGOON_CLEAR_KIND 0x77

// Forward declaration: defined mid-file, called from earlier helpers.
void CheckDetection_EvaluateGoal(void);

// Set the sent_checks bit in both save and the shared-memory mirror. No-op if
// already set. Returns 1 if newly set, 0 if already set (for callers that
// want to detect transitions).
static inline int SetSentCheck(u8 mode, u8 clear_kind)
{
    u64 bit = 1ULL << (clear_kind & 63);
    int word = clear_kind >> 6;
    if (ap_save->sent_checks[mode][word] & bit)
        return 0;
    ap_save->sent_checks[mode][word] |= bit;
    ap_data->sent_checks[mode][word] |= bit;
    return 1;
}

// Clear sent_checks for a single mode in both save and mirror.
static inline void ClearSentChecksForMode(u8 mode)
{
    ap_save->sent_checks[mode][0] = 0;
    ap_save->sent_checks[mode][1] = 0;
    ap_data->sent_checks[mode][0] = 0;
    ap_data->sent_checks[mode][1] = 0;
}

// popcount the two u64 words covering the 0..CLEAR_KIND_NUM-1 range.
static inline int PopcountMode(u8 mode)
{
    return Popcount64(ap_save->sent_checks[mode][0])
         + Popcount64(ap_save->sent_checks[mode][1]);
}

// Record a check: set the save bit, mirror to shared memory, re-evaluate goal.
// No-op if the bit was already set. Caller is responsible for bounds checking.
static void RecordCheck(u8 mode, u8 clear_kind)
{
    if (mode >= GMMODE_NUM || clear_kind >= CLEAR_KIND_NUM)
        return;
    if (!SetSentCheck(mode, clear_kind))
        return;

    u8 src_mode, src_ri;
    if (ChecklistRewards_ResolveCell(mode, clear_kind, &src_mode, &src_ri))
    {
        u8 rtype = stc_reward_table_ptrs[src_mode][src_ri].reward_type;
        OSReport("[Check] mode=%d clear_kind=%d type=%s (%d) recorded\n",
                 mode, clear_kind,
                 Reward_TypeName(rtype), rtype);
    }
    else
    {
        OSReport("[Check] mode=%d clear_kind=%d recorded (no local reward placement)\n",
                 mode, clear_kind);
    }
    tb_api->EnqueueColoredNoun(NULL, "Check", tb_api->CheckColor, " sent");

    CheckDetection_EvaluateGoal();
    Hoshi_WriteSave();
}

// Replacement for ClearChecker_SetNewUnlock at 0x8004A054.
// Called whenever any gameplay code completes a checklist objective. We
// detect the moment of transition (bit was previously unset) and record the
// check, then run the original vanilla logic so the in-game checklist UI
// continues to work normally (including the SFX cue with one-frame cooldown).
static void CheckDetection_SetNewUnlockReplacement(int mode, int clear_kind)
{
    if ((unsigned)mode >= GMMODE_NUM || (unsigned)clear_kind >= CLEAR_KIND_NUM)
        return;
    GameClearData *cd = gmGetClearcheckerTypeP(mode);
    if (!cd)
        return;

    int fresh = !cd->clear[clear_kind].is_new && !cd->clear[clear_kind].is_unlocked;

    // Transition detection runs regardless of cache state so AP never misses a check.
    if (fresh)
        RecordCheck((u8)mode, (u8)clear_kind);

    // Vanilla short-circuit: when the unlock cache is valid the rest is a no-op.
    if (Checklist_IsCacheValid() != 0)
        return;

    // Play the unlock SFX at most once per frame (matches vanilla behavior).
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

// Per-mode clear_kind of the "Fill in over 100 Checklist blocks!" cell. This is a
// real vanilla checklist checkbox the game auto-completes once the player fills over
// 100 of that mode's boxes (see the MetaUnlock_*100 hooks below). GOAL_100_CHECKLIST
// keys off this specific cell, distinct from the synthetic popcount GOAL_N_CHECKLIST.
// Returns 0xFF for an unknown mode (callers must range-check before indexing).
static u8 Fill100ClearKind(u8 mode)
{
    switch (mode)
    {
    case GMMODE_AIRRIDE:   return AR_CLEAR_FILL_100_BLOCKS;
    case GMMODE_TOPRIDE:   return TR_CLEAR_FILL_100_BLOCKS;
    case GMMODE_CITYTRIAL: return CT_CLEAR_FILL_100_BLOCKS;
    default:               return 0xFF;
    }
}

// Evaluate a single mode's goal condition. Returns 1 if satisfied (or NONE).
// `mode` is the GameMode index, `count` is the popcount, `n` is the threshold
// for GOAL_N_CHECKLIST.
static int goal_satisfied(APGoalKind goal, u8 mode, int count, int n)
{
    switch (goal)
    {
    case GOAL_NONE:
        return 1;  // vacuously satisfied
    case GOAL_100_CHECKLIST:
    {
        // The actual "Fill in over 100 Checklist blocks!" cell, NOT a popcount of
        // arbitrary boxes. The game auto-checks this cell once over 100 boxes are
        // filled; we bind to it the same way the HYDRA/KD goals bind to their cells.
        // (count is unused here; GOAL_N_CHECKLIST below is the synthetic count goal.)
        u8 k = Fill100ClearKind(mode);
        return (k < CLEAR_KIND_NUM) && SENT_CHECK_BIT(mode, k);
    }
    case GOAL_N_CHECKLIST:
        return count >= n;
    case GOAL_HYDRA_AND_DRAGOON:
        return SENT_CHECK_BIT(GMMODE_CITYTRIAL, HYDRA_DRAGOON_CLEAR_KIND);
    case GOAL_BEAT_KING_DEDEDE:
        return SENT_CHECK_BIT(GMMODE_CITYTRIAL, KD_CLEAR_KIND);
    case GOAL_CHECKLIST_LIST:
    {
        u64 *gc = ap_save->options.goal_checks[mode];
        return ((ap_save->sent_checks[mode][0] & gc[0]) == gc[0])
            && ((ap_save->sent_checks[mode][1] & gc[1]) == gc[1]);
    }
    case GOAL_MAX_STATS_CT:
        // Set by goal_max_stats_ct.c when a human player's CT stats all hit
        // the per-slot patch-cap target in one trial round. Mode-independent.
        return ap_save->max_stats_ct_achieved;
    }
    return 0;
}

void CheckDetection_EvaluateGoal(void)
{
    if (ap_save->goal_complete)
        return;  // sticky once set

    APSlotOptions *opt = &ap_save->options;

    // At least one mode must have a non-NONE goal, and all non-NONE goals
    // must be satisfied. If every mode is GOAL_NONE, victory never fires.
    int any_real_goal = 0;
    int all_ok = 1;
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        if (opt->goal[m] != GOAL_NONE)
            any_real_goal = 1;
        if (!goal_satisfied((APGoalKind)opt->goal[m], (u8)m, PopcountMode((u8)m), opt->checklist_amount[m]))
            all_ok = 0;
    }

    if (any_real_goal && all_ok)
    {
        ap_save->goal_complete = 1;
        ap_data->goal_complete = 1;
        OSReport("[Check] GOALS COMPLETE\n");
        tb_api->EnqueueColoredNoun(NULL, "All Goals", tb_api->GoalColor, " complete!");
        Hoshi_WriteSave();
    }
}

// Process bits the client wrote into ap_data->client_backfill.
// Treats them as additive: each newly set bit triggers a "this checkbox is
// now checked" event with all the side effects (sent_checks bit, clear[]
// is_unlocked, optionally has_reward, goal re-eval).
static void ProcessBackfill(void)
{
    // Quick check: any nonzero word?
    int has_data = 0;
    for (int m = 0; m < GMMODE_NUM && !has_data; m++)
    {
        if (ap_data->client_backfill[m][0] | ap_data->client_backfill[m][1])
            has_data = 1;
    }
    if (!has_data)
        return;

    int processed_any = 0;
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        for (int word = 0; word < 2; word++)
        {
            u64 incoming = ap_data->client_backfill[m][word];
            u64 already  = ap_save->sent_checks[m][word];
            u64 new_bits = incoming & ~already;
            if (!new_bits)
                continue;

            GameClearData *cd = gmGetClearcheckerTypeP((GameMode)m);

            while (new_bits)
            {
                int bit = __builtin_ctzll(new_bits);
                new_bits &= new_bits - 1;
                u8 clear_kind = (u8)(word * 64 + bit);
                if (clear_kind >= CLEAR_KIND_NUM)
                    continue;

                SetSentCheck((u8)m, clear_kind);

                // Set is_unlocked + is_visible on the local checklist for
                // visual consistency (is_visible is what the grid actually
                // renders as revealed), and set has_reward if a local
                // placement exists and its source item has been received.
                if (cd)
                {
                    cd->clear[clear_kind].is_unlocked = 1;
                    cd->clear[clear_kind].is_visible = 1;
                    if (ChecklistRewards_CellHasReceivedReward((u8)m, clear_kind))
                        cd->clear[clear_kind].has_reward = 1;
                }

                processed_any = 1;
            }
        }
    }

    // Clear the backfill field (single-writer protocol — mod consumes, then zero).
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        ap_data->client_backfill[m][0] = 0;
        ap_data->client_backfill[m][1] = 0;
    }

    if (processed_any)
    {
        OSReport("[Check] Backfill processed\n");
        CheckDetection_EvaluateGoal();
        Hoshi_WriteSave();
    }
}

// Meta auto-unlock hooks (Checklist_ProcessUnlock 0x8017e490)
// Thin handlers — each hook knows its (mode, clear_kind) at compile time since
// the five hook sites live at distinct code addresses. Each handler records
// the check (RecordCheck is idempotent — no-op if the sent_checks bit is
// already set) and then decides whether to let vanilla's `stb` run.
//
// Return 0 → let vanilla's auto-unlock `stb` execute normally (clear[k] = 1,
//            wiping any prior bits on the byte). This is the usual path when
//            the cell's auto-unlock condition is newly met.
// Return 1 → SKIP the `stb`, preserving the current byte intact. Used when
//            the cell was already filler'd (is_filler=1): the filler path
//            already drove the cell to a completed state through SetNewUnlock,
//            and letting the store run would wipe the is_filler indicator
//            (and any other bits) that no other code path will re-set.
#define META_UNLOCK_HANDLER(name, mode, kind)                            \
    static int name(void)                                                \
    {                                                                    \
        RecordCheck((mode), (kind));                                     \
        GameClearData *cd = gmGetClearcheckerTypeP((mode));              \
        return (cd && cd->clear[(kind)].is_filler) ? 1 : 0;              \
    }

META_UNLOCK_HANDLER(MetaUnlock_AirRide100,       GMMODE_AIRRIDE,   0x18)
META_UNLOCK_HANDLER(MetaUnlock_TopRide100,       GMMODE_TOPRIDE,   0x77)
META_UNLOCK_HANDLER(MetaUnlock_CityTrial100,     GMMODE_CITYTRIAL, 0x37)
META_UNLOCK_HANDLER(MetaUnlock_CityTrialDragoon, GMMODE_CITYTRIAL, 0x6D)
META_UNLOCK_HANDLER(MetaUnlock_CityTrialHydra,   GMMODE_CITYTRIAL, 0x6E)

// Hook sites. Each clobbered instruction is a one-byte store of 1 via a
// volatile register (r4 for the 100-checklist stores, r0 for the legendary
// assembly stores). The epilogue re-materializes the 1 into that register
// so the trampoline's auto-re-execute of the clobbered `stb` (taken on the
// accept/return-0 path) still lands the correct value after `bl` trashed
// volatiles. On the reject/return-1 path the clobbered `stb` is skipped and
// control jumps directly to the function tail at `0x8017f394`, bypassing both
// the store and the display_state update that follows it — which is the
// correct behavior when the cell is already filler-completed, because the
// filler path already drove both.
#define META_SKIP_EXIT 0x8017f394

// 0x8017efc0: stb r4, 148(r30)   — AR: Complete 100 checkboxes (clear_kind 0x18)
CODEPATCH_HOOKCONDITIONALCREATE(0x8017efc0, "", MetaUnlock_AirRide100,       "li 4, 1\n\t", 0, META_SKIP_EXIT)

// 0x8017eff8: stb r4, 243(r30)   — TR: Complete 100 checkboxes (clear_kind 0x77)
CODEPATCH_HOOKCONDITIONALCREATE(0x8017eff8, "", MetaUnlock_TopRide100,       "li 4, 1\n\t", 0, META_SKIP_EXIT)

// 0x8017f030: stb r4, 179(r30)   — CT: Complete 100 checkboxes (clear_kind 0x37)
CODEPATCH_HOOKCONDITIONALCREATE(0x8017f030, "", MetaUnlock_CityTrial100,     "li 4, 1\n\t", 0, META_SKIP_EXIT)

// 0x8017f0ac: stb r0, 233(r30)   — CT: Unlock Dragoon Parts on the Checklist (clear_kind 0x6D),
// auto-completes once all three Dragoon part rewards are received. Not the goal cell (0x77).
CODEPATCH_HOOKCONDITIONALCREATE(0x8017f0ac, "", MetaUnlock_CityTrialDragoon, "li 0, 1\n\t", 0, META_SKIP_EXIT)

// 0x8017f120: stb r0, 234(r30)   — CT: Unlock Hydra Parts on the Checklist (clear_kind 0x6E),
// auto-completes once all three Hydra part rewards are received. Not the goal cell (0x77).
CODEPATCH_HOOKCONDITIONALCREATE(0x8017f120, "", MetaUnlock_CityTrialHydra,   "li 0, 1\n\t", 0, META_SKIP_EXIT)

// Filler gate (Checklist_Think case 8)
// Vanilla hardcodes a filler reject for physical grid slots 0, 11, and 119
// via 3 immediate compares at 0x80180A74..0x80180A98 inside Checklist_Think.
// Under vanilla's fixed grid_mapping[] those 3 slots correspond to the 5
// meta auto-unlock cells (AR/TR/CT 100-checklist, CT Dragoon, CT Hydra). The
// block exists to stop the player from cheesing auto-unlock cells by spending
// a filler on them.
//
// Under our reward shuffle, that reasoning no longer holds — any of those
// cells may now hold a legitimate shuffled reward that the player should be
// free to filler. So we remove vanilla's gate entirely and substitute a
// goal-aware check: only reject fillers on cells that, if completed, would
// satisfy the active mode's APGoalKind without the player actually doing the
// underlying objective.
//
// GOAL_N_CHECKLIST is a count threshold with no single "goal cell" — filler'ing
// any cell still costs a filler token, so there's no cheese to prevent. GOAL_NONE
// likewise. GOAL_100_CHECKLIST binds to the per-mode "Fill in over 100 Checklist
// blocks!" cell, and GOAL_HYDRA_AND_DRAGOON / GOAL_BEAT_KING_DEDEDE to their
// specific CT clear_kinds, so all three reject a filler on that one goal cell (it
// would otherwise be a free win). GOAL_CHECKLIST_LIST protects every clear_kind
// whose bit is set in goal_checks[mode].

// Returns 1 to reject (caller branches to the errorNoise path), 0 to accept.
// phys_slot is the cursor's physical grid position (row + col*12, column-major
// over the 12x10 grid). Goal cells are defined by clear_kind and translated
// to their current physical slot via grid_mapping[].
static int FillerGate_IsRejected(u8 mode, u8 phys_slot)
{
    if (mode >= GMMODE_NUM)
        return 0;

    GameClearData *cd = gmGetClearcheckerTypeP((GameMode)mode);
    if (!cd)
        return 0;

    APGoalKind goal = (APGoalKind)ap_save->options.goal[mode];
    switch (goal)
    {
    case GOAL_100_CHECKLIST:
    {
        // Protect this mode's "Fill in over 100 Checklist blocks!" cell — it is the
        // goal cell, so a filler on it would satisfy the goal without filling 100 boxes.
        u8 k = Fill100ClearKind(mode);
        return (k < CLEAR_KIND_NUM) && cd->grid_mapping[k] == phys_slot;
    }
    case GOAL_HYDRA_AND_DRAGOON:
        if (mode != GMMODE_CITYTRIAL)
            return 0;
        return cd->grid_mapping[HYDRA_DRAGOON_CLEAR_KIND] == phys_slot;
    case GOAL_BEAT_KING_DEDEDE:
        if (mode != GMMODE_CITYTRIAL)
            return 0;
        return cd->grid_mapping[KD_CLEAR_KIND] == phys_slot;
    case GOAL_CHECKLIST_LIST:
    {
        // Protect every clear_kind whose bit is set in goal_checks[mode].
        u64 *gc = ap_save->options.goal_checks[mode];
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

// Filler-apply hook (Checklist_Think, 0x80180dc4)
// Vanilla's filler-apply path (0x80180dbc: `ori r0, r0, 2; stb r0, 124(r3)`)
// sets clear[k].is_filler=1 directly and does not call ClearChecker_SetNewUnlock,
// so the SetNewUnlock REPLACEFUNC never sees it. Without a separate hook the
// check would never be sent when the player spends a filler. This hook is the
// AP-side completion notifier for the filler path. RecordCheck is idempotent,
// so even repeated firings are harmless. At this point r31 = UI state struct
// (mode at +0x14) and r18 = clear_kind (both non-volatile, held across the
// preceding `bl` to the filler SFX at 0x80180db0).
static void CheckDetection_OnFillerApplied(int mode, int clear_kind)
{
    if ((unsigned)mode >= GMMODE_NUM || (unsigned)clear_kind >= CLEAR_KIND_NUM)
        return;
    RecordCheck((u8)mode, (u8)clear_kind);
}

// Hook site: 0x80180dc4. Clobbered instruction is `lbz r3, 2(r29)` (start of
// the checkbox_filler_num decrement); auto re-execution after the helper
// returns reloads r3 from r29 (non-volatile), so no epilogue is needed.
CODEPATCH_HOOKCREATE(
    0x80180dc4,
    "lbz 3, 20(31)\n\t"   // r3 = mode
    "mr 4, 18\n\t",        // r4 = clear_kind
    CheckDetection_OnFillerApplied,
    "",
    0
)

// Hook site: 0x80180A64 (`lbz r3, 20(r31)` — vanilla's mode load at the top
// of the filler-eligibility block). We chose this site specifically because
// the clobbered instruction naturally restores r3 = mode on the accept path,
// which the downstream `bl gmGetClearcheckerTypeP` at 0x80180A9C needs.
//
// Prologue replays vanilla's full phys_slot computation (col = +0x18, row =
// +0x17, phys_slot = row + col*12), stashing phys_slot in r18 — where the
// downstream code at 0x80180AA4 expects it — because r18 is non-volatile and
// survives the `bl` into our helper. Mode goes in r3, phys_slot in r4 for
// the C call. The call target FillerGate_IsRejected returns 1 to reject.
//
// On accept (return 0):
//   - trampoline runs the clobbered `lbz r3, 20(r31)` (auto re-exec),
//     restoring r3 = mode.
//   - branches to 0x80180A9C, skipping vanilla's 3 hardcoded immediate rejects
//     entirely and landing on the valid-cell path.
// On reject (return 1):
//   - branches to 0x80180C24, the sole playSoundFX_errorNoise call site.
//     errorNoise takes no args (it immediately rewrites r3 to its own SFX id),
//     so our return value in r3 is harmless.
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
    // Mirror save state into shared memory for the client to read.
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        ap_data->sent_checks[m][0] = ap_save->sent_checks[m][0];
        ap_data->sent_checks[m][1] = ap_save->sent_checks[m][1];
    }
    ap_data->goal_complete = ap_save->goal_complete;

    // Initial goal evaluation in case options changed since last boot or
    // saved checks already satisfy the active goal.
    CheckDetection_EvaluateGoal();

    OSReport("[Check] Loaded sent_checks AR=%d TR=%d CT=%d goal=%d\n",
             PopcountMode(GMMODE_AIRRIDE),
             PopcountMode(GMMODE_TOPRIDE),
             PopcountMode(GMMODE_CITYTRIAL),
             ap_save->goal_complete);
}

void CheckDetection_OnBoot(void)
{
    CODEPATCH_REPLACEFUNC(ClearChecker_SetNewUnlock, CheckDetection_SetNewUnlockReplacement);

    // Meta auto-unlock hooks inside Checklist_ProcessUnlock.
    CODEPATCH_HOOKAPPLY(0x8017efc0);  // AR 100-checklist
    CODEPATCH_HOOKAPPLY(0x8017eff8);  // TR 100-checklist
    CODEPATCH_HOOKAPPLY(0x8017f030);  // CT 100-checklist
    CODEPATCH_HOOKAPPLY(0x8017f0ac);  // CT Dragoon assembly
    CODEPATCH_HOOKAPPLY(0x8017f120);  // CT Hydra assembly

    // Filler gate: replace vanilla's 3 hardcoded immediate rejects with a
    // goal-aware check.
    CODEPATCH_HOOKAPPLY(0x80180a64);

    // Filler-apply: record the check when the player spends a filler.
    CODEPATCH_HOOKAPPLY(0x80180dc4);
    OSReport("[Check] Hooks installed\n");
}

void CheckDetection_ResetAll(void)
{
    for (int m = 0; m < GMMODE_NUM; m++)
        ClearSentChecksForMode((u8)m);
    ap_save->goal_complete = 0;
    ap_data->goal_complete = 0;
    ap_save->max_stats_ct_achieved = 0;
}

void CheckDetection_DebugClearAll(void)
{
    CheckDetection_ResetAll();
    Hoshi_WriteSave();
    OSReport("[Check] Debug: cleared all sent_checks and goal_complete\n");
}

void CheckDetection_DebugForceMarkAll(void)
{
    // Set bits 0..CLEAR_KIND_NUM-1 for each mode. With CLEAR_KIND_NUM=120:
    // word 0 holds bits 0..63 (all set), word 1 holds bits 64..119.
    _Static_assert(CLEAR_KIND_NUM > 64 && CLEAR_KIND_NUM <= 128,
                   "clear-kind packing assumes 2 u64 words");
    const u64 lo_mask = ~0ULL;
    const u64 hi_mask = (CLEAR_KIND_NUM == 128) ? ~0ULL
                                                 : ((1ULL << (CLEAR_KIND_NUM - 64)) - 1);
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        ap_save->sent_checks[m][0] = lo_mask;
        ap_save->sent_checks[m][1] = hi_mask;
        ap_data->sent_checks[m][0] = lo_mask;
        ap_data->sent_checks[m][1] = hi_mask;
    }
    ap_save->goal_complete = 1;
    ap_data->goal_complete = 1;
    ap_save->max_stats_ct_achieved = 1;
    Hoshi_WriteSave();
    OSReport("[Check] Debug: force-marked all sent_checks and goal_complete\n");
}

void CheckDetection_DebugTriggerGoal(void)
{
    ap_save->goal_complete = 1;
    ap_data->goal_complete = 1;
    Hoshi_WriteSave();
    OSReport("[Check] Debug: goal_complete forced\n");
}

