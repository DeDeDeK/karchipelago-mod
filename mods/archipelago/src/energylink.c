#include "game.h"

#include "main.h"
#include "textbox.h"

// Per-player tracking state. Indexed by player index (0-4).
// Kept separate so multiple human players in city trial each track their own
// stats independently and don't corrupt each other's delta calculations.
static int prev_obj_destroyed[5];
static float prev_stats[5][PATCHKIND_NUM];

// Shared accumulator — contributions from all players pool into one value
static float energy_give_accumulator;

// TODO: charging a machine gives energy, receiving energy charges machine
void EnergyLink_PerFrame(GOBJ *rg)
{
    RiderData *rd = rg->userdata;
    int ply = rd->ply;

    // Objects destroyed
    int diff = stc_playerdata[ply].objects_destroyed_num - prev_obj_destroyed[ply];
    prev_obj_destroyed[ply] = stc_playerdata[ply].objects_destroyed_num;
    if (diff > 0)
    {
        OSReport("generated %d energy from destroying objects\n", diff);
        energy_give_accumulator += diff;
    }

    // Patches collected
    int sum = 0;
    for (int i = 0; i < PATCHKIND_NUM; i++)
    {
        float stat_diff = rd->stats.values[i] - prev_stats[ply][i];
        if (stat_diff > 0)
            sum += stat_diff;
        prev_stats[ply][i] = rd->stats.values[i];
    }
    if (sum > 0)
    {
        OSReport("generated %d energy from collecting patches\n", sum);
        energy_give_accumulator += sum;
    }

    // Flush accumulated energy to shared field when client has cleared it
    if (energy_give_accumulator > 0 && archipelago_data->energy_give == 0)
    {
        archipelago_data->energy_give = energy_give_accumulator;
        OSReport("flushed %f energy to energy_give\n", energy_give_accumulator);
        energy_give_accumulator = 0;
    }

    // Check for received energy and apply it to vehicle charge
    float energy_received = archipelago_data->energy_receive;
    if (energy_received > 0)
    {
        GOBJ *mg = rd->machine_gobj;
        if (mg)
        {
            MachineData *md = mg->userdata;
            // TODO: clamp to 10 max value (overcharge unleashes all extra as a large boost)
            OSReport("adding energy received (%f) to machine charge...\n", energy_received);
            md->charge_value += energy_received;
        }
        // Clear so client can write the next value
        archipelago_data->energy_receive = 0;
    }
}

void EnergyLink_On3DLoadEnd()
{
    // Reset per-player tracking so deltas are clean for the new round
    for (int i = 0; i < 5; i++)
    {
        prev_obj_destroyed[i] = 0;
        for (int j = 0; j < PATCHKIND_NUM; j++)
            prev_stats[i][j] = 0.0f;
    }
    energy_give_accumulator = 0;

    // Add the energylink check to all human players, in City Trial only
    if (Gm_IsInCity())
    {
        for (int i = 0; i < 4; i++)
        {
            if (Ply_GetPKind(i) == PKIND_HMN)
            {
                GOBJ *r = Ply_GetRiderGObj(i);
                if (r)
                {
                    // RDPRI is set to after hit collision is applied
                    GObj_AddProc(r, EnergyLink_PerFrame, RDPRI_HITCOLL + 1);
                }
            }
        }
    }
}
