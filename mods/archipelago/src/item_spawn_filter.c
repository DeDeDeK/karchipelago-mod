#include "os.h"
#include "game.h"
#include "code_patch/code_patch.h"

#include "item_spawn_filter.h"
#include "gate_abilities.h"
#include "gate_patches.h"
#include "gate_items.h"
#include "goal_max_stats_ct.h"

// Runs from hooks after the game populates the item spawn tables. Each gate file
// filters its own item categories out of both box pools and event drop pools.
static void FilterAllSpawnTables()
{
    OSReport("[SpawnFilter] FilterAllSpawnTables called (GrKind=%d, StageKind=%d)\n",
             Gr_GetCurrentGrKind(), Gm_GetCurrentStageKind());

    // Before the filters, so injected entries pass through them too.
    GateItems_EnsureAllUpInSpawnPools();

    // Box spawn pools (grBoxGeneObj)
    GateAbilities_FilterSpawnTables();
    GatePatches_FilterSpawnTables();
    GateItems_FilterSpawnTables();

    // Event drop pools (grBoxGeneInfo - Tac, meteor, pillar, chamber, UFO, misc)
    GateAbilities_FilterEventDropTables();
    GatePatches_FilterEventDropTables();
    GateItems_FilterEventDropTables();

    // After the gate filters, so the multiplier isn't spent on entries that are
    // about to be removed or zeroed.
    GoalMaxStatsCT_ApplyDropBias();
}

// End of CityItemSpawn_InitItemFallChances. Clobbered: lwz r0, 0x34(r1)
CODEPATCH_HOOKCREATE(0x800eb558,
    "",
    FilterAllSpawnTables,
    "",
    0
)

// End of CityEvent_ModifyItemFallDesc. Clobbered: lwz r0, 0x14(r1)
CODEPATCH_HOOKCREATE(0x800ed7f0,
    "",
    FilterAllSpawnTables,
    "",
    0
)

void ItemSpawnFilter_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x800eb558);
    CODEPATCH_HOOKAPPLY(0x800ed7f0);
}

void ItemSpawnFilter_On3DLoadEnd()
{
    // Stadium and Air Ride never run the CityItemSpawn init path, so neither hook
    // above fires there.
    if (!Gm_IsInCity() && *stc_grBoxGeneObj)
    {
        OSReport("[SpawnFilter] Filtering spawn tables for non-CT mode (GrKind=%d)\n",
                 Gr_GetCurrentGrKind());
        FilterAllSpawnTables();
    }
}
