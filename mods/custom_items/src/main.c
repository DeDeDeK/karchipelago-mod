#include "os.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"

#include "custom_items.h"

static char *stc_toggle_names[] = {
    "Disabled",
    "Enabled",
};

static void OnChangeEnabled(int val)
{
    OSReport("[CustomItems] Custom item spawning %s\n", val ? "enabled" : "disabled");
}

// Per-item enable toggles are deferred: the item set is discovered at runtime,
// so those options must be built dynamically once the registry is populated.
// For now a single master switch gates the whole framework.
static MenuDesc top_menu = {
    .option_num = 1,
    .options = {
        &(OptionDesc){
            .name = "Enabled",
            .description = "Allow discovered custom items to spawn in City Trial",
            .kind = OPTKIND_VALUE,
            .val = &custom_items_enabled,
            .value_num = 2,
            .value_names = stc_toggle_names,
            .on_change = OnChangeEnabled,
        },
    },
};

OptionDesc ModSettings = {
    .name = "Custom Items",
    .description = "Spawn custom items dropped into the items/ folder",
    .kind = OPTKIND_MENU,
    .menu_ptr = &top_menu,
};

static void OnBoot(void)
{
    CustomItems_OnBoot();
}

ModDesc mod_desc = {
    .name = "custom_items",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .option_desc = &ModSettings,
    .OnBoot = OnBoot,
};
