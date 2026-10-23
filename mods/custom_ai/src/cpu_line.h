#ifndef CPU_LINE_H
#define CPU_LINE_H

#include "rider.h"

typedef enum CpuRacingLine
{
    CPU_LINE_OFF,
    CPU_LINE_RACERS, // Racer presets only
    CPU_LINE_ALL,    // every race CPU, followed more closely with skill
    CPU_LINE_NUM,
} CpuRacingLine;

extern int cpu_racing_line;
extern char *cpu_racing_line_names[CPU_LINE_NUM];

int CpuLine_IsReady(void);
void CpuLine_Brake(RiderData *rd);
int CpuLine_GetPoints(int ply, Vec3 *out, int max);
void CpuLine_On3DLoadStart(void);
void CpuLine_On3DLoadEnd(void);
void CpuLine_InstallHooks(void);

#endif // CPU_LINE_H
