#ifndef ARCHIPELAGO_CHECK_DETECTION_H
#define ARCHIPELAGO_CHECK_DETECTION_H

// Install hooks and patches. Call from OnBoot.
void CheckDetection_OnBoot(void);

// Restore sent_checks and goal_complete to shared mirror, run initial goal eval.
// Call from OnSaveLoaded.
void CheckDetection_OnSaveLoaded(void);

// Per-frame: process backfill from client, poll meta auto-unlocks, etc.
// Call from OnFrameStart.
void CheckDetection_OnFrameStart(void);

// Re-run goal evaluation. Idempotent and sticky — once goal_complete is set,
// further calls are no-ops. Used by external state changes (e.g. max-stats CT
// detection) that flip a goal-relevant save bit outside the sent_checks flow.
void CheckDetection_EvaluateGoal(void);

// Reset all sent_checks + goal_complete in both save and shared-memory mirror.
// Does NOT persist or re-evaluate goal — caller owns those.
void CheckDetection_ResetAll(void);

// Debug menu helpers.
void CheckDetection_DebugClearAll(void);
void CheckDetection_DebugForceMarkAll(void);
void CheckDetection_DebugTriggerGoal(void);

#endif // ARCHIPELAGO_CHECK_DETECTION_H
