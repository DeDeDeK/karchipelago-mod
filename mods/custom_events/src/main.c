#include "game.h"
#include "hoshi/mod.h"

#include "custom_events.h"

static void On3DLoadEnd(void)
{
    if (Gm_IsInCity())
        CustomEvents_InitSis();
}

ModDesc mod_desc = {
    .name = CUSTOM_EVENTS_MOD_NAME,
    .author = "DeDeDK",
    .version.major = CUSTOM_EVENTS_API_MAJOR,
    .version.minor = CUSTOM_EVENTS_API_MINOR,
    .affects_gameplay = 1,
    .OnBoot = CustomEvents_OnBoot,
    .On3DLoadEnd = On3DLoadEnd,
    .On3DExit = CustomEvents_On3DExit,
};
