// Waddle Dee Swarm — custom City Trial event
//
// Waddle Dee State Machine Reference
// ===================================
// Actors start at state 0 (memset). The animation script bytecode drives
// transitions through common states, eventually reaching per-type state 0x0E.
// All common-to-0x0E-to-0x0F transitions happen atomically within priority 1
// (ProcUpdate), so our priority-10 proc never observes state 0x0E.
//
// Common states (shared by all enemy types):
//   0x00-0x08  Normal behavioral  func1=AnimEnter func2=AnimTick func3=AnimExit
//              AnimTick has a death counter (ed+0xA28) that destroys the actor
//              after ~31 frames if state doesn't advance to a per-type state.
//   0x09       Death/despawn      Cleanup, death FX, sound
//   0x0A       Inhaled            Being sucked in by Kirby
//   0x0B       Knockback          Hit reaction, launch trajectory
//   0x0C       Launched/airborne  Spline projectile motion
//   0x0D       Grounded/sliding   Friction deceleration, recovery
//
// Per-type states (Waddle Dee, actor 0x17):
//   State  Anim  func1        func2        func3        func4
//   0x0E   -1    0x80219638   NULL         NULL         NULL
//          Idle. func1 calls SetVisibility, re-inits path following
//          (snaps pos to nearest spline!), then EnemyStateChange to 0x0F.
//   0x0F   14    NULL         NULL         0x802196E0   0x80219704
//          Walk A. func3=ground path walk, func4=orientation+step counter.
//   0x10   15    0x802197AC   0x802197E8   0x802197EC   NULL
//          Walk B variant.
//   0x11   16    0x80219908   0x80219988   0x80219A48   0x80219A84
//          Turn/walk left.
//   0x12   17    0x80219B4C   NULL         0x80219C40   0x80219C64
//          Turn/walk right.
//
// Chase override strategy:
//   We can't intercept the 0x0E→0x0F transition (it completes within
//   priority 1 before our priority-10 proc runs). The path re-init in
//   state 0x0E func1 teleports standalone-spawned actors to the nearest
//   spline. To fix this, we save the intended spawn position (pos_initial)
//   and restore it on the first frame where state >= 0x0E, undoing the
//   teleport. Then we override func1-4 every frame for chase behavior.

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

static GOBJ *test_event_enemies[WADDLE_DEE_MAX_COUNT];
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
// ed->x3c0 is the movement speed parameter (float stored as int field).
static void WaddleDeeChaseGroundSnap(EnemyData *ed)
{
    float scale = *(float *)&ed->x3c0;
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
        if (test_event_enemies[i] == gobj)
            return i;
    return -1;
}

static void WaddleDeeUntrack(GOBJ *gobj)
{
    int slot = WaddleDeeFindSlot(gobj);
    if (slot >= 0)
    {
        test_event_enemies[slot] = NULL;
        fade_timer[slot] = 0;
    }
}

// Debug: only log slot 0 to avoid spam
#define WD_DEBUG_SLOT 0
static int wd_debug_frame;

// GOBJProc (priority 10): installs chase callbacks, checks for hit → despawn.
static void WaddleDeeChaseProc(GOBJ *gobj)
{
    EnemyData *ed = gobj->userdata;
    int is_debug = (test_event_enemies[WD_DEBUG_SLOT] == gobj);

    // Event ended: self-destruct
    if (!swarm_active)
    {
        if (is_debug) OSReport("[WD] destroyed: event ended\n");
        WaddleDeeUntrack(gobj);
        EventActor_Destroy(gobj);
        return;
    }

    // If inhaled/dying, untrack
    if (ed->state == 0x09 || ed->state == 0x0A)
    {
        if (is_debug) OSReport("[WD] untrack: state=0x%02X (dying/inhaled)\n", ed->state);
        WaddleDeeUntrack(gobj);
        return;
    }

    if (is_debug)
    {
        OSReport("[WD] f=%d state=0x%02X pos=(%.1f,%.1f,%.1f) vel=(%.1f,%.1f,%.1f) "
                 "flags=%02X pathflag=%.1f f1=%p f2=%p f3=%p f4=%p\n",
                 wd_debug_frame,
                 ed->state,
                 ed->pos.X, ed->pos.Y, ed->pos.Z,
                 ed->vel.X, ed->vel.Y, ed->vel.Z,
                 *(unsigned char *)((char *)ed + 0xB08),
                 ed->path_active_flag,
                 ed->state_func1, ed->state_func2,
                 ed->state_func3, ed->state_func4);
        wd_debug_frame++;
    }

    // Wait for vanilla init to complete.
    if (ed->state < 0x0E)
        return;

    ed->state_func2 = (void *)WaddleDeeChaseMovement;
    ed->state_func3 = (void *)WaddleDeeChaseGroundSnap;
    ed->state_func4 = (void *)WaddleDeeChaseOrientation;

    int slot = WaddleDeeFindSlot(gobj);

    // Handle fade-out animation
    if (slot >= 0 && fade_timer[slot] > 0)
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
    // TODO: Add custom damage/knockback. Direct Machine_GiveDamage + Machine_EnterHitReaction
    // didn't produce visible results — needs more investigation into the machine hit reaction
    // pipeline and timing. See docs/hurtdata-system.md for the full system overview.
    if (ed->target_player_idx >= 0)
    {
        float dist = EnemyActor_DistToPlayer(ed->target_player_idx, &ed->pos.X);
        if (dist < WADDLE_DEE_HIT_RADIUS && slot >= 0)
        {
            fade_timer[slot] = WADDLE_DEE_FADE_FRAMES;
            fade_scale0[slot] = ed->final_scale;
            return;
        }
    }
}

// Spawn one waddle dee near a random human player. Returns true if spawned.
static int WaddleDeeSpawnOne(void)
{
    // Find an empty slot
    int slot = -1;
    for (int i = 0; i < WADDLE_DEE_MAX_COUNT; i++)
    {
        if (!test_event_enemies[i])
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

    test_event_enemies[slot] = actor;
    GObj_AddProc(actor, WaddleDeeChaseProc, 10);
    if (slot == WD_DEBUG_SLOT)
    {
        EnemyData *ed = actor->userdata;
        wd_debug_frame = 0;
        OSReport("[WD] spawned slot %d at (%.1f,%.1f,%.1f) state=0x%02X pathflag=%.1f\n",
                 slot, ed->pos.X, ed->pos.Y, ed->pos.Z, ed->state, ed->path_active_flag);
    }
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
    // here — pointers may be stale if vanilla already destroyed the enemy
    // (OOB kill, etc.). The chase procs will destroy themselves next frame.
    swarm_active = 0;
    for (int i = 0; i < WADDLE_DEE_MAX_COUNT; i++)
        test_event_enemies[i] = NULL;
}
