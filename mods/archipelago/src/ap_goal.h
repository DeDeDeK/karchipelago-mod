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

// The goal set for a checklist-mode row, and the square count its count form needs.
// out_amount may be null.
int APGoal_Get(int row, int *out_amount);

// Debug menu helpers. DebugSetGoals overrides all CHECKLIST_MODE_NUM slot options at
// once and re-evaluates; `amount` is the square count the count goal needs and reaches
// only the rows set to it.
void APGoal_DebugSetGoals(const int *goals, int amount);
void APGoal_DebugComplete(void);

#endif // ARCHIPELAGO_AP_GOAL_H
