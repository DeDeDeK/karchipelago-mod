#ifndef ARCHIPELAGO_GOAL_MAX_STATS_CT_H
#define ARCHIPELAGO_GOAL_MAX_STATS_CT_H

// Attaches a per-rider per-frame proc that detects when any human player's
// 9 CT stats all reach PATCH_STAT_MAX in a single City Trial trial round.
// On detection, sets ap_save->max_stats_ct_achieved (sticky) and re-evaluates
// the goal. No-op outside CITYMODE_TRIAL or once the flag is already set.
// Call from On3DLoadEnd.
void GoalMaxStatsCT_On3DLoadEnd(void);

// Apply +1 patch / All-Up drop-weight bias to all spawn pools when the active
// City Trial goal is GOAL_MAX_STATS_CT. Invoked from item_spawn_filter.c after
// the gate filters have run.
void GoalMaxStatsCT_ApplyDropBias(void);

#endif // ARCHIPELAGO_GOAL_MAX_STATS_CT_H
