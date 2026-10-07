#ifndef ARCHIPELAGO_AP_GOAL_H
#define ARCHIPELAGO_AP_GOAL_H

void APGoal_OnBoot(void);

// Latches every row goal now met, announces it, and republishes goal_satisfied_mask and
// goal_complete. A no-op until the slot options arrive.
void APGoal_Evaluate(void);

// Clears the goal latches, max_stats_ct_achieved and the published goal state.
void APGoal_Reset(void);

// A row's goal; *out_amount (nullable) gets the count goal's square count.
int APGoal_Get(int row, int *out_amount);

// Debug menu helpers.
void APGoal_DebugSetGoals(const int *goals, int amount);
void APGoal_DebugComplete(void);

#endif // ARCHIPELAGO_AP_GOAL_H
