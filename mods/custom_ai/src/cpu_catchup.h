#ifndef CPU_CATCHUP_H
#define CPU_CATCHUP_H

#include "rider.h"

typedef enum CpuCatchup
{
    CPU_CATCHUP_OFF,
    CPU_CATCHUP_VANILLA,
    CPU_CATCHUP_STRONG,
    CPU_CATCHUP_SKILL, // no CPU-only speed bonuses; CPUs play sharper behind the leading human
    CPU_CATCHUP_NUM,
} CpuCatchup;

extern int cpu_catchup;
extern int cpu_adaptive;
extern char *cpu_catchup_names[CPU_CATCHUP_NUM];

void CpuCatchup_AdjustLevel(RiderData *rd);
float CpuCatchup_SkillBonus(RiderData *rd);
int CpuCatchup_GetLevelOffset(void);
void CpuCatchup_SetBaseOffset(int offset);
void CpuCatchup_On3DLoadEnd(void);
void CpuCatchup_On3DExit(void);

#endif // CPU_CATCHUP_H
