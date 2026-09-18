#include "game.h"
#include "code_patch/code_patch.h"

#include "cpu_stat_growth.h"

int cpu_stat_pool = CPU_STAT_POOL_DEFAULT;

char *cpu_stat_pool_names[CPU_STAT_POOL_NUM] = {
    [CPU_STAT_POOL_OFF] = "Off",
    [CPU_STAT_POOL_LOW] = "Low",
    [CPU_STAT_POOL_DEFAULT] = "Default",
    [CPU_STAT_POOL_HIGH] = "High",
    [CPU_STAT_POOL_MAX] = "Max",
};

static const float stc_pool_scale[CPU_STAT_POOL_NUM] = {
    [CPU_STAT_POOL_OFF] = 0.0f,
    [CPU_STAT_POOL_LOW] = 0.5f,
    [CPU_STAT_POOL_DEFAULT] = 1.0f,
    [CPU_STAT_POOL_HIGH] = 1.5f,
    [CPU_STAT_POOL_MAX] = 2.0f,
};

// The per-frame drainer hands out whatever is left in the pool, so scaling the
// seed scales the round's growth. Humans are seeded 0.
void CpuStatGrowth_ScaleSeed(int slot)
{
    Gm_GetGameData()->city.cpu_stat_budget[slot] *= stc_pool_scale[cpu_stat_pool];
}

// SceneLoad_3D (0x8001442c), City Trial seed loop `addi r25,r25,1`: reached
// once per slot right after the pool store, r25 still the slot.
CODEPATCH_HOOKCREATE(0x80014ad4,
    "mr 3, 25\n\t",
    CpuStatGrowth_ScaleSeed,
    "",
    0)

void CpuStatGrowth_InstallHook(void)
{
    CODEPATCH_HOOKAPPLY(0x80014ad4);
}
