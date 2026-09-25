#include "game.h"
#include "hsd.h"
#include "item.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "cpu_ai.h"

// Skill-scaled values run from the first number at CPU level 0 to the sum at 8.
#define CPU_AI_CHASE_RANGE_MIN 150.0f
#define CPU_AI_CHASE_RANGE_SKILL 450.0f
#define CPU_AI_RAM_CHANCE_MIN 0.02f
#define CPU_AI_RAM_CHANCE_SKILL 0.2f
#define CPU_AI_DODGE_CHANCE_MIN 0.05f
#define CPU_AI_DODGE_CHANCE_SKILL 0.3f
#define CPU_AI_HOARD_RESCAN_MIN 300
#define CPU_AI_HOARD_RESCAN_SKILL -270
#define CPU_AI_HOARD_SCORE 100
#define CPU_AI_HOARD_SCAN_RADIUS 100000.0f // Rider_CPUDecideNavigate's own whole-map radius

int cpu_ai_menu_ct = CPU_AI_DEFAULT;
int cpu_ai_menu_ar = CPU_AI_DEFAULT;

char *cpu_ai_menu_ct_names[CPU_AI_NUM + 1] = {
    [CPU_AI_DEFAULT] = "Default",
    [CPU_AI_CAUTIOUS] = "Cautious",
    [CPU_AI_RECKLESS] = "Reckless",
    [CPU_AI_AGGRESSIVE] = "Aggressive",
    [CPU_AI_HOARDER] = "Hoarder",
    [CPU_AI_NUM] = "Random",
};

char *cpu_ai_menu_ar_names[CPU_AI_AR_NUM + 1] = {
    [CPU_AI_DEFAULT] = "Default",
    [CPU_AI_CAUTIOUS] = "Cautious",
    [CPU_AI_RECKLESS] = "Reckless",
    [CPU_AI_AR_NUM] = "Random",
};

// Indexed by RiderData.ply, written at every Rider_CPUInit.
static u8 stc_rider_preset[5];

static int CpuAI_Resolve(int selection, int preset_num)
{
    if (selection == preset_num)
        return CPU_AI_DEFAULT + 1 + HSD_Randi(preset_num - 1);
    return selection;
}

static float CpuAI_Skill(RiderData *rd, float min, float skill)
{
    return min + skill * Rider_CPUDifficultyScale(rd);
}

// Runs once per rider at init, so Random rolls per rider and a menu change
// applies from the next spawn.
void CpuAI_AssignPreset(RiderData *rd)
{
    int preset = CPU_AI_DEFAULT;

    // Game_Think gives a human slot an autopilot CpuData before marking it CPU.
    // Stadium events keep vanilla behavior.
    if (Ply_GetPKind(rd->ply) == PKIND_CPU)
    {
        switch (Scene_GetCurrentMajor())
        {
        case MJRKIND_AIR:
            preset = CpuAI_Resolve(cpu_ai_menu_ar, CPU_AI_AR_NUM);
            break;
        case MJRKIND_CITY:
            if (!CityTrial_IsInStadium())
                preset = CpuAI_Resolve(cpu_ai_menu_ct, CPU_AI_NUM);
            break;
        default:
            break;
        }
    }

    stc_rider_preset[rd->ply] = preset;
}

// Aggressive: chase the rival Rider_CPURivalSelect picks while it is in range,
// ahead of any item or machine target.
void CpuAI_NavigateTarget(RiderData *rd)
{
    CpuData *cpu = rd->cpu;
    Vec3 pos;
    float range;

    if (stc_rider_preset[rd->ply] != CPU_AI_AGGRESSIVE)
        return;

    Rider_CPURivalSelect(rd);
    if (cpu->rival_player_idx == 5)
        return;

    Ply_GetPosition(cpu->rival_player_idx, &pos);
    range = CpuAI_Skill(rd, CPU_AI_CHASE_RANGE_MIN, CPU_AI_CHASE_RANGE_SKILL);
    if (VECSquareDistance(&rd->pos, &pos) < range * range)
        cpu->nav_target_pos = pos;
}

// Hoarder: patches, boxes, timed boosts and legendary parts outrank abilities,
// weapons and candy, which it ignores. Food keeps its HP-scaled vanilla score.
int CpuAI_GetItemScore(RiderData *rd, int kind)
{
    int score = Rider_CPUGetItemScore(rd, kind);

    if (stc_rider_preset[rd->ply] != CPU_AI_HOARDER || score < 0)
        return score;

    if (kind <= ITKIND_CHARGENONE || (kind >= ITKIND_HYDRA1 && kind <= ITKIND_DRAGOON3))
        return CPU_AI_HOARD_SCORE;
    if (kind >= ITKIND_FOODMAXIMTOMATO && kind <= ITKIND_FOODAPPLE)
        return score;
    return 0;
}

// Navigate keeps an item target until it is gone. A Hoarder also rescans the
// whole city on a skill-scaled interval, trading a far target for a closer one.
GOBJ *CpuAI_RevalidateItem(RiderData *rd, Vec3 *center, Vec3 *facing, float radius)
{
    GOBJ *item;
    int interval;

    if (stc_rider_preset[rd->ply] == CPU_AI_HOARDER)
    {
        interval = (int)CpuAI_Skill(rd, CPU_AI_HOARD_RESCAN_MIN, CPU_AI_HOARD_RESCAN_SKILL);
        if (rd->cpu->frame_counter % interval == 0)
        {
            item = Rider_CPUScanItems(rd, &rd->pos, NULL, CPU_AI_HOARD_SCAN_RADIUS);
            if (item)
                return item;
        }
    }

    return Rider_CPUScanItems(rd, center, facing, radius);
}

// The cascade commits these ahead of its hazard Wiggle, so no preset replaces them.
static int CpuAI_IsUrgent(int maneuver)
{
    switch (maneuver)
    {
    case CPUMAN_COAST:
    case CPUMAN_AVOID_OBSTACLE:
    case CPUMAN_NAV_STEER:
    case CPUMAN_NAV_STEER_TAP:
    case CPUMAN_CHARGED_NAV_STEER:
    case CPUMAN_TAP_ONCE:
    case CPUMAN_WIGGLE:
        return 1;
    default:
        return 0;
    }
}

static void CpuAI_Veto(CpuData *cpu, int a, int b)
{
    if (cpu->maneuver == a || cpu->maneuver == b)
        cpu->maneuver = cpu->base_maneuver;
}

// A second roll at the Wiggle the cascade commits for an imminent hazard.
static void CpuAI_Dodge(RiderData *rd)
{
    CpuData *cpu = rd->cpu;
    CpuHazard *h;
    int i;

    if (CpuAI_IsUrgent(cpu->maneuver) || (cpu->desire_flags & CPUDESIRE_NO_DODGE))
        return;

    for (i = 0; i < stc_cpu_hazards->num; i++)
    {
        h = &stc_cpu_hazards->entries[i];
        if ((h->x04 || h->x08 || h->x0c) && h->imminent)
        {
            if (HSD_Randf() < CpuAI_Skill(rd, CPU_AI_DODGE_CHANCE_MIN, CPU_AI_DODGE_CHANCE_SKILL))
                cpu->maneuver = CPUMAN_WIGGLE;
            return;
        }
    }
}

// A second roll at RamCharge on the nearest rammable rider ahead, taken only
// over cruising, pursuing or charging.
static void CpuAI_Ram(RiderData *rd)
{
    CpuData *cpu = rd->cpu;
    CpuForwardTarget *t;
    CpuForwardTarget *best = NULL;
    float dist;
    float best_dist = 0;
    int i;

    if (cpu->maneuver != cpu->base_maneuver && cpu->maneuver != CPUMAN_PURSUE_LOS &&
        cpu->maneuver != CPUMAN_CHARGE_HOLD)
        return;
    if (HSD_Randf() >= CpuAI_Skill(rd, CPU_AI_RAM_CHANCE_MIN, CPU_AI_RAM_CHANCE_SKILL))
        return;

    for (i = 0; i < stc_cpu_forward->num; i++)
    {
        t = &stc_cpu_forward->entries[i];
        if (t->side != 1)
            continue;
        dist = VECSquareDistance(&rd->pos, &t->pos);
        if (!best || dist < best_dist)
        {
            best = t;
            best_dist = dist;
        }
    }
    if (!best)
        return;

    cpu->xd0 = best->x00;
    cpu->ramcharge_target_pos = best->pos;
    cpu->maneuver = CPUMAN_RAM_CHARGE;
}

void CpuAI_AdjustManeuver(RiderData *rd)
{
    CpuData *cpu = rd->cpu;

    switch (stc_rider_preset[rd->ply])
    {
    case CPU_AI_CAUTIOUS:
        CpuAI_Veto(cpu, CPUMAN_RAM_CHARGE, CPUMAN_PURSUE_LOS);
        CpuAI_Dodge(rd);
        break;
    case CPU_AI_RECKLESS:
        CpuAI_Veto(cpu, CPUMAN_DODGE_PROJECTILE, CPUMAN_WIGGLE);
        CpuAI_Ram(rd);
        break;
    case CPU_AI_AGGRESSIVE:
        CpuAI_Ram(rd);
        break;
    case CPU_AI_HOARDER:
        CpuAI_Veto(cpu, CPUMAN_RAM_CHARGE, CPUMAN_PURSUE_LOS);
        break;
    }
}

// Rider_CPUInit (0x80262d6c) epilogue `lwz r0,36(r1)`: every CpuData field is
// written and r31 still holds the RiderData*.
CODEPATCH_HOOKCREATE(0x80262fbc,
    "mr 3, 31\n\t",
    CpuAI_AssignPreset,
    "",
    0)

// Rider_CPUDecideNavigate (0x80271eb4) `lwz r3,184(r29)`: nav_target_pos is
// picked and about to become the steer target the route planner follows. r28
// holds the RiderData*.
CODEPATCH_HOOKCREATE(0x80272258,
    "mr 3, 28\n\t",
    CpuAI_NavigateTarget,
    "",
    0)

// Rider_CPUArbitrateManeuver (0x80274ec0) exit `psq_l f31,120(r1)`: the maneuver
// is committed and r31 still holds the RiderData*.
CODEPATCH_HOOKCREATE(0x80275bc4,
    "mr 3, 31\n\t",
    CpuAI_AdjustManeuver,
    "",
    0)

void CpuAI_InstallHooks(void)
{
    CODEPATCH_HOOKAPPLY(0x80262fbc);
    CODEPATCH_HOOKAPPLY(0x80272258);
    CODEPATCH_HOOKAPPLY(0x80275bc4);
    // Rider_CPUScanItems (0x80263c4c), the per-item score.
    CODEPATCH_REPLACECALL(0x80263d00, CpuAI_GetItemScore);
    // Rider_CPUDecideNavigate (0x80271eb4), the rescan of its current item_target.
    CODEPATCH_REPLACECALL(0x80272144, CpuAI_RevalidateItem);
}
