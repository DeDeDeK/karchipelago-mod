// Waddle Dee Swarm - custom City Trial event. Spawns standalone Waddle Dees
// (ACTORID_WADDLE_DEE, 0x17) that chase the nearest human player and fade out
// on contact. Two mechanics make standalone chase AI work:
//
// Spline snap: every vanilla state transition re-runs the actor's func1, which
// snaps its position to the nearest spline point - at priority 1, before our
// proc runs. We undo it at priority 10 by restoring last frame's saved_pos
// whenever ed->state changed since the previous frame.
//
// Chase override: we reinstall func2/func3/func4 every frame for movement,
// ground-snap, and facing (func1 left alone, so walk animations keep playing).
// FindNearestPlayer's range cap is bypassed by pre-setting target_player_idx
// with a non-zero retarget_cooldown.

#include "game.h"
#include "os.h"
#include "inline.h"
#include "enemy.h"

#include "event_waddle_dee_swarm.h"

#define WADDLE_DEE_MAX_COUNT    10
#define WADDLE_DEE_CHASE_SPEED  0.6f
#define WADDLE_DEE_HIT_RADIUS   1.0f
#define WADDLE_DEE_SPAWN_INTERVAL 20  // frames between spawn attempts
#define WADDLE_DEE_FADE_FRAMES  20

static GOBJ *swarm_gobjs[WADDLE_DEE_MAX_COUNT];
static Vec3 saved_pos[WADDLE_DEE_MAX_COUNT];   // last known good position (end of chase proc)
static int saved_state[WADDLE_DEE_MAX_COUNT];  // state at end of last chase proc frame
static int chase_active[WADDLE_DEE_MAX_COUNT]; // 1 once chase callbacks are installed
static int fade_timer[WADDLE_DEE_MAX_COUNT];   // >0 = fading out, 0 = normal
static float fade_scale0[WADDLE_DEE_MAX_COUNT]; // scale at fade start
static int spawn_timer;
static int swarm_active;


// Custom func2 (priority 4, called from EnemyPhysicsProc with EnemyData*).
// Targets nearest player and sets velocity toward them.
// Facing is handled in func4 (after GroundSnap, before model matrix).
static void WaddleDeeChaseMovement(EnemyData *ed)
{
    // Find nearest player with no range limit. EnemyActor_FindNearestPlayer
    // has a max detection range; we bypass it by always pre-setting a target.
    float best_dist = 1e30f;
    int best = -1;
    for (int i = 0; i < 4; i++)
    {
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;
        float d = EnemyActor_DistToPlayer(i, &ed->pos.X);
        if (d < best_dist)
        {
            best_dist = d;
            best = i;
        }
    }
    ed->target_player_idx = best;

    // Let vanilla compute chase_direction and orientation from our target.
    // Cooldown must be non-zero so FindNearestPlayer keeps our target
    // instead of re-evaluating with its range check.
    ed->chase_flag = 0.0f;
    ed->retarget_cooldown = 2;
    EnemyActor_FindNearestPlayer(ed);

    if (ed->target_player_idx >= 0)
    {
        // chase_direction = normalized (enemy - player), pointing away.
        // Negate for velocity toward player.
        ed->vel.X = -ed->chase_direction.X * WADDLE_DEE_CHASE_SPEED;
        ed->vel.Z = -ed->chase_direction.Z * WADDLE_DEE_CHASE_SPEED;
    }

    // GroundSnap handles Y positioning; prevent gravity accumulation
    ed->vel.Y = 0.0f;
}

// Custom func3 (priority 5): snap to ground each frame.
// Same pattern as the vanilla Waddle Dee walk state (0x80219A48).
// ed->param_move_speed is the movement speed parameter.
static void WaddleDeeChaseGroundSnap(EnemyData *ed)
{
    float scale = ed->param_move_speed;
    EventActor_GroundSnap(ed, scale);
}

// Custom func4 (priority 6): set facing direction toward target.
// Runs AFTER GroundSnap (which modifies up and re-orthogonalizes forward),
// but BEFORE EventActor_SharedUpdate computes the model matrix.
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

static int WaddleDeeFindSlot(GOBJ *gobj)
{
    for (int i = 0; i < WADDLE_DEE_MAX_COUNT; i++)
        if (swarm_gobjs[i] == gobj)
            return i;
    return -1;
}


static void WaddleDeeUntrack(GOBJ *gobj)
{
    int slot = WaddleDeeFindSlot(gobj);
    if (slot >= 0)
    {
        swarm_gobjs[slot] = NULL;
        chase_active[slot] = 0;
        fade_timer[slot] = 0;
    }
}

// GOBJProc (priority 10): installs chase callbacks, checks for hit → despawn.
static void WaddleDeeChaseProc(GOBJ *gobj)
{
    EnemyData *ed = gobj->userdata;

    // Event ended: self-destruct
    if (!swarm_active)
    {
        WaddleDeeUntrack(gobj);
        EventActor_Destroy(gobj);
        return;
    }

    // If inhaled/dying, untrack
    if (ed->state == 0x09 || ed->state == 0x0A)
    {
        WaddleDeeUntrack(gobj);
        return;
    }

    // Wait for vanilla init to complete.
    if (ed->state < 0x0E)
        return;

    int slot = WaddleDeeFindSlot(gobj);
    if (slot < 0)
        return;

    if (!chase_active[slot])
    {
        // First frame past init - undo the initial state 0x0E spline snap.
        ed->pos = saved_pos[slot]; // saved_pos was init'd to desc.position
        saved_state[slot] = ed->state;
        chase_active[slot] = 1;
    }
    else if (ed->state != saved_state[slot])
    {
        // Vanilla state transition occurred (animation end → new walk state).
        // The new state's func1 snapped pos to the nearest spline point.
        // Restore from saved_pos (last known good position from end of previous frame).
        ed->pos = saved_pos[slot];
        saved_state[slot] = ed->state;
    }

    ed->state_func2 = (void *)WaddleDeeChaseMovement;
    ed->state_func3 = (void *)WaddleDeeChaseGroundSnap;
    ed->state_func4 = (void *)WaddleDeeChaseOrientation;

    // Handle fade-out animation
    if (fade_timer[slot] > 0)
    {
        fade_timer[slot]--;
        if (fade_timer[slot] <= 0)
        {
            WaddleDeeUntrack(gobj);
            EventActor_Destroy(gobj);
            return;
        }
        // Shrink scale toward 0
        float t = (float)fade_timer[slot] / (float)WADDLE_DEE_FADE_FRAMES;
        ed->final_scale = fade_scale0[slot] * t;
        // Stop moving during fade
        ed->vel.X = 0.0f;
        ed->vel.Z = 0.0f;
        return;
    }

    // Despawn on contact: start fade-out
    if (ed->target_player_idx >= 0)
    {
        float dist = EnemyActor_DistToPlayer(ed->target_player_idx, &ed->pos.X);
        if (dist < WADDLE_DEE_HIT_RADIUS)
        {
            fade_timer[slot] = WADDLE_DEE_FADE_FRAMES;
            fade_scale0[slot] = ed->final_scale;
            return;
        }
    }

    // Save position and state for next frame's spline-snap detection.
    saved_pos[slot] = ed->pos;
    saved_state[slot] = ed->state;
}

// Spawn one waddle dee near a random human player. Returns true if spawned.
static int WaddleDeeSpawnOne(void)
{
    // Find an empty slot
    int slot = -1;
    for (int i = 0; i < WADDLE_DEE_MAX_COUNT; i++)
    {
        if (!swarm_gobjs[i])
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return 0;

    // Pick a random human player to spawn near
    GOBJ *candidates[5];
    int count = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) == PKIND_HMN && Ply_GetRiderGObj(i))
            candidates[count++] = Ply_GetRiderGObj(i);
    }
    if (!count)
        return 0;

    RiderData *rd = candidates[HSD_Randi(count)]->userdata;

    // Spawn at a random offset around the player (~40 units out)
    static const float offsets[][2] = {
        {  0.0f,  40.0f}, { 38.0f,  12.4f}, { 23.5f, -32.4f},
        {-23.5f, -32.4f}, {-38.0f,  12.4f}, { 28.3f,  28.3f},
        {-28.3f,  28.3f}, { 28.3f, -28.3f}, {-28.3f, -28.3f},
        {  40.0f,  0.0f}, {-40.0f,  0.0f},  {  0.0f, -40.0f},
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
        return 0;

    swarm_gobjs[slot] = actor;
    saved_pos[slot] = desc.position;
    chase_active[slot] = 0;
    GObj_AddProc(actor, WaddleDeeChaseProc, 10);
    return 1;
}

void WaddleDeeSwarm_Start(EventCheckData *ev_chk)
{
    Enemy_CheckAndLoad(ACTORID_WADDLE_DEE);
    spawn_timer = 0;
    swarm_active = 1;
}

void WaddleDeeSwarm_Active(EventCheckData *ev_chk)
{
    // Periodically spawn replacements up to max count
    if (++spawn_timer >= WADDLE_DEE_SPAWN_INTERVAL)
    {
        spawn_timer = 0;
        WaddleDeeSpawnOne();
    }
}

void WaddleDeeSwarm_End2(EventCheckData *ev_chk)
{
    // Signal chase procs to self-destruct. Don't call EventActor_Destroy
    // here - pointers may be stale if vanilla already destroyed the enemy
    // (OOB kill, etc.). The chase procs will destroy themselves next frame.
    swarm_active = 0;
}
