#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "gx.h"
#include "game.h"
#include "stage.h"
#include "collision.h"

#include "weather_fx.h"

char *weather_toggle_names[] = {"Preset", "Off", "On"};
char *weather_onoff_names[] = {"Off", "On"};
char *weather_enable_names[] = {"Disabled", "Enabled"};

float Weather_Randf2(void)
{
    return HSD_Randf() * 2.0f - 1.0f;
}

float Weather_RandRange(float lo, float hi)
{
    return lo + (hi - lo) * HSD_Randf();
}

int Weather_RandRangeI(int lo, int hi)
{
    if (hi <= lo)
        return lo;
    return lo + HSD_Randi(hi - lo + 1);
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

HSD_Fog *Weather_LiveFog(void)
{
    GrObj *gr = *stc_grobj;
    if (!gr || !gr->sky_gobj)
        return NULL;
    return (HSD_Fog *)gr->sky_gobj->hsd_object;
}

float Weather_GroundYAt(StageNode *sn, float x, float z, float fallback)
{
    Vec3 start = {x, sn->oob_max.Y + 50.0f, z};
    Vec3 end = {x, sn->oob_min.Y - 50.0f, z};
    Vec3 hit;
    if (Raycast_Ground(&start, &end, &hit) < 0)
        return fallback;
    return hit.Y;
}

void Weather_PlayBox(StageNode *sn, float *cx, float *cz, float *hx, float *hz)
{
    *cx = 0.5f * (sn->oob_min.X + sn->oob_max.X);
    *cz = 0.5f * (sn->oob_min.Z + sn->oob_max.Z);
    *hx = 0.5f * (sn->oob_max.X - sn->oob_min.X);
    *hz = 0.5f * (sn->oob_max.Z - sn->oob_min.Z);
}

GOBJ *Weather_FindDonorRider(void)
{
    for (int i = 0; i < WEATHER_PLAYER_SLOTS; i++)
    {
        GOBJ *rg = stc_playerdata[i].rider_gobj;
        if (stc_playerdata[i].player_kind == PKIND_NONE || !rg || !rg->userdata)
            continue;
        return rg;
    }
    return NULL;
}

int Weather_SeedSchedule(float *out, int n, float p)
{
    for (int i = 0; i < n; i++)
        out[i] = ((float)i + 0.15f + 0.70f * HSD_Randf()) / (float)n;

    int next = 0;
    while (next < n && out[next] <= p)
        next++;
    return next;
}

float Weather_WrapStep(float d, float v, float box)
{
    d += v;
    if (d >= box)
        d -= box;
    else if (d < 0.0f)
        d += box;
    return d;
}

int Weather_PickEnabled(const int *mask, int n)
{
    int count = 0;
    for (int i = 0; i < n; i++)
    {
        if (mask[i])
            count++;
    }
    if (count == 0)
        return -1;

    int pick = HSD_Randi(count);
    for (int i = 0; i < n; i++)
    {
        if (!mask[i])
            continue;
        if (pick == 0)
            return i;
        pick--;
    }
    return -1;
}

void Weather_SetAllEnabled(int *mask, int n, int val)
{
    for (int i = 0; i < n; i++)
        mask[i] = val;
}

void WeatherGX_BeginXlu(COBJ *cam, int additive, int line_width)
{
    HSD_StateInitDirect(GX_VTXFMT0, 2);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumTexGens(0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0, GX_DISABLE, Vertex, Vertex, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetChanCtrl(GX_ALPHA0, GX_DISABLE, Vertex, Vertex, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA,
                   additive ? GX_BL_ONE : GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZMode(GX_ENABLE, GX_LEQUAL, GX_DISABLE);
    GXSetCullMode(GX_CULL_NONE);
    if (line_width > 0)
        GXSetLineWidth((u8)line_width, 5);
    GXLoadPosMtxImm(&cam->view_mtx, GX_PNMTX0);
}

// Callers cache the NULL and retry every frame, so the warning latches; pool
// exhaustion is a package-wide condition, not a per-layer one.
static int stc_pool_warned = 0;

GOBJ *WeatherGX_EnsureLayer(int entity_class, int p_link, void *cb, const char *tag)
{
    GOBJ *g = GObj_Create(entity_class, p_link, 0);
    if (!g)
    {
        if (!stc_pool_warned)
        {
            OSReport("[WeatherFX] %s layer not created, GObj pool exhausted\n", tag);
            stc_pool_warned = 1;
        }
        return NULL;
    }
    GObj_AddGXLink(g, cb, WEATHER_GX_LINK, WEATHER_GX_PRI);
    return g;
}

void WeatherGX_CameraEye(COBJ *cam, Vec3 *out)
{
    float (*m)[4] = cam->view_mtx;
    float tx = m[0][3], ty = m[1][3], tz = m[2][3];
    out->X = -(m[0][0] * tx + m[1][0] * ty + m[2][0] * tz);
    out->Y = -(m[0][1] * tx + m[1][1] * ty + m[2][1] * tz);
    out->Z = -(m[0][2] * tx + m[1][2] * ty + m[2][2] * tz);
}

void WeatherGX_BillboardVert(const Vec3 *P, const Vec3 *R, const Vec3 *U,
                             float u, float v, u8 cr, u8 cg, u8 cb, u8 ca)
{
    GXPosition3f32(P->X + u * R->X + v * U->X,
                   P->Y + u * R->Y + v * U->Y,
                   P->Z + u * R->Z + v * U->Z);
    GXColor4u8(cr, cg, cb, ca);
}

float WeatherGX_PlaceOnDome(const Vec3 *dir, const Vec3 *eye, float e2,
                            float want, float dome_frac, float maxd, Vec3 *P)
{
    float edotd = eye->X * dir->X + eye->Y * dir->Y + eye->Z * dir->Z;
    float disc = edotd * edotd + WEATHER_DOME_R * WEATHER_DOME_R - e2;
    float t_dome = (disc > 0.0f) ? (-edotd + sqrtf(disc)) : WEATHER_DOME_R;

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
