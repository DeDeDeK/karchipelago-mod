#include "game.h"
#include "os.h"
#include "hoshi/mod.h"

#include "custom_events.h"

static void OnBoot(void)
{
    CustomEvents_OnBoot();
}

static void On3DLoadEnd(void)
{
    // Initialize custom event SIS text entries when in City Trial
    if (Gm_GetCurrentGrKind() == GRKIND_CITY1)
        CustomEvents_InitSis();
}

ModDesc mod_desc = {
    .name = "custom_events",
    .author = "DeDeDK",
    .version.major = CUSTOM_EVENTS_API_MAJOR,
    .version.minor = CUSTOM_EVENTS_API_MINOR,
    .OnBoot = OnBoot,
    .On3DLoadEnd = On3DLoadEnd,
};
