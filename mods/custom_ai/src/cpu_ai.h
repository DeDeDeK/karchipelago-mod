#ifndef CPU_AI_H
#define CPU_AI_H

#include "datatypes.h"

// Behavior presets for CPU-controlled riders (the AI opponents). The same preset
// set is offered independently per mode - City Trial, Air Ride, and Top Ride each
// keep their own selection. City Trial and Air Ride CPUs share one underlying AI
// (the virtual-pad system on RiderData); Top Ride's CPUs are a separate system,
// so its selection will drive a separate hook. The concrete presets are
// 0..CPU_AI_PRESET_NUM-1; CPU_AI_RANDOM is a menu-only sentinel that resolves to
// one of them.
typedef enum CpuAIPreset
{
    CPU_AI_DEFAULT = 0,     // Vanilla CPU behavior
    CPU_AI_AGGRESSIVE,      // Rams and contests riders, fights over items
    CPU_AI_HOARDER,         // Beelines for patches and item boxes
    CPU_AI_CAUTIOUS,        // Plays safe - avoids combat, conserves boost
    CPU_AI_RECKLESS,        // Boosts/charges constantly, high risk
    CPU_AI_PRESET_NUM,      // Count of concrete presets

    CPU_AI_RANDOM = CPU_AI_PRESET_NUM, // Menu-only: roll a concrete preset
    CPU_AI_MENU_NUM,        // Total selectable menu entries (presets + Random)
} CpuAIPreset;

// Vanilla CPU AI profiles == ai_state values (CpuData+0x08). The City Trial / Air
// Ride CPU brain has no dynamic strategic FSM: ai_state is a per-stage/stadium
// "personality" chosen once at Rider_CPUInit and then fixed. We re-profile a CPU
// by overwriting ai_state at init (CpuAI_ReprofileRider). These are the handful of
// vanilla profiles our presets map onto.
enum
{
    CPU_PROFILE_KEEP   = 0,   // leave the vanilla per-stage profile untouched
    CPU_PROFILE_CRUISE = 1,   // follow the racing line, leanest flags (no combat)
    CPU_PROFILE_ROUTE  = 5,   // beeline a route to items/goals
    CPU_PROFILE_CHARGE = 7,   // drive to an anchor and commit charge/boost
    CPU_PROFILE_ATTACK = 8,   // acquire the nearest rival and ram/attack
};

// Per-preset tuning knobs for CPU riders. The behavioral weights (0.0-1.0) are
// scaffold knobs and are NOT applied yet. cpu_level maps to the vanilla per-player
// difficulty (0..4, -1 = inherit). ai_profile is the vanilla ai_state the preset
// forces at init (CPU_PROFILE_KEEP = don't override).
typedef struct CpuAIPresetDef
{
    const char *name;           // Menu label
    const char *description;     // One-line behavior summary
    float aggression;            // 0=passive .. 1=hyper-aggressive toward riders
    float item_focus;            // 0=ignores items .. 1=beelines for patches/boxes
    float boost_usage;           // 0=conservative .. 1=boosts/charges at every chance
    int   cpu_level;             // Difficulty 0..4, or -1 to inherit the match setting
    int   ai_profile;            // Vanilla ai_state to force at init (CPU_PROFILE_*)
} CpuAIPresetDef;

// Per-mode menu selections (each may be CPU_AI_RANDOM). Bound to the settings
// menu in main.c. Independent so e.g. City Trial can run Aggressive CPUs while
// Top Ride stays on Default.
extern int cpu_ai_preset_ct;   // City Trial CPU riders
extern int cpu_ai_preset_ar;   // Air Ride CPU riders
extern int cpu_ai_preset_tr;   // Top Ride CPU riders

const CpuAIPresetDef *CpuAI_GetPresetDef(int preset);
const char *CpuAI_GetPresetName(int preset);
// Name for any menu selection, including the "Random" sentinel.
const char *CpuAI_GetSelectionName(int selection);
// Resolve a menu selection to a concrete preset index, rolling for "Random".
int CpuAI_Resolve(int selection);

// Install the Rider_CPUInit hook that re-profiles CT/AR CPU riders to the
// selected preset. Call once at boot. No-op effect for Top Ride (separate AI).
void CpuAI_InstallHook(void);

#endif // CPU_AI_H
