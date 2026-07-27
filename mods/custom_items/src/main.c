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

// MenuDesc's options[] is a flexible array member, so the runtime-sized menu is
// held in this fixed-size mirror of its layout, cast to MenuDesc*.
typedef struct
{
    MenuDesc *prev;
    u16 cursor;
    u16 scroll;
    u16 option_num;
    OptionDesc *options[1 + CUSTOM_ITEM_MAX]; // master toggle + one per item
} TopMenuStorage;
static TopMenuStorage stc_top_menu;

static OptionDesc stc_master_option = {
    .name = "Enabled",
    .description = "Allow discovered custom items to spawn in City Trial",
    .kind = OPTKIND_VALUE,
    .val = &custom_items_enabled,
    .value_num = 2,
    .value_names = stc_toggle_names,
    .on_change = OnChangeEnabled,
};

// Per-item toggle descriptors, filled from the registry.
static OptionDesc stc_item_options[CUSTOM_ITEM_MAX];

OptionDesc ModSettings = {
    .name = "Custom Items",
    .description = "Spawn custom items dropped into the items/ folder",
    .kind = OPTKIND_MENU,
    .menu_ptr = (MenuDesc *)&stc_top_menu,
};

// Per-item toggles are labeled with the stable menu_label, so their saved state
// (hashed on the option name) survives reboots. Must run in OnBoot, before
// hoshi reads the menu for save-sizing and restore.
static void BuildSettingsMenu(void)
{
    MenuDesc *menu = (MenuDesc *)&stc_top_menu;
    menu->option_num = 0;
    menu->options[menu->option_num++] = &stc_master_option;

    int count = CustomItems_GetCount();
    for (int i = 0; i < count && i < CUSTOM_ITEM_MAX; i++)
    {
        CustomItemEntry *e = CustomItems_GetEntry(i);
        if (e == NULL)
            continue;

        OptionDesc *opt = &stc_item_options[i];
        opt->name = e->menu_label;
        opt->description = "Allow this custom item to spawn";
        opt->kind = OPTKIND_VALUE;
        opt->val = &e->enabled;
        opt->value_num = 2;
        opt->value_names = stc_toggle_names;
        menu->options[menu->option_num++] = opt;
    }
}

static void OnBoot(void)
{
    CustomItems_OnBoot();
    BuildSettingsMenu();
}

ModDesc mod_desc = {
    .name = "custom_items",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .option_desc = &ModSettings,
    .OnBoot = OnBoot,
};
