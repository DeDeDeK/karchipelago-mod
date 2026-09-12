#include "hoshi/mod.h"

#include "custom_items.h"

ModDesc mod_desc = {
    .name = CUSTOM_ITEMS_MOD_NAME,
    .author = "DeDeDK",
    .version.major = CUSTOM_ITEMS_API_MAJOR,
    .version.minor = CUSTOM_ITEMS_API_MINOR,
    .affects_gameplay = 1,
    .OnBoot = CustomItems_OnBoot,
    .On3DLoadStart = CustomItems_On3DLoadStart,
};
