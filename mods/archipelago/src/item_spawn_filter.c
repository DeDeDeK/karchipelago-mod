#include "os.h"
#include "game.h"
#include "code_patch/code_patch.h"

#include "item_spawn_filter.h"
#include "gate_abilities.h"
#include "gate_patches.h"
#include "gate_items.h"
#include "goal_max_stats_ct.h"

static int IsItemLocked(u8 it_kind)
{
    return GateAbilities_IsItemLocked(it_kind)
        || GatePatches_IsItemLocked(it_kind)
        || GateItems_IsItemLocked(it_kind);
}

// Stable compaction: survivors keep their order and weights.
static void FilterBoxPool(u8 *kinds, u8 *chances, u8 *pool_num)
{
    u8 num = *pool_num;
    u8 write = 0;

    for (u8 read = 0; read < num; read++)
    {
        if (IsItemLocked(kinds[read]))
            continue;

        if (write != read)
        {
            kinds[write] = kinds[read];
            chances[write] = chances[read];
        }
        write++;
    }

    *pool_num = write;
}

static void FilterBoxPools(void)
{
    grBoxGeneObj *obj = *stc_grBoxGeneObj;
    if (!obj)
        return;

    for (int box = 0; box < BOXKIND_NUM; box++)
    {
        FilterBoxPool(obj->item_group_spawn[box].it_kind,
                      obj->item_group_spawn[box].chance,
                      &obj->item_group_spawn[box].num);
    }

    FilterBoxPool(obj->sameitem_it_kind, obj->sameitem_chance, &obj->sameitem_num);
    FilterBoxPool(obj->subsequent_it_kind, obj->subsequent_chance, &obj->subsequent_num);
}

// event_source_drop has no count to compact, so a row is removed by zeroing every source
// column _CityItem_GetEventItem weighs.
static void FilterEventDrops(void)
{
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (!info || !info->item_desc)
        return;

    for (int i = 0; i < info->item_desc->event_source_drop_num; i++)
    {
        ItemEventSourceDrop *row = &info->item_desc->event_source_drop[i];
        if (!IsItemLocked((u8)row->it_kind))
            continue;

        row->chance_dyna = 0;
        row->chance_tac = 0;
        row->chance_meteor = 0;
        row->chance_destructible = 0;
        row->chance_chamber = 0;
        row->chance_ufo = 0;
    }
}

static void FilterAllSpawnTables(void)
{
    // Before the filters, so injected entries pass through them too.
    GoalMaxStatsCT_EnsureAllUpInPools();

    FilterBoxPools();
    FilterEventDrops();
}

// End of CityItemSpawn_InitItemFallChances (0x800eb374). Clobbered: lwz r0, 0x34(r1).
CODEPATCH_HOOKCREATE(0x800eb558,
    "",
    FilterAllSpawnTables,
    "",
    0
)

// mtlr r0 in the epilogue of CityEvent_ModifyItemFallDesc (0x800ed784). r0 holds the
// caller's LR across the bl.
CODEPATCH_HOOKCREATE(0x800ed7f4,
    "stwu 1, -16(1)\n\t"
    "stw 0, 8(1)\n\t",
    FilterAllSpawnTables,
    "lwz 0, 8(1)\n\t"
    "addi 1, 1, 16\n\t",
    0
)

void ItemSpawnFilter_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x800eb558);
    CODEPATCH_HOOKAPPLY(0x800ed7f4);
    OSReport("[SpawnFilter] Hooks installed\n");
}

// Stadiums and Air Ride never run the CityItemSpawn init path, so neither hook fires
// there.
void ItemSpawnFilter_On3DLoadEnd()
{
    if (!Gm_IsInCity() && *stc_grBoxGeneObj)
    {
        FilterAllSpawnTables();
        OSReport("[SpawnFilter] Filtered spawn tables for non-CT mode (GrKind=%d)\n",
                 Gr_GetCurrentGrKind());
    }
}
