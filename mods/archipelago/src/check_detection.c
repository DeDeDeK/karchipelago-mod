#include "game.h"
#include "audio.h"
#include "os.h"
#include "code_patch/code_patch.h"
#include "hoshi/func.h"

#include "main.h"
#include "check_detection.h"
#include "checklist_rewards.h"  // for cross_mode_slots, reward_counts

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

// Mirror a sent_checks bit into APData (for the client to read).
static inline void MirrorSentCheckBit(u8 mode, u8 clear_kind)
{
    ap_data->sent_checks[mode][clear_kind >> 6] |=
        (1ULL << (clear_kind & 63));
}

// SWAR popcount for u64. Hand-rolled because the freestanding PPC build has
// no libgcc helper (__popcountdi2 is undefined at link time).
static inline int Popcount64(u64 x)
{
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (int)((x * 0x0101010101010101ULL) >> 56);
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
    if (SENT_CHECK_BIT(mode, clear_kind))
        return;

    SET_SENT_CHECK(mode, clear_kind);
    MirrorSentCheckBit(mode, clear_kind);

    OSReport("[Check] mode=%d clear_kind=0x%02x recorded\n", mode, clear_kind);

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
    // Vanilla short-circuit: when the unlock cache is valid, the function is
    // a no-op. We still want to record the check regardless of cache state,
    // so the transition detection runs before the early return.
    GameClearData *cd = NULL;
    if ((unsigned)mode < GMMODE_NUM && (unsigned)clear_kind < CLEAR_KIND_NUM)
    {
        cd = gmGetClearcheckerTypeP(mode);
        if (cd && (cd->clear[clear_kind].is_new == 0)
               && (cd->clear[clear_kind].is_unlocked == 0))
        {
            RecordCheck((u8)mode, (u8)clear_kind);
        }
    }

    // Vanilla logic: bail if cache valid.
    if (Checklist_IsCacheValid() != 0)
        return;

    // Vanilla OOB handling: the original function prints an assert for
    // clear_kind >= 120 but still proceeds to index clear[] — we safely
    // bail here instead.
    if ((unsigned)clear_kind >= CLEAR_KIND_NUM || (unsigned)mode >= GMMODE_NUM)
        return;
    if (!cd)
        return;

    // Play the unlock SFX at most once per frame (matches vanilla behavior).
    if ((cd->clear[clear_kind].is_new == 0) && (cd->clear[clear_kind].is_unlocked == 0))
    {
        int frame = ClearChecker_GetFrameIndex();
        if (*stc_clearchecker_sfx_last_frame != frame)
        {
            SFX_PlayFullVolume(CHECKLIST_UNLOCK_SFX);
            *stc_clearchecker_sfx_last_frame = ClearChecker_GetFrameIndex();
        }
    }

    cd->clear[clear_kind].is_new = 1;
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
        return count >= 100;
    case GOAL_N_CHECKLIST:
        return count >= n;
    case GOAL_HYDRA_AND_DRAGOON:
        return SENT_CHECK_BIT(GMMODE_CITYTRIAL, 0x6D)
            && SENT_CHECK_BIT(GMMODE_CITYTRIAL, 0x6E);
    case GOAL_BEAT_KING_DEDEDE:
        return SENT_CHECK_BIT(GMMODE_CITYTRIAL, KD_CLEAR_KIND);
    case GOAL_CHECKLIST_LIST:
    {
        u64 *gc = ap_save->options.goal_checks[mode];
        return ((ap_save->sent_checks[mode][0] & gc[0]) == gc[0])
            && ((ap_save->sent_checks[mode][1] & gc[1]) == gc[1]);
    }
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
        OSReport("[Check] GOAL COMPLETE\n");
        Hoshi_WriteSave();
    }
}

// Set has_reward on clear[mode][clear_kind] if a received reward is actually
// placed at this checkbox (either same-mode or cross-mode). Mirrors the logic
// in ChecklistRewards_Grant but keyed by checkbox instead of reward index.
static void ApplyBackfillHasReward(GameClearData *cd, u8 mode, u8 clear_kind)
{
    // Cross-mode: a reward from another mode is placed here.
    CrossModeSlot *slot = &cross_mode_slots[mode][clear_kind];
    if (slot->source_mode != 0xFF)
    {
        if (ap_save->received_checklist_rewards[slot->source_mode]
            & (1ULL << slot->source_reward_index))
        {
            cd->clear[clear_kind].has_reward = 1;
        }
        return;
    }

    // Same-mode: scan this mode's save location array for an entry encoding
    // (mode << 8) | clear_kind. Comparing against the full u16 encoding
    // avoids the clear_kind=0 sentinel aliasing that a RewardEntry.clear_kind
    // scan would suffer.
    u16 *save_arr = ap_save->shuffled_rewards[mode];
    if (!save_arr)
        return;
    u16 target = ((u16)mode << 8) | clear_kind;
    int count = reward_counts[mode];
    for (int ri = 0; ri < count; ri++)
    {
        if (save_arr[ri] != target)
            continue;
        if (ap_save->received_checklist_rewards[mode] & (1ULL << ri))
        {
            cd->clear[clear_kind].has_reward = 1;
        }
        return;
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

                // Set the sent_checks bit (save + mirror).
                SET_SENT_CHECK(m, clear_kind);
                MirrorSentCheckBit((u8)m, clear_kind);

                // Set is_unlocked on the local checklist for visual consistency,
                // and set has_reward if a local placement exists and its source
                // item has been received.
                if (cd)
                {
                    cd->clear[clear_kind].is_unlocked = 1;
                    ApplyBackfillHasReward(cd, (u8)m, clear_kind);
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
static int MetaUnlock_AirRide100(void)
{
    RecordCheck(GMMODE_AIRRIDE, 0x18);
    GameClearData *cd = gmGetClearcheckerTypeP(GMMODE_AIRRIDE);
    return (cd && cd->clear[0x18].is_filler) ? 1 : 0;
}
static int MetaUnlock_TopRide100(void)
{
    RecordCheck(GMMODE_TOPRIDE, 0x77);
    GameClearData *cd = gmGetClearcheckerTypeP(GMMODE_TOPRIDE);
    return (cd && cd->clear[0x77].is_filler) ? 1 : 0;
}
static int MetaUnlock_CityTrial100(void)
{
    RecordCheck(GMMODE_CITYTRIAL, 0x37);
    GameClearData *cd = gmGetClearcheckerTypeP(GMMODE_CITYTRIAL);
    return (cd && cd->clear[0x37].is_filler) ? 1 : 0;
}
static int MetaUnlock_CityTrialDragoon(void)
{
    RecordCheck(GMMODE_CITYTRIAL, 0x6D);
    GameClearData *cd = gmGetClearcheckerTypeP(GMMODE_CITYTRIAL);
    return (cd && cd->clear[0x6D].is_filler) ? 1 : 0;
}
static int MetaUnlock_CityTrialHydra(void)
{
    RecordCheck(GMMODE_CITYTRIAL, 0x6E);
    GameClearData *cd = gmGetClearcheckerTypeP(GMMODE_CITYTRIAL);
    return (cd && cd->clear[0x6E].is_filler) ? 1 : 0;
}

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

// 0x8017f0ac: stb r0, 233(r30)   — CT: Dragoon assembly (clear_kind 0x6D)
CODEPATCH_HOOKCONDITIONALCREATE(0x8017f0ac, "", MetaUnlock_CityTrialDragoon, "li 0, 1\n\t", 0, META_SKIP_EXIT)

// 0x8017f120: stb r0, 234(r30)   — CT: Hydra assembly (clear_kind 0x6E)
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
// GOAL_100_CHECKLIST / GOAL_N_CHECKLIST are count thresholds with no single
// "goal cell" — filler'ing any cell (including a meta cell) still costs a
// filler token, so there's no cheese to prevent. GOAL_NONE likewise.
// GOAL_HYDRA_AND_DRAGOON and GOAL_BEAT_KING_DEDEDE protect specific CT
// clear_kinds. GOAL_CHECKLIST_LIST protects every clear_kind whose bit is
// set in goal_checks[mode].

// Returns 1 to reject (caller branches to the errorNoise path), 0 to accept.
// phys_slot is the cursor's physical grid position (row + col*12, column-major
// over the 12x10 grid). Goal cells are defined by clear_kind and translated
// to their current physical slot via grid_mapping[].
static int FillerGate_IsRejected(u8 mode, u8 phys_slot)
{
    if (mode >= GMMODE_NUM)
        return 0;

    APGoalKind goal = (APGoalKind)ap_save->options.goal[mode];

    u8 goal_kinds[CLEAR_KIND_NUM];
    int goal_count = 0;
    switch (goal)
    {
    case GOAL_HYDRA_AND_DRAGOON:
        if (mode == GMMODE_CITYTRIAL)
        {
            goal_kinds[goal_count++] = 0x6D;  // Dragoon assembly
            goal_kinds[goal_count++] = 0x6E;  // Hydra assembly
        }
        break;
    case GOAL_BEAT_KING_DEDEDE:
        if (mode == GMMODE_CITYTRIAL)
            goal_kinds[goal_count++] = KD_CLEAR_KIND;
        break;
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
                goal_kinds[goal_count++] = (u8)(w * 64 + bit);
            }
        }
        break;
    }
    default:
        return 0;  // count-based or NONE: no specific cell to protect
    }

    GameClearData *cd = gmGetClearcheckerTypeP((GameMode)mode);
    if (!cd)
        return 0;
    for (int i = 0; i < goal_count; i++)
    {
        if (cd->grid_mapping[goal_kinds[i]] == phys_slot)
            return 1;
    }
    return 0;
}

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
    OSReport("[Check] Hooks installed\n");
}

void CheckDetection_DebugClearAll(void)
{
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        ap_save->sent_checks[m][0] = 0;
        ap_save->sent_checks[m][1] = 0;
        ap_data->sent_checks[m][0] = 0;
        ap_data->sent_checks[m][1] = 0;
    }
    ap_save->goal_complete = 0;
    ap_data->goal_complete = 0;
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
