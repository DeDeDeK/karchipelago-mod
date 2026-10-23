#include <string.h>

#include "game.h"
#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "stage.h"
#include "event.h"
#include "enemy.h"
#include "weapon.h"
#include "camera.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "custom_events.h"
#include "event_danger_rain.h"
#include "hazard_weapon.h"
#include "rain_dragon.h"
#include "road_spot.h"

#define RAIN_EDGE_MARGIN     0.1f   // share of the road width kept clear at each edge
#define RAIN_DROP_HEIGHT     220.0f // above the floor a drop lands on
#define RAIN_WEAPON_FALL     1.0f   // starting fall speed; the weapons add stage gravity
#define RAIN_WEAPON_LIFETIME 900    // a bomb never expires on its own
#define RAIN_SENSOR_FUSE     480    // a sensor bomb's fuse from its spawn
#define RAIN_SENSOR_SPREAD   300

#define METEOR_SCALE         2.5f  // the event's meteors are 5 to 33
#define METEOR_FALL_SPEED    5.0f
#define METEOR_QUAKE_SCALE   0.25f
#define METEOR_RUMBLE_RADIUS 80.0f

typedef struct RainDesc
{
    const char *label;
    int interval;          // frames between drops
    int max_live;          // drops alive at once
    int weapon;            // the ownerless WeaponKind it drops, -1 for an actor
    int (*prepare)(void);  // an actor's: readies its data at the start, 0 when it is not loaded
    int (*count)(void);    // an actor's: drops alive now
    int (*spawn)(Vec3 *floor, Vec3 *drop); // drop is RAIN_DROP_HEIGHT above floor
} RainDesc;

static int rain = -1;        // the running DangerRainKind, -1 = none
static int next_rain = -1;   // the next start's kind, -1 = random
static int spawn_timer;
static int gordo_spawning;
static float saved_rumble_radius;

// A live rider to lend a Gordo its throw basis.
static GOBJ *DangerRain_DonorRider(void)
{
    for (int i = 0; i < PLY_NUM; i++)
    {
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (Ply_GetPKind(i) != PKIND_NONE && rg)
            return rg;
    }
    return NULL;
}

static int DangerRain_CountOwnerless(WeaponKind kind)
{
    int num = 0;
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_WEAPON]; g; g = g->next)
    {
        WeaponData *wp = g->userdata;
        num += wp->kind == kind && !wp->owner_gobj;
    }
    return num;
}

// HELD would snap the bomb to its missing owner's hand on the first proc. The thrown
// state detonates it on its first contact.
static int DangerRain_SpawnBomb(Vec3 *floor, Vec3 *drop)
{
    WeaponData *wp = HazardWeapon_Create(WPKIND_BOMB, drop, NULL);
    if (!wp)
        return 0;

    Weapon_StateChange(wp, BOMB_STATE_THROWN, 0.0f, 1.0f, 1);
    wp->vel = (Vec3){0.0f, -RAIN_WEAPON_FALL, 0.0f};
    wp->lifetime = RAIN_WEAPON_LIFETIME;
    return 1;
}

// Falls armed until SensorBomb_Land embeds it, then senses until its fuse runs out.
static int DangerRain_SpawnSensor(Vec3 *floor, Vec3 *drop)
{
    WeaponData *wp = HazardWeapon_Create(WPKIND_SENSORBOMB, drop, NULL);
    if (!wp)
        return 0;

    Weapon_StateChange(wp, SENSOR_BOMB_STATE_ARMED_FLYING, 0.0f, 1.0f, 1);
    wp->vel = (Vec3){0.0f, -RAIN_WEAPON_FALL, 0.0f};
    wp->kind_scratch.sensor_bomb.fuse = RAIN_SENSOR_FUSE + HSD_Randi(RAIN_SENSOR_SPREAD);
    return 1;
}

// WeaponGObj_EnterGordoThrownState takes the Gordo's spin axis from its owner's rider,
// so a live rider owns it for the throw and is dropped right after. The throw's state
// change credits the owner with a Gordo attack, which is put back.
static int DangerRain_SpawnGordo(Vec3 *floor, Vec3 *drop)
{
    GOBJ *donor = DangerRain_DonorRider();
    if (!donor)
        return 0;

    PlayerStats *stats = Ply_GetStats(((RiderData *)donor->userdata)->ply);
    int saved_use_num = stats->attack_use_num;
    int saved_attack_num[GetElementsIn(stats->attack_num)];
    memcpy(saved_attack_num, stats->attack_num, sizeof(saved_attack_num));

    WeaponData *wp = HazardWeapon_Create(WPKIND_GORDO, drop, donor);
    if (wp)
    {
        Vec3 vel = {0.0f, -RAIN_WEAPON_FALL, 0.0f};
        gordo_spawning = 1;
        WeaponGObj_EnterGordoThrownState(wp->gobj, &vel, drop);
        gordo_spawning = 0;
        wp->owner_gobj = NULL;
    }

    stats->attack_use_num = saved_use_num;
    memcpy(stats->attack_num, saved_attack_num, sizeof(saved_attack_num));
    return wp != NULL;
}

// Meteor_BehaviorInit reads its fall from the meteor event's globals, so they point
// at this block for the length of the spawn: zone 0 and speed 0, straight down from
// the zone's height above the spawn point.
static MeteorZone meteor_zone = {.height = RAIN_DROP_HEIGHT, .fall_speed = METEOR_FALL_SPEED};
static MeteorSpeed meteor_speed;
static MeteorEventData meteor_event = {.speeds = &meteor_speed, .zones = &meteor_zone};

static int DangerRain_SpawnMeteor(Vec3 *floor, Vec3 *drop)
{
    EventActorDesc desc;
    CustomEvents_InitActorDesc(&desc, ENEMYKIND_METEOR, floor, METEOR_SCALE);
    desc.is_airborne = 1;
    desc.ground_normal.Y = 1.0f;

    int saved_data = *stc_meteor_data;
    MeteorEventData *saved_event = *stc_meteor_event_data;
    *stc_meteor_data = 1;
    *stc_meteor_event_data = &meteor_event;

    GOBJ *g = EventActor_Create(&desc);
    if (g)
        Meteor_BehaviorInit(g->userdata);

    *stc_meteor_data = saved_data;
    *stc_meteor_event_data = saved_event;
    if (!g)
        return 0;

    // Creation hides the meteor three ways.
    EnemyData *ed = g->userdata;
    EventActor_SetVisibility(ed);
    EventActorGObj_EnableRendering(g);
    JObj_ClearFlagsAll(g->hsd_object, JOBJ_HIDDEN);
    return 1;
}

static int DangerRain_MeteorCount(void)
{
    int num = 0;
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ENEMY]; g; g = g->next)
        num += ((EnemyData *)g->userdata)->kind == ENEMYKIND_METEOR;
    return num;
}

static MeteorParams *DangerRain_MeteorParams(void)
{
    EnemyActorData *ad = Enemy_GetActorData(ENEMYKIND_METEOR);
    return ad ? ad->kind_params : NULL;
}

// Meteor_Landing rumbles every player within a radius sized for the event meteor. The
// parameters are the archive's, shared by every meteor, and no other meteor falls
// while another event runs.
static int DangerRain_PrepareMeteor(void)
{
    MeteorParams *mp = DangerRain_MeteorParams();
    if (!mp)
        return 0;
    saved_rumble_radius = mp->rumble_radius;
    mp->rumble_radius = METEOR_RUMBLE_RADIUS;
    return 1;
}

static void DangerRain_RestoreMeteor(void)
{
    MeteorParams *mp = DangerRain_MeteorParams();
    if (mp)
        mp->rumble_radius = saved_rumble_radius;
}

static const RainDesc rains[DANGER_RAIN_NUM] = {
    [DANGER_RAIN_METEOR] = {
        .label = "meteor",
        .interval = 8,
        .max_live = 24,
        .weapon = -1,
        .prepare = DangerRain_PrepareMeteor,
        .count = DangerRain_MeteorCount,
        .spawn = DangerRain_SpawnMeteor,
    },
    [DANGER_RAIN_GORDO] = {
        .label = "Gordo",
        .interval = 6,
        .max_live = 48,
        .weapon = WPKIND_GORDO,
        .spawn = DangerRain_SpawnGordo,
    },
    [DANGER_RAIN_SENSOR_BOMB] = {
        .label = "sensor bomb",
        .interval = 6,
        .max_live = 64,
        .weapon = WPKIND_SENSORBOMB,
        .spawn = DangerRain_SpawnSensor,
    },
    [DANGER_RAIN_BOMB] = {
        .label = "bomb",
        .interval = 4,
        .max_live = 48,
        .weapon = WPKIND_BOMB,
        .spawn = DangerRain_SpawnBomb,
    },
    [DANGER_RAIN_DRAGON] = {
        .label = "dragon",
        .interval = 10,
        .max_live = RAIN_DRAGON_MAX,
        .weapon = -1,
        .prepare = RainDragon_Prepare,
        .count = RainDragon_Count,
        .spawn = RainDragon_Spawn,
    },
};

static int DangerRain_Prepare(const RainDesc *r)
{
    return r->weapon >= 0 ? wp_kind_data[r->weapon] != NULL : r->prepare();
}

static int DangerRain_Count(const RainDesc *r)
{
    return r->weapon >= 0 ? DangerRain_CountOwnerless(r->weapon) : r->count();
}

void DangerRain_Start(void)
{
    int kind = next_rain >= 0 ? next_rain : HSD_Randi(DANGER_RAIN_NUM);
    next_rain = -1;
    spawn_timer = 0;
    gordo_spawning = 0;

    rain = DangerRain_Prepare(&rains[kind]) ? kind : -1;
    if (rain < 0)
    {
        OSReport("[DangerRain] No %s data loaded, rain skipped\n", rains[kind].label);
        return;
    }
    OSReport("[DangerRain] Raining %ss\n", rains[rain].label);
}

void DangerRain_Active(void)
{
    if (rain < 0 || ++spawn_timer < rains[rain].interval)
        return;
    spawn_timer = 0;

    if (DangerRain_Count(&rains[rain]) >= rains[rain].max_live || !CustomEvents_HeapOk())
        return;

    Vec3 floor, drop;
    if (RoadSpot_PickOpenSky(&floor, &drop, RAIN_EDGE_MARGIN, RAIN_DROP_HEIGHT))
        rains[rain].spawn(&floor, &drop);
}

// The meteors and dragons have all landed by now, 300 frames after the last spawn.
void DangerRain_End2(void)
{
    if (rain == DANGER_RAIN_METEOR)
        DangerRain_RestoreMeteor();
    else if (rain == DANGER_RAIN_DRAGON)
        RainDragon_RemoveAll();
    rain = -1;
}

// A cut-off siren can leave a requested kind unconsumed.
void DangerRain_Abort(void)
{
    if (rain == DANGER_RAIN_METEOR)
        DangerRain_RestoreMeteor();
    else if (rain == DANGER_RAIN_DRAGON)
        RainDragon_Forget();
    rain = -1;
    next_rain = -1;
}

void DangerRain_SetNext(int kind)
{
    next_rain = kind;
}

// The large every-view quake Meteor_HitTransition starts is sized for the event meteor.
static void DangerRain_MeteorQuake(u32 id, float amp_right, float amp_up)
{
    if (rain == DANGER_RAIN_METEOR)
    {
        amp_right *= METEOR_QUAKE_SCALE;
        amp_up *= METEOR_QUAKE_SCALE;
    }
    Quake_SetAmplitude(id, amp_right, amp_up);
}

// Gordo_EnforceMaxActive counts every thrown Gordo, whatever its owner, and despawns
// each one the running count puts past its cap. A rain Gordo's own throw skips the cap,
// and a player's throw does not count it.
static void DangerRain_EnforceGordoMax(void)
{
    if (!gordo_spawning)
        HazardWeapon_EnforceMax(WPKIND_GORDO, Gordo_EnforceMaxActive);
}

// The bl Quake_SetAmplitude in Meteor_HitTransition (0x8021e7c4) and the bl
// Gordo_EnforceMaxActive in WeaponGObj_EnterGordoThrownState (0x8022a544).
void DangerRain_InstallHooks(void)
{
    CODEPATCH_REPLACECALL(0x8021e918, DangerRain_MeteorQuake);
    CODEPATCH_REPLACECALL(0x8022a6b0, DangerRain_EnforceGordoMax);
}
