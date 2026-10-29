// Ground puddles for custom_weather: a roaming field of shallow pools on the
// City Trial ground that drag any machine driving through them.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "gx.h"
#include "machine.h"
#include "collision.h"
#include "inline.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

#define PUDDLE_MAX            64      // pool capacity; resolved count clamps to this
#define PUDDLE_SEGMENTS       22      // rim vertices per disc (triangle fan)
#define PUDDLE_LIFT           2.0f    // raise the disc above the ground to beat z-fighting
#define PUDDLE_PLAY_FRACTION  0.72f   // scatter within this fraction of the OoB box (keep off the rim)
#define PUDDLE_MIN_NORMAL_Y   0.85f   // reject hits steeper than this (only near-flat ground)
#define PUDDLE_OVAL_MIN       0.55f   // shortest oval axis = radius * [this .. 1]
#define PUDDLE_PICK_ATTEMPTS  12      // ground-raycast tries per (re)spawn; about a third of the box is land
#define PUDDLE_ALPHA_EPS      0.01f   // below this opacity a pool neither draws nor drags
#define PUDDLE_DRAG_HEIGHT    30.0f   // a machine this far above or below a pool is on another floor

// Lifecycle timing in frames: a slot waits dormant, wells up over FADE_IN, holds a
// random HOLD, dries out over FADE_OUT, then waits a random GAP before re-rolling a
// spot. INIT_STAGGER spreads the first appearances across the round's opening.
#define PUDDLE_FADE_IN        24
#define PUDDLE_FADE_OUT       36
#define PUDDLE_HOLD_MIN       300
#define PUDDLE_HOLD_MAX       900
#define PUDDLE_GAP_MIN        120
#define PUDDLE_GAP_MAX        480
#define PUDDLE_INIT_STAGGER   240
#define PUDDLE_RETRY_GAP      30      // dormant re-wait when a spot pick finds no ground

// Defaults applied when a preset leaves the matching PuddleDef field 0.
static const GXColor puddle_def_color = {150, 178, 205, 195};  // bright reflective pool
#define PUDDLE_DEF_COUNT     24
#define PUDDLE_DEF_RADIUS    32.0f
#define PUDDLE_DEF_FACTOR    0.90f                  // damp horizontal velocity 10%/frame inside
#define PUDDLE_MIN_RADIUS    1.0f                   // PointInPuddle divides by the axes

typedef enum PuddlePhase
{
    PUD_DORMANT = 0, // hidden, waiting to (re)appear at a new spot
    PUD_FADE_IN,     // welling up (alpha 0 -> 1)
    PUD_HELD,        // fully present
    PUD_FADE_OUT,    // drying out (alpha 1 -> 0)
} PuddlePhase;

typedef struct Puddle
{
    Vec3  center;  // world center, on the surface (lifted slightly along the normal)
    Vec3  u;       // unit in-plane tangent (the rx axis), from the ground normal
    Vec3  v;       // unit in-plane tangent (the rz axis), from the ground normal
    float rx;      // ellipse half-extent along u
    float rz;      // ellipse half-extent along v
    int   phase;   // PuddlePhase
    int   timer;   // frames remaining in the current phase
    float alpha;   // 0..1 current opacity / drag scale
} Puddle;

static GOBJ *stc_puddle_gobj = NULL;

static int stc_active = 0;
static int stc_inited = 0;

static Puddle stc_puddles[PUDDLE_MAX];
static int    stc_count = 0;

// Resolved per-preset config (PuddleDef + defaults), before the menu scalars.
static GXColor stc_color;
static int     stc_base_count;
static float   stc_base_radius;
static float   stc_base_factor;

// The rim's unit circle, shared by every pool.
static float stc_rim_cos[PUDDLE_SEGMENTS + 1];
static float stc_rim_sin[PUDDLE_SEGMENTS + 1];
static int   stc_rim_seeded = 0;

// Index 0 ("Preset") is the pass-through value on every knob below. Slowdown scales
// the per-preset drag amount (1 - factor); Off removes the slow but still draws.
static const float slow_strength_factors[] = {1.0f, 0.0f, 0.5f, 1.5f, 2.0f};
static char *slow_strength_names[] = {"Preset", "Off", "50%", "150%", "200%"};
WEATHER_KNOB_CHECK(slow_strength_factors, slow_strength_names);
static int slow_strength_index = 0;

// Frequency scales the slot count.
static const float freq_factors[] = {1.0f, 0.5f, 1.75f};
static char *freq_names[] = {"Preset", "Few", "Many"};
WEATHER_KNOB_CHECK(freq_factors, freq_names);
static int freq_index = 0;

// Size scales the radius.
static const float size_factors[] = {1.0f, 0.7f, 1.4f};
static char *size_names[] = {"Preset", "Small", "Large"};
WEATHER_KNOB_CHECK(size_factors, size_names);
static int size_index = 0;

static int puddle_roaming = 1;
static int show_puddles = 1;

// Returns 0 when no ground was found: stage not ready, or every try landed on a wall
// or the void.
static int PickSpot(Puddle *p)
{
    StageNode *sn = Weather_StageNode();
    if (!sn)
        return 0;

    float radius = stc_base_radius * size_factors[size_index];
    if (radius < PUDDLE_MIN_RADIUS)
        radius = PUDDLE_MIN_RADIUS;

    for (int a = 0; a < PUDDLE_PICK_ATTEMPTS; a++)
    {
        float x, z;
        Weather_RandBoxPoint(sn, PUDDLE_PLAY_FRACTION, &x, &z);

        Vec3 hit;
        int tri = Weather_CastDown(sn, x, z, sn->oob_min.Y - WEATHER_CAST_MARGIN, &hit);
        if (tri < 0 || grGetGroundTypeFromTriangleID(tri) == GRCOLL_GROUNDTYPE_SEA)
            continue; // no ground below this XZ, or open sea nobody drives on

        Vec3 nrm;
        PointCollision_GetNormalByID(tri, &nrm);
        if (nrm.Y < PUDDLE_MIN_NORMAL_Y)
            continue; // wall / steep ramp - not flat ground

        // Orthonormal tangent basis in the surface plane. nrm.Y >= 0.85 (near
        // up), so world +X is safely non-parallel to the normal.
        Vec3 ref = {1.0f, 0.0f, 0.0f};
        Vec3 u, v;
        VEC_CrossNormalizeSnap(&ref, &nrm, &u); // u = normalize(ref x nrm), in-plane
        VECCrossProduct(&nrm, &u, &v);          // v = nrm x u, unit & in-plane

        // Circular by default; at random, stretch one axis into an oval.
        float aspect = RandomInRangef(PUDDLE_OVAL_MIN, 1.0f);
        float ax = radius;
        float az = radius * aspect;
        if (HSD_Randf() < 0.5f)
        {
            float t = ax;
            ax = az;
            az = t;
        }

        // Seat the disc off the surface along the normal, not world +Y, so the
        // offset is perpendicular to a sloped face too.
        p->center.X = hit.X + nrm.X * PUDDLE_LIFT;
        p->center.Y = hit.Y + nrm.Y * PUDDLE_LIFT;
        p->center.Z = hit.Z + nrm.Z * PUDDLE_LIFT;
        p->u = u;
        p->v = v;
        p->rx = ax;
        p->rz = az;
        return 1;
    }
    return 0;
}

// Advance one pool slot's lifecycle by a frame. `roaming` off freezes the hold phase
// so the field, once surfaced, stays put.
static void StepPuddle(Puddle *p, int roaming)
{
    switch (p->phase)
    {
    case PUD_DORMANT:
        if (--p->timer <= 0)
        {
            if (PickSpot(p))
            {
                p->phase = PUD_FADE_IN;
                p->timer = PUDDLE_FADE_IN;
                p->alpha = 0.0f;
            }
            else
            {
                p->timer = PUDDLE_RETRY_GAP; // stage not ready / no ground - retry soon
            }
        }
        break;

    case PUD_FADE_IN:
        if (--p->timer <= 0)
        {
            p->alpha = 1.0f;
            p->phase = PUD_HELD;
            p->timer = RandomInRange(PUDDLE_HOLD_MIN, PUDDLE_HOLD_MAX);
        }
        else
        {
            p->alpha = 1.0f - (float)p->timer / (float)PUDDLE_FADE_IN;
        }
        break;

    case PUD_HELD:
        p->alpha = 1.0f;
        // With roaming off the hold timer is never consumed, so the pool holds
        // indefinitely.
        if (roaming && --p->timer <= 0)
        {
            p->phase = PUD_FADE_OUT;
            p->timer = PUDDLE_FADE_OUT;
        }
        break;

    case PUD_FADE_OUT:
        if (--p->timer <= 0)
        {
            p->alpha = 0.0f;
            p->phase = PUD_DORMANT;
            p->timer = RandomInRange(PUDDLE_GAP_MIN, PUDDLE_GAP_MAX);
        }
        else
        {
            p->alpha = (float)p->timer / (float)PUDDLE_FADE_OUT;
        }
        break;
    }
}

// A pool is cast onto the highest floor under its spot, so a machine on another floor
// at the same XZ is outside it.
static int PointInPuddle(const Vec3 *pos, const Puddle *p)
{
    float oy = pos->Y - p->center.Y;
    if (oy > PUDDLE_DRAG_HEIGHT || oy < -PUDDLE_DRAG_HEIGHT)
        return 0;

    // The axes' Y components are tiny on near-flat ground, so the XZ projection is an
    // accurate footprint.
    float ox = pos->X - p->center.X;
    float oz = pos->Z - p->center.Z;
    float du = (ox * p->u.X + oz * p->u.Z) / p->rx;
    float dv = (ox * p->v.X + oz * p->v.Z) / p->rz;
    return (du * du + dv * dv) <= 1.0f;
}

// Positions are rolled lazily per slot in StepPuddle, so this needs no stage loaded.
static void Puddle_Arm(void)
{
    stc_count = Weather_ScaleCount(stc_base_count, freq_factors[freq_index], PUDDLE_MAX);

    for (int i = 0; i < stc_count; i++)
    {
        Puddle *p = &stc_puddles[i];
        p->phase = PUD_DORMANT;
        p->timer = RandomInRange(0, PUDDLE_INIT_STAGGER);
        p->alpha = 0.0f;
    }

    stc_inited = 1;
    OSReport("[Puddle] Armed %d pools (roaming %s)\n",
             stc_count, puddle_roaming ? "on" : "off");
}

// GX callback on the world camera link, XLU pass. Each surfaced pool is a flat
// translucent fan scaled by its fade opacity.
static void Puddle_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != GXPASS_TEXEDGE)
        return;
    if (!stc_active || !show_puddles || stc_count <= 0)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    if (!stc_rim_seeded)
    {
        for (int s = 0; s <= PUDDLE_SEGMENTS; s++)
        {
            float ang = (float)s * (2.0f * M_PI / (float)PUDDLE_SEGMENTS);
            stc_rim_cos[s] = cosf(ang);
            stc_rim_sin[s] = sinf(ang);
        }
        stc_rim_seeded = 1;
    }

    GX_BeginXlu(cam, RENDER_VERTEX, GX_BL_INVSRCALPHA);

    for (int i = 0; i < stc_count; i++)
    {
        Puddle *p = &stc_puddles[i];
        if (p->alpha <= PUDDLE_ALPHA_EPS)
            continue;

        // The rim at half the centre's opacity: a soft but present edge.
        u8 ca = (u8)((float)stc_color.a * p->alpha);
        u8 ra = ca / 2;

        // Fan = center vertex + a closed rim ring (rim[0] repeated to seal it).
        // Each rim point is center + (cos*rx)*u + (sin*rz)*v, so the disc lies in
        // the surface plane rather than the world XZ plane.
        GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, PUDDLE_SEGMENTS + 2);
        GXPosition3f32(p->center.X, p->center.Y, p->center.Z);
        GXColor4u8(stc_color.r, stc_color.g, stc_color.b, ca);
        for (int s = 0; s <= PUDDLE_SEGMENTS; s++)
        {
            float cu = stc_rim_cos[s] * p->rx;
            float cv = stc_rim_sin[s] * p->rz;
            float vx = p->center.X + cu * p->u.X + cv * p->v.X;
            float vy = p->center.Y + cu * p->u.Y + cv * p->v.Y;
            float vz = p->center.Z + cu * p->u.Z + cv * p->v.Z;
            GXPosition3f32(vx, vy, vz);
            GXColor4u8(stc_color.r, stc_color.g, stc_color.b, ra);
        }
    }

    HSD_StateInvalidate(HSD_STATE_ALL);
}

void Puddle_SetActive(const PuddleDef *def)
{
    if (!def || !def->enabled)
    {
        stc_active = 0;
        return;
    }
    stc_active = 1;

    stc_color = def->color ? GXColor_Unpack(def->color) : puddle_def_color;
    stc_base_count = def->count > 0 ? def->count : PUDDLE_DEF_COUNT;
    stc_base_radius = def->radius > 0.0f ? def->radius : PUDDLE_DEF_RADIUS;
    stc_base_factor = (def->slow_factor > 0.0f && def->slow_factor < 1.0f)
                          ? def->slow_factor
                          : PUDDLE_DEF_FACTOR;

    stc_inited = 0;
    stc_count = 0;
}

void Puddle_Tick(void)
{
    if (!stc_active)
        return;

    // Base per-frame drag before per-pool opacity.
    float base_amt = (1.0f - stc_base_factor) * slow_strength_factors[slow_strength_index];

    // Invisible and inert: skip the lifecycle and its ground raycasts entirely.
    if (!show_puddles && base_amt <= 0.0f)
        return;

    if (!stc_inited)
        Puddle_Arm();
    WeatherGX_EnsureLayer(&stc_puddle_gobj, Puddle_GX, "Puddle");

    for (int i = 0; i < stc_count; i++)
        StepPuddle(&stc_puddles[i], puddle_roaming);

    // Riders are still boarding through the intro, so nothing drags them there.
    if (base_amt <= 0.0f || Weather_RoundProgress() < 0.0f)
        return;

    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        MachineData *md = Ply_GetMachineData(ply);
        if (md == NULL)
            continue;
        if (md->is_airborne != 0 || md->is_dead) // grounded states only
            continue;

        for (int i = 0; i < stc_count; i++)
        {
            Puddle *p = &stc_puddles[i];
            if (p->alpha <= PUDDLE_ALPHA_EPS)
                continue;
            if (!PointInPuddle(&md->pos, p))
                continue;

            // Drag scales with how filled the pool is, so a forming or drying pool
            // bites less than a full one.
            float amt = base_amt * p->alpha;
            if (amt > 0.99f)
                amt = 0.99f;
            md->velocity.X *= (1.0f - amt);
            md->velocity.Z *= (1.0f - amt);
            break; // one pool's drag per frame, even where they overlap
        }
    }
}

void Puddle_Reset(void)
{
    stc_puddle_gobj = NULL;
    stc_inited = 0;
    stc_count = 0;
    stc_active = 0;
}

static void OnPuddleSlowChange(int val)
{
    OSReport("[Puddle] Slowdown %s\n", slow_strength_names[val]);
}

static void OnPuddleFreqChange(int val)
{
    OSReport("[Puddle] Frequency %s\n", freq_names[val]);
}

static void OnPuddleSizeChange(int val)
{
    OSReport("[Puddle] Size %s\n", size_names[val]);
}

static void OnPuddleRoamingChange(int val)
{
    OSReport("[Puddle] Roaming %s\n", weather_onoff_names[val]);
}

MenuDesc puddle_menu = {
    .option_num = 5,
    .options = {
        &(OptionDesc){
            .name = "Slowdown",
            .description = "How hard puddles drag a machine driving through them (Off = discs only, no slow)",
            .kind = OPTKIND_VALUE,
            .val = &slow_strength_index,
            .value_num = GetElementsIn(slow_strength_factors),
            .value_names = slow_strength_names,
            .on_change = OnPuddleSlowChange,
        },
        &(OptionDesc){
            .name = "Frequency",
            .description = "How many puddles the field carries",
            .kind = OPTKIND_VALUE,
            .val = &freq_index,
            .value_num = GetElementsIn(freq_factors),
            .value_names = freq_names,
            .on_change = OnPuddleFreqChange,
        },
        &(OptionDesc){
            .name = "Size",
            .description = "How large the puddles are",
            .kind = OPTKIND_VALUE,
            .val = &size_index,
            .value_num = GetElementsIn(size_factors),
            .value_names = size_names,
            .on_change = OnPuddleSizeChange,
        },
        &(OptionDesc){
            .name = "Roaming",
            .description = "Puddles fade in and out at new spots over time (Off = a fixed field)",
            .kind = OPTKIND_VALUE,
            .val = &puddle_roaming,
            .value_num = WEATHER_ONOFF_NUM,
            .value_names = weather_onoff_names,
            .on_change = OnPuddleRoamingChange,
        },
        &(OptionDesc){
            .name = "Show Puddles",
            .description = "Draw the puddle discs (the slowdown still applies when off)",
            .kind = OPTKIND_VALUE,
            .val = &show_puddles,
            .value_num = WEATHER_ONOFF_NUM,
            .value_names = weather_onoff_names,
        },
    },
};
