#ifndef ARCHIPELAGO_AP_CHECKLIST_H
#define ARCHIPELAGO_AP_CHECKLIST_H

// Display name and theme color of the AP checklist tab. Shared by the tab descriptor
// and by every textbox that names the tab, so the wording and tint stay one value:
// the tab's framework-assigned mode is >= GMMODE_NUM and so has no ModeColors[] slot
// of its own. The dominant channel sets the hue - the framework retints City Trial's
// green grid template onto it, since it borrows CT's template. Tunable.
#define AP_CHECKLIST_NAME "Archipelago"
#define AP_THEME_R 40
#define AP_THEME_G 120
#define AP_THEME_B 230

// Register the AP checklist as a custom checklist tab with the custom_checklist
// framework: imports the framework API and hands it the AP descriptor (checks,
// blue theme, tab art, and the sent_checks_ap record callbacks). The framework
// owns the tab's presentation and per-frame check evaluation thereafter.
//
// Call from OnSaveLoaded (not OnBoot): the framework mod boots after archipelago,
// so its API only resolves once all mods have exported. Idempotent.
void APChecklist_Register(void);

#endif // ARCHIPELAGO_AP_CHECKLIST_H
