#include "game.h"
#include "topride.h"
#include "inline.h"

#include "main.h"
#include "settings_menu.h"
#include "energylink.h"
#include "textbox.h"

// Per-player tracking state. Indexed by player index (0-4).
// Kept separate so multiple human players in city trial each track their own
// stats independently and don't corrupt each other's delta calculations.
static int prev_obj_destroyed[5];
static float prev_stats[5][PATCHKIND_NUM];
static float prev_charge_value[5];

// Per-player flag: when set, the next per-frame call snapshots current stats
// as the baseline without counting any delta as energy. Prevents permanent
// patches applied at round start from being counted as generated energy.
static int needs_baseline[5];

// Shared accumulator — contributions from all players pool into one value.
// Buys subtract; the net signed value is flushed to ap_data->energy_send.
static float energy_send_accumulator;

// Last observed value of ap_data->energy_balance. Used to detect AP-side
// receives (other players' generated energy arriving from the server).
// Mod-side withdrawals also change the balance; those show up as negative
// deltas which we silently absorb.
static float prev_energy_balance;

// Detect a positive delta in ap_data->energy_balance since the last call and
// announce it once. Idempotent: safe to invoke multiple times per frame from
// the per-rider procs in CT/AR — only the first call in a frame will see the
// rise (prev == curr after that).
static void TrackEnergyReceive(void)
{
    float curr = ap_data->energy_balance;
    float delta = curr - prev_energy_balance;
    prev_energy_balance = curr;
    if (delta > 0)
        TextBox_Enqueue("Received %.0f energy", delta);
}

// Scale factor for charge energy: a full 0→1 charge is worth this many energy units
#define CHARGE_ENERGY_SCALE 5.0f

// Flush accumulated energy to the shared field when client has cleared it.
static void FlushEnergy(void)
{
    if (energy_send_accumulator != 0 && ap_data->energy_send == 0)
    {
        ap_data->energy_send = energy_send_accumulator;
        OSReport("[EnergyLink] flushed %f energy to energy_send\n", energy_send_accumulator);
        energy_send_accumulator = 0;
    }
}

// Per-frame proc attached to each human rider GOBJ in Air Ride / City Trial.
// Tracks objects destroyed, patches collected, and machine charge.
static void EnergyLink_PerFrame(GOBJ *rg)
{
    TrackEnergyReceive();

    RiderData *rd = rg->userdata;
    int ply = rd->ply;
    GOBJ *mg = rd->machine_gobj;
    MachineData *md = mg ? mg->userdata : 0;

    // On the first frame after intro, snapshot current stats as the baseline
    // so that permanent patches applied at round start aren't counted as energy.
    if (needs_baseline[ply])
    {
        if (Gm_GetIntroState() != GMINTRO_END)
            return;
        needs_baseline[ply] = 0;
        prev_obj_destroyed[ply] = stc_playerdata[ply].objects_destroyed_num;
        for (int i = 0; i < PATCHKIND_NUM; i++)
            prev_stats[ply][i] = rd->stats.values[i];
        prev_charge_value[ply] = md ? md->charge_value : 0.0f;
        return;
    }

    // Objects destroyed (City Trial only — always 0 in Air Ride)
    int diff = stc_playerdata[ply].objects_destroyed_num - prev_obj_destroyed[ply];
    prev_obj_destroyed[ply] = stc_playerdata[ply].objects_destroyed_num;
    if (diff > 0)
        energy_send_accumulator += diff;

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
        energy_send_accumulator += sum;

    // Charge gained from holding A
    if (md)
    {
        float charge_diff = md->charge_value - prev_charge_value[ply];
        if (charge_diff > 0)
            energy_send_accumulator += charge_diff * CHARGE_ENERGY_SCALE;
        prev_charge_value[ply] = md->charge_value;
    }

    FlushEnergy();

    // Auto-charge: spend energy from pool to fill machine charge meter.
    // Computing charge_gain directly avoids the round-trip through SCALE.
    if (ap_menu_settings.energylink_autocharge && md)
    {
        float deficit = 1.0f - md->charge_value;
        float max_gain = ap_data->energy_balance / CHARGE_ENERGY_SCALE;
        float charge_gain = (deficit < max_gain) ? deficit : max_gain;
        if (charge_gain > 0)
        {
            md->charge_value += charge_gain;
            // Inject is invisible to next frame's send delta
            prev_charge_value[ply] = md->charge_value;

            EnergyLink_Withdraw(charge_gain * CHARGE_ENERGY_SCALE);
        }
    }
}

// Per-frame proc for Top Ride charge tracking.
// Top Ride has its own player system (Kirby/Fielder) — no RiderData or MachineData.
// Charge value is at TopRideKirby.charge.charge_value (see docs/topride-system.md).
static void EnergyLink_TopRidePerFrame(GOBJ *g)
{
    TrackEnergyReceive();

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
            energy_send_accumulator += charge_diff * CHARGE_ENERGY_SCALE;
        prev_charge_value[i] = charge;
    }

    FlushEnergy();
}

static void ResetTracking(int needs_baseline_value)
{
    for (int i = 0; i < 5; i++)
    {
        prev_obj_destroyed[i] = 0;
        prev_charge_value[i] = 0.0f;
        needs_baseline[i] = needs_baseline_value;
        for (int j = 0; j < PATCHKIND_NUM; j++)
            prev_stats[i][j] = 0.0f;
    }
    energy_send_accumulator = 0;
    // Seed at the current balance so the first per-frame call doesn't
    // treat the carried-over balance as a fresh receive.
    prev_energy_balance = ap_data->energy_balance;
}

void EnergyLink_On3DLoadEnd()
{
    OSReport("[EnergyLink] Active\n");
    ResetTracking(1);

    // Add the energylink check to all human players in Air Ride / City Trial.
    for (int i = 0; i < 5; i++)
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
    // Top Ride has no patches or intro sequence — no baseline needed.
    ResetTracking(0);
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, EnergyLink_TopRidePerFrame, 0, 0, 0, 0);
    OSReport("[EnergyLink] Active (Top Ride)\n");
}

void EnergyLink_Withdraw(float amount)
{
    ap_data->energy_balance -= amount;
    energy_send_accumulator -= amount;
}

void EnergyLink_Deposit(float amount)
{
    ap_data->energy_balance += amount;
}

void EnergyLink_RebaseStats(int ply)
{
    if (ply < 0 || ply >= 5)
        return;
    GOBJ *r = Ply_GetRiderGObj(ply);
    if (!r)
        return;
    RiderData *rd = r->userdata;
    for (int i = 0; i < PATCHKIND_NUM; i++)
        prev_stats[ply][i] = rd->stats.values[i];
}
