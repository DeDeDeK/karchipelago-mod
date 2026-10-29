#ifndef ARCHIPELAGO_AP_OPTIONS_H
#define ARCHIPELAGO_AP_OPTIONS_H

#include "archipelago_api.h"

// Copies the slot options in on the client's first write and acknowledges every write.
void APOptions_OnFrameStart(void);

// Opens the checklists the slot options pre-reveal. The AP tab's cells live in RAM, so
// this has to run every boot.
void APOptions_ApplyRevealChecklists(void);

// A category's gating flag, 0 or 1. AP_UNLOCK_AP_STAR_PIECE follows item_gating_enabled.
int APOptions_GetGating(APUnlockCategory cat);
void APOptions_GetPatchCapRange(int *out_min, int *out_max);
int APOptions_GetSpawnRateMin(void);

// Debug overrides of the client-owned slot options. A gating change shows only through
// APOptions_DebugReapply; the cap range and spawn floor are read live.
void APOptions_DebugSetGating(APUnlockCategory cat, int enabled);
void APOptions_DebugSetPatchCapMin(int min);
void APOptions_DebugSetPatchCapMax(int max);
void APOptions_DebugSetSpawnRateMin(int percent);
void APOptions_DebugReapply(void);

// Logs the save and wire state, and rolls received-item progression back to a fresh
// save's.
void APOptions_DebugReportState(void);
void APOptions_DebugResetProgression(void);

#endif // ARCHIPELAGO_AP_OPTIONS_H
