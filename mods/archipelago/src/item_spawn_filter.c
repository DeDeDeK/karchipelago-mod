#include "os.h"
#include "game.h"
#include "code_patch/code_patch.h"

#include "item_spawn_filter.h"
#include "gate_abilities.h"
#include "gate_patches.h"
#include "gate_items.h"
#include "gate_boxes.h"

// Central spawn table filter — called from hooks after the game populates
// item spawn tables (CityItemSpawn_InitItemFallChances, CityEvent_ModifyItemFallDesc).
// Each gate file filters its own item categories from both box pools and event drop pools.
static void FilterAllSpawnTables()
{
    OSReport("[SpawnFilter] FilterAllSpawnTables called (GrKind=%d, StageKind=%d)\n",
             Gm_GetCurrentGrKind(), Gm_GetCurrentStageKind());

    // Box spawn pools (grBoxGeneObj)
    GateAbilities_FilterSpawnTables();
    GatePatches_FilterSpawnTables();
    GateItems_FilterSpawnTables();

    // Event drop pools (grBoxGeneInfo — Tac, meteor, pillar, chamber, UFO, misc)
    GateAbilities_FilterEventDropTables();
    GatePatches_FilterEventDropTables();
    GateItems_FilterEventDropTables();

    // Update which box types still have valid items after filtering
    GateBoxes_UpdateItemAvailability();
}

// Hook at end of CityItemSpawn_InitItemFallChances (0x800eb558).
// Clobbered instruction: lwz r0, 0x34(r1)
CODEPATCH_HOOKCREATE(0x800eb558,
    "",
    FilterAllSpawnTables,
    "",
    0
)

// Hook at end of CityEvent_ModifyItemFallDesc (0x800ed7f0).
// Clobbered instruction: lwz r0, 0x14(r1)
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
    // For stadium/Air Ride modes, the CityItemSpawn init path doesn't run,
    // so the hooks at 0x800eb558/0x800ed7f0 never fire. Filter here instead.
    if (!Gm_IsInCity() && *stc_grBoxGeneObj)
    {
        OSReport("[SpawnFilter] Filtering spawn tables for non-CT mode (GrKind=%d)\n",
                 Gm_GetCurrentGrKind());
        FilterAllSpawnTables();
    }
}
