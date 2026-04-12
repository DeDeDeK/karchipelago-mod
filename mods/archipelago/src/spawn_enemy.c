#include "os.h"
#include "game.h"
#include "inline.h"
#include "enemy.h"
#include "event.h"
#include "code_patch/code_patch.h"

#include "spawn_enemy.h"

// Null-safe replacement for EventActor_GetParentScale (0x802049b8).
// Original crashes when parent_gobj is null (standalone spawns have no parent).
static double EventActor_GetParentScale_Safe(GOBJ *gobj)
{
    if (!gobj)
        return 1.0;
    EnemyData *ed = gobj->userdata;
    return (double)ed->scale;
}

// Null-safe replacement for splArcLengthPoint (0x80415958).
// Original calls splArcLengthGetParameter then splGetSplinePoint.
// Crashes when spline data pointer is null (standalone-spawned actors have no path).
static void splArcLengthPoint_Safe(Vec3 *output, void *spline)
{
    if (!spline)
    {
        output->X = 0.0f;
        output->Y = 0.0f;
        output->Z = 0.0f;
        return;
    }
    float param = splArcLengthGetParameter(spline);
    splGetSplinePoint(output, spline, param);
}

// Fake event data for Meteor_BehaviorInit.
// BehaviorInit reads zone/speed from stc_meteor_data/stc_meteor_event_data.
// Without valid data, it computes NaN position and zero velocity. With this
// struct, BehaviorInit computes: pos.Y = initial + 0, vel.Y = -zone_speed.
// Internal pointers and zone_speed set in OnBoot.
static struct
{
    int dummy;                  // +0x00
    int speed_table_ptr;        // +0x04: → speed_entry
    int dummy2;                 // +0x08
    int zone_table_ptr;         // +0x0C: → zone_entry
    // speed_entry (8 bytes):
    float speed_unused;         // +0x10
    float speed_angle_degrees;  // +0x14: 0.0 = straight down
    // zone_entry (12 bytes):
    float zone_offset;          // +0x18: 0.0 = no Y offset from spawn pos
    float zone_speed;           // +0x1C: fall speed (set in OnBoot)
    float zone_angle;           // +0x20: 0.0 = straight down
} s_fake_event_data;

#define METEOR_FALL_SPEED     5.0f
#define METEOR_LANDING_FRAMES 210 // 3.5 seconds for impact effects to play out

void SpawnEnemy_OnBoot(void)
{
    CODEPATCH_REPLACEFUNC(EventActor_GetParentScale, EventActor_GetParentScale_Safe);
    CODEPATCH_REPLACEFUNC(splArcLengthPoint, splArcLengthPoint_Safe);

    // Set up fake event data so BehaviorInit produces correct position and velocity.
    s_fake_event_data.speed_table_ptr = (int)&s_fake_event_data.speed_unused;
    s_fake_event_data.zone_table_ptr = (int)&s_fake_event_data.zone_offset;
    s_fake_event_data.zone_speed = METEOR_FALL_SPEED;
}

// GOBJProc callback: counts down EnemyData.lifetime_counter and destroys the actor when it reaches 0.
static void ActorDespawnProc(GOBJ *gobj)
{
    EnemyData *ed = gobj->userdata;
    if (--ed->lifetime_counter <= 0)
        EventActor_Destroy(gobj);
}

// GOBJProc (priority 20): destroy after impact effects play out.
// Vanilla meteors are cleaned up by the meteor event system, not individually.
// State 16 func3 never calls EventActor_Destroy for standalone spawns.
// We destroy after a timer once state 16 (impact) is reached.
static void MeteorDespawnProc(GOBJ *gobj)
{
    EnemyData *ed = gobj->userdata;
    ed->lifetime_counter++;

    if (ed->state == 16)
    {
        if (ed->spawn_index == 0)
            ed->spawn_index = ed->lifetime_counter; // record frame of impact

        if (ed->lifetime_counter - ed->spawn_index >= METEOR_LANDING_FRAMES)
        {
            OSReport("[SpawnEnemy] Meteor[%d]: despawn after landing\n", ed->lifetime_counter);
            // Vanilla state 16 func3 runs these before destroying. Cleans up
            // collision sphere (xB74) and VFX handles that persist otherwise.
            EventActor_CleanupCollisionSphere(ed);
            EventActor_CleanupVfxA3C(ed);
            EventActor_CleanupVfxA40(ed);
            EventActor_Destroy(gobj);
        }
    }
}

GOBJ *SpawnEnemy_Random(GOBJ *machine_gobj, int use_splines)
{
    MachineData *md = machine_gobj->userdata;
    ActorID id = HSD_Randi(ACTORID_NUM);
    Enemy_CheckAndLoad(id);

    EventActorDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.actor_id = id;
    desc.position.X = md->pos.X + md->forward.X * 30.0f;
    desc.position.Y = md->pos.Y + md->forward.Y * 30.0f;
    desc.position.Z = md->pos.Z + md->forward.Z * 30.0f;
    desc.forward.Z = 1.0f;
    desc.up.Y = 1.0f;
    desc.scale = 1.0f;
    desc.spawn_index = -1;
    desc.spawn_slot = -1;
    desc.bounds_flag = -1.0f;

    GOBJ *actor = EventActor_Create(&desc);
    if (actor)
    {
        EnemyData *ed = actor->userdata;
        ed->parent_gobj = machine_gobj;
        ed->lifetime_counter = 600; // ~10 seconds at 60fps
        GObj_AddProc(actor, ActorDespawnProc, 0x14);

        if (use_splines)
        {
            ed->spline_path_ready = 1;
            ed->spline_direction = 1;
            ed->path_active_flag = -1.0f;
            EnemyPath_Init(ed);
        }
    }

    OSReport("[SpawnEnemy] Spawned actor ID 0x%02X GOBJ=%p at (%.1f, %.1f, %.1f) splines=%d\n",
             id, actor, desc.position.X, desc.position.Y, desc.position.Z, use_splines);
    return actor;
}

// Spawn a meteor above a single player.
// Saves/restores meteor event globals around creation so an active meteor event
// is not corrupted. Calls Meteor_BehaviorInit to enter state 15 (physics-driven
// falling) with correct velocity, then manually enables rendering since we skip
// state 14's boundary check which normally handles that.
static GOBJ *SpawnMeteorOnPlayer(int ply_idx)
{
    GOBJ *rg = Ply_GetRiderGObj(ply_idx);
    if (!rg)
        return NULL;

    RiderData *rd = rg->userdata;

    Enemy_CheckAndLoad(ACTORID_METEOR);

    // Lead the target: offset spawn by ~80 frames of current velocity so the
    // meteor lands on a moving player (400 / METEOR_FALL_SPEED=5.0 = 80 frames).
    EventActorDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.actor_id = ACTORID_METEOR;
    desc.position.X = rd->pos.X + rd->self_vel.X * 80.0f;
    desc.position.Y = rd->pos.Y + 400.0f;
    desc.position.Z = rd->pos.Z + rd->self_vel.Z * 80.0f;
    desc.forward.Z = 1.0f;
    desc.up.Y = 1.0f;
    desc.scale = 1.0f;
    desc.spawn_index = -1;
    desc.spawn_slot = -1;
    desc.bounds_flag = -1.0f;

    // Save real meteor event globals (may be live during meteor event)
    volatile int saved_meteor_data = *stc_meteor_data;
    volatile int saved_meteor_event_data = *stc_meteor_event_data;

    // Point at fake data so BehaviorInit reads valid zone_speed
    *stc_meteor_data = 1;
    *stc_meteor_event_data = (int)&s_fake_event_data;

    GOBJ *meteor = EventActor_Create(&desc);
    if (!meteor)
    {
        *stc_meteor_data = saved_meteor_data;
        *stc_meteor_event_data = saved_meteor_event_data;
        return NULL;
    }

    EnemyData *ed = meteor->userdata;

    // BehaviorInit transitions to state 15 with physics velocity (vel.Y = -speed).
    // State 14's animation-driven motion doesn't work for standalone spawns.
    Meteor_BehaviorInit(ed);

    // Restore real globals immediately after BehaviorInit reads them
    *stc_meteor_data = saved_meteor_data;
    *stc_meteor_event_data = saved_meteor_event_data;

    // BehaviorInit disables rendering. Clear all visibility flags.
    EventActor_EnableRendering(meteor);
    *((u8 *)&ed->render_flags) &= ~0x80;
    // Also clear JOBJ_HIDDEN on the model's JObj tree.
    JOBJ *root_jobj = (JOBJ *)meteor->hsd_object;
    if (root_jobj)
        JObj_ClearFlagsAll(root_jobj, JOBJ_HIDDEN);

    ed->lifetime_counter = 0; // frame counter for MeteorDespawnProc
    ed->spawn_index = 0; // impact frame timestamp (0 = not yet impacted)

    GObj_AddProc(meteor, MeteorDespawnProc, 0x14);

    OSReport("[SpawnEnemy] Meteor trap: spawned above player %d at (%.1f, %.1f, %.1f)\n",
             ply_idx + 1, desc.position.X, desc.position.Y, desc.position.Z);
    return meteor;
}

int SpawnEnemy_MeteorTrap(void)
{
    int spawned = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        if (SpawnMeteorOnPlayer(i))
            spawned++;
    }
    return spawned > 0;
}
