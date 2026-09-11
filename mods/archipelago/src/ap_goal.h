#ifndef ARCHIPELAGO_AP_GOAL_H
#define ARCHIPELAGO_AP_GOAL_H

// Installs the goal-aware checklist filler gate. Call from OnBoot.
void APGoal_OnBoot(void);

// Re-run goal evaluation. Idempotent and sticky - once goal_complete is set, further
// calls are no-ops. For state changes that flip a goal-relevant save bit outside the
// sent_checks flow.
void APGoal_Evaluate(void);

// Clear goal_complete, the per-row announce flags and the published satisfied mask.
void APGoal_Reset(void);

// Debug menu helper.
void APGoal_DebugComplete(void);

#endif // ARCHIPELAGO_AP_GOAL_H
