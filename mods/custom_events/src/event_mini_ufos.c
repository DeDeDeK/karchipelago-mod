#include <float.h>
#include <math.h>
#include <string.h>

#include "game.h"
#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "stage.h"
#include "event.h"
#include "enemy.h"
#include "item.h"
#include "rider.h"
#include "weapon.h"
#include "yakumono.h"
#include "collision.h"
#include "inline.h"

#include "custom_events.h"
#include "event_mini_ufos.h"

#define UFO_NUM            4
#define UFO_GOBJ_CLASS     216
#define UFO_GX_PRI         2       // after Map_GX (1), whose backdrop draws with no depth test
#define UFO_SCALE          0.155f  // the vanilla saucer is 643 units across, this one 100
#define UFO_SPEED          0.9f    // along the loop: about one lap per event
#define UFO_HOVER          40.0f   // above the highest floor under the saucer
#define UFO_BOB            2.0f
#define UFO_BOB_PERIOD     120.0f
#define UFO_TRAVEL_FRAMES  90      // the arrival and the departure each
#define UFO_TRAVEL_HEIGHT  150.0f  // above the hover, where they begin and end
#define UFO_MIN_SCALE      0.05f   // of UFO_SCALE, while growing in and shrinking out

#define PATH_MAX           400
#define PATH_STEP          10.0f
#define PATH_FOOTPRINT     25.0f   // floor probes around the center, so the saucer clears roofs beside its track
#define PATH_SLOPE         0.5f    // the most the hover rises or falls per unit along the loop
#define PATH_PROBE_TOP     300.0f  // under the garden in the sky
#define PATH_PROBE_BOTTOM  -100.0f

#define DROP_INTERVAL      300
#define DROP_JITTER        60
#define DROP_STAGGER       75      // between the UFOs' first drops
#define DROP_BELOW         8.0f
#define DROP_PITCH         -0.5f   // radians below the horizontal
#define DROP_SPEED         0.6f
#define DROP_BUSY          20      // frames before the item can be picked up

#define SHOT_INTERVAL      180
#define SHOT_JITTER        30
#define SHOT_RANGE         150.0f
#define SHOT_SCALE         0.5f
#define SHOT_DAMAGE        0.5f    // of a full-charge ball's
#define SHOT_SPEED         2.4f
#define SHOT_TURN          0.06f   // share of the direction to the target blended in per frame
#define SHOT_LIFE          150
#define SHOT_AIM_HEIGHT    3.0f
#define SHOT_BELOW         4.0f

typedef struct UFOSlot
{
    GOBJ *gobj;     // NULL = free
    float dist;     // along the loop
    int age;        // frames since the start
    int leave;      // frames into the departure, -1 = not leaving
    int drop_timer;
    int shot_timer;
} UFOSlot;

static UFOSlot ufos[UFO_NUM];
static int ufo_active; // state 2: the UFOs drop and shoot
static int drop_num;
static int shot_num;

// The flight path over the course loop, rebuilt at every City Trial load. Each point
// is the loop's, raised to UFO_HOVER over the floor under it.
static Vec3 path[PATH_MAX];
static int path_num; // 0 = no loop, the event cannot run
static float path_step;
static float path_length;

// A missing loop is permanent and the load runs every round, so the report latches.
static int warned;

// Plasma D's init and post_init read the owner's player through RiderGObj_GetPly, so
// a ball is created with this stand-in as its owner, then cleared. PLY_NUM is no
// player: Ply_PlayFGM skips the firing sound for it, and nothing treats a GObj of
// class 0 as a rider.
static RiderData stand_in_data = {.ply = PLY_NUM};
static GOBJ stand_in = {.userdata = &stand_in_data};

// dist wrapped onto [0, path_length).
static float MiniUFO_Wrap(float dist)
{
    while (dist >= path_length)
        dist -= path_length;
    return dist;
}

// The vanilla UFO's parameter block, which carries its model.
static YakuUFOParam *MiniUFO_GetParam(void)
{
    GrData *gd = (*stc_grobj)->gr_data;
    EventConfigData *cfg = gd->event_config;
    YakumonoTable *table = gd->yakumono;
    if (!cfg || !cfg->bgm_sky || !table)
        return NULL;

    int *idx = cfg->bgm_sky[EVKIND_UFO].event_data;
    if (!idx || *idx < 0 || *idx >= table->spawn_data_count)
        return NULL;
    return table->spawn_data_array[*idx];
}

static HSD_Spline *MiniUFO_GetLoop(void)
{
    GrSplineNode *sn = (*stc_grobj)->gr_data->spline_node;
    if (!sn || !sn->course || !sn->course->loop)
        return NULL;

    GrSplineList *pieces = sn->course->pieces;
    if (!pieces || pieces->num < 1)
        return NULL;
    return pieces->splines[0];
}

// The highest floor under the saucer's footprint, or fallback when there is none.
static float MiniUFO_FloorUnder(float x, float z, float fallback)
{
    static const float probes[][2] = {
        {0.0f, 0.0f},
        {PATH_FOOTPRINT, 0.0f}, {-PATH_FOOTPRINT, 0.0f},
        {0.0f, PATH_FOOTPRINT}, {0.0f, -PATH_FOOTPRINT},
    };

    float top = -FLT_MAX;
    for (int i = 0; i < GetElementsIn(probes); i++)
    {
        Vec3 from = {x + probes[i][0], PATH_PROBE_TOP, z + probes[i][1]};
        Vec3 to = {from.X, PATH_PROBE_BOTTOM, from.Z};
        Vec3 hit;
        if (Raycast_Ground(&from, &to, &hit) >= 0 && hit.Y > top)
            top = hit.Y;
    }
    return top > -FLT_MAX ? top : fallback;
}

static void MiniUFO_RaiseTo(Vec3 *p, float y)
{
    if (p->Y < y)
        p->Y = y;
}

void MiniUFOs_OnCityLoad(void)
{
    path_num = 0;

    HSD_Spline *loop = MiniUFO_GetLoop();
    if (!loop)
    {
        if (!warned)
            OSReport("[MiniUFOs] No course loop on this stage, event disabled\n");
        warned = 1;
        return;
    }

    int num = (int)(loop->total_length / PATH_STEP);
    if (num > PATH_MAX)
        num = PATH_MAX;
    if (num < 3)
        return;

    path_length = loop->total_length;
    path_step = path_length / num;
    for (int i = 0; i < num; i++)
    {
        Vec3 p;
        splArcLengthPoint(&p, loop, i * path_step);
        path[i] = (Vec3){p.X, MiniUFO_FloorUnder(p.X, p.Z, p.Y - UFO_HOVER) + UFO_HOVER, p.Z};
    }

    // Raise each point to within the slope of its neighbours, so the hover climbs
    // ahead of a roof instead of at it. Two rounds carry a rise across the seam.
    float fall = PATH_SLOPE * path_step;
    for (int round = 0; round < 2; round++)
    {
        for (int i = 0; i < num; i++)
            MiniUFO_RaiseTo(&path[i], path[(i + num - 1) % num].Y - fall);
        for (int i = num - 1; i >= 0; i--)
            MiniUFO_RaiseTo(&path[i], path[(i + 1) % num].Y - fall);
    }
    path_num = num;
}

static void MiniUFO_PathPoint(float dist, Vec3 *out)
{
    float f = dist / path_step;
    int i = (int)f % path_num;
    float t = f - (int)f;
    Vec3 *a = &path[i];
    Vec3 *b = &path[(i + 1) % path_num];
    out->X = lerp(a->X, b->X, t);
    out->Y = lerp(a->Y, b->Y, t);
    out->Z = lerp(a->Z, b->Z, t);
}

static int MiniUFO_AimAt(int ply, Vec3 *from, Vec3 *dir)
{
    Vec3 target;
    Ply_GetPosition(ply, &target);
    target.Y += SHOT_AIM_HEIGHT;
    VECSubtract(&target, from, dir);
    if (VECMag(dir) < 0.001f)
        return 0;
    VECNormalize(dir, dir);
    return 1;
}

// PlasmaD_Burst restarts the shrink from the ball's inner joint, the size of a
// full-charge ball, so a smaller ball's ramp is scaled to match. The burst stays where
// it began.
static void MiniUFO_BurstShot(WeaponData *wp)
{
    PlasmaD_Burst(wp);
    wp->kind_scratch.plasma_d.ramp_scale *= SHOT_SCALE;
    wp->kind_scratch.plasma_d.ramp_step *= SHOT_SCALE;
    wp->phys_callback = NULL;
    wp->vel = (Vec3){0.0f, 0.0f, 0.0f};
}

// phys_callback: homes gently on the nearest rider, and bursts at the end of its life.
static void MiniUFO_ShotPhys(void *p)
{
    WeaponData *wp = p;
    if (wp->frame_counter >= SHOT_LIFE)
    {
        MiniUFO_BurstShot(wp);
        return;
    }

    int ply = CustomEvents_NearestRider(&wp->pos, SHOT_RANGE, 0);
    Vec3 to;
    if (ply < 0 || !MiniUFO_AimAt(ply, &wp->pos, &to))
        return;

    Vec3 dir;
    VECNormalize(&wp->vel, &dir);
    dir.X += to.X * SHOT_TURN;
    dir.Y += to.Y * SHOT_TURN;
    dir.Z += to.Z * SHOT_TURN;
    VECNormalize(&dir, &dir);
    VECScale(&dir, &wp->vel, SHOT_SPEED);
}

// envcoll_callback: the full sweep, bursting on any contact. Vanilla's sweeps floors
// only and passes through walls.
static void MiniUFO_ShotEnvColl(void *p)
{
    WeaponData *wp = p;
    Weapon_UpdateEnvColl(wp);
    if (wp->flag_b & WP_FLAGB_ENV_CONTACT)
        MiniUFO_BurstShot(wp);
}

// A full-charge Plasma ball at SHOT_SCALE, ownerless, fired at the nearest rider in
// range. Returns 0 when nobody is in range.
static int MiniUFO_Fire(Vec3 *from)
{
    // Weapon_Create reads the kind data unguarded.
    if (!wp_kind_data[WPKIND_PLASMA_D])
        return 0;

    int ply = CustomEvents_NearestRider(from, SHOT_RANGE, 0);
    Vec3 dir;
    if (ply < 0 || !MiniUFO_AimAt(ply, from, &dir))
        return 0;

    WeaponDesc desc = {
        .kind = WPKIND_PLASMA_D,
        .owner_gobj = &stand_in,
        .owner_gobj2 = &stand_in,
        .pos = *from,
        .forward = {0.0f, 0.0f, 1.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .scale = 1.0f,
        .type_flag = 1,
        .charge = SHOT_DAMAGE,
    };
    GOBJ *g = Weapon_Create(&desc);
    if (!g)
        return 0;

    WeaponData *wp = g->userdata;
    wp->owner_gobj = NULL;
    wp->owner_gobj2 = NULL;

    // The ball ends on its own timer, so PlasmaD_OnHit never shortens its life.
    wp->lifetime = 0;
    VECScale(&dir, &wp->vel, SHOT_SPEED);
    wp->scale *= SHOT_SCALE;
    wp->kind_scratch.plasma_d.ramp_scale *= SHOT_SCALE;
    wp->kind_scratch.plasma_d.ramp_step *= SHOT_SCALE;
    wp->phys_callback = MiniUFO_ShotPhys;
    wp->envcoll_callback = MiniUFO_ShotEnvColl;
    shot_num++;
    return 1;
}

// The UFO's own drop column, kept to the stat-up kinds: the nine up patches and All
// Up. A mod that locks an item kind zeroes its rows, so it never drops.
static int MiniUFO_PickDrop(void)
{
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (!info || !info->item_desc)
        return -1;

    ItemEventSourceDrop *rows = info->item_desc->event_source_drop;
    int num = info->item_desc->event_source_drop_num;
    int total = 0;
    for (int i = 0; i < num; i++)
    {
        if (Item_IsStatUpKind(rows[i].it_kind) && rows[i].chance_ufo > 0)
            total += rows[i].chance_ufo;
    }
    if (total == 0)
        return -1;

    int roll = HSD_Randi(total);
    for (int i = 0; i < num; i++)
    {
        if (!Item_IsStatUpKind(rows[i].it_kind) || rows[i].chance_ufo <= 0)
            continue;
        roll -= rows[i].chance_ufo;
        if (roll < 0)
            return rows[i].it_kind;
    }
    return -1;
}

static void MiniUFO_Drop(Vec3 *from)
{
    int kind = MiniUFO_PickDrop();
    if (kind < 0)
        return;

    Vec3 dir = CustomEvents_RandomHeading();
    Vec3 pos = {from->X, from->Y - DROP_BELOW, from->Z};
    if (CityItem_Throw(kind, ITSPAWN_UFO, &pos, &dir, DROP_BUSY, DROP_PITCH, DROP_SPEED))
        drop_num++;
}

static UFOSlot *MiniUFO_FindSlot(GOBJ *gobj)
{
    for (int i = 0; i < UFO_NUM; i++)
    {
        if (ufos[i].gobj == gobj)
            return &ufos[i];
    }
    return NULL;
}

static void MiniUFO_Think(GOBJ *gobj)
{
    UFOSlot *s = MiniUFO_FindSlot(gobj);
    if (!s)
        return;

    s->dist = MiniUFO_Wrap(s->dist + UFO_SPEED);
    Vec3 pos;
    MiniUFO_PathPoint(s->dist, &pos);
    pos.Y += UFO_BOB * sinf(2.0f * M_PI * (s->age + (s - ufos) * UFO_BOB_PERIOD / UFO_NUM) / UFO_BOB_PERIOD);

    float t = 1.0f;
    if (s->leave >= 0)
    {
        if (++s->leave > UFO_TRAVEL_FRAMES)
        {
            s->gobj = NULL;
            GObj_Destroy(gobj);
            return;
        }
        t = 1.0f - (float)s->leave / UFO_TRAVEL_FRAMES;
    }
    else if (s->age < UFO_TRAVEL_FRAMES)
        t = (float)s->age / UFO_TRAVEL_FRAMES;
    else if (ufo_active)
    {
        if (--s->drop_timer <= 0)
        {
            MiniUFO_Drop(&pos);
            s->drop_timer = RandomInRange(DROP_INTERVAL - DROP_JITTER, DROP_INTERVAL + DROP_JITTER);
        }

        // With nobody in range the UFO fires as soon as someone comes close.
        Vec3 muzzle = {pos.X, pos.Y - SHOT_BELOW, pos.Z};
        if (--s->shot_timer <= 0 && MiniUFO_Fire(&muzzle))
            s->shot_timer = RandomInRange(SHOT_INTERVAL - SHOT_JITTER, SHOT_INTERVAL + SHOT_JITTER);
    }
    s->age++;

    // Arriving and leaving, it travels the last UFO_TRAVEL_HEIGHT easing in and out of
    // the hover, and grows or shrinks with it.
    pos.Y += UFO_TRAVEL_HEIGHT * (1.0f - t) * (1.0f - t);
    float scale = UFO_SCALE * (t > UFO_MIN_SCALE ? t : UFO_MIN_SCALE);

    JOBJ *root = gobj->hsd_object;
    root->trans = pos;
    root->scale = (Vec3){scale, scale, scale};
    JObj_SetMtxDirtySub(root);
    JObj_AnimAll(root);
}

int MiniUFOs_CanStart(void)
{
    return path_num > 0 && MiniUFO_GetParam() != NULL;
}

void MiniUFOs_Start(void)
{
    // A round cut off mid-event leaves dead pointers behind.
    memset(ufos, 0, sizeof(ufos));
    drop_num = 0;
    shot_num = 0;
    ufo_active = 1;

    YakuUFOParam *param = MiniUFO_GetParam();
    GrModelMotionAnim *spin = &param->motion[0];
    float base = HSD_Randf() * path_length;
    for (int i = 0; i < UFO_NUM; i++)
    {
        GOBJ *g = GObj_Create(UFO_GOBJ_CLASS, GAMEPLINK_1, 0);
        JOBJ *root = JObj_LoadJoint(param->model[0]->root);
        GObj_AddObject(g, HSD_OBJKIND_JOBJ, root);
        GObj_AddGXLink(g, JObj_GX, GAMEGX_MAP, UFO_GX_PRI);
        GObj_AddProc(g, MiniUFO_Think, 0);

        JObj_AddAnimAll(root, spin->anim_joint, spin->matanim_joint, NULL);
        JObj_ReqAnimAll(root, 0.0f);
        JObj_SetAllAOBJLoopByFlags(root, ANIMBYFLAGS_ALL);

        ufos[i] = (UFOSlot){
            .gobj = g,
            .dist = MiniUFO_Wrap(base + i * path_length / UFO_NUM),
            .leave = -1,
            .drop_timer = DROP_STAGGER * (i + 1),
            .shot_timer = SHOT_INTERVAL,
        };
    }

    OSReport("[MiniUFOs] %d UFOs launched over a %.0f-unit loop\n", UFO_NUM, path_length);
}

void MiniUFOs_End(void)
{
    if (!ufo_active)
        return;

    ufo_active = 0;
    for (int i = 0; i < UFO_NUM; i++)
    {
        if (ufos[i].gobj)
            ufos[i].leave = 0;
    }
}

void MiniUFOs_End2(void)
{
    for (int i = 0; i < UFO_NUM; i++)
    {
        if (ufos[i].gobj)
            GObj_Destroy(ufos[i].gobj);
    }
    memset(ufos, 0, sizeof(ufos));

    OSReport("[MiniUFOs] Dropped %d items and fired %d shots\n", drop_num, shot_num);
}

void MiniUFOs_Abort(void)
{
    memset(ufos, 0, sizeof(ufos));
    ufo_active = 0;
}
