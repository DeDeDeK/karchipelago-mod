#include <string.h>

#include "game.h"
#include "hsd.h"
#include "obj.h"
#include "enemy.h"

#include "event_waddle_dee_swarm.h"

#define WADDLE_DEE_MAX_COUNT      10
#define WADDLE_DEE_CHASE_SPEED    0.6f
#define WADDLE_DEE_HIT_RADIUS     1.0f
#define WADDLE_DEE_SPAWN_INTERVAL 20
#define WADDLE_DEE_FADE_FRAMES    20

typedef struct SwarmSlot
{
    GOBJ *gobj;        // NULL = free
    Vec3 saved_pos;    // position at the end of the last chase proc
    int saved_state;   // state at the end of the last chase proc
    int chase_active;  // chase callbacks installed
    int fade_timer;    // >0 = fading out
    float fade_scale0; // final_scale when the fade started
} SwarmSlot;

static SwarmSlot swarm[WADDLE_DEE_MAX_COUNT];
static int spawn_timer;
static int swarm_active;

// func2 (priority 4): velocity toward the nearest rider, CPUs included.
static void WaddleDeeChaseMovement(EnemyData *ed)
{
    // EnemyActor_FindNearestPlayer caps detection range; pre-setting a target
    // bypasses that so the swarm can hunt across the whole map. The distance to an
    // empty slot is FLT_MAX.
    float best_dist = 1e30f;
    int best = -1;
    for (int i = 0; i < 4; i++)
    {
        float d = EnemyActor_DistToPlayer(i, &ed->pos.X);
        if (d < best_dist)
        {
            best_dist = d;
            best = i;
        }
    }
    ed->target_player_idx = best;

    // A non-zero cooldown makes FindNearestPlayer keep our target and just
    // compute chase_direction/orientation from it.
    ed->chase_flag = 0.0f;
    ed->retarget_cooldown = 2;
    EnemyActor_FindNearestPlayer(ed);

    if (ed->target_player_idx >= 0)
    {
        // chase_direction points away from the player.
        ed->vel.X = -ed->chase_direction.X * WADDLE_DEE_CHASE_SPEED;
        ed->vel.Z = -ed->chase_direction.Z * WADDLE_DEE_CHASE_SPEED;
    }

    // GroundSnap owns Y; zeroing here stops gravity accumulating.
    ed->vel.Y = 0.0f;
}

// func3 (priority 5): the ground snap the vanilla per-type states run.
static void WaddleDeeChaseGroundSnap(EnemyData *ed)
{
    EventActor_GroundSnap(ed, ed->param_move_speed);
}

// func4 (priority 6). Must run after GroundSnap, which rewrites up and
// re-orthogonalizes forward, and before EventActor_SharedUpdate builds the
// model matrix.
static void WaddleDeeChaseOrientation(EnemyData *ed)
{
    if (ed->target_player_idx >= 0)
    {
        ed->forward.X = -ed->chase_direction.X;
        ed->forward.Y = 0.0f;
        ed->forward.Z = -ed->chase_direction.Z;
        EventActor_UpdateOrientation(ed);
    }
}

static SwarmSlot *WaddleDeeFindSlot(GOBJ *gobj)
{
    for (int i = 0; i < WADDLE_DEE_MAX_COUNT; i++)
    {
        if (swarm[i].gobj == gobj)
            return &swarm[i];
    }
    return NULL;
}

// GObj proc (priority 10): installs the chase callbacks and handles despawn.
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
        EventActor_Destroy(gobj);
        return;
    }

    // Spawn, knockback, launched and sliding stay vanilla.
    if (ed->state < ENEMYSTATE_PERTYPE)
        return;

    // Entering a per-type state runs its func1, which snaps pos to the nearest
    // spline point; put the actor back where it was. The first time, saved_pos is
    // the spawn position.
    if (!s->chase_active || ed->state != s->saved_state)
        ed->pos = s->saved_pos;
    s->chase_active = 1;
    s->saved_state = ed->state;

    // Vanilla resets these on every state change.
    ed->state_func2 = (void *)WaddleDeeChaseMovement;
    ed->state_func3 = (void *)WaddleDeeChaseGroundSnap;
    ed->state_func4 = (void *)WaddleDeeChaseOrientation;

    if (s->fade_timer > 0)
    {
        if (--s->fade_timer == 0)
        {
            s->gobj = NULL;
            EventActor_Destroy(gobj);
            return;
        }
        ed->final_scale = s->fade_scale0 * (float)s->fade_timer / (float)WADDLE_DEE_FADE_FRAMES;
        ed->vel.X = 0.0f;
        ed->vel.Z = 0.0f;
        return;
    }

    // Contact starts the fade-out.
    if (ed->target_player_idx >= 0 &&
        EnemyActor_DistToPlayer(ed->target_player_idx, &ed->pos.X) < WADDLE_DEE_HIT_RADIUS)
    {
        s->fade_timer = WADDLE_DEE_FADE_FRAMES;
        s->fade_scale0 = ed->final_scale;
        return;
    }

    s->saved_pos = ed->pos;
}

// Frees the slots of actors vanilla destroyed outside the death and inhale states
// (knockback, out of bounds, kill floor), whose chase proc went with them.
static void WaddleDeePruneSlots(void)
{
    for (int i = 0; i < WADDLE_DEE_MAX_COUNT; i++)
    {
        SwarmSlot *s = &swarm[i];
        if (!s->gobj)
            continue;

        int live = 0;
        for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ENEMY]; g; g = g->next)
        {
            if (g == s->gobj)
            {
                live = ((EnemyData *)g->userdata)->kind == ACTORID_WADDLE_DEE;
                break;
            }
        }
        if (!live)
            s->gobj = NULL;
    }
}

static void WaddleDeeSpawnOne(void)
{
    SwarmSlot *s = WaddleDeeFindSlot(NULL);
    if (!s)
        return;

    GOBJ *candidates[5];
    int count = 0;
    for (int i = 0; i < 5; i++)
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
    int ofs_idx = HSD_Randi(sizeof(offsets) / sizeof(offsets[0]));

    EventActorDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.actor_id = ACTORID_WADDLE_DEE;
    desc.position.X = rd->pos.X + offsets[ofs_idx][0];
    desc.position.Y = rd->pos.Y;
    desc.position.Z = rd->pos.Z + offsets[ofs_idx][1];
    desc.forward.Z = 1.0f;
    desc.up.Y = 1.0f;
    desc.scale = 1.0f;
    desc.spawn_index = -1;
    desc.spawn_slot = -1;
    desc.bounds_flag = -1.0f;

    GOBJ *actor = EventActor_Create(&desc);
    if (!actor)
        return;

    *s = (SwarmSlot){.gobj = actor, .saved_pos = desc.position};
    GObj_AddProc(actor, WaddleDeeChaseProc, 10);
}

void WaddleDeeSwarm_Start(void)
{
    Enemy_CheckAndLoad(ACTORID_WADDLE_DEE);

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
