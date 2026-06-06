#include "game.h"
#include "topride.h"
#include "inline.h"

#include "main.h"
#include "settings_menu.h"
#include "energylink.h"
#include "textbox_api.h"

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
// Stays float so per-frame fractional charge gains accumulate exactly. Only
// the integer-truncated portion is committed to ap_data->energy_send each
// flush; the fractional remainder rolls into the next flush.
static float energy_send_accumulator;

// Last observed value of ap_data->energy_balance. Used to detect AP-side
// receives (other players' generated energy arriving from the server).
// Mod-side withdrawals also change the balance; those show up as negative
// deltas which we silently absorb.
static s64 prev_energy_balance;

// Detect a positive delta in ap_data->energy_balance since the last call and
// announce it once. Idempotent: safe to invoke multiple times per frame from
// the per-rider procs in CT/AR — only the first call in a frame will see the
// rise (prev == curr after that).
static void TrackEnergyReceive(void)
{
    s64 curr = ap_data->energy_balance;
    s64 delta = curr - prev_energy_balance;
    prev_energy_balance = curr;
    if (delta > 0)
        tb_api->EnqueueColoredNounFmt("Received: ", "energy", tb_api->EnergyColor, " (+%lld)", delta);
}

// Scale factor for charge energy: a full 0→1 charge is worth this many energy units
#define CHARGE_ENERGY_SCALE 5.0f

// Flush accumulated energy to the shared field when client has cleared it.
// Only the integer portion of the float accumulator is sent; the fractional
// remainder stays behind for the next flush.
//
// The cast goes through s32 deliberately — PowerPC has hardware float→s32
// (fctiwz) but no float→s64 instruction, and we don't link against the
// libgcc soft routines (__fixsfdi). The accumulator only ever holds small
// per-frame deltas (well under s32 range), so the intermediate s32 is safe.
static void FlushEnergy(void)
{
    if (ap_data->energy_send != 0)
        return;
    s32 whole = (s32)energy_send_accumulator;  // truncate toward zero
    if (whole == 0)
        return;
    ap_data->energy_send = (s64)whole;
    energy_send_accumulator -= (float)whole;
    OSReport("[EnergyLink] flushed %d energy to energy_send\n", whole);
}

// Per-frame charge-meter gain for each Auto-Charge Rate setting
// (Slow/Medium/Fast). charge_value spans 0.0–1.0, so ~1/rate frames fill the
// meter from empty — at 60fps that's roughly 3.0s / 1.5s / 0.75s. Auto-Charge
// adds at most this much per frame so the meter rises steadily and stacks with
// the player's own charging, rather than snapping straight to full.
#define AUTOCHARGE_RATE_NUM 3
static const float AUTOCHARGE_RATES[AUTOCHARGE_RATE_NUM] = {
    0.00555f, // Slow   ~180 frames (~3.0s)
    0.01111f, // Medium  ~90 frames (~1.5s)
    0.02222f, // Fast    ~45 frames (~0.75s)
};

// Charge-meter gain to apply this frame for Auto-Charge, given the current
// charge level. Bounded by both the per-frame rate cap (so the meter rises
// steadily) and the remaining deficit (so it never overshoots 1.0). Returns 0
// when no energy is available.
//
// The rate cap keeps the cost (gain * SCALE) well under one energy unit (max
// ≈ 0.11), so any positive integer balance can pay for a full step. That's why
// we only gate on balance > 0 here — none of the s64→float partial-affordability
// math the old fill-the-whole-deficit version needed.
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

    // Auto-charge: spend energy from the pool to top up the machine charge
    // meter, capped per frame so it rises steadily and assists (rather than
    // replaces) the player's own charging. Computing charge_gain directly
    // avoids the round-trip through SCALE.
    if (ap_menu_settings.energylink_autocharge && md)
    {
        float charge_gain = AutoCharge_Gain(md->charge_value);
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
        if (!kirby)
            continue;
        if (TopRide_GetPlayerKind(kirby->player_slot) != TR_PKIND_HMN)
            continue;

        float charge = kirby->charge.charge_value;
        float charge_diff = charge - prev_charge_value[i];
        if (charge_diff > 0)
            energy_send_accumulator += charge_diff * CHARGE_ENERGY_SCALE;
        prev_charge_value[i] = charge;

        // Auto-charge: spend energy from pool to fill the kirby's charge meter.
        // Gated on charge_ready so we don't overwrite charge_value during the
        // post-release depletion phase — that depletion drives the boost
        // mechanic (boost_speed is computed from charge_at_release at the
        // moment A is released; see docs/topride-system.md).
        if (ap_menu_settings.energylink_autocharge && kirby->charge.charge_ready)
        {
            float charge_gain = AutoCharge_Gain(kirby->charge.charge_value);
            if (charge_gain > 0)
            {
                kirby->charge.charge_value += charge_gain;
                // Inject is invisible to next frame's send delta
                prev_charge_value[i] = kirby->charge.charge_value;

                EnergyLink_Withdraw(charge_gain * CHARGE_ENERGY_SCALE);
            }
        }
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

// Queues a withdrawal on the send accumulator. Does NOT update
// ap_data->energy_balance — that's authoritative state owned by the client.
// Callers that want immediate balance UI feedback for an integer purchase
// should decrement ap_data->energy_balance themselves (the next client
// SetReply push will overwrite anyway, but the local subtract avoids a
// brief stale display).
void EnergyLink_Withdraw(float amount)
{
    energy_send_accumulator -= amount;
}

// Local debug-only deposit: bumps the displayed balance without queuing a
// send. The server still owns the true pool value, so the next client push
// will overwrite this. Useful for testing UI/purchase paths without burning
// real pool energy.
//
// Float→s32 cast uses hardware fctiwz; debug deposits are always small
// integer amounts so the s32 intermediate is safe.
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
