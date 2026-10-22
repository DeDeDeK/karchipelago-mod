// Tornado for custom_weather: a funnel wanders City Trial on a random path, drawing
// loose items, breakable props and parked machines into an orbit around its core and
// dragging at ridden machines that stray inside.

#include <string.h>

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "camera.h"
#include "effect.h"
#include "item.h"
#include "machine.h"
#include "yakumono.h"
#include "collision.h"
#include "inline.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

// Defaults applied when a preset leaves the matching TornadoDef field 0.
#define TORN_DEF_COUNT     2
#define TORN_DEF_DURATION  1500    // 25s at 60fps
#define TORN_DEF_SIZE      1.0f
#define TORN_DEF_STRENGTH  1.0f
#define TORN_DEF_SPEED     2.2f    // world units/frame the funnel wanders

// Funnel geometry at size 1.0, world units. The core is the tight column debris
// settles into; influence reaches well past the visible swirl so things drift in
// before they are visibly touched.
#define TORN_CORE_RADIUS   30.0f
#define TORN_INFLUENCE    240.0f   // horizontal capture radius
#define TORN_HEIGHT       272.0f   // how far up the funnel carries things
#define TORN_BELOW         80.0f   // capture reach below the funnel's ground point

// The whirlwind model is authored along its local +Z: the narrow mouth end sits at the
// origin and the swirl flares out to a radius of 8.07 at z = 9.95. The funnel maps that
// axis onto world +Y, so the flare becomes the top of the tornado.
#define TORN_MODEL_LENGTH  9.95f
#define TORN_MODEL_RADIUS  8.07f
#define TORN_MODEL_WIDTH   0.66f   // visible funnel radius as a fraction of the capture reach
#define TORN_MODEL_SPIN    0.28f   // radians/frame the funnel turns about its own axis

// The funnel forms and ropes out over TORN_FADE_FRAMES at each end of its life by
// narrowing the column; its height stays.
#define TORN_FADE_FRAMES     120   // 2s at 60fps, each way
#define TORN_FADE_MIN_WIDTH  0.02f

// Orbit of a captured target. Angular speed is derived from a constant tangential
// speed so the outer edge does not whip around faster than the core, and is capped
// so a target that reaches the middle does not spin into a blur.
#define TORN_TANGENT       9.0f    // tangential world units/frame
#define TORN_SPIN_MAX      0.24f   // radians/frame cap
#define TORN_INHALE_RATE   0.05f   // fraction of the excess radius shed per frame
#define TORN_LIFT          3.0f    // world units/frame a captured target climbs

// Drawn once when a target is claimed and then fixed. The orbit is otherwise a pure
// function of position, so everything at the same radius would converge on one rigid
// spiral; re-rolling per frame would instead read as jitter.
#define TORN_VARY_SPIN_LO  0.75f
#define TORN_VARY_SPIN_HI  1.35f
#define TORN_VARY_RING_LO  0.55f   // ring a target settles into, as a multiple of the core
#define TORN_VARY_RING_HI  2.40f
#define TORN_VARY_LIFT_LO  0.60f
#define TORN_VARY_LIFT_HI  1.45f
#define TORN_VARY_WOB_LO   0.010f  // radians/frame of the ring's radial breathing
#define TORN_VARY_WOB_HI   0.035f
#define TORN_VARY_WOB_AMP  0.14f   // breathing depth as a fraction of the ring radius

// A ridden machine is pushed, never possessed: these are per-frame accelerations added
// to its own velocity, so a fast machine can drive or boost its way back out.
#define TORN_RIDER_PULL    0.55f   // inward accel at the funnel center
#define TORN_RIDER_SPIN    0.85f   // tangential accel
#define TORN_RIDER_LIFT    0.40f   // upward accel

// Props are carried for a moment and then torn apart.
#define TORN_YAKU_HOLD     110     // frames a claimed prop orbits before it breaks

// Camera shake, felt from further out than the funnel actually reaches. Priority 1 is
// above the machine speed shake (0) and below the medium and large event quakes and the
// earthquake (2).
#define TORN_SHAKE_REACH    1.6f   // multiple of the influence radius
#define TORN_SHAKE_AMP      2.4f   // world units of camera offset at the center
#define TORN_SHAKE_PRIORITY 1

// The funnel steers toward waypoints drawn over a disc around the city's centre.
#define TORN_PLAY_FRACTION 0.68f   // play radius as a fraction of the OOB half-extent
#define TORN_STEER         0.045f  // per-frame blend of the heading toward the waypoint
#define TORN_TURN          0.030f  // radians/frame of random jitter on top of the steer
#define TORN_WAYPOINT_HIT  150.0f  // distance at which a waypoint counts as reached

#define TORN_MAX_ITEMS     96
#define TORN_MAX_YAKU      128
#define TORN_MAX_MACHINES  24
#define TORN_MAX_PARENTS   16

// One Effect_SpawnSync attempt per this many frames, so a spawn that never adopts
// cannot fire one per frame for the whole tornado.
#define TORN_SPAWN_RETRY   30

// p_link 1 freezes with the stage tick under the match pause and the hitstops.
#define TORN_GOBJ_CLASS    210
#define TORN_GOBJ_PLINK    GAMEPLINK_1
// The last proc priority, after item and machine physics (4), the item ground snap (5)
// and PlyCamGObj_Think (13), any of which would overwrite the orbit's writes.
#define TORN_PROC_PRI      23

static int stc_active = 0;

// The live preset's TornadoDef with the module defaults and menu overrides folded in,
// latched by Tornado_SetActive. Size is applied through the funnel geometry.
static int   stc_count;
static int   stc_duration;
static float stc_strength;
static float stc_speed;
static float stc_core;
static float stc_reach;
static float stc_top;

static WeatherSchedule stc_schedule;

// The live funnel.
static int   stc_live = 0;
static int   stc_frames_left = 0;
static float stc_cx = 0.0f, stc_cz = 0.0f;  // axis position
static float stc_ground_y = 0.0f;           // funnel base
static float stc_dirx = 0.0f, stc_dirz = 1.0f;  // unit wander heading
static float stc_wpx = 0.0f, stc_wpz = 0.0f;    // waypoint being crossed to
static float stc_spin = 0.0f;               // funnel model's roll about its own axis
static int   stc_efgroup = -1;              // the funnel's own effect group
static GOBJ *stc_model = NULL;              // the whirlwind effect GObj
static int   stc_model_ready = 0;           // its anim loop is armed
static GOBJ *stc_proc_gobj = NULL;          // runs TornadoApply; freed with the scene heap
static int   stc_spawn_cd = 0;              // frames until the next spawn attempt
static u32   stc_shake_src[CM_CAMERA_MAX];  // per player view, 0 = no quake source

typedef struct OrbitVary
{
    float spin;    // multiplier on angular speed
    float ring;    // radius it settles into, as a multiple of the core radius
    float lift;    // multiplier on climb rate
    float wobble;  // radians/frame the ring breathes
    float phase;   // where in that breath it currently is
} OrbitVary;

typedef struct ItemClaim
{
    ItemData *item;
    OrbitVary vary;
} ItemClaim;

typedef struct MachineClaim
{
    MachineData *machine;
    OrbitVary    vary;
} MachineClaim;

typedef struct YakuClaim
{
    GrCollRecord *record;
    Vec3          home;   // the record's world translation at claim
    int           held;   // frames it has been carried
    OrbitVary     vary;
} YakuClaim;

static ItemClaim    stc_items[TORN_MAX_ITEMS];
static int          stc_item_count = 0;
static YakuClaim    stc_yaku[TORN_MAX_YAKU];
static int          stc_yaku_count = 0;
static MachineClaim stc_machines[TORN_MAX_MACHINES];
static int          stc_machine_count = 0;

// Menu overrides. Index 0 is "Preset", the pass-through value, on every knob.
static int show_index = 0;

static const int count_values[] = {0, 1, 2, 3, 5};
static char *count_names[] = {"Preset", "1", "2", "3", "5"};
WEATHER_KNOB_CHECK(count_values, count_names);
static int count_index = 0;

static const float duration_factors[] = {1.0f, 0.45f, 1.8f, 3.0f};
static char *duration_names[] = {"Preset", "Brief", "Long", "Sustained"};
WEATHER_KNOB_CHECK(duration_factors, duration_names);
static int duration_index = 0;

static const float size_factors[] = {1.0f, 0.55f, 1.6f, 2.4f};
static char *size_names[] = {"Preset", "Small", "Large", "Colossal"};
WEATHER_KNOB_CHECK(size_factors, size_names);
static int size_index = 0;

static const float strength_factors[] = {1.0f, 0.5f, 1.7f};
static char *strength_names[] = {"Preset", "Gentle", "Violent"};
WEATHER_KNOB_CHECK(strength_factors, strength_names);
static int strength_index = 0;

static int shake_index = 1;

// True while `g` is still in the model-effect bucket as our whirlwind; a group kill
// can take it at any time, so the pointer is re-validated by walking rather than
// trusted.
static int TornadoModelAlive(GOBJ *g)
{
    if (!g)
        return 0;
    for (GOBJ *e = (*stc_gobj_lookup)[GAMEPLINK_EFFECTMODEL]; e != NULL; e = e->next)
    {
        if (e != g)
            continue;
        if (e->entity_class != GAMEENTITY_EFFECT || !e->userdata)
            return 0;
        return ((struct Effect *)e->userdata)->kind == EFFECT_ID_INHALE_WHIRLWIND;
    }
    return 0;
}

// Anchor mode 1's post-spawn callback: the only place the spawned GObj is handed
// out, since Effect_SpawnSync itself returns an opaque handle.
static void TornadoAdoptEffect(void *node)
{
    GOBJ *g = *(GOBJ **)((char *)node + EFFECT_NODE_GOBJ);
    if (!g || g->entity_class != GAMEENTITY_EFFECT || !g->userdata)
        return;

    stc_model = g;
    stc_model_ready = 0;
}

// The funnel spawns into a group of its own: a rider's group is retired on most of
// that rider's state changes, which would take the funnel with it.
static void TornadoSpawnModel(void)
{
    if (stc_efgroup < 0)
        stc_efgroup = Effect_AllocEfGroup();
    if (stc_efgroup < 0)
        return;
    Effect_SpawnSync(NULL, EFFECT_ID_INHALE_WHIRLWIND, stc_efgroup, EFFECT_ANCHOR_DETACHED,
                     TornadoAdoptEffect);
}

// The column's width for this frame: ramping up out of the touchdown, down into the
// lift, and 1.0 in between. A tornado too short to finish either ramp never reaches
// full width.
static float TornadoFade(void)
{
    int elapsed = stc_duration - stc_frames_left;
    int n = (elapsed < stc_frames_left) ? elapsed : stc_frames_left;
    if (n <= 0)
        return 0.0f;
    if (n >= TORN_FADE_FRAMES)
        return 1.0f;
    return (float)n / (float)TORN_FADE_FRAMES;
}

// The world matrix is written directly rather than the root's SRT: it puts the model's
// local +Z axis on world +Y with no euler-order guesswork, and overrides whatever the
// effect's own animation does to the root joint.
static void TornadoPlaceModel(void)
{
    if (!TornadoModelAlive(stc_model))
    {
        stc_model = NULL;
        stc_model_ready = 0;
    }
    if (!stc_model && --stc_spawn_cd <= 0)
    {
        TornadoSpawnModel();
        stc_spawn_cd = TORN_SPAWN_RETRY;
    }
    if (!stc_model)
        return;

    JOBJ *root = (JOBJ *)stc_model->hsd_object;
    if (!root)
        return;

    // Deferred to the first frame the root is reachable, since the spawn callback is
    // handed the spawn node before the root is attached. Anchor mode 1 skips the
    // effect's one-shot init, so the anim loop is armed here and stepped below.
    if (!stc_model_ready)
    {
        JObj_SetAllAOBJLoopByFlags(root, ANIMBYFLAGS_ALL);
        stc_model_ready = 1;
    }

    // The width is floored so the matrix never degenerates.
    float fade = TornadoFade();
    float wide = (fade < TORN_FADE_MIN_WIDTH) ? TORN_FADE_MIN_WIDTH : fade;
    float sa = stc_top / TORN_MODEL_LENGTH;
    float sr = stc_reach * TORN_MODEL_WIDTH * wide / TORN_MODEL_RADIUS;
    float c = cosf(stc_spin), s = sinf(stc_spin);
    float *m = (float *)&root->rotMtx;

    m[0] = c * sr;  m[1] = s * sr;   m[2] = 0.0f;  m[3] = stc_cx;
    m[4] = 0.0f;    m[5] = 0.0f;     m[6] = sa;    m[7] = stc_ground_y;
    m[8] = s * sr;  m[9] = -c * sr;  m[10] = 0.0f; m[11] = stc_cz;

    JObj_SetFlags(root, JOBJ_USER_DEFINED_MTX);
    JObj_ClearFlagsAll(root, JOBJ_HIDDEN);
    JObj_SetMtxDirtySub(root);
    JObj_AnimAll(root);
}

// The effect must never be destroyed by hand: its spawn node still points at the GObj,
// so the engine double-frees it when the group is retired. Hiding the tree is what ends
// a tornado, and the model is reused for the rest of the round.
static void TornadoHideModel(void)
{
    if (!TornadoModelAlive(stc_model))
    {
        stc_model = NULL;
        stc_model_ready = 0;
        return;
    }
    JObj_SetFlagsAll((JOBJ *)stc_model->hsd_object, JOBJ_HIDDEN);
}

// With no attacker, GrYakuBreak_GetAttackerPly (0x80105cb0) credits the break to the
// first present player.
static int TornadoCreditedPly(void)
{
    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) != PKIND_NONE)
            return i;
    }
    return -1;
}

// Breaks a carried prop with no attacker. The break would credit the fallback
// player, so that player's break stats are put back: the tornado breaks things for
// nobody. Returns 1 if the break fired.
static int TornadoBreakInstance(GrCollRecord *record)
{
    int ply = TornadoCreditedPly();
    PlayerStats *stats = (ply >= 0) ? Ply_GetStats(ply) : NULL;
    u8 saved_breaks[sizeof(stats->yakumono_break)];
    int saved_pillar = 0;
    if (stats)
    {
        memcpy(saved_breaks, stats->yakumono_break, sizeof(saved_breaks));
        saved_pillar = stats->fastest_pillar_break;
    }

    int broke = Gr_BreakInstance(record, NULL);

    if (stats)
    {
        memcpy(stats->yakumono_break, saved_breaks, sizeof(saved_breaks));
        stats->fastest_pillar_break = saved_pillar;
    }
    return broke;
}

// Horizontal distance from the axis, and whether `p` is inside the funnel's reach.
static int TornadoInReach(Vec3 *p, float *out_dist)
{
    float dx = p->X - stc_cx;
    float dz = p->Z - stc_cz;
    float d2 = dx * dx + dz * dz;
    if (out_dist)
        *out_dist = sqrtf(d2);
    if (d2 > stc_reach * stc_reach)
        return 0;
    return p->Y >= stc_ground_y - TORN_BELOW && p->Y <= stc_ground_y + stc_top;
}

static void TornadoDrawVary(OrbitVary *v)
{
    v->spin   = RandomInRangef(TORN_VARY_SPIN_LO, TORN_VARY_SPIN_HI);
    v->ring   = RandomInRangef(TORN_VARY_RING_LO, TORN_VARY_RING_HI);
    v->lift   = RandomInRangef(TORN_VARY_LIFT_LO, TORN_VARY_LIFT_HI);
    v->wobble = RandomInRangef(TORN_VARY_WOB_LO, TORN_VARY_WOB_HI);
    v->phase  = RandomAngle();
}

// Derived from the live position every frame, so a target nudged by something else
// self-corrects instead of drifting out of the swirl.
static void TornadoOrbit(Vec3 *p, OrbitVary *v)
{
    float dx = p->X - stc_cx;
    float dz = p->Z - stc_cz;
    float r = sqrtf(dx * dx + dz * dz);
    if (r < 0.01f)
    {
        // Dead on the axis: nudge it off so there is an orbit plane to rotate in.
        dx = stc_core;
        dz = 0.0f;
        r = stc_core;
    }

    float w = TORN_TANGENT * stc_strength * v->spin / r;
    float wmax = TORN_SPIN_MAX * v->spin;  // per-target, or everything near the core resyncs
    if (w > wmax)
        w = wmax;

    float c = cosf(w), s = sinf(w);
    float rx = dx * c - dz * s;
    float rz = dx * s + dz * c;

    v->phase += v->wobble;
    if (v->phase > 2.0f * M_PI)
        v->phase -= 2.0f * M_PI;
    float ring = stc_core * v->ring * (1.0f + TORN_VARY_WOB_AMP * sinf(v->phase));

    float nr = r - TORN_INHALE_RATE * stc_strength * (r - ring);
    if (nr < stc_core * 0.3f)
        nr = stc_core * 0.3f;
    float k = nr / r;

    p->X = stc_cx + rx * k;
    p->Z = stc_cz + rz * k;

    float top = stc_ground_y + stc_top;
    p->Y += TORN_LIFT * stc_strength * v->lift;
    if (p->Y > top)
        p->Y = top;
}

static int TornadoItemClaimed(ItemData *it)
{
    for (int i = 0; i < stc_item_count; i++)
        if (stc_items[i].item == it)
            return 1;
    return 0;
}

static int TornadoMachineClaimed(MachineData *md)
{
    for (int i = 0; i < stc_machine_count; i++)
        if (stc_machines[i].machine == md)
            return 1;
    return 0;
}

static int TornadoYakuClaimed(GrCollRecord *record)
{
    for (int i = 0; i < stc_yaku_count; i++)
        if (stc_yaku[i].record == record)
            return 1;
    return 0;
}

// Boxes are left alone - only loose power-ups get swept.
static int TornadoItemIsTarget(GOBJ *g, ItemData **out)
{
    ItemData *it = (ItemData *)g->userdata;
    if (Item_IsBox(it))
        return 0;
    *out = it;
    return 1;
}

static void TornadoClaimItems(void)
{
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ITEM];
         g != NULL && stc_item_count < TORN_MAX_ITEMS; g = g->next)
    {
        ItemData *it;
        if (!TornadoItemIsTarget(g, &it))
            continue;
        if (!TornadoInReach(&it->pos, NULL) || TornadoItemClaimed(it))
            continue;
        stc_items[stc_item_count].item = it;
        TornadoDrawVary(&stc_items[stc_item_count].vary);
        stc_item_count++;
    }
}

static void TornadoClaimMachines(void)
{
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_MACHINE];
         g != NULL && stc_machine_count < TORN_MAX_MACHINES; g = g->next)
    {
        MachineData *md = (MachineData *)g->userdata;
        if (md->rider_gobj != NULL) // ridden machines are pushed, not carried
            continue;
        if (md->is_dead || md->is_fall_dead)
            continue;
        if (!TornadoInReach(&md->pos, NULL) || TornadoMachineClaimed(md))
            continue;
        stc_machines[stc_machine_count].machine = md;
        TornadoDrawVary(&stc_machines[stc_machine_count].vary);
        stc_machine_count++;
    }
}

// The CT breakables the tornado is allowed to rip up. Passive zones, the big
// set-piece structures, and the forest pitfall, which a single hit only cracks, are
// left alone.
static int TornadoIsBreakableYaku(YakuKind kind)
{
    switch (kind)
    {
    case YAKUKIND_STARPOLE:
    case YAKUKIND_CORAL:
    case YAKUKIND_TREE:
    case YAKUKIND_ROCK:
    case YAKUKIND_BREAKHPCOLLDOOR:
    case YAKUKIND_BREAKHPCOLLHOLE:
    case YAKUKIND_BREAKHPCOLLHOUSE:
        return 1;
    default:
        return 0;
    }
}

// Claim intact props inside the funnel. Collision is retired at claim time so nobody
// runs into a prop that is already airborne, and the joint is marked user-defined so
// the skeleton families stop rebuilding their matrix from the authored SRT.
static void TornadoClaimYakumono(void)
{
    int count = 0;
    GrCollRecord *pool = Gr_GetCollRecords(&count);
    if (!pool)
        return;

    GOBJ *parents[TORN_MAX_PARENTS];
    int nparents = Yaku_CollectGObjs(TornadoIsBreakableYaku, parents, TORN_MAX_PARENTS);
    if (nparents == 0)
        return;

    for (int i = 0; i < count && stc_yaku_count < TORN_MAX_YAKU; i++)
    {
        GrCollRecord *record = &pool[i];
        if (!Gr_IsRecordOwnedBy(record, parents, nparents) || TornadoYakuClaimed(record))
            continue;
        if (!grScene_IsInstanceCollAll(record, 1)) // already broken
            continue;

        Vec3 pos;
        Mtx_GetTrans(record->world, &pos);
        if (!TornadoInReach(&pos, NULL))
            continue;

        JOBJ *j = record->jobj;
        if (!j)
            continue;

        JObj_SetFlags(j, JOBJ_USER_DEFINED_MTX);
        grScene_SetInstanceColl(record, 0);
        YakuClaim *claim = &stc_yaku[stc_yaku_count++];
        claim->record = record;
        claim->home = pos;
        claim->held = 0;
        TornadoDrawVary(&claim->vary);
    }
}

static int TornadoItemIsLive(ItemData *it)
{
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ITEM]; g != NULL; g = g->next)
    {
        ItemData *cand;
        if (!TornadoItemIsTarget(g, &cand))
            continue;
        if (cand == it)
            return 1;
    }
    return 0; // collected or despawned
}

// Carried items keep their claim after the funnel has moved on, so the physics that
// would fight the override is neutralized every frame.
static void TornadoProcessItems(void)
{
    for (int i = stc_item_count - 1; i >= 0; i--)
    {
        ItemData *it = stc_items[i].item;
        if (!TornadoItemIsLive(it))
        {
            stc_items[i] = stc_items[--stc_item_count];
            continue;
        }

        TornadoOrbit(&it->pos, &stc_items[i].vary);

        // CityItem_PhysicsThink integrates pos += vel, and Item_GenericEnvColl snaps
        // the item to the ground unless is_airborne is -1.
        it->vel.X = 0.0f;
        it->vel.Y = 0.0f;
        it->vel.Z = 0.0f;
        it->is_airborne = -1;
        it->flags_x35a &= (u8)~ITEM_X35A_GROUNDED;
    }
}

// Released items fall under vanilla physics. Each goes back on point collision with no
// ground triangle, so a resting one's envcoll enters its fall state, and a falling one
// re-casts the ground under where it is now rather than landing on the spot it was
// lifted from.
static void TornadoReleaseItems(void)
{
    for (int i = 0; i < stc_item_count; i++)
    {
        ItemData *it = stc_items[i].item;
        if (!TornadoItemIsLive(it))
            continue;
        it->coll_kind = ITEM_COLL_POINT;
        it->point_coll.raycast_idx = -1;
        Item_SetAirborne(it);
    }
    stc_item_count = 0;
}

static int TornadoMachineIsLive(MachineData *md)
{
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_MACHINE]; g != NULL; g = g->next)
    {
        if ((MachineData *)g->userdata == md)
            return md->rider_gobj == NULL && !md->is_dead && !md->is_fall_dead;
    }
    return 0;
}

static void TornadoProcessMachines(void)
{
    for (int i = stc_machine_count - 1; i >= 0; i--)
    {
        MachineData *md = stc_machines[i].machine;
        // A machine somebody mounted mid-flight drops out of the carried set and is
        // pushed as a ridden machine from the next frame.
        if (!TornadoMachineIsLive(md))
        {
            stc_machines[i] = stc_machines[--stc_machine_count];
            continue;
        }

        TornadoOrbit(&md->pos, &stc_machines[i].vary);

        // Machine_PhysicsThink integrates accel and velocity into pos every frame.
        md->accel.X = 0.0f;
        md->accel.Y = 0.0f;
        md->accel.Z = 0.0f;
        md->velocity.X = 0.0f;
        md->velocity.Y = 0.0f;
        md->velocity.Z = 0.0f;
    }
}

// A prop whose break did not fire goes back where it stood, collidable again.
static void TornadoReturnProp(YakuClaim *claim)
{
    GrCollRecord *record = claim->record;
    Mtx_SetTrans(record->world, &claim->home);
    JObj_ClearFlags(record->jobj, JOBJ_USER_DEFINED_MTX);
    grScene_SetInstanceColl(record, 1);
}

static void TornadoProcessYakumono(int force_break)
{
    for (int i = stc_yaku_count - 1; i >= 0; i--)
    {
        YakuClaim *claim = &stc_yaku[i];
        GrCollRecord *record = claim->record;
        JOBJ *j = record->jobj;

        // The prop's real transform is its JObj world matrix; record->world is what
        // the break reads back for its contact point.
        Vec3 pos;
        Mtx_GetTrans(j->rotMtx, &pos);

        claim->held++;
        if (force_break || claim->held >= TORN_YAKU_HOLD)
        {
            Mtx_SetTrans(record->world, &pos);
            if (!TornadoBreakInstance(record))
                TornadoReturnProp(claim);
            stc_yaku[i] = stc_yaku[--stc_yaku_count];
            continue;
        }

        TornadoOrbit(&pos, &claim->vary);
        JObj_SetFlags(j, JOBJ_USER_DEFINED_MTX);
        Mtx_SetTrans(j->rotMtx, &pos);
        grScene_SetInstanceColl(record, 0); // keep it retired for the whole flight
    }
}

// An inward + tangential + upward acceleration added to each ridden machine's own
// velocity, so speed and boost still count for something.
static void TornadoPushRiders(void)
{
    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) == PKIND_NONE)
            continue;
        MachineData *md = Ply_GetMachineData(i);
        if (!md || md->is_dead || md->is_fall_dead)
            continue;
        if (md->rider_gobj == NULL)
            continue; // dismounted - the carried path owns it

        float dist;
        if (!TornadoInReach(&md->pos, &dist))
            continue;

        float dx = md->pos.X - stc_cx;
        float dz = md->pos.Z - stc_cz;
        if (dist < 0.01f)
            continue;
        float ix = -dx / dist, iz = -dz / dist;  // inward unit
        float tx = -iz, tz = ix;                 // tangential unit (same sense as the orbit)

        // Linear falloff to nothing at the edge of the funnel's reach.
        float f = 1.0f - dist / stc_reach;
        if (f < 0.0f)
            f = 0.0f;
        f *= stc_strength;

        md->velocity.X += (ix * TORN_RIDER_PULL + tx * TORN_RIDER_SPIN) * f;
        md->velocity.Z += (iz * TORN_RIDER_PULL + tz * TORN_RIDER_SPIN) * f;
        md->velocity.Y += TORN_RIDER_LIFT * f;
    }
}

// The view of camera slot `i` and how hard the funnel shakes it: falling off to nothing
// at the outer reach, measured from the view's own aim point (CamData.view.interest),
// which is what that camera is actually looking at. 0 when it is out of reach.
static float TornadoShakeAmp(int i, CamShakeRec **out_rec)
{
    GOBJ *cg = stc_plycam_lookup->cam_gobjs[i];
    if (!cg || !cg->userdata)
        return 0.0f;
    PlayerCamData *pc = (PlayerCamData *)cg->userdata;
    CamData *cam = pc->cam_data;
    *out_rec = pc->shake;
    if (!*out_rec || !cam)
        return 0.0f;

    float outer = stc_reach * TORN_SHAKE_REACH;
    float dx = cam->view.interest.X - stc_cx;
    float dz = cam->view.interest.Z - stc_cz;
    float d = sqrtf(dx * dx + dz * dz);
    if (d >= outer)
        return 0.0f;
    return TORN_SHAKE_AMP * stc_strength * (1.0f - d / outer);
}

// A light shake on the views near the funnel. Each view in reach gets a quake source of
// its own, since a view applies only its highest-priority source: one global source
// would mute the machine speed shake on every view out of reach. The jitter comes from
// the frame count, so the shake draws nothing from the game RNG.
static void TornadoShake(void)
{
    float t = (float)stc_frames_left;
    for (int i = 0; i < CM_CAMERA_MAX; i++)
    {
        CamShakeRec *rec = NULL;
        float amp = shake_index ? TornadoShakeAmp(i, &rec) : 0.0f;
        if (amp <= 0.0f)
        {
            Weather_ShakeStop(&stc_shake_src[i]);
            continue;
        }
        Weather_ShakeSet(&stc_shake_src[i], rec, TORN_SHAKE_PRIORITY,
                         amp * sinf(t * 2.3f + (float)i),
                         amp * sinf(t * 3.7f + (float)i * 1.9f));
    }
}

static void TornadoShakeStop(void)
{
    for (int i = 0; i < CM_CAMERA_MAX; i++)
        Weather_ShakeStop(&stc_shake_src[i]);
}

// Everything still in flight when the funnel lifts: props are torn apart (they have
// no intact place left to land), items fall, machines drop out of the sky.
static void TornadoEndFunnel(void)
{
    TornadoProcessYakumono(1);
    TornadoReleaseItems();
    TornadoShakeStop();
    stc_machine_count = 0;
    TornadoHideModel();
    stc_live = 0;
    stc_frames_left = 0;
}

// What the funnel does to the world: the orbit's position writes and the rider push.
static void TornadoApply(GOBJ *g)
{
    (void)g;
    if (!stc_live)
        return;

    TornadoProcessItems();
    TornadoProcessMachines();
    TornadoProcessYakumono(0);
    TornadoPushRiders();
}

static void TornadoEnsureProc(void)
{
    if (stc_proc_gobj)
        return;

    stc_proc_gobj = GObj_Create(TORN_GOBJ_CLASS, TORN_GOBJ_PLINK, 0);
    if (stc_proc_gobj)
    {
        GObj_AddProc(stc_proc_gobj, TornadoApply, TORN_PROC_PRI);
        return;
    }

    static int warned;
    if (!warned)
    {
        warned = 1;
        OSReport("[Tornado] Funnel proc not created, GObj pool exhausted\n");
    }
}

static float TornadoGroundY(StageNode *sn, float x, float z, float fallback)
{
    Vec3 hit;
    if (Weather_CastDown(sn, x, z, sn->oob_min.Y - WEATHER_CAST_MARGIN, &hit) < 0)
        return fallback;
    return hit.Y;
}

// The disc the funnel is allowed to roam: centered on the out-of-bounds box, with the
// radius taken from its shorter half-extent so the funnel never leans on a wall.
static void TornadoPlayArea(StageNode *sn, float *cx, float *cz, float *radius)
{
    float hx, hz;
    Weather_PlayBox(sn, cx, cz, &hx, &hz);
    *radius = ((hx < hz) ? hx : hz) * TORN_PLAY_FRACTION;
}

// A point uniform over the play disc's area, so waypoints are as likely to be drawn
// through the middle of the city as around its rim.
static void TornadoPickPoint(StageNode *sn, float *x, float *z)
{
    float cx, cz, r;
    TornadoPlayArea(sn, &cx, &cz, &r);
    float a = RandomAngle();
    float d = r * sqrtf(HSD_Randf());
    *x = cx + sinf(a) * d;
    *z = cz + cosf(a) * d;
}

static void TornadoTouchDown(StageNode *sn)
{
    TornadoPickPoint(sn, &stc_cx, &stc_cz);
    TornadoPickPoint(sn, &stc_wpx, &stc_wpz);

    float dx = stc_wpx - stc_cx;
    float dz = stc_wpz - stc_cz;
    float d = sqrtf(dx * dx + dz * dz);
    if (d < 1e-4f)
    {
        dx = 0.0f;
        dz = 1.0f;
        d = 1.0f;
    }
    stc_dirx = dx / d;
    stc_dirz = dz / d;

    stc_ground_y = TornadoGroundY(sn, stc_cx, stc_cz, sn->oob_min.Y);
    stc_live = 1;
    stc_frames_left = stc_duration;
    TornadoEnsureProc();
}

// Cross the play disc between waypoints: steer the heading toward the current one,
// jitter it so the track curves, and draw a fresh waypoint on arrival.
static void TornadoWander(StageNode *sn)
{
    float cx, cz, r;
    TornadoPlayArea(sn, &cx, &cz, &r);

    float tx = stc_wpx - stc_cx;
    float tz = stc_wpz - stc_cz;
    float td = sqrtf(tx * tx + tz * tz);
    if (td < TORN_WAYPOINT_HIT)
    {
        TornadoPickPoint(sn, &stc_wpx, &stc_wpz);
        tx = stc_wpx - stc_cx;
        tz = stc_wpz - stc_cz;
        td = sqrtf(tx * tx + tz * tz);
    }

    float dx = stc_dirx;
    float dz = stc_dirz;
    if (td > 1e-4f)
    {
        dx += (tx / td - dx) * TORN_STEER;
        dz += (tz / td - dz) * TORN_STEER;
    }

    float w = Weather_Randf2() * TORN_TURN;
    float c = cosf(w), s = sinf(w);
    float jx = dx * c - dz * s;
    float jz = dx * s + dz * c;

    float dl = sqrtf(jx * jx + jz * jz);
    if (dl > 1e-4f)
    {
        stc_dirx = jx / dl;
        stc_dirz = jz / dl;
    }

    stc_cx += stc_dirx * stc_speed;
    stc_cz += stc_dirz * stc_speed;

    // A waypoint is always inside the disc, so this only ever catches the jitter
    // walking the funnel a little past the rim.
    float ox = stc_cx - cx;
    float oz = stc_cz - cz;
    float od = sqrtf(ox * ox + oz * oz);
    if (od > r && od > 1e-4f)
    {
        stc_cx = cx + ox / od * r;
        stc_cz = cz + oz / od * r;
    }

    stc_ground_y = TornadoGroundY(sn, stc_cx, stc_cz, stc_ground_y);
}

// A new preset mid-round - a legendary machine's sky - ends a live funnel and keeps
// the round's schedule.
void Tornado_SetActive(const TornadoDef *def)
{
    if (stc_live)
        TornadoEndFunnel();

    stc_active = WeatherToggle(show_index, def && def->enabled);

    int def_count = (def && def->count > 0) ? def->count : TORN_DEF_COUNT;
    stc_count = (count_index > 0) ? count_values[count_index] : def_count;

    int def_duration = (def && def->duration > 0) ? def->duration : TORN_DEF_DURATION;
    stc_duration = (int)(def_duration * duration_factors[duration_index]);
    if (stc_duration < 1)
        stc_duration = 1;

    float def_size = (def && def->size > 0.0f) ? def->size : TORN_DEF_SIZE;
    float def_strength = (def && def->strength > 0.0f) ? def->strength : TORN_DEF_STRENGTH;
    float size = size_factors[size_index] * def_size;
    stc_strength = strength_factors[strength_index] * def_strength;
    stc_speed = (def && def->speed > 0.0f) ? def->speed : TORN_DEF_SPEED;

    stc_core = TORN_CORE_RADIUS * size;
    stc_reach = TORN_INFLUENCE * size;
    stc_top = TORN_HEIGHT * size;
}

void Tornado_Tick(void)
{
    if (!stc_active)
        return;

    StageNode *sn = Weather_StageNode();
    if (!sn)
        return;

    float p = Weather_RoundProgress();
    if (p < 0.0f)
        return;

    if (stc_live)
    {
        if (--stc_frames_left <= 0)
        {
            OSReport("[Tornado] Lifted at (%d, %d)\n", (int)stc_cx, (int)stc_cz);
            TornadoEndFunnel();
            return;
        }
        TornadoWander(sn);
        TornadoShake();
        stc_spin += TORN_MODEL_SPIN * stc_strength;
        if (stc_spin > 2.0f * M_PI)
            stc_spin -= 2.0f * M_PI;
        TornadoPlaceModel();
        TornadoClaimItems();
        TornadoClaimMachines();
        TornadoClaimYakumono();
        return;
    }

    if (WeatherSchedule_Due(&stc_schedule, stc_count, p))
    {
        TornadoTouchDown(sn);
        TornadoPlaceModel();
        OSReport("[Tornado] Touchdown %d/%d at progress %d%%, (%d, %d), %d frames\n",
                 stc_schedule.next, stc_schedule.num, (int)(p * 100.0f),
                 (int)stc_cx, (int)stc_cz, stc_duration);
    }
}

// Quake_Init wipes the source pool at every 3D load, so held sources are dropped rather
// than removed.
void Tornado_Reset(void)
{
    memset(stc_shake_src, 0, sizeof(stc_shake_src));
    stc_active = 0;
    stc_schedule.planned = 0;
    stc_live = 0;
    stc_frames_left = 0;
    stc_item_count = 0;
    stc_yaku_count = 0;
    stc_machine_count = 0;
    stc_proc_gobj = NULL;
    stc_efgroup = -1;
    stc_model = NULL;
    stc_model_ready = 0;
    stc_spawn_cd = 0;
    stc_spin = 0.0f;
}

static void OnTornadoShowChange(int val)
{
    OSReport("[Tornado] Tornado %s\n", weather_toggle_names[val]);
}

static void OnTornadoCountChange(int val)
{
    OSReport("[Tornado] Appearances %s\n", count_names[val]);
}

static void OnTornadoDurationChange(int val)
{
    OSReport("[Tornado] Duration %s\n", duration_names[val]);
}

static void OnTornadoSizeChange(int val)
{
    OSReport("[Tornado] Size %s\n", size_names[val]);
}

static void OnTornadoStrengthChange(int val)
{
    OSReport("[Tornado] Strength %s\n", strength_names[val]);
}

MenuDesc tornado_menu = {
    .option_num = 6,
    .options = {
        &(OptionDesc){
            .name = "Tornado",
            .description = "Let a tornado sweep City Trial: Preset = only presets that set it, Off = never, On = all presets",
            .kind = OPTKIND_VALUE,
            .val = &show_index,
            .value_num = WEATHER_TOGGLE_NUM,
            .value_names = weather_toggle_names,
            .on_change = OnTornadoShowChange,
        },
        &(OptionDesc){
            .name = "Appearances",
            .description = "How many tornadoes touch down over a City Trial round, spread across the match timer",
            .kind = OPTKIND_VALUE,
            .val = &count_index,
            .value_num = GetElementsIn(count_values),
            .value_names = count_names,
            .on_change = OnTornadoCountChange,
        },
        &(OptionDesc){
            .name = "Duration",
            .description = "How long each tornado roams the city before it lifts",
            .kind = OPTKIND_VALUE,
            .val = &duration_index,
            .value_num = GetElementsIn(duration_factors),
            .value_names = duration_names,
            .on_change = OnTornadoDurationChange,
        },
        &(OptionDesc){
            .name = "Size",
            .description = "Funnel scale, which also sets how far out it can reach for debris and riders",
            .kind = OPTKIND_VALUE,
            .val = &size_index,
            .value_num = GetElementsIn(size_factors),
            .value_names = size_names,
            .on_change = OnTornadoSizeChange,
        },
        &(OptionDesc){
            .name = "Strength",
            .description = "How hard the funnel spins what it catches and how much it drags at a passing rider",
            .kind = OPTKIND_VALUE,
            .val = &strength_index,
            .value_num = GetElementsIn(strength_factors),
            .value_names = strength_names,
            .on_change = OnTornadoStrengthChange,
        },
        &(OptionDesc){
            .name = "Screen Shake",
            .description = "Shake the camera of players who get close to a tornado",
            .kind = OPTKIND_VALUE,
            .val = &shake_index,
            .value_num = WEATHER_ONOFF_NUM,
            .value_names = weather_onoff_names,
        },
    },
};
