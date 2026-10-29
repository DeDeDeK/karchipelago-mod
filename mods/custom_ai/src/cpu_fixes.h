#ifndef CPU_FIXES_H
#define CPU_FIXES_H

#include "rider.h"

extern int cpu_fixes;

void CpuFixes_Apply(void);
void CpuFixes_ResetRider(int ply);
void CpuFixes_MeleeChase(RiderData *rd);
void CpuFixes_InstallHooks(void);

#endif // CPU_FIXES_H
