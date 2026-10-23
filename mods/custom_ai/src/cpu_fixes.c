#include "game.h"
#include "rider.h"
#include "machine.h"
#include "hsd.h"
#include "os.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "cpu_ai.h"
#include "cpu_sense.h"
#include "cpu_stadium.h"
#include "cpu_fixes.h"

// Flight pitch: full stick at this error, in radians.
#define CPU_FIX_PITCH_FULL 0.35f

// Bomb: thrown only at a rider ahead, held longer the further it is (the throw charge counts
// held frames up to 60).
#define CPU_FIX_BOMB_RANGE 120.0f
#define CPU_FIX_BOMB_CONE 0.8f
#define CPU_FIX_BOMB_HOLD_PER_UNIT 0.5f
#define CPU_FIX_BOMB_HOLD_MAX 60

// Sword, Dedede and Meta Knight swing on their own at anyone within a few units, so they only
// need to get close.
#define CPU_FIX_MELEE_RANGE 120.0f

// ApproachWaypoint sends a city CPU to a Recovery Zone under this HP ratio (vanilla 0.9).
#define CPU_FIX_HEAL_RATIO 0.6f

// Avoidance: the steering bend covers the hazard's angular size, capped at this tangent.
#define CPU_FIX_AVOID_TAN_MAX 2.0f

// A charge release level this high (Rocket Star's 0.99) marks a machine that needs a full
// charge to move.
#define CPU_FIX_FULL_RELEASE 0.95f

// A latched stall ends once the CPU moves this fast again, or its charge pump has run its
// tap, hold 20, hold 40 cycle.
#define CPU_FIX_UNSTUCK_SPEED 0.3f
#define CPU_FIX_STUTTER_STEPS 3

// Rider_CPURivalSelect re-picks every 60-119 frames, or after 30 with no rival.
#define RIVAL_RESELECT_MIN 60
#define RIVAL_RESELECT_RAND 60
#define RIVAL_NONE_FRAMES 30
#define RIVAL_WEAK_RATIO 0.5f
// The weak bonus is 5 while Gm_GetDededeHPMargin is under this, outside Patrol.
#define RIVAL_DEDEDE_MARGIN -0.15f

int cpu_fixes = 1;

// Instruction-level fixes, swapped in and out with the setting.
typedef struct CpuFixPatch
{
    int addr;
    int fixed;
} CpuFixPatch;

static const CpuFixPatch stc_patches[] = {
    // Rider_CPUScanItems (0x80263c4c) gives its +5 to items outside the 45-degree cone ahead.
    {0x80263de8, 0x41800008}, // blt 0x80263df0
    // Rider_CPUScanCityObjects (0x802638a4) takes 10 off machines 100-500 away instead of
    // past 500.
    {0x80263a44, 0x41800008}, // blt 0x80263a4c
    // Rider_CPUEmitAbilityAction's (0x80273d1c) Fire target search compares against FLT_MAX
    // backwards.
    {0x80274104, 0xfc00e840}, // fcmpo f0,f29
    // Rider_CPUArbitrateManeuver's (0x80274ec0) can-counter list names Needle and Tire, which
    // have no counter, instead of Plasma and Tornado, which do.
    {0x802751d8, 0x2c000005}, // cmpwi r0,COPYKIND_PLASMA
    {0x802751e0, 0x2c000009}, // cmpwi r0,COPYKIND_TORNADO
    // Rider_CPUResolveAvoidVector (0x8026d1fc) takes the first listed hazard and bends around
    // it only while it is 10 or more frames away; the list is sorted, so the first eligible
    // one wins.
    {0x8026d314, 0x48000020}, // b 0x8026d334
    {0x8026d34c, PPC_NOP},
    // Rider_CPUCollectHazards' (0x80269928) weapon gate, whose pass runs first.
    {0x80269bc8, PPC_NOP},
};

static int stc_patch_vanilla[GetElementsIn(stc_patches)];
static u8 stc_stuck_latch[PLY_NUM];
static float stc_heal_vanilla;
static int stc_saved;

void CpuFixes_Apply(void)
{
    int i;

    if (!stc_saved)
    {
        for (i = 0; i < GetElementsIn(stc_patches); i++)
            stc_patch_vanilla[i] = *(int *)stc_patches[i].addr;
        stc_heal_vanilla = *stc_cpu_heal_hp_ratio;
        stc_saved = 1;
    }

    for (i = 0; i < GetElementsIn(stc_patches); i++)
        CODEPATCH_REPLACEINSTRUCTION(stc_patches[i].addr, cpu_fixes ? stc_patches[i].fixed : stc_patch_vanilla[i]);
    *stc_cpu_heal_hp_ratio = cpu_fixes ? CPU_FIX_HEAL_RATIO : stc_heal_vanilla;
}

// Rider_CPUEmitFlySteer's pitch output: vanilla compares its error against a dead zone of
// +/-57.3 that a radian error never leaves, and builds the error from the target pitch in
// radians minus forward.y, a sine. Proportional, both sides in radians; stick down pitches
// up. The flight stadiums' fixes need it even with the fixes off.
static int CpuFixes_FlyPitch(RiderData *rd, float target, float forward_y, int cap)
{
    int state = rd->cpu->ai_state;
    float err;
    int stick;

    if (!cpu_fixes && !(cpu_stadium && (state == CPUSTATE_REPOSITION || state == CPUSTATE_CHARGE)))
        return 0;
    target = CpuStadium_TargetPitch(rd, target);
    err = target - CpuAI_Elevation(forward_y);
    stick = (int)(-(float)cap * err / CPU_FIX_PITCH_FULL);
    if (stick > cap)
        stick = cap;
    if (stick < -cap)
        stick = -cap;
    Rider_CPUPushCmd2(rd, CPUCMD_STICK_Y, stick);
    return 1;
}

// Rider_CPUResolveAvoidVector bends by atan2(|dir|, radius) on a normalized dir, so larger
// hazards get smaller bends. Returns the first atan2 operand, the radius being the second:
// the bend becomes the hazard's angular size from the rider.
static float CpuFixes_AvoidSpread(CpuHazard *h, float vanilla)
{
    float dist, ratio;

    if (!cpu_fixes || h->radius <= 0)
        return vanilla;
    dist = VECMag(&h->rel_pos);
    ratio = dist > h->radius / CPU_FIX_AVOID_TAN_MAX ? h->radius / dist : CPU_FIX_AVOID_TAN_MAX;
    return h->radius * ratio;
}

static int CpuFixes_RivalScore(RiderData *rd, int ply, int weak_bonus)
{
    GOBJ *g = Ply_GetRiderGObj(ply);
    RiderData *other;
    Vec3 pos;
    float dist, hp, max_hp;
    int copy;
    int score = 0;

    if (rd->cpu->city_kind == CITYKIND_VSDEDEDE)
        return ply == VSDEDEDE_DEDEDE_PLY;
    if (!g || Ply_IsOut(ply) || ply == rd->ply)
        return 0;

    other = g->userdata;
    Ply_GetPosition(ply, &pos);
    dist = VECSquareDistance(&rd->pos, &pos);
    if (dist < 50.0f * 50.0f)
        score = 3;
    else if (dist < 100.0f * 100.0f)
        score = 2;
    else if (dist < 200.0f * 200.0f)
        score = 1;

    copy = other->copy_kind;
    if (copy != COPYKIND_SWORD && copy != COPYKIND_NEEDLE && copy != COPYKIND_TIRE && copy != COPYKIND_ICE)
        score += 2;
    if (copy == COPYKIND_NONE)
        score += 1;
    if (!other->cpu)
        score += 2;

    if (Gm_IsDamageEnabled())
    {
        hp = Ply_GetHP(ply);
        max_hp = Ply_GetMaxHP(ply);
        // Vanilla divides the two as integers, so any rider below full HP counts as weak.
        if (cpu_fixes ? (max_hp > 0 && hp / max_hp < RIVAL_WEAK_RATIO)
                      : ((int)max_hp != 0 && (float)((int)hp / (int)max_hp) < RIVAL_WEAK_RATIO))
            score += weak_bonus;
    }
    return score;
}

// Replaces Rider_CPURivalSelect (0x80264210). Vanilla starts its pick at player 1 and keeps
// it when nobody outscores it, even with a score of 0, so a CPU with no rival in reach
// targets player 1 wherever (or whether) it is.
static void CpuFixes_RivalSelect(RiderData *rd)
{
    CpuData *cpu = rd->cpu;
    int score[PLY_NUM];
    int weak_bonus = 1;
    int best, i;

    if (cpu->rival_ply != PLY_NUM && cpu->rival_reselect_timer > 0)
    {
        cpu->rival_reselect_timer--;
        return;
    }

    if (cpu->ai_state != CPUSTATE_PATROL && Gm_GetDededeHPMargin() < RIVAL_DEDEDE_MARGIN)
        weak_bonus = 5;
    for (i = 0; i < PLY_NUM; i++)
        score[i] = CpuFixes_RivalScore(rd, i, weak_bonus);

    best = cpu_fixes ? PLY_NUM : 0;
    for (i = cpu_fixes ? 0 : 1; i < PLY_NUM; i++)
    {
        if (score[i] > 0 && (best == PLY_NUM || score[best] < score[i]))
            best = i;
    }

    if (best == PLY_NUM || best == rd->ply)
    {
        cpu->rival_ply = PLY_NUM;
        cpu->rival_reselect_timer = RIVAL_NONE_FRAMES;
    }
    else
    {
        cpu->rival_ply = best;
        cpu->rival_reselect_timer = HSD_Randi(RIVAL_RESELECT_RAND) + RIVAL_RESELECT_MIN;
    }
}

// Rider_UpdateCPU's (0x8026beec) Rider_CPUDecideState call (0x8026bf08), after perceive. The
// velocity detector raises CPUSTATUS_VEL_STUCK for one frame and restarts its count, and
// Rider_CPUEmitChargeStutter only sees it when the command player happens to be idle that
// frame, so a stalled CPU's charge pump rarely runs. The bit is held until the CPU moves.
static void CpuFixes_Decide(RiderData *rd)
{
    CpuData *cpu = rd->cpu;
    u8 *latch = &stc_stuck_latch[rd->ply];

    if (cpu_fixes)
    {
        if (cpu->status_flags & CPUSTATUS_VEL_STUCK)
        {
            *latch = 1;
        }
        else if (*latch && CpuSense_Self(rd->ply)->speed > CPU_FIX_UNSTUCK_SPEED)
        {
            *latch = 0;
            cpu->maneuver_step = 0;
        }
        if (*latch)
            cpu->status_flags |= CPUSTATUS_VEL_STUCK;
    }
    Rider_CPUDecideState(rd);
}

// The maneuvers' Rider_CPUEmitChargeStutter calls: a full pump cycle releases the latch.
static int CpuFixes_ChargeStutter(RiderData *rd)
{
    int emitted = Rider_CPUEmitChargeStutter(rd);

    if (emitted && rd->cpu->maneuver_step >= CPU_FIX_STUTTER_STEPS)
        stc_stuck_latch[rd->ply] = 0;
    return emitted;
}

void CpuFixes_ResetRider(int ply)
{
    stc_stuck_latch[ply] = 0;
}

// Rider_CPUManeuverChargeHold's profile test: once aligned, only a race CPU holds the gauge
// to the machine's release level, and every other profile lets go at once. Drag Race shares
// the race handler, and Rocket Star only moves on a released charge, so they hold too.
// Returns 1 to take the hold-to-release path.
static int CpuFixes_HoldToRelease(RiderData *rd)
{
    int state = rd->cpu->ai_state;

    if (!cpu_fixes || !rd->machine_gobj)
        return 0;
    return state == CPUSTATE_DRAG_RACE || state > CPUSTATE_PATROL ||
           MachineGObj_CPUGetChargeRelease(rd->machine_gobj) >= CPU_FIX_FULL_RELEASE;
}

static int CpuFixes_IsMelee(RiderData *rd)
{
    return rd->kind == RDKIND_DEDEDE || rd->kind == RDKIND_METAKNIGHT || rd->copy_kind == COPYKIND_SWORD;
}

// The nearest other rider within range ahead of rd (dot of the unit direction with the
// facing at least cone), or PLY_NUM.
static int CpuFixes_NearestAhead(RiderData *rd, float range, float cone, float *dist_out)
{
    int best = PLY_NUM;
    float best_dist = range * range;
    float dist;
    Vec3 pos, to;
    int i;

    for (i = 0; i < PLY_NUM; i++)
    {
        if (i == rd->ply || !Ply_GetRiderGObj(i))
            continue;
        Ply_GetPosition(i, &pos);
        dist = VECSquareDistance(&rd->pos, &pos);
        if (dist >= best_dist)
            continue;
        VECSubtract(&pos, &rd->pos, &to);
        VECNormalize(&to, &to);
        if (VECDotProduct(&to, &rd->forward) < cone)
            continue;
        best = i;
        best_dist = dist;
    }
    *dist_out = sqrtf(best_dist);
    return best;
}

// Riders whose attacks are automatic close in on anyone nearby.
void CpuFixes_MeleeChase(RiderData *rd)
{
    float dist;
    int target;

    if (!CpuFixes_IsMelee(rd))
        return;
    target = CpuFixes_NearestAhead(rd, CPU_FIX_MELEE_RANGE, -1.0f, &dist);
    if (target != PLY_NUM)
        CpuAI_ChaseRival(rd, target, CPU_FIX_MELEE_RANGE);
}

// The copy wheel stops on a press; vanilla presses at random and often lands on Sleep.
static void CpuFixes_WheelStop(RiderData *rd)
{
    int *list = rd->copy_wheel_ability_list;
    int index = rd->copy_wheel_index;

    if (list && index >= 0 && list[index] != COPYKIND_SLEEP)
        Rider_CPUPushCmd1(rd, CPUCMD_PRESS);
}

static void CpuFixes_Bomb(RiderData *rd)
{
    float dist;
    int hold;

    if (CpuFixes_NearestAhead(rd, CPU_FIX_BOMB_RANGE, CPU_FIX_BOMB_CONE, &dist) == PLY_NUM)
        return;
    hold = (int)(dist * CPU_FIX_BOMB_HOLD_PER_UNIT);
    if (hold < 1)
        hold = 1;
    if (hold > CPU_FIX_BOMB_HOLD_MAX)
        hold = CPU_FIX_BOMB_HOLD_MAX;
    Rider_CPUPushCmd2(rd, CPUCMD_PRESS_HOLD, hold);
}

// Rider_ProcessCPUManeuver's (0x8026bf30) Rider_CPUEmitAbilityAction call (0x8026c07c), run
// once per emitted script.
static void CpuFixes_EmitAbilityAction(RiderData *rd)
{
    if (cpu_fixes && Ply_GetPKind(rd->ply) == PKIND_CPU)
    {
        if (rd->kind == RDKIND_KIRBY && (rd->xa30 & RIDER_XA30_COPY_WHEEL))
        {
            CpuFixes_WheelStop(rd);
            return;
        }
        if (rd->copy_kind == COPYKIND_BOMB)
        {
            CpuFixes_Bomb(rd);
            return;
        }
    }
    Rider_CPUEmitAbilityAction(rd);
}

// Rider_CPUEmitFlySteer (0x8026f0e4) `fsubs f1,f29,f30`: f29 is the target pitch, f30
// forward.y, r28 the RiderData and the steer envelope's cap at sp+8. Returns past the pitch
// output.
CODEPATCH_HOOKCONDITIONALCREATE(0x8026fb38,
    "mr 3, 28\n\t"
    "fmr 1, 29\n\t"
    "fmr 2, 30\n\t"
    "lwz 4, 8(1)\n\t",
    CpuFixes_FlyPitch,
    "",
    0,
    0x8026fb98)

// Rider_CPUResolveAvoidVector (0x8026d1fc) `lfs f2,52(r29)` before its atan2: r29 is the
// chosen CpuHazard and f1 the first operand; nothing else volatile is live.
CODEPATCH_HOOKCREATE(0x8026d3b0,
    "mr 3, 29\n\t",
    CpuFixes_AvoidSpread,
    "",
    0)

static const int stc_stutter_calls[] = {
    0x8026ddf4, // Rider_CPUManeuverRecoverForward (0x8026dd4c)
    0x8026df10, // Rider_CPUManeuverSteerToNav (0x8026de68)
};

// Rider_CPUManeuverChargeHold (0x8026ed4c) `lwz r0,8(r31)`: r30 is the RiderData, r31 its
// CpuData, and the paths after load what they need. Returns to the hold-to-release check
// (0x8026edec).
CODEPATCH_HOOKCONDITIONALCREATE(0x8026ede0,
    "mr 3, 30\n\t",
    CpuFixes_HoldToRelease,
    "",
    0,
    0x8026edec)

void CpuFixes_InstallHooks(void)
{
    int i;

    CODEPATCH_HOOKAPPLY(0x8026fb38);
    CODEPATCH_HOOKAPPLY(0x8026d3b0);
    CODEPATCH_HOOKAPPLY(0x8026ede0);
    CODEPATCH_REPLACECALL(0x8026c07c, CpuFixes_EmitAbilityAction);
    CODEPATCH_REPLACEFUNC(Rider_CPURivalSelect, CpuFixes_RivalSelect);
    CODEPATCH_REPLACECALL(0x8026bf08, CpuFixes_Decide);
    for (i = 0; i < GetElementsIn(stc_stutter_calls); i++)
        CODEPATCH_REPLACECALL(stc_stutter_calls[i], CpuFixes_ChargeStutter);
}
