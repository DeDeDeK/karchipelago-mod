#ifndef ARCHIPELAGO_CHECK_DETECTION_H
#define ARCHIPELAGO_CHECK_DETECTION_H

#include "structs.h"
#include "game.h"

// Bit accessors for sent_checks[mode][2] u64 packing.
// Mode m, clear_kind k in [0..CLEAR_KIND_NUM-1]:
//   word index = k / 64
//   bit index  = k % 64
#define SENT_CHECK_BIT(m, k)  ((ap_save->sent_checks[(m)][(k) >> 6] >> ((k) & 63)) & 1ULL)
#define SET_SENT_CHECK(m, k)  (ap_save->sent_checks[(m)][(k) >> 6] |= (1ULL << ((k) & 63)))

// Beat King Dedede goal: CT clear_kind 0x2F
#define KD_CLEAR_KIND 0x2F

// Install hooks and patches. Call from OnBoot.
void CheckDetection_OnBoot(void);

// Restore sent_checks and goal_complete to shared mirror, run initial goal eval.
// Call from OnSaveLoaded.
void CheckDetection_OnSaveLoaded(void);

// Per-frame: process backfill from client, poll meta auto-unlocks, etc.
// Call from OnFrameStart.
void CheckDetection_OnFrameStart(void);

// Re-evaluate the goal condition and set goal_complete if satisfied.
// Idempotent: sticky once set. Saves on first transition.
void CheckDetection_EvaluateGoal(void);

// Debug menu helpers.
void CheckDetection_DebugClearAll(void);
void CheckDetection_DebugForceMarkAll(void);
void CheckDetection_DebugTriggerGoal(void);

#endif // ARCHIPELAGO_CHECK_DETECTION_H
