// Files this mod adds to City Trial's preload set, so an archive a cinematic loads
// synchronously mid-round is already resident and never hits the disc.

#include <string.h>

#include "os.h"
#include "preload.h"
#include "game.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

// Two cinematic archives per machine, plus room for a consumer's own.
#define PRELOAD_MAX (CUSTOM_MACHINE_MAX * 2 + 8)

// Scene 9 is City Trial, the only scene the vanilla call queues for.
#define SCENE_CITY_TRIAL 9

static const char *stc_path[PRELOAD_MAX];
static int stc_count;

int CustomMachinePreload_Add(const char *path)
{
    if (path == NULL || stc_count >= PRELOAD_MAX)
        return 0;

    for (int i = 0; i < stc_count; i++)
    {
        if (strcmp(stc_path[i], path) == 0)
            return 1;
    }

    stc_path[stc_count++] = path;
    return 1;
}

// Replaces the bl LegendaryMachine_PreloadAssemblyArchives at 0x80262be8, the tail
// of Preload_AllCityFiles (0x80262ba4). The arguments are the ones the vanilla
// legendary archives are queued with.
static void PreloadCityFiles(int scene)
{
    LegendaryMachine_PreloadAssemblyArchives(scene);
    if (scene != SCENE_CITY_TRIAL)
        return;

    for (int i = 0; i < stc_count; i++)
        Preload_CreateEntry(5, (char *)stc_path[i], 6, 6, 0, 1, 5, 0x20, 0);
}

void CustomMachinePreload_OnBoot(void)
{
    CODEPATCH_REPLACECALL(0x80262be8, PreloadCityFiles);
    OSReport("[MachinePreload] %d file(s) queued with City Trial\n", stc_count);
}
