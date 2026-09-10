#include <string.h>

#include "os.h"
#include "game.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"

#include "hypernova.h"
#include "hypernova_api.h"

#include "custom_items_api.h"

// Must match the custom item descriptor's name.
#define HYPERNOVA_TRIGGER_ITEM_NAME "Miracle Fruit"

static const CustomItemsAPI *stc_ci_api;
static u32 stc_item_hash;

// Grants Hypernova to the player who collected the Miracle Fruit, and to nobody else.
static void OnCustomItemPickup(u32 id_hash, const char *name, int player)
{
    (void)name;
    if (id_hash == stc_item_hash)
        Hypernova_ActivatePlayer(player, 0);
}

// Retried every scene change: mods boot in FST order, so custom_items' export
// may not exist yet when this one boots.
static void TryBind(void)
{
    if (stc_item_hash != 0)
        return;
    if (stc_ci_api == NULL)
        stc_ci_api = (const CustomItemsAPI *)Hoshi_ImportMod(
            (char *)CUSTOM_ITEMS_MOD_NAME, CUSTOM_ITEMS_API_MAJOR, CUSTOM_ITEMS_API_MINOR);
    if (stc_ci_api == NULL)
        return;

    for (int i = 0; i < stc_ci_api->GetCount(); i++)
    {
        const char *n = stc_ci_api->GetName(i);
        if (n == NULL || strcmp(n, HYPERNOVA_TRIGGER_ITEM_NAME) != 0)
            continue;
        stc_item_hash = stc_ci_api->GetIdHash(i);
        stc_ci_api->AddPickupHandler(OnCustomItemPickup);
        OSReport("[Hypernova] Bound %s\n", HYPERNOVA_TRIGGER_ITEM_NAME);
        return;
    }
}

// Order must match the values written to the bound ints.
static char *stc_toggle_names[] = {
    "Disabled",
    "Enabled",
};

static char *stc_duration_names[] = {
    "Short",   // 300 frames (~5s)
    "Medium",  // 600 frames (~10s)
    "Long",    // 1200 frames (~20s)
};

static void OnSceneChange(void)
{
    Hypernova_OnSceneChange();
    TryBind();
}

// custom_items assigns kinds at CityItemSpawn_Init, after this, and skips a
// disabled item - so a fruit held out here is never handed an ItemKind and no
// path can spawn it. The title screen's attract demo is a City Trial round in
// every respect, and a fruit in it is a fruit no player can use.
static void On3DLoadStart(void)
{
    TryBind();
    if (stc_item_hash != 0)
        stc_ci_api->SetEnabled(stc_item_hash,
                               hypernova_enabled && !Gm_IsAutoDemo() && Gm_IsInCity());
}

static void OnChangeEnabled(int val)
{
    OSReport("[Hypernova] Hypernova %s\n", val ? "enabled" : "disabled");
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
    .option_num = 6,
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
            .name = "Suck Machines",
            .description = "Also vacuum unridden machines (they explode on arrival)",
            .kind = OPTKIND_VALUE,
            .val = &hypernova_suck_machines,
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
    .OnBoot = Hypernova_OnBoot,
    .OnSceneChange = OnSceneChange,
    .On3DLoadStart = On3DLoadStart,
    .OnFrameEnd = Hypernova_OnFrameEnd,
};
