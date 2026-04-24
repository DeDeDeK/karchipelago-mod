#include "game.h"
#include "os.h"
#include "hoshi/func.h"

#include "main.h"
#include "check_detection.h"
#include "goal_max_stats_ct.h"

// Per-frame proc: if every PATCHKIND_NUM stat is >= PATCH_STAT_MAX, latch the
// sticky save flag and re-run goal evaluation. Idempotent — once the flag is
// set, the proc early-outs.
static void GoalMaxStatsCT_PerFrame(GOBJ *rg)
{
    if (ap_save->max_stats_ct_achieved)
        return;

    RiderData *rd = rg->userdata;
    for (int i = 0; i < PATCHKIND_NUM; i++)
    {
        if (rd->stats.values[i] < (float)PATCH_STAT_MAX)
            return;
    }

    ap_save->max_stats_ct_achieved = 1;
    OSReport("[GoalMaxStatsCT] Player %d hit max on all %d stats — goal latched\n",
             rd->ply + 1, PATCHKIND_NUM);
    CheckDetection_EvaluateGoal();
    Hoshi_WriteSave();
}

void GoalMaxStatsCT_On3DLoadEnd(void)
{
    // Trial mode only — Free Run and Stadium don't count as a "CT run".
    if (!Gm_IsInCity() || Gm_GetCityMode() != CITYMODE_TRIAL)
        return;

    // Sticky flag — nothing to do if already achieved.
    if (ap_save->max_stats_ct_achieved)
        return;

    int attached = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *r = Ply_GetRiderGObj(i);
        if (!r)
            continue;
        GObj_AddProc(r, GoalMaxStatsCT_PerFrame, RDPRI_HITCOLL + 1);
        attached++;
    }
    OSReport("[GoalMaxStatsCT] Active (%d players)\n", attached);
}
