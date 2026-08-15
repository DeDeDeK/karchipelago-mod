#include "os.h"
#include "hoshi/mod.h"

#include "custom_machines.h"

ModDesc mod_desc = {
    .name = "custom_machines",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .affects_gameplay = 1,
    .OnBoot = CustomMachines_OnBoot,
};
