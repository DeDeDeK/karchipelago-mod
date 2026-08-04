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
    // Top Ride CPUs are a separate AI and still have no hook.
    CpuAI_InstallHook();
    EnemyAI_InstallHook();
    CpuStatGrowth_InstallHook();

    OSReport("[CustomAI] Initialized (CT cpu=%s enemy=%s | AR cpu=%s enemy=%s | TR cpu=%s)\n",
             CpuAI_GetSelectionName(cpu_ai_preset_ct),
             EnemyAI_GetSelectionName(enemy_ai_preset_ct),
             CpuAI_GetSelectionName(cpu_ai_preset_ar),
             EnemyAI_GetSelectionName(enemy_ai_preset_ar),
             CpuAI_GetSelectionName(cpu_ai_preset_tr));
}
