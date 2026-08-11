// Tornado for custom_weather: a funnel wanders City Trial on a random path, drawing
// loose items, breakable props and parked machines into an orbit around its core and
// dragging at riders who stray inside. Appearances are spread across the round by the
// match timer, the same way volcano eruptions are.
//
// The work is split across two entry points on purpose. Tornado_Tick runs from the
// weather runtime (inside the stage think) and decides where the funnel is, drives its
// model, and claims new targets. Tornado_OnFrameEnd runs after the frame's game procs,
// which is the only place a position override survives - item physics, machine physics
// and the ground snap all run at proc priorities 4-6 and would otherwise stomp it.

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
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

#define TORN_PI 3.14159265358979f

#define ITEM_GOBJ_KIND    22  // gobj->entity_class for a City Trial item
#define MACHINE_GOBJ_KIND GAMEENTITY_MACHINE
#define EFFECT_GOBJ_KIND  25  // gobj->entity_class for a model effect
#define EFFECT_PLINK      16

// The inhale suction whirlwind, borrowed as the funnel model. Anchor mode 1 spawns
// it detached instead of bolted to a mouth bone: no follow proc is installed, so the
// model root's SRT is ours to drive.
#define TORN_EFFECT_ID     0x3a982
#define TORN_EFFECT_ANCHOR 1
#define TORN_RIDER_EFGROUP_OFF 0x440  // RiderData -> the EfGroup the inhale spawns into

// Defaults applied when a preset leaves the matching TornadoDef field 0.
#define TORN_DEF_COUNT     2
#define TORN_DEF_DURATION  1500    // 25s at 60fps
#define TORN_DEF_SIZE      1.0f
#define TORN_DEF_STRENGTH  1.0f
#define TORN_DEF_SPEED     2.2f    // world units/frame the funnel wanders

#define TORN_MAX_COUNT     8       // schedule capacity

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

// The funnel forms and ropes out over TORN_FADE_FRAMES at each end of its life. The ramp
// narrows the column and scales every material's authored opacity; each MObj owns a
// copied HSD_Material, so the opacity write dims this funnel alone and never a player's
// inhale whirlwind.
#define TORN_FADE_FRAMES     120   // 2s at 60fps, each way
#define TORN_FADE_MIN_WIDTH  0.02f
#define TORN_MAX_MATS        8

// Orbit of a captured target. Angular speed is derived from a constant tangential
// speed so the outer edge does not whip around faster than the core, and is capped
// so a target that reaches the middle does not spin into a blur.
#define TORN_TANGENT       9.0f    // tangential world units/frame
#define TORN_SPIN_MAX      0.24f   // radians/frame cap
#define TORN_INHALE_RATE   0.05f   // fraction of the excess radius shed per frame
#define TORN_LIFT          3.0f    // world units/frame a captured target climbs

// Per-target orbit variation, drawn once when a target is claimed and then fixed. The
// orbit is otherwise a pure function of position, so everything at the same radius laps
// at the same rate and converges on the same ring - one rigid spiral. These spread the
// debris across rings, let objects overtake each other, and breathe each ring in and out.
#define TORN_VARY_SPIN_LO  0.75f
#define TORN_VARY_SPIN_HI  1.35f
#define TORN_VARY_RING_LO  0.55f   // ring a target settles into, as a multiple of the core
#define TORN_VARY_RING_HI  2.40f
#define TORN_VARY_LIFT_LO  0.60f
#define TORN_VARY_LIFT_HI  1.45f
#define TORN_VARY_WOB_LO   0.010f  // radians/frame of the ring's radial breathing
#define TORN_VARY_WOB_HI   0.035f
#define TORN_VARY_WOB_AMP  0.14f   // breathing depth as a fraction of the ring radius

// A rider is pushed, never possessed: these are per-frame accelerations added to the
// machine's own velocity, so a fast machine can drive or boost its way back out.
#define TORN_RIDER_PULL    0.55f   // inward accel at the funnel center
#define TORN_RIDER_SPIN    0.85f   // tangential accel
#define TORN_RIDER_LIFT    0.40f   // upward accel

// Props are carried for a moment and then torn apart.
#define TORN_YAKU_HOLD     110     // frames a claimed prop orbits before it breaks
#define TORN_BREAK_RADIUS  1.0e9f  // synthetic collider radius; one-hit break
#define TORN_BREAK_DELTA   100.0f  // synthetic impact delta magnitude

// Camera shake, felt from further out than the funnel actually reaches.
#define TORN_SHAKE_REACH   1.6f    // multiple of the influence radius
#define TORN_SHAKE_AMP     2.4f    // world units of camera offset at the center

// The engine's own per-view shake, driven instead of patching the camera solve.
// PlyCam_Think builds the camera's right/up basis and then, gated on rec+0x44 being
// positive, offsets the eye it hands the COBJ by
//   eye += right * rec[+0x28] * rec[+0x20] + up * rec[+0x2c] * rec[+0x24]
// with the two scale fields initialised to 1.0, so writing the offsets in world units
// and raising the gate is the whole mechanism. CamData.eye_pos/interest_pos are NOT
// in the render path - the param that reaches the COBJ is CamData.x14.
#define TORN_SHAKE_REC_OFF 0x74    // PlayerCamData -> shake record (past the header's decl)
#define TORN_SHAKE_EYE_R   0x28    // offset along the camera's right axis
#define TORN_SHAKE_EYE_U   0x2c    // offset along the camera's up axis
#define TORN_SHAKE_GATE    0x44    // > 0 enables the offset

// Wander: the funnel crosses a disc of play area between random waypoints. Waypoints
// rather than a free heading walk, because a walk that only turns when it would leave
// the box ends up sliding along the boundary for the whole tornado. Nothing here reads
// the wind - the path is the funnel's own.
#define TORN_PLAY_FRACTION 0.68f   // play radius as a fraction of the OOB half-extent
#define TORN_STEER         0.045f  // per-frame blend of the heading toward the waypoint
#define TORN_TURN          0.030f  // radians/frame of random jitter on top of the steer
#define TORN_WAYPOINT_HIT  150.0f  // distance at which a waypoint counts as reached

#define TORN_MAX_ITEMS     96
#define TORN_MAX_YAKU      128
#define TORN_MAX_MACHINES  24
#define TORN_MAX_PARENTS   16

// Weak break families pin their debris to a node at the prop's baked spot, so their
// dragged mesh must be collapsed by hand after the break.
#define TORN_HIT_WEAK_OBJECT ((void *)0x80107914)

static int stc_active = 0;

// Latched preset config with the module defaults filled in; never written elsewhere,
// so a menu knob returned to "Preset" resolves back to it.
static int   stc_def_count = TORN_DEF_COUNT;
static int   stc_def_duration = TORN_DEF_DURATION;
static float stc_def_size = TORN_DEF_SIZE;
static float stc_def_strength = TORN_DEF_STRENGTH;
static float stc_def_speed = TORN_DEF_SPEED;

// Effective config this frame: the preset values with the menu overrides folded in.
static int   stc_count = TORN_DEF_COUNT;
static int   stc_duration = TORN_DEF_DURATION;
static float stc_size = TORN_DEF_SIZE;
static float stc_strength = TORN_DEF_STRENGTH;
static float stc_speed = TORN_DEF_SPEED;

// Round schedule: normalized match progress at which each tornado touches down.
static float stc_schedule[TORN_MAX_COUNT];
static int   stc_scheduled = 0;   // count the schedule was planned for; 0 = unplanned
static int   stc_next = 0;        // next unfired entry

// The live funnel.
static int   stc_live = 0;
static int   stc_frames_left = 0;
static float stc_cx = 0.0f, stc_cz = 0.0f;  // axis position
static float stc_ground_y = 0.0f;           // funnel base
static float stc_dirx = 0.0f, stc_dirz = 1.0f;  // unit wander heading
static float stc_wpx = 0.0f, stc_wpz = 0.0f;    // waypoint being crossed to
static float stc_spin = 0.0f;               // funnel model's roll about its own axis
static GOBJ *stc_model = NULL;              // borrowed whirlwind effect GObj

// The model's materials and the opacity each was authored with, which is what the fade
// scales. Collected once per spawn.
static HSD_Material *stc_mats[TORN_MAX_MATS];
static float         stc_mat_alpha[TORN_MAX_MATS];
static int           stc_mat_count = 0;
static int           stc_model_ready = 0;

// Effective geometry for the live funnel, recomputed each frame from stc_size.
static float stc_core = TORN_CORE_RADIUS;
static float stc_reach = TORN_INFLUENCE;
static float stc_top = TORN_HEIGHT;

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
    void     *record;
    int       held;    // frames it has been carried
    OrbitVary vary;
} YakuClaim;

static ItemClaim    stc_items[TORN_MAX_ITEMS];
static int          stc_item_count = 0;
static YakuClaim    stc_yaku[TORN_MAX_YAKU];
static int          stc_yaku_count = 0;
static MachineClaim stc_machines[TORN_MAX_MACHINES];
static int          stc_machine_count = 0;

// Menu overrides. Index 0 is "Preset" on every knob.
static char *toggle_names[] = {"Preset", "Off", "On"};
static int show_index = 0;

static const int count_values[] = {0, 0, 1, 2, 3, 5};
static char *count_names[] = {"Preset", "Off", "1", "2", "3", "5"};
#define TORN_COUNT_NUM (int)(sizeof(count_values) / sizeof(count_values[0]))
static int count_index = 0;

static const float duration_factors[] = {0.0f, 0.45f, 1.0f, 1.8f, 3.0f};
static char *duration_names[] = {"Preset", "Brief", "Normal", "Long", "Sustained"};
#define TORN_DURATION_NUM (int)(sizeof(duration_factors) / sizeof(duration_factors[0]))
static int duration_index = 0;

static const float size_factors[] = {0.0f, 0.55f, 1.0f, 1.6f, 2.4f};
static char *size_names[] = {"Preset", "Small", "Normal", "Large", "Colossal"};
#define TORN_SIZE_NUM (int)(sizeof(size_factors) / sizeof(size_factors[0]))
static int size_index = 0;

static const float strength_factors[] = {0.0f, 0.5f, 1.0f, 1.7f};
static char *strength_names[] = {"Preset", "Gentle", "Normal", "Violent"};
#define TORN_STRENGTH_NUM (int)(sizeof(strength_factors) / sizeof(strength_factors[0]))
static int strength_index = 0;

static char *shake_names[] = {"On", "Off"};
static int shake_index = 0;

static StageNode *TornadoStageNode(void)
{
    GrObj *gr = *stc_grobj;
    if (!gr || !gr->gr_data || !gr->gr_data->stage_node)
        return NULL;
    return gr->gr_data->stage_node;
}

// Ground height under (x, z), or `fallback` where the raycast finds nothing (the
// funnel is over a gap or off the plaza).
static float TornadoGroundY(StageNode *sn, float x, float z, float fallback)
{
    Vec3 start = {x, sn->oob_max.Y + 50.0f, z};
    Vec3 end = {x, sn->oob_min.Y - 50.0f, z};
    Vec3 hit;
    if (Raycast_Ground(&start, &end, &hit) < 0)
        return fallback;
    return hit.Y;
}

// The whirlwind is a plain model GObj with no think proc, so once spawned nothing
// re-anchors it and its root JObj transform is ours to write.
static JOBJ *TornadoModelRoot(void)
{
    if (!stc_model)
        return NULL;
    return (JOBJ *)stc_model->hsd_object;
}

// True while `g` is still in the model-effect bucket as our whirlwind. The effect's
// lifetime counter despawns it on its own, so the pointer is re-validated by walking
// rather than trusted.
static int TornadoModelAlive(GOBJ *g)
{
    if (!g)
        return 0;
    for (GOBJ *e = (*stc_gobj_lookup)[EFFECT_PLINK]; e != NULL; e = e->next)
    {
        if (e != g)
            continue;
        if (e->entity_class != EFFECT_GOBJ_KIND || !e->userdata)
            return 0;
        return ((struct Effect *)e->userdata)->kind == TORN_EFFECT_ID;
    }
    return 0;
}

// Any live rider works: the spawn only reads the parent to resolve the anchor, and
// the funnel overwrites the placement immediately afterwards.
static GOBJ *TornadoDonorRider(void)
{
    for (int i = 0; i < 4; i++)
    {
        GOBJ *rg = stc_playerdata[i].rider_gobj;
        if (stc_playerdata[i].player_kind == PKIND_NONE || !rg || !rg->userdata)
            continue;
        return rg;
    }
    return NULL;
}

// Every part of the whirlwind is its own MObj with its own material, and the parts sit
// on separate child joints, so the whole tree is walked.
static void TornadoCollectMaterials(JOBJ *j)
{
    while (j != NULL)
    {
        for (DOBJ *d = j->dobj; d != NULL; d = d->next)
        {
            MOBJ *m = d->mobj;
            if (!m || !m->mat || stc_mat_count >= TORN_MAX_MATS)
                continue;
            stc_mats[stc_mat_count] = m->mat;
            stc_mat_alpha[stc_mat_count] = m->mat->alpha;
            stc_mat_count++;
        }
        if (j->child != NULL)
            TornadoCollectMaterials(j->child);
        j = j->sibling;
    }
}

// One-time setup on the model tree, deferred to the first frame the root is reachable
// rather than done in the spawn callback: the callback is handed the spawn node, and a
// root that is not attached yet there would silently leave the swirl unanimated and the
// fade with no materials to scale.
//
// Mode 1 skips the effect's one-shot init, so nothing arms the anim loop and no proc
// advances it. Arming the loop here and stepping it from the tick is what keeps the
// swirl moving instead of frozen on its first frame.
static void TornadoPrepareModel(JOBJ *root)
{
    JObj_SetAllAOBJLoopByFlags(root, ALL_ANIM);
    stc_mat_count = 0;
    TornadoCollectMaterials(root);
    stc_model_ready = 1;
}

// Anchor mode 1's post-spawn callback: the only place the spawned GObj is handed
// out, since Effect_SpawnSync itself returns an opaque handle.
static void TornadoAdoptEffect(void *node)
{
    GOBJ *g = *(GOBJ **)((char *)node + EFFECT_NODE_GOBJ);
    if (!g || g->entity_class != EFFECT_GOBJ_KIND || !g->userdata)
        return;

    stc_model = g;
    stc_model_ready = 0;
}

// The efgroup argument asserts on -1, so it is borrowed from a live rider - the same
// group the inhale spawns its whirlwind into.
static void TornadoSpawnModel(void)
{
    GOBJ *rider_gobj = TornadoDonorRider();
    if (!rider_gobj || !rider_gobj->userdata)
        return;

    int efgroup = *(int *)((char *)rider_gobj->userdata + TORN_RIDER_EFGROUP_OFF);
    Effect_SpawnSync(NULL, TORN_EFFECT_ID, efgroup, TORN_EFFECT_ANCHOR,
                     TornadoAdoptEffect);
}

// Opacity for this frame: ramping up out of the touchdown, down into the lift, and 1.0
// in between. A tornado too short to finish either ramp just never reaches full opacity.
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

// Written after the frame's anim step, which is the only thing that would otherwise put
// the authored alpha back.
static void TornadoApplyFade(float fade)
{
    for (int i = 0; i < stc_mat_count; i++)
        stc_mats[i]->alpha = stc_mat_alpha[i] * fade;
}

// Plant the funnel model on the axis, standing it upright and spinning it. The world
// matrix is written directly rather than the root's SRT: it puts the model's local +Z
// axis on world +Y with no euler-order guesswork, lets the length and the cross section
// scale independently, and overrides whatever the effect's own animation does to the
// root joint.
static void TornadoPlaceModel(void)
{
    if (!TornadoModelAlive(stc_model))
        stc_model = NULL;
    if (!stc_model)
        TornadoSpawnModel();

    JOBJ *root = TornadoModelRoot();
    if (!root)
        return;
    if (!stc_model_ready)
        TornadoPrepareModel(root);

    // The funnel ropes out as it fades: the whirlwind's own alpha stage is its texture's,
    // so scaling the materials alone is not guaranteed to reach the screen, and narrowing
    // the column to nothing is what makes the ends unmistakably gradual.
    float fade = TornadoFade();
    float wide = (fade < TORN_FADE_MIN_WIDTH) ? TORN_FADE_MIN_WIDTH : fade;  // never a degenerate matrix
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
    TornadoApplyFade(fade);
}

// The funnel model is spawned once and then kept for the rest of the stage. The effect
// system's own spawn node still points at the GObj, so destroying it by hand leaves that
// node dangling and the engine double-frees the GObj the next time it retires the group
// - which corrupts the GObj free list and asserts out of the next GObj_AddUserData.
// Hiding the whole tree is what ends a tornado instead.
static void TornadoHideModel(void)
{
    if (!TornadoModelAlive(stc_model))
    {
        stc_model = NULL;
        return;
    }
    JObj_SetFlagsAll((JOBJ *)stc_model->hsd_object, JOBJ_HIDDEN);
}

// Global index of the region the synthesized break should be attributed to: the
// record's regions are a contiguous slice of the global array.
static int TornadoRecordRegionIndex(void *record)
{
    YakuCollRegion *base = Yaku_GetRegionArray();
    YakuCollRegion *regions = Yaku_InstanceRegions(record);
    if (!base || !regions)
        return -1;
    int idx = (int)(regions - base);
    return (idx >= 0) ? idx : -1;
}

static int TornadoIsWeakFamily(GOBJ *yaku_gobj)
{
    YakumonoData *yd = (YakumonoData *)yaku_gobj->userdata;
    if (!yd)
        return 0;
    return Yaku_GetDescCollFunc(yd->desc_id) == TORN_HIT_WEAK_OBJECT;
}

// Break a carried prop through its own family coll_func so the break runs with every
// genuine consequence - debris, item drops, break-count credit, broken state. The
// collider is synthesized: a huge radius clears any prop's HP in one hit, and the
// frame delta has to point INTO the contacted region's outward normal or the engine
// clamps the impact speed to zero and nothing breaks.
static int TornadoBreakInstance(void *record)
{
    void *holder = Yaku_GetCollHolder();
    GOBJ *yaku_gobj = Yaku_InstanceParent(record);
    if (!holder || !yaku_gobj)
        return 0;

    int base_idx = TornadoRecordRegionIndex(record);
    if (base_idx < 0)
        return 0;

    YakuCollRegion *regions = Yaku_GetRegionArray();
    int region_count = Yaku_InstanceRegionCount(record);
    if (region_count <= 0)
        region_count = 1;

    int region_idx = -1;
    Vec3 n_unit = {0.0f, 0.0f, 0.0f};
    for (int k = 0; k < region_count; k++)
    {
        Vec3 n = regions[base_idx + k].normal;
        if (VECSquareMag(&n) > 1.0e-6f)
        {
            VECNormalize(&n, &n_unit);
            region_idx = base_idx + k;
            break;
        }
    }
    if (region_idx < 0)
        return 0; // degenerate prop (no usable normal)

    // The family tail's "still collidable?" guard has to pass, and the flight retired
    // this record's collision.
    grScene_SetInstanceColl(record, 1);

    // mpCollInfo+0x1d0 = -1 marks "no BigStar region", so destroyBigStar returns 0
    // and the break proceeds.
    u8 coll_info[0x200];
    CollData coll;
    memset(coll_info, 0, sizeof(coll_info));
    memset(&coll, 0, sizeof(coll));
    *(int *)(coll_info + 0x1d0) = -1;
    coll.g = NULL;  // ownerless: the tornado credits the break to nobody
    coll.coll_info = (mpCollInfo *)coll_info;
    coll.radius = TORN_BREAK_RADIUS;
    VECScale(&n_unit, &coll.pos_delta, -TORN_BREAK_DELTA);

    // Skip the geometry-refined impact path; it can rewrite a synthetic delta from
    // the prop's own matrices.
    YakuCollRegion *region = &regions[region_idx];
    u32 saved = region->refine_flags;
    region->refine_flags = saved & ~(u32)YAKU_REGION_REFINE;

    Vec3 contact;
    Yaku_InstanceCachedPos(record, &contact);

    collideWithObject(yaku_gobj, &coll, holder, region_idx, &contact);

    region->refine_flags = saved;

    if (!grScene_IsInstanceCollAll(record, 1))
    {
        // The weak families never hide the dragged intact mesh inline, so clearing
        // USER_DEF_MTX drops the joint back to its degenerate SRT instead of leaving
        // a whole tree frozen in mid-air.
        void *jobj = Yaku_InstanceJObj(record);
        if (jobj && TornadoIsWeakFamily(yaku_gobj))
            JObj_ClearFlags((JOBJ *)jobj, JOBJ_USER_DEFINED_MTX);
        return 1;
    }

    // It did not fire - keep the prop retired for the rest of the flight.
    grScene_SetInstanceColl(record, 0);
    return 0;
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

static float TornadoRange(float lo, float hi)
{
    return lo + (hi - lo) * HSD_Randf();
}

static void TornadoDrawVary(OrbitVary *v)
{
    v->spin   = TornadoRange(TORN_VARY_SPIN_LO, TORN_VARY_SPIN_HI);
    v->ring   = TornadoRange(TORN_VARY_RING_LO, TORN_VARY_RING_HI);
    v->lift   = TornadoRange(TORN_VARY_LIFT_LO, TORN_VARY_LIFT_HI);
    v->wobble = TornadoRange(TORN_VARY_WOB_LO, TORN_VARY_WOB_HI);
    v->phase  = HSD_Randf() * 2.0f * TORN_PI;
}

// Advance one carried position around the funnel: rotate about the axis, shed part of
// the distance to the target's own ring, and climb. Derived from the live position every
// frame, so a target nudged by something else self-corrects instead of drifting out of
// the swirl.
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
    if (v->phase > 2.0f * TORN_PI)
        v->phase -= 2.0f * TORN_PI;
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

static int TornadoYakuClaimed(void *record)
{
    for (int i = 0; i < stc_yaku_count; i++)
        if (stc_yaku[i].record == record)
            return 1;
    return 0;
}

// Boxes are left alone - only loose power-ups get swept.
static int TornadoItemIsTarget(GOBJ *g, ItemData **out)
{
    if (g->entity_class != ITEM_GOBJ_KIND)
        return 0;
    ItemData *it = (ItemData *)g->userdata;
    if (!it || it->item_category == 0)
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
        if (g->entity_class != MACHINE_GOBJ_KIND)
            continue;
        MachineData *md = (MachineData *)g->userdata;
        if (!md || md->rider_gobj != NULL) // ridden machines are pushed, not carried
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

// The CT breakables the tornado is allowed to rip up: star pole, forest pitfall,
// coral, trees, rocks, volcano walls, volcano-base holes, houses. Passive zones and
// the big set-piece structures are left alone.
static int TornadoIsBreakableYaku(int desc_id)
{
    switch (desc_id)
    {
    case 29: case 32: case 33: case 34:
    case 35: case 36: case 37: case 38:
        return 1;
    default:
        return 0;
    }
}

static int TornadoCollectBreakParents(GOBJ **out, int max)
{
    int n = 0;
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_YAKUMONO];
         g != NULL && n < max; g = g->next)
    {
        if (g->entity_class != YAKUMONO_GOBJ_KIND)
            continue;
        YakumonoData *yd = (YakumonoData *)g->userdata;
        if (yd && TornadoIsBreakableYaku(yd->desc_id))
            out[n++] = g;
    }
    return n;
}

// Claim intact props inside the funnel. Collision is retired at claim time so nobody
// runs into a prop that is already airborne, and the joint is marked user-defined so
// the skeleton families stop rebuilding their matrix from the authored SRT.
static void TornadoClaimYakumono(void)
{
    int count = 0;
    void *pool = Yaku_GetInstancePool(&count);
    if (!pool)
        return;

    GOBJ *parents[TORN_MAX_PARENTS];
    int nparents = TornadoCollectBreakParents(parents, TORN_MAX_PARENTS);
    if (nparents == 0)
        return;

    for (int i = 0; i < count && stc_yaku_count < TORN_MAX_YAKU; i++)
    {
        void *record = Yaku_GetInstance(pool, i);
        GOBJ *owner = Yaku_InstanceParent(record);
        if (!owner)
            continue;

        int is_target = 0;
        for (int p = 0; p < nparents; p++)
        {
            if (parents[p] == owner)
            {
                is_target = 1;
                break;
            }
        }
        if (!is_target || TornadoYakuClaimed(record))
            continue;
        if (!grScene_IsInstanceCollAll(record, 1)) // already broken
            continue;

        Vec3 pos;
        Yaku_InstanceCachedPos(record, &pos);
        if (!TornadoInReach(&pos, NULL))
            continue;

        JOBJ *j = (JOBJ *)Yaku_InstanceJObj(record);
        if (!j)
            continue;

        JObj_SetFlags(j, JOBJ_USER_DEFINED_MTX);
        grScene_SetInstanceColl(record, 0);
        stc_yaku[stc_yaku_count].record = record;
        stc_yaku[stc_yaku_count].held = 0;
        TornadoDrawVary(&stc_yaku[stc_yaku_count].vary);
        stc_yaku_count++;
    }
}

// Carried items keep their claim after the funnel has moved on, so the physics that
// would fight the override is neutralized every frame.
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
        it->x35a &= (u8)~0x10; // clear grounded
    }
}

// Released items fall back under vanilla physics: re-arming the ground check is all
// that is needed, since the tornado only ever zeroed their velocity.
static void TornadoReleaseItems(void)
{
    for (int i = 0; i < stc_item_count; i++)
    {
        if (TornadoItemIsLive(stc_items[i].item))
            stc_items[i].item->is_airborne = 1;
    }
    stc_item_count = 0;
}

static int TornadoMachineIsLive(MachineData *md)
{
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_MACHINE]; g != NULL; g = g->next)
    {
        if (g->entity_class != MACHINE_GOBJ_KIND)
            continue;
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
        // handled as a rider from the next frame.
        if (!TornadoMachineIsLive(md))
        {
            stc_machines[i] = stc_machines[--stc_machine_count];
            continue;
        }

        TornadoOrbit(&md->pos, &stc_machines[i].vary);

        // Machine_PhysicsThink integrates accel and velocity into pos every frame.
        float *accel = (float *)((char *)md + 0x318);
        accel[0] = 0.0f;
        accel[1] = 0.0f;
        accel[2] = 0.0f;
        md->velocity.X = 0.0f;
        md->velocity.Y = 0.0f;
        md->velocity.Z = 0.0f;
    }
}

static void TornadoProcessYakumono(int force_break)
{
    for (int i = stc_yaku_count - 1; i >= 0; i--)
    {
        void *record = stc_yaku[i].record;
        JOBJ *j = (JOBJ *)Yaku_InstanceJObj(record);
        if (!j)
        {
            stc_yaku[i] = stc_yaku[--stc_yaku_count];
            continue;
        }

        // The prop's real transform is its JObj world matrix; the cached copy at
        // record+0x2c is what the break reads back for its contact point.
        float *m = (float *)&j->rotMtx;
        Vec3 pos = {m[3], m[7], m[11]};

        stc_yaku[i].held++;
        if (force_break || stc_yaku[i].held >= TORN_YAKU_HOLD)
        {
            float *cached = (float *)((char *)record + YAKU_INST_MATRIX);
            cached[3] = pos.X;
            cached[7] = pos.Y;
            cached[11] = pos.Z;
            TornadoBreakInstance(record);
            stc_yaku[i] = stc_yaku[--stc_yaku_count];
            continue;
        }

        TornadoOrbit(&pos, &stc_yaku[i].vary);
        JObj_SetFlags(j, JOBJ_USER_DEFINED_MTX);
        m[3] = pos.X;
        m[7] = pos.Y;
        m[11] = pos.Z;
        grScene_SetInstanceColl(record, 0); // keep it retired for the whole flight
    }
}

// Riders are pushed, not possessed: an inward + tangential + upward acceleration is
// added to the machine's own velocity, so speed and boost still count for something.
static void TornadoPushRiders(void)
{
    for (int i = 0; i < WEATHER_PLAYER_SLOTS; i++)
    {
        if (stc_playerdata[i].player_kind == PKIND_NONE)
            continue;
        GOBJ *mg = Ply_GetMachineGObj(i);
        if (!mg || !mg->userdata)
            continue;
        MachineData *md = (MachineData *)mg->userdata;
        if (md->is_dead || md->is_fall_dead)
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

// The shake record sits past the end of the declared PlayerCamData, so it is
// sanity-checked as an aligned MEM1 pointer before anything is written through it.
static int TornadoValidPtr(void *p)
{
    u32 a = (u32)p;
    return p != NULL && (a & 3) == 0 && a >= 0x80000000 && a < 0x81800000;
}

static void *TornadoShakeRecord(GOBJ *cam_gobj)
{
    if (!cam_gobj || !cam_gobj->userdata)
        return NULL;
    void *rec = *(void **)((char *)cam_gobj->userdata + TORN_SHAKE_REC_OFF);
    return TornadoValidPtr(rec) ? rec : NULL;
}

static void TornadoWriteShake(void *rec, float right, float up, float gate)
{
    *(float *)((char *)rec + TORN_SHAKE_EYE_R) = right;
    *(float *)((char *)rec + TORN_SHAKE_EYE_U) = up;
    *(float *)((char *)rec + TORN_SHAKE_GATE) = gate;
}

// A light shake on the cameras of players near the funnel, falling off to nothing at
// the outer reach. Distance is measured from the view's own aim point (CamData.x14's
// interest), which is what that camera is actually looking at.
static void TornadoShakeCameras(void)
{
    PlayerCamLookup *lookup = stc_plycam_lookup;
    if (!lookup)
        return;

    int on = (shake_index == 0);
    float outer = stc_reach * TORN_SHAKE_REACH;

    for (int i = 0; i < CM_CAMERA_MAX; i++)
    {
        GOBJ *cg = lookup->cam_gobjs[i];
        void *rec = TornadoShakeRecord(cg);
        if (!rec)
            continue;
        CamData *cam = ((PlayerCamData *)cg->userdata)->cam_data;
        if (!cam)
            continue;

        float amp = 0.0f;
        if (on)
        {
            float dx = cam->x14.interest.X - stc_cx;
            float dz = cam->x14.interest.Z - stc_cz;
            float d = sqrtf(dx * dx + dz * dz);
            if (d < outer)
                amp = TORN_SHAKE_AMP * stc_strength * (1.0f - d / outer);
        }

        if (amp <= 0.0f)
            TornadoWriteShake(rec, 0.0f, 0.0f, 0.0f);
        else
            TornadoWriteShake(rec, Weather_Randf2() * amp, Weather_Randf2() * amp, 1.0f);
    }
}

// The gate is sticky, so it has to be lowered explicitly when the funnel lifts.
static void TornadoStopShake(void)
{
    PlayerCamLookup *lookup = stc_plycam_lookup;
    if (!lookup)
        return;
    for (int i = 0; i < CM_CAMERA_MAX; i++)
    {
        void *rec = TornadoShakeRecord(lookup->cam_gobjs[i]);
        if (rec)
            TornadoWriteShake(rec, 0.0f, 0.0f, 0.0f);
    }
}

// Everything still in flight when the funnel lifts: props are torn apart (they have
// no intact place left to land), items fall, machines drop out of the sky.
static void TornadoEndFunnel(void)
{
    TornadoProcessYakumono(1);
    TornadoReleaseItems();
    stc_machine_count = 0;
    stc_yaku_count = 0;
    TornadoStopShake();
    TornadoHideModel();
    stc_live = 0;
    stc_frames_left = 0;
}

// Spread the round's tornadoes over the match, one per equal slice with jitter inside
// the slice. Entries already behind `p` are skipped so re-planning mid-round does not
// replay them.
static void SeedSchedule(int n, float p)
{
    for (int i = 0; i < n; i++)
        stc_schedule[i] = ((float)i + 0.15f + 0.70f * HSD_Randf()) / (float)n;
    stc_next = 0;
    while (stc_next < n && stc_schedule[stc_next] <= p)
        stc_next++;
    stc_scheduled = n;
}

// The disc the funnel is allowed to roam: centered on the out-of-bounds box, with the
// radius taken from its shorter half-extent so the funnel never leans on a wall.
static void TornadoPlayArea(StageNode *sn, float *cx, float *cz, float *radius)
{
    float hx = 0.5f * (sn->oob_max.X - sn->oob_min.X);
    float hz = 0.5f * (sn->oob_max.Z - sn->oob_min.Z);
    *cx = 0.5f * (sn->oob_min.X + sn->oob_max.X);
    *cz = 0.5f * (sn->oob_min.Z + sn->oob_max.Z);
    *radius = ((hx < hz) ? hx : hz) * TORN_PLAY_FRACTION;
}

// A point uniform over the play disc's area, so waypoints are as likely to be drawn
// through the middle of the city as around its rim.
static void TornadoPickPoint(StageNode *sn, float *x, float *z)
{
    float cx, cz, r;
    TornadoPlayArea(sn, &cx, &cz, &r);
    float a = HSD_Randf() * 2.0f * TORN_PI;
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
    stc_item_count = 0;
    stc_yaku_count = 0;
    stc_machine_count = 0;
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

// Fold the menu overrides over the latched preset config. Returns 0 when no tornado
// can appear this round.
static int ResolveConfig(void)
{
    if (!WeatherToggle(show_index, stc_active))
        return 0;

    stc_count = (count_index > 0) ? count_values[count_index] : stc_def_count;
    if (stc_count <= 0)
        return 0;
    if (stc_count > TORN_MAX_COUNT)
        stc_count = TORN_MAX_COUNT;

    stc_duration = (duration_index > 0)
                       ? (int)(stc_def_duration * duration_factors[duration_index])
                       : stc_def_duration;
    if (stc_duration < 1)
        stc_duration = 1;

    stc_size = (size_index > 0) ? size_factors[size_index] : stc_def_size;
    stc_strength = (strength_index > 0) ? strength_factors[strength_index] : stc_def_strength;
    stc_speed = stc_def_speed;

    stc_core = TORN_CORE_RADIUS * stc_size;
    stc_reach = TORN_INFLUENCE * stc_size;
    stc_top = TORN_HEIGHT * stc_size;
    return 1;
}

void Tornado_SetActive(const TornadoDef *def)
{
    stc_active = (def && def->enabled) ? 1 : 0;

    stc_def_count    = (def && def->count > 0) ? def->count : TORN_DEF_COUNT;
    stc_def_duration = (def && def->duration > 0) ? def->duration : TORN_DEF_DURATION;
    stc_def_size     = (def && def->size > 0.0f) ? def->size : TORN_DEF_SIZE;
    stc_def_strength = (def && def->strength > 0.0f) ? def->strength : TORN_DEF_STRENGTH;
    stc_def_speed    = (def && def->speed > 0.0f) ? def->speed : TORN_DEF_SPEED;

    // A new preset mid-round reschedules the tornadoes it has left.
    stc_scheduled = 0;
    if (stc_live)
        TornadoEndFunnel();
}

void Tornado_Tick(void)
{
    if (!ResolveConfig())
    {
        if (stc_live)
            TornadoEndFunnel();
        return;
    }

    StageNode *sn = TornadoStageNode();
    if (!sn)
        return;

    float p = Weather_RoundProgress();
    if (p < 0.0f)
        return;

    if (stc_scheduled != stc_count)
        SeedSchedule(stc_count, p);

    if (stc_live)
    {
        if (--stc_frames_left <= 0)
        {
            OSReport("[Tornado] Lifted at (%d, %d)\n", (int)stc_cx, (int)stc_cz);
            TornadoEndFunnel();
            return;
        }
        TornadoWander(sn);
        stc_spin += TORN_MODEL_SPIN * stc_strength;
        if (stc_spin > 2.0f * TORN_PI)
            stc_spin -= 2.0f * TORN_PI;
        TornadoPlaceModel();
        TornadoClaimItems();
        TornadoClaimMachines();
        TornadoClaimYakumono();
        return;
    }

    if (stc_next < stc_count && p >= stc_schedule[stc_next])
    {
        stc_next++;
        TornadoTouchDown(sn);
        TornadoPlaceModel();
        OSReport("[Tornado] Touchdown %d/%d at progress %d%%, (%d, %d), %d frames\n",
                 stc_next, stc_count, (int)(p * 100.0f),
                 (int)stc_cx, (int)stc_cz, stc_duration);
    }
}

// Runs after the frame's game procs, so these position writes are the last word over
// item physics, machine physics and the ground snap.
void Tornado_OnFrameEnd(void)
{
    if (!stc_live)
        return;

    // This hook fires in every scene, but the per-stage reset only runs on the next
    // City Trial entry - so leaving CT with a funnel up would otherwise walk claims
    // full of freed pointers. Drop everything the moment the stage stops being CT.
    GrObj *gr = *stc_grobj;
    if (!gr || gr->gr_kind != GR_CITY1 || !TornadoStageNode())
    {
        Tornado_Reset();
        return;
    }

    TornadoProcessItems();
    TornadoProcessMachines();
    TornadoProcessYakumono(0);
    TornadoPushRiders();
    TornadoShakeCameras();
}

void Tornado_Reset(void)
{
    stc_active = 0;
    stc_scheduled = 0;
    stc_next = 0;
    stc_live = 0;
    stc_frames_left = 0;
    stc_item_count = 0;
    stc_yaku_count = 0;
    stc_machine_count = 0;
    stc_model = NULL;
    stc_mat_count = 0;  // the materials belong to that GObj
    stc_model_ready = 0;
}

MenuDesc tornado_menu = {
    .option_num = 6,
    .options = {
        &(OptionDesc){
            .name = "Tornado",
            .description = "Let a tornado sweep City Trial: Preset = only presets that set it, Off = never, On = every CT preset",
            .kind = OPTKIND_VALUE,
            .val = &show_index,
            .value_num = 3,
            .value_names = toggle_names,
        },
        &(OptionDesc){
            .name = "Appearances",
            .description = "How many tornadoes touch down over a City Trial round, spread across the match timer",
            .kind = OPTKIND_VALUE,
            .val = &count_index,
            .value_num = TORN_COUNT_NUM,
            .value_names = count_names,
        },
        &(OptionDesc){
            .name = "Duration",
            .description = "How long each tornado roams the city before it lifts",
            .kind = OPTKIND_VALUE,
            .val = &duration_index,
            .value_num = TORN_DURATION_NUM,
            .value_names = duration_names,
        },
        &(OptionDesc){
            .name = "Size",
            .description = "Funnel scale, which also sets how far out it can reach for debris and riders",
            .kind = OPTKIND_VALUE,
            .val = &size_index,
            .value_num = TORN_SIZE_NUM,
            .value_names = size_names,
        },
        &(OptionDesc){
            .name = "Strength",
            .description = "How hard the funnel spins what it catches and how much it drags at a passing rider",
            .kind = OPTKIND_VALUE,
            .val = &strength_index,
            .value_num = TORN_STRENGTH_NUM,
            .value_names = strength_names,
        },
        &(OptionDesc){
            .name = "Screen Shake",
            .description = "Shake the camera of players who get close to a tornado",
            .kind = OPTKIND_VALUE,
            .val = &shake_index,
            .value_num = 2,
            .value_names = shake_names,
        },
    },
};
