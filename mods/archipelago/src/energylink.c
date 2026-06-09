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

// Sub-MJ carry for the cumulative send counter. Generation (+) and Auto-Charge
// withdrawals (−) from all players fold in here at float precision; whenever the
// carry crosses a whole MJ, EnergyLink_Emit commits that whole part to
// ap_data->energy_sent_total and keeps the fractional remainder. Persists across
// scene loads (it's pending sub-MJ energy, not per-scene state) — ResetTracking
// must not zero it; only a fresh mod boot resets the channel. See EnergyLink_Emit.
static float energy_frac_accumulator;

// Fractional-MJ carry for the local-balance decrement in EnergyLink_Withdraw.
// Auto-Charge withdraws < 1 MJ per frame but ap_data->energy_balance is integer
// raw MJ, so we accumulate the fraction here and commit only whole MJ. Stays in
// [0, 1). Reset by ResetTracking. See EnergyLink_Withdraw for the rationale.
static float withdraw_balance_remainder;

// Scale factor for charge energy: a full 0→1 charge is worth this many energy units
#define CHARGE_ENERGY_SCALE 5.0f

// Commit energy into the cumulative game→client counter.
// amount: + for a deposit (generation), − for a withdrawal (spend/Auto-Charge).
// Sub-MJ amounts accumulate in energy_frac_accumulator; whenever the carry
// crosses a whole MJ, that whole part is committed to ap_data->energy_sent_total
// and the remainder rolls forward. No flush gate, no slot handshake, no
// per-frame polling — the client reads-and-diffs the counter on its own ~1s
// cadence, so any number of Emits between polls collapse into one net delta.
//
// The cast goes through s32 deliberately — PowerPC has hardware float→s32
// (fctiwz) but no float→s64 instruction, and we don't link against the libgcc
// soft routines (__fixsfdi). The carry only ever holds small per-frame deltas
// (well under s32 range), so the intermediate s32 is safe. The s64 += s32 on the
// counter is inline (addc/adde), no libgcc.
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

    // Auto-charge: spend energy from the pool to top up the machine charge
    // meter, capped per frame so it rises steadily and assists (rather than
    // replaces) the player's own charging. Computing charge_gain directly
    // avoids the round-trip through SCALE.
    //
    // Skipped for Meta Knight (VCKIND_WINGMETAKNIGHT): his Wing machine has no
    // chargeable boost meter, so charge_value behaves as a raw speed term —
    // pinning it at 1.0 every frame gives him a constant max-speed buff rather
    // than assisting a charge. (Dedede has a normal charge meter and is fine.)
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
// Top Ride has its own player system (Kirby/Fielder) — no RiderData or MachineData.
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

        // Auto-charge: spend energy from the pool to assist the kirby's charge.
        //
        // Gated on is_charging (A held) AND charge_ready, because in Top Ride
        // the engine DECAYS charge_value toward 0 every frame that A isn't held:
        // TopRide_ChargeUpdate takes its depletion branch whenever
        // (!A_held || !charge_ready) and subtracts ~0.3/frame — far more than
        // our per-frame inject cap (~0.02). So injecting while the player is idle
        // is futile (the engine wipes it next frame, meter never fills). The only
        // window where the engine ACCUMULATES — so our injection sticks and
        // stacks — is while the player is actively charging: A held (is_charging)
        // and not in the post-release boost lockout (charge_ready). Topping up
        // there lets them reach a bigger boost with less hold time.
        //
        // Known limitation vs AR/CT: in 3D mode the engine doesn't bleed off an
        // idle charge, so the AR/CT block above (no is_charging gate) fills the
        // meter PASSIVELY even when A isn't held. Top Ride can't — the decay
        // above makes passive fill physically impossible, so here Auto-Charge
        // only assists active charging.
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
    // energy_frac_accumulator is intentionally NOT reset here: it holds pending
    // sub-MJ energy that must persist across scene loads, since the cumulative
    // counter only resets on a fresh mod boot. withdraw_balance_remainder is
    // local display rounding for energy_balance (which set_notify overwrites
    // anyway), so clearing it per scene is harmless.
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

void EnergyLink_OnTopRideLoad()
{
    // Top Ride has no patches or intro sequence — no baseline needed.
    ResetTracking(0);
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, EnergyLink_TopRidePerFrame, 0, 0, 0, 0);
    OSReport("[EnergyLink] Active (Top Ride)\n");
}

// Emits a withdrawal into the cumulative send counter (the client diffs the
// counter and forwards the delta to the server) AND decrements the locally-known
// balance so affordability checks self-limit.
//
// ap_data->energy_balance is the mod's view of the shared pool, refreshed by
// the client's set_notify push at its ~1s poll rate. The Auto-Charge gate
// (AutoCharge_Gain) and the Buy gate both read it to decide whether energy is
// available. If withdrawals didn't reduce it until the next push, Auto-Charge
// would keep approving spends against a stale positive balance every frame for
// up to ~1s — committing far more energy than the pool holds, which the client
// then reports back as "withdrawal under-subtracted ... pool was lower than the
// mod expected". Decrementing here closes that window: the gate sees the
// balance fall in real time and stops once the locally-known pool is exhausted.
// set_notify still overwrites with the authoritative value — it replaces rather
// than subtracts, so the local decrement isn't double-counted.
//
// Auto-Charge spends fractional MJ per frame but energy_balance is integer raw
// MJ, so we accumulate the fraction in withdraw_balance_remainder and commit
// only whole MJ. amount is always positive (callers withdraw; deposits use
// EnergyLink_Deposit), so the remainder stays in [0, 1). All casts are hardware
// float<->s32 (fctiwz) — no s64<->float libgcc helpers.
void EnergyLink_Withdraw(float amount)
{
    // Send: fold the withdrawal into the cumulative counter (fractional-safe).
    EnergyLink_Emit(-amount);

    // Local balance: decrement whole MJ immediately so affordability gates
    // self-limit (see rationale above); the fractional remainder rolls forward.
    withdraw_balance_remainder += amount;
    s32 whole = (s32)withdraw_balance_remainder;
    if (whole > 0)
    {
        ap_data->energy_balance -= whole;
        withdraw_balance_remainder -= (float)whole;
    }
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
