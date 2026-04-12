#include "game.h"
#include "topride.h"
#include "inline.h"
#include "scene.h"

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

// Flush accumulated energy to the shared field when client has cleared it.
static void FlushEnergy(void)
{
    if (energy_send_accumulator != 0 && ap_data->energy_send == 0)
    {
        ap_data->energy_send = energy_send_accumulator;
        OSReport("flushed %f energy to energy_send\n", energy_send_accumulator);
        energy_send_accumulator = 0;
    }
}

// Per-frame proc attached to each human rider GOBJ in Air Ride / City Trial.
// Tracks objects destroyed, patches collected, and machine charge.
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

    FlushEnergy();

    // Auto-charge: spend energy from pool to fill machine charge meter
    if (ap_menu_settings.energylink_autocharge && mg)
    {
        MachineData *md = mg->userdata;
        float deficit = 1.0f - md->charge_value;
        float balance = ap_data->energy_balance;
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
            ap_data->energy_balance -= energy_needed;
            energy_send_accumulator -= energy_needed;
        }
    }
}

// Per-frame proc for Top Ride charge tracking.
// Top Ride has its own player system (Kirby/Fielder) — no RiderData or MachineData.
// Charge value is at TopRideKirby.charge.charge_value (see docs/topride-system.md).
static void EnergyLink_TopRidePerFrame(GOBJ *g)
{
    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    if (!mgr)
        return;

    for (int i = 0; i < 4; i++)
    {
        TopRideKirby *kirby = mgr->kirbys[i];
        if (!kirby || kirby->cpu_level != 0)
            continue;

        float charge = kirby->charge.charge_value;
        float charge_diff = charge - prev_charge_value[i];
        if (charge_diff > 0)
        {
            float charge_energy = charge_diff * CHARGE_ENERGY_SCALE;
            OSReport("generated %f energy from TR charging (ply %d)\n", charge_energy, i);
            energy_send_accumulator += charge_energy;
        }
        prev_charge_value[i] = charge;
    }

    FlushEnergy();
}

static void ResetTracking(void)
{
    for (int i = 0; i < 5; i++)
    {
        prev_obj_destroyed[i] = 0;
        prev_charge_value[i] = 0.0f;
        needs_baseline[i] = 1;
        for (int j = 0; j < PATCHKIND_NUM; j++)
            prev_stats[i][j] = 0.0f;
    }
    energy_send_accumulator = 0;
}

void EnergyLink_On3DLoadEnd()
{
    ResetTracking();

    // Add the energylink check to all human players in Air Ride / City Trial
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

void EnergyLink_OnTopRideLoad()
{
    ResetTracking();

    // Top Ride has no rider GOBJs, so create a standalone GOBJ for charge tracking.
    // Baseline is not needed — Top Ride has no patches or intro sequence to skip.
    for (int i = 0; i < 4; i++)
        needs_baseline[i] = 0;

    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, EnergyLink_TopRidePerFrame, 0, 0, 0, 0);
    OSReport("[EnergyLink] Top Ride charge tracking installed\n");
}

void EnergyLink_Withdraw(float amount)
{
    energy_send_accumulator -= amount;
}
