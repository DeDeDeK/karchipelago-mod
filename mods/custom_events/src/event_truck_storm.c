#include <string.h>

#include "game.h"
#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "enemy.h"
#include "stage.h"
#include "trigger.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "custom_events.h"
#include "event_truck_storm.h"

#define TRUCK_MAX_COUNT      24
#define TRUCK_SPAWN_INTERVAL 10
#define TRUCK_SPAWN_DIST_MIN 80.0f
#define TRUCK_SPAWN_DIST_MAX 300.0f
#define TRUCK_STRAND_DIST    450.0f // a truck this far from every human is recycled
#define TRUCK_KB_SCALE       4.0f
#define TRUCK_SPEED_SCALE    2.0f
#define TRUCK_FADE_FRAMES    30
#define TRUCK_FADE_STAGGER   90
#define TRUCK_ARCHIVE_BYTES  (128 * 1024) // EmDeeTruckData.dat is 95 KB
#define TRUCK_REGION_NUM     2            // attack regions per enemy HurtData

typedef struct TruckKb
{
    float base;   // base_knockback last written
    float factor; // kb_distance_factor last written
} TruckKb;

typedef struct TruckSlot
{
    GOBJ *gobj;                      // the driver, NULL = free
    TruckKb kb[2][TRUCK_REGION_NUM]; // [driver, cart][region]
    float scale0[2];                 // [driver, cart] final_scale at spawn
    int fading;
    int fade_delay;                  // frames before the shrink starts
    int fade_timer;                  // shrink frames left
} TruckSlot;

static TruckSlot trucks[TRUCK_MAX_COUNT];
static int spawn_timer;
static int storm_active;
static int storm_ending;
static int ramp_reported;
static int heap_at_start;

static TruckSlot *TruckStorm_FindSlot(GOBJ *gobj)
{
    for (int i = 0; i < TRUCK_MAX_COUNT; i++)
    {
        if (trucks[i].gobj == gobj)
            return &trucks[i];
    }
    return NULL;
}

// The driver and the cart it spawns as its child, each with its own HurtData.
static int TruckStorm_GetParts(GOBJ *gobj, EnemyData *parts[2])
{
    EnemyData *ed = gobj->userdata;
    parts[0] = ed;
    parts[1] = ed->child_gobj ? ed->child_gobj->userdata : NULL;
    return parts[1] ? 2 : 1;
}

// The animation script re-arms a region with the archive's values, so a region
// that no longer holds what was last written is scaled again.
static void TruckStorm_BoostKnockback(HurtData *hd, TruckKb *kb)
{
    for (int i = 0; i < hd->region_count && i < TRUCK_REGION_NUM; i++)
    {
        HurtParams *p = &hd->regions[i].params;
        if (p->base_knockback == kb[i].base && p->kb_distance_factor == kb[i].factor)
            continue;

        p->base_knockback *= TRUCK_KB_SCALE;
        p->kb_distance_factor *= TRUCK_KB_SCALE;
        kb[i] = (TruckKb){p->base_knockback, p->kb_distance_factor};
    }
}

static void TruckStorm_DisableHitboxes(HurtData *hd)
{
    for (int i = 0; i < hd->region_count; i++)
        hd->regions[i].state = TRIGGERSTATUS_INACTIVE;
}

// GObj proc at priority 8: after the animation script arms the hitboxes
// (priority 1) and before the machine and rider hit passes read them (9).
static void TruckStorm_TruckProc(GOBJ *gobj)
{
    TruckSlot *s = TruckStorm_FindSlot(gobj);
    if (!s)
        return;

    if (!storm_active)
    {
        s->gobj = NULL;
        EventActorGObj_Destroy(gobj);
        return;
    }

    EnemyData *parts[2];
    int num = TruckStorm_GetParts(gobj, parts);

    if (!s->fading &&
        (storm_ending || CustomEvents_NearestRider(&parts[0]->pos, TRUCK_STRAND_DIST, 1) < 0))
    {
        s->fading = 1;
        s->fade_delay = storm_ending ? HSD_Randi(TRUCK_FADE_STAGGER) : 0;
        s->fade_timer = TRUCK_FADE_FRAMES;
    }

    if (s->fading && s->fade_delay == 0)
    {
        if (--s->fade_timer == 0)
        {
            s->gobj = NULL;
            EventActorGObj_Destroy(gobj);
            return;
        }

        for (int i = 0; i < num; i++)
        {
            parts[i]->final_scale = s->scale0[i] * (float)s->fade_timer / (float)TRUCK_FADE_FRAMES;
            TruckStorm_DisableHitboxes(parts[i]->hurt_data);
        }
        return;
    }

    if (s->fade_delay > 0)
        s->fade_delay--;
    for (int i = 0; i < num; i++)
        TruckStorm_BoostKnockback(parts[i]->hurt_data, s->kb[i]);
}

static void TruckStorm_PruneSlots(void)
{
    for (int i = 0; i < TRUCK_MAX_COUNT; i++)
    {
        if (trucks[i].gobj && !CustomEvents_IsActorLive(trucks[i].gobj, ENEMYKIND_WADDLE_DEE_TRUCK))
            trucks[i].gobj = NULL;
    }
}

// A random range-spline midpoint TRUCK_SPAWN_DIST_MIN-MAX from a random human
// player and no closer than TRUCK_SPAWN_DIST_MIN to any rider.
static int TruckStorm_PickSpawn(Vec3 *out)
{
    int humans[PLY_NUM];
    int count = 0;
    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) == PKIND_HMN && Ply_GetRiderGObj(i))
            humans[count++] = i;
    }
    if (!count)
        return 0;

    int target = humans[HSD_Randi(count)];
    int found = 0;
    int seg_num = Spline_GetCount();
    for (int seg = 0; seg < seg_num; seg++)
    {
        Vec3 pt;
        splGetSplinePoint(&pt, Spline_GetForward(seg), 0.5f);
        if (Enemy_DistToPlayer(target, &pt) > TRUCK_SPAWN_DIST_MAX ||
            CustomEvents_NearestRider(&pt, TRUCK_SPAWN_DIST_MIN, 0) >= 0)
            continue;

        // Reservoir sampling keeps every candidate equally likely.
        if (HSD_Randi(++found) == 0)
            *out = pt;
    }
    return found > 0;
}

static void TruckStorm_SpawnOne(TruckSlot *s)
{
    Vec3 pos;
    if (!TruckStorm_PickSpawn(&pos))
        return;

    // What Enemy_SpawnActor (0x800f13a8) passes, minus the spawn pool.
    EventActorDesc desc;
    CustomEvents_InitActorDesc(&desc, ENEMYKIND_WADDLE_DEE_TRUCK, &pos, 1.0f);
    desc.is_airborne = 1;
    desc.ground_normal.Y = 1.0f;

    GOBJ *actor = EventActor_Create(&desc);
    if (!actor)
        return;

    *s = (TruckSlot){.gobj = actor};
    EnemyData *parts[2];
    int num = TruckStorm_GetParts(actor, parts);
    for (int i = 0; i < num; i++)
        s->scale0[i] = parts[i]->final_scale;

    // The actor's own hit pass runs only while damage_accum_1 < param_hp; the cart
    // skips it already.
    parts[0]->param_hp = 0;

    // The path speed is recomputed from these every frame, so the next frame lifts
    // the speed EventActor_Create set to the new floor.
    parts[0]->param_speed *= TRUCK_SPEED_SCALE;
    parts[0]->param_speed_max *= TRUCK_SPEED_SCALE;
    parts[0]->param_accel *= TRUCK_SPEED_SCALE;

    GObj_AddProc(actor, TruckStorm_TruckProc, ENEMYPRI_8);
}

static int TruckStorm_CountLive(void)
{
    int live = 0;
    for (int i = 0; i < TRUCK_MAX_COUNT; i++)
        live += trucks[i].gobj != NULL;
    return live;
}

// Once per storm, when spawning first stops.
static void TruckStorm_ReportRamp(int heap_free, const char *limit)
{
    if (ramp_reported)
        return;
    ramp_reported = 1;

    int live = TruckStorm_CountLive();
    float per_truck = live ? BytesToKB(heap_at_start - heap_free) / live : 0.0f;
    OSReport("[TruckStorm] Spawning stopped at %d trucks (%s), %.1fkb heap free, ~%.1fkb per truck\n",
             live, limit, BytesToKB(heap_free), per_truck);
}

void TruckStorm_Start(void)
{
    // A round cut off mid-storm leaves dead pointers behind.
    memset(trucks, 0, sizeof(trucks));
    spawn_timer = 0;
    storm_ending = 0;
    ramp_reported = 0;

    // An allocation the heap cannot serve crashes rather than failing the load.
    storm_active = Enemy_GetActorData(ENEMYKIND_WADDLE_DEE_TRUCK) ||
                   OSCheckHeap(HSD_GetHeapID()) >= CUSTOM_EVENTS_HEAP_RESERVE + TRUCK_ARCHIVE_BYTES;
    if (!storm_active)
    {
        OSReport("[TruckStorm] Too little heap to load the trucks, storm skipped\n");
        return;
    }

    Enemy_CheckAndLoad(ENEMYKIND_WADDLE_DEE_TRUCK);
    heap_at_start = OSCheckHeap(HSD_GetHeapID());
}

void TruckStorm_Active(void)
{
    if (!storm_active || ++spawn_timer < TRUCK_SPAWN_INTERVAL)
        return;
    spawn_timer = 0;

    TruckStorm_PruneSlots();
    TruckSlot *s = TruckStorm_FindSlot(NULL);
    int heap_free = OSCheckHeap(HSD_GetHeapID());
    if (!s || heap_free < CUSTOM_EVENTS_HEAP_RESERVE)
    {
        TruckStorm_ReportRamp(heap_free, s ? "heap reserve" : "cap");
        return;
    }
    TruckStorm_SpawnOne(s);
}

// Each truck shrinks away after a random delay, well inside the 300-frame state 3.
void TruckStorm_End(void)
{
    storm_ending = 1;
}

// Only flags the end: vanilla may already have destroyed a tracked truck, so each
// truck proc destroys its own truck on its next run. Also the abort, so the inhale
// wrapper never matches a pointer from a round the scene exit cut off.
void TruckStorm_End2(void)
{
    storm_active = 0;
}

static int TruckStorm_IsInhalable(GOBJ *gobj)
{
    if (storm_active && TruckStorm_FindSlot(gobj))
        return 0;
    return EventActorGObj_IsInhalable(gobj);
}

// The bl EventActorGObj_IsInhalable in Rider_TryStartInhale (0x8019c5ac),
// Rider_InhaleCaptureScan (0x8019c63c) and Rider_CPUScanInhaleTargets (0x8026cfec).
static const int inhalable_calls[] = {0x8019c5e0, 0x8019c6a4, 0x8026d0b8};

void TruckStorm_InstallHooks(void)
{
    for (int i = 0; i < GetElementsIn(inhalable_calls); i++)
        CODEPATCH_REPLACECALL(inhalable_calls[i], TruckStorm_IsInhalable);
}
