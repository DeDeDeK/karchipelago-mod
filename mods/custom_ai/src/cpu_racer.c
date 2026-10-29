#include "game.h"
#include "hsd.h"
#include "machine.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "cpu_ai.h"
#include "cpu_catchup.h"
#include "cpu_racer.h"

#define RACER_SKILL_BONUS 0.25f
#define RACER_ZONE_CHANCE 0.25f

// The CPU rider a CpuData belongs to, NULL if none rides.
static RiderData *CpuRacer_GetRider(CpuData *cpu)
{
    GOBJ *g;
    int i;

    for (i = 0; i < PLY_NUM; i++)
    {
        g = Ply_GetRiderGObj(i);
        if (g && ((RiderData *)g->userdata)->cpu == cpu)
            return g->userdata;
    }
    return NULL;
}

static int CpuRacer_IsRacer(RiderData *rd)
{
    return rd && CpuAI_GetPreset(rd->ply) == CPU_AI_RACER;
}

static int CpuRacer_Level(CpuData *cpu)
{
    int level = cpu->difficulty_level;

    if (level < 0)
        return 0;
    if (level > CPU_LEVEL_MAX)
        return CPU_LEVEL_MAX;
    return level;
}

// Replaces Rider_CPUDifficultyScale (0x80276f00): level / 8, plus a Racer's bonus and the
// Skill catch-up.
static float CpuRacer_DifficultyScale(RiderData *rd)
{
    float scale = (float)CpuRacer_Level(rd->cpu) / CPU_LEVEL_MAX;

    if (CpuRacer_IsRacer(rd))
        scale += RACER_SKILL_BONUS;
    scale += CpuCatchup_SkillBonus(rd);
    if (scale < 0)
        return 0;
    return scale > 1.0f ? 1.0f : scale;
}

// Replaces Rider_CPUInvDifficultyScale (0x80276f7c), which reads the level itself; its only
// caller is Rider_CPUEmitSteerStick's alignment dead zone.
static float CpuRacer_InvDifficultyScale(RiderData *rd)
{
    return 1.0f - CpuRacer_DifficultyScale(rd);
}

// Replaces Rider_CPUGetSteerEnvelope (0x80276650): a Racer steers with level 8's
// sharpest (step, cap), and the Skill catch-up moves the level it reads.
static void CpuRacer_GetSteerEnvelope(CpuData *cpu, int *step, int *cap)
{
    RiderData *rd = CpuRacer_GetRider(cpu);
    int level = CpuRacer_IsRacer(rd) ? CPU_LEVEL_MAX : CpuRacer_Level(cpu);

    if (rd)
        level += (int)(CpuCatchup_SkillBonus(rd) * CPU_LEVEL_MAX);
    if (level < 0)
        level = 0;
    if (level > CPU_LEVEL_MAX)
        level = CPU_LEVEL_MAX;

    *step = stc_cpu_steer_envelope[level].step;
    *cap = stc_cpu_steer_envelope[level].cap;
}

// The pre-GO launch charge vanilla rolls for only above level 3, and a roll every frame at
// the dash zones ahead, so a Racer takes nearly all of them.
void CpuRacer_Maneuver(RiderData *rd)
{
    CpuData *cpu = rd->cpu;

    if (CpuAI_IsUrgent(cpu->maneuver) || !rd->machine_gobj)
        return;

    if (!rd->round_started)
    {
        if (MachineGObj_CPUCanBrake(rd->machine_gobj))
            cpu->maneuver = CPUMAN_LAUNCH_CHARGE;
        return;
    }

    if (cpu->maneuver != cpu->base_maneuver || stc_cpu_forward->num == 0 || HSD_Randf() >= RACER_ZONE_CHANCE)
        return;
    CpuAI_CommitZone(cpu, &stc_cpu_forward->entries[0]);
}

void CpuRacer_InstallHooks(void)
{
    CODEPATCH_REPLACEFUNC(Rider_CPUDifficultyScale, CpuRacer_DifficultyScale);
    CODEPATCH_REPLACEFUNC(Rider_CPUInvDifficultyScale, CpuRacer_InvDifficultyScale);
    CODEPATCH_REPLACEFUNC(Rider_CPUGetSteerEnvelope, CpuRacer_GetSteerEnvelope);
}
