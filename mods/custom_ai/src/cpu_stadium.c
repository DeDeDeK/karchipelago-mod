#include "game.h"
#include "hsd.h"
#include "machine.h"
#include "enemy.h"
#include "stage.h"
#include "os.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "cpu_ai.h"
#include "cpu_glide.h"
#include "cpu_stadium.h"

// Kirby Melee: the brain only Wiggles at hazards, so it never spins at an enemy in reach.
#define MELEE_SPIN_RANGE 12.0f
#define MELEE_SPIN_CHANCE_MIN 0.05f
#define MELEE_SPIN_CHANCE_SKILL 0.25f

// Kirby Melee 2's upper deck and rim share no road with the pit below; a CPU up there whose
// goal is this far below drives off the ledge instead of lapping the rim.
#define MELEE2_UPPER_Y 15.0f
#define MELEE2_DROP 15.0f

// Destruction Derby and the Dedede fight: Rider_CPUDecideAttack's spin burst is pushed into
// the command buffer during decide and overwritten before it plays. Its intended roll.
#define ATTACK_SPIN_CHANCE_MIN 0.1f
#define ATTACK_SPIN_CHANCE_SKILL 0.8f

// Target Flight: a target counts as reachable when it lies within the predicted glide's
// reach, with this margin, and this cone ahead.
#define TF_REACH_MARGIN 1.2f
#define TF_CONE 0.8f

// Air Glider: climb while faster than the high ratio of the air speed cap, dive below the
// low one to win speed back.
#define GLIDER_CLIMB_RATIO 0.85f
#define GLIDER_DIVE_RATIO 0.6f
#define GLIDER_CLIMB_PITCH 0.2f
#define GLIDER_DIVE_PITCH -0.35f
// High Jump: steep while rising, flat once falling.
#define HIGHJUMP_RISE_PITCH 0.6f
#define HIGHJUMP_FALL_PITCH 0.15f

// The ramps' lips: High Jump runs +X to X = -1850, Air Glider -Z to Z = 5150.
#define HIGHJUMP_LIP_X -1850.0f
#define GLIDER_LIP_Z 5150.0f
// Charge from this many frames before the lip and release at the second, so the boost
// is spent on the ramp rather than waiting in the air for a landing.
#define RAMP_CHARGE_FRAMES 70.0f
#define RAMP_RELEASE_FRAMES 12.0f

int cpu_stadium = 1;

static u8 stc_diving[PLY_NUM];

static EnemyData *CpuStadium_NearestEnemy(RiderData *rd, float range)
{
    EnemyData *best = NULL;
    float best_dist = range * range;
    float dist;
    EnemyData *ed;
    GOBJ *g;

    for (g = (*stc_gobj_lookup)[GAMEPLINK_ENEMY]; g; g = g->next)
    {
        ed = g->userdata;
        if (ed->kind >= ENEMYKIND_SPECIAL_START || ed->state < ENEMYSTATE_PERTYPE || ed->captor_gobj)
            continue;
        dist = VECSquareDistance(&rd->pos, &ed->pos);
        if (dist < best_dist)
        {
            best = ed;
            best_dist = dist;
        }
    }
    return best;
}

static void CpuStadium_MeleeSpin(RiderData *rd)
{
    CpuData *cpu = rd->cpu;

    if (!CpuAI_CanWiggle(cpu) || !CpuStadium_NearestEnemy(rd, MELEE_SPIN_RANGE))
        return;
    if (HSD_Randf() < CpuAI_Skill(rd, MELEE_SPIN_CHANCE_MIN, MELEE_SPIN_CHANCE_SKILL))
        cpu->maneuver = CPUMAN_WIGGLE;
}

// The Dedede fight's CPUs only ever pick Dedede; the item detour the Attack profile rolls
// every 300 frames pulls them off him.
static void CpuStadium_Attack(RiderData *rd)
{
    CpuData *cpu = rd->cpu;

    if (cpu->city_kind == CITYKIND_VSDEDEDE)
        cpu->item_target = NULL;
    CpuAI_SpinAt(rd, cpu->rival_ply, CpuAI_Skill(rd, ATTACK_SPIN_CHANCE_MIN, ATTACK_SPIN_CHANCE_SKILL));
}

// Arbitration exit, for stadium profiles only.
void CpuStadium_Maneuver(RiderData *rd)
{
    if (!cpu_stadium)
        return;

    switch (rd->cpu->ai_state)
    {
    case CPUSTATE_ROUTE_FOLLOW:
    case CPUSTATE_ROUTE_FOLLOW_CITY:
        CpuStadium_MeleeSpin(rd);
        break;
    case CPUSTATE_ATTACK:
        CpuStadium_Attack(rd);
        break;
    default:
        break;
    }
}

// The pitch the flight stadiums' experts fly, radians up; vanilla's target otherwise.
float CpuStadium_TargetPitch(RiderData *rd, float vanilla)
{
    MachineData *md;
    Vec3 *vel;
    float ratio;
    int ply = rd->ply;

    if (!cpu_stadium || rd->cpu->ai_state != CPUSTATE_REPOSITION || !rd->machine_gobj)
        return vanilla;
    md = rd->machine_gobj->userdata;
    vel = &md->world_velocity;

    switch (rd->cpu->stage_kind)
    {
    case STAGEKIND_AIRGLIDER:
        if (md->top_speed_air <= 0)
            return vanilla;
        ratio = sqrtf(vel->X * vel->X + vel->Z * vel->Z) / md->top_speed_air;
        if (ratio < GLIDER_DIVE_RATIO)
            stc_diving[ply] = 1;
        else if (ratio > GLIDER_CLIMB_RATIO)
            stc_diving[ply] = 0;
        return stc_diving[ply] ? GLIDER_DIVE_PITCH : GLIDER_CLIMB_PITCH;
    case STAGEKIND_HIGHJUMP:
        return vel->Y > 0 ? HIGHJUMP_RISE_PITCH : HIGHJUMP_FALL_PITCH;
    default:
        return vanilla;
    }
}

// Frames to the ramp's lip at the current speed, or -1 when not heading for it.
static float CpuStadium_FramesToLip(RiderData *rd, Vec3 *vel)
{
    switch (rd->cpu->stage_kind)
    {
    case STAGEKIND_HIGHJUMP:
        return vel->X > 0 && rd->pos.X < HIGHJUMP_LIP_X ? (HIGHJUMP_LIP_X - rd->pos.X) / vel->X : -1.0f;
    case STAGEKIND_AIRGLIDER:
        return vel->Z < 0 && rd->pos.Z > GLIDER_LIP_Z ? (rd->pos.Z - GLIDER_LIP_Z) / -vel->Z : -1.0f;
    default:
        return -1.0f;
    }
}

// Vanilla never charges on the ramp: the press is held from RAMP_CHARGE_FRAMES before the lip
// and let go at RAMP_RELEASE_FRAMES. Written after the brain's update, every frame of the
// approach, so it wins over whatever the command scripts played.
void CpuStadium_AfterUpdate(RiderData *rd)
{
    CpuData *cpu = rd->cpu;
    MachineData *md;
    float frames;

    if (!cpu_stadium || cpu->ai_state != CPUSTATE_REPOSITION || !rd->machine_gobj)
        return;
    md = rd->machine_gobj->userdata;
    if (md->is_airborne || md->is_flying)
        return;
    stc_diving[rd->ply] = 0;

    if (!rd->round_started || !MachineGObj_CPUCanChargeHold(rd->machine_gobj))
        return;
    frames = CpuStadium_FramesToLip(rd, &md->world_velocity);
    if (frames < 0 || frames > RAMP_CHARGE_FRAMES)
        return;
    if (frames > RAMP_RELEASE_FRAMES)
        cpu->buttons |= PAD_BUTTON_A;
    else
        cpu->buttons &= ~PAD_BUTTON_A;
}

// The jump-pad override rewrites only steer_target_pos, while the junction ranking reads
// nav_target_pos, so a CPU below its goal circles instead of heading for the pad.
static void CpuStadium_Melee2Climb(RiderData *rd)
{
    CpuData *cpu = rd->cpu;

    if (cpu_stadium)
        cpu->nav_target_pos = cpu->steer_target_pos;
}

// Clearing the route-valid bit sends Rider_CPUGetRouteDir down its direct path.
static void CpuStadium_Melee2Drop(RiderData *rd)
{
    CpuData *cpu = rd->cpu;

    if (!cpu_stadium || !cpu->route_goal)
        return;
    if (rd->pos.Y > MELEE2_UPPER_Y && cpu->route_goal_pos.Y < rd->pos.Y - MELEE2_DROP)
        cpu->status_flags &= ~CPUSTATUS_ROUTE_VALID;
}

// Rider_CPUDecideCharge's (0x80272dd0) Rider_CPUSelectChargeAnchor calls (0x80272eec,
// 0x80272fbc). In flight, the best-scoring target below that the predicted glide can still
// reach; otherwise vanilla's random pick among the best few anywhere on the field.
static int CpuStadium_SelectTarget(RiderData *rd)
{
    int best = -1;
    int best_score = 0;
    int num, id, score;
    Vec3 vel, pos, to, land;
    float speed_h, reach, dist_h;

    if (!cpu_stadium || !rd->machine_gobj || !MachineGObj_IsFlying(rd->machine_gobj) || !CpuGlide_Predict(rd, &land))
        return Rider_CPUSelectChargeAnchor(rd);

    MachineGObj_GetSelfVel(rd->machine_gobj, &vel);
    speed_h = sqrtf(vel.X * vel.X + vel.Z * vel.Z);
    if (speed_h <= 0)
        return Rider_CPUSelectChargeAnchor(rd);
    to = (Vec3){land.X - rd->pos.X, 0, land.Z - rd->pos.Z};
    reach = VECMag(&to) * TF_REACH_MARGIN;

    num = Gr_GetTargetCount();
    for (id = 0; id < num; id++)
    {
        score = Gr_GetTargetScore(id);
        if (score <= best_score)
            continue;
        Gr_GetTargetPos(id, &pos);
        if (pos.Y >= rd->pos.Y)
            continue;
        to = (Vec3){pos.X - rd->pos.X, 0, pos.Z - rd->pos.Z};
        dist_h = VECMag(&to);
        if (dist_h > reach)
            continue;
        if (dist_h > 0 && (to.X * vel.X + to.Z * vel.Z) / (dist_h * speed_h) < TF_CONE)
            continue;
        best = id;
        best_score = score;
    }

    return best >= 0 ? best : Rider_CPUSelectChargeAnchor(rd);
}

// Read by the Target Flight hook's epilogue into f29.
float cpu_stadium_tf_elevation;

// Rider_CPUEmitFlySteer's Target Flight branch projects both directions onto the plane
// across the flight axis, where their angle is about 0 or pi, so the CPU can neither dive
// nor climb at a target. The fix pitches toward the direction's own elevation.
static int CpuStadium_TargetFlightPitch(Vec3 *dir)
{
    if (!cpu_stadium)
        return 0;
    cpu_stadium_tf_elevation = CpuAI_Elevation(dir->Y);
    return 1;
}

// Rider_CPUDecideRouteFollowCity (0x80272888) `lis r3`, before Rider_CPUBuildRoute: the
// jump-pad override has run, r30 is the RiderData and no volatile register is live.
CODEPATCH_HOOKCREATE(0x80272b00,
    "mr 3, 30\n\t",
    CpuStadium_Melee2Climb,
    "",
    0)

// Rider_CPUDecideRouteFollowCity (0x80272888) `mr r3,r30` after Rider_CPUCollectHazards: the
// route-valid bit is stored and nothing volatile is live.
CODEPATCH_HOOKCREATE(0x80272bb0,
    "mr 3, 30\n\t",
    CpuStadium_Melee2Drop,
    "",
    0)

// Rider_CPUEmitFlySteer (0x8026f0e4) `lis r3` at the head of the Target Flight branch: r29
// is the desired direction and r3-r6 are loaded fresh after. With the fix on, returns to the
// shared pitch output (0x8026fa00) with f29 the elevation.
CODEPATCH_HOOKCONDITIONALCREATE(0x8026f8ec,
    "mr 3, 29\n\t",
    CpuStadium_TargetFlightPitch,
    "lis 4, cpu_stadium_tf_elevation@ha\n\t"
    "lfs 29, cpu_stadium_tf_elevation@l(4)\n\t",
    0,
    0x8026fa00)

void CpuStadium_InstallHooks(void)
{
    CODEPATCH_HOOKAPPLY(0x80272b00);
    CODEPATCH_HOOKAPPLY(0x80272bb0);
    CODEPATCH_HOOKAPPLY(0x8026f8ec);
    CODEPATCH_REPLACECALL(0x80272eec, CpuStadium_SelectTarget);
    CODEPATCH_REPLACECALL(0x80272fbc, CpuStadium_SelectTarget);
}
