#include "game.h"
#include "event.h"
#include "hoshi/mod.h"

#include "custom_events.h"
#include "event_cannon.h"
#include "event_mini_ufos.h"

static void On3DLoadEnd(void)
{
    // NULL when the round has City Trial events off, so no event can run.
    if (!Gm_IsInCity() || !*stc_eventcheck_gobj)
        return;
    CustomEvents_OnCityLoad();
    Cannon_OnCityLoad();
    MiniUFOs_OnCityLoad();
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
