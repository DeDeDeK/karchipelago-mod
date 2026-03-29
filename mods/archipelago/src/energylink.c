#include "game.h"

#include "main.h"
#include "textbox.h"

// Per-player tracking state. Indexed by player index (0-4).
// Kept separate so multiple human players in city trial each track their own
// stats independently and don't corrupt each other's delta calculations.
static int prev_obj_destroyed[5];
static float prev_stats[5][PATCHKIND_NUM];
static float prev_charge_value[5];

// Per-player flag: when set, the next EnergyLink_PerFrame call snapshots
// current stats as the baseline without counting any delta as energy.
// This prevents permanent patches applied at round start from being
// counted as generated energy.
static int needs_baseline[5];

// Shared accumulator — contributions from all players pool into one value
static float energy_send_accumulator;

// Scale factor for charge energy: a full 0→1 charge is worth this many energy units
#define CHARGE_ENERGY_SCALE 5.0f

void EnergyLink_PerFrame(GOBJ *rg)
{
    RiderData *rd = rg->userdata;
    int ply = rd->ply;

    // On the first frame after intro, snapshot current stats as the baseline
    // so that permanent patches applied at round start aren't counted as energy.
    if (needs_baseline[ply])
    {
        if (Gm_GetIntroState() != GMINTRO_END)
            return;
        needs_baseline[ply] = 0;
        prev_obj_destroyed[ply] = stc_playerdata[ply].objects_destroyed_num;
        prev_charge_value[ply] = 0.0f;
        for (int i = 0; i < PATCHKIND_NUM; i++)
            prev_stats[ply][i] = rd->stats.values[i];
        GOBJ *mg = rd->machine_gobj;
        if (mg)
        {
            MachineData *md = mg->userdata;
            prev_charge_value[ply] = md->charge_value;
        }
        return;
    }

    // Objects destroyed
    int diff = stc_playerdata[ply].objects_destroyed_num - prev_obj_destroyed[ply];
    prev_obj_destroyed[ply] = stc_playerdata[ply].objects_destroyed_num;
    if (diff > 0)
    {
        OSReport("generated %d energy from destroying objects\n", diff);
        energy_send_accumulator += diff;
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
        energy_send_accumulator += sum;
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
            energy_send_accumulator += charge_energy;
        }
        prev_charge_value[ply] = md->charge_value;
    }

    // Flush accumulated energy to shared field when client has cleared it.
    // Accumulator can be positive (deposit) or negative (net withdrawal).
    if (energy_send_accumulator != 0 && archipelago_data->energy_send == 0)
    {
        archipelago_data->energy_send = energy_send_accumulator;
        OSReport("flushed %f energy to energy_send\n", energy_send_accumulator);
        energy_send_accumulator = 0;
    }

    // Auto-charge: spend energy from pool to fill machine charge meter
    if (hoshi_menu_settings.energylink_autocharge && mg)
    {
        MachineData *md = mg->userdata;
        float deficit = 1.0f - md->charge_value;
        float balance = archipelago_data->energy_balance;
        if (deficit > 0 && balance > 0)
        {
            // Convert charge units to energy cost
            float energy_needed = deficit * CHARGE_ENERGY_SCALE;
            if (energy_needed > balance)
                energy_needed = balance;

            float charge_gain = energy_needed / CHARGE_ENERGY_SCALE;
            md->charge_value += charge_gain;
            if (md->charge_value > 1.0f)
                md->charge_value = 1.0f;

            // Update prev so the injected charge is not re-counted as generated energy
            prev_charge_value[ply] = md->charge_value;

            // Deduct from balance and queue withdrawal through accumulator
            archipelago_data->energy_balance -= energy_needed;
            energy_send_accumulator -= energy_needed;
        }
    }
}

void EnergyLink_On3DLoadEnd()
{
    // Reset per-player tracking so deltas are clean for the new round.
    // Set needs_baseline so EnergyLink waits until intro ends and snapshots
    // current stats (after permanent patches are applied) before tracking.
    for (int i = 0; i < 5; i++)
    {
        prev_obj_destroyed[i] = 0;
        prev_charge_value[i] = 0.0f;
        needs_baseline[i] = 1;
        for (int j = 0; j < PATCHKIND_NUM; j++)
            prev_stats[i][j] = 0.0f;
    }
    energy_send_accumulator = 0;

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

void EnergyLink_Withdraw(float amount)
{
    energy_send_accumulator -= amount;
}
