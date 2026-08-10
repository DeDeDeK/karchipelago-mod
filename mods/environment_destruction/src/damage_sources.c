#include <string.h>

#include "os.h"
#include "game.h"
#include "obj.h"
#include "rider.h"
#include "machine.h"
#include "item.h"
#include "projectile.h"
#include "collision.h"

#include "environment_destruction.h"

int env_src_spin       = 1;
int env_src_projectile = 1;
int env_src_machine    = 1;
int env_src_charge     = 1;

// RiderData.state_idx while a quick spin is active. Kirby and Dedede enter 0x2c
// (Rider_QuickSpin_Enter / Rider_Dedede_QuickSpin_Enter), Meta Knight 0x2d.
// State tables are per character, so the number alone does not identify a spin.
#define ENV_SPIN_STATE      0x2c
#define ENV_SPIN_STATE_MK   0x2d
// A machine counts as ramming above this fraction of its current top speed.
#define ENV_RAM_FRACTION 0.5f
// charge_value (0..1) that counts as a charged hit.
#define ENV_CHARGE_MIN   0.85f
// Player slots to scan.
#define ENV_MAX_PLAYERS  5

// Contact probes are cast out from an entity's own origin, so their lengths are
// measured against the collision spheres the game gives those entities: a
// machine's is about 0.76 world units and it settles about 0.4 above the ground
// it rests on, against a ground top speed near 1.1 units a frame.
//
// A spin swings out past the machine and its exact hitbox is not what decides a
// carve here, so the reach is a plain number: far enough that a spin bouncing
// off a wall still registers on its approach, short enough that spinning beside
// a wall does nothing.
#define ENV_SPIN_REACH    3.0f
// Added to a mover's travel this frame, so a ram means "will be inside this wall
// shortly" rather than "is pointed at it".
#define ENV_TRAVEL_MARGIN 2.0f

// Frames a source waits after landing a chunk. The wall it cut is retired and no
// longer carveable, so this is not what stops it drilling one spot - it is what
// keeps a rider held against a building from spending the chunk pool and the
// mesh arena as fast as the carver can run.
#define ENV_SOURCE_COOLDOWN 20

// Carves a single frame may run. The contact probes are cheap map queries; the
// carve behind them is not.
#define ENV_CARVES_PER_FRAME 1

static int stc_budget;
static int stc_cool[ENV_MAX_PLAYERS];
static int stc_poll_tick;
// Each machine's world displacement as of the previous poll.
static Vec3 stc_was[ENV_MAX_PLAYERS];

void EnvDamage_Reset(void)
{
    memset(stc_cool, 0, sizeof(stc_cool));
    memset(stc_was, 0, sizeof(stc_was));
    stc_budget = 0;
    stc_poll_tick = 0;
}

static float Len(Vec3 *v)
{
    return sqrtf(v->X * v->X + v->Y * v->Y + v->Z * v->Z);
}

// Carve at whatever wall a probe of len along dir runs into. Returns 1 when a
// chunk was actually taken, so a caller sweeping several directions can stop.
static int TryProbe(Vec3 *from, Vec3 *dir, float len, float radius, float force)
{
    Vec3 hit, nrm;

    if (stc_budget <= 0)
        return 0;
    if (!EnvColl_RayHit(from, dir, len, &hit, &nrm))
        return 0;
    if (EnvDestruct_ApplyImpact(&hit, &nrm, radius, force) <= 0)
        return 0;
    stc_budget--;
    return 1;
}

// Carve at a wall the engine reports this body was stopped against. This is the
// strongest evidence of contact there is - the pushback that actually held the
// machine back names the triangle - so it is tried first, and the probes are
// what cover the frames it has nothing to say about. A crater face counts as a
// wall here: it is the surface of a hole the source already made, and cutting
// from one is what takes that hole deeper.
//
// The pushback records one contact per kind per substep as it resolves, and the
// top of the next collision step clears them, so a non-empty wall list is this
// frame's own contact. Every record in it is offered rather than just the first:
// a machine wedged into a crater is stopped by its floor as much as its side,
// and only one of those is worth cutting.
static int TryContact(CollData *cd, Vec3 *body, float radius, float force)
{
    mpCollInfo *ci;
    int i, num;

    if (stc_budget <= 0 || cd == NULL)
        return 0;
    ci = cd->coll_info;
    if (ci == NULL)
        return 0;

    num = ci->wall_rec_num;
    if (num > (int)(sizeof(ci->wall_recs) / sizeof(ci->wall_recs[0])))
        num = (int)(sizeof(ci->wall_recs) / sizeof(ci->wall_recs[0]));

    for (i = 0; i < num; i++)
    {
        mpCollRec *rec = ci->wall_recs[i];
        Vec3 point, nrm, toward;

        if (rec == NULL || rec->wall == NULL || rec->wall_num <= 0)
            continue;
        point = rec->wall->pos;

        toward.X = body->X - point.X;
        toward.Y = body->Y - point.Y;
        toward.Z = body->Z - point.Z;
        if (!EnvColl_WallAt(rec->wall->tri_id, &toward, &nrm))
            continue;
        if (EnvDestruct_ApplyImpact(&point, &nrm, radius, force) <= 0)
            continue;
        stc_budget--;
        return 1;
    }
    return 0;
}

// Probe along a mover's own motion. Nothing is carved by an entity that is not
// closing on the wall, which is what makes a ram read as running into a
// building rather than driving past one.
static int TryTravel(Vec3 *from, Vec3 *vel, float radius, float force)
{
    Vec3 dir;
    float speed = Len(vel);

    if (speed < 1e-4f)
        return 0;
    dir.X = vel->X / speed;
    dir.Y = vel->Y / speed;
    dir.Z = vel->Z / speed;
    return TryProbe(from, &dir, speed + ENV_TRAVEL_MARGIN, radius, force);
}

// A spin is a swing in every direction at once, so it probes outward on the
// horizontal axes instead of along a heading. Which one lands first does not
// matter: only one of them can be facing the wall being leaned on.
static const Vec3 stc_spin_dir[4] =
{
    {  1.0f, 0.0f,  0.0f },
    { -1.0f, 0.0f,  0.0f },
    {  0.0f, 0.0f,  1.0f },
    {  0.0f, 0.0f, -1.0f },
};

static int TrySpin(Vec3 *from, float radius)
{
    int i;

    for (i = 0; i < 4; i++)
        if (TryProbe(from, (Vec3 *)&stc_spin_dir[i], ENV_SPIN_REACH, radius, 1.0f))
            return 1;
    return 0;
}

static int RiderIsSpinning(RiderData *rd)
{
    int want = (rd->kind == RDKIND_METAKNIGHT) ? ENV_SPIN_STATE_MK : ENV_SPIN_STATE;

    return rd->state_idx == want;
}

// Ram strength as a multiple of the base chunk depth: at the threshold a ram
// barely dents, at top speed it drives a chunk twice as deep.
//
// The approach speed is what a ram is, and by the time the poll runs the
// pushback has already resolved the frame and taken it away - a machine driven
// head-on into a building reads as stopped on exactly the frame its contact
// record names the wall. So the previous poll's displacement counts too, and the
// larger of the two decides.
static float MachineRamForce(MachineData *md, Vec3 *was)
{
    float cur = Len(&md->world_velocity);
    float sp = Len(was);
    float f;

    if (cur > sp)
        sp = cur;
    if (md->top_speed_current <= 0.0f || sp < ENV_RAM_FRACTION * md->top_speed_current)
        return 0.0f;
    f = sp / md->top_speed_current;
    if (f > 2.0f)
        f = 2.0f;
    return f;
}

static void PollPlayer(int p, float radius)
{
    GOBJ *rg, *mg;
    RiderData *rd = NULL;
    MachineData *md = NULL;
    Vec3 was;

    if (Ply_GetPKind(p) != PKIND_HMN)
        return;

    rg = Ply_GetRiderGObj(p);
    if (rg != NULL)
        rd = (RiderData *)rg->userdata;
    mg = Ply_GetMachineGObj(p);
    if (mg != NULL)
        md = (MachineData *)mg->userdata;

    // Rolled forward ahead of the cooldown, so the speed a ram is judged on is
    // always the frame before the one being polled.
    was = stc_was[p];
    if (md != NULL)
        stc_was[p] = md->world_velocity;
    else
        memset(&stc_was[p], 0, sizeof(stc_was[p]));

    if (stc_cool[p] > 0)
    {
        stc_cool[p]--;
        return;
    }

    // A quick spin is done from the machine, so the machine is what strikes the
    // wall; the rider is only where the state lives.
    if (env_src_spin && rd != NULL && RiderIsSpinning(rd))
    {
        Vec3 *body = (md != NULL) ? &md->pos : &rd->pos;
        CollData *cd = (md != NULL) ? md->coll_data : rd->coll_data;

        if (TryContact(cd, body, radius, 1.0f) || TrySpin(body, radius))
        {
            stc_cool[p] = ENV_SOURCE_COOLDOWN;
            return;
        }
    }

    if (md != NULL)
    {
        float ram = env_src_machine ? MachineRamForce(md, &was) : 0.0f;

        if (ram > 0.0f)
        {
            // Along whichever of the two carries the approach: on the contact
            // frame itself the machine has already been stopped, and a ray along
            // what is left of its motion points nowhere near the wall.
            Vec3 *along = (Len(&md->world_velocity) >= Len(&was)) ? &md->world_velocity : &was;

            if (TryContact(md->coll_data, &md->pos, radius, ram) ||
                TryTravel(&md->pos, along, radius, ram))
                stc_cool[p] = ENV_SOURCE_COOLDOWN;
        }
        else if (env_src_charge && md->charge_value >= ENV_CHARGE_MIN)
        {
            float force = 1.0f + md->charge_value;

            if (TryContact(md->coll_data, &md->pos, radius, force) ||
                TryProbe(&md->pos, &md->forward, ENV_SPIN_REACH, radius, force))
                stc_cool[p] = ENV_SOURCE_COOLDOWN;
        }
    }
}

// Every enabled source is asked, every frame, whether it has run into a wall.
// The probes are map raycasts, so a carve only ever happens where something
// actually made contact.
void EnvDamage_Poll(void)
{
    float radius = EnvDestruct_CurrentSize();
    int i;
    GOBJ *g;

    stc_budget = ENV_CARVES_PER_FRAME;
    stc_poll_tick++;

    // Rotate who gets first refusal so a rider holding a spin against a wall
    // cannot starve the other players or the projectiles.
    for (i = 0; i < ENV_MAX_PLAYERS; i++)
        PollPlayer((stc_poll_tick + i) % ENV_MAX_PLAYERS, radius);

    if (!env_src_projectile)
        return;

    for (g = (*stc_gobj_lookup)[GAMEPLINK_PROJECTILE]; g != NULL; g = g->next)
    {
        ProjectileData *pd = (ProjectileData *)g->userdata;
        if (pd != NULL)
            TryTravel(&pd->position, &pd->velocity, radius, 1.0f);
    }
    for (g = (*stc_gobj_lookup)[GAMEPLINK_ITEM]; g != NULL; g = g->next)
    {
        ItemData *id = (ItemData *)g->userdata;
        if (id != NULL)
            TryTravel(&id->pos, &id->vel, radius, 1.0f);
    }
}
