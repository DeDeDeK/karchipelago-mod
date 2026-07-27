#ifndef ARCHIPELAGO_GOAL_MAX_STATS_CT_H
#define ARCHIPELAGO_GOAL_MAX_STATS_CT_H

// Attach a per-rider proc that detects when a human player's 9 CT stats have all
// accumulated ap_save->options.city_trial_patch_cap_max patches (1-127, not
// PATCH_STAT_MAX) in one City Trial trial round, then sets the sticky
// ap_save->max_stats_ct_achieved and re-evaluates the goal. No-op outside
// CITYMODE_TRIAL or once the flag is set. Call from On3DLoadEnd.
void GoalMaxStatsCT_On3DLoadEnd(void);

// Apply +1 patch / All-Up drop-weight bias to all spawn pools when the active City
// Trial goal is GOAL_MAX_STATS_CT. Call after the gate spawn filters have run.
void GoalMaxStatsCT_ApplyDropBias(void);

#endif // ARCHIPELAGO_GOAL_MAX_STATS_CT_H
