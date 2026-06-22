#include "os.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"

#include "hypernova.h"
#include "hypernova_api.h"

// Menu value labels. Order must match the values written to the bound ints.
static char *stc_toggle_names[] = {
    "Disabled",
    "Enabled",
};

static char *stc_duration_names[] = {
    "Short",   // 300 frames (~5s)
    "Medium",  // 600 frames (~10s)
    "Long",    // 1200 frames (~20s)
};

static void OnBoot(void)
{
    Hypernova_OnBoot();
}

static void OnSceneChange(void)
{
    Hypernova_OnSceneChange();
}

static void OnFrameEnd(void)
{
    Hypernova_OnFrameEnd();
}

static void OnChangeEnabled(int val)
{
    OSReport("[Hypernova] %s\n", val ? "enabled" : "disabled");
}

static void OnChangeSelfTest(int val)
{
    OSReport("[Hypernova] Self-test trigger %s\n", val ? "enabled" : "disabled");
}

static void OnChangeDebugCone(int val)
{
    OSReport("[Hypernova] Debug cone overlay %s\n", val ? "enabled" : "disabled");
}

static MenuDesc top_menu = {
    .option_num = 5,
    .options = {
        &(OptionDesc){
            .name = "Enabled",
            .description = "Enable or disable Hypernova",
            .kind = OPTKIND_VALUE,
            .val = &hypernova_enabled,
            .value_num = 2,
            .value_names = stc_toggle_names,
            .on_change = OnChangeEnabled,
        },
        &(OptionDesc){
            .name = "Duration",
            .description = "How long Hypernova lasts when activated",
            .kind = OPTKIND_VALUE,
            .val = &hypernova_duration_sel,
            .value_num = HYPERNOVA_DURATION_NUM,
            .value_names = stc_duration_names,
        },
        &(OptionDesc){
            .name = "Suck Yakumono",
            .description = "Also vacuum yakumonos",
            .kind = OPTKIND_VALUE,
            .val = &hypernova_suck_yaku,
            .value_num = 2,
            .value_names = stc_toggle_names,
        },
        &(OptionDesc){
            .name = "Self-Test (D-Pad Up)",
            .description = "Hold D-Pad Up to trigger Hypernova",
            .kind = OPTKIND_VALUE,
            .val = &hypernova_selftest,
            .value_num = 2,
            .value_names = stc_toggle_names,
            .on_change = OnChangeSelfTest,
        },
        &(OptionDesc){
            .name = "Debug Cone",
            .description = "Draw the suction cone",
            .kind = OPTKIND_VALUE,
            .val = &hypernova_debug_cone,
            .value_num = 2,
            .value_names = stc_toggle_names,
            .on_change = OnChangeDebugCone,
        },
    },
};

OptionDesc ModSettings = {
    .name = "Hypernova",
    .description = "City Trial super-inhale: grow 2x and vacuum items + props",
    .kind = OPTKIND_MENU,
    .menu_ptr = &top_menu,
};

ModDesc mod_desc = {
    .name = "hypernova",
    .author = "DeDeDK",
    .version.major = HYPERNOVA_API_MAJOR,
    .version.minor = HYPERNOVA_API_MINOR,
    .option_desc = &ModSettings,
    .OnBoot = OnBoot,
    .OnSceneChange = OnSceneChange,
    .OnFrameEnd = OnFrameEnd,
};
