#ifndef ARCHIPELAGO_DEBUG_MENU_H
#define ARCHIPELAGO_DEBUG_MENU_H

#include "hoshi/settings.h"
#include "archipelago_api.h"

// NULL until imported.
extern const ArchipelagoAPI *ap_api;

extern OptionDesc DebugMod_RootOption;

// Pull every gate mask into the local toggle state arrays; the reverse of the
// menu's on_change writeback. Call after masks change outside the menu.
void DebugMenu_RefreshStateFromMasks(void);

// Nonzero if the "Auto-Grant on Z Unlock" toggle is enabled.
int DebugMenu_ShouldAutoGrantOnUnlock(void);

#endif // ARCHIPELAGO_DEBUG_MENU_H
