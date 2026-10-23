#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"
#include "hoshi/func.h"

#include "main.h"
#include "ap_goal.h"
#include "ap_checks.h"
#include "ap_checklist.h"
#include "ap_check_detect.h"
#include "ap_announce.h"
#include "ap_colors.h"
#include "textbox_api.h"

// The row's vanilla "Fill in over 100 Checklist blocks!" cell, or 0xFF on the AP row.
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

// count is the row's popcount, n the GOAL_N_CHECKLIST threshold.
static int GoalSatisfied(APGoalKind goal, int row, int count, int n)
{
    switch (goal)
    {
    case GOAL_100_CHECKLIST:
    {
        // The vanilla 100-blocks cell; GOAL_N_CHECKLIST is the popcount goal.
        u8 k = Fill100ClearKind(row);
        return (k < CLEAR_KIND_NUM) && SENT_CHECK_BIT(row, k);
    }
    case GOAL_N_CHECKLIST:
        return count >= n;
    case GOAL_HYDRA_AND_DRAGOON:
        return SENT_CHECK_BIT(GMMODE_CITYTRIAL, CT_CLEAR_COMPLETE_DRAGOON_AND_HYDRA);
    case GOAL_BEAT_KING_DEDEDE:
        return SENT_CHECK_BIT(GMMODE_CITYTRIAL, CT_CLEAR_STD_VS_DEDEDE_1MIN);
    case GOAL_CHECKLIST_LIST:
    {
        u64 *gc = ap_save->options.goal_checks[row];
        u64 *sc = ap_save->sent_checks[row];
        // An empty list means none was given; the subset test would pass vacuously.
        if (!gc[0] && !gc[1])
            return 0;
        return ((sc[0] & gc[0]) == gc[0]) && ((sc[1] & gc[1]) == gc[1]);
    }
    case GOAL_MAX_STATS_CT:
        return ap_save->max_stats_ct_achieved;
    case GOAL_ASSEMBLE_AP_STAR:
        return SENT_CHECK_BIT(AP_CHECKLIST_ROW, APCK_ASSEMBLE_AP_STAR);
    case GOAL_ALL_LEGENDARIES_CT:
        return SENT_CHECK_BIT(AP_CHECKLIST_ROW, APCK_ASSEMBLE_ALL_LEGENDARY);
    default:
        return 0;
    }
}

// Rows with a goal; GOAL_NONE rows count as met.
static u8 RealGoalRows(void)
{
    u8 rows = 0;
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
        if (ap_save->options.goal[r] != GOAL_NONE)
            rows |= (u8)(1 << r);
    return rows;
}

// Victory needs at least one real goal, every one of them met.
static int GoalComplete(void)
{
    u8 real = RealGoalRows();
    return real && (ap_save->goal_latched & real) == real;
}

static void PublishGoals(void)
{
    ap_data->goal_satisfied_mask = ap_save->goal_latched & RealGoalRows();
    ap_data->goal_complete = GoalComplete();
}

static void AnnounceRowGoal(int row)
{
    TextSegment segs[2] = {
        { APChecklist_RowName(row), APChecklist_RowColor(row) },
        { " goal complete!",        APColor_Goal },
    };
    if (APAnnounce_LocalEnabled(APLOCAL_GOAL))
        tb_api->EnqueueSegments(segs, 2);
    OSReport("[APGoal] %s goal met\n", APChecklist_RowName(row));
}

void APGoal_Evaluate(void)
{
    // Every goal reads 0, GOAL_100_CHECKLIST, until the slot options arrive.
    if (!ap_save->options_received)
        return;

    int was_complete = GoalComplete();
    u8 fresh = 0;
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
    {
        APGoalKind goal = (APGoalKind)ap_save->options.goal[r];
        if (goal == GOAL_NONE || (ap_save->goal_latched & (1 << r)))
            continue;
        if (GoalSatisfied(goal, r, APChecks_PopcountRow(r), ap_save->options.checklist_amount[r]))
            fresh |= (u8)(1 << r);
    }
    ap_save->goal_latched |= fresh;
    PublishGoals();

    if (!fresh)
        return;

    // The aggregate line stands in for the last row's own.
    if (ap_data->goal_complete && !was_complete)
    {
        OSReport("[APGoal] All goals completed\n");
        if (APAnnounce_LocalEnabled(APLOCAL_GOAL))
            tb_api->EnqueueColoredNoun(NULL, "All Goals", APColor_Goal, " complete!");
        return;
    }

    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
        if (fresh & (1 << r))
            AnnounceRowGoal(r);
}

// Replaces Checklist_Think's 3 hardcoded filler rejects, which under reward shuffle can
// hold real rewards: a filler is refused only on a cell that would satisfy the row's goal
// without the objective. Returns 1 to refuse. phys_slot = col + row*12, what grid_mapping[]
// maps a clear_kind to.
static int FillerGate_IsRejected(u8 mode, u8 phys_slot)
{
    // A custom tab other than the AP one has no goal to protect.
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
        u8 k = Fill100ClearKind(row);
        return (k < CLEAR_KIND_NUM) && cd->grid_mapping[k] == phys_slot;
    }
    case GOAL_HYDRA_AND_DRAGOON:
        if (row != GMMODE_CITYTRIAL)
            return 0;
        return cd->grid_mapping[CT_CLEAR_COMPLETE_DRAGOON_AND_HYDRA] == phys_slot;
    case GOAL_BEAT_KING_DEDEDE:
        if (row != GMMODE_CITYTRIAL)
            return 0;
        return cd->grid_mapping[CT_CLEAR_STD_VS_DEDEDE_1MIN] == phys_slot;
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
        return 0;
    }
}

// Hook at 0x80180a64 (lbz r3, 20(r31)) in Checklist_Think (0x8017f3bc). The prologue
// replays vanilla's phys_slot computation into r18, where 0x80180aa4 expects it. Accept
// re-runs the clobbered load and goes past the vanilla rejects; refuse goes to errorNoise.
CODEPATCH_HOOKCONDITIONALCREATE(
    0x80180a64,
    "lbz 3, 20(31)\n\t"    // r3 = mode (helper arg 1)
    "lbz 0, 24(31)\n\t"    // r0 = cursor_row
    "extsb 0, 0\n\t"
    "mulli 0, 0, 12\n\t"   // r0 = row * 12
    "lbz 4, 23(31)\n\t"    // r4 = cursor_col
    "extsb 4, 4\n\t"
    "add 18, 4, 0\n\t"     // r18 = col + row*12 = phys_slot (non-volatile)
    "mr 4, 18\n\t"         // r4 = phys_slot (helper arg 2)
    "clrlwi 4, 4, 24\n\t", // r4 = phys_slot & 0xFF
    FillerGate_IsRejected,
    "",
    0x80180a9c,            // accept: skip vanilla immediate rejects
    0x80180c24             // reject: errorNoise
)

void APGoal_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x80180a64);
    OSReport("[APGoal] Hooks installed\n");
}

void APGoal_Reset(void)
{
    ap_save->goal_latched = 0;
    ap_save->max_stats_ct_achieved = 0;
    PublishGoals();
}

int APGoal_Get(int row, int *out_amount)
{
    if (row < 0 || row >= CHECKLIST_MODE_NUM)
        return GOAL_NONE;
    if (out_amount)
        *out_amount = (int)ap_save->options.checklist_amount[row];
    return (int)ap_save->options.goal[row];
}

// All rows at once: evaluation runs over the whole set, so a partial set could award
// victory. A row whose goal changes is judged afresh. GOAL_MAX_STATS_CT arms its rider
// proc at round load, so it takes effect from the next round.
void APGoal_DebugSetGoals(const int *goals, int amount)
{
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
    {
        int goal = goals[r];
        if (goal < 0 || goal > GOAL_NONE)
            continue;
        if ((u32)goal != ap_save->options.goal[r])
            ap_save->goal_latched &= (u8)~(1 << r);
        ap_save->options.goal[r] = (u32)goal;
        if (goal == GOAL_N_CHECKLIST && amount > 0)
            ap_save->options.checklist_amount[r] = (u32)amount;
    }

    APGoal_Evaluate();
    Hoshi_WriteSave();
}

void APGoal_DebugComplete(void)
{
    ap_save->goal_latched = (u8)((1 << CHECKLIST_MODE_NUM) - 1);
    PublishGoals();
    Hoshi_WriteSave();
    OSReport("[APGoal] Debug: every goal latched (complete %d)\n", ap_data->goal_complete);
}
