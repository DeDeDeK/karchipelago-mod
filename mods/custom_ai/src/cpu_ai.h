#ifndef CPU_AI_H
#define CPU_AI_H

// Overlays on the vanilla per-stage profile. The first CPU_AI_AR_NUM also apply
// in Air Ride; the rest need the city's item and rival targeting.
typedef enum CpuAIPreset
{
    CPU_AI_DEFAULT,
    CPU_AI_CAUTIOUS,
    CPU_AI_RECKLESS,
    CPU_AI_AR_NUM,
    CPU_AI_AGGRESSIVE = CPU_AI_AR_NUM,
    CPU_AI_HOARDER,
    CPU_AI_NUM,
} CpuAIPreset;

// Menu selections are a CpuAIPreset, or the preset count for Random.
extern int cpu_ai_menu_ct; // City Trial city
extern int cpu_ai_menu_ar;
extern char *cpu_ai_menu_ct_names[CPU_AI_NUM + 1];
extern char *cpu_ai_menu_ar_names[CPU_AI_AR_NUM + 1];

void CpuAI_InstallHooks(void);

#endif // CPU_AI_H
