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

#define KD_CLEAR_KIND CT_CLEAR_STD_VS_DEDEDE_1MIN

// The "complete both Dragoon and Hydra in one match!" cell, not the part-unlock cells
// CT_CLEAR_UNLOCK_DRAGOON / CT_CLEAR_UNLOCK_HYDRA (unrelated reward markers).
#define HYDRA_DRAGOON_CLEAR_KIND CT_CLEAR_COMPLETE_DRAGOON_AND_HYDRA

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
static int GoalSatisfied(APGoalKind goal, int row, int count, int n)
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
        // An empty list is a row that was never given a list, not one already met.
        // A subset test against zero is vacuously true and would hand out victory.
        if (!gc[0] && !gc[1])
            return 0;
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
        { " goal complete!",  APColor_Goal },
    };
    if (APAnnounce_LocalEnabled(APLOCAL_GOAL))
        tb_api->EnqueueSegments(segs, 2);
    OSReport("[APGoal] %s goal satisfied\n", name);
}

void APGoal_Evaluate(void)
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
        int sat = GoalSatisfied(goal, r, APChecks_PopcountRow(r), opt->checklist_amount[r]);
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
        OSReport("[APGoal] GOALS COMPLETE\n");
        if (APAnnounce_LocalEnabled(APLOCAL_GOAL))
            tb_api->EnqueueColoredNoun(NULL, "All Goals", APColor_Goal, " complete!");
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

// Filler gate (Checklist_Think case 8), replacing vanilla's 3 hardcoded slot rejects:
// under reward shuffle those cells may hold legitimate rewards, so reject a filler
// only on a cell that would satisfy the active mode's goal without doing the objective.
// Returns 1 to reject (caller branches to errorNoise), 0 to accept. phys_slot is the
// cursor's grid position (col + row*12), which grid_mapping[] maps a clear_kind to.
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

// Hook site 0x80180A64 (vanilla's `lbz r3, 20(r31)` mode-load). The prologue replays
// vanilla's phys_slot computation into the non-volatile r18 where downstream code at
// 0x80180AA4 expects it. Accept (return 0): clobbered insn auto-re-execs (r3 = mode)
// and branches to 0x80180A9C, past vanilla's 3 hardcoded rejects. Reject (return 1):
// branches to 0x80180C24 (errorNoise).
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
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
        ap_save->goal_announced[r] = 0;
    ap_save->goal_complete = 0;
    ap_data->goal_complete = 0;
    ap_data->goal_satisfied_mask = 0;
    ap_save->max_stats_ct_achieved = 0;
}

int APGoal_Get(int row, int *out_amount)
{
    if (row < 0 || row >= CHECKLIST_MODE_NUM)
        return GOAL_NONE;
    if (out_amount)
        *out_amount = (int)ap_save->options.checklist_amount[row];
    return (int)ap_save->options.goal[row];
}

// Goals the seed did not ship, for testing the predicates without re-rolling one.
// All four rows at once, because evaluation is over the whole set: a half-applied set
// can satisfy every row and hand out a victory the caller never asked for. `amount`
// reaches only the rows actually on the count goal, so the others keep their own.
// GOAL_MAX_STATS_CT is the exception to taking effect here: its rider proc is armed at
// round load against this same option, so it starts watching from the next round.
void APGoal_DebugSetGoals(const int *goals, int amount)
{
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
    {
        int goal = goals[r];
        if (goal < 0 || goal > GOAL_NONE)
            continue;
        ap_save->options.goal[r] = (u32)goal;
        if (goal == GOAL_N_CHECKLIST && amount > 0)
            ap_save->options.checklist_amount[r] = (u32)amount;
    }

    APGoal_Evaluate();
    Hoshi_WriteSave();
}

void APGoal_DebugComplete(void)
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
    OSReport("[APGoal] Debug: goal_complete forced\n");
}
