#include "game.h"
#include "os.h"
#include "hoshi/settings.h"

#include "main.h"
#include "settings_menu.h"
#include "energylink_spend.h"

// Hoshi's Mod_CopyFromSave overwrites these later if a saved hash exists.
APMenuSettings ap_menu_settings = {
    .ct_permanent_patches_enabled         = 1,
    .ct_stadium_permanent_patches_enabled = 1,
    .ar_permanent_patches_enabled         = 1,
    .energylink_autocharge_rate           = 1,
    .ct_random_start_machine              = 1,
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

static const char *stc_off_on[] = {"Off", "On"};
static const char *stc_slow_med_fast[] = {"Slow", "Medium", "Fast"};

void SyncMenuStateToAPData(void)
{
    if (!ap_data)
        return;
    ap_data->deathlink_menu_enabled  = ap_menu_settings.deathlink_enabled;
    ap_data->energylink_menu_enabled = ap_menu_settings.energylink_enabled;
    ap_data->traplink_menu_enabled   = ap_menu_settings.traplink_enabled;

    u32 mask = 0;
    for (int i = 0; i < APTEXT_KIND_NUM; i++)
        if (ap_menu_settings.text_messages[i])
            mask |= 1 << i;
    ap_data->text_menu_mask = mask;
}

static void OnToggleDeathLink(int val)          { OSReport("[Settings] DeathLink toggled %s\n", stc_off_on[val]); SyncMenuStateToAPData(); }
static void OnToggleEnergyLink(int val)         { OSReport("[Settings] EnergyLink toggled %s\n", stc_off_on[val]); SyncMenuStateToAPData(); }
static void OnToggleAutoCharge(int val)         { OSReport("[Settings] EnergyLink AutoCharge toggled %s\n", stc_off_on[val]); }
static void OnChangeAutoChargeRate(int val)     { OSReport("[Settings] EnergyLink AutoCharge rate set to %s\n", stc_slow_med_fast[val]); }
static void OnToggleTrapLink(int val)           { OSReport("[Settings] TrapLink toggled %s\n", stc_off_on[val]); SyncMenuStateToAPData(); }
static void OnToggleCTPermanent(int val)        { OSReport("[Settings] CT Permanent Patches toggled %s\n", stc_off_on[val]); }
static void OnToggleCTStadiumPermanent(int val) { OSReport("[Settings] CT Stadium Permanent Patches toggled %s\n", stc_off_on[val]); }
static void OnToggleARPermanent(int val)        { OSReport("[Settings] AR Permanent Patches toggled %s\n", stc_off_on[val]); }
static void OnToggleRandomStartMachine(int val) { OSReport("[Settings] CT Random Start Machine toggled %s\n", stc_off_on[val]); }
static void OnToggleDropAbility(int val)         { OSReport("[Settings] Drop Ability toggled %s\n", stc_off_on[val]); }
static void OnToggleAirQuickSpin(int val)        { OSReport("[Settings] Air Quick Spin toggled %s\n", stc_off_on[val]); }
static void OnToggleOnFootZoom(int val)          { OSReport("[Settings] On-Foot Zoom toggled %s\n", stc_off_on[val]); }
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

// The lines the mod composes itself. Checks, items and links default off: a client
// attached to the same event posts a richer line a poll later.
static MenuDesc local_messages_menu = {
    .option_num = 4,
    .options = {
        &(OptionDesc){
            .name = "Checks",
            .description = "Show when checks are recorded",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.local_messages[APLOCAL_CHECK],
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleLocalChecks,
        },
        &(OptionDesc){
            .name = "Items",
            .description = "Show when items are applied",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.local_messages[APLOCAL_ITEM],
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleLocalItems,
        },
        &(OptionDesc){
            .name = "Goals",
            .description = "Show when goals are complete",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.local_messages[APLOCAL_GOAL],
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleLocalGoals,
        },
        &(OptionDesc){
            .name = "Links",
            .description = "Show when a DeathLink or TrapLink is sent, and when one lands",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.local_messages[APLOCAL_LINK],
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleLocalLinks,
        },
    },
};

static MenuDesc messages_menu = {
    .option_num = 7,
    .options = {
        &(OptionDesc){
            .name = "Checks",
            .description = "Show which item a completed checkbox sent, and to whom",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.text_messages[APTEXT_KIND_CHECK],
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleCheckMessages,
        },
        &(OptionDesc){
            .name = "Items",
            .description = "Show received items and who found them",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.text_messages[APTEXT_KIND_ITEM],
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleItemMessages,
        },
        &(OptionDesc){
            .name = "Hints",
            .description = "Show server hints for your items and for items hidden in your world",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.text_messages[APTEXT_KIND_HINT],
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleHintMessages,
        },
        &(OptionDesc){
            .name = "Status",
            .description = "Show goal/release/collect and client connection changes",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.text_messages[APTEXT_KIND_STATUS],
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleStatusMessages,
        },
        &(OptionDesc){
            .name = "Chat",
            .description = "Show player and server chat",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.text_messages[APTEXT_KIND_CHAT],
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleChatMessages,
        },
        &(OptionDesc){
            .name = "Links",
            .description = "Show DeathLink and TrapLink traffic, and who sent it",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.text_messages[APTEXT_KIND_LINK],
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleLinkMessages,
        },
        &(OptionDesc){
            .name = "Local",
            .description = "Offline messages settings",
            .kind = OPTKIND_MENU,
            .menu_ptr = &local_messages_menu,
        },
    },
};

// Only round-start application is toggled here - receiving AP permanent-patch items
// still increments the save counters either way.
static MenuDesc permanent_patches_menu = {
    .option_num = 3,
    .options = {
        &(OptionDesc){
            .name = "City Trial",
            .description = "Apply permanent patches at the start of each City Trial round",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.ct_permanent_patches_enabled,
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleCTPermanent,
        },
        &(OptionDesc){
            .name = "CT Stadium",
            .description = "Apply permanent patches when entering a City Trial stadium",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.ct_stadium_permanent_patches_enabled,
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleCTStadiumPermanent,
        },
        &(OptionDesc){
            .name = "Air Ride",
            .description = "Apply permanent patches at the start of each Air Ride race",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.ar_permanent_patches_enabled,
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleARPermanent,
        },
    },
};

OptionDesc ModSettings = {
    .name = "Archipelago Settings",
    .description = "Interface with mod settings here",
    .kind = OPTKIND_MENU,
    .menu_ptr = &(MenuDesc){
        .option_num = 9,
        .options = {
            &(OptionDesc){
                .name = "Death Link",
                .description = "Enable or Disable Death Link",
                .kind = OPTKIND_VALUE,
                .val = &ap_menu_settings.deathlink_enabled,
                .value_num = 2,
                .value_names = (char *[]){
                    "Off",
                    "On",
                },
                .on_change = OnToggleDeathLink,
            },
            &(OptionDesc){
                .name = "Energy Link",
                .description = "Energy Link settings and shop",
                .kind = OPTKIND_MENU,
                .menu_ptr = &(MenuDesc){
                    .option_num = 4,
                    .options = {
                        &(OptionDesc){
                            .name = "Enabled",
                            .description = "Enable or Disable Energy Link",
                            .kind = OPTKIND_VALUE,
                            .val = &ap_menu_settings.energylink_enabled,
                            .value_num = 2,
                            .value_names = (char *[]){
                                "Off",
                                "On",
                            },
                            .on_change = OnToggleEnergyLink,
                        },
                        &(OptionDesc){
                            .name = "Auto-Charge",
                            .description = "Automatically spend energy to fill machine charge meter",
                            .kind = OPTKIND_VALUE,
                            .val = &ap_menu_settings.energylink_autocharge,
                            .value_num = 2,
                            .value_names = (char *[]){
                                "Off",
                                "On",
                            },
                            .on_change = OnToggleAutoCharge,
                        },
                        &(OptionDesc){
                            .name = "Auto-Charge Rate",
                            .description = "How fast Auto-Charge fills the meter",
                            .kind = OPTKIND_VALUE,
                            .val = &ap_menu_settings.energylink_autocharge_rate,
                            .value_num = 3,
                            .value_names = (char *[]){
                                "Slow",
                                "Medium",
                                "Fast",
                            },
                            .on_change = OnChangeAutoChargeRate,
                        },
                        &(OptionDesc){
                            .name = "Spend",
                            .description = "Purchase items with pooled energy",
                            .kind = OPTKIND_MENU,
                            .menu_ptr = &energylink_spend_menu,
                        },
                    },
                },
            },
            &(OptionDesc){
                .name = "Trap Link",
                .description = "Enable or Disable Trap Link",
                .kind = OPTKIND_VALUE,
                .val = &ap_menu_settings.traplink_enabled,
                .value_num = 2,
                .value_names = (char *[]){
                    "Off",
                    "On",
                },
                .on_change = OnToggleTrapLink,
            },
            &(OptionDesc){
                .name = "Messages",
                .description = "Choose which Archipelago messages appear in the text box",
                .kind = OPTKIND_MENU,
                .menu_ptr = &messages_menu,
            },
            &(OptionDesc){
                .name = "Permanent Patches",
                .description = "Control whether permanent patches are re-applied at round start",
                .kind = OPTKIND_MENU,
                .menu_ptr = &permanent_patches_menu,
            },
            &(OptionDesc){
                .name = "Random Start Machine",
                .description = "Start City Trial on a random unlocked machine instead of Compact",
                .kind = OPTKIND_VALUE,
                .val = &ap_menu_settings.ct_random_start_machine,
                .value_num = 2,
                .value_names = (char *[]){
                    "Off",
                    "On",
                },
                .on_change = OnToggleRandomStartMachine,
            },
            &(OptionDesc){
                .name = "Drop Ability",
                .description = "Press Z to discard your copy ability, or your item/power in Top Ride",
                .kind = OPTKIND_VALUE,
                .val = &ap_menu_settings.drop_ability_enabled,
                .value_num = 2,
                .value_names = (char *[]){
                    "Off",
                    "On",
                },
                .on_change = OnToggleDropAbility,
            },
            &(OptionDesc){
                .name = "Air Quick Spin",
                .description = "Allow quick spinning in the air",
                .kind = OPTKIND_VALUE,
                .val = &ap_menu_settings.air_quick_spin_enabled,
                .value_num = 2,
                .value_names = (char *[]){
                    "Off",
                    "On",
                },
                .on_change = OnToggleAirQuickSpin,
            },
            &(OptionDesc){
                .name = "On-Foot Zoom",
                .description = "Allow camera zoom control when off of a machine",
                .kind = OPTKIND_VALUE,
                .val = &ap_menu_settings.onfoot_zoom_enabled,
                .value_num = 2,
                .value_names = (char *[]){
                    "Off",
                    "On",
                },
                .on_change = OnToggleOnFootZoom,
            },
        },
    },
};
