#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "cpu_ai.h"

// Re-profile a freshly-initialized CT/AR CPU rider to the menu-selected preset.
// ai_state is fixed for the match once Rider_CPUInit picks it, so overwriting it
// right after init is what installs a custom personality. Top Ride uses a
// separate C++ AI and never reaches here.
//
// Runs at init only, so a menu change takes effect on the next CPU spawn, and
// "Random" is resolved per rider (a match can hold a mix of personalities).
void CpuAI_ReprofileRider(RiderData *rider)
{
    CpuData *cpu;
    int selection;
    int preset;
    const CpuAIPresetDef *def;
    MajorKind major;

    if (rider == NULL)
        return;
    cpu = rider->cpu;
    if (cpu == NULL) // human rider
        return;

    major = Scene_GetCurrentMajor();
    if (major == MJRKIND_CITY)
        selection = cpu_ai_preset_ct;
    else if (major == MJRKIND_AIR)
        selection = cpu_ai_preset_ar;
    else
        return;

    preset = CpuAI_Resolve(selection);
    def = CpuAI_GetPresetDef(preset);

    if (def->ai_profile != CPU_PROFILE_KEEP)
        cpu->ai_state = def->ai_profile;

    // The preset scale is 0..4; vanilla difficulty_level (CpuData+0x22, read by
    // Rider_CPUDifficultyScale) is 0..8.
    if (def->cpu_level >= 0)
        cpu->difficulty_level = (u8)(def->cpu_level * 2);
}

// First epilogue instruction of Rider_CPUInit (`lwz r0,36(r1)`), by which point
// every CpuData field is initialized and r31 still holds the RiderData*.
CODEPATCH_HOOKCREATE(0x80262fbc,
    "mr 3, 31\n\t",
    CpuAI_ReprofileRider,
    "",
    0)

void CpuAI_InstallHook(void)
{
    CODEPATCH_HOOKAPPLY(0x80262fbc);
}
