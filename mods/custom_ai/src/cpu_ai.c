#include "game.h"
#include "hsd.h"
#include "item.h"
#include "machine.h"
#include "stage.h"
#include "enemy.h"
#include "os.h"
#include "inline.h"
#include "hoshi/mod.h"
#include "code_patch/code_patch.h"
#include "custom_items_api.h"

#include "cpu_ai.h"
#include "cpu_hazards.h"
#include "cpu_sense.h"
#include "cpu_team.h"
#include "cpu_line.h"
#include "cpu_glide.h"
#include "ai_overlay.h"
#include "cpu_strategist.h"
#include "cpu_target.h"
#include "cpu_racer.h"
#include "cpu_fixes.h"
#include "cpu_stadium.h"
#include "cpu_catchup.h"

// Skill-scaled values run from the first number at CPU level 0 to the sum at 8.
#define CPU_AI_CHASE_RANGE_MIN 150.0f
#define CPU_AI_CHASE_RANGE_SKILL 450.0f
#define CPU_AI_ZONE_CHANCE_MIN 0.02f
#define CPU_AI_ZONE_CHANCE_SKILL 0.2f
#define CPU_AI_DODGE_CHANCE_MIN 0.05f
#define CPU_AI_DODGE_CHANCE_SKILL 0.3f
#define CPU_AI_SPIN_CHANCE_MIN 0.05f
#define CPU_AI_SPIN_CHANCE_SKILL 0.25f
#define CPU_AI_SPIN_RANGE 25.0f
#define CPU_AI_HOARD_RESCAN_MIN 300
#define CPU_AI_HOARD_RESCAN_SKILL -270
#define CPU_AI_HOARD_SCORE 100
// Chases aim where the rival will be, up to this many frames ahead.
#define CPU_AI_LEAD_FRAMES_MAX 30.0f
#define CPU_AI_LEAD_SPEED_MIN 0.5f
// Cautious keeps this far from a rider it cannot hurt (Candy, invincible).
#define CPU_AI_KEEP_CLEAR_RANGE 150.0f
#define CPU_AI_KEEP_CLEAR_DIST 300.0f
// A custom item granting stats scores as a vanilla patch does.
#define CPU_AI_CUSTOM_PATCH_SCORE 50

// A goal counts as reachable when one of its range's lines passes this close.
#define CPU_AI_REACH_DY 20.0f
#define CPU_AI_REACH_DXZ 60.0f
#define CPU_AI_REACH_CACHE_NUM 256
#define CPU_AI_REACH_CACHE_FRAMES 120

int cpu_ai_menu_ct = 0;
int cpu_ai_menu_ar = 0;

static const u8 stc_ct_presets[CPU_AI_CT_NUM] = {
    CPU_AI_DEFAULT, CPU_AI_CAUTIOUS, CPU_AI_RECKLESS, CPU_AI_AGGRESSIVE, CPU_AI_HOARDER, CPU_AI_STRATEGIST,
};

static const u8 stc_ar_presets[CPU_AI_AR_NUM] = {
    CPU_AI_DEFAULT, CPU_AI_CAUTIOUS, CPU_AI_RECKLESS, CPU_AI_RACER,
};

char *cpu_ai_menu_ct_names[CPU_AI_CT_NUM + 1] = {
    "Default", "Cautious", "Reckless", "Aggressive", "Hoarder", "Strategist", "Random",
};

char *cpu_ai_menu_ar_names[CPU_AI_AR_NUM + 1] = {
    "Default", "Cautious", "Reckless", "Racer", "Random",
};

static const char *stc_preset_names[CPU_AI_NUM] = {
    [CPU_AI_DEFAULT] = "Default",
    [CPU_AI_CAUTIOUS] = "Cautious",
    [CPU_AI_RECKLESS] = "Reckless",
    [CPU_AI_AGGRESSIVE] = "Aggressive",
    [CPU_AI_HOARDER] = "Hoarder",
    [CPU_AI_STRATEGIST] = "Strategist",
    [CPU_AI_RACER] = "Racer",
};

// Indexed by RiderData.ply, written at every Rider_CPUInit.
static u8 stc_rider_preset[PLY_NUM];
// The rider an Aggressive CPU's Navigate chased this frame, PLY_NUM for none.
static u8 stc_chase_ply[PLY_NUM];

typedef struct ReachEntry
{
    void *key;
    Vec3 pos;
    u32 frame;
    u8 ok;
} ReachEntry;

static ReachEntry stc_reach_cache[CPU_AI_REACH_CACHE_NUM];

// The item Rider_CPUScanItems is scoring, stashed for its Rider_CPUGetItemScore call.
static ItemData *stc_scan_item;

static const CustomItemsAPI *stc_items_api;

static u32 stc_tick_frame;

CpuAIPreset CpuAI_GetPreset(int ply)
{
    return stc_rider_preset[ply];
}

const char *CpuAI_GetPresetName(int ply)
{
    return stc_preset_names[stc_rider_preset[ply]];
}

// Slot ply's CPU rider, NULL when the slot has none or it has no CpuData.
RiderData *CpuAI_GetCpuRider(int ply)
{
    GOBJ *g = Ply_GetRiderGObj(ply);
    RiderData *rd;

    if (Ply_GetPKind(ply) != PKIND_CPU || !g)
        return NULL;
    rd = g->userdata;
    return rd->cpu ? rd : NULL;
}

// Patches collected plus City Trial's free CPU growth.
float CpuAI_StatTotal(int ply)
{
    PlayerData *pd = &stc_playerdata[ply];
    float total = 0;
    int i;

    for (i = 0; i < GetElementsIn(pd->stat_aux); i++)
        total += pd->stats.values[i] + pd->stat_aux[i];
    return total;
}

// The riding player furthest ahead other than exclude: race rank on a course, stat total in
// the city. PLY_NUM when there is none.
int CpuAI_Leader(int exclude, int humans_only)
{
    GameData *gd = Gm_GetGameData();
    int is_race = Scene_GetCurrentMajor() == MJRKIND_AIR;
    int best = PLY_NUM;
    float best_val = 0;
    float val;
    int i;

    for (i = 0; i < PLY_NUM; i++)
    {
        if (i == exclude || !Ply_GetRiderGObj(i) || (humans_only && Ply_GetPKind(i) != PKIND_HMN))
            continue;
        val = is_race ? -(float)gd->player_rank[i] : CpuAI_StatTotal(i);
        if (best == PLY_NUM || val > best_val)
        {
            best = i;
            best_val = val;
        }
    }
    return best;
}

float CpuAI_Skill(RiderData *rd, float min, float skill)
{
    return min + skill * Rider_CPUDifficultyScale(rd);
}

// Radians above the horizontal of a unit direction with this y.
float CpuAI_Elevation(float y)
{
    if (y > 1.0f)
        y = 1.0f;
    if (y < -1.0f)
        y = -1.0f;
    return (float)asin(y);
}

// The ground-plane point dist units from rd directly away from `from`, or along rd's facing
// when it is on top of it.
void CpuAI_AwayFrom(RiderData *rd, const Vec3 *from, float dist, Vec3 *out)
{
    Vec3 away = {rd->pos.X - from->X, 0, rd->pos.Z - from->Z};

    if (VECMag(&away) < 1.0f)
        away = rd->forward;
    away.Y = 0;
    VECNormalize(&away, &away);
    *out = (Vec3){rd->pos.X + away.X * dist, rd->pos.Y, rd->pos.Z + away.Z * dist};
}

int CpuAI_IsCityNavigate(RiderData *rd)
{
    return rd->cpu->ai_state == CPUSTATE_NAVIGATE;
}

static int CpuAI_LineReaches(HSD_Spline *line, float along, Vec3 *pos)
{
    Vec3 p;
    float dx, dz;

    if (!line)
        return 0;
    splArcLengthPoint(&p, line, along);
    dx = p.X - pos->X;
    dz = p.Z - pos->Z;
    return _fabs(p.Y - pos->Y) < CPU_AI_REACH_DY && dx * dx + dz * dz < CPU_AI_REACH_DXZ * CPU_AI_REACH_DXZ;
}

static int CpuAI_CheckReachable(RiderData *rd, Vec3 *pos)
{
    int range;
    float along;

    if (!Rider_CPUFindGoalRange(rd, pos, &range, &along) || range < 0)
        return 0;
    return CpuAI_LineReaches(Spline_GetForward(range), along, pos) ||
           CpuAI_LineReaches(Spline_GetBackward(range), along, pos) ||
           CpuAI_LineReaches(Spline_GetCenter(range), along, pos);
}

// Rider_CPUFindGoalRange matches the nearest range by height alone, and a goal it finds no
// range for gets no plan, so a goal on a rooftop or ledge leaves the CPU stalled on the road
// beneath or beside it. Cached per goal object in a direct-mapped table, since every CPU
// rescores every item every frame.
int CpuAI_IsReachable(RiderData *rd, void *key, Vec3 *pos)
{
    ReachEntry *e = &stc_reach_cache[(((u32)key * 2654435761u) >> 16) % CPU_AI_REACH_CACHE_NUM];
    u32 frame = stc_hsd_update->engine_frames;

    if (e->key == key && frame - e->frame < CPU_AI_REACH_CACHE_FRAMES && VECSquareDistance(&e->pos, pos) < 100.0f)
        return e->ok;

    e->key = key;
    e->pos = *pos;
    e->frame = frame;
    e->ok = CpuAI_CheckReachable(rd, pos);
    return e->ok;
}

void CpuAI_ImportApis(void)
{
    stc_items_api = (const CustomItemsAPI *)Hoshi_ImportMod(CUSTOM_ITEMS_MOD_NAME, CUSTOM_ITEMS_API_MAJOR,
                                                           CUSTOM_ITEMS_API_MINOR);
}

// The scene's GObjs are gone and their addresses come back as new ones, and no CPU has
// rolled a preset yet.
void CpuAI_On3DLoadStart(void)
{
    memset(stc_reach_cache, 0, sizeof(stc_reach_cache));
    memset(stc_rider_preset, CPU_AI_DEFAULT, sizeof(stc_rider_preset));
}

static CpuAIPreset CpuAI_Resolve(int selection, const u8 *list, int num)
{
    if (selection >= num)
        selection = RandomInRange(1, num - 1);
    return list[selection];
}

// Runs once per rider at init, so Random rolls per rider.
static void CpuAI_AssignPreset(RiderData *rd)
{
    int preset = CPU_AI_DEFAULT;
    int ply = rd->ply;

    // Game_Think gives a human slot an autopilot CpuData before marking it CPU, and the title
    // demo stays vanilla.
    if (Ply_GetPKind(ply) == PKIND_CPU && !Gm_IsAutoDemo())
    {
        CpuCatchup_AdjustLevel(rd);

        // Stadium events keep vanilla presets.
        switch (Scene_GetCurrentMajor())
        {
        case MJRKIND_AIR:
            preset = CpuAI_Resolve(cpu_ai_menu_ar, stc_ar_presets, CPU_AI_AR_NUM);
            break;
        case MJRKIND_CITY:
            if (!CityTrial_IsInStadium())
                preset = CpuAI_Resolve(cpu_ai_menu_ct, stc_ct_presets, CPU_AI_CT_NUM);
            break;
        default:
            break;
        }
    }

    stc_rider_preset[ply] = preset;
    stc_chase_ply[ply] = PLY_NUM;
    CpuFixes_ResetRider(ply);
    Strategist_Init(ply);
}

int CpuAI_FindRival(RiderData *rd)
{
    int rival = CpuTarget_PickRival(rd);

    if (rival != PLY_NUM)
        return rival;
    Rider_CPURivalSelect(rd);
    return rd->cpu->rival_ply;
}

// Steers at where the rival will be when the CPU gets there.
void CpuAI_ChaseRival(RiderData *rd, int rival, float range)
{
    const CpuRiderView *v;
    float dist, speed, lead;
    Vec3 pos;

    if (rival >= PLY_NUM || rival == rd->ply || !Ply_GetRiderGObj(rival))
        return;
    v = CpuSense_Rider(rival);
    dist = VECDistance(&rd->pos, (Vec3 *)&v->pos);
    if (dist > range)
        return;
    speed = CpuSense_Self(rd->ply)->speed;
    lead = dist / (speed > CPU_AI_LEAD_SPEED_MIN ? speed : CPU_AI_LEAD_SPEED_MIN);
    if (lead > CPU_AI_LEAD_FRAMES_MAX)
        lead = CPU_AI_LEAD_FRAMES_MAX;
    VECScale((Vec3 *)&v->vel, &pos, lead);
    VECAdd(&pos, (Vec3 *)&v->pos, &pos);
    if (CpuAI_IsReachable(rd, Ply_GetRiderGObj(rival), &pos))
        rd->cpu->nav_target_pos = pos;
}

// The nearest rider in range a hit would land on hard, else the usual rival; never one that
// cannot be hurt.
static int CpuAI_AggressiveRival(RiderData *rd, float range)
{
    int best = PLY_NUM;
    float best_dist = range * range;
    float dist;
    int i;

    for (i = 0; i < PLY_NUM; i++)
    {
        if (i == rd->ply || !CpuSense_IsVulnerable(i))
            continue;
        dist = VECSquareDistance(&rd->pos, (Vec3 *)&CpuSense_Rider(i)->pos);
        if (dist < best_dist)
        {
            best = i;
            best_dist = dist;
        }
    }
    if (best == PLY_NUM)
        best = CpuAI_FindRival(rd);
    if (best < PLY_NUM && CpuSense_Rider(best)->invincible)
        return PLY_NUM;
    return best;
}

// Aggressive: chase the rival while it is in range, ahead of any item or machine target.
static void CpuAI_AggressiveNavigate(RiderData *rd)
{
    float range = CpuAI_Skill(rd, CPU_AI_CHASE_RANGE_MIN, CPU_AI_CHASE_RANGE_SKILL);
    int target = CpuAI_AggressiveRival(rd, range);

    stc_chase_ply[rd->ply] = target;
    CpuAI_ChaseRival(rd, target, range);
}

// Cautious: away from a rider within range that cannot be hurt.
static void CpuAI_KeepClear(RiderData *rd)
{
    const CpuRiderView *v;
    Vec3 away;
    int i;

    for (i = 0; i < PLY_NUM; i++)
    {
        v = CpuSense_Rider(i);
        if (i == rd->ply || !v->present || !v->invincible ||
            VECSquareDistance(&rd->pos, (Vec3 *)&v->pos) > CPU_AI_KEEP_CLEAR_RANGE * CPU_AI_KEEP_CLEAR_RANGE)
            continue;
        CpuAI_AwayFrom(rd, &v->pos, CPU_AI_KEEP_CLEAR_DIST, &away);
        if (CpuAI_IsReachable(rd, NULL, &away))
            rd->cpu->nav_target_pos = away;
        return;
    }
}

// City goals written over the nav_target_pos Navigate picked: the fixes, a hunt or frenzy,
// then the preset.
static void CpuAI_NavigateTarget(RiderData *rd)
{
    if (Ply_GetPKind(rd->ply) != PKIND_CPU)
        return;
    stc_chase_ply[rd->ply] = PLY_NUM;

    if (cpu_fixes)
    {
        // Rider_CPUGetRouteDir ignores the planned route and steers straight at
        // nav_target_pos whenever navigate_frames / 1200 is odd, so every other 20 seconds a
        // city CPU drives into whatever stands between it and its target. Navigate counts it
        // up after this point.
        rd->cpu->navigate_frames = 0;
        CpuFixes_MeleeChase(rd);
    }

    if (CpuTarget_Navigate(rd))
        return;

    switch (stc_rider_preset[rd->ply])
    {
    case CPU_AI_AGGRESSIVE:
        CpuAI_AggressiveNavigate(rd);
        break;
    case CPU_AI_CAUTIOUS:
        Strategist_Heal(rd);
        CpuAI_KeepClear(rd);
        break;
    case CPU_AI_STRATEGIST:
        Strategist_Navigate(rd);
        break;
    default:
        break;
    }
}

// Hoarder: patches, boxes, timed boosts and legendary parts outrank abilities, weapons and
// candy, which it ignores. Food keeps its HP-scaled vanilla score.
static int CpuAI_HoarderScore(int kind, int score)
{
    if (kind <= ITKIND_CHARGENONE || (kind >= ITKIND_HYDRA1 && kind <= ITKIND_DRAGOON3))
        return CPU_AI_HOARD_SCORE;
    if (kind >= ITKIND_FOODMAXIMTOMATO && kind <= ITKIND_FOODAPPLE)
        return score;
    return 0;
}

static void CpuAI_StashScanItem(ItemData *it)
{
    stc_scan_item = it;
}

// A custom item's ItemKind, or -1. ItemData.kind holds a custom item's base kind.
static int CpuAI_CustomKind(ItemData *it)
{
    int kind;

    if (!stc_items_api)
        return -1;
    kind = stc_items_api->GetItemKind(it);
    return kind >= ITKIND_NUM ? kind : -1;
}

// What a custom item does lives in its mod's code, which a CPU cannot see, so only the stat
// grants it can read count; a bad or fake one is avoided.
static int CpuAI_CustomItemScore(int kind)
{
    PatchEffectInfo *info = Item_GetEffectInfo(kind);
    float gain = 0;
    int i;

    if (!info)
        return 0;
    if (info->group != ITGROUP_GOOD)
        return -1;
    for (i = 0; i < info->count; i++)
    {
        if (info->entries[i].type <= PATCHEFFECT_ALLUP)
            gain += info->entries[i].value;
    }
    return gain > 0 ? CPU_AI_CUSTOM_PATCH_SCORE : 0;
}

// Rider_CPUScanItems' per-item score. A score below 1 skips the item.
static int CpuAI_GetItemScore(RiderData *rd, int kind)
{
    int custom = CpuAI_CustomKind(stc_scan_item);
    int preset = stc_rider_preset[rd->ply];
    int score;

    if (custom >= 0)
    {
        score = CpuAI_CustomItemScore(custom);
    }
    else
    {
        score = Rider_CPUGetItemScore(rd, kind);
        if (score < 0)
            return score;
        switch (preset)
        {
        case CPU_AI_HOARDER:
            score = CpuAI_HoarderScore(kind, score);
            break;
        case CPU_AI_STRATEGIST:
            score = Strategist_ItemScore(rd, kind, score);
            break;
        default:
            break;
        }
    }

    score = CpuTeam_ItemScore(rd, stc_scan_item, score);

    // An unreachable item holds a city CPU under it for good. The fixes skip one, and so do
    // the whole-city scans of Hoarder and Strategist.
    if (score > 0 && (preset == CPU_AI_HOARDER || preset == CPU_AI_STRATEGIST || cpu_fixes) &&
        CpuAI_IsCityNavigate(rd) && !CpuAI_IsReachable(rd, stc_scan_item, &stc_scan_item->pos))
        return 0;

    return score;
}

// Navigate keeps an item target until it is gone. A Hoarder or Strategist also rescans the
// whole city on a skill-scaled interval, trading a far target for a better one.
static GOBJ *CpuAI_RevalidateItem(RiderData *rd, Vec3 *center, Vec3 *facing, float radius)
{
    GOBJ *item;
    int interval;
    int preset = stc_rider_preset[rd->ply];

    if (preset == CPU_AI_HOARDER || preset == CPU_AI_STRATEGIST)
    {
        interval = (int)CpuAI_Skill(rd, CPU_AI_HOARD_RESCAN_MIN, CPU_AI_HOARD_RESCAN_SKILL);
        if (rd->cpu->frame_counter % interval == 0)
        {
            item = Rider_CPUScanItems(rd, &rd->pos, NULL, CPU_AI_WHOLE_MAP);
            if (item)
                return item;
        }
    }

    return Rider_CPUScanItems(rd, center, facing, radius);
}

// Machine-state and committed maneuvers no preset replaces: flight, rails, on foot, a
// dismount, the launch charge and a running dodge.
int CpuAI_IsUrgent(int maneuver)
{
    switch (maneuver)
    {
    case CPUMAN_COAST:
    case CPUMAN_FLY:
    case CPUMAN_RAIL_RIDE:
    case CPUMAN_NAV_STEER:
    case CPUMAN_NAV_STEER_TAP:
    case CPUMAN_CHARGED_NAV_STEER:
    case CPUMAN_TAP_ONCE:
    case CPUMAN_WIGGLE:
    case CPUMAN_DISMOUNT:
    case CPUMAN_LAUNCH_CHARGE:
        return 1;
    default:
        return 0;
    }
}

// A Wiggle may replace the committed maneuver: nothing urgent runs and the rider's status
// does not inhibit the dodge.
int CpuAI_CanWiggle(CpuData *cpu)
{
    return !CpuAI_IsUrgent(cpu->maneuver) && !(cpu->desire_flags & CPUDESIRE_NO_DODGE);
}

void CpuAI_CommitZone(CpuData *cpu, CpuForwardTarget *t)
{
    cpu->zone_target_id = t->zone;
    cpu->zone_target_pos = t->pos;
    cpu->maneuver = t->side == 1 ? CPUMAN_ZONE_PRESS : CPUMAN_ZONE_PASS;
}

static int CpuAI_IsCruising(CpuData *cpu)
{
    return cpu->maneuver == cpu->base_maneuver || cpu->maneuver == CPUMAN_ZONE_PASS ||
           cpu->maneuver == CPUMAN_CHARGE_HOLD;
}

// A second roll at the Wiggle the cascade commits for an imminent hazard: an enemy, a
// projectile, or a rider that is attacking. A rider merely about to bump is left alone.
static void CpuAI_Dodge(RiderData *rd)
{
    CpuData *cpu = rd->cpu;
    const CpuThreat *t = CpuHazards_GetThreat(rd->ply);

    if (!CpuAI_CanWiggle(cpu))
        return;
    if (t->kind == CPU_THREAT_NONE || t->kind == CPU_THREAT_STATIC)
        return;
    if ((t->kind == CPU_THREAT_RIDER || t->kind == CPU_THREAT_MACHINE) &&
        (t->ply >= PLY_NUM || !CpuSense_IsThreatening(t->ply)))
        return;
    if (HSD_Randf() < CpuAI_Skill(rd, CPU_AI_DODGE_CHANCE_MIN, CPU_AI_DODGE_CHANCE_SKILL))
        cpu->maneuver = CPUMAN_WIGGLE;
}

// A second roll at the cascade's press-zone maneuver on the nearest side-1 zone ahead,
// taken only over cruising.
static void CpuAI_ZonePress(RiderData *rd)
{
    CpuData *cpu = rd->cpu;
    CpuForwardTarget *t;
    CpuForwardTarget *best = NULL;
    float dist;
    float best_dist = 0;
    int i;

    if (!CpuAI_IsCruising(cpu))
        return;
    if (HSD_Randf() >= CpuAI_Skill(rd, CPU_AI_ZONE_CHANCE_MIN, CPU_AI_ZONE_CHANCE_SKILL))
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
    if (best)
        CpuAI_CommitZone(cpu, best);
}

// A quick spin at a rider close ahead, the same stick shake the hazard Wiggle uses.
int CpuAI_SpinAt(RiderData *rd, int ply, float chance)
{
    CpuData *cpu = rd->cpu;
    Vec3 pos, to;

    if (ply >= PLY_NUM || ply == rd->ply || !Ply_GetRiderGObj(ply) || !CpuAI_CanWiggle(cpu))
        return 0;

    Ply_GetPosition(ply, &pos);
    if (VECSquareDistance(&rd->pos, &pos) > CPU_AI_SPIN_RANGE * CPU_AI_SPIN_RANGE)
        return 0;
    VECSubtract(&pos, &rd->pos, &to);
    if (VECDotProduct(&to, &rd->forward) < 0)
        return 0;
    if (HSD_Randf() >= chance)
        return 0;

    cpu->maneuver = CPUMAN_WIGGLE;
    return 1;
}

// Rider_CPUArbitrateManeuver exit, for every profile: the maneuver is committed and
// emitted next if the command player is idle.
static void CpuAI_AdjustManeuver(RiderData *rd)
{
    CpuData *cpu = rd->cpu;

    if (Ply_GetPKind(rd->ply) != PKIND_CPU)
        return;

    switch (stc_rider_preset[rd->ply])
    {
    case CPU_AI_CAUTIOUS:
        CpuAI_Dodge(rd);
        break;
    case CPU_AI_RECKLESS:
        if (cpu->maneuver == CPUMAN_WIGGLE)
            cpu->maneuver = cpu->base_maneuver;
        CpuAI_ZonePress(rd);
        break;
    case CPU_AI_AGGRESSIVE:
        CpuAI_SpinAt(rd, stc_chase_ply[rd->ply], CpuAI_Skill(rd, CPU_AI_SPIN_CHANCE_MIN, CPU_AI_SPIN_CHANCE_SKILL));
        CpuAI_ZonePress(rd);
        break;
    case CPU_AI_STRATEGIST:
        Strategist_Maneuver(rd);
        break;
    case CPU_AI_RACER:
        CpuRacer_Maneuver(rd);
        break;
    default:
        break;
    }

    if (cpu->ai_state == CPUSTATE_CRUISE_PLUS)
        CpuLine_Brake(rd);
    CpuTarget_Maneuver(rd);
    CpuStadium_Maneuver(rd);
}

// RiderGObj_CPUThink's (0x8018fc58) _Rider_UpdateCPU call (0x8018fc80), the whole brain for
// one CPU. The first CPU update of each tick refreshes what every CPU shares, so it runs only
// where CPUs think and freezes with them.
static void CpuAI_UpdateCPU(RiderData *rd)
{
    if (stc_tick_frame != stc_hsd_update->engine_frames)
    {
        stc_tick_frame = stc_hsd_update->engine_frames;
        CpuSense_OnTick();
        CpuTeam_OnTick();
        CpuTarget_OnTick();
    }

    CpuSense_UpdateSelf(rd);
    CpuGlide_Track(rd);
    Rider_UpdateCPU(rd);
    CpuStadium_AfterUpdate(rd);
    AIOverlay_RecordCPU(rd);
}

// Rider_CPUInit (0x80262d6c) epilogue `lwz r0,36(r1)`: every CpuData field is written and
// r31 still holds the RiderData*.
CODEPATCH_HOOKCREATE(0x80262fbc,
    "mr 3, 31\n\t",
    CpuAI_AssignPreset,
    "",
    0)

// Rider_CPUDecideNavigate (0x80271eb4) `lwz r3,184(r29)`: nav_target_pos is picked and
// about to become the steer target the route planner follows. r28 holds the RiderData*.
CODEPATCH_HOOKCREATE(0x80272258,
    "mr 3, 28\n\t",
    CpuAI_NavigateTarget,
    "",
    0)

// Rider_CPUArbitrateManeuver (0x80274ec0) exit `psq_l f31,120(r1)`: the maneuver is
// committed and r31 still holds the RiderData*.
CODEPATCH_HOOKCREATE(0x80275bc4,
    "mr 3, 31\n\t",
    CpuAI_AdjustManeuver,
    "",
    0)

// Rider_CPUScanItems (0x80263c4c) `lwz r4,28(r19)`, before its per-item score call: r19
// is the ItemData and no volatile register is live.
CODEPATCH_HOOKCREATE(0x80263cf8,
    "mr 3, 19\n\t",
    CpuAI_StashScanItem,
    "",
    0)

void CpuAI_InstallHooks(void)
{
    CODEPATCH_HOOKAPPLY(0x80262fbc);
    CODEPATCH_HOOKAPPLY(0x80272258);
    CODEPATCH_HOOKAPPLY(0x80275bc4);
    CODEPATCH_HOOKAPPLY(0x80263cf8);
    // Rider_CPUScanItems (0x80263c4c), the per-item score.
    CODEPATCH_REPLACECALL(0x80263d00, CpuAI_GetItemScore);
    // Rider_CPUDecideNavigate (0x80271eb4), the rescan of its current item_target.
    CODEPATCH_REPLACECALL(0x80272144, CpuAI_RevalidateItem);
    CODEPATCH_REPLACECALL(0x8018fc80, CpuAI_UpdateCPU);

    CpuFixes_InstallHooks();
    CpuHazards_InstallHooks();
    CpuLine_InstallHooks();
    CpuRacer_InstallHooks();
    CpuStadium_InstallHooks();
}
