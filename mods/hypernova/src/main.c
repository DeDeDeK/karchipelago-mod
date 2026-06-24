#include <string.h>

#include "os.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"

#include "hypernova.h"
#include "hypernova_api.h"

#include "custom_items_api.h"

// Name of the custom item that grants Hypernova on pickup (matches the descriptor
// name in mods/custom_items/assets/items/MiracleFruit.dat).
#define HYPERNOVA_TRIGGER_ITEM_NAME "Miracle Fruit"

static const CustomItemsAPI *stc_ci_api;
static const HypernovaAPI   *stc_hn_api;
static int stc_pickup_registered;

// custom_items pickup handler: when a rider collects the Miracle Fruit, grant Hypernova
// to that player only.
static void OnCustomItemPickup(u32 id_hash, const char *name, int player)
{
    (void)id_hash;
    if (name == NULL || strcmp(name, HYPERNOVA_TRIGGER_ITEM_NAME) != 0)
        return;
    if (stc_hn_api == NULL)
        stc_hn_api = (const HypernovaAPI *)Hoshi_ImportMod(
            (char *)HYPERNOVA_MOD_NAME, HYPERNOVA_API_MAJOR, HYPERNOVA_API_MINOR);
    if (stc_hn_api != NULL && stc_hn_api->ActivatePlayer != NULL)
        stc_hn_api->ActivatePlayer(player, 0);
}

// Import custom_items and register the pickup handler once it is available. Called
// from boot and scene-change so it succeeds regardless of mod load order.
static void TryRegisterPickupHandler(void)
{
    if (stc_pickup_registered)
        return;
    if (stc_ci_api == NULL)
        stc_ci_api = (const CustomItemsAPI *)Hoshi_ImportMod(
            (char *)CUSTOM_ITEMS_MOD_NAME, CUSTOM_ITEMS_API_MAJOR, CUSTOM_ITEMS_API_MINOR);
    if (stc_ci_api != NULL && stc_ci_api->SetPickupHandler != NULL)
    {
        stc_ci_api->SetPickupHandler(OnCustomItemPickup);
        stc_pickup_registered = 1;
        OSReport("[Hypernova] Miracle Fruit pickup handler registered\n");
    }
}

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
    TryRegisterPickupHandler();
}

static void OnSceneChange(void)
{
    Hypernova_OnSceneChange();
    TryRegisterPickupHandler(); // retry until custom_items is available
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
            .name = "D Pad self test",
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
    .description = "Unleash the power of the Hypernova!",
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
