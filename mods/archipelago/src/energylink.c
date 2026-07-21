#include "game.h"
#include "topride.h"
#include "inline.h"

#include "main.h"
#include "settings_menu.h"
#include "energylink.h"

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

// Sub-MJ carry for the cumulative send counter. Generation and Auto-Charge
// withdrawals fold in at float precision; EnergyLink_Emit commits whole MJ to
// ap_data->energy_sent_total and keeps the remainder. Persists across scene
// loads (pending sub-MJ energy) - ResetTracking must not zero it.
static float energy_frac_accumulator;

// Fractional-MJ carry for the local-balance decrement. Auto-Charge withdraws
// < 1 MJ/frame but energy_balance is integer raw MJ, so the fraction accumulates
// here and only whole MJ commit. Stays in [0, 1); reset by ResetTracking.
static float withdraw_balance_remainder;

// Scale factor for charge energy: a full 0→1 charge is worth this many energy units
#define CHARGE_ENERGY_SCALE 5.0f

// Internal: Auto-Charge fractional withdrawal. Called above its definition.
static void EnergyLink_Withdraw(float amount);

// Commit energy into the cumulative game→client counter (+ deposit, − withdrawal).
// Sub-MJ amounts accumulate in energy_frac_accumulator; whole MJ commit to
// ap_data->energy_sent_total and the remainder rolls forward. The client reads-
// and-diffs the counter on its own ~1s cadence, so no flush or handshake is needed.
// The cast goes through s32 deliberately: PPC has hardware float→s32 (fctiwz) but
// not float→s64, and we don't link libgcc soft routines; per-frame deltas fit s32.
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

// Per-frame charge-meter gain for each Auto-Charge Rate setting
// (Slow/Medium/Fast). charge_value spans 0.0–1.0, so ~1/rate frames fill the
// meter from empty - at 60fps that's roughly 3.0s / 1.5s / 0.75s. Auto-Charge
// adds at most this much per frame so the meter rises steadily and stacks with
// the player's own charging, rather than snapping straight to full.
#define AUTOCHARGE_RATE_NUM 3
static const float AUTOCHARGE_RATES[AUTOCHARGE_RATE_NUM] = {
    0.00555f, // Slow   ~180 frames (~3.0s)
    0.01111f, // Medium  ~90 frames (~1.5s)
    0.02222f, // Fast    ~45 frames (~0.75s)
};

// Charge-meter gain to apply this frame for Auto-Charge. Bounded by the per-frame
// rate cap (steady rise) and the remaining deficit (never overshoots 1.0). The
// cost (gain * SCALE, max ≈ 0.11) stays under one energy unit, so any positive
// balance covers a step - hence the plain balance > 0 gate. Returns 0 if broke.
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
        prev_obj_destroyed[ply] = stc_playerdata[ply].stat_record.objects_destroyed_num;
        for (int i = 0; i < PATCHKIND_NUM; i++)
            prev_stats[ply][i] = rd->stats.values[i];
        prev_charge_value[ply] = md ? md->charge_value : 0.0f;
        return;
    }

    // Objects destroyed (City Trial only - always 0 in Air Ride)
    int diff = stc_playerdata[ply].stat_record.objects_destroyed_num - prev_obj_destroyed[ply];
    prev_obj_destroyed[ply] = stc_playerdata[ply].stat_record.objects_destroyed_num;
    if (diff > 0)
        EnergyLink_Emit((float)diff);

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
        EnergyLink_Emit((float)sum);

    // Charge gained from holding A
    if (md)
    {
        float charge_diff = md->charge_value - prev_charge_value[ply];
        if (charge_diff > 0)
            EnergyLink_Emit(charge_diff * CHARGE_ENERGY_SCALE);
        prev_charge_value[ply] = md->charge_value;
    }

    // Auto-charge: spend pool energy to top up the machine charge meter, capped
    // per frame so it assists rather than replaces the player's own charging.
    // Skipped for Meta Knight (VCKIND_WINGMETAKNIGHT): his Wing machine has no
    // charge meter, so charge_value is a raw speed term and pinning it to 1.0
    // would be a constant max-speed buff. (Dedede's meter is normal, so he's fine.)
    if (ap_menu_settings.energylink_autocharge && md && md->kind != VCKIND_WINGMETAKNIGHT)
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
// Top Ride has its own player system (Kirby/Fielder) - no RiderData or MachineData.
// Charge value is at TopRideKirby.charge.charge_value.
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

        // Auto-charge: assist the kirby's charge. Gated on is_charging (A held)
        // AND charge_ready because TopRide_ChargeUpdate decays charge_value toward
        // 0 (~0.3/frame) whenever A isn't held - far more than our inject cap
        // (~0.02), so passive fill is impossible in TR. Injecting only while the
        // player actively charges lets them reach a bigger boost with less hold.
        if (ap_menu_settings.energylink_autocharge && kirby->charge.is_charging && kirby->charge.charge_ready)
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
    // energy_frac_accumulator is NOT reset here: it holds pending sub-MJ energy
    // that persists across scene loads (the counter only resets on mod boot).
    // withdraw_balance_remainder is harmless to clear (set_notify overwrites it).
    withdraw_balance_remainder = 0;
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

void EnergyLink_OnTopRideLoadEnd()
{
    // Top Ride has no patches or intro sequence - no baseline needed.
    ResetTracking(0);
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, EnergyLink_TopRidePerFrame, 0, 0, 0, 0);
    OSReport("[EnergyLink] Active (Top Ride)\n");
}

// Emit a withdrawal into the send counter AND decrement the local balance so
// affordability gates self-limit. energy_balance is only refreshed by set_notify
// at ~1s; without the immediate decrement, Auto-Charge would keep approving spends
// against a stale positive balance every frame and over-commit the pool. set_notify
// later overwrites with the authoritative value (replaces, so no double-count).
// Fractional MJ accumulate in withdraw_balance_remainder; only whole MJ commit.
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

// Local debug-only deposit: bumps the displayed balance without queuing a send.
// The next client push overwrites it, so this only touches the local view.
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
