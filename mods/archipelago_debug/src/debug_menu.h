#ifndef ARCHIPELAGO_DEBUG_MENU_H
#define ARCHIPELAGO_DEBUG_MENU_H

#include "hoshi/settings.h"
#include "archipelago_api.h"
#include "textbox_api.h"

// NULL until imported.
extern const ArchipelagoAPI *ap_api;
extern const TextBoxAPI *tb_api;

extern OptionDesc DebugMod_RootOption;

// Pull every gate mask, slot option and progress counter into the local row state;
// the reverse of the menu's on_change writeback. Call after any of them changes
// outside the menu, and before boot replays every on_change.
void DebugMenu_RefreshState(void);

int DebugMenu_ShouldAutoGrantOnUnlock(void);

#endif // ARCHIPELAGO_DEBUG_MENU_H
