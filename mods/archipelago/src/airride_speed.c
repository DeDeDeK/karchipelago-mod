#include "game.h"
#include "inline.h"
#include "machine.h"
#include "os.h"

#include "main.h"
#include "textbox.h"
#include "airride_speed.h"

#define SPEED_PATCHES_PER_ITEM 3

void AirRideSpeed_Increment(void)
{
    save_data->airride_speed_boost_count++;
    OSReport("Air Ride speed boost received (total: %d).\n",
             save_data->airride_speed_boost_count);
    TextBox_Enqueue("Speed Boost received! (%d)", save_data->airride_speed_boost_count);
}

static int applied;

static void AirRideSpeed_PerFrame(GOBJ *g)
{
    if (applied)
        return;
    if (Gm_GetIntroState() != GMINTRO_END)
        return;

    applied = 1;

    int total = save_data->airride_speed_boost_count * SPEED_PATCHES_PER_ITEM;
    OSReport("Air Ride speed boost: giving %d top speed patches (%d items).\n",
             total, save_data->airride_speed_boost_count);

    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *mg = Ply_GetMachineGObj(i);
        if (!mg)
            continue;
        MachineData *md = mg->userdata;
        Machine_GivePatch(md, PATCHKIND_TOPSPEED, total);
    }
}

void AirRideSpeed_On3DLoadEnd(void)
{
    // Only apply in Air Ride courses, not City Trial or stadiums
    if (Gm_IsInCity() || CityTrial_IsInStadium())
        return;
    if (!save_data || save_data->airride_speed_boost_count == 0)
        return;

    applied = 0;
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, AirRideSpeed_PerFrame, 0, 0, 0, 0);
}
