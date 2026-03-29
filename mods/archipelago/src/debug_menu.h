#ifndef DEBUG_MENU_H
#define DEBUG_MENU_H

#include "hoshi/settings.h"

// Menu descriptor for the Debug submenu.
// Plug into the main settings menu as an OPTKIND_MENU option.
extern MenuDesc debug_menu;

// Apply hoshi-saved toggle states to save data masks.
// Call after save data is loaded (e.g. in OnSaveLoaded).
// Hoshi restores menu values automatically, so this writes them into the masks.
void DebugMenu_ApplyToSave(void);

#endif // DEBUG_MENU_H
