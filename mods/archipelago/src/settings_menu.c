#include "game.h"
#include "os.h"
#include "hoshi/settings.h"

#include "main.h"
#include "settings_menu.h"
#include "energylink_spend.h"

// Hoshi's Mod_CopyFromSave overwrites these when a saved value exists. Checks, items and
// links default off locally: a connected client posts a richer line a poll later.
APMenuSettings ap_menu_settings = {
    .ct_permanent_patches_enabled         = 1,
    .ct_stadium_permanent_patches_enabled = 1,
    .ar_permanent_patches_enabled         = 1,
    .ct_random_start_machine              = 1,
    .drop_ability_enabled                 = 1,
    .air_quick_spin_enabled               = 1,
    .onfoot_zoom_enabled                  = 1,
    .ap_box_rate                          = APBOXRATE_MEDIUM,
    .energy_sources = {
        [APENERGYSRC_OBJECTS] = 1,
        [APENERGYSRC_PATCHES] = 1,
        [APENERGYSRC_CHARGE]  = 1,
    },
    .text_messages = {
        [APTEXT_KIND_CHECK]  = 1,
        [APTEXT_KIND_ITEM]   = 1,
        [APTEXT_KIND_HINT]   = 1,
        [APTEXT_KIND_STATUS] = 1,
        [APTEXT_KIND_CHAT]   = 0,
        [APTEXT_KIND_LINK]   = 1,
    },
    .local_messages = {
        [APLOCAL_CHECK] = 0,
        [APLOCAL_ITEM]  = 0,
        [APLOCAL_GOAL]  = 1,
        [APLOCAL_LINK]  = 0,
    },
};

static char *stc_off_on[] = {"Off", "On"};
static char *stc_auto_charge[APAUTOCHARGE_NUM] = {"Off", "Slow", "Med", "Fast"};
static char *stc_ap_box_rate[APBOXRATE_NUM] = {"Rare", "Low", "Med", "High"};

void SettingsMenu_SeedFromSlotOptions(const APSlotOptions *opts)
{
    ap_menu_settings.deathlink_enabled = opts->death_link_enabled;
    ap_menu_settings.energylink_enabled = opts->energy_link_enabled;
    ap_menu_settings.traplink_enabled = opts->trap_link_enabled;
}

void SyncMenuStateToAPData(void)
{
    ap_data->deathlink_menu_enabled  = ap_menu_settings.deathlink_enabled;
    ap_data->energylink_menu_enabled = SettingsMenu_EnergyLinkEnabled();
    ap_data->traplink_menu_enabled   = ap_menu_settings.traplink_enabled;

    u32 mask = 0;
    for (int i = 0; i < APTEXT_KIND_NUM; i++)
        if (ap_menu_settings.text_messages[i])
            mask |= 1 << i;
    ap_data->text_menu_mask = mask;
}

static void OnToggleDeathLink(int val)          { OSReport("[Settings] DeathLink toggled %s\n", stc_off_on[val]); SyncMenuStateToAPData(); }
static void OnToggleEnergyLink(int val)         { OSReport("[Settings] EnergyLink toggled %s\n", stc_off_on[val]); SyncMenuStateToAPData(); }
static void OnChangeAutoCharge(int val)         { OSReport("[Settings] Machine Charge set to %s\n", stc_auto_charge[val]); }
static void OnToggleObjectEnergy(int val)       { OSReport("[Settings] Object energy toggled %s\n", stc_off_on[val]); }
static void OnTogglePatchEnergy(int val)        { OSReport("[Settings] Patch energy toggled %s\n", stc_off_on[val]); }
static void OnToggleChargeEnergy(int val)       { OSReport("[Settings] Charge energy toggled %s\n", stc_off_on[val]); }
static void OnToggleTrapLink(int val)           { OSReport("[Settings] TrapLink toggled %s\n", stc_off_on[val]); SyncMenuStateToAPData(); }
static void OnToggleCTPermanent(int val)        { OSReport("[Settings] CT Permanent Patches toggled %s\n", stc_off_on[val]); }
static void OnToggleCTStadiumPermanent(int val) { OSReport("[Settings] CT Stadium Permanent Patches toggled %s\n", stc_off_on[val]); }
static void OnToggleARPermanent(int val)        { OSReport("[Settings] AR Permanent Patches toggled %s\n", stc_off_on[val]); }
static void OnToggleRandomStartMachine(int val) { OSReport("[Settings] CT Random Start Machine toggled %s\n", stc_off_on[val]); }
static void OnToggleDropAbility(int val)        { OSReport("[Settings] Drop Ability toggled %s\n", stc_off_on[val]); }
static void OnToggleAirQuickSpin(int val)       { OSReport("[Settings] Air Quick Spin toggled %s\n", stc_off_on[val]); }
static void OnToggleOnFootZoom(int val)         { OSReport("[Settings] On-Foot Zoom toggled %s\n", stc_off_on[val]); }
static void OnChangeApBoxRate(int val)          { OSReport("[Settings] AP Box rate set to %s\n", stc_ap_box_rate[val]); }
static void OnToggleCheckMessages(int val)      { OSReport("[Settings] Check messages toggled %s\n", stc_off_on[val]); SyncMenuStateToAPData(); }
static void OnToggleItemMessages(int val)       { OSReport("[Settings] Item messages toggled %s\n", stc_off_on[val]); SyncMenuStateToAPData(); }
static void OnToggleHintMessages(int val)       { OSReport("[Settings] Hint messages toggled %s\n", stc_off_on[val]); SyncMenuStateToAPData(); }
static void OnToggleStatusMessages(int val)     { OSReport("[Settings] Status messages toggled %s\n", stc_off_on[val]); SyncMenuStateToAPData(); }
static void OnToggleChatMessages(int val)       { OSReport("[Settings] Chat messages toggled %s\n", stc_off_on[val]); SyncMenuStateToAPData(); }
static void OnToggleLinkMessages(int val)       { OSReport("[Settings] Link messages toggled %s\n", stc_off_on[val]); SyncMenuStateToAPData(); }
static void OnToggleLocalChecks(int val)        { OSReport("[Settings] Local check messages toggled %s\n", stc_off_on[val]); }
static void OnToggleLocalItems(int val)         { OSReport("[Settings] Local item messages toggled %s\n", stc_off_on[val]); }
static void OnToggleLocalGoals(int val)         { OSReport("[Settings] Local goal messages toggled %s\n", stc_off_on[val]); }
static void OnToggleLocalLinks(int val)         { OSReport("[Settings] Local link messages toggled %s\n", stc_off_on[val]); }

#define OFF_ON_OPTION(_name, _desc, _val, _on_change) \
    &(OptionDesc){                                    \
        .name = _name,                                \
        .description = _desc,                         \
        .kind = OPTKIND_VALUE,                        \
        .val = _val,                                  \
        .value_num = 2,                               \
        .value_names = stc_off_on,                    \
        .on_change = _on_change,                      \
    }

static MenuDesc local_messages_menu = {
    .option_num = 4,
    .options = {
        OFF_ON_OPTION("Checks", "Show when checks are recorded",
                      &ap_menu_settings.local_messages[APLOCAL_CHECK], OnToggleLocalChecks),
        OFF_ON_OPTION("Items", "Show when items are applied",
                      &ap_menu_settings.local_messages[APLOCAL_ITEM], OnToggleLocalItems),
        OFF_ON_OPTION("Goals", "Show when goals are complete",
                      &ap_menu_settings.local_messages[APLOCAL_GOAL], OnToggleLocalGoals),
        OFF_ON_OPTION("Links", "Show when a DeathLink or TrapLink is sent, and when one lands",
                      &ap_menu_settings.local_messages[APLOCAL_LINK], OnToggleLocalLinks),
    },
};

static MenuDesc messages_menu = {
    .option_num = 7,
    .options = {
        OFF_ON_OPTION("Checks", "Show which item a completed checkbox sent, and to whom",
                      &ap_menu_settings.text_messages[APTEXT_KIND_CHECK], OnToggleCheckMessages),
        OFF_ON_OPTION("Items", "Show received items and who found them",
                      &ap_menu_settings.text_messages[APTEXT_KIND_ITEM], OnToggleItemMessages),
        OFF_ON_OPTION("Hints", "Show server hints for your items and for items hidden in your world",
                      &ap_menu_settings.text_messages[APTEXT_KIND_HINT], OnToggleHintMessages),
        OFF_ON_OPTION("Status", "Show goal/release/collect and client connection changes",
                      &ap_menu_settings.text_messages[APTEXT_KIND_STATUS], OnToggleStatusMessages),
        OFF_ON_OPTION("Chat", "Show player and server chat",
                      &ap_menu_settings.text_messages[APTEXT_KIND_CHAT], OnToggleChatMessages),
        OFF_ON_OPTION("Links", "Show DeathLink and TrapLink traffic, and who sent it",
                      &ap_menu_settings.text_messages[APTEXT_KIND_LINK], OnToggleLinkMessages),
        &(OptionDesc){
            .name = "Local",
            .description = "Messages the mod writes itself, with or without a client",
            .kind = OPTKIND_MENU,
            .menu_ptr = &local_messages_menu,
        },
    },
};

// Round-start application only; received permanent patches count either way.
static MenuDesc permanent_patches_menu = {
    .option_num = 3,
    .options = {
        OFF_ON_OPTION("City Trial", "Apply permanent patches at the start of each City Trial round",
                      &ap_menu_settings.ct_permanent_patches_enabled, OnToggleCTPermanent),
        OFF_ON_OPTION("CT Stadium", "Apply permanent patches in stadiums picked from the Stadium menu",
                      &ap_menu_settings.ct_stadium_permanent_patches_enabled, OnToggleCTStadiumPermanent),
        OFF_ON_OPTION("Air Ride", "Apply permanent patches at the start of each Air Ride round",
                      &ap_menu_settings.ar_permanent_patches_enabled, OnToggleARPermanent),
    },
};

static MenuDesc energy_sources_menu = {
    .option_num = 3,
    .options = {
        OFF_ON_OPTION("Objects", "Earn energy by destroying objects in City Trial",
                      &ap_menu_settings.energy_sources[APENERGYSRC_OBJECTS], OnToggleObjectEnergy),
        OFF_ON_OPTION("Patches", "Earn energy by collecting stat patches",
                      &ap_menu_settings.energy_sources[APENERGYSRC_PATCHES], OnTogglePatchEnergy),
        OFF_ON_OPTION("Charge", "Earn energy by charging your machine",
                      &ap_menu_settings.energy_sources[APENERGYSRC_CHARGE], OnToggleChargeEnergy),
    },
};

static MenuDesc energylink_menu = {
    .option_num = 4,
    .options = {
        OFF_ON_OPTION("Energy Link", "Share energy with the multiworld",
                      &ap_menu_settings.energylink_enabled, OnToggleEnergyLink),
        &(OptionDesc){
            .name = "Machine Charge",
            .description = "Spend energy to fill your charge meter, at this rate",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.auto_charge,
            .value_num = APAUTOCHARGE_NUM,
            .value_names = stc_auto_charge,
            .on_change = OnChangeAutoCharge,
        },
        &(OptionDesc){
            .name = "Sources",
            .description = "Choose what earns energy",
            .kind = OPTKIND_MENU,
            .menu_ptr = &energy_sources_menu,
        },
        &(OptionDesc){
            .name = "Spend",
            .description = "Purchase items with pooled energy",
            .kind = OPTKIND_MENU,
            .menu_ptr = &energylink_spend_menu,
        },
    },
};

static MenuDesc root_menu = {
    .option_num = 10,
    .options = {
        OFF_ON_OPTION("Death Link", "Enable or Disable Death Link",
                      &ap_menu_settings.deathlink_enabled, OnToggleDeathLink),
        &(OptionDesc){
            .name = "Energy Link",
            .description = "Energy Link settings and shop",
            .kind = OPTKIND_MENU,
            .menu_ptr = &energylink_menu,
        },
        OFF_ON_OPTION("Trap Link", "Enable or Disable Trap Link",
                      &ap_menu_settings.traplink_enabled, OnToggleTrapLink),
        &(OptionDesc){
            .name = "Messages",
            .description = "Choose which Archipelago messages appear in the text box",
            .kind = OPTKIND_MENU,
            .menu_ptr = &messages_menu,
        },
        &(OptionDesc){
            .name = "Permanent Patches",
            .description = "Controls where permanent patches are applied",
            .kind = OPTKIND_MENU,
            .menu_ptr = &permanent_patches_menu,
        },
        OFF_ON_OPTION("Random Start Machine", "Start a City Trial run on a random unlocked machine instead of Compact",
                      &ap_menu_settings.ct_random_start_machine, OnToggleRandomStartMachine),
        &(OptionDesc){
            .name = "AP Box Rate",
            .description = "How often AP Boxes fall in City Trial",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.ap_box_rate,
            .value_num = APBOXRATE_NUM,
            .value_names = stc_ap_box_rate,
            .on_change = OnChangeApBoxRate,
        },
        OFF_ON_OPTION("Drop Ability", "Press Z to discard your copy ability, or your item/power in Top Ride",
                      &ap_menu_settings.drop_ability_enabled, OnToggleDropAbility),
        OFF_ON_OPTION("Air Quick Spin", "Allow quick spinning in the air",
                      &ap_menu_settings.air_quick_spin_enabled, OnToggleAirQuickSpin),
        OFF_ON_OPTION("On-Foot Zoom", "Allow camera zoom control when off of a machine",
                      &ap_menu_settings.onfoot_zoom_enabled, OnToggleOnFootZoom),
    },
};

OptionDesc ModSettings = {
    .name = "Archipelago Settings",
    .description = "Interface with mod settings here",
    .kind = OPTKIND_MENU,
    .menu_ptr = &root_menu,
};
