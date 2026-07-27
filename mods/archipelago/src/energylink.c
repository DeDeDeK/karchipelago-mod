#include "game.h"
#include "topride.h"
#include "inline.h"

#include "main.h"
#include "settings_menu.h"
#include "energylink.h"

// Per-player tracking state, so multiple humans in City Trial don't corrupt each
// other's delta calculations.
static int prev_obj_destroyed[5];
static float prev_stats[5][PATCHKIND_NUM];
static float prev_charge_value[5];

// When set, the next per-frame call snapshots current stats as the baseline
// without counting any delta as energy, so permanent patches applied at round
// start don't mint energy.
static int needs_baseline[5];

// Sub-MJ carry for the cumulative send counter. Persists across scene loads -
// ResetTracking must not zero it.
static float energy_frac_accumulator;

// Fractional-MJ carry for the local-balance decrement, kept in [0, 1) because
// energy_balance is integer raw MJ but Auto-Charge withdraws less than 1 MJ/frame.
static float withdraw_balance_remainder;

// A full 0->1 charge is worth this many energy units.
#define CHARGE_ENERGY_SCALE 5.0f

static void EnergyLink_Withdraw(float amount);

// Commit energy into the cumulative game -> client counter (+ deposit,
// - withdrawal). Whole MJ land on ap_data->energy_sent_total and the remainder
// rolls forward. The cast goes through s32 deliberately: PPC has hardware
// float->s32 (fctiwz) but not float->s64, and we don't link the libgcc soft
// routines; per-frame deltas fit s32.
static void EnergyLink_Emit(float amount)
{
    energy_frac_accumulator += amount;
    s32 whole = (s32)energy_frac_accumulator;  // truncate toward zero
    if (whole != 0)
    {
        ap_data->energy_sent_total += whole;
        energy_frac_accumulator -= (float)whole;
    }
}

// Per-frame charge-meter gain for each Auto-Charge Rate setting. Capping the gain
// makes the meter rise steadily and stack with the player's own charging instead
// of snapping straight to full.
#define AUTOCHARGE_RATE_NUM 3
static const float AUTOCHARGE_RATES[AUTOCHARGE_RATE_NUM] = {
    0.00555f, // Slow   ~180 frames (~3.0s)
    0.01111f, // Medium  ~90 frames (~1.5s)
    0.02222f, // Fast    ~45 frames (~0.75s)
};

// Bounded by the per-frame rate cap and the remaining deficit, so the cost
// (gain * SCALE, max ~0.11) stays under one energy unit and any positive balance
// covers a step - hence the plain balance > 0 gate.
static float AutoCharge_Gain(float charge_value)
{
    if (ap_data->energy_balance <= 0)
        return 0.0f;
    int ri = ap_menu_settings.energylink_autocharge_rate;
    if (ri < 0)
        ri = 0;
    else if (ri >= AUTOCHARGE_RATE_NUM)
        ri = AUTOCHARGE_RATE_NUM - 1;
    float cap = AUTOCHARGE_RATES[ri];
    float deficit = 1.0f - charge_value;
    return (deficit < cap) ? deficit : cap;
}

// Per-frame proc attached to each human rider GOBJ in Air Ride / City Trial.
static void EnergyLink_PerFrame(GOBJ *rg)
{
    RiderData *rd = rg->userdata;
    int ply = rd->ply;
    GOBJ *mg = rd->machine_gobj;
    MachineData *md = mg ? mg->userdata : 0;

    if (needs_baseline[ply])
    {
        if (Gm_GetIntroState() != GMINTRO_END)
            return;
        needs_baseline[ply] = 0;
        prev_obj_destroyed[ply] = stc_playerdata[ply].stat_record.objects_destroyed_num;
        for (int i = 0; i < PATCHKIND_NUM; i++)
            prev_stats[ply][i] = rd->stats.values[i];
        prev_charge_value[ply] = md ? md->charge_value : 0.0f;
        return;
    }

    // Objects destroyed - always 0 in Air Ride
    int diff = stc_playerdata[ply].stat_record.objects_destroyed_num - prev_obj_destroyed[ply];
    prev_obj_destroyed[ply] = stc_playerdata[ply].stat_record.objects_destroyed_num;
    if (diff > 0)
        EnergyLink_Emit((float)diff);

    int sum = 0;
    for (int i = 0; i < PATCHKIND_NUM; i++)
    {
        float stat_diff = rd->stats.values[i] - prev_stats[ply][i];
        if (stat_diff > 0)
            sum += stat_diff;
        prev_stats[ply][i] = rd->stats.values[i];
    }
    if (sum > 0)
        EnergyLink_Emit((float)sum);

    if (md)
    {
        float charge_diff = md->charge_value - prev_charge_value[ply];
        if (charge_diff > 0)
            EnergyLink_Emit(charge_diff * CHARGE_ENERGY_SCALE);
        prev_charge_value[ply] = md->charge_value;
    }

    // Skipped for Meta Knight: his Wing machine has no charge meter, so
    // charge_value is a raw speed term and pinning it to 1.0 would be a constant
    // max-speed buff. Dedede's meter is normal.
    if (ap_menu_settings.energylink_autocharge && md && md->kind != VCKIND_WINGMETAKNIGHT)
    {
        float charge_gain = AutoCharge_Gain(md->charge_value);
        if (charge_gain > 0)
        {
            md->charge_value += charge_gain;
            // Keep the inject invisible to next frame's send delta
            prev_charge_value[ply] = md->charge_value;

            EnergyLink_Withdraw(charge_gain * CHARGE_ENERGY_SCALE);
        }
    }
}

// Top Ride has its own player system - no RiderData or MachineData.
static void EnergyLink_TopRidePerFrame(GOBJ *g)
{
    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    if (!mgr)
        return;

    for (int i = 0; i < 4; i++)
    {
        TopRideKirby *kirby = mgr->kirbys[i];
        if (!kirby)
            continue;
        if (TopRide_GetPlayerKind(kirby->player_slot) != TR_PKIND_HMN)
            continue;

        float charge = kirby->charge.charge_value;
        float charge_diff = charge - prev_charge_value[i];
        if (charge_diff > 0)
            EnergyLink_Emit(charge_diff * CHARGE_ENERGY_SCALE);
        prev_charge_value[i] = charge;

        // Gated on is_charging (A held) as well as charge_ready because
        // TopRide_ChargeUpdate decays charge_value toward 0 at ~0.3/frame whenever
        // A isn't held - far more than the inject cap (~0.02), so passive fill is
        // impossible in TR.
        if (ap_menu_settings.energylink_autocharge && kirby->charge.is_charging && kirby->charge.charge_ready)
        {
            float charge_gain = AutoCharge_Gain(kirby->charge.charge_value);
            if (charge_gain > 0)
            {
                kirby->charge.charge_value += charge_gain;
                // Keep the inject invisible to next frame's send delta
                prev_charge_value[i] = kirby->charge.charge_value;

                EnergyLink_Withdraw(charge_gain * CHARGE_ENERGY_SCALE);
            }
        }
    }
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
    // energy_frac_accumulator is deliberately not reset: it holds pending sub-MJ
    // energy that persists across scene loads. withdraw_balance_remainder is
    // harmless to clear, since the client's next push overwrites the balance.
    withdraw_balance_remainder = 0;
}

void EnergyLink_On3DLoadEnd()
{
    OSReport("[EnergyLink] Active\n");
    ResetTracking(1);

    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) == PKIND_HMN)
        {
            GOBJ *r = Ply_GetRiderGObj(i);
            if (r)
            {
                // Runs after hit collision is applied
                GObj_AddProc(r, EnergyLink_PerFrame, RDPRI_HITCOLL + 1);
            }
        }
    }
}

void EnergyLink_OnTopRideLoadEnd()
{
    // Top Ride has no patches or intro sequence - no baseline needed.
    ResetTracking(0);
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, EnergyLink_TopRidePerFrame, 0, 0, 0, 0);
    OSReport("[EnergyLink] Active (Top Ride)\n");
}

// Emit a withdrawal into the send counter and decrement the local balance so
// affordability gates self-limit. The client only refreshes energy_balance about
// once a second; without the immediate decrement Auto-Charge would keep approving
// spends against a stale positive balance and over-commit the pool. The client's
// push replaces rather than subtracts, so the local decrement is never
// double-counted.
static void EnergyLink_Withdraw(float amount)
{
    EnergyLink_Emit(-amount);

    // Decrement whole MJ immediately; the fractional remainder rolls forward.
    withdraw_balance_remainder += amount;
    s32 whole = (s32)withdraw_balance_remainder;
    if (whole > 0)
    {
        ap_data->energy_balance -= whole;
        withdraw_balance_remainder -= (float)whole;
    }
}

// Debug only: bumps the displayed balance without queuing a send. The next client
// push overwrites it.
void EnergyLink_Deposit(float amount)
{
    ap_data->energy_balance += (s64)(s32)amount;
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
