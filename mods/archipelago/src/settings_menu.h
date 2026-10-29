#ifndef ARCHIPELAGO_SETTINGS_MENU_H
#define ARCHIPELAGO_SETTINGS_MENU_H

#include "hoshi/settings.h"

#include "main.h"
#include "ap_announce.h"

// How often an AP Box comes up as an outcome of the City Trial box roll.
typedef enum APBoxRate
{
    APBOXRATE_RARE,
    APBOXRATE_LOW,
    APBOXRATE_MEDIUM,
    APBOXRATE_HIGH,
    APBOXRATE_NUM,
} APBoxRate;

// Rate at which Auto-Charge spends Energy Link energy filling the charge meter.
typedef enum APAutoCharge
{
    APAUTOCHARGE_OFF,
    APAUTOCHARGE_SLOW,
    APAUTOCHARGE_MEDIUM,
    APAUTOCHARGE_FAST,
    APAUTOCHARGE_NUM,
} APAutoCharge;

typedef enum APEnergySource
{
    APENERGYSRC_OBJECTS,
    APENERGYSRC_PATCHES,
    APENERGYSRC_CHARGE,
    APENERGYSRC_NUM,
} APEnergySource;

// Settings menu state. The link toggles are seeded from the slot options on first connect.
typedef struct APMenuSettings
{
    int deathlink_enabled;
    int energylink_enabled;
    int auto_charge;           // APAutoCharge
    int energy_sources[APENERGYSRC_NUM];
    int traplink_enabled;
    int ct_permanent_patches_enabled;
    int ct_stadium_permanent_patches_enabled;
    int ar_permanent_patches_enabled;
    int ct_random_start_machine;
    int drop_ability_enabled;
    int air_quick_spin_enabled;
    int onfoot_zoom_enabled;
    int ap_box_rate;           // APBoxRate
    int text_messages[APTEXT_KIND_NUM];
    int local_messages[APLOCAL_NUM];
} APMenuSettings;

extern APMenuSettings ap_menu_settings;

extern OptionDesc ModSettings;

static inline int SettingsMenu_EnergyLinkEnabled(void)
{
    return ap_menu_settings.energylink_enabled;
}

static inline int SettingsMenu_EnergySourceEnabled(APEnergySource src)
{
    return ap_menu_settings.energy_sources[src];
}

// Auto-Charge rate 0-2, or -1 while Auto-Charge is off.
static inline int SettingsMenu_AutoChargeRate(void)
{
    return ap_menu_settings.auto_charge - APAUTOCHARGE_SLOW;
}

void SettingsMenu_SeedFromSlotOptions(const APSlotOptions *opts);

// Publishes the link toggles and the message-kind mask into APData for the client.
void SyncMenuStateToAPData(void);

#endif // ARCHIPELAGO_SETTINGS_MENU_H
