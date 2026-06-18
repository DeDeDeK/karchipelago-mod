#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "cpu_stat_growth.h"

// Default to vanilla behavior: growth on, unscaled pool. A stock build is then
// indistinguishable from vanilla until the menu is touched.
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

// Rescale one slot's just-seeded stat-growth pool. The vanilla seed loop stores
// GameData.city.cpu_stat_budget[slot] = ct_cpu_stat_seed[cpu_level] for each CPU
// slot (0 for humans); we multiply that freshly stored float in place. Disabling
// collapses the pool to 0, so the per-frame drainer finds nothing to hand out.
// Humans already hold 0, so scaling them is a no-op.
void CpuStatGrowth_ScaleSeed(int slot)
{
    GameData *gd = Gm_GetGameData();
    float factor = cpu_stat_growth_enabled ? CpuStatGrowth_Factor() : 0.0f;

    gd->city.cpu_stat_budget[slot] *= factor;
}

// Land on the seed loop's slot-increment `addi r25,r25,1` (0x80014ad4), reached
// once per player slot right after the pool store. r25 still holds the current
// slot index there (pre-increment), so `mr 3, 25` hands it to the callback. The
// clobbered `addi` is re-run by the hook framework.
CODEPATCH_HOOKCREATE(0x80014ad4,
    "mr 3, 25\n\t",
    CpuStatGrowth_ScaleSeed,
    "",
    0)

void CpuStatGrowth_InstallHook(void)
{
    CODEPATCH_HOOKAPPLY(0x80014ad4);
    OSReport("[CustomAI] CT CPU stat-growth seed hook installed\n");
}
