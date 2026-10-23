#ifndef CPU_TARGET_H
#define CPU_TARGET_H

#include "rider.h"

typedef enum CpuTargeting
{
    CPU_TARGET_DEFAULT,
    CPU_TARGET_HUMANS,     // the nearest human
    CPU_TARGET_LEADER,     // the rider furthest ahead
    CPU_TARGET_LEAD_HUMAN, // the human furthest ahead, for every CPU at once
    CPU_TARGET_NUM,
} CpuTargeting;

extern int cpu_targeting;
extern char *cpu_targeting_names[CPU_TARGET_NUM];

int CpuTarget_PickRival(RiderData *rd);
int CpuTarget_IsChasing(RiderData *rd);
int CpuTarget_Navigate(RiderData *rd);
void CpuTarget_Maneuver(RiderData *rd);
void CpuTarget_Hunt(int ply, int frames);
void CpuTarget_Frenzy(int frames);
int CpuTarget_GetHuntPly(void);
void CpuTarget_OnTick(void);
void CpuTarget_On3DLoadStart(void);

#endif // CPU_TARGET_H
