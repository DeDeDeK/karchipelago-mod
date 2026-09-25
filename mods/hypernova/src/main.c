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
static int stc_bind_warned;

// Grants Hypernova to the player who collected the Miracle Fruit, and to nobody else.
static void OnCustomItemPickup(u32 id_hash, const char *name, int player)
{
    (void)name;
    if (id_hash == stc_item_hash)
        Hypernova_ActivatePlayer(player, 0);
}

// Run from scene changes: mods boot in FST order, so custom_items' export may not
// exist yet when this one boots. The import is tried once, since a build without
// custom_items would warn on every attempt.
static void TryBind(void)
{
    static int import_tried;

    if (stc_item_hash != 0)
        return;
    if (!import_tried)
    {
        import_tried = 1;
        stc_ci_api = (const CustomItemsAPI *)Hoshi_ImportMod(
            (char *)CUSTOM_ITEMS_MOD_NAME, CUSTOM_ITEMS_API_MAJOR, CUSTOM_ITEMS_API_MINOR);
    }

    if (stc_ci_api != NULL)
    {
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

    // Latched: without custom_items or the archive, only the API and self-test grant Hypernova.
    if (!stc_bind_warned)
    {
        stc_bind_warned = 1;
        OSReport("[Hypernova] %s unavailable\n", HYPERNOVA_TRIGGER_ITEM_NAME);
    }
}

// Order must match the values written to the bound ints.
static char *stc_toggle_names[] = {
    "Disabled",
    "Enabled",
};

static char *stc_duration_names[] = {
    "Short",
    "Medium",
    "Long",
};

static void OnSceneChange(void)
{
    Hypernova_OnSceneChange();
    TryBind();
}

// custom_items assigns kinds at CityItemSpawn_Init, after this, and skips a disabled item.
static void On3DLoadStart(void)
{
    if (stc_ci_api != NULL && stc_item_hash != 0)
        stc_ci_api->SetEnabled(stc_item_hash,
                               hypernova_enabled && !Gm_IsAutoDemo() && Gm_IsInCity());
}

// Turning it off mid-round would otherwise strand live players: OnFrameEnd stops running, so
// their scale, rainbow priority pin and claims would all freeze until the next scene.
static void OnChangeEnabled(int val)
{
    if (!val)
        Hypernova_Deactivate();
    OSReport("[Hypernova] %s\n", val ? "Enabled" : "Disabled");
}

static void OnChangeDuration(int val) { OSReport("[Hypernova] Duration %s\n", stc_duration_names[val]); }
static void OnChangeSuckProps(int val) { OSReport("[Hypernova] Suck props %s\n", val ? "enabled" : "disabled"); }
static void OnChangeSuckMachines(int val) { OSReport("[Hypernova] Suck machines %s\n", val ? "enabled" : "disabled"); }
static void OnChangeSelfTest(int val) { OSReport("[Hypernova] Self-test trigger %s\n", val ? "enabled" : "disabled"); }
static void OnChangeDebugCone(int val) { OSReport("[Hypernova] Debug cone overlay %s\n", val ? "enabled" : "disabled"); }

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
            .on_change = OnChangeDuration,
        },
        &(OptionDesc){
            .name = "Suck Props",
            .description = "Also vacuum breakable props (they shatter on arrival)",
            .kind = OPTKIND_VALUE,
            .val = &hypernova_suck_yaku,
            .value_num = 2,
            .value_names = stc_toggle_names,
            .on_change = OnChangeSuckProps,
        },
        &(OptionDesc){
            .name = "Suck Machines",
            .description = "Also vacuum unridden machines (they explode on arrival)",
            .kind = OPTKIND_VALUE,
            .val = &hypernova_suck_machines,
            .value_num = 2,
            .value_names = stc_toggle_names,
            .on_change = OnChangeSuckMachines,
        },
        &(OptionDesc){
            .name = "D Pad self test",
            .description = "Press D-Pad Up on port 1 to give every human Hypernova",
            .kind = OPTKIND_VALUE,
            .val = &hypernova_selftest,
            .value_num = 2,
            .value_names = stc_toggle_names,
            .no_save = 1,
            .on_change = OnChangeSelfTest,
        },
        &(OptionDesc){
            .name = "Debug Cone",
            .description = "Draw the suction cone, with or without Hypernova active",
            .kind = OPTKIND_VALUE,
            .val = &hypernova_debug_cone,
            .value_num = 2,
            .value_names = stc_toggle_names,
            .no_save = 1,
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
    .name = HYPERNOVA_MOD_NAME,
    .author = "DeDeDK",
    .version.major = HYPERNOVA_API_MAJOR,
    .version.minor = HYPERNOVA_API_MINOR,
    .affects_gameplay = 1,
    .option_desc = &ModSettings,
    .OnBoot = Hypernova_OnBoot,
    .OnSceneChange = OnSceneChange,
    .On3DLoadStart = On3DLoadStart,
    .OnFrameEnd = Hypernova_OnFrameEnd,
};
