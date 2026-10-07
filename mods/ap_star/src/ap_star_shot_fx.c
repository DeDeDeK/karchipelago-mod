#include <string.h>

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "gx.h"
#include "game.h"
#include "weapon.h"
#include "inline.h"

#include "ap_star.h"
#include "ap_star_shot_fx.h"

#define FX_MAX 24 // shots and draining tails drawn at once

#define TRAIL_POINTS 12   // one sample a frame, so also how many frames a tail lasts
#define TRAIL_WIDTH  0.7f // half-width at the head, as a fraction of the shot's radius
#define TRAIL_TAPER  0.6f // the share of that width the oldest sample loses
#define TRAIL_ALPHA  190

#define HALO_SCALE      1.9f  // outer radius, as a multiple of the shot's
#define HALO_CORE       0.45f // the inner ring's radius, as a fraction of the outer
#define HALO_CORE_ALPHA 0.45f // and its alpha, as a fraction of the center's
#define HALO_ALPHA      120
#define HALO_SEGS       16

#define POP_FRAMES    9
#define POP_GROWTH    1.2f // extra halo radius the flare reaches
#define POP_BOOST     1.6f // flare alpha at its start, over the halo's
#define POP_MIN_SCALE 0.5f // a shot that had already shrunk out ends without one

// p_link 1 freezes with the pause and the hitstop, as the projectiles do.
#define FX_PLINK   GAMEPLINK_1
#define FX_GX_LINK 0 // one the world camera draws

typedef struct ShotFx
{
    WeaponData *proj;      // NULL once the shot is gone
    Vec3 pt[TRAIL_POINTS];     // ring of past positions, newest at `head`
    float radius;              // the shot's at scale 1
    float scale;               // WeaponData.scale, held from the frame the shot went
    u8 used;
    u8 head;
    u8 count;
    u8 stale;                  // frames since the shot went
    u8 pop;                    // flare frames left
    GXColor color;
} ShotFx;

static ShotFx stc_fx[FX_MAX];
static int stc_fx_num;
static GOBJ *stc_fx_gobj;
static float stc_circle[HALO_SEGS + 1][2];

static void PushPoint(ShotFx *fx, const Vec3 *p)
{
    fx->head = (u8)((fx->head + 1) % TRAIL_POINTS);
    fx->pt[fx->head] = *p;
    if (fx->count < TRAIL_POINTS)
        fx->count++;
}

// Samples the live shots and ages the tails whose shots have gone.
static void FxThink(GOBJ *g)
{
    (void)g;
    for (int i = 0; i < FX_MAX; i++)
    {
        ShotFx *fx = &stc_fx[i];
        if (!fx->used)
            continue;

        if (fx->proj != NULL)
        {
            PushPoint(fx, &fx->proj->pos);
            continue;
        }

        if (fx->pop != 0)
            fx->pop--;
        if (fx->stale < TRAIL_POINTS)
            fx->stale++;
        if (fx->stale >= TRAIL_POINTS && fx->pop == 0)
        {
            fx->used = 0;
            stc_fx_num--;
        }
    }
}

static void Vert(const Vec3 *p, float ox, float oy, float oz, GXColor rgb, u8 a)
{
    GXPosition3f32(p->X + ox, p->Y + oy, p->Z + oz);
    GXColor4u8(rgb.r, rgb.g, rgb.b, a);
}

// A camera-facing ribbon, three vertices across so the edges can fade to nothing
// without a texture. Width and alpha fall off with each sample's age.
static void DrawTrail(const ShotFx *fx, const Vec3 *eye)
{
    Vec3 p[TRAIL_POINTS + 1];
    int age[TRAIL_POINTS + 1];
    int n = 0;
    int base;
    float scale;

    if (fx->proj != NULL)
    {
        p[n] = fx->proj->pos;
        age[n++] = 0;
        base = 1;
        scale = fx->proj->scale;
    }
    else
    {
        base = fx->stale;
        scale = fx->scale;
    }

    for (int k = 0; k < fx->count && base + k < TRAIL_POINTS; k++)
    {
        p[n] = fx->pt[(fx->head + TRAIL_POINTS - k) % TRAIL_POINTS];
        age[n++] = base + k;
    }
    if (n < 2)
        return;

    Vec3 side[TRAIL_POINTS + 1];
    u8 alpha[TRAIL_POINTS + 1];
    for (int i = 0; i < n; i++)
    {
        const Vec3 *ahead = &p[i > 0 ? i - 1 : i];
        const Vec3 *behind = &p[i < n - 1 ? i + 1 : i];
        Vec3 t = { ahead->X - behind->X, ahead->Y - behind->Y, ahead->Z - behind->Z };
        Vec3 v = { eye->X - p[i].X, eye->Y - p[i].Y, eye->Z - p[i].Z };
        Vec3 s;
        VECCrossProduct(&t, &v, &s);

        float f = 1.0f - (float)age[i] / (float)TRAIL_POINTS;
        float w = fx->radius * scale * TRAIL_WIDTH * (1.0f - TRAIL_TAPER * (1.0f - f));
        if (VECSquareMag(&s) > 0.000001f)
        {
            VECNormalize(&s, &s);
            VECScale(&s, &s, w);
        }
        else
        {
            // Travel straight at the eye has no side to it; borrow the neighbour's.
            s = i > 0 ? side[i - 1] : (Vec3){ 0.0f, 0.0f, 0.0f };
        }
        side[i] = s;
        alpha[i] = (u8)((float)TRAIL_ALPHA * f * f);
    }

    GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, (u16)(n * 2));
    for (int i = 0; i < n; i++)
    {
        Vert(&p[i], -side[i].X, -side[i].Y, -side[i].Z, fx->color, 0);
        Vert(&p[i], 0.0f, 0.0f, 0.0f, fx->color, alpha[i]);
    }
    GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, (u16)(n * 2));
    for (int i = 0; i < n; i++)
    {
        Vert(&p[i], 0.0f, 0.0f, 0.0f, fx->color, alpha[i]);
        Vert(&p[i], side[i].X, side[i].Y, side[i].Z, fx->color, 0);
    }
}

static void RingVert(const Vec3 *c, const Vec3 *right, const Vec3 *up, int k, float r,
                     GXColor rgb, u8 a)
{
    GX_BillboardVert(c, right, up, stc_circle[k][0] * r, stc_circle[k][1] * r, rgb, a);
}

// A camera-facing disc through the shot's center, bright in the middle and gone at
// the rim. The sphere hides its middle, so what shows is a glow around the edge.
static void DrawHalo(const ShotFx *fx, const Vec3 *right, const Vec3 *up)
{
    Vec3 c;
    float r;
    float a;

    if (fx->proj != NULL)
    {
        c = fx->proj->pos;
        r = fx->radius * fx->proj->scale * HALO_SCALE;
        a = (float)HALO_ALPHA;
    }
    else if (fx->pop != 0)
    {
        float t = 1.0f - (float)fx->pop / (float)POP_FRAMES;
        c = fx->pt[fx->head];
        r = fx->radius * fx->scale * HALO_SCALE * (1.0f + POP_GROWTH * t);
        a = (float)HALO_ALPHA * POP_BOOST * (1.0f - t);
    }
    else
    {
        return;
    }

    u8 a0 = (u8)a;
    u8 a1 = (u8)(a * HALO_CORE_ALPHA);
    float rc = r * HALO_CORE;

    GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, HALO_SEGS + 2);
    Vert(&c, 0.0f, 0.0f, 0.0f, fx->color, a0);
    for (int k = 0; k <= HALO_SEGS; k++)
        RingVert(&c, right, up, k, rc, fx->color, a1);

    GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, (HALO_SEGS + 1) * 2);
    for (int k = 0; k <= HALO_SEGS; k++)
    {
        RingVert(&c, right, up, k, rc, fx->color, a1);
        RingVert(&c, right, up, k, r, fx->color, 0);
    }
}

// Runs once per camera, so every split-screen viewport faces its own ribbons and halos.
// Ribbons blend normally to keep the sphere's hue; halos add, to glow.
static void FxGX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != 1 || stc_fx_num == 0)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (cam == NULL)
        return;

    Vec3 right, up, eye;
    COBJ_GetViewAxes(cam, &right, &up);
    COBJ_GetViewEye(cam, &eye);

    GX_BeginXlu(cam, 2, GX_BL_INVSRCALPHA);
    for (int i = 0; i < FX_MAX; i++)
    {
        if (stc_fx[i].used)
            DrawTrail(&stc_fx[i], &eye);
    }

    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
    for (int i = 0; i < FX_MAX; i++)
    {
        if (stc_fx[i].used)
            DrawHalo(&stc_fx[i], &right, &up);
    }

    HSD_StateInvalidate(-1);
}

// Made on the first shot of a scene. The scene's teardown frees it along with the
// rest of the world, so the handle is only ever forgotten, never destroyed.
static int EnsureGObj(void)
{
    if (stc_fx_gobj != NULL)
        return 1;

    static int failure_reported;
    stc_fx_gobj = GObj_Create(0, FX_PLINK, 0);
    if (stc_fx_gobj == NULL)
    {
        if (!failure_reported)
        {
            failure_reported = 1;
            OSReport("[ApStarShotFx] GObj pool exhausted, shots draw without glow or trail\n");
        }
        return 0;
    }
    GObj_AddProc(stc_fx_gobj, FxThink, 0);
    GObj_AddGXLink(stc_fx_gobj, FxGX, FX_GX_LINK, 0);
    return 1;
}

int ApStarShotFx_Attach(WeaponData *proj, GXColor color, float radius)
{
    if (!EnsureGObj())
        return 0;

    for (int i = 0; i < FX_MAX; i++)
    {
        ShotFx *fx = &stc_fx[i];
        if (fx->used)
            continue;

        memset(fx, 0, sizeof(*fx));
        fx->used = 1;
        fx->proj = proj;
        fx->radius = radius;
        fx->scale = proj->scale;
        fx->color = color;
        PushPoint(fx, &proj->pos);
        stc_fx_num++;
        return i + 1;
    }
    return 0;
}

void ApStarShotFx_Detach(int handle)
{
    if (handle == 0)
        return;

    ShotFx *fx = &stc_fx[handle - 1];
    fx->scale = fx->proj->scale;
    PushPoint(fx, &fx->proj->pos);
    fx->proj = NULL;
    fx->stale = 0;
    fx->pop = fx->scale >= POP_MIN_SCALE ? POP_FRAMES : 0;
}

void ApStarShotFx_OnBoot(void)
{
    for (int k = 0; k <= HALO_SEGS; k++)
    {
        float t = 2.0f * M_PI * (float)k / (float)HALO_SEGS;
        stc_circle[k][0] = cosf(t);
        stc_circle[k][1] = sinf(t);
    }
}

void ApStarShotFx_OnSceneChange(void)
{
    memset(stc_fx, 0, sizeof(stc_fx));
    stc_fx_num = 0;
    stc_fx_gobj = NULL;
}
