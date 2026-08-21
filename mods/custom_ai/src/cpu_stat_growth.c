#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "cpu_stat_growth.h"

// Vanilla behavior: growth on, unscaled pool.
int cpu_stat_growth_enabled = 1;
int cpu_stat_growth_amount = CPU_STAT_BUDGET_DEFAULT;

float CpuStatGrowth_Factor(void)
{
    switch (cpu_stat_growth_amount)
    {
    case CPU_STAT_BUDGET_LOW:
        return 0.5f;
    case CPU_STAT_BUDGET_MEDIUM:
        return 1.5f;
    case CPU_STAT_BUDGET_HIGH:
        return 2.0f;
    case CPU_STAT_BUDGET_DEFAULT:
    default:
        return 1.0f;
    }
}

// Multiplies the float the vanilla seed loop just stored. A factor of 0 leaves
// the per-frame drainer nothing to hand out. Humans already hold 0.
void CpuStatGrowth_ScaleSeed(int slot)
{
    GameData *gd = Gm_GetGameData();
    float factor = cpu_stat_growth_enabled ? CpuStatGrowth_Factor() : 0.0f;

    gd->city.cpu_stat_budget[slot] *= factor;
}

// The seed loop's slot-increment `addi r25,r25,1`, reached once per player slot
// right after the pool store, with r25 still holding the pre-increment index.
CODEPATCH_HOOKCREATE(0x80014ad4,
    "mr 3, 25\n\t",
    CpuStatGrowth_ScaleSeed,
    "",
    0)

void CpuStatGrowth_InstallHook(void)
{
    CODEPATCH_HOOKAPPLY(0x80014ad4);
}
