#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "cpu_ai.h"
#include "cpu_stat_growth.h"

int cpu_stat_pool = CPU_STAT_POOL_DEFAULT;

char *cpu_stat_pool_names[CPU_STAT_POOL_NUM] = {
    [CPU_STAT_POOL_OFF] = "Off",
    [CPU_STAT_POOL_LOW] = "Low",
    [CPU_STAT_POOL_DEFAULT] = "Default",
    [CPU_STAT_POOL_HIGH] = "High",
    [CPU_STAT_POOL_MAX] = "Max",
    [CPU_STAT_POOL_ADAPTIVE] = "Adaptive",
};

// Adaptive starts empty and is refilled before each growth tick.
static const float stc_pool_scale[CPU_STAT_POOL_NUM] = {
    [CPU_STAT_POOL_OFF] = 0.0f,
    [CPU_STAT_POOL_LOW] = 0.5f,
    [CPU_STAT_POOL_DEFAULT] = 1.0f,
    [CPU_STAT_POOL_HIGH] = 1.5f,
    [CPU_STAT_POOL_MAX] = 2.0f,
    [CPU_STAT_POOL_ADAPTIVE] = 0.0f,
};

// The growth drains whatever is left in the pool, so scaling the seed scales the round's
// growth. Humans are seeded 0, and the title demo stays vanilla.
static void CpuStatGrowth_ScaleSeed(int slot)
{
    if (!Gm_IsAutoDemo())
        Gm_GetGameData()->city.cpu_stat_budget[slot] *= stc_pool_scale[cpu_stat_pool];
}

// Adaptive: before each growth tick drains it, a CPU's pool is set to how far its stat total
// trails the best human's, so it closes the gap at its level's drain rate and stops once
// level.
static void CpuStatGrowth_Refill(void)
{
    GameData *gd = Gm_GetGameData();
    int leader;
    float best, gap;
    int i;

    if (cpu_stat_pool != CPU_STAT_POOL_ADAPTIVE)
        return;
    leader = CpuAI_Leader(PLY_NUM, 1);
    if (leader == PLY_NUM)
        return;
    best = CpuAI_StatTotal(leader);

    for (i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) != PKIND_CPU)
            continue;
        gap = best - CpuAI_StatTotal(i);
        gd->city.cpu_stat_budget[i] = gap > 0 ? gap : 0;
    }
}

// SceneLoad_3D (0x8001442c), City Trial seed loop `addi r25,r25,1`: reached once per slot
// right after the pool store, r25 still the slot.
CODEPATCH_HOOKCREATE(0x80014ad4,
    "mr 3, 25\n\t",
    CpuStatGrowth_ScaleSeed,
    "",
    0)

// CityTrial_GrowCpuStats (0x80015a00) `bl Gm_GetGameData` once the round frame is a multiple
// of ct_cpu_stat_interval, before the drain: nothing volatile is live.
CODEPATCH_HOOKCREATE(0x80015a50,
    "",
    CpuStatGrowth_Refill,
    "",
    0)

void CpuStatGrowth_InstallHooks(void)
{
    CODEPATCH_HOOKAPPLY(0x80014ad4);
    CODEPATCH_HOOKAPPLY(0x80015a50);
}
