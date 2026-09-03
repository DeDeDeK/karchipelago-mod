#ifndef ARCHIPELAGO_AP_CHECKLIST_H
#define ARCHIPELAGO_AP_CHECKLIST_H

// Display name and theme color of the AP checklist tab, shared by the tab descriptor
// and by every textbox that names the tab: the tab's framework-assigned mode is
// >= GMMODE_NUM and so has no ModeColors[] slot of its own.
#define AP_CHECKLIST_NAME "Archipelago"
#define AP_THEME_R 40
#define AP_THEME_G 120
#define AP_THEME_B 230

// Hand the AP descriptor to the custom_checklist framework, which owns the tab's
// presentation and per-frame check evaluation thereafter. Idempotent. Call from
// OnSaveLoaded, not OnBoot: the framework mod boots after archipelago, so its API
// only resolves once all mods have exported.
void APChecklist_Register(void);

// 1 once custom_checklist has accepted the tab and its clear data exists. Anything
// that indexes the AP row's clear data has to check this first: without the framework
// in the build the vanilla accessor asserts on a mode past City Trial.
int APChecklist_IsRegistered(void);

// Make every AP checklist cell that backs an objective visible. Only the cells in
// ap_checks[] are revealed - the rest of the 120-cell grid has no objective behind
// it. No-op if the tab is not registered.
void APChecklist_RevealAll(void);

#endif // ARCHIPELAGO_AP_CHECKLIST_H
