#include "os.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"

#include "custom_ai_api.h"
#include "cpu_ai.h"
#include "cpu_team.h"
#include "cpu_line.h"
#include "cpu_catchup.h"
#include "cpu_fixes.h"
#include "cpu_stadium.h"
#include "cpu_stat_growth.h"
#include "cpu_target.h"
#include "enemy_ai.h"
#include "topride_ai.h"
#include "ai_overlay.h"

static char *stc_onoff_names[2] = {"Off", "On"};

static void OnChangeCpuCT(int val)
{
    OSReport("[CustomAI] City Trial CPU preset set to %s\n", cpu_ai_menu_ct_names[val]);
}

static void OnChangeCpuAR(int val)
{
    OSReport("[CustomAI] Air Ride CPU preset set to %s\n", cpu_ai_menu_ar_names[val]);
}

static void OnChangeCpuTR(int val)
{
    OSReport("[CustomAI] Top Ride CPU preset set to %s\n", topride_ai_names[val]);
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

static void OnChangeCatchup(int val)
{
    OSReport("[CustomAI] Air Ride CPU catch-up set to %s\n", cpu_catchup_names[val]);
}

static void OnChangeFixes(int val)
{
    CpuFixes_Apply();
    OSReport("[CustomAI] CPU fixes %s\n", stc_onoff_names[val]);
}

static void OnChangeTargeting(int val)
{
    OSReport("[CustomAI] CPU targeting set to %s\n", cpu_targeting_names[val]);
}

static void OnChangeAdaptive(int val)
{
    OSReport("[CustomAI] Adaptive CPUs %s\n", stc_onoff_names[val]);
}

static void OnChangeStadium(int val)
{
    OSReport("[CustomAI] Stadium CPU fixes %s\n", stc_onoff_names[val]);
}

static void OnChangeEnemyDamage(int val)
{
    OSReport("[CustomAI] Enemy damage set to %s\n", enemy_damage_names[val]);
}

static void OnChangeEnemyBehavior(int val)
{
    OSReport("[CustomAI] Enemy behavior set to %s\n", enemy_behavior_names[val]);
}

static void OnChangeTeamwork(int val)
{
    OSReport("[CustomAI] CPU teamwork %s\n", stc_onoff_names[val]);
}

static void OnChangeOverlay(int val)
{
    OSReport("[CustomAI] AI overlay set to %s\n", ai_overlay_names[val]);
}

static void OnChangeRacingLine(int val)
{
    OSReport("[CustomAI] Racing line set to %s\n", cpu_racing_line_names[val]);
}

static MenuDesc ct_menu = {
    .option_num = 3,
    .options = {
        &(OptionDesc){
            .name = "CPU AI",
            .description = "Behavior preset for CPU riders in the city, played at their CPU level",
            .kind = OPTKIND_VALUE,
            .val = &cpu_ai_menu_ct,
            .value_num = CPU_AI_CT_NUM + 1,
            .value_names = cpu_ai_menu_ct_names,
            .on_change = OnChangeCpuCT,
        },
        &(OptionDesc){
            .name = "Enemy AI",
            .description = "Behavior preset for Kirby Melee enemies and the city's event actors",
            .kind = OPTKIND_VALUE,
            .val = &enemy_ai_preset_ct,
            .value_num = ENEMY_AI_MENU_NUM,
            .value_names = enemy_ai_preset_names,
            .on_change = OnChangeEnemyCT,
        },
        &(OptionDesc){
            .name = "CPU Stat Pool",
            .description = "Free machine stats CPUs gain over a round; Adaptive keeps them level with you",
            .kind = OPTKIND_VALUE,
            .val = &cpu_stat_pool,
            .value_num = CPU_STAT_POOL_NUM,
            .value_names = cpu_stat_pool_names,
            .on_change = OnChangeStatPool,
        },
    },
};

static MenuDesc ar_menu = {
    .option_num = 4,
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
        &(OptionDesc){
            .name = "CPU Catch-up",
            .description = "Scales the speed bonuses the Speed Help rule gives CPUs; Skill makes trailing CPUs play sharper instead",
            .kind = OPTKIND_VALUE,
            .val = &cpu_catchup,
            .value_num = CPU_CATCHUP_NUM,
            .value_names = cpu_catchup_names,
            .on_change = OnChangeCatchup,
        },
        &(OptionDesc){
            .name = "Racing Line",
            .description = "CPUs drive a line computed from each course, brake before corners and take shorter branches",
            .kind = OPTKIND_VALUE,
            .val = &cpu_racing_line,
            .value_num = CPU_LINE_NUM,
            .value_names = cpu_racing_line_names,
            .on_change = OnChangeRacingLine,
        },
    },
};

static MenuDesc tr_menu = {
    .option_num = 1,
    .options = {
        &(OptionDesc){
            .name = "CPU AI",
            .description = "Behavior preset for Top Ride CPUs; Expert plays at the top level",
            .kind = OPTKIND_VALUE,
            .val = &topride_ai_menu,
            .value_num = TOPRIDE_AI_MENU_NUM,
            .value_names = topride_ai_names,
            .on_change = OnChangeCpuTR,
        },
    },
};

static MenuDesc general_menu = {
    .option_num = 8,
    .options = {
        &(OptionDesc){
            .name = "CPU Fixes",
            .description = "Fixes vanilla CPU bugs in flight, avoidance, targeting, item and machine choice and abilities",
            .kind = OPTKIND_VALUE,
            .val = &cpu_fixes,
            .value_num = 2,
            .value_names = stc_onoff_names,
            .on_change = OnChangeFixes,
        },
        &(OptionDesc){
            .name = "Stadium CPUs",
            .description = "Fixes for stadium CPUs that vanilla leaves stuck or idle",
            .kind = OPTKIND_VALUE,
            .val = &cpu_stadium,
            .value_num = 2,
            .value_names = stc_onoff_names,
            .on_change = OnChangeStadium,
        },
        &(OptionDesc){
            .name = "CPU Targeting",
            .description = "Who CPUs pick as their rival: vanilla's choice, the nearest human, the leader or the leading human",
            .kind = OPTKIND_VALUE,
            .val = &cpu_targeting,
            .value_num = CPU_TARGET_NUM,
            .value_names = cpu_targeting_names,
            .on_change = OnChangeTargeting,
        },
        &(OptionDesc){
            .name = "CPU Teamwork",
            .description = "CPUs leave an item or machine to whichever CPU is closer",
            .kind = OPTKIND_VALUE,
            .val = &cpu_teamwork,
            .value_num = 2,
            .value_names = stc_onoff_names,
            .on_change = OnChangeTeamwork,
        },
        &(OptionDesc){
            .name = "Adaptive CPUs",
            .description = "CPUs play a level higher after you win a race or stadium, and lower after you finish last",
            .kind = OPTKIND_VALUE,
            .val = &cpu_adaptive,
            .value_num = 2,
            .value_names = stc_onoff_names,
            .on_change = OnChangeAdaptive,
        },
        &(OptionDesc){
            .name = "Enemy Damage",
            .description = "Scales the damage and knockback of enemy attacks",
            .kind = OPTKIND_VALUE,
            .val = &enemy_damage,
            .value_num = ENEMY_DAMAGE_NUM,
            .value_names = enemy_damage_names,
            .on_change = OnChangeEnemyDamage,
        },
        &(OptionDesc){
            .name = "Enemy Behavior",
            .description = "Flying enemies drift toward riders (Pursue) or away from them (Skittish)",
            .kind = OPTKIND_VALUE,
            .val = &enemy_behavior,
            .value_num = ENEMY_BEHAVIOR_NUM,
            .value_names = enemy_behavior_names,
            .on_change = OnChangeEnemyBehavior,
        },
        &(OptionDesc){
            .name = "AI Overlay",
            .description = "Debug view: each CPU's state as text, its route and the hazards it sees in the world, or both",
            .kind = OPTKIND_VALUE,
            .val = &ai_overlay,
            .value_num = AI_OVERLAY_NUM,
            .value_names = ai_overlay_names,
            .on_change = OnChangeOverlay,
        },
    },
};

static MenuDesc top_menu = {
    .option_num = 4,
    .options = {
        &(OptionDesc){
            .name = "City Trial AI",
            .description = "CPU and enemy presets and CPU stat growth for City Trial",
            .kind = OPTKIND_MENU,
            .menu_ptr = &ct_menu,
        },
        &(OptionDesc){
            .name = "Air Ride AI",
            .description = "CPU and enemy presets, CPU catch-up and the racing line for Air Ride",
            .kind = OPTKIND_MENU,
            .menu_ptr = &ar_menu,
        },
        &(OptionDesc){
            .name = "Top Ride AI",
            .description = "CPU presets for Top Ride",
            .kind = OPTKIND_MENU,
            .menu_ptr = &tr_menu,
        },
        &(OptionDesc){
            .name = "General",
            .description = "CPU fixes, targeting, teamwork and difficulty, enemy damage and behavior, debug overlay",
            .kind = OPTKIND_MENU,
            .menu_ptr = &general_menu,
        },
    },
};

OptionDesc ModSettings = {
    .name = "Custom AI",
    .description = "Behavior presets, fixes and difficulty controls for CPU riders and enemies",
    .kind = OPTKIND_MENU,
    .menu_ptr = &top_menu,
};

static const CustomAIAPI api = {
    .Hunt = CpuTarget_Hunt,
    .Frenzy = CpuTarget_Frenzy,
    .SetLevelOffset = CpuCatchup_SetBaseOffset,
};

static void OnBoot(void)
{
    CpuAI_InstallHooks();
    EnemyAI_InstallHooks();
    CpuStatGrowth_InstallHooks();
    TopRideAI_InstallHooks();

    Hoshi_ExportMod((void *)&api);

    OSReport("[CustomAI] Hooks installed, API exported (v%d.%d)\n", CUSTOM_AI_API_MAJOR, CUSTOM_AI_API_MINOR);
}

static void OnSaveLoaded(void)
{
    CpuAI_ImportApis();
}

static void On3DLoadStart(void)
{
    CpuAI_On3DLoadStart();
    CpuTarget_On3DLoadStart();
    CpuLine_On3DLoadStart();
}

static void On3DLoadEnd(void)
{
    CpuLine_On3DLoadEnd();
    CpuCatchup_On3DLoadEnd();
    EnemyAI_On3DLoadEnd();
    AIOverlay_On3DLoadEnd();
}

static void On3DExit(void)
{
    CpuCatchup_On3DExit();
    AIOverlay_On3DExit();
}

ModDesc mod_desc = {
    .name = CUSTOM_AI_MOD_NAME,
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .affects_gameplay = 1,
    .option_desc = &ModSettings,
    .OnBoot = OnBoot,
    .OnSaveLoaded = OnSaveLoaded,
    .On3DLoadStart = On3DLoadStart,
    .On3DLoadEnd = On3DLoadEnd,
    .On3DExit = On3DExit,
    .OnTopRideLoadEnd = TopRideAI_OnLoadEnd,
};
