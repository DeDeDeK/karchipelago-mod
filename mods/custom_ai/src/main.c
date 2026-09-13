#include "os.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"

#include "cpu_ai.h"
#include "cpu_stat_growth.h"
#include "enemy_ai.h"

static void OnChangeCpuCT(int val)
{
    OSReport("[CustomAI] City Trial CPU preset set to %s\n", cpu_ai_menu_ct_names[val]);
}

static void OnChangeCpuAR(int val)
{
    OSReport("[CustomAI] Air Ride CPU preset set to %s\n", cpu_ai_menu_ar_names[val]);
}

static void OnChangeEnemyAR(int val)
{
    OSReport("[CustomAI] Air Ride enemy preset set to %s\n", enemy_ai_preset_names[val]);
}

static void OnChangeEnemyCT(int val)
{
    OSReport("[CustomAI] City Trial enemy preset set to %s\n", enemy_ai_preset_names[val]);
}

static void OnChangeStatPool(int val)
{
    OSReport("[CustomAI] City Trial CPU stat pool set to %s\n", cpu_stat_pool_names[val]);
}

static MenuDesc ct_menu = {
    .option_num = 3,
    .options = {
        &(OptionDesc){
            .name = "CPU AI",
            .description = "Behavior preset for CPU riders in the city, played at their CPU level",
            .kind = OPTKIND_VALUE,
            .val = &cpu_ai_menu_ct,
            .value_num = CPU_AI_NUM + 1,
            .value_names = cpu_ai_menu_ct_names,
            .on_change = OnChangeCpuCT,
        },
        &(OptionDesc){
            .name = "Enemy AI",
            .description = "Behavior preset for City Trial enemies",
            .kind = OPTKIND_VALUE,
            .val = &enemy_ai_preset_ct,
            .value_num = ENEMY_AI_MENU_NUM,
            .value_names = enemy_ai_preset_names,
            .on_change = OnChangeEnemyCT,
        },
        &(OptionDesc){
            .name = "CPU Stat Pool",
            .description = "Scales the free machine stats CPUs gain over a round (x0-x2)",
            .kind = OPTKIND_VALUE,
            .val = &cpu_stat_pool,
            .value_num = CPU_STAT_POOL_NUM,
            .value_names = cpu_stat_pool_names,
            .on_change = OnChangeStatPool,
        },
    },
};

static MenuDesc ar_menu = {
    .option_num = 2,
    .options = {
        &(OptionDesc){
            .name = "CPU AI",
            .description = "Behavior preset for Air Ride CPU riders, played at their CPU level",
            .kind = OPTKIND_VALUE,
            .val = &cpu_ai_menu_ar,
            .value_num = CPU_AI_AR_NUM + 1,
            .value_names = cpu_ai_menu_ar_names,
            .on_change = OnChangeCpuAR,
        },
        &(OptionDesc){
            .name = "Enemy AI",
            .description = "Behavior preset for Air Ride enemies",
            .kind = OPTKIND_VALUE,
            .val = &enemy_ai_preset_ar,
            .value_num = ENEMY_AI_MENU_NUM,
            .value_names = enemy_ai_preset_names,
            .on_change = OnChangeEnemyAR,
        },
    },
};

static MenuDesc top_menu = {
    .option_num = 2,
    .options = {
        &(OptionDesc){
            .name = "City Trial AI",
            .description = "AI presets for City Trial",
            .kind = OPTKIND_MENU,
            .menu_ptr = &ct_menu,
        },
        &(OptionDesc){
            .name = "Air Ride AI",
            .description = "AI presets for Air Ride",
            .kind = OPTKIND_MENU,
            .menu_ptr = &ar_menu,
        },
    },
};

OptionDesc ModSettings = {
    .name = "Custom AI",
    .description = "Behavior presets for CPU riders and enemies, and City Trial CPU stat growth",
    .kind = OPTKIND_MENU,
    .menu_ptr = &top_menu,
};

static void OnBoot(void)
{
    CpuAI_InstallHooks();
    EnemyAI_InstallHook();
    CpuStatGrowth_InstallHook();

    OSReport("[CustomAI] Hooks installed\n");
}

ModDesc mod_desc = {
    .name = "custom_ai",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .affects_gameplay = 1,
    .option_desc = &ModSettings,
    .OnBoot = OnBoot,
};
