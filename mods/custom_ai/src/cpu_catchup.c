#include "game.h"
#include "machine.h"
#include "scene.h"
#include "os.h"
#include "inline.h"

#include "cpu_sense.h"
#include "cpu_catchup.h"

// Skill catch-up: a CPU this many metres behind the leading human plays this much sharper,
// and one as far ahead this much looser.
#define SKILL_GAP_FULL 150.0f
#define SKILL_BONUS_BEHIND 0.5f
#define SKILL_BONUS_AHEAD -0.25f
#define ADAPTIVE_OFFSET_MAX 4

int cpu_catchup = CPU_CATCHUP_VANILLA;
int cpu_adaptive = 0;

char *cpu_catchup_names[CPU_CATCHUP_NUM] = {
    [CPU_CATCHUP_OFF] = "Off",
    [CPU_CATCHUP_VANILLA] = "Default",
    [CPU_CATCHUP_STRONG] = "Strong",
    [CPU_CATCHUP_SKILL] = "Skill",
};

static const float stc_catchup_mult[CPU_CATCHUP_NUM] = {
    [CPU_CATCHUP_OFF] = 0.0f,
    [CPU_CATCHUP_VANILLA] = 1.0f,
    [CPU_CATCHUP_STRONG] = 2.0f,
    [CPU_CATCHUP_SKILL] = 0.0f,
};

// Speed Help's CPU-only tables, as shipped.
static float stc_vanilla_catchup_scale[CPU_LEVEL_MAX + 1];
static float stc_vanilla_speed_bonus[CPU_LEVEL_MAX + 1];
static float stc_vanilla_mid_race_bonus[CPU_LEVEL_MAX + 1];
static int stc_vanilla_saved;

// Added to every CPU's level at init: the adaptive offset moves after each race or stadium
// a human finishes, the base offset is set through the API.
static int stc_level_offset;
static int stc_base_offset;

int CpuCatchup_GetLevelOffset(void)
{
    return stc_base_offset + (cpu_adaptive ? stc_level_offset : 0);
}

void CpuCatchup_SetBaseOffset(int offset)
{
    stc_base_offset = offset;
    OSReport("[CustomAI] Base CPU level offset set to %d\n", offset);
}

void CpuCatchup_AdjustLevel(RiderData *rd)
{
    int offset = CpuCatchup_GetLevelOffset();
    int level;

    if (offset == 0)
        return;
    level = rd->cpu->difficulty_level + offset;
    if (level < 0)
        level = 0;
    if (level > CPU_LEVEL_MAX)
        level = CPU_LEVEL_MAX;
    rd->cpu->difficulty_level = level;
}

// Added to a CPU's 0-1 skill under the Skill catch-up, by its gap to the leading human
// along the course.
float CpuCatchup_SkillBonus(RiderData *rd)
{
    const CpuRiderView *v;
    float best = 0;
    float gap;
    int found = 0;
    int i;

    if (cpu_catchup != CPU_CATCHUP_SKILL || Scene_GetCurrentMajor() != MJRKIND_AIR)
        return 0;
    for (i = 0; i < PLY_NUM; i++)
    {
        v = CpuSense_Rider(i);
        if (!v->present || !v->human)
            continue;
        if (!found || v->progress > best)
            best = v->progress;
        found = 1;
    }
    if (!found)
        return 0;

    gap = (best - CpuSense_Rider(rd->ply)->progress) / SKILL_GAP_FULL;
    if (gap > 1.0f)
        gap = 1.0f;
    if (gap < -1.0f)
        gap = -1.0f;
    return gap * (gap > 0 ? SKILL_BONUS_BEHIND : -SKILL_BONUS_AHEAD);
}

// AirRide_ApplySpeedHelp adds three per-level terms only CPUs get: catch-up toward the best
// human (cpu_catchup_scale), a flat bonus and a mid-race bonus. Nothing else reads the three
// tables, so scaling them from the shipped values scales exactly those terms.
void CpuCatchup_On3DLoadEnd(void)
{
    gmGameParams *gp = (*stc_gmdataall)->game_params;
    float mult = Gm_IsAutoDemo() ? 1.0f : stc_catchup_mult[cpu_catchup];
    int i;

    if (!stc_vanilla_saved)
    {
        memcpy(stc_vanilla_catchup_scale, gp->cpu_catchup_scale, sizeof(stc_vanilla_catchup_scale));
        memcpy(stc_vanilla_speed_bonus, gp->cpu_speed_bonus, sizeof(stc_vanilla_speed_bonus));
        memcpy(stc_vanilla_mid_race_bonus, gp->cpu_mid_race_bonus, sizeof(stc_vanilla_mid_race_bonus));
        stc_vanilla_saved = 1;
    }

    for (i = 0; i <= CPU_LEVEL_MAX; i++)
    {
        gp->cpu_catchup_scale[i] = stc_vanilla_catchup_scale[i] * mult;
        gp->cpu_speed_bonus[i] = stc_vanilla_speed_bonus[i] * mult;
        gp->cpu_mid_race_bonus[i] = stc_vanilla_mid_race_bonus[i] * mult;
    }
}

static int CpuCatchup_HasCpu(void)
{
    int i;

    for (i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) == PKIND_CPU)
            return 1;
    }
    return 0;
}

// After a race or a stadium against CPUs: the best-placed human winning raises the offset,
// finishing last lowers it.
void CpuCatchup_On3DExit(void)
{
    GameData *gd = Gm_GetGameData();
    int major = Scene_GetCurrentMajor();
    int best = PLY_NUM;
    int field = 0;
    int i;

    if (!cpu_adaptive || !CpuCatchup_HasCpu())
        return;
    if (major != MJRKIND_AIR && !(major == MJRKIND_CITY && CityTrial_IsInStadium()))
        return;

    for (i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) == PKIND_NONE)
            continue;
        field++;
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        if (major == MJRKIND_AIR && !gd->player_finished_flag[i])
            continue;
        if (gd->player_rank[i] < best)
            best = gd->player_rank[i];
    }
    if (best == PLY_NUM)
        return;

    if (best == 0 && stc_level_offset < ADAPTIVE_OFFSET_MAX)
        stc_level_offset++;
    else if (best == field - 1 && stc_level_offset > -ADAPTIVE_OFFSET_MAX)
        stc_level_offset--;
    else
        return;

    OSReport("[CustomAI] Adaptive CPU level offset now %d\n", stc_level_offset);
}
