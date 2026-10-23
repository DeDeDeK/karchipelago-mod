#ifndef CPU_AI_H
#define CPU_AI_H

#include "game.h"
#include "rider.h"

// A radius past every stage, Rider_CPUDecideNavigate's own whole-map item scan.
#define CPU_AI_WHOLE_MAP 100000.0f

// Overlays on the vanilla per-stage profile. Each menu offers its own subset.
typedef enum CpuAIPreset
{
    CPU_AI_DEFAULT,
    CPU_AI_CAUTIOUS,
    CPU_AI_RECKLESS,
    CPU_AI_AGGRESSIVE,
    CPU_AI_HOARDER,
    CPU_AI_STRATEGIST,
    CPU_AI_RACER,
    CPU_AI_NUM,
} CpuAIPreset;

// Menu selections index the menu's preset list; the list length means Random.
#define CPU_AI_CT_NUM 6
#define CPU_AI_AR_NUM 4

extern int cpu_ai_menu_ct;
extern int cpu_ai_menu_ar;
extern char *cpu_ai_menu_ct_names[CPU_AI_CT_NUM + 1];
extern char *cpu_ai_menu_ar_names[CPU_AI_AR_NUM + 1];

CpuAIPreset CpuAI_GetPreset(int ply);
const char *CpuAI_GetPresetName(int ply);
RiderData *CpuAI_GetCpuRider(int ply);
float CpuAI_Skill(RiderData *rd, float min, float skill);
float CpuAI_StatTotal(int ply);
int CpuAI_Leader(int exclude, int humans_only);
float CpuAI_Elevation(float y);
void CpuAI_AwayFrom(RiderData *rd, const Vec3 *from, float dist, Vec3 *out);
int CpuAI_IsCityNavigate(RiderData *rd);
int CpuAI_IsReachable(RiderData *rd, void *key, Vec3 *pos);
int CpuAI_IsUrgent(int maneuver);
int CpuAI_CanWiggle(CpuData *cpu);
void CpuAI_CommitZone(CpuData *cpu, CpuForwardTarget *t);
int CpuAI_FindRival(RiderData *rd);
void CpuAI_ChaseRival(RiderData *rd, int rival, float range);
int CpuAI_SpinAt(RiderData *rd, int ply, float chance);

void CpuAI_ImportApis(void);
void CpuAI_On3DLoadStart(void);
void CpuAI_InstallHooks(void);

#endif // CPU_AI_H
