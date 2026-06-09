#include "game.h"
#include "os.h"
#include "hoshi/func.h"

#include "main.h"
#include "check_detection.h"
#include "goal_max_stats_ct.h"

// Per-frame proc: if every PATCHKIND_NUM stat is >= the slot's patch-cap
// target, latch the sticky save flag and re-run goal evaluation. Idempotent
// — once the flag is set, the proc early-outs.
static void GoalMaxStatsCT_PerFrame(GOBJ *rg)
{
    if (ap_save->max_stats_ct_achieved)
        return;

    float threshold = (float)ap_save->options.city_trial_patch_cap_amount;
    RiderData *rd = rg->userdata;
    for (int i = 0; i < PATCHKIND_NUM; i++)
    {
        if (rd->stats.values[i] < threshold)
            return;
    }

    ap_save->max_stats_ct_achieved = 1;
    OSReport("[GoalMaxStatsCT] Player %d hit %d on all %d stats — goal latched\n",
             rd->ply + 1, (int)threshold, PATCHKIND_NUM);
    CheckDetection_EvaluateGoal();
    Hoshi_WriteSave();
}

void GoalMaxStatsCT_On3DLoadEnd(void)
{
    // Only active when this slot's CT goal is GOAL_MAX_STATS_CT — no point
    // running the per-frame check for slots that can't goal off it.
    if (ap_save->options.goal[GMMODE_CITYTRIAL] != GOAL_MAX_STATS_CT)
        return;

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
    OSReport("[GoalMaxStatsCT] Active (%d players, target %d)\n",
             attached, (int)ap_save->options.city_trial_patch_cap_amount);
}

// Multiplier applied to +1 patch and All-Up spawn weights while the Max Stats
// Insanity goal is active, so patches dominate the rolls without fully
// suppressing other drops. Sole tuning knob — raise if throughput is too low.
#define MAX_STATS_PATCH_BIAS 8

static int IsPatchOrAllUpItemKind(u8 it_kind)
{
    switch (it_kind)
    {
        case ITKIND_WEIGHT:
        case ITKIND_ACCEL:
        case ITKIND_TOPSPEED:
        case ITKIND_TURN:
        case ITKIND_CHARGE:
        case ITKIND_GLIDE:
        case ITKIND_OFFENSE:
        case ITKIND_DEFENSE:
        case ITKIND_HP:
        case ITKIND_ALLUP:
            return 1;
        default:
            return 0;
    }
}

static u8 ScaleU8(u8 v)
{
    int s = (int)v * MAX_STATS_PATCH_BIAS;
    if (s > 255) s = 255;
    return (u8)s;
}

static u16 ScaleU16(u16 v)
{
    int s = (int)v * MAX_STATS_PATCH_BIAS;
    if (s > 65535) s = 65535;
    return (u16)s;
}

static void BiasBoxPool(u8 *kinds, u8 *chances, u8 num)
{
    for (u8 i = 0; i < num; i++)
    {
        if (IsPatchOrAllUpItemKind(kinds[i]))
            chances[i] = ScaleU8(chances[i]);
    }
}

void GoalMaxStatsCT_ApplyDropBias(void)
{
    if (ap_save->options.goal[GMMODE_CITYTRIAL] != GOAL_MAX_STATS_CT)
        return;

    OSReport("[GoalMaxStatsCT] Applying %dx patch/All-Up drop bias\n", MAX_STATS_PATCH_BIAS);

    grBoxGeneObj *obj = *stc_grBoxGeneObj;
    if (obj)
    {
        for (int box = 0; box < BOXKIND_NUM; box++)
        {
            BiasBoxPool(obj->item_group_spawn[box].it_kind,
                        obj->item_group_spawn[box].chance,
                        obj->item_group_spawn[box].num);
        }
        BiasBoxPool(obj->sameitem_it_kind, obj->sameitem_chance, obj->sameitem_num);
        BiasBoxPool(obj->subsequent_it_kind, obj->subsequent_chance, obj->subsequent_num);
    }

    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (info && info->item_desc)
    {
        for (int i = 0; i < info->item_desc->event_source_drop_num; i++)
        {
            if (!IsPatchOrAllUpItemKind((u8)info->item_desc->event_source_drop[i].it_kind))
                continue;
            info->item_desc->event_source_drop[i].chance_dyna         = ScaleU16(info->item_desc->event_source_drop[i].chance_dyna);
            info->item_desc->event_source_drop[i].chance_tac          = ScaleU16(info->item_desc->event_source_drop[i].chance_tac);
            info->item_desc->event_source_drop[i].chance_meteor       = ScaleU16(info->item_desc->event_source_drop[i].chance_meteor);
            info->item_desc->event_source_drop[i].chance_destructible = ScaleU16(info->item_desc->event_source_drop[i].chance_destructible);
            info->item_desc->event_source_drop[i].chance_chamber      = ScaleU16(info->item_desc->event_source_drop[i].chance_chamber);
            info->item_desc->event_source_drop[i].chance_ufo          = ScaleU16(info->item_desc->event_source_drop[i].chance_ufo);
        }
    }
}
