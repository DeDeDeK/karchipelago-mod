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

// Make every AP checklist cell that backs an objective visible, the tab's half of the
// reveal_checklists option and the debug menu's "Reveal All Checklists". Only the cells
// in ap_checks[] are revealed - the rest of the 120-cell grid has no objective behind it
// and would render as boxes that can never be checked. No-op if the tab is not registered.
void APChecklist_RevealAll(void);

#endif // ARCHIPELAGO_AP_CHECKLIST_H
