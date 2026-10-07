#include "game.h"
#include "topride.h"
#include "inline.h"

#include "main.h"
#include "settings_menu.h"
#include "energylink.h"

static int prev_obj_destroyed[PLY_NUM];
static float prev_stats[PLY_NUM][PATCHKIND_NUM];
static float prev_charge_value[PLY_NUM];

// Set when the next frame should snapshot a baseline instead of counting a delta, so
// permanent patches applied at round start don't mint energy.
static int needs_baseline[PLY_NUM];

// Sub-MJ carry for the send counters. Kept across scene loads.
static float energy_frac_accumulator;

// Fractional-MJ carry for the local balance decrement, in [0, 1): energy_balance is whole
// MJ but Auto-Charge withdraws less than 1 MJ a frame.
static float withdraw_balance_remainder;

// A full 0 -> 1 charge is worth this many energy units.
#define CHARGE_ENERGY_SCALE 5.0f

static void EnergyLink_Withdraw(float amount);

// Positive amounts are deposits, negative withdrawals. Cast through s32: no libgcc
// float -> s64 routine is linked, and per-frame deltas fit.
static void EnergyLink_Emit(float amount)
{
    energy_frac_accumulator += amount;
    s32 whole = (s32)energy_frac_accumulator;
    if (whole != 0)
    {
        if (whole > 0)
            ap_data->energy_deposit_total += (u32)whole;
        else
            ap_data->energy_withdraw_total += (u32)-whole;
        energy_frac_accumulator -= (float)whole;
    }
}

// Per-frame charge-meter gain per Auto-Charge rate. Capped so the meter rises steadily and
// stacks with the player's own charging instead of snapping to full.
static const float autocharge_rates[] = {
    0.00555f, // Slow,   ~180 frames
    0.01111f, // Medium,  ~90 frames
    0.02222f, // Fast,    ~45 frames
};
_Static_assert(GetElementsIn(autocharge_rates) ==
               APAUTOCHARGE_NUM - APAUTOCHARGE_SLOW, "one rate per Auto-Charge mode");

// The gain's cost (at most ~0.11) is under one energy unit, so any positive balance
// covers a step.
static float AutoCharge_Gain(float charge_value)
{
    int rate = SettingsMenu_AutoChargeRate();
    if (rate < 0 || ap_data->energy_balance <= 0)
        return 0.0f;
    float cap = autocharge_rates[rate];
    float deficit = 1.0f - charge_value;
    return (deficit < cap) ? deficit : cap;
}

static void EnergyLink_PerFrame(GOBJ *rg)
{
    RiderData *rd = rg->userdata;
    int ply = rd->ply;
    GOBJ *mg = rd->machine_gobj;
    MachineData *md = mg ? mg->userdata : 0;

    // A save's first client connect can seed the Mode off mid-round.
    if (!SettingsMenu_EnergyLinkEnabled())
        return;

    if (needs_baseline[ply])
    {
        if (Gm_GetIntroState() != GMINTRO_END)
            return;
        needs_baseline[ply] = 0;
        prev_obj_destroyed[ply] = Ply_GetStats(ply)->objects_destroyed_num;
        memcpy(prev_stats[ply], rd->stats.values, sizeof(prev_stats[ply]));
        prev_charge_value[ply] = md ? md->charge_value : 0.0f;
        return;
    }

    int diff = Ply_GetStats(ply)->objects_destroyed_num - prev_obj_destroyed[ply];
    prev_obj_destroyed[ply] = Ply_GetStats(ply)->objects_destroyed_num;
    if (diff > 0 && SettingsMenu_EnergySourceEnabled(APENERGYSRC_OBJECTS))
        EnergyLink_Emit((float)diff);

    int sum = 0;
    for (int i = 0; i < PATCHKIND_NUM; i++)
    {
        float stat_diff = rd->stats.values[i] - prev_stats[ply][i];
        if (stat_diff > 0)
            sum += stat_diff;
        prev_stats[ply][i] = rd->stats.values[i];
    }
    if (sum > 0 && SettingsMenu_EnergySourceEnabled(APENERGYSRC_PATCHES))
        EnergyLink_Emit((float)sum);

    if (md)
    {
        float charge_diff = md->charge_value - prev_charge_value[ply];
        if (charge_diff > 0 && SettingsMenu_EnergySourceEnabled(APENERGYSRC_CHARGE))
            EnergyLink_Emit(charge_diff * CHARGE_ENERGY_SCALE);
        prev_charge_value[ply] = md->charge_value;
    }

    // Meta Knight's wing has no charge meter: its charge_value is a raw speed term, and
    // pinning it to 1.0 would be a constant max-speed buff.
    if (md && md->kind != VCKIND_WINGMETAKNIGHT)
    {
        float charge_gain = AutoCharge_Gain(md->charge_value);
        if (charge_gain > 0)
        {
            md->charge_value += charge_gain;
            prev_charge_value[ply] = md->charge_value;
            EnergyLink_Withdraw(charge_gain * CHARGE_ENERGY_SCALE);
        }
    }
}

// Top Ride has no RiderData or MachineData.
static void EnergyLink_TopRidePerFrame(GOBJ *g)
{
    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    if (!mgr || !SettingsMenu_EnergyLinkEnabled())
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
        prev_charge_value[i] = charge;
        if (charge_diff > 0 && SettingsMenu_EnergySourceEnabled(APENERGYSRC_CHARGE))
            EnergyLink_Emit(charge_diff * CHARGE_ENERGY_SCALE);

        // TopRide_ChargeUpdate decays charge_value at ~0.3/frame while A isn't held, far
        // more than any gain, so Auto-Charge only tops up a held charge.
        if (kirby->charge.is_charging && kirby->charge.charge_ready)
        {
            float charge_gain = AutoCharge_Gain(kirby->charge.charge_value);
            if (charge_gain > 0)
            {
                kirby->charge.charge_value += charge_gain;
                prev_charge_value[i] = kirby->charge.charge_value;
                EnergyLink_Withdraw(charge_gain * CHARGE_ENERGY_SCALE);
            }
        }
    }
}

static void ResetTracking(int needs_baseline_value)
{
    for (int i = 0; i < PLY_NUM; i++)
    {
        prev_obj_destroyed[i] = 0;
        prev_charge_value[i] = 0.0f;
        needs_baseline[i] = needs_baseline_value;
        for (int j = 0; j < PATCHKIND_NUM; j++)
            prev_stats[i][j] = 0.0f;
    }
    // Safe to clear: the client's next push overwrites the balance.
    withdraw_balance_remainder = 0;
}

void EnergyLink_On3DLoadEnd()
{
    ResetTracking(1);
    OSReport("[EnergyLink] Active for %d player(s)\n", AP_AttachHumanRiderProcs(EnergyLink_PerFrame));
}

void EnergyLink_OnTopRideLoadEnd()
{
    // No patches and no intro, so no baseline frame.
    ResetTracking(0);
    GOBJ_EZCreator(0, GAMEPLINK_SYS, 0, 0, 0, HSD_OBJKIND_NONE, 0, EnergyLink_TopRidePerFrame, 0, 0, 0, 0);
    OSReport("[EnergyLink] Active (Top Ride)\n");
}

// Also decrements the local balance at once: the client refreshes it only on its poll and
// replaces it on push, so Auto-Charge would otherwise keep spending a stale balance.
static void EnergyLink_Withdraw(float amount)
{
    EnergyLink_Emit(-amount);

    withdraw_balance_remainder += amount;
    s32 whole = (s32)withdraw_balance_remainder;
    if (whole > 0)
    {
        ap_data->energy_balance -= whole;
        withdraw_balance_remainder -= (float)whole;
    }
}

s64 EnergyLink_GetBalance(void)
{
    return ap_data->energy_balance;
}

void EnergyLink_DebugSetBalance(s64 mj)
{
    ap_data->energy_balance = mj;
}

void EnergyLink_RebaseStats(int ply)
{
    if (ply < 0 || ply >= PLY_NUM)
        return;
    GOBJ *r = Ply_GetRiderGObj(ply);
    if (!r)
        return;
    RiderData *rd = r->userdata;
    memcpy(prev_stats[ply], rd->stats.values, sizeof(prev_stats[ply]));
}
