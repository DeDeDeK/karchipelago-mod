#include "os.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"

#include "custom_ai.h"
#include "enemy_ai.h"
#include "cpu_ai.h"

// Menu labels for each selector. Ordered to match the preset enums (concrete
// presets then the "Random" sentinel) - keep in sync with the preset tables in
// enemy_ai.c / cpu_ai.c.
static char *stc_enemy_names[] = {
    "Default",
    "Aggressive",
    "Item Hoarder",
    "Coward",
    "Erratic",
    "Random",
};

static char *stc_cpu_names[] = {
    "Default",
    "Aggressive",
    "Hoarder",
    "Cautious",
    "Reckless",
    "Random",
};

static void OnBoot(void)
{
    CustomAI_OnBoot();
}

static void OnChangeCpuCT(int val)
{
    OSReport("[CustomAI] City Trial CPU preset set to %s\n", CpuAI_GetSelectionName(val));
}

static void OnChangeCpuAR(int val)
{
    OSReport("[CustomAI] Air Ride CPU preset set to %s\n", CpuAI_GetSelectionName(val));
}

static void OnChangeCpuTR(int val)
{
    OSReport("[CustomAI] Top Ride CPU preset set to %s\n", CpuAI_GetSelectionName(val));
}

static void OnChangeEnemy(int val)
{
    OSReport("[CustomAI] Air Ride enemy preset set to %s\n", EnemyAI_GetSelectionName(val));
}

// City Trial: CPU riders only.
static MenuDesc ct_menu = {
    .option_num = 1,
    .options = {
        &(OptionDesc){
            .name = "CPU AI",
            .description = "Behavior preset for City Trial CPU riders",
            .kind = OPTKIND_VALUE,
            .val = &cpu_ai_preset_ct,
            .value_num = CPU_AI_MENU_NUM,
            .value_names = stc_cpu_names,
            .on_change = OnChangeCpuCT,
        },
    },
};

// Air Ride: CPU riders and inhalable enemies.
static MenuDesc ar_menu = {
    .option_num = 2,
    .options = {
        &(OptionDesc){
            .name = "CPU AI",
            .description = "Behavior preset for Air Ride CPU riders",
            .kind = OPTKIND_VALUE,
            .val = &cpu_ai_preset_ar,
            .value_num = CPU_AI_MENU_NUM,
            .value_names = stc_cpu_names,
            .on_change = OnChangeCpuAR,
        },
        &(OptionDesc){
            .name = "Enemy AI",
            .description = "Behavior preset for Air Ride enemies",
            .kind = OPTKIND_VALUE,
            .val = &enemy_ai_preset,
            .value_num = ENEMY_AI_MENU_NUM,
            .value_names = stc_enemy_names,
            .on_change = OnChangeEnemy,
        },
    },
};

// Top Ride: CPU riders only.
static MenuDesc tr_menu = {
    .option_num = 1,
    .options = {
        &(OptionDesc){
            .name = "CPU AI",
            .description = "Behavior preset for Top Ride CPU riders",
            .kind = OPTKIND_VALUE,
            .val = &cpu_ai_preset_tr,
            .value_num = CPU_AI_MENU_NUM,
            .value_names = stc_cpu_names,
            .on_change = OnChangeCpuTR,
        },
    },
};

// One submenu per game mode, each exposing only the AI domains that mode has.
static MenuDesc top_menu = {
    .option_num = 3,
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
        &(OptionDesc){
            .name = "Top Ride AI",
            .description = "AI presets for Top Ride",
            .kind = OPTKIND_MENU,
            .menu_ptr = &tr_menu,
        },
    },
};

OptionDesc ModSettings = {
    .name = "Custom AI",
    .description = "Behavior presets for CPU riders (all modes) and Air Ride enemies",
    .kind = OPTKIND_MENU,
    .menu_ptr = &top_menu,
};

ModDesc mod_desc = {
    .name = "custom_ai",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .option_desc = &ModSettings,
    .OnBoot = OnBoot,
};
