#ifndef ARCHIPELAGO_CHECK_DETECTION_H
#define ARCHIPELAGO_CHECK_DETECTION_H

#include "main.h"

// Map a runtime checklist mode to its row in the per-checklist-mode arrays
// (sent_checks, goal_checks, cross_mode_slots, ...), or -1 for a mode this mod does
// not record. The 3 real game modes map to themselves; the AP checklist tab maps to
// AP_CHECKLIST_ROW wherever the custom_checklist framework happened to place it. The
// single answer to "which row is this mode" - do not re-derive it per consumer.
static inline int ChecklistModeRow(int mode)
{
    if (mode >= 0 && mode < GMMODE_NUM)
        return mode;
    if (mode == ap_checklist_mode)
        return AP_CHECKLIST_ROW;
    return -1;
}

// Inverse: the runtime mode a row belongs to. Needed wherever a row has to be handed
// back to game code (gmGetClearcheckerTypeP and friends index by runtime mode).
static inline int ChecklistRowMode(int row)
{
    return row == AP_CHECKLIST_ROW ? ap_checklist_mode : row;
}

// Install hooks and patches. Call from OnBoot.
void CheckDetection_OnBoot(void);

// Restore sent_checks and goal_complete to shared mirror, run initial goal eval.
// Call from OnSaveLoaded.
void CheckDetection_OnSaveLoaded(void);

// Per-frame: process backfill from client, poll meta auto-unlocks, etc.
// Call from OnFrameStart.
void CheckDetection_OnFrameStart(void);

// Re-run goal evaluation. Idempotent and sticky - once goal_complete is set,
// further calls are no-ops. Used by external state changes (e.g. max-stats CT
// detection) that flip a goal-relevant save bit outside the sent_checks flow.
void CheckDetection_EvaluateGoal(void);

// Reset all sent_checks + goal_complete in both save and shared-memory mirror.
// Does NOT persist or re-evaluate goal - caller owns those.
void CheckDetection_ResetAll(void);

// Debug menu helpers.
void CheckDetection_DebugClearAll(void);
void CheckDetection_DebugForceMarkAll(void);
void CheckDetection_DebugTriggerGoal(void);

#endif // ARCHIPELAGO_CHECK_DETECTION_H
