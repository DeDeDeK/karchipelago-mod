#include "game.h"

#include "main.h"
#include "textbox.h"

// Per-player tracking state. Indexed by player index (0-4).
// Kept separate so multiple human players in city trial each track their own
// stats independently and don't corrupt each other's delta calculations.
static int prev_obj_destroyed[5];
static float prev_stats[5][PATCHKIND_NUM];
static float prev_charge_value[5];

// Shared accumulator — contributions from all players pool into one value
static float energy_give_accumulator;

// Scale factor for charge energy: a full 0→1 charge is worth this many energy units
#define CHARGE_ENERGY_SCALE 5.0f

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

    // Charge gained from holding A — snapshot BEFORE applying received energy
    GOBJ *mg = rd->machine_gobj;
    if (mg)
    {
        MachineData *md = mg->userdata;
        float charge_diff = md->charge_value - prev_charge_value[ply];
        if (charge_diff > 0)
        {
            float charge_energy = charge_diff * CHARGE_ENERGY_SCALE;
            OSReport("generated %f energy from charging\n", charge_energy);
            energy_give_accumulator += charge_energy;
        }
        prev_charge_value[ply] = md->charge_value;
    }

    // Flush accumulated energy to shared field when client has cleared it
    if (energy_give_accumulator > 0 && archipelago_data->energy_give == 0)
    {
        archipelago_data->energy_give = energy_give_accumulator;
        OSReport("flushed %f energy to energy_give\n", energy_give_accumulator);
        energy_give_accumulator = 0;
    }

    // Apply received energy to vehicle charge
    float energy_received = archipelago_data->energy_receive;
    if (energy_received > 0 && mg)
    {
        MachineData *md = mg->userdata;
        OSReport("adding energy received (%f) to machine charge...\n", energy_received);
        md->charge_value += energy_received;
        if (md->charge_value > 1.0f)
            md->charge_value = 1.0f;

        // Update prev so the injected charge is not re-counted as send energy next frame
        prev_charge_value[ply] = md->charge_value;

        archipelago_data->energy_receive = 0;
    }
    else if (energy_received > 0)
    {
        // No machine available, clear so client isn't stuck
        archipelago_data->energy_receive = 0;
    }
}

void EnergyLink_On3DLoadEnd()
{
    // Reset per-player tracking so deltas are clean for the new round
    for (int i = 0; i < 5; i++)
    {
        prev_obj_destroyed[i] = 0;
        prev_charge_value[i] = 0.0f;
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
