#ifndef ARCHIPELAGO_DEBUG_MENU_H
#define ARCHIPELAGO_DEBUG_MENU_H

#include "hoshi/settings.h"
#include "archipelago_api.h"

// Cached pointer to the archipelago mod's exported API. NULL until imported.
extern const ArchipelagoAPI *ap_api;

// Top-level descriptor wired into the debug mod's settings menu.
extern OptionDesc DebugMod_RootOption;

// Pull every gate mask from archipelago via the API into the debug menu's
// local toggle state arrays. Reverse of the menu's on_change writeback. Call
// after anything that changes masks outside the menu (AP grants, save load).
void DebugMenu_RefreshStateFromMasks(void);

// Returns nonzero if the "Auto-Grant on Z Unlock" debug toggle is enabled.
int DebugMenu_ShouldAutoGrantOnUnlock(void);

#endif // ARCHIPELAGO_DEBUG_MENU_H
