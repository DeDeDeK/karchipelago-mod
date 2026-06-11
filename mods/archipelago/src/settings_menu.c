#include "game.h"
#include "os.h"
#include "hoshi/settings.h"

#include "main.h"
#include "settings_menu.h"
#include "energylink_spend.h"

// Defaults match pre-toggle behavior so existing installs keep working on
// first boot. Hoshi's Mod_CopyFromSave overwrites these later if a saved hash
// exists.
APMenuSettings ap_menu_settings = {
    .ct_permanent_patches_enabled         = 1,
    .ct_stadium_permanent_patches_enabled = 1,
    .ar_permanent_patches_enabled         = 1,
    .energylink_autocharge_rate           = 1, // Medium by default (~1.5s to fill)
};

static const char *stc_off_on[] = {"Off", "On"};
static const char *stc_slow_med_fast[] = {"Slow", "Medium", "Fast"};

void SyncLinkMenuStateToAPData(void)
{
    if (!ap_data)
        return;
    ap_data->deathlink_menu_enabled  = ap_menu_settings.deathlink_enabled;
    ap_data->energylink_menu_enabled = ap_menu_settings.energylink_enabled;
    ap_data->traplink_menu_enabled   = ap_menu_settings.traplink_enabled;
}

static void OnToggleDeathLink(int val)          { OSReport("[Main] DeathLink toggled %s\n", stc_off_on[val]); SyncLinkMenuStateToAPData(); }
static void OnToggleEnergyLink(int val)         { OSReport("[Main] EnergyLink toggled %s\n", stc_off_on[val]); SyncLinkMenuStateToAPData(); }
static void OnToggleAutoCharge(int val)         { OSReport("[Main] EnergyLink AutoCharge toggled %s\n", stc_off_on[val]); }
static void OnChangeAutoChargeRate(int val)     { OSReport("[Main] EnergyLink AutoCharge rate set to %s\n", stc_slow_med_fast[val]); }
static void OnToggleTrapLink(int val)           { OSReport("[Main] TrapLink toggled %s\n", stc_off_on[val]); SyncLinkMenuStateToAPData(); }
static void OnToggleCTPermanent(int val)        { OSReport("[Main] CT Permanent Patches toggled %s\n", stc_off_on[val]); }
static void OnToggleCTStadiumPermanent(int val) { OSReport("[Main] CT Stadium Permanent Patches toggled %s\n", stc_off_on[val]); }
static void OnToggleARPermanent(int val)        { OSReport("[Main] AR Permanent Patches toggled %s\n", stc_off_on[val]); }

// Submenu: controls whether accumulated permanent stat patches are re-applied
// at the start of each round/race. Receiving AP permanent-patch items still
// increments save counters unconditionally - the toggles only gate round-start
// application. Default: all On, matching the historical behavior before the
// toggles existed.
static MenuDesc permanent_patches_menu = {
    .option_num = 3,
    .options = {
        &(OptionDesc){
            .name = "City Trial",
            .description = "Apply accumulated permanent patches at the start of each City Trial round",
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
            .description = "Apply accumulated permanent patches when entering a City Trial stadium",
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
            .description = "Apply accumulated permanent patches at the start of each Air Ride race",
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

// Top-level Archipelago Settings menu. Wired into mod_desc.option_desc in main.c.
OptionDesc ModSettings = {
    .name = "Archipelago Settings",
    .description = "Interface with mod settings here.",
    .kind = OPTKIND_MENU,
    .menu_ptr = &(MenuDesc){
        .option_num = 4,
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
                .description = "Energy Link settings and item shop.",
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
                            .description = "How fast Auto-Charge fills the meter (slower = energy lasts longer)",
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
                            .description = "Purchase items with pooled energy.",
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
                .name = "Permanent Patches",
                .description = "Control whether permanent patches are re-applied at round start",
                .kind = OPTKIND_MENU,
                .menu_ptr = &permanent_patches_menu,
            },
        },
    },
};
