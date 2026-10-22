#include <float.h>
#include <string.h>

#include "game.h"
#include "hsd.h"
#include "obj.h"
#include "enemy.h"
#include "inline.h"

#include "custom_events.h"
#include "event_waddle_dee_swarm.h"

#define WADDLE_DEE_MAX_COUNT      10
#define WADDLE_DEE_SPAWN_INTERVAL 20
#define WADDLE_DEE_FADE_FRAMES    20

#define WADDLE_DEE_HOP_RANGE      15.0f
// The hop animation runs 27 frames, off the ground from 4 to 25.
#define WADDLE_DEE_HOP_FRAMES     27
#define WADDLE_DEE_HOP_TAKEOFF    4
#define WADDLE_DEE_HOP_LANDING    25
#define WADDLE_DEE_LUNGE_SCALE    1.5f // airborne hop speed over the target's top speed

// Vanilla's hitbox deals 3 damage and 25 knockback.
#define WADDLE_DEE_DAMAGE         8
#define WADDLE_DEE_KNOCKBACK      60.0f

typedef struct SwarmSlot
{
    GOBJ *gobj;        // NULL = free
    Vec3 spawn_pos;
    int chasing;       // chase callbacks installed
    int hop_frame;     // frames into the current hop
    int fade_timer;    // >0 = fading out
    float fade_scale0; // final_scale when the fade started
} SwarmSlot;

static SwarmSlot swarm[WADDLE_DEE_MAX_COUNT];
static int spawn_timer;
static int swarm_active;

static SwarmSlot *WaddleDeeFindSlot(GOBJ *gobj)
{
    for (int i = 0; i < WADDLE_DEE_MAX_COUNT; i++)
    {
        if (swarm[i].gobj == gobj)
            return &swarm[i];
    }
    return NULL;
}

// The cruise cap of the player's machine, or their walk cap on foot.
static float WaddleDeeTargetSpeed(int ply)
{
    GOBJ *rg = Ply_GetRiderGObj(ply);
    if (!rg)
        return 0.0f;
    RiderData *rd = rg->userdata;
    if (rd->machine_gobj)
        return ((MachineData *)rd->machine_gobj->userdata)->top_speed_ground;
    return rd->walk_speed_max;
}

// Zero while fading out and through the hop's crouch and landing.
static float WaddleDeeChaseSpeed(EnemyData *ed)
{
    SwarmSlot *s = WaddleDeeFindSlot(ed->gobj);
    if (!s || s->fade_timer > 0)
        return 0.0f;
    float speed = WaddleDeeTargetSpeed(ed->target_ply);
    if (ed->state != WADDLE_DEE_STATE_HOP)
        return speed;
    if (s->hop_frame < WADDLE_DEE_HOP_TAKEOFF || s->hop_frame >= WADDLE_DEE_HOP_LANDING)
        return 0.0f;
    return speed * WADDLE_DEE_LUNGE_SCALE;
}

// phys_cb (priority 4): velocity toward the nearest rider, CPUs included.
// EventActor_FindNearestPlayer only looks within detect_range (50), far too short for
// a swarm.
static void WaddleDeeChaseMovement(EnemyData *ed)
{
    ed->target_ply = CustomEvents_NearestRider(&ed->pos, FLT_MAX, 0);

    float speed = 0.0f;
    if (ed->target_ply >= 0)
    {
        // turn_to points from the target to the actor, as vanilla's targeting fills it.
        Vec3 target;
        Ply_GetPosition(ed->target_ply, &target);
        Vec3 away = {ed->pos.X - target.X, 0.0f, ed->pos.Z - target.Z};
        if (VECMag(&away) > 0.0001f)
            VECNormalize(&away, &ed->turn_to);
        speed = WaddleDeeChaseSpeed(ed);
    }
    ed->vel.X = -ed->turn_to.X * speed;
    ed->vel.Z = -ed->turn_to.Z * speed;

    // GroundSnap owns Y; zeroing here stops gravity accumulating.
    ed->vel.Y = 0.0f;
}

// envcoll_cb (priority 5): drops the actor onto the ground below it.
static void WaddleDeeChaseGroundSnap(EnemyData *ed)
{
    EventActor_GroundSnap(ed, ed->param_ground_clearance);
}

// pri6_cb (priority 6). Runs after GroundSnap, which rewrites up, and before
// EventActor_SharedUpdate builds the model matrix from forward and up, so forward
// has to be a unit vector perpendicular to up.
static void WaddleDeeChaseOrientation(EnemyData *ed)
{
    if (ed->target_ply < 0)
        return;

    Vec3 fwd = {-ed->turn_to.X, 0.0f, -ed->turn_to.Z};
    float d = VECDotProduct(&fwd, &ed->up);
    fwd.X -= d * ed->up.X;
    fwd.Y -= d * ed->up.Y;
    fwd.Z -= d * ed->up.Z;
    if (VECMag(&fwd) > 0.0001f)
        VECNormalize(&fwd, &ed->forward);
    EventActor_UpdateGravity(ed);
}

// Whether the hitbox has struck a rider, or the machine a mounted rider is hit
// through. A region lists each victim it strikes for its rehit interval.
static int WaddleDeeLandedHit(EnemyData *ed)
{
    HurtData *hd = ed->hurt_data;
    for (int i = 0; i < PLY_NUM; i++)
    {
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;

        RiderData *rd = rg->userdata;
        HurtData *victim = rd->machine_gobj ? MachineGObj_GetHurtData(rd->machine_gobj) : rd->hurt_data;
        for (int r = 0; r < hd->region_count; r++)
        {
            if (Hit_IsVictimRecorded(victim, &hd->regions[r]))
                return 1;
        }
    }
    return 0;
}

// The animation script arms the hitbox at vanilla strength on every state change.
static void WaddleDeeTuneHitbox(HurtData *hd, int spent)
{
    for (int i = 0; i < hd->region_count; i++)
    {
        HitRegion *r = &hd->regions[i];
        if (spent)
        {
            r->state = TRIGGERSTATUS_INACTIVE;
            continue;
        }
        r->params.base_damage = WADDLE_DEE_DAMAGE;
        r->params.base_knockback = WADDLE_DEE_KNOCKBACK;
    }
}

static void WaddleDeeUpdateHop(SwarmSlot *s, EnemyData *ed)
{
    if (ed->state == WADDLE_DEE_STATE_HOP)
    {
        if (++s->hop_frame >= WADDLE_DEE_HOP_FRAMES)
            EventActor_ChangeState(ed, WADDLE_DEE_STATE_PATROL, 0, 0.0f, 1.0f);
    }
    else if (ed->target_ply >= 0 &&
             Enemy_DistToPlayer(ed->target_ply, &ed->pos) < WADDLE_DEE_HOP_RANGE)
    {
        EventActor_ChangeState(ed, WADDLE_DEE_STATE_HOP, 0, 0.0f, 1.0f);
        s->hop_frame = 0;
    }
}

// GObj proc at priority 2, right after the state machine (1). Whatever changed the
// state this frame, it follows before any callback runs, so vanilla's per-type callbacks
// never get a frame and the hit passes (priority 9) see the retuned hitbox.
static void WaddleDeeChaseProc(GOBJ *gobj)
{
    SwarmSlot *s = WaddleDeeFindSlot(gobj);
    if (!s)
        return;

    EnemyData *ed = gobj->userdata;

    // Vanilla finishes a dying or inhaled actor itself.
    if (ed->state == ENEMYSTATE_DEATH || ed->state == ENEMYSTATE_INHALED)
    {
        s->gobj = NULL;
        return;
    }

    if (!swarm_active)
    {
        s->gobj = NULL;
        EventActorGObj_Destroy(gobj);
        return;
    }

    // The spawn state and the common ones (knockback and what follows it) stay vanilla.
    if (ed->state < WADDLE_DEE_STATE_PATROL)
        return;

    // The spawn state's anim_cb snapped the actor onto the nearest stage spline.
    if (!s->chasing)
    {
        ed->pos = s->spawn_pos;
        s->chasing = 1;
    }

    if (s->fade_timer > 0)
    {
        if (--s->fade_timer == 0)
        {
            s->gobj = NULL;
            EventActorGObj_Destroy(gobj);
            return;
        }
        ed->final_scale = s->fade_scale0 * (float)s->fade_timer / (float)WADDLE_DEE_FADE_FRAMES;
    }
    // A Dee is spent once it lands a hit.
    else if (WaddleDeeLandedHit(ed))
    {
        s->fade_timer = WADDLE_DEE_FADE_FRAMES;
        s->fade_scale0 = ed->final_scale;
    }
    else
        WaddleDeeUpdateHop(s, ed);

    WaddleDeeTuneHitbox(ed->hurt_data, s->fade_timer > 0);

    // Every state change installs the state's own callbacks. The hop's anim_cb would
    // move on to the sideways dash when the animation ends.
    ed->anim_cb = NULL;
    ed->phys_cb = WaddleDeeChaseMovement;
    ed->envcoll_cb = WaddleDeeChaseGroundSnap;
    ed->pri6_cb = WaddleDeeChaseOrientation;
}

// Frees the slots of actors vanilla destroyed outside the death and inhale states
// (knockback, out of bounds, kill floor), whose chase proc went with them.
static void WaddleDeePruneSlots(void)
{
    for (int i = 0; i < WADDLE_DEE_MAX_COUNT; i++)
    {
        if (swarm[i].gobj && !CustomEvents_IsActorLive(swarm[i].gobj, ENEMYKIND_WADDLE_DEE))
            swarm[i].gobj = NULL;
    }
}

static void WaddleDeeSpawnOne(void)
{
    SwarmSlot *s = WaddleDeeFindSlot(NULL);
    if (!s)
        return;

    GOBJ *candidates[PLY_NUM];
    int count = 0;
    for (int i = 0; i < PLY_NUM; i++)
    {
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (rg && Ply_GetPKind(i) == PKIND_HMN)
            candidates[count++] = rg;
    }
    if (!count)
        return;

    RiderData *rd = candidates[HSD_Randi(count)]->userdata;

    // ~40 units out from the player.
    static const float offsets[][2] = {
        {  0.0f,  40.0f}, { 38.0f,  12.4f}, { 23.5f, -32.4f},
        {-23.5f, -32.4f}, {-38.0f,  12.4f}, { 28.3f,  28.3f},
        {-28.3f,  28.3f}, { 28.3f, -28.3f}, {-28.3f, -28.3f},
        { 40.0f,   0.0f}, {-40.0f,   0.0f}, {  0.0f, -40.0f},
    };
    int ofs_idx = HSD_Randi(GetElementsIn(offsets));

    Vec3 pos = {rd->pos.X + offsets[ofs_idx][0], rd->pos.Y, rd->pos.Z + offsets[ofs_idx][1]};
    EventActorDesc desc;
    CustomEvents_InitActorDesc(&desc, ENEMYKIND_WADDLE_DEE, &pos, 1.0f);

    GOBJ *actor = EventActor_Create(&desc);
    if (!actor)
        return;

    *s = (SwarmSlot){.gobj = actor, .spawn_pos = desc.position};
    GObj_AddProc(actor, WaddleDeeChaseProc, ENEMYPRI_ANIM + 1);
}

void WaddleDeeSwarm_Start(void)
{
    Enemy_CheckAndLoad(ENEMYKIND_WADDLE_DEE);

    // A round cut off mid-swarm leaves dead pointers behind.
    memset(swarm, 0, sizeof(swarm));
    spawn_timer = 0;
    swarm_active = 1;
}

void WaddleDeeSwarm_Active(void)
{
    if (++spawn_timer < WADDLE_DEE_SPAWN_INTERVAL)
        return;

    spawn_timer = 0;
    WaddleDeePruneSlots();
    WaddleDeeSpawnOne();
}

// Only flags the end: vanilla may already have destroyed a tracked actor, so each
// chase proc destroys its own actor on its next run.
void WaddleDeeSwarm_End2(void)
{
    swarm_active = 0;
}
