#ifndef CPU_STADIUM_H
#define CPU_STADIUM_H

#include "rider.h"

extern int cpu_stadium;

float CpuStadium_TargetPitch(RiderData *rd, float vanilla);
void CpuStadium_Maneuver(RiderData *rd);
void CpuStadium_AfterUpdate(RiderData *rd);
void CpuStadium_InstallHooks(void);

#endif // CPU_STADIUM_H
