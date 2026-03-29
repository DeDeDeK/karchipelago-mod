#include "game.h"
#include "inline.h"
#include "machine.h"
#include "os.h"

#include "main.h"
#include "textbox.h"
#include "airride_speed.h"

#define SPEED_BOOST_PER_ITEM 3.0f

void AirRideSpeed_Increment(void)
{
    save_data->airride_speed_boost_count++;
    OSReport("Air Ride speed boost received (total: %d).\n",
             save_data->airride_speed_boost_count);
    TextBox_Enqueue("Speed Boost received! (%d)", save_data->airride_speed_boost_count);
}

static float speed_bonus;
static float base_top_speed[5];
static int base_captured;
static int debug_timer;

static void AirRideSpeed_PerFrame(GOBJ *g)
{
    // Capture base top_speed_ground values on first frame (before we modify)
    if (!base_captured)
    {
        base_captured = 1;
        for (int i = 0; i < 5; i++)
        {
            GOBJ *mg = Ply_GetMachineGObj(i);
            if (mg)
            {
                MachineData *md = mg->userdata;
                base_top_speed[i] = md->top_speed_ground;
            }
        }
    }

    // Set absolute value each frame: base + bonus (no compounding)
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *mg = Ply_GetMachineGObj(i);
        if (!mg)
            continue;
        MachineData *md = mg->userdata;
        md->top_speed_ground = base_top_speed[i] + speed_bonus;
    }

    // Debug: print P0's top_speed_ground every 60 frames (~1 second)
    if (++debug_timer >= 60)
    {
        debug_timer = 0;
        GOBJ *mg = Ply_GetMachineGObj(0);
        if (mg)
        {
            MachineData *md = mg->userdata;
            OSReport("[AirRideSpeed] P0 top_speed_ground = %.2f (base=%.2f +%.2f)\n",
                     md->top_speed_ground, base_top_speed[0], speed_bonus);
        }
    }
}

void AirRideSpeed_On3DLoadEnd(void)
{
    if (Gm_IsInCity())
        return;
    if (save_data->airride_speed_boost_count == 0)
        return;

    speed_bonus = save_data->airride_speed_boost_count * SPEED_BOOST_PER_ITEM;
    base_captured = 0;
    debug_timer = 0;
    OSReport("Air Ride speed boost active: +%.1f (%d items)\n",
             speed_bonus, save_data->airride_speed_boost_count);
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, AirRideSpeed_PerFrame, 0, 0, 0, 0);
}
