#include "game.h"
#include "hsd.h"
#include "item.h"
#include "machine.h"
#include "enemy.h"
#include "stadium.h"
#include "yakumono.h"
#include "os.h"
#include "inline.h"

#include "cpu_ai.h"
#include "cpu_sense.h"
#include "cpu_team.h"
#include "cpu_target.h"
#include "cpu_strategist.h"

// Goals are re-scored this often (staggered per rider) and held at least this long.
#define STRAT_THINK_FRAMES 20
#define STRAT_HOLD_FRAMES 90
// A goal that gets no closer by STRAT_PROGRESS units in STRAT_STALL_FRAMES is dropped and
// its target ignored for STRAT_BAN_FRAMES.
#define STRAT_PROGRESS 20.0f
#define STRAT_STALL_FRAMES 300
#define STRAT_BAN_FRAMES 900

#define STRAT_HEAL_RATIO 0.45f
// A meteor is fled when its blast, with this margin, will reach the rider within the frames.
#define STRAT_FLEE_MARGIN 1.5f
#define STRAT_FLEE_FRAMES 240.0f
#define STRAT_FLEE_DIST 500.0f
#define STRAT_STRIKE_RANGE 900.0f
#define STRAT_VISIT_RANGE 1500.0f
#define STRAT_MACHINE_RANGE 1200.0f
#define STRAT_MACHINE_MARGIN 3
#define STRAT_DISMOUNT_RANGE 30.0f
#define STRAT_CLAIM_RANGE 100.0f // the radius of the cascade's own machine-swap scan
#define STRAT_SPIN_RANGE 25.0f
// Late in the round the stadium is close, so the machine for it matters more.
#define STRAT_ENDGAME_PROGRESS 0.75f
#define STRAT_ENDGAME_BONUS 20
// A machine no table rates (a custom one).
#define STRAT_SUIT_UNKNOWN 5

// Base utility of each goal; items are what Navigate does on its own.
#define STRAT_UTIL_ITEMS 35
#define STRAT_UTIL_FLEE 100
#define STRAT_UTIL_HEAL 70
#define STRAT_UTIL_VISIT 60
#define STRAT_UTIL_STRIKE 55
#define STRAT_UTIL_MACHINE 30

#define STRAT_SCORE_PER_WEIGHT 6
#define STRAT_SCORE_ALLUP 70
#define STRAT_SCORE_BOX 40
#define STRAT_SCORE_LEGEND 60
#define STRAT_SCORE_LEGEND_HELD 15 // per piece of the same machine already held
#define STRAT_SCORE_COMBAT_ABILITY 15
#define STRAT_SCORE_COMBAT_CANDY 10
#define STRAT_SCORE_TIMED 5

typedef enum StratGoal
{
    STRAT_GOAL_ITEMS,
    STRAT_GOAL_MACHINE,
    STRAT_GOAL_HEAL,
    STRAT_GOAL_FLEE,
    STRAT_GOAL_STRIKE,
    STRAT_GOAL_VISIT,
    STRAT_GOAL_NUM,
} StratGoal;

static const char *stc_goal_names[STRAT_GOAL_NUM] = {
    [STRAT_GOAL_ITEMS] = "items",
    [STRAT_GOAL_MACHINE] = "machine",
    [STRAT_GOAL_HEAL] = "heal",
    [STRAT_GOAL_FLEE] = "flee",
    [STRAT_GOAL_STRIKE] = "strike",
    [STRAT_GOAL_VISIT] = "visit",
};

typedef struct StratState
{
    u8 goal;
    s16 hold;
    s16 stall;
    s16 ban_timer;
    float best_dist;
    void *target; // the GOBJ a goal follows, NULL for a fixed spot
    void *banned;
    Vec3 pos;
} StratState;

typedef struct StratCandidate
{
    int goal;
    int utility;
    void *target;
    Vec3 pos;
} StratCandidate;

static StratState stc_strat[PLY_NUM];

// How much each stadium group wants each stat, 0-10, by PatchKind: weight, boost, top speed,
// turn, charge, glide, offense, defense, HP.
static const u8 stc_stat_weight[STGROUP_NUM][PATCHKIND_NUM] = {
    [STGROUP_DRAGRACE] = {2, 9, 10, 1, 6, 1, 1, 1, 1},
    [STGROUP_AIRGLIDER] = {0, 6, 7, 2, 5, 10, 0, 0, 1},
    [STGROUP_TARGETFLIGHT] = {1, 6, 7, 4, 5, 10, 0, 0, 1},
    [STGROUP_HIGHJUMP] = {0, 7, 8, 1, 8, 9, 0, 0, 1},
    [STGROUP_MELEE] = {3, 4, 3, 6, 3, 1, 10, 8, 9},
    [STGROUP_DESTRUCTION] = {7, 5, 5, 5, 4, 1, 10, 9, 9},
    [STGROUP_SINGLERACE] = {3, 8, 9, 8, 5, 5, 2, 3, 3},
    [STGROUP_VSKINGDEDEDE] = {4, 5, 5, 6, 4, 2, 10, 8, 10},
};

// How well each machine suits each stadium group, 0-10, by MachineKind. Kinds a CPU can
// not find in the city are 0.
static const u8 stc_machine_suit[STGROUP_NUM][VCKIND_NUM] = {
    //                  Wrp Cmp Wng Shd Hyd Blk Slk For Drg Wag Rck Swv Trb Jet
    [STGROUP_DRAGRACE] = {5, 3, 4, 5, 10, 3, 6, 9, 8, 3, 8, 5, 9, 8,
                          [VCKIND_WHEELIEBIKE] = 7, [VCKIND_REXWHEELIE] = 6, [VCKIND_WHEELIESCOOTER] = 5},
    [STGROUP_AIRGLIDER] = {5, 4, 9, 4, 4, 2, 6, 3, 10, 3, 4, 5, 4, 7},
    [STGROUP_TARGETFLIGHT] = {5, 4, 9, 4, 6, 2, 6, 4, 10, 3, 5, 5, 5, 7},
    [STGROUP_HIGHJUMP] = {5, 4, 9, 4, 6, 2, 5, 5, 10, 3, 7, 4, 6, 8,
                          [VCKIND_WHEELIEBIKE] = 1, [VCKIND_REXWHEELIE] = 1, [VCKIND_WHEELIESCOOTER] = 1},
    [STGROUP_MELEE] = {5, 6, 4, 6, 9, 8, 4, 4, 8, 9, 4, 5, 5, 4,
                       [VCKIND_WHEELIEBIKE] = 5, [VCKIND_REXWHEELIE] = 7, [VCKIND_WHEELIESCOOTER] = 5},
    [STGROUP_DESTRUCTION] = {5, 6, 4, 6, 10, 9, 4, 4, 8, 9, 5, 5, 6, 5,
                             [VCKIND_WHEELIEBIKE] = 5, [VCKIND_REXWHEELIE] = 8, [VCKIND_WHEELIESCOOTER] = 5},
    [STGROUP_SINGLERACE] = {6, 4, 6, 6, 9, 3, 7, 7, 10, 3, 6, 7, 8, 7,
                            [VCKIND_WHEELIEBIKE] = 7, [VCKIND_REXWHEELIE] = 6, [VCKIND_WHEELIESCOOTER] = 5},
    [STGROUP_VSKINGDEDEDE] = {5, 5, 5, 6, 10, 8, 5, 5, 9, 8, 5, 5, 6, 5,
                              [VCKIND_WHEELIEBIKE] = 5, [VCKIND_REXWHEELIE] = 7, [VCKIND_WHEELIESCOOTER] = 5},
};

// Pieces of the part's machine the rider already holds.
static int Strategist_PiecesHeld(int ply, int kind)
{
    return Popcount64(kind <= ITKIND_HYDRA3 ? Ply_GetHydraPieceMask(ply) : Ply_GetDragoonPieceMask(ply));
}

static int Strategist_Group(void)
{
    int group = Gm_GetCurrentStadiumGroup();

    if (group < 0 || group >= STGROUP_NUM)
        return STGROUP_SINGLERACE;
    return group;
}

static int Strategist_IsCombatGroup(int group)
{
    return group == STGROUP_MELEE || group == STGROUP_DESTRUCTION || group == STGROUP_VSKINGDEDEDE;
}

// Every stat patch scores by how much the round's stadium wants it; a stat it does not want
// at all is left on the ground. Down and fake patches never get here: vanilla scores them -1.
int Strategist_ItemScore(RiderData *rd, int kind, int score)
{
    int group = Strategist_Group();
    int stat = Item_KindToPatchKind(kind);

    if (stat >= 0)
        return stc_stat_weight[group][stat] * STRAT_SCORE_PER_WEIGHT;

    switch (kind)
    {
    case ITKIND_ALLUP:
        return STRAT_SCORE_ALLUP;
    case ITKIND_BOXBLUE:
        return STRAT_SCORE_BOX;
    case ITKIND_SPEEDMAX:
    case ITKIND_OFFENSEMAX:
    case ITKIND_DEFENSEMAX:
    case ITKIND_CHARGEMAX:
        return STRAT_SCORE_TIMED;
    case ITKIND_CANDY:
        return Strategist_IsCombatGroup(group) ? STRAT_SCORE_COMBAT_CANDY : score;
    default:
        break;
    }

    if (kind >= ITKIND_HYDRA1 && kind <= ITKIND_DRAGOON3)
        return STRAT_SCORE_LEGEND + STRAT_SCORE_LEGEND_HELD * Strategist_PiecesHeld(rd->ply, kind);
    if (kind >= ITKIND_COPYBOMB && kind <= ITKIND_COPYMIKE && kind != ITKIND_COPYSLEEP &&
        Strategist_IsCombatGroup(group))
        return STRAT_SCORE_COMBAT_ABILITY;
    return score;
}

void Strategist_Init(int ply)
{
    memset(&stc_strat[ply], 0, sizeof(StratState));
}

const char *Strategist_GetGoalName(int ply)
{
    return stc_goal_names[stc_strat[ply].goal];
}

// The object the rider's goal follows, if any.
int Strategist_GetTarget(int ply, void **target, Vec3 *pos)
{
    StratState *st = &stc_strat[ply];

    if (st->goal == STRAT_GOAL_ITEMS || !st->target)
        return 0;
    *target = st->target;
    *pos = st->pos;
    return 1;
}

static int Strategist_MachineSuit(int group, GOBJ *machine)
{
    MachineData *md;

    if (!machine)
        return 0;
    md = machine->userdata;
    if (md->kind >= (md->is_bike ? VCWHEEL_NUM : VCSTAR_NUM))
        return STRAT_SUIT_UNKNOWN;
    return stc_machine_suit[group][MachineGObj_GetAbsoluteKind(machine)];
}

static void Strategist_Offer(StratCandidate *best, int goal, int utility, void *target, Vec3 *pos)
{
    if (utility <= best->utility)
        return;
    best->goal = goal;
    best->utility = utility;
    best->target = target;
    best->pos = *pos;
}

// Out of a meteor's predicted blast.
static void Strategist_ConsiderFlee(RiderData *rd, StratCandidate *best)
{
    const CpuWorldView *w = CpuSense_World();
    const CpuMeteorView *m;
    float reach;
    Vec3 spot;
    int i;

    for (i = 0; i < w->meteor_num; i++)
    {
        m = &w->meteor[i];
        reach = m->radius * STRAT_FLEE_MARGIN;
        if (m->frames > STRAT_FLEE_FRAMES || VECSquareDistance(&rd->pos, (Vec3 *)&m->impact) > reach * reach)
            continue;
        CpuAI_AwayFrom(rd, &m->impact, STRAT_FLEE_DIST, &spot);
        Strategist_Offer(best, STRAT_GOAL_FLEE, STRAT_UTIL_FLEE, NULL, &spot);
        return;
    }
}

static void Strategist_ConsiderHeal(RiderData *rd, StratCandidate *best)
{
    Vec3 zone;
    float ratio = CpuSense_Rider(rd->ply)->hp_ratio;

    if (ratio >= STRAT_HEAL_RATIO)
        return;
    if (!GrYakuRecoveryZone_FindNearest(&rd->pos, &zone) || !CpuAI_IsReachable(rd, NULL, &zone))
        return;
    Strategist_Offer(best, STRAT_GOAL_HEAL, STRAT_UTIL_HEAL + (int)((STRAT_HEAL_RATIO - ratio) * 100), NULL, &zone);
}

static int Strategist_InRange(RiderData *rd, Vec3 *pos, float range)
{
    return VECSquareDistance(&rd->pos, pos) < range * range;
}

// TAC drops what it stole when hit, and the Pillar event's pillars break into items.
static void Strategist_ConsiderStrike(RiderData *rd, StratCandidate *best, void *banned)
{
    EnemyData *ed;
    Vec3 pos;
    GOBJ *g;

    for (g = (*stc_gobj_lookup)[GAMEPLINK_ENEMY]; g; g = g->next)
    {
        ed = g->userdata;
        if (ed->kind != ENEMYKIND_TAC || g == banned)
            continue;
        if (Strategist_InRange(rd, &ed->pos, STRAT_STRIKE_RANGE) && CpuAI_IsReachable(rd, g, &ed->pos))
            Strategist_Offer(best, STRAT_GOAL_STRIKE, STRAT_UTIL_STRIKE, g, &ed->pos);
    }

    if (GrYakuEventPillar_GetPos(&pos) && Strategist_InRange(rd, &pos, STRAT_VISIT_RANGE) &&
        CpuAI_IsReachable(rd, NULL, &pos))
        Strategist_Offer(best, STRAT_GOAL_STRIKE, STRAT_UTIL_STRIKE - 5, NULL, &pos);
}

static void Strategist_ConsiderVisit(RiderData *rd, StratCandidate *best, void *banned)
{
    Vec3 pos;
    GOBJ *g;

    for (g = (*stc_gobj_lookup)[GAMEPLINK_YAKUMONO]; g; g = g->next)
    {
        if (YakumonoGObj_GetKind(g) != YAKUKIND_SECRETCHAMBER || g == banned)
            continue;
        YakumonoGObj_GetWorldPos(g, &pos);
        if (Strategist_InRange(rd, &pos, STRAT_VISIT_RANGE) && CpuAI_IsReachable(rd, g, &pos))
            Strategist_Offer(best, STRAT_GOAL_VISIT, STRAT_UTIL_VISIT, g, &pos);
    }

    if (Lighthouse_FindBeamSpot(&rd->pos, &pos) && Strategist_InRange(rd, &pos, STRAT_VISIT_RANGE) &&
        CpuAI_IsReachable(rd, NULL, &pos))
        Strategist_Offer(best, STRAT_GOAL_VISIT, STRAT_UTIL_VISIT - 15, NULL, &pos);
}

// A field machine that suits the round's stadium clearly better than the one ridden. Hydra
// and Dragoon are never left, as in vanilla.
static void Strategist_ConsiderMachine(RiderData *rd, StratCandidate *best, void *banned)
{
    int group = Strategist_Group();
    int cur = Strategist_MachineSuit(group, rd->machine_gobj);
    int suit, utility;
    MachineData *md;
    Vec3 pos;
    float dist;
    GOBJ *g;
    int bonus = CpuSense_World()->round_progress > STRAT_ENDGAME_PROGRESS ? STRAT_ENDGAME_BONUS : 0;

    if (rd->machine_gobj && !MachineGObj_CPUCanSwap(rd->machine_gobj))
        return;

    for (g = (*stc_gobj_lookup)[GAMEPLINK_MACHINE]; g; g = g->next)
    {
        md = g->userdata;
        if (md->rider_gobj || g == banned || MachineGObj_IsDead(g) || MachineGObj_IsStuck(g))
            continue;
        suit = Strategist_MachineSuit(group, g);
        if (suit < cur + STRAT_MACHINE_MARGIN)
            continue;
        MachineGObj_GetPos(g, &pos);
        dist = VECDistance(&rd->pos, &pos);
        if (dist > STRAT_MACHINE_RANGE || CpuTeam_IsClaimed(rd->ply, g, dist) || !CpuAI_IsReachable(rd, g, &pos))
            continue;
        utility = STRAT_UTIL_MACHINE + bonus + (suit - cur) * 5 - (int)(dist / 100.0f);
        Strategist_Offer(best, STRAT_GOAL_MACHINE, utility, g, &pos);
    }
}

// The goal's target still exists and still wants this rider; refreshes its position.
static int Strategist_TargetValid(StratState *st)
{
    GOBJ *g = st->target;
    GOBJ *it;
    int plink;

    if (!g)
        return 1;

    switch (st->goal)
    {
    case STRAT_GOAL_MACHINE:
        plink = GAMEPLINK_MACHINE;
        break;
    case STRAT_GOAL_STRIKE:
        plink = GAMEPLINK_ENEMY;
        break;
    default:
        plink = GAMEPLINK_YAKUMONO;
        break;
    }

    for (it = (*stc_gobj_lookup)[plink]; it; it = it->next)
    {
        if (it == g)
            break;
    }
    if (!it)
        return 0;

    switch (st->goal)
    {
    case STRAT_GOAL_MACHINE:
        if (((MachineData *)g->userdata)->rider_gobj || MachineGObj_IsDead(g))
            return 0;
        MachineGObj_GetPos(g, &st->pos);
        break;
    case STRAT_GOAL_STRIKE:
        st->pos = ((EnemyData *)g->userdata)->pos;
        break;
    default:
        break;
    }
    return 1;
}

static void Strategist_Think(RiderData *rd, StratState *st)
{
    StratCandidate best = {.goal = STRAT_GOAL_ITEMS, .utility = STRAT_UTIL_ITEMS};
    void *banned = st->ban_timer > 0 ? st->banned : NULL;

    Strategist_ConsiderFlee(rd, &best);
    Strategist_ConsiderHeal(rd, &best);
    Strategist_ConsiderStrike(rd, &best, banned);
    Strategist_ConsiderVisit(rd, &best, banned);
    Strategist_ConsiderMachine(rd, &best, banned);

    // A held flee follows the danger it flees.
    if (best.goal == STRAT_GOAL_FLEE && st->goal == STRAT_GOAL_FLEE)
    {
        st->pos = best.pos;
        return;
    }

    // Fleeing preempts a held goal; anything else waits out the hold.
    if (st->hold > 0 && best.goal != STRAT_GOAL_FLEE)
        return;
    if (best.goal == st->goal && best.target == st->target)
        return;

    st->goal = best.goal;
    st->target = best.target;
    st->pos = best.pos;
    st->hold = STRAT_HOLD_FRAMES;
    st->stall = 0;
    st->best_dist = VECDistance(&rd->pos, &best.pos);
}

static void Strategist_Drop(StratState *st)
{
    st->banned = st->target;
    st->ban_timer = STRAT_BAN_FRAMES;
    st->goal = STRAT_GOAL_ITEMS;
    st->target = NULL;
    st->hold = 0;
}

static void Strategist_TrackProgress(RiderData *rd, StratState *st)
{
    float dist = VECDistance(&rd->pos, &st->pos);

    if (dist < st->best_dist - STRAT_PROGRESS)
    {
        st->best_dist = dist;
        st->stall = 0;
    }
    else if (++st->stall > STRAT_STALL_FRAMES)
    {
        Strategist_Drop(st);
    }
}

// Picks the rider's goal and steers Navigate at it; items stay with vanilla targeting,
// rescored by Strategist_ItemScore.
void Strategist_Navigate(RiderData *rd)
{
    StratState *st = &stc_strat[rd->ply];

    if (st->hold > 0)
        st->hold--;
    if (st->ban_timer > 0)
        st->ban_timer--;

    if (st->goal != STRAT_GOAL_ITEMS && !Strategist_TargetValid(st))
        Strategist_Drop(st);

    if ((rd->cpu->frame_counter + rd->ply * 4) % STRAT_THINK_FRAMES == 0)
        Strategist_Think(rd, st);

    if (st->goal == STRAT_GOAL_ITEMS)
        return;

    Strategist_TrackProgress(rd, st);
    if (st->goal != STRAT_GOAL_ITEMS)
        rd->cpu->nav_target_pos = st->pos;
}

// Cautious riders head for a Recovery Zone when hurt.
void Strategist_Heal(RiderData *rd)
{
    StratCandidate best = {.goal = STRAT_GOAL_ITEMS, .utility = 0};

    Strategist_ConsiderHeal(rd, &best);
    if (best.goal == STRAT_GOAL_HEAL)
        rd->cpu->nav_target_pos = best.pos;
}

// Dismounts beside the chosen machine, which vanilla then boards by contact, and spins
// into a strike target. A hunt or frenzy holds Navigate instead, which leaves the goal
// unchecked.
void Strategist_Maneuver(RiderData *rd)
{
    StratState *st = &stc_strat[rd->ply];
    CpuData *cpu = rd->cpu;

    if (!CpuAI_IsCityNavigate(rd) || CpuTarget_IsChasing(rd))
        return;

    // Claimed even on foot, where the cascade's NavSteer walks to city_object_pos.
    if (st->goal == STRAT_GOAL_MACHINE && Strategist_InRange(rd, &st->pos, STRAT_CLAIM_RANGE))
    {
        cpu->city_object = st->target;
        cpu->city_object_pos = st->pos;
    }

    if (CpuAI_IsUrgent(cpu->maneuver))
        return;

    switch (st->goal)
    {
    case STRAT_GOAL_MACHINE:
        if (rd->machine_gobj && Strategist_InRange(rd, &st->pos, STRAT_DISMOUNT_RANGE))
            cpu->maneuver = CPUMAN_DISMOUNT;
        break;
    case STRAT_GOAL_STRIKE:
        if (Strategist_InRange(rd, &st->pos, STRAT_SPIN_RANGE) && CpuAI_CanWiggle(cpu))
            cpu->maneuver = CPUMAN_WIGGLE;
        break;
    default:
        break;
    }
}
