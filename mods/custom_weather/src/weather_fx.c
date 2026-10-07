#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "gx.h"
#include "game.h"
#include "stage.h"
#include "collision.h"
#include "inline.h"

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
    return lerp(lo, hi, HSD_Randf());
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
    for (int i = 0; i < PLY_NUM; i++)
    {
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (Ply_GetPKind(i) == PKIND_NONE || !rg || !rg->userdata)
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

void Weather_SetAllEnabled(int *mask, int n, int val)
{
    for (int i = 0; i < n; i++)
        mask[i] = val;
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
