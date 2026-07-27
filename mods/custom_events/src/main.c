#include "game.h"
#include "os.h"
#include "hsd.h"
#include "hoshi/mod.h"

#include "custom_events.h"
#include "cannon_event.h"

static void OnBoot(void)
{
    CustomEvents_OnBoot();
}

static void On3DLoadEnd(void)
{
    if (stGetCurrentStageKind() == STAGEKIND_CITY1)
        CustomEvents_InitSis();

    // Cannon scaffolding runs diagnostic spawns and memory dumps on every City
    // Trial load, so it stays off.
    // CannonEvent_On3DLoadEnd();
}

ModDesc mod_desc = {
    .name = "custom_events",
    .author = "DeDeDK",
    .version.major = CUSTOM_EVENTS_API_MAJOR,
    .version.minor = CUSTOM_EVENTS_API_MINOR,
    .OnBoot = OnBoot,
    .On3DLoadEnd = On3DLoadEnd,
};
