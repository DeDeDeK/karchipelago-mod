#include "hoshi/mod.h"

#include "custom_items.h"

static void OnBoot(void)
{
    CustomItems_OnBoot();
}

ModDesc mod_desc = {
    .name = "custom_items",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .OnBoot = OnBoot,
};
