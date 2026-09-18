#ifndef CPU_STAT_GROWTH_H
#define CPU_STAT_GROWTH_H

// Scale on the free machine stat pool City Trial seeds for each CPU at load.
typedef enum CpuStatPool
{
    CPU_STAT_POOL_OFF,
    CPU_STAT_POOL_LOW,
    CPU_STAT_POOL_DEFAULT,
    CPU_STAT_POOL_HIGH,
    CPU_STAT_POOL_MAX,
    CPU_STAT_POOL_NUM,
} CpuStatPool;

extern int cpu_stat_pool;
extern char *cpu_stat_pool_names[CPU_STAT_POOL_NUM];

void CpuStatGrowth_InstallHook(void);

#endif // CPU_STAT_GROWTH_H
