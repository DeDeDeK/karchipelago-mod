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

// Menu toggle state, bound to the Settings menu via OptionDesc. Seeded from
// APSlotOptions on first connect, then owned by the player.
typedef struct APMenuSettings
{
    int deathlink_enabled;
    int energylink_enabled;
    int energylink_autocharge; // 0 = off, else AUTOCHARGE_RATES index + 1
    int traplink_enabled;
    int ct_permanent_patches_enabled;
    int ct_stadium_permanent_patches_enabled;
    int ar_permanent_patches_enabled;
    int ct_random_start_machine;
    int drop_ability_enabled;
    int air_quick_spin_enabled;
    int onfoot_zoom_enabled;
    int ap_box_rate;
    int text_messages[APTEXT_KIND_NUM];
    int local_messages[APLOCAL_NUM];
} APMenuSettings;

extern APMenuSettings ap_menu_settings;

// Top-level settings page wired into mod_desc.option_desc.
extern OptionDesc ModSettings;

// Publishes the link toggles and the message-kind mask into APData for the client.
// Safe any time after OnBoot allocates ap_data.
void SyncMenuStateToAPData(void);

#endif // ARCHIPELAGO_SETTINGS_MENU_H
