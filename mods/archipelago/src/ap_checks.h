#ifndef ARCHIPELAGO_AP_CHECKS_H
#define ARCHIPELAGO_AP_CHECKS_H

#include "main.h"
#include "inline.h"

// How many checkboxes of a row are recorded complete.
static inline int APChecks_PopcountRow(int row)
{
    return Popcount64(ap_save->sent_checks[row][0]) + Popcount64(ap_save->sent_checks[row][1]);
}

// Install the ClearChecker seam and the meta auto-unlock hooks. Call from OnBoot.
void APChecks_OnBoot(void);

// Mirror sent_checks and goal_complete into shared memory, run initial goal eval.
// Call from OnSaveLoaded.
void APChecks_OnSaveLoaded(void);

// Apply the checks the client backfilled. Call only while ap_data->backfill_valid is set.
void APChecks_ApplyBackfill(void);

// Reset all sent_checks and goal state in both save and shared-memory mirror.
// Does NOT persist or re-evaluate the goal - the caller owns those.
void APChecks_ResetAll(void);

// Debug menu helpers.
void APChecks_DebugClearAll(void);
void APChecks_DebugForceMarkAll(void);

#endif // ARCHIPELAGO_AP_CHECKS_H
