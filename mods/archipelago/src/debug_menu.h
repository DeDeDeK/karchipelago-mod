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

// Pull every gate mask from ap_save back into the debug menu's local toggle
// state arrays. Reverse of DebugMenu_ApplyToSave. Call after anything that
// modifies masks outside the debug menu (AP grants, regrant at OnSaveLoaded)
// so the menu display reflects the true unlock state.
void DebugMenu_RefreshStateFromMasks(void);

// Returns nonzero if the "Auto-Grant on Z Unlock" debug toggle is enabled.
// When enabled, the Z-button debug checklist unlock path also simulates AP
// item receipt by granting whatever reward is placed at the unlocked cell.
// When disabled, only the check is sent — the AP server is responsible for
// delivering the corresponding item.
int DebugMenu_ShouldAutoGrantOnUnlock(void);

#endif // DEBUG_MENU_H
