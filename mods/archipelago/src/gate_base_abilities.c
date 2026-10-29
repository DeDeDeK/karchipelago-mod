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
#include "ap_announce.h"

static const char *const BaseAbility_Names[BASEABILITY_NUM] = {
    "Inhale",
    "Quick Spin",
    "Charge",
};

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

// Replaces the bl Rider_StartInhale at 0x8019c610 in Rider_TryStartInhale (0x8019c5ac).
static void GateBaseAbilities_StartInhale(RiderData *rd)
{
    if (IsBaseAbilityLocked(BASEABILITY_INHALE) && RiderIsHuman(rd))
        return;
    Rider_StartInhale(rd);
}

// Replaces both bl RiderState_QuickSpinEnter: 0x801b7ec0 in RiderState_QuickSpinInterrupt
// (0x801b7e80) and 0x801b7e58 in Rider_TryQuickSpinNeutral (0x801b7e0c).
static void GateBaseAbilities_QuickSpinEnter(float f, RiderData *rd, int dir, int flag)
{
    if (IsBaseAbilityLocked(BASEABILITY_QUICKSPIN) && RiderIsHuman(rd))
        return;
    RiderState_QuickSpinEnter(f, rd, dir, flag);
}

// Dedede's and Meta Knight's own enters, one call each: 0x801c05d4 in
// RiderState_DededeQuickSpinInterrupt (0x801c05a8) and 0x801c3f6c in
// RiderState_MetaKnightQuickSpinInterrupt (0x801c3f40).
static void GateBaseAbilities_DededeSpinEnter(RiderData *rd, int dir)
{
    if (IsBaseAbilityLocked(BASEABILITY_QUICKSPIN) && RiderIsHuman(rd))
        return;
    RiderState_DededeQuickSpinEnter(rd, dir);
}

static void GateBaseAbilities_MetaKnightSpinEnter(RiderData *rd, int dir)
{
    if (IsBaseAbilityLocked(BASEABILITY_QUICKSPIN) && RiderIsHuman(rd))
        return;
    RiderState_MetaKnightQuickSpinEnter(rd, dir);
}

// Replaces every bl Machine_IncrementCharge: 0x801ef424 in MachinePhys_Charge (0x801ef364),
// 0x801ef350 in Machine_Star_PushChargeUpdate (0x801ef338), 0x801fa1d4 in
// Machine_Wheel_PushChargeUpdate (0x801fa1c8) and 0x801fa29c in
// fn_VehicleStatTableFuncCallbacks_Wheel_RunPush_3 (0x801fa1e8).
static void GateBaseAbilities_IncrementCharge(MachineData *md)
{
    if (IsBaseAbilityLocked(BASEABILITY_CHARGE) && MachineRiderIsHuman(md))
        return;
    Machine_IncrementCharge(md);
}

// The explicit-rate accumulators; rate is a named parameter so f1 survives the human
// check. AddCharge's one site is 0x801efa6c in
// fn_VehicleStatTableFuncCallbacks_Star_Fly_3_HandleFlightPhysics (0x801ef9a0); AddChargeEx's
// are 0x801eb968 in Machine_Star_RailPushAddCharge (0x801eb95c) and 0x801f5f30 in
// Machine_Wheel_PushAddCharge (0x801f5f24).
static void GateBaseAbilities_AddCharge(double rate, MachineData *md)
{
    if (IsBaseAbilityLocked(BASEABILITY_CHARGE) && MachineRiderIsHuman(md))
        return;
    Machine_AddCharge(rate, md);
}

static void GateBaseAbilities_AddChargeEx(double rate, MachineData *md)
{
    if (IsBaseAbilityLocked(BASEABILITY_CHARGE) && MachineRiderIsHuman(md))
        return;
    Machine_AddChargeEx(rate, md);
}

// The inline charge store at 0x802e01b4 (stfs f0, 52(r3)) in TopRide_ChargeUpdate
// (0x802df900): r3 = the charge component, f1 = the new value. Always takes the alt exit,
// so the original store never runs.
static int GateBaseAbilities_TopRideChargeStore(TopRideChargeComponent *comp, float new_value)
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

// Replaces the bl TopRide_KirbyHistoryQuery at 0x802d5f90 in TopRide_KirbyPhysUpdate
// (0x802d5ec0), the stick-flick spin's query. Returning 0 ("no flick") skips the spin.
static int GateBaseAbilities_TopRideQuickSpinQuery(int *history)
{
    TopRideKirby *k = (TopRideKirby *)((char *)history - offsetof(TopRideKirby, history));
    if (IsBaseAbilityLocked(BASEABILITY_QUICKSPIN) && TRKirbyIsHuman(k))
        return 0;
    return TopRide_KirbyHistoryQuery(history);
}

void GateBaseAbilities_OnBoot(void)
{
    CODEPATCH_REPLACECALL(0x8019c610, GateBaseAbilities_StartInhale);

    CODEPATCH_REPLACECALL(0x801b7ec0, GateBaseAbilities_QuickSpinEnter);
    CODEPATCH_REPLACECALL(0x801b7e58, GateBaseAbilities_QuickSpinEnter);
    CODEPATCH_REPLACECALL(0x801c05d4, GateBaseAbilities_DededeSpinEnter);
    CODEPATCH_REPLACECALL(0x801c3f6c, GateBaseAbilities_MetaKnightSpinEnter);
    CODEPATCH_REPLACECALL(0x802d5f90, GateBaseAbilities_TopRideQuickSpinQuery);

    CODEPATCH_REPLACECALL(0x801ef424, GateBaseAbilities_IncrementCharge);
    CODEPATCH_REPLACECALL(0x801ef350, GateBaseAbilities_IncrementCharge);
    CODEPATCH_REPLACECALL(0x801fa1d4, GateBaseAbilities_IncrementCharge);
    CODEPATCH_REPLACECALL(0x801fa29c, GateBaseAbilities_IncrementCharge);
    CODEPATCH_REPLACECALL(0x801efa6c, GateBaseAbilities_AddCharge);
    CODEPATCH_REPLACECALL(0x801eb968, GateBaseAbilities_AddChargeEx);
    CODEPATCH_REPLACECALL(0x801f5f30, GateBaseAbilities_AddChargeEx);

    CODEPATCH_HOOKAPPLY(0x802e01b4);

    OSReport("[GateBaseAbilities] Hooks installed\n");
}

int GateBaseAbilities_UnlockAbility(BaseAbilityKind kind)
{
    if (kind >= BASEABILITY_NUM)
        return 0;

    ap_save->base_ability_unlocked_mask |= (1 << kind);
    OSReport("[GateBaseAbilities] Base ability %d (%s) unlocked (mask = %s)\n",
             kind, BaseAbility_Names[kind],
             MaskBits(ap_save->base_ability_unlocked_mask, BASEABILITY_NUM));
    APAnnounce_Grant("Unlocked Ability: ", BaseAbility_Names[kind], tb_api->DefaultColor, NULL);
    return 1;
}
