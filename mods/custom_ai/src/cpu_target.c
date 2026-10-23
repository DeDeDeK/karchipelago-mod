#include "game.h"
#include "os.h"
#include "inline.h"

#include "cpu_ai.h"
#include "cpu_target.h"

#define HUNT_SPIN_CHANCE 0.35f
#define FRENZY_CHASE_RANGE 600.0f

int cpu_targeting = CPU_TARGET_DEFAULT;

char *cpu_targeting_names[CPU_TARGET_NUM] = {
    [CPU_TARGET_DEFAULT] = "Default",
    [CPU_TARGET_HUMANS] = "Humans",
    [CPU_TARGET_LEADER] = "Leader",
    [CPU_TARGET_LEAD_HUMAN] = "Lead Human",
};

static int stc_hunt_ply = PLY_NUM;
static int stc_hunt_frames;
// A frenzy raises the CPUs' levels on the next CPU tick, so a call from outside a round
// touches no rider.
static int stc_frenzy_frames;
static u8 stc_frenzy_on;
static s16 stc_frenzy_saved_level[PLY_NUM];

static int CpuTarget_NearestHuman(RiderData *rd)
{
    int best = PLY_NUM;
    float best_dist = 0;
    float dist;
    Vec3 pos;
    int i;

    for (i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN || !Ply_GetRiderGObj(i))
            continue;
        Ply_GetPosition(i, &pos);
        dist = VECSquareDistance(&rd->pos, &pos);
        if (best == PLY_NUM || dist < best_dist)
        {
            best = i;
            best_dist = dist;
        }
    }
    return best;
}

static int CpuTarget_HuntActive(RiderData *rd)
{
    return stc_hunt_frames > 0 && stc_hunt_ply != rd->ply && Ply_GetRiderGObj(stc_hunt_ply);
}

// The rival an active hunt or the targeting setting picks, PLY_NUM to keep vanilla's.
int CpuTarget_PickRival(RiderData *rd)
{
    if (CpuTarget_HuntActive(rd))
        return stc_hunt_ply;

    switch (cpu_targeting)
    {
    case CPU_TARGET_HUMANS:
        return CpuTarget_NearestHuman(rd);
    case CPU_TARGET_LEADER:
        return CpuAI_Leader(rd->ply, 0);
    case CPU_TARGET_LEAD_HUMAN:
        return CpuAI_Leader(rd->ply, 1);
    default:
        return PLY_NUM;
    }
}

// A hunt or frenzy has taken over the CPU's city targeting.
int CpuTarget_IsChasing(RiderData *rd)
{
    return CpuTarget_HuntActive(rd) || stc_frenzy_on;
}

// A hunt sends every city CPU after its target, wherever it is; a frenzy after its rival.
int CpuTarget_Navigate(RiderData *rd)
{
    if (CpuTarget_HuntActive(rd))
        CpuAI_ChaseRival(rd, stc_hunt_ply, CPU_AI_WHOLE_MAP);
    else if (stc_frenzy_on)
        CpuAI_ChaseRival(rd, CpuAI_FindRival(rd), FRENZY_CHASE_RANGE);
    else
        return 0;
    return 1;
}

// Keeps rival_ply on the pick, which Attack and Patrol steer at and the Aggressive, hunt and
// frenzy spins aim at; vanilla only re-picks it every 60-119 frames.
void CpuTarget_Maneuver(RiderData *rd)
{
    int rival;

    // The Dedede fight's rivals are fixed: Dedede for the challengers, none for him.
    if (rd->cpu->city_kind == CITYKIND_VSDEDEDE)
        return;
    rival = CpuTarget_PickRival(rd);

    if (rival != PLY_NUM && rival != rd->ply)
        rd->cpu->rival_ply = rival;

    if (CpuTarget_IsChasing(rd))
        CpuAI_SpinAt(rd, rd->cpu->rival_ply, HUNT_SPIN_CHANCE);
}

static void CpuTarget_SetFrenzyLevels(int on)
{
    RiderData *rd;
    int i;

    for (i = 0; i < PLY_NUM; i++)
    {
        rd = CpuAI_GetCpuRider(i);
        if (!rd)
            continue;
        if (on)
        {
            stc_frenzy_saved_level[i] = rd->cpu->difficulty_level;
            rd->cpu->difficulty_level = CPU_LEVEL_MAX;
        }
        else
        {
            rd->cpu->difficulty_level = stc_frenzy_saved_level[i];
        }
    }
    stc_frenzy_on = on;
}

void CpuTarget_Frenzy(int frames)
{
    if (frames <= 0)
        return;
    stc_frenzy_frames = frames;
    OSReport("[CustomAI] Frenzy started for %d frames\n", frames);
}

void CpuTarget_Hunt(int ply, int frames)
{
    if (ply < 0 || ply >= PLY_NUM)
        return;
    stc_hunt_ply = ply;
    stc_hunt_frames = frames;
    OSReport("[CustomAI] Hunt started on P%d for %d frames\n", ply + 1, frames);
}

int CpuTarget_GetHuntPly(void)
{
    return stc_hunt_frames > 0 ? stc_hunt_ply : PLY_NUM;
}

void CpuTarget_OnTick(void)
{
    if (stc_hunt_frames > 0 && --stc_hunt_frames == 0)
        OSReport("[CustomAI] Hunt ended\n");

    if (stc_frenzy_frames > 0 && !stc_frenzy_on)
        CpuTarget_SetFrenzyLevels(1);
    if (stc_frenzy_on && --stc_frenzy_frames <= 0)
    {
        CpuTarget_SetFrenzyLevels(0);
        OSReport("[CustomAI] Frenzy ended\n");
    }
}

// Neither carries into the next scene, whose CPUs init from scratch.
void CpuTarget_On3DLoadStart(void)
{
    stc_hunt_frames = 0;
    stc_frenzy_frames = 0;
    stc_frenzy_on = 0;
}
