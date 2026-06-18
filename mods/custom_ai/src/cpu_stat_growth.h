#ifndef CPU_STAT_GROWTH_H
#define CPU_STAT_GROWTH_H

// City Trial passive CPU stat growth control.
//
// In vanilla City Trial every CPU rider is seeded a finite, cpu_level-scaled pool
// of "free" machine stats at stage load; a per-frame drainer hands it out into
// random stats over the round, independent of any patch pickups. The seed lives
// in GameData.city.cpu_stat_budget[] (one float per slot) and is set inside the
// 3D scene loader.
//
// This module hooks the seed store and rescales each CPU's freshly seeded pool:
//   - disabled            -> pool x0   (no passive growth at all)
//   - budget = Low/Med/Hi -> pool x0.5 / x1.5 / x2.0
//   - budget = Default    -> pool x1.0 (vanilla)
// Both settings take effect on the next City Trial start (when the pool is
// re-seeded), matching the "next spawn" semantics of the CPU AI preset selector.

// Enable flag (0 = disabled, 1 = enabled). Bound to the City Trial AI menu.
extern int cpu_stat_growth_enabled;

// Budget multiplier selector. Bound to the City Trial AI menu. Order must match
// the menu's value_names in main.c.
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

// Install the seed-store hook. Call once at boot.
void CpuStatGrowth_InstallHook(void);

#endif // CPU_STAT_GROWTH_H
