#ifndef ARCHIPELAGO_AP_CHECKS_H
#define ARCHIPELAGO_AP_CHECKS_H

#include "main.h"
#include "inline.h"

// How many checkboxes of a row are recorded complete.
static inline int APChecks_PopcountRow(int row)
{
    return Popcount64(ap_save->sent_checks[row][0]) + Popcount64(ap_save->sent_checks[row][1]);
}

void APChecks_OnBoot(void);

// Mirrors sent_checks into APData and publishes the goal state.
void APChecks_OnSaveLoaded(void);

// Applies the checks the client backfilled. Call only while ap_data->backfill_valid is set.
void APChecks_ApplyBackfill(void);

// Clears sent_checks and the goal state in save and mirror; neither writes the card nor
// re-evaluates the goal.
void APChecks_ResetAll(void);

// Debug menu helpers; both also cover the AP Patches.
void APChecks_DebugClearAll(void);
void APChecks_DebugForceMarkAll(void);

#endif // ARCHIPELAGO_AP_CHECKS_H
