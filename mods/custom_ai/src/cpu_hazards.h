#ifndef CPU_HAZARDS_H
#define CPU_HAZARDS_H

#include "rider.h"

typedef enum CpuThreatKind
{
    CPU_THREAT_NONE,
    CPU_THREAT_RIDER,
    CPU_THREAT_MACHINE,
    CPU_THREAT_ENEMY,
    CPU_THREAT_WEAPON,
    CPU_THREAT_STATIC,
} CpuThreatKind;

// The soonest imminent hazard on a CPU's list this frame.
typedef struct CpuThreat
{
    u8 kind;       // CpuThreatKind
    u8 ply;        // the rider behind it, PLY_NUM for none
    float time;    // frames to impact
    u32 frame;     // HSD_Update.engine_frames when collected
} CpuThreat;

// A static hazard every CPU steers around, in world units.
typedef struct CpuSphere
{
    Vec3 pos;
    float radius;
} CpuSphere;

const CpuThreat *CpuHazards_GetThreat(int ply);
int CpuHazards_GetSpheres(const CpuSphere **out);
void CpuHazards_InstallHooks(void);

#endif // CPU_HAZARDS_H
