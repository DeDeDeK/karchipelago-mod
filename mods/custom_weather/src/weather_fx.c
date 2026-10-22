#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "gx.h"
#include "game.h"
#include "stage.h"
#include "collision.h"
#include "audio.h"
#include "effect.h"
#include "inline.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

char *weather_toggle_names[] = {"Preset", "Off", "On"};
char *weather_onoff_names[] = {"Off", "On"};
char *weather_enable_names[] = {"Disabled", "Enabled"};

_Static_assert(GetElementsIn(weather_toggle_names) == WEATHER_TOGGLE_NUM,
               "weather_toggle_names must match WEATHER_TOGGLE_NUM");
_Static_assert(GetElementsIn(weather_onoff_names) == WEATHER_ONOFF_NUM,
               "weather_onoff_names must match WEATHER_ONOFF_NUM");
_Static_assert(GetElementsIn(weather_enable_names) == WEATHER_ONOFF_NUM,
               "weather_enable_names must match WEATHER_ONOFF_NUM");

float Weather_Randf2(void)
{
    return RandomInRangef(-1.0f, 1.0f);
}

float Weather_RandScale(float var, float min)
{
    float s = 1.0f + Weather_Randf2() * var;
    return (s < min) ? min : s;
}

int Weather_ScaleCount(int base, float factor, int max)
{
    int n = (int)(base * factor + 0.5f);
    return (n > max) ? max : n;
}

static u8 ScaleByte(u8 v, float s)
{
    float f = v * s;
    return (f > 255.0f) ? 255 : (u8)f;
}

GXColor Weather_ScaleRGB(GXColor c, float s)
{
    return (GXColor){ScaleByte(c.r, s), ScaleByte(c.g, s), ScaleByte(c.b, s), c.a};
}

void Weather_OverrideRGB(GXColor *c, u32 rgb)
{
    if (!rgb)
        return;
    GXColor ov = GXColor_Unpack(rgb);
    *c = (GXColor){ov.r, ov.g, ov.b, c->a};
}

float Weather_RoundProgress(void)
{
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (!info)
        return -1.0f;
    if (info->flags_x2a8 & GRBOX_FLAG_MATCH_INTRO)
        return -1.0f;
    float p = info->match_progress;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}

StageNode *Weather_StageNode(void)
{
    GrObj *gr = *stc_grobj;
    if (!gr || !gr->gr_data)
        return NULL;
    return gr->gr_data->stage_node;
}

int Weather_CastDown(StageNode *sn, float x, float z, float floor_y, Vec3 *hit)
{
    Vec3 start = {x, sn->oob_max.Y + WEATHER_CAST_MARGIN, z};
    Vec3 end = {x, floor_y, z};
    return Raycast_Ground(&start, &end, hit);
}

void Weather_PlaySoundAt(int fgm_id, const Vec3 *pos)
{
    AudioEmitter em = Map_AllocAudioEmitter(0);
    if (em == -1)
        return;
    Vec3 p = *pos;
    AudioEmitter_SetPosition(em, &p, 0.0f);
    AudioEmitter_Init(em);
    AudioEmitter_Play(fgm_id, 0, em);
    // A freed emitter is held until its voice ends, so the sound plays out.
    AudioEmitter_Free(em);
}

void Weather_SpawnParticle(int effect_id, const Vec3 *pos, float scale)
{
    Vec3 p = *pos;
    Effect_SpawnSync(NULL, effect_id, 0, EFFECT_ANCHOR_WORLDPOS_SCALED, &p, &scale);
}

void Weather_ShakeSet(u32 *src, CamShakeRec *view, int priority, float right, float up)
{
    if (*src && Quake_SetConstantOffset(*src, right, up))
        return;
    *src = Quake_AddConstantSource(view, priority, right, up);
}

// Removing an id the pool no longer holds frees NULL, and Quake_SetConstantOffset answers
// 0 for one, so only a live source is removed.
void Weather_ShakeStop(u32 *src)
{
    if (*src && Quake_SetConstantOffset(*src, 0.0f, 0.0f))
        Quake_RemoveSource(*src);
    *src = 0;
}

void Weather_PlayBox(StageNode *sn, float *cx, float *cz, float *hx, float *hz)
{
    *cx = 0.5f * (sn->oob_min.X + sn->oob_max.X);
    *cz = 0.5f * (sn->oob_min.Z + sn->oob_max.Z);
    *hx = 0.5f * (sn->oob_max.X - sn->oob_min.X);
    *hz = 0.5f * (sn->oob_max.Z - sn->oob_min.Z);
}

void Weather_RandBoxPoint(StageNode *sn, float frac, float *x, float *z)
{
    float cx, cz, hx, hz;
    Weather_PlayBox(sn, &cx, &cz, &hx, &hz);
    *x = cx + Weather_Randf2() * hx * frac;
    *z = cz + Weather_Randf2() * hz * frac;
}

int WeatherSchedule_Due(WeatherSchedule *s, int num, float p)
{
    if (!s->planned)
    {
        s->num = (num < WEATHER_SCHEDULE_MAX) ? num : WEATHER_SCHEDULE_MAX;
        for (int i = 0; i < s->num; i++)
            s->at[i] = ((float)i + RandomInRangef(0.15f, 0.85f)) / (float)s->num;
        s->next = 0;
        while (s->next < s->num && s->at[s->next] <= p)
            s->next++;
        s->planned = 1;
    }

    if (s->next >= s->num || p < s->at[s->next])
        return 0;
    s->next++;
    return 1;
}

void WeatherBox_Seed(WeatherBox *b, int num)
{
    b->offset = HSD_MemAlloc(num * sizeof(Vec3));
    for (int i = 0; i < num; i++)
    {
        b->offset[i].X = HSD_Randf() * WEATHER_BOX;
        b->offset[i].Y = HSD_Randf() * WEATHER_BOX;
        b->offset[i].Z = HSD_Randf() * WEATHER_BOX;
    }
    b->drift = (Vec3){0.0f, 0.0f, 0.0f};
}

// One add and one wrap keep `d` in [0, box) only while |v| < box, so `v` is clamped
// to that.
static float WrapStep(float d, float v)
{
    float lim = WEATHER_BOX - 1.0f;
    if (v > lim)
        v = lim;
    else if (v < -lim)
        v = -lim;

    d += v;
    if (d >= WEATHER_BOX)
        d -= WEATHER_BOX;
    else if (d < 0.0f)
        d += WEATHER_BOX;
    return d;
}

void WeatherBox_Step(WeatherBox *b, float vx, float vy, float vz)
{
    b->drift.X = WrapStep(b->drift.X, vx);
    b->drift.Y = WrapStep(b->drift.Y, vy);
    b->drift.Z = WrapStep(b->drift.Z, vz);
}

static int PoolSetAll(OptionDesc *self, int val)
{
    WeatherPool *pool = self->user_data;
    for (int i = 0; i < pool->num; i++)
        pool->enabled[i] = val;
    if (pool->on_change)
        pool->on_change(val);
    return 1;
}

int WeatherPool_EnableAll(OptionDesc *self)
{
    return PoolSetAll(self, 1);
}

int WeatherPool_DisableAll(OptionDesc *self)
{
    return PoolSetAll(self, 0);
}

u32 WeatherPool_Mask(const WeatherPool *pool)
{
    u32 mask = 0;
    for (int i = 0; i < pool->num && i < 32; i++)
    {
        if (pool->enabled[i])
            mask |= 1u << i;
    }
    return mask;
}

// Callers retry a NULL every frame and pool exhaustion is package-wide, so the warning
// latches across layers.
static int stc_pool_warned = 0;

void WeatherGX_EnsureLayer(GOBJ **slot, void *cb, const char *tag)
{
    if (*slot)
        return;
    GOBJ *g = GObj_Create(WEATHER_GOBJ_CLASS, WEATHER_GOBJ_PLINK, 0);
    if (!g)
    {
        if (!stc_pool_warned)
        {
            OSReport("[WeatherFX] %s layer not created, GObj pool exhausted\n", tag);
            stc_pool_warned = 1;
        }
        return;
    }
    GObj_AddGXLink(g, cb, WEATHER_GX_LINK, WEATHER_GX_PRI);
    *slot = g;
}

void WeatherGX_BeginSky(COBJ *cam, GXBlendFactor dst)
{
    GX_BeginXlu(cam, RENDER_VERTEX, dst);
    HSD_FogSet(NULL);
}

void WeatherGX_EndSky(void)
{
    HSD_FogSet(HSD_FogGetCurrent());
    HSD_StateInvalidate(HSD_STATE_ALL);
}

float WeatherGX_PlaceOnDome(const Vec3 *dir, const Vec3 *eye, float e2,
                            float want, float dome_frac, float maxd, Vec3 *P)
{
    float dome_r = WEATHER_DOME_R_BASE * CustomBackdrop_GetDistance();
    float edotd = eye->X * dir->X + eye->Y * dir->Y + eye->Z * dir->Z;
    float disc = edotd * edotd + dome_r * dome_r - e2;
    float t_dome = (disc > 0.0f) ? (-edotd + sqrtf(disc)) : dome_r;

    float dist = want;
    float lim = dome_frac * t_dome;
    if (dist > lim)
        dist = lim;
    if (dist > maxd)
        dist = maxd;
    if (dist < 1.0f)
        dist = 1.0f;  // never a degenerate or mirrored billboard

    P->X = eye->X + dir->X * dist;
    P->Y = eye->Y + dir->Y * dist;
    P->Z = eye->Z + dir->Z * dist;
    return dist;
}

#define WEATHER_DOT_SEGS 6

static float stc_dot_cos[WEATHER_DOT_SEGS + 1];
static float stc_dot_sin[WEATHER_DOT_SEGS + 1];
static int   stc_dot_seeded = 0;

void WeatherGX_SoftDot(const Vec3 *P, const Vec3 *R, const Vec3 *U, float r, GXColor rgb, u8 a)
{
    if (!stc_dot_seeded)
    {
        for (int s = 0; s <= WEATHER_DOT_SEGS; s++)
        {
            float ang = 2.0f * M_PI * (float)s / (float)WEATHER_DOT_SEGS;
            stc_dot_cos[s] = cosf(ang);
            stc_dot_sin[s] = sinf(ang);
        }
        stc_dot_seeded = 1;
    }

    GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, WEATHER_DOT_SEGS + 2);
    GX_BillboardVert(P, R, U, 0.0f, 0.0f, rgb, a);
    for (int s = 0; s <= WEATHER_DOT_SEGS; s++)
        GX_BillboardVert(P, R, U, stc_dot_cos[s] * r, stc_dot_sin[s] * r, rgb, 0);
}

LOBJ *Weather_LoadLight(LObjDesc *desc)
{
    GOBJ *g = GObj_Create(WEATHER_GOBJ_CLASS, WEATHER_GOBJ_PLINK, 0);
    if (!g)
        return NULL;
    LOBJ *l = LObj_LoadDesc(desc);
    if (!l)
    {
        // Callers retry every frame, so a failed load must not orphan its carrier.
        GObj_Destroy(g);
        return NULL;
    }
    GObj_AddObject(g, HSD_OBJKIND_LOBJ, l);
    GObj_AddGXLink(g, LObj_GX, WEATHER_GX_LINK, WEATHER_GX_PRI);
    return l;
}

void Weather_SetLightColor(LOBJ *l, GXColor c)
{
    if (!l)
        return;
    l->color = (GXColor){c.r, c.g, c.b, 0xFF};
    l->hw_color = l->color;
}
