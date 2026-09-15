// CPU riders read per-machine rows out of DOL tables indexed by kind, and a few of their
// decisions switch on the kind itself. Every reader reaches the kind through
// Machine_GetAbsoluteKind, which folds an appended class slot onto some other kind, so each
// is replaced here: a vanilla machine gets exactly its engine row, a custom machine the CPU
// rows its descriptor authors.

#include "os.h"
#include "machine.h"
#include "rider.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

static const CustomMachineCpu *CpuFor(MachineData *md)
{
    CustomMachineEntry *e = CustomMachines_FindByClassSlot(md->is_bike, md->kind);
    return e != NULL ? &e->cpu : NULL;
}

static const CustomMachineCpu *RiderCpu(RiderData *rd)
{
    return rd->machine_gobj != NULL ? CpuFor(rd->machine_gobj->userdata) : NULL;
}

static const CpuMachineCaps *VanillaCaps(MachineData *md)
{
    return md->is_bike ? &stc_cpu_machine_caps_bike[md->kind] : &stc_cpu_machine_caps_star[md->kind];
}

// Bikes and riders with no machine read the Warp Star's row.
static const CpuMachineSteer *VanillaSteer(RiderData *rd)
{
    MachineData *md = rd->machine_gobj != NULL ? rd->machine_gobj->userdata : NULL;
    return &stc_cpu_machine_steer_star[md != NULL && !md->is_bike ? md->kind : 0];
}

static int HasCap(GOBJ *machine, int flag)
{
    if (machine == NULL)
        return 0;

    MachineData *md = machine->userdata;
    const CustomMachineCpu *c = CpuFor(md);
    return ((c != NULL ? c->flags : VanillaCaps(md)->flags) & flag) != 0;
}

static int GetSwapScore(GOBJ *machine)
{
    if (machine == NULL)
        return -1;

    MachineData *md = machine->userdata;
    const CustomMachineCpu *c = CpuFor(md);
    return c != NULL ? c->swap_score : VanillaCaps(md)->swap_score;
}

static float GetChargeRelease(GOBJ *machine)
{
    if (machine == NULL)
        return 0.0f;

    MachineData *md = machine->userdata;
    const CustomMachineCpu *c = CpuFor(md);
    return c != NULL ? c->charge_release : VanillaCaps(md)->charge_release;
}

static int CanSwap(GOBJ *machine)            { return HasCap(machine, CPUMACHCAP_SWAP); }
static int CanBrake(GOBJ *machine)           { return HasCap(machine, CPUMACHCAP_BRAKE); }
static int CanChargeHold(GOBJ *machine)      { return HasCap(machine, CPUMACHCAP_CHARGE_HOLD); }
static int CanRamCharge(GOBJ *machine)       { return HasCap(machine, CPUMACHCAP_RAM_CHARGE); }
static int SkipsPassageBranch(GOBJ *machine) { return HasCap(machine, CPUMACHCAP_SKIP_PASSAGE_BRANCH); }

// Vanilla numbers the spawn slot it asks after with the machine's absolute kind. A
// custom kind has no slot of that number, so it asks after its own.
static int IsKindSlotFilled(GOBJ *machine)
{
    MachineData *md = machine->userdata;

    if (CpuFor(md) != NULL)
        return md->city_spawn_slot < CITY_SPAWN_SLOT_NUM &&
               CityMachineSpawn_GetSlotGObj(md->city_spawn_slot) != NULL;
    return CityMachineSpawn_GetSlotGObj(Machine_GetAbsoluteKind(machine)) != NULL;
}

static int GetChargeHoldGate(GOBJ *machine, float *a, float *b)
{
    const CustomMachineCpu *c = CpuFor(machine->userdata);

    if (c != NULL)
    {
        if (!c->has_charge_hold_gate)
            return 0;
        *a = c->charge_hold_gate[0];
        *b = c->charge_hold_gate[1];
        return 1;
    }

    switch (Machine_GetAbsoluteKind(machine))
    {
        case VCKIND_BULK:    *a = *stc_cpu_charge_gate_bulk;    *b = *stc_cpu_charge_gate_low;       return 1;
        case VCKIND_HYDRA:   *a = *stc_cpu_charge_gate_hydra;   *b = *stc_cpu_charge_gate_hydra_low; return 1;
        case VCKIND_ROCKET:  *a = *stc_cpu_charge_gate_rocket;  *b = *stc_cpu_charge_gate_low;       return 1;
        case VCKIND_FORMULA: *a = *stc_cpu_charge_gate_formula; *b = *stc_cpu_charge_gate_low;       return 1;
        default:             return 0;
    }
}

static int GetChargeReleaseOverride(GOBJ *machine, float *level)
{
    const CustomMachineCpu *c = CpuFor(machine->userdata);

    if (c != NULL)
    {
        if (!c->has_release_level)
            return 0;
        *level = c->release_level;
        return 1;
    }

    switch (Machine_GetAbsoluteKind(machine))
    {
        case VCKIND_BULK:  *level = *stc_cpu_charge_release_bulk;  return 1;
        case VCKIND_HYDRA: *level = *stc_cpu_charge_release_hydra; return 1;
        default:           return 0;
    }
}

static float AlignCosNear(RiderData *rd)
{
    const CustomMachineCpu *c = RiderCpu(rd);
    return c != NULL ? c->align_cos_near : VanillaSteer(rd)->align_cos_near;
}

static float AlignCosFar(RiderData *rd)
{
    const CustomMachineCpu *c = RiderCpu(rd);
    return c != NULL ? c->align_cos_far : VanillaSteer(rd)->align_cos_far;
}

static float TurnTolerance(RiderData *rd)
{
    const CustomMachineCpu *c = RiderCpu(rd);
    return c != NULL ? c->turn_tolerance : VanillaSteer(rd)->turn_tolerance;
}

static float StuckAngle(RiderData *rd)
{
    const CustomMachineCpu *c = RiderCpu(rd);
    return c != NULL ? c->stuck_angle : VanillaSteer(rd)->stuck_angle;
}

static int StadiumParam(RiderData *rd, int high_jump, float *pitch, float *min_len)
{
    if (rd->machine_gobj == NULL)
        return 0;

    const CustomMachineCpu *c = RiderCpu(rd);
    if (c != NULL)
    {
        *pitch = high_jump ? c->high_jump_pitch : c->air_glider_pitch;
        *min_len = high_jump ? c->high_jump_min_len : c->air_glider_min_len;
        return 1;
    }

    CpuStadiumMachineParam *table = high_jump ? stc_cpu_highjump_machine : stc_cpu_airglider_machine;
    CpuStadiumMachineParam *p = &table[Machine_GetAbsoluteKind(rd->machine_gobj)];
    *pitch = p->pitch;
    *min_len = p->min_len;
    return 1;
}

static int AirGliderParam(RiderData *rd, float *pitch, float *min_len)
{
    return StadiumParam(rd, 0, pitch, min_len);
}

static int HighJumpParam(RiderData *rd, float *pitch, float *min_len)
{
    return StadiumParam(rd, 1, pitch, min_len);
}

// Rider_ProcessCPUDistance caches the kind for Rider_CPUEmitSteerStick, which only compares
// it against Hydra, Winged and Jet Star to pick a stick pitch. A custom machine caches
// whichever of those flies the pitch it authors, and its own MachineKind otherwise.
static int CachedKind(GOBJ *machine)
{
    MachineData *md = machine->userdata;
    CustomMachineEntry *e = CustomMachines_FindByClassSlot(md->is_bike, md->kind);

    if (e == NULL)
        return Machine_GetAbsoluteKind(machine);
    if (e->cpu.stick_pitch == CUSTOM_MACHINE_CPU_PITCH_CLIMB)
        return VCKIND_HYDRA;
    if (e->cpu.stick_pitch == CUSTOM_MACHINE_CPU_PITCH_DIVE)
        return VCKIND_WINGED;
    return e->machine_kind;
}

void CustomMachineCpu_OnBoot(void)
{
    CODEPATCH_REPLACEFUNC(Machine_CPUGetSwapScore, GetSwapScore);
    CODEPATCH_REPLACEFUNC(Machine_CPUCanSwap, CanSwap);
    CODEPATCH_REPLACEFUNC(Machine_CPUCanBrake, CanBrake);
    CODEPATCH_REPLACEFUNC(Machine_CPUCanChargeHold, CanChargeHold);
    CODEPATCH_REPLACEFUNC(Machine_CPUGetChargeRelease, GetChargeRelease);
    CODEPATCH_REPLACEFUNC(Machine_CPUCanRamCharge, CanRamCharge);
    CODEPATCH_REPLACEFUNC(Machine_CPUSkipsPassageBranch, SkipsPassageBranch);
    CODEPATCH_REPLACEFUNC(Machine_CPUIsKindSlotFilled, IsKindSlotFilled);
    CODEPATCH_REPLACEFUNC(Machine_CPUGetChargeHoldGate, GetChargeHoldGate);
    CODEPATCH_REPLACEFUNC(Machine_CPUGetChargeReleaseOverride, GetChargeReleaseOverride);
    CODEPATCH_REPLACEFUNC(Rider_CPUGetMachineAlignCosNear, AlignCosNear);
    CODEPATCH_REPLACEFUNC(Rider_CPUGetMachineAlignCosFar, AlignCosFar);
    CODEPATCH_REPLACEFUNC(Rider_CPUGetMachineTurnTolerance, TurnTolerance);
    CODEPATCH_REPLACEFUNC(Rider_CPUGetMachineStuckAngle, StuckAngle);
    CODEPATCH_REPLACEFUNC(Rider_CPUGetAirGliderMachineParam, AirGliderParam);
    CODEPATCH_REPLACEFUNC(Rider_CPUGetHighJumpMachineParam, HighJumpParam);
    CODEPATCH_REPLACECALL(0x8026bc6c, CachedKind); // Rider_ProcessCPUDistance

    OSReport("[MachineCpu] CPU machine rows redirected for %d machine(s)\n",
             CustomMachines_GetCount());
}
