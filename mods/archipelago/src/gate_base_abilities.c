#include "game.h"
#include "rider.h"
#include "machine.h"
#include "topride.h"
#include "os.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_base_abilities.h"
#include "textbox_api.h"

// Kirby's three fundamental moves, gated behind AP unlock items. Each stays dead
// until its bit is set in ap_save->base_ability_unlocked_mask. Gating is human-
// only - CPUs keep every ability (gating the universal machine charge for CPUs
// would leave them unable to boost). Every gate is reversible: the patches stay
// installed and simply run the original engine behavior once unlocked.

static const char *const BaseAbility_Names[BASEABILITY_NUM] = {
    "Inhale",
    "Quick Spin",
    "Charge",
};

// Caller-side bounds checks ensure kind is in [0, BASEABILITY_NUM).
static int IsBaseAbilityLocked(BaseAbilityKind kind)
{
    return (ap_save->base_ability_unlocked_mask & (1 << kind)) == 0;
}

static int RiderIsHuman(RiderData *rd)
{
    return rd && Ply_GetPKind(rd->ply) == PKIND_HMN;
}

static int MachineRiderIsHuman(MachineData *md)
{
    GOBJ *rg = md ? md->rider_gobj : 0;
    return rg && RiderIsHuman((RiderData *)rg->userdata);
}

static int TRKirbyIsHuman(TopRideKirby *k)
{
    return k && TopRide_GetPlayerKind(k->player_slot) == TR_PKIND_HMN;
}

// Replaces the bl Rider_StartInhale at 0x8019c610 in Rider_TryStartInhale. When
// inhale is locked for a human, the probe never enters the suck state; otherwise
// the native inhale runs unchanged. hypernova's direct Rider_StartInhale call
// bypasses this probe and is unaffected.
void GateBaseAbilities_StartInhale(RiderData *rd)
{
    if (IsBaseAbilityLocked(BASEABILITY_INHALE) && RiderIsHuman(rd))
        return;
    Rider_StartInhale(rd);
}

// Replaces both bl Rider_QuickSpin_Enter sites (0x801b7ec0 in
// Rider_IASACheck_QuickSpin, 0x801b7e58 in Rider_TryQuickSpinNeutral). Suppresses the spin
// transition for a locked human; the Tornado copy-ability spin uses a different
// entry and is unaffected. Kirby only - Dedede and Meta Knight have their own
// spin enters (below).
void GateBaseAbilities_QuickSpinEnter(float f, RiderData *rd, int dir, int flag)
{
    if (IsBaseAbilityLocked(BASEABILITY_QUICKSPIN) && RiderIsHuman(rd))
        return;
    Rider_QuickSpin_Enter(f, rd, dir, flag);
}

// Dedede and Meta Knight are separate rider characters with their own quick-spin
// enters (action-states 0x2c / 0x2d), reached through per-character IASA checks
// that never touch Kirby's Rider_QuickSpin_Enter. Each enter has a single call
// site, so one REPLACECALL per character gates every state that spins.
void GateBaseAbilities_DededeSpinEnter(RiderData *rd, int dir)
{
    if (IsBaseAbilityLocked(BASEABILITY_QUICKSPIN) && RiderIsHuman(rd))
        return;
    Rider_Dedede_QuickSpin_Enter(rd, dir);
}

void GateBaseAbilities_MetaKnightSpinEnter(RiderData *rd, int dir)
{
    if (IsBaseAbilityLocked(BASEABILITY_QUICKSPIN) && RiderIsHuman(rd))
        return;
    Rider_MetaKnight_QuickSpin_Enter(rd, dir);
}

// Replaces every bl Machine_IncrementCharge: the generic MachinePhys_Charge site
// (0x801ef424), its minimal sibling (0x801ef350), and the Wheel/wheelie vehicle
// callbacks (0x801fa1d4, 0x801fa29c) - wheelie machines charge through a separate
// physics path that bypasses MachinePhys_Charge. Skipping the per-frame
// accumulation for a locked human means holding A builds no charge - so the
// player contributes no EnergyLink energy - while EnergyLink Auto-Charge (which
// writes charge_value directly) still fills the meter from received energy and
// the boost can still be released.
void GateBaseAbilities_IncrementCharge(MachineData *md)
{
    if (IsBaseAbilityLocked(BASEABILITY_CHARGE) && MachineRiderIsHuman(md))
        return;
    Machine_IncrementCharge(md);
}

// The explicit-rate accumulators (Machine_AddCharge / Machine_AddChargeEx) are the
// other way charge_value grows while holding A - flight physics (glide) and
// rail-run / wheelie ready push feed them a precomputed rate. rate stays a named
// param so the compiler preserves f1 across the human check before forwarding it.
void GateBaseAbilities_AddCharge(double rate, MachineData *md)
{
    if (IsBaseAbilityLocked(BASEABILITY_CHARGE) && MachineRiderIsHuman(md))
        return;
    Machine_AddCharge(rate, md);
}

void GateBaseAbilities_AddChargeEx(double rate, MachineData *md)
{
    if (IsBaseAbilityLocked(BASEABILITY_CHARGE) && MachineRiderIsHuman(md))
        return;
    Machine_AddChargeEx(rate, md);
}

// Conditional-hook body for the inline charge-accumulation store at 0x802e01b4
// (stfs f0,52(r3)) in TopRide_ChargeUpdate; the hook passes the charge component
// in r3 and the post-add value in f1. Performs the store for CPUs / unlocked
// humans and skips it for a locked human. Always returns 1 so the hook takes its
// alt exit (0x802e01b8) and the original store never re-runs. is_charging /
// charge_ready and the decay branch are untouched, so EnergyLink's Top Ride
// Auto-Charge (gated on those) still injects received energy.
int GateBaseAbilities_TopRideChargeStore(TopRideChargeComponent *comp, float new_value)
{
    if (IsBaseAbilityLocked(BASEABILITY_CHARGE) && TRKirbyIsHuman((TopRideKirby *)comp->kirby_ptr))
        return 1;
    comp->charge_value = new_value;
    return 1;
}

CODEPATCH_HOOKCONDITIONALCREATE(0x802e01b4,
    "fmr 1,0\n\t",
    GateBaseAbilities_TopRideChargeStore,
    "",
    0,
    0x802e01b8)

// Top Ride's voluntary quick spin (the L/R stick-flick spin attack) is triggered
// in TopRide_KirbyPhysUpdate: each frame it pushes the stick input into the
// kirby's history ring and calls TopRide_KirbyHistoryQuery to measure the ring's
// oscillation - 0 means "no flick", +/-1 means "flick" (and direction). A nonzero
// result enters the spin-attack state. Replacing the bl at 0x802d5f90 with this
// wrapper reports "no flick" (0) for a locked human, so PhysUpdate skips the whole
// spin block; normal steering, which reads the stick separately, is untouched.
// This is a distinct move from the KirbySpin damage spin-out (a hazard reaction),
// which is deliberately NOT gated. The query receives &kirby->history (kirby+0x64)
// in r3, so the kirby is that pointer minus 0x64.
int GateBaseAbilities_TopRideQuickSpinQuery(int *history)
{
    TopRideKirby *k = (TopRideKirby *)((char *)history - 0x64);
    if (IsBaseAbilityLocked(BASEABILITY_QUICKSPIN) && TRKirbyIsHuman(k))
        return 0;
    return TopRide_KirbyHistoryQuery(history);
}

void GateBaseAbilities_OnBoot(void)
{
    // Inhale.
    CODEPATCH_REPLACECALL(0x8019c610, GateBaseAbilities_StartInhale);

    // Quick spin - Kirby (both call sites), Dedede, Meta Knight.
    CODEPATCH_REPLACECALL(0x801b7ec0, GateBaseAbilities_QuickSpinEnter);
    CODEPATCH_REPLACECALL(0x801b7e58, GateBaseAbilities_QuickSpinEnter);
    CODEPATCH_REPLACECALL(0x801c05d4, GateBaseAbilities_DededeSpinEnter);
    CODEPATCH_REPLACECALL(0x801c3f6c, GateBaseAbilities_MetaKnightSpinEnter);
    // Top Ride quick spin - the history-oscillation query that gates the
    // stick-flick spin attack in TopRide_KirbyPhysUpdate.
    CODEPATCH_REPLACECALL(0x802d5f90, GateBaseAbilities_TopRideQuickSpinQuery);

    // 3D machine charge - every accumulator that grows charge_value while holding
    // A. Machine_IncrementCharge (generic grounded + wheelie): four sites.
    CODEPATCH_REPLACECALL(0x801ef424, GateBaseAbilities_IncrementCharge);
    CODEPATCH_REPLACECALL(0x801ef350, GateBaseAbilities_IncrementCharge);
    CODEPATCH_REPLACECALL(0x801fa1d4, GateBaseAbilities_IncrementCharge);
    CODEPATCH_REPLACECALL(0x801fa29c, GateBaseAbilities_IncrementCharge);
    // Explicit-rate accumulators: Machine_AddCharge (flight/glide) and
    // Machine_AddChargeEx (rail-run / wheelie ready push).
    CODEPATCH_REPLACECALL(0x801efa6c, GateBaseAbilities_AddCharge);
    CODEPATCH_REPLACECALL(0x801eb968, GateBaseAbilities_AddChargeEx);
    CODEPATCH_REPLACECALL(0x801f5f30, GateBaseAbilities_AddChargeEx);

    // Top Ride charge accumulation (inline store).
    CODEPATCH_HOOKAPPLY(0x802e01b4);

    OSReport("[GateBaseAbilities] Base ability gating hooks installed\n");
}

int GateBaseAbilities_UnlockAbility(BaseAbilityKind kind)
{
    if (kind >= BASEABILITY_NUM)
        return 0;

    ap_save->base_ability_unlocked_mask |= (1 << kind);
    OSReport("[GateBaseAbilities] Base ability %d (%s) unlocked (mask = %s)\n",
             kind, BaseAbility_Names[kind],
             MaskBits(ap_save->base_ability_unlocked_mask, BASEABILITY_NUM));
    tb_api->EnqueueColoredNoun("Unlock: ", BaseAbility_Names[kind], tb_api->DefaultColor, NULL);
    return 1;
}
