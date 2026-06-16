#include "custom_ai.h"
#include "cpu_ai.h"

// Per-mode menu selections, bound to the settings menu (see main.c). Each may
// hold CPU_AI_RANDOM; resolve to a concrete preset with CpuAI_Resolve.
int cpu_ai_preset_ct = CPU_AI_DEFAULT;
int cpu_ai_preset_ar = CPU_AI_DEFAULT;
int cpu_ai_preset_tr = CPU_AI_DEFAULT;

// Preset tuning table. Behavioral weights are 0.0-1.0; cpu_level is the vanilla
// 0..4 difficulty (-1 = inherit the match setting). First-pass numbers - expect
// to retune once the CPU-rider hook that applies them exists.
static const CpuAIPresetDef cpu_presets[CPU_AI_PRESET_NUM] = {
    [CPU_AI_DEFAULT] = {
        .name = "Default",
        .description = "Vanilla CPU behavior - no changes",
        .aggression  = 0.5f,
        .item_focus  = 0.5f,
        .boost_usage = 0.5f,
        .cpu_level   = -1,
        .ai_profile  = CPU_PROFILE_KEEP,
    },
    [CPU_AI_AGGRESSIVE] = {
        .name = "Aggressive",
        .description = "Rams and contests riders, fights over items",
        .aggression  = 1.0f,
        .item_focus  = 0.4f,
        .boost_usage = 0.8f,
        .cpu_level   = 4,
        .ai_profile  = CPU_PROFILE_ATTACK,
    },
    [CPU_AI_HOARDER] = {
        .name = "Hoarder",
        .description = "Beelines for patches and item boxes, avoids fights",
        .aggression  = 0.2f,
        .item_focus  = 1.0f,
        .boost_usage = 0.5f,
        .cpu_level   = 3,
        .ai_profile  = CPU_PROFILE_ROUTE,
    },
    [CPU_AI_CAUTIOUS] = {
        .name = "Cautious",
        .description = "Plays safe - avoids combat, conserves boost",
        .aggression  = 0.1f,
        .item_focus  = 0.5f,
        .boost_usage = 0.25f,
        .cpu_level   = 2,
        .ai_profile  = CPU_PROFILE_CRUISE,
    },
    [CPU_AI_RECKLESS] = {
        .name = "Reckless",
        .description = "Boosts and charges constantly, high risk",
        .aggression  = 0.7f,
        .item_focus  = 0.3f,
        .boost_usage = 1.0f,
        .cpu_level   = 4,
        .ai_profile  = CPU_PROFILE_CHARGE,
    },
};

const CpuAIPresetDef *CpuAI_GetPresetDef(int preset)
{
    if (preset < 0 || preset >= CPU_AI_PRESET_NUM)
        preset = CPU_AI_DEFAULT;
    return &cpu_presets[preset];
}

const char *CpuAI_GetPresetName(int preset)
{
    return CpuAI_GetPresetDef(preset)->name;
}

const char *CpuAI_GetSelectionName(int selection)
{
    if (selection == CPU_AI_RANDOM)
        return "Random";
    return CpuAI_GetPresetName(selection);
}

int CpuAI_Resolve(int selection)
{
    // Roll among the non-default presets so "Random" always changes behavior.
    if (selection == CPU_AI_RANDOM)
        return CPU_AI_DEFAULT + 1 + CustomAI_RollRandom(CPU_AI_PRESET_NUM - 1);
    if (selection < 0 || selection >= CPU_AI_PRESET_NUM)
        return CPU_AI_DEFAULT;
    return selection;
}
