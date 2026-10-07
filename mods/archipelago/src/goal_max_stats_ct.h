#ifndef ARCHIPELAGO_GOAL_MAX_STATS_CT_H
#define ARCHIPELAGO_GOAL_MAX_STATS_CT_H

// Attaches the per-rider all-stats check in a City Trial trial round while
// GOAL_MAX_STATS_CT is unmet.
void GoalMaxStatsCT_On3DLoadEnd(void);

// While the City Trial goal is GOAL_MAX_STATS_CT, adds All Up to every spawn table that
// lacks it. Runs before the gate filters.
void GoalMaxStatsCT_EnsureAllUpInPools(void);

#endif // ARCHIPELAGO_GOAL_MAX_STATS_CT_H
