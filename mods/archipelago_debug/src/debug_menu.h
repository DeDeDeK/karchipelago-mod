#ifndef ARCHIPELAGO_DEBUG_MENU_H
#define ARCHIPELAGO_DEBUG_MENU_H

#include "hoshi/settings.h"
#include "archipelago_api.h"
#include "custom_machines_api.h"

// NULL until imported.
extern const ArchipelagoAPI *ap_api;

// Name and expose one Machines-menu row per registered custom machine, and widen the
// machine unlock mask the menu drives. Ignored when the registry is absent.
void DebugMenu_BindCustomMachines(const CustomMachinesAPI *api);

extern OptionDesc DebugMod_RootOption;

// Pull every gate mask into the local toggle state arrays; the reverse of the
// menu's on_change writeback. Call after masks change outside the menu.
void DebugMenu_RefreshStateFromMasks(void);

// Nonzero if the "Auto-Grant on Z Unlock" toggle is enabled.
int DebugMenu_ShouldAutoGrantOnUnlock(void);

#endif // ARCHIPELAGO_DEBUG_MENU_H
