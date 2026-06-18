#include "os.h"
#include "hsd.h"

#include "custom_ai.h"
#include "enemy_ai.h"
#include "cpu_ai.h"
#include "cpu_stat_growth.h"

int CustomAI_RollRandom(int count)
{
    if (count <= 0)
        return 0;
    return HSD_Randi(count);
}

void CustomAI_OnBoot(void)
{
    // CT/AR CPU re-profiling is live via the Rider_CPUInit hook. The enemy-spawn
    // and Top Ride CPU hooks are still TODO.
    CpuAI_InstallHook();

    // City Trial passive CPU stat growth: enable/disable + pool scaling.
    CpuStatGrowth_InstallHook();

    OSReport("[CustomAI] Initialized (CT cpu=%s | AR cpu=%s, enemy=%s | TR cpu=%s)\n",
             CpuAI_GetSelectionName(cpu_ai_preset_ct),
             CpuAI_GetSelectionName(cpu_ai_preset_ar),
             EnemyAI_GetSelectionName(enemy_ai_preset),
             CpuAI_GetSelectionName(cpu_ai_preset_tr));
}
