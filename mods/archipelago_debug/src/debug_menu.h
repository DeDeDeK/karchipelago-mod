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

// Pull every gate mask, slot option and progress counter into the local row state;
// the reverse of the menu's on_change writeback. Call after any of them changes
// outside the menu, and before boot replays every on_change.
void DebugMenu_RefreshState(void);

// Player slot the pad bindings that drop an item act on, 0-3.
int DebugMenu_TargetPlayer(void);

int DebugMenu_ShouldAutoGrantOnUnlock(void);

// Queue one item at random: any AP unlock or persistent progression item, or one
// appropriate to the major mode a round is running under.
void DebugMenu_GiveRandomUnlock(void);
void DebugMenu_GiveRandomModeItem(MajorKind major);

#endif // ARCHIPELAGO_DEBUG_MENU_H
