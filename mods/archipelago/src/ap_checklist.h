#ifndef ARCHIPELAGO_AP_CHECKLIST_H
#define ARCHIPELAGO_AP_CHECKLIST_H

// Register the AP checklist as a custom checklist tab with the custom_checklist
// framework: imports the framework API and hands it the AP descriptor (checks,
// blue theme, tab art, and the sent_checks_ap record callbacks). The framework
// owns the tab's presentation and per-frame check evaluation thereafter.
//
// Call from OnSaveLoaded (not OnBoot): the framework mod boots after archipelago,
// so its API only resolves once all mods have exported. Idempotent.
void APChecklist_Register(void);

#endif // ARCHIPELAGO_AP_CHECKLIST_H
