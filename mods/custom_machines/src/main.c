#include "os.h"
#include "hoshi/mod.h"

#include "custom_machines.h"

ModDesc mod_desc = {
    .name = "custom_machines",
    .author = "DeDeDK",
    .version.major = CUSTOM_MACHINES_API_MAJOR,
    .version.minor = CUSTOM_MACHINES_API_MINOR,
    .affects_gameplay = 1,
    .OnBoot = CustomMachines_OnBoot,
    .On3DLoadStart = CustomMachines_On3DLoadStart,
    .OnFrameStart = CustomMachines_OnFrameStart,
};
