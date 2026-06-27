#ifndef ARCHIPELAGO_AP_CHECKLIST_H
#define ARCHIPELAGO_AP_CHECKLIST_H

#include "game.h"

// Install the AP-checklist (AP_CHECKLIST_MODE) clear-checker plumbing: the
// mod-owned GameClearData and the gmGetClearcheckerTypeP REPLACEFUNC that serves
// it. Call from OnBoot.
void APChecklist_OnBoot(void);

// Per-frame evaluator: completes any custom check whose condition is now met by
// calling ClearChecker_SetNewUnlock(AP_CHECKLIST_MODE, clear_kind). Idempotent.
// Call from OnFrameStart; no-ops until the save is loaded.
void APChecklist_OnFrameStart(void);

// Per-frame blue-theme recolor for the AP tab. Must run after the menu's
// material-animation pass (which re-applies the green tint each frame), so call
// from OnFrameEnd. No-ops unless the AP tab is the current scene.
void APChecklist_OnFrameEnd(void);

#endif // ARCHIPELAGO_AP_CHECKLIST_H
