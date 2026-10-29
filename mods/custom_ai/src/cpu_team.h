#ifndef CPU_TEAM_H
#define CPU_TEAM_H

#include "rider.h"

// CPUs leave an item or machine to whichever CPU is closer.
extern int cpu_teamwork;

void CpuTeam_OnTick(void);
int CpuTeam_IsClaimed(int ply, void *key, float dist);
int CpuTeam_ItemScore(RiderData *rd, ItemData *it, int score);

#endif // CPU_TEAM_H
