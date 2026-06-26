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
#include "hoshi/settings.h"

#include "custom_weather.h"

#define PUDDLE_PI  3.14159265358979f

// ---- Module constants ----
#define PUDDLE_MAX            64      // pool capacity; resolved count clamps to this
#define PUDDLE_SEGMENTS       22      // rim vertices per disc (triangle fan)
#define PUDDLE_PLAYER_SLOTS   5
#define PUDDLE_LIFT           2.0f    // raise the disc above the ground to beat z-fighting
#define PUDDLE_PLAY_FRACTION  0.72f   // scatter within this fraction of the OoB box (keep off the rim)
#define PUDDLE_MIN_NORMAL_Y   0.85f   // reject hits steeper than this (only near-flat ground)
#define PUDDLE_OVAL_MIN       0.55f   // shortest oval axis = radius * [this .. 1]
#define PUDDLE_RIM_ALPHA_NUM  1       // rim alpha = center alpha * NUM/DEN (soft but present edge)
#define PUDDLE_RIM_ALPHA_DEN  2
#define PUDDLE_PICK_ATTEMPTS  6       // ground-raycast tries per (re)spawn
#define PUDDLE_ALPHA_EPS      0.01f   // below this opacity a pool neither draws nor drags

// ---- Lifecycle timing (frames @ 60fps). Each pool slot waits dormant, wells up
// over FADE_IN, holds for a random HOLD, dries out over FADE_OUT, then waits a
// random GAP before re-rolling a new spot. INIT_STAGGER spreads the first
// appearance across the round's opening so they don't all surface at once.
#define PUDDLE_FADE_IN        24
#define PUDDLE_FADE_OUT       36
#define PUDDLE_HOLD_MIN       300
#define PUDDLE_HOLD_MAX       900
#define PUDDLE_GAP_MIN        120
#define PUDDLE_GAP_MAX        480
#define PUDDLE_INIT_STAGGER   240
#define PUDDLE_RETRY_GAP      30      // dormant re-wait when a spot pick finds no ground

// Defaults applied when a preset leaves the matching PuddleDef field 0. A light,
// sky-reflective tone at a fairly high opacity: a flat ground disc is heavily
// foreshortened at the City Trial camera angle, and a bright reflective pool is
// both what a real puddle looks like at that grazing angle and far easier to
// read against the dark, tinted wet ground than a dim dark-blue one.
#define PUDDLE_DEF_COLOR     RGBA(150, 178, 205, 195) // bright reflective pool
#define PUDDLE_DEF_COUNT     24
#define PUDDLE_DEF_RADIUS    32.0f
#define PUDDLE_DEF_FACTOR    0.90f                  // damp horizontal velocity 10%/frame inside

// Render GObj: arbitrary high entity class (avoids real engine classes), a spare
// p_link, and the world camera's gx_link 0 on the XLU sub-pass - matching the
// rain / lightning layers so the discs share the same depth-tested world pass.
#define PUDDLE_GOBJ_CLASS  203
#define PUDDLE_GOBJ_PLINK  27
#define PUDDLE_GX_LINK     0
#define PUDDLE_GX_PRI      0

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

// Cached only to avoid recreating the GObj every frame; never dereferenced (the
// engine owns it and frees it on scene teardown).
static GOBJ *stc_puddle_gobj = NULL;

static int stc_active = 0;
static int stc_inited = 0;

static Puddle stc_puddles[PUDDLE_MAX];
static int    stc_count = 0;

// Resolved per-preset config (PuddleDef + defaults), before the menu scalars.
static GXColor stc_color = {30, 55, 85, 140};
static int     stc_base_count = PUDDLE_DEF_COUNT;
static float   stc_base_radius = PUDDLE_DEF_RADIUS;
static float   stc_base_factor = PUDDLE_DEF_FACTOR;

// ---- Settings (persisted by hoshi menu save) ----
// Slowdown scales the per-preset drag *amount* (1 - factor): Off removes the
// slow entirely (discs still show), 200% doubles it. Mirrors Wind Strength.
static const float slow_strength_factors[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
static char *slow_strength_names[] = {"Off", "50%", "100%", "150%", "200%"};
#define PUDDLE_SLOW_NUM (sizeof(slow_strength_factors) / sizeof(slow_strength_factors[0]))
static int slow_strength_index = 2; // default 100%

// Frequency scales the slot count; applied at the next round's arm. Size scales
// the radius and is read at each (re)spawn, so it affects pools that surface
// after the change too.
static const float freq_factors[] = {0.5f, 1.0f, 1.75f};
static char *freq_names[] = {"Few", "Normal", "Many"};
#define PUDDLE_FREQ_NUM (sizeof(freq_factors) / sizeof(freq_factors[0]))
static int freq_index = 1; // default Normal

static const float size_factors[] = {0.7f, 1.0f, 1.4f};
static char *size_names[] = {"Small", "Normal", "Large"};
#define PUDDLE_SIZE_NUM (sizeof(size_factors) / sizeof(size_factors[0]))
static int size_index = 1; // default Normal

static char *puddle_toggle_names[] = {"Off", "On"};
static int puddle_roaming = 1; // default On: pools pop in/out and move over time
static int show_puddles = 1;   // default On: draw the discs

// Inclusive random integer in [lo, hi].
static int RandRange(int lo, int hi)
{
    if (hi <= lo)
        return lo;
    return lo + HSD_Randi(hi - lo + 1);
}

// Roll a fresh location + oval shape on flat ground and lay the disc flush in the
// surface plane. Each candidate XZ is raycast straight down against the map
// collision; only near-flat hits are kept, then the disc is built from a tangent
// basis derived from the ground normal (so it conforms to slopes instead of
// clipping through them) and seated just above the surface along that normal.
// Fills the slot's center/u/v/rx/rz on success; returns 1, or 0 if no ground was
// found (stage not ready yet, or every try landed on a wall / the void).
static int PickSpot(Puddle *p)
{
    GrObj *gr = *stc_grobj;
    if (!gr || !gr->gr_data || !gr->gr_data->stage_node)
        return 0;
    StageNode *sn = gr->gr_data->stage_node;

    float cx = (sn->oob_min.X + sn->oob_max.X) * 0.5f;
    float cz = (sn->oob_min.Z + sn->oob_max.Z) * 0.5f;
    float hx = (sn->oob_max.X - sn->oob_min.X) * 0.5f * PUDDLE_PLAY_FRACTION;
    float hz = (sn->oob_max.Z - sn->oob_min.Z) * 0.5f * PUDDLE_PLAY_FRACTION;
    float top_y = sn->oob_max.Y + 50.0f;  // raycast from above the box
    float bot_y = sn->oob_min.Y - 50.0f;  // down to below it

    float radius = stc_base_radius * size_factors[size_index];

    for (int a = 0; a < PUDDLE_PICK_ATTEMPTS; a++)
    {
        float x = cx + (HSD_Randf() * 2.0f - 1.0f) * hx;
        float z = cz + (HSD_Randf() * 2.0f - 1.0f) * hz;

        Vec3 start = {x, top_y, z};
        Vec3 end = {x, bot_y, z};
        Vec3 hit;
        int tri = EnvColl_Raycast(&start, &end, &hit);
        if (tri < 0)
            continue; // no ground below this XZ (void / off the plaza)

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
        float aspect = PUDDLE_OVAL_MIN + HSD_Randf() * (1.0f - PUDDLE_OVAL_MIN);
        float ax = radius;
        float az = radius * aspect;
        if (HSD_Randf() < 0.5f)
        {
            float t = ax;
            ax = az;
            az = t;
        }

        // Seat the disc just off the surface along the normal (not world +Y) so
        // the offset is perpendicular to a sloped face too.
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

// Advance one pool slot's lifecycle by a frame. `roaming` off freezes the hold
// phase so the field, once surfaced, stays put.
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
            p->timer = RandRange(PUDDLE_HOLD_MIN, PUDDLE_HOLD_MAX);
        }
        else
        {
            p->alpha = 1.0f - (float)p->timer / (float)PUDDLE_FADE_IN;
        }
        break;

    case PUD_HELD:
        p->alpha = 1.0f;
        // The hold timer always counts during roaming; with roaming off it is
        // simply never consumed, so the pool holds indefinitely.
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
            p->timer = RandRange(PUDDLE_GAP_MIN, PUDDLE_GAP_MAX);
        }
        else
        {
            p->alpha = (float)p->timer / (float)PUDDLE_FADE_OUT;
        }
        break;
    }
}

static int PointInPuddle(float x, float z, const Puddle *p)
{
    // Project the horizontal offset onto the disc's in-plane axes (their Y
    // components are tiny on near-flat ground, so the XZ projection is an
    // accurate footprint), then test against the ellipse.
    float ox = x - p->center.X;
    float oz = z - p->center.Z;
    float du = (ox * p->u.X + oz * p->u.Z) / p->rx;
    float dv = (ox * p->v.X + oz * p->v.Z) / p->rz;
    return (du * du + dv * dv) <= 1.0f;
}

// Arm the pool field for the round: size the active slot set from the preset
// count and the Frequency scalar, and start every slot dormant on a staggered
// random timer so they surface across the round's opening rather than all at
// once. Positions are rolled lazily per slot in StepPuddle (PickSpot), which
// waits for the stage, so this needs nothing loaded.
static void Puddle_Arm(void)
{
    int want = (int)(stc_base_count * freq_factors[freq_index] + 0.5f);
    if (want > PUDDLE_MAX)
        want = PUDDLE_MAX;
    if (want < 0)
        want = 0;
    stc_count = want;

    for (int i = 0; i < stc_count; i++)
    {
        Puddle *p = &stc_puddles[i];
        p->phase = PUD_DORMANT;
        p->timer = HSD_Randi(PUDDLE_INIT_STAGGER + 1);
        p->alpha = 0.0f;
        p->center.X = p->center.Y = p->center.Z = 0.0f;
        p->u.X = 1.0f; p->u.Y = 0.0f; p->u.Z = 0.0f;
        p->v.X = 0.0f; p->v.Y = 0.0f; p->v.Z = 1.0f;
        p->rx = p->rz = stc_base_radius;
    }

    stc_inited = 1;
    OSReport("[Puddle] Armed %d roaming pools\n", stc_count);
}

// GX callback on the world camera link. Draws each surfaced pool as a flat
// translucent triangle fan on the XLU pass (pass 1): an opaque-ish center fading
// to a soft rim, the whole disc scaled by the pool's fade opacity, depth-tested
// but not depth-writing so opaque geometry occludes pools behind/below it.
// Mirrors rain.c's GX setup.
static void Puddle_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != 1)
        return;
    if (!stc_active || !show_puddles || stc_count <= 0)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    HSD_StateInitDirect(GX_VTXFMT0, 2);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumTexGens(0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0, GX_DISABLE, Vertex, Vertex, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetChanCtrl(GX_ALPHA0, GX_DISABLE, Vertex, Vertex, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZMode(GX_ENABLE, GX_LEQUAL, GX_DISABLE);
    GXSetCullMode(GX_CULL_NONE);
    GXLoadPosMtxImm(&cam->view_mtx, GX_PNMTX0);

    for (int i = 0; i < stc_count; i++)
    {
        Puddle *p = &stc_puddles[i];
        if (p->alpha <= PUDDLE_ALPHA_EPS)
            continue;

        u8 ca = (u8)((float)stc_color.a * p->alpha);
        u8 ra = (u8)((int)ca * PUDDLE_RIM_ALPHA_NUM / PUDDLE_RIM_ALPHA_DEN);

        // Fan = center vertex + a closed rim ring (rim[0] repeated to seal it).
        // Each rim point is center + (cos*rx)*u + (sin*rz)*v, so the disc lies in
        // the surface plane (conforms to slopes) rather than the world XZ plane.
        GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, PUDDLE_SEGMENTS + 2);
        GXPosition3f32(p->center.X, p->center.Y, p->center.Z);
        GXColor4u8(stc_color.r, stc_color.g, stc_color.b, ca);
        for (int s = 0; s <= PUDDLE_SEGMENTS; s++)
        {
            float ang = (float)s * (2.0f * PUDDLE_PI / (float)PUDDLE_SEGMENTS);
            float cu = cosf(ang) * p->rx;
            float cv = sinf(ang) * p->rz;
            float vx = p->center.X + cu * p->u.X + cv * p->v.X;
            float vy = p->center.Y + cu * p->u.Y + cv * p->v.Y;
            float vz = p->center.Z + cu * p->u.Z + cv * p->v.Z;
            GXPosition3f32(vx, vy, vz);
            GXColor4u8(stc_color.r, stc_color.g, stc_color.b, ra);
        }
    }

    HSD_StateInvalidate(-1);
}

static void Puddle_Ensure(void)
{
    if (stc_puddle_gobj)
        return;
    GOBJ *g = GObj_Create(PUDDLE_GOBJ_CLASS, PUDDLE_GOBJ_PLINK, 0);
    if (!g)
        return;
    GObj_AddGXLink(g, Puddle_GX, PUDDLE_GX_LINK, PUDDLE_GX_PRI);
    stc_puddle_gobj = g;
    OSReport("[Puddle] Ground puddle layer installed\n");
}

// Latch the active preset's puddle config, resolving each 0 field to its module
// default, and arm a fresh field for the round. NULL or def->enabled == 0 turns
// puddles off.
void Puddle_SetActive(const PuddleDef *def)
{
    if (!def || !def->enabled)
    {
        stc_active = 0;
        return;
    }
    stc_active = 1;

    stc_color = GXColor_Unpack(def->color ? def->color : PUDDLE_DEF_COLOR);

    stc_base_count = def->count > 0 ? def->count : PUDDLE_DEF_COUNT;
    if (stc_base_count > PUDDLE_MAX)
        stc_base_count = PUDDLE_MAX;

    stc_base_radius = def->radius > 0.0f ? def->radius : PUDDLE_DEF_RADIUS;
    stc_base_factor = (def->slow_factor > 0.0f && def->slow_factor < 1.0f)
                          ? def->slow_factor
                          : PUDDLE_DEF_FACTOR;

    // Re-arm the field for the new round on the next active frame.
    stc_inited = 0;
    stc_count = 0;
}

void Puddle_Tick(void)
{
    if (!stc_active)
        return;

    if (!stc_inited)
        Puddle_Arm();
    Puddle_Ensure();

    int roaming = puddle_roaming;
    for (int i = 0; i < stc_count; i++)
        StepPuddle(&stc_puddles[i], roaming);

    // Base per-frame drag amount before per-pool opacity; Off short-circuits the
    // slowdown but the lifecycle/render above still run (discs keep cycling).
    float base_amt = (1.0f - stc_base_factor) * slow_strength_factors[slow_strength_index];
    if (base_amt <= 0.0f)
        return;

    for (int ply = 0; ply < PUDDLE_PLAYER_SLOTS; ply++)
    {
        GOBJ *mg = Ply_GetMachineGObj(ply);
        if (mg == NULL)
            continue;
        MachineData *md = (MachineData *)mg->userdata;
        if (md == NULL)
            continue;
        if (md->action_state_class != 0) // grounded states only
            continue;
        if (Machine_IsDead(md))
            continue;

        float x = md->pos.X;
        float z = md->pos.Z;
        for (int i = 0; i < stc_count; i++)
        {
            Puddle *p = &stc_puddles[i];
            if (p->alpha <= PUDDLE_ALPHA_EPS)
                continue;
            if (!PointInPuddle(x, z, p))
                continue;

            // Drag scales with how filled the pool is, so a forming/drying pool
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
    // The engine frees every world GObj on scene teardown; drop our cached handle
    // so the next active frame recreates it, and clear the round's field.
    stc_puddle_gobj = NULL;
    stc_inited = 0;
    stc_count = 0;
    stc_active = 0;
}

MenuDesc puddle_menu = {
    .option_num = 5,
    .options = {
        &(OptionDesc){
            .name = "Slowdown",
            .description = "How hard puddles drag a machine driving through them (Off = discs only, no slow)",
            .kind = OPTKIND_VALUE,
            .val = &slow_strength_index,
            .value_num = PUDDLE_SLOW_NUM,
            .value_names = slow_strength_names,
        },
        &(OptionDesc){
            .name = "Frequency",
            .description = "How many puddles the field carries (applies next round)",
            .kind = OPTKIND_VALUE,
            .val = &freq_index,
            .value_num = PUDDLE_FREQ_NUM,
            .value_names = freq_names,
        },
        &(OptionDesc){
            .name = "Size",
            .description = "How large the puddles are (applies to puddles that form after the change)",
            .kind = OPTKIND_VALUE,
            .val = &size_index,
            .value_num = PUDDLE_SIZE_NUM,
            .value_names = size_names,
        },
        &(OptionDesc){
            .name = "Roaming",
            .description = "Puddles fade in and out at new spots over time (Off = a fixed field)",
            .kind = OPTKIND_VALUE,
            .val = &puddle_roaming,
            .value_num = 2,
            .value_names = puddle_toggle_names,
        },
        &(OptionDesc){
            .name = "Show Puddles",
            .description = "Draw the puddle discs (the slowdown still applies when off)",
            .kind = OPTKIND_VALUE,
            .val = &show_puddles,
            .value_num = 2,
            .value_names = puddle_toggle_names,
        },
    },
};
