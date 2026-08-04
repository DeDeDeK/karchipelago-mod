#ifndef CPU_STAT_GROWTH_H
#define CPU_STAT_GROWTH_H

// Rescales the finite, cpu_level-scaled pool of free machine stats City Trial
// seeds into GameData.city.cpu_stat_budget[] for each CPU at stage load, which a
// per-frame drainer then hands out into random stats over the round. Both
// settings take effect on the next City Trial start, when the pool is re-seeded.

// 0 = disabled (pool x0), 1 = enabled.
extern int cpu_stat_growth_enabled;

typedef enum CpuStatBudget
{
    CPU_STAT_BUDGET_DEFAULT = 0, // 1.0x - vanilla pool
    CPU_STAT_BUDGET_LOW,         // 0.5x
    CPU_STAT_BUDGET_MEDIUM,      // 1.5x
    CPU_STAT_BUDGET_HIGH,        // 2.0x
    CPU_STAT_BUDGET_NUM,         // count of selectable entries
} CpuStatBudget;

extern int cpu_stat_growth_amount;

// Pool multiplier for the current budget selection (1.0 for Default / out of range).
float CpuStatGrowth_Factor(void);

// Installs the seed-store hook. Call once at boot.
void CpuStatGrowth_InstallHook(void);

#endif // CPU_STAT_GROWTH_H
