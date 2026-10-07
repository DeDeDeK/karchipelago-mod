#include "game.h"
#include "os.h"

#include "main.h"
#include "ap_goal.h"
#include "patch_cap.h"
#include "gate_items.h"
#include "goal_max_stats_ct.h"

// All-Up weights where a pool lacks it, sized to sit beside the vanilla +1 patch weights.
#define ALLUP_BOX_POOL_CHANCE      8
#define ALLUP_CHANCE_DESTRUCTIBLE  16
#define ALLUP_CHANCE_DYNA          4

static int GoalActive(void)
{
    return ap_save->options.goal[GMMODE_CITYTRIAL] == GOAL_MAX_STATS_CT &&
           Gm_IsInCity() && Gm_GetCityMode() == CITYMODE_TRIAL;
}

static void GoalMaxStatsCT_PerFrame(GOBJ *rg)
{
    if (ap_save->max_stats_ct_achieved)
        return;

    int target = PatchCap_GetMax();
    RiderData *rd = rg->userdata;
    for (int i = 0; i < PATCHKIND_NUM; i++)
    {
        if (!PatchCap_IsStatAt(rd->stats.values, i, target))
            return;
    }

    ap_save->max_stats_ct_achieved = 1;
    OSReport("[GoalMaxStatsCT] Player %d reached %d patches on all %d stats, goal latched\n",
             rd->ply + 1, target, PATCHKIND_NUM);
    APGoal_Evaluate();
}

void GoalMaxStatsCT_On3DLoadEnd(void)
{
    if (Gm_IsAutoDemo() || !GoalActive() || ap_save->max_stats_ct_achieved)
        return;

    int attached = AP_AttachHumanRiderProcs(GoalMaxStatsCT_PerFrame);
    if (attached)
        OSReport("[GoalMaxStatsCT] Active (%d players, target %d)\n",
                 attached, PatchCap_GetMax());
}

static void EnsureItemInPool(u8 *kinds, u8 *chances, u8 *num, int max_entries, u8 it_kind, u8 weight)
{
    for (u8 i = 0; i < *num; i++)
    {
        if (kinds[i] == it_kind)
            return;
    }
    if (*num >= max_entries)
        return;
    kinds[*num] = it_kind;
    chances[*num] = weight;
    *num += 1;
}

// Makes All Up reachable from every patch source the vanilla tables miss: the three box
// pools, the Same Item and subsequent pools, and the destructible and Dyna Blade columns.
void GoalMaxStatsCT_EnsureAllUpInPools(void)
{
    if (ap_save->options.goal[GMMODE_CITYTRIAL] != GOAL_MAX_STATS_CT)
        return;
    if (GateItems_IsItemLocked(ITKIND_ALLUP))
        return;

    grBoxGeneObj *obj = *stc_grBoxGeneObj;
    if (obj)
    {
        for (int box = 0; box < BOXKIND_NUM; box++)
            EnsureItemInPool(obj->item_group_spawn[box].it_kind, obj->item_group_spawn[box].chance,
                             &obj->item_group_spawn[box].num, sizeof(obj->item_group_spawn[box].it_kind),
                             ITKIND_ALLUP, ALLUP_BOX_POOL_CHANCE);
        EnsureItemInPool(obj->sameitem_it_kind, obj->sameitem_chance, &obj->sameitem_num,
                         sizeof(obj->sameitem_it_kind), ITKIND_ALLUP, ALLUP_BOX_POOL_CHANCE);
        EnsureItemInPool(obj->subsequent_it_kind, obj->subsequent_chance, &obj->subsequent_num,
                         sizeof(obj->subsequent_it_kind), ITKIND_ALLUP, ALLUP_BOX_POOL_CHANCE);
    }

    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (info && info->item_desc)
    {
        for (int i = 0; i < info->item_desc->event_source_drop_num; i++)
        {
            ItemEventSourceDrop *row = &info->item_desc->event_source_drop[i];
            if (row->it_kind != ITKIND_ALLUP)
                continue;
            if (row->chance_destructible == 0)
                row->chance_destructible = ALLUP_CHANCE_DESTRUCTIBLE;
            if (row->chance_dyna == 0)
                row->chance_dyna = ALLUP_CHANCE_DYNA;
            break;
        }
    }
}
