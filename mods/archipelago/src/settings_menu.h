#ifndef ARCHIPELAGO_SETTINGS_MENU_H
#define ARCHIPELAGO_SETTINGS_MENU_H

#include "hoshi/settings.h"

// In-game menu toggle state, bound to the Settings menu via OptionDesc. Initial
// values come from APSlotOptions on first connect; the player can override them.
typedef struct APMenuSettings
{
    int deathlink_enabled;
    int energylink_enabled;
    int energylink_autocharge;
    int energylink_autocharge_rate; // index into AUTOCHARGE_RATES: 0=Slow, 1=Medium, 2=Fast
    int traplink_enabled;
    int ct_permanent_patches_enabled;
    int ct_stadium_permanent_patches_enabled;
    int ar_permanent_patches_enabled;
    int ct_random_start_machine; // start City Trial on a random unlocked machine
    int drop_ability_enabled;    // press Z to discard the copy ability (CT/AR) or ability-power item (TR)
    int air_quick_spin_enabled;  // allow the L/R-flick quick spin while airborne (CT/AR)
    int onfoot_zoom_enabled;     // allow C-Stick camera zoom while on foot (CT)
    int ap_star_shot_enabled;    // Archipelago Star fires a sphere on every full-charge release
} APMenuSettings;

extern APMenuSettings ap_menu_settings;

// Top-level settings page wired into mod_desc.option_desc.
extern OptionDesc ModSettings;

// Publish the current death/energy/trap link menu state into APData so the Python
// client can forward a mid-session toggle to the AP server. Safe to call any time
// after OnBoot allocates ap_data.
void SyncLinkMenuStateToAPData(void);

#endif // ARCHIPELAGO_SETTINGS_MENU_H
