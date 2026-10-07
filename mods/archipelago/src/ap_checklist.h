#ifndef ARCHIPELAGO_AP_CHECKLIST_H
#define ARCHIPELAGO_AP_CHECKLIST_H

#include "gx.h"

// The AP tab's display name and theme color.
#define AP_CHECKLIST_NAME "Archipelago"
#define AP_THEME_COLOR {40, 120, 230, 255}

// Registers the AP tab with custom_checklist. Idempotent; call from OnSaveLoaded, since
// custom_checklist boots after archipelago.
void APChecklist_Register(void);

// 1 once custom_checklist has accepted the tab and its clear data exists. Without the
// framework the vanilla accessor asserts on a mode past City Trial, so anything indexing
// the AP row's clear data checks this first.
int APChecklist_IsRegistered(void);

// The checklist mode of the tab the framework is building, or -1 outside a build. A build
// runs under GMMODE_CITYTRIAL while the clear data is already the custom tab's.
int APChecklist_GetBuildMode(void);

// Makes every AP cell visible for the session. No-op if the tab is not registered.
void APChecklist_RevealAll(void);

// Display name and textbox color of a checklist-mode row.
const char *APChecklist_RowName(int row);
GXColor APChecklist_RowColor(int row);

#endif // ARCHIPELAGO_AP_CHECKLIST_H
