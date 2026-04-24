#ifndef ARCHIPELAGO_SETTINGS_MENU_H
#define ARCHIPELAGO_SETTINGS_MENU_H

#include "hoshi/settings.h"

// In-game menu toggle state. Bound to the Settings menu via OptionDesc.
// Initial values are set from APSlotOptions on first connect; the player
// can override them at any time via the Settings menu.
typedef struct APMenuSettings
{
    int deathlink_enabled;
    int energylink_enabled;
    int energylink_autocharge;
    int traplink_enabled;
    int textbox_enabled;
    int ct_permanent_patches_enabled;
    int ct_stadium_permanent_patches_enabled;
    int ar_permanent_patches_enabled;
} APMenuSettings;

extern APMenuSettings ap_menu_settings;

// Top-level settings page wired into mod_desc.option_desc.
extern OptionDesc ModSettings;

// Publish the current death/energy/trap link menu state into APData so the
// Python client can forward enable/disable to the AP server when the player
// toggles mid-session. Safe to call any time after OnBoot allocates ap_data.
void SyncLinkMenuStateToAPData(void);

#endif // ARCHIPELAGO_SETTINGS_MENU_H
