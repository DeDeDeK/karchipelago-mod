#ifndef CPU_AI_H
#define CPU_AI_H

#include "datatypes.h"

// Behavior presets for CPU-controlled riders, offered independently per mode.
// City Trial and Air Ride CPUs share one underlying AI (the virtual-pad system
// on RiderData); Top Ride's CPUs are a separate system needing a separate hook.
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

// Vanilla ai_state values (CpuData+0x08). There is no dynamic strategic FSM:
// ai_state is a per-stage/stadium personality chosen once at Rider_CPUInit and
// then fixed for the match. These are the profiles the presets map onto.
enum
{
    CPU_PROFILE_KEEP   = 0,   // leave the vanilla per-stage profile untouched
    CPU_PROFILE_CRUISE = 1,   // follow the racing line, leanest flags (no combat)
    CPU_PROFILE_ROUTE  = 5,   // beeline a route to items/goals
    CPU_PROFILE_CHARGE = 7,   // drive to an anchor and commit charge/boost
    CPU_PROFILE_ATTACK = 8,   // acquire the nearest rival and ram/attack
};

// The behavioral weights are scaffold knobs and are not applied yet.
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

// Independent per-mode selections, each of which may be CPU_AI_RANDOM. Top Ride
// has no entry: its CPUs run a separate AI that Rider_CPUInit never reaches.
extern int cpu_ai_preset_ct;   // City Trial CPU riders
extern int cpu_ai_preset_ar;   // Air Ride CPU riders

const CpuAIPresetDef *CpuAI_GetPresetDef(int preset);
const char *CpuAI_GetPresetName(int preset);
// Name for any menu selection, including the "Random" sentinel.
const char *CpuAI_GetSelectionName(int selection);
// Resolve a menu selection to a concrete preset index, rolling for "Random".
int CpuAI_Resolve(int selection);

// Installs the Rider_CPUInit hook that re-profiles CT/AR CPU riders to the
// selected preset. Call once at boot; Top Ride never reaches it.
void CpuAI_InstallHook(void);

#endif // CPU_AI_H
