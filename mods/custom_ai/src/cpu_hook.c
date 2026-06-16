#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "cpu_ai.h"

// Re-profile a freshly-initialized CT/AR CPU rider to the menu-selected preset.
//
// The City Trial / Air Ride CPU brain has no dynamic strategic FSM: ai_state
// (CpuData+0x08) is a per-stage/stadium "personality" chosen once inside
// Rider_CPUInit (by Rider_CPUSelectProfile) and then fixed for the match. So the
// cleanest way to install a custom personality is to overwrite ai_state right
// after init finishes - which is exactly what this does. Top Ride uses a separate
// C++ AI (not Rider_CPUInit), so it never reaches here.
//
// Re-profiling happens at init only: changing the menu mid-match takes effect the
// next time a CPU is spawned. "Random" is resolved per rider, so a match can hold
// a mix of personalities.
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
    if (cpu == NULL) // human rider / not allocated - nothing to re-profile
        return;

    // Only the RiderData CPU brain (City Trial, Air Ride) is driven from here.
    major = Scene_GetCurrentMajor();
    if (major == MJRKIND_CITY)
        selection = cpu_ai_preset_ct;
    else if (major == MJRKIND_AIR)
        selection = cpu_ai_preset_ar;
    else
        return;

    preset = CpuAI_Resolve(selection); // resolves the "Random" sentinel
    def = CpuAI_GetPresetDef(preset);

    // Default keeps the vanilla per-stage profile; other presets force one.
    if (def->ai_profile != CPU_PROFILE_KEEP)
        cpu->ai_state = def->ai_profile;

    // Optional difficulty override. Our preset scale is 0..4; the vanilla
    // difficulty_level (CpuData+0x22, read by Rider_CPUDifficultyScale) is 0..8.
    if (def->cpu_level >= 0)
        cpu->difficulty_level = (u8)(def->cpu_level * 2);
}

// Land on the first epilogue instruction of Rider_CPUInit (0x80262fbc,
// `lwz r0,36(r1)`): every CpuData field has been initialized by this point, and
// r31 still holds the RiderData* argument, so `mr 3, 31` hands it to the callback.
// The clobbered load is re-executed by the hook framework before returning.
CODEPATCH_HOOKCREATE(0x80262fbc,
    "mr 3, 31\n\t",
    CpuAI_ReprofileRider,
    "",
    0)

void CpuAI_InstallHook(void)
{
    CODEPATCH_HOOKAPPLY(0x80262fbc);
    OSReport("[CustomAI] CPU re-profile hook installed (Rider_CPUInit)\n");
}
