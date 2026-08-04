// Cloud deck for custom_weather: soft, fly-through clusters of translucent
// spheroids drifting over the City Trial map with the global wind.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "gx.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

#define CLOUD_PI      3.14159265358979f
#define CLOUD_DEG2RAD (CLOUD_PI / 180.0f)

#define CLOUD_MAX       30   // field capacity; resolved count clamps to this
#define CLOUD_PUFFS     5    // overlapping spheroids per cloud (fluffy silhouette)

// Each puff is a flattened UV sphere, re-emitted as triangle strips every frame.
// The tessellation stays coarse: the soft per-vertex alpha hides the low poly count.
#define CLOUD_SPHERE_RINGS    4      // latitude bands
#define CLOUD_SPHERE_SECTORS  8      // longitude divisions
#define CLOUD_SPHERE_VERTS    ((CLOUD_SPHERE_RINGS + 1) * (CLOUD_SPHERE_SECTORS + 1))
#define CLOUD_FLATTEN         0.55f  // spheroid vertical squash (ry = r * this)
#define CLOUD_RIM_MIN         0.12f  // silhouette alpha floor (0 = fully wispy edge)

// Defaults applied when a preset leaves the matching CloudDef field 0; the color
// alpha is the base per-cloud opacity before the menu scalar.
#define CLOUD_DEF_COLOR       RGBA(236, 240, 248, 205)
#define CLOUD_DEF_COUNT       12
#define CLOUD_DEF_SIZE        58.0f   // base puff radius (world units)
#define CLOUD_DEF_SIZE_VAR    0.35f   // +/- fractional per-cloud size spread
#define CLOUD_DEF_PUFF_VAR    0.6f    // per-puff size variance within a cluster (0..1)
#define CLOUD_DEF_HEIGHT_VAR  90.0f   // +/- world units of height spread about the deck

// Global multiplier over the resolved size and the menu Size scalar.
#define CLOUD_SIZE_SCALE      1.2f

// Deck height when a preset leaves CloudDef.height 0: this fraction up the CT OOB box.
#define CLOUD_DECK_FRACTION   0.35f

// Puffs scatter within these multiples of the cloud's puff radius; puff 0 anchors
// the center core.
#define CLOUD_SPREAD_H  1.6f
#define CLOUD_SPREAD_V  0.45f

// How far scattered puffs shrink below the core at variance 1, and the floor on
// that shrink.
#define CLOUD_PUFF_SPREAD  0.85f
#define CLOUD_PUFF_FLOOR   0.15f

// Clouds follow the wind direction at a damped speed, floored so a calm preset
// still drifts them and capped so gusts don't rip the deck across.
#define CLOUD_WIND_FACTOR   0.30f
#define CLOUD_DRIFT_MIN     0.45f   // world units/frame floor (the calm drift)
#define CLOUD_DRIFT_MAX     3.5f
#define CLOUD_CALM_HEADING  40.0f   // drift heading (deg) when the wind is calm

// Horizontal OOB clearance (world units) over which a cloud fades to full, so it
// ghosts in/out at the walls rather than popping. Y clearance is ignored.
#define CLOUD_EDGE_FADE  260.0f

// Render GObj on the world camera's gx_link 0, XLU sub-pass.
#define CLOUD_GOBJ_CLASS  205
#define CLOUD_GOBJ_PLINK  29
#define CLOUD_GX_LINK     0
#define CLOUD_GX_PRI      0

typedef struct CloudPuff
{
    Vec3  off;   // offset from the cloud center in world axes (clouds don't rotate)
    float r;     // this puff's radius
} CloudPuff;

typedef struct Cloud
{
    Vec3      pos;               // world center; drifts horizontally each frame
    CloudPuff puff[CLOUD_PUFFS];
    float     alpha_scale;       // subtle per-cloud opacity variety, 0..1
} Cloud;

// Cached only to avoid recreating the GObj every frame; never dereferenced.
static GOBJ *stc_cloud_gobj = NULL;

static int stc_active = 0;
static int stc_inited = 0;

static Cloud stc_clouds[CLOUD_MAX];
static int   stc_count = 0;

// Resolved per-preset config (CloudDef + defaults), before the menu scalars.
static GXColor stc_color = {236, 240, 248, 205};
static int     stc_base_count = CLOUD_DEF_COUNT;
static float   stc_base_size = CLOUD_DEF_SIZE;
static float   stc_size_var = CLOUD_DEF_SIZE_VAR;
static float   stc_puff_var = CLOUD_DEF_PUFF_VAR;
static float   stc_pre_height = 0.0f;   // absolute preset height, or 0 = derive from OOB box
static float   stc_height_var = CLOUD_DEF_HEIGHT_VAR;

// Menu knobs layered over the active preset's CloudDef: master coverage/opacity/
// size scalars, a height offset, and an optional tint override.
static const float cover_factors[] = {1.0f, 0.0f, 0.55f, 1.0f, 1.6f};
static char *cover_names[] = {"Preset", "Off", "Sparse", "Normal", "Dense"};
#define CLOUD_COVER_NUM ((int)(sizeof(cover_factors) / sizeof(cover_factors[0])))
static int cover_index = 0;

static const float opacity_factors[] = {1.0f, 0.6f, 1.0f, 1.35f};
static char *opacity_names[] = {"Preset", "Thin", "Normal", "Thick"};
#define CLOUD_OPACITY_NUM ((int)(sizeof(opacity_factors) / sizeof(opacity_factors[0])))
static int opacity_index = 0;

static const float size_factors[] = {1.0f, 0.7f, 1.0f, 1.4f};
static char *size_names[] = {"Preset", "Small", "Normal", "Large"};
#define CLOUD_SIZE_NUM ((int)(sizeof(size_factors) / sizeof(size_factors[0])))
static int size_index = 0;

// Master scalar over the preset's per-puff variance (resolved var clamped 0..1).
static const float variance_factors[] = {1.0f, 0.2f, 1.0f, 1.7f};
static char *variance_names[] = {"Preset", "Uniform", "Normal", "Varied"};
#define CLOUD_VARIANCE_NUM ((int)(sizeof(variance_factors) / sizeof(variance_factors[0])))
static int variance_index = 0;

// Additive world-unit offset applied to the resolved deck height.
static const float height_offsets[] = {0.0f, -220.0f, 0.0f, 220.0f};
static char *height_names[] = {"Preset", "Low", "Normal", "High"};
#define CLOUD_HEIGHT_NUM ((int)(sizeof(height_offsets) / sizeof(height_offsets[0])))
static int height_index = 0;

// Index 0 keeps the per-preset RGB; the rest force an RGB, leaving the alpha from
// the preset opacity * the Opacity scalar.
static const u32 color_overrides[] = {0, RGBA(246, 249, 255, 255), RGBA(150, 160, 175, 255), RGBA(66, 72, 86, 255)};
static char *color_names[] = {"Preset", "White", "Gray", "Storm"};
#define CLOUD_COLOR_NUM ((int)(sizeof(color_overrides) / sizeof(color_overrides[0])))
static int color_index = 0;

static void Cloud_GX(GOBJ *g, int pass);

static StageNode *CloudStageNode(void)
{
    GrObj *gr = *stc_grobj;
    if (!gr || !gr->gr_data || !gr->gr_data->stage_node)
        return NULL;
    return gr->gr_data->stage_node;
}

// Unit-sphere vertex directions (latitude rows x longitude), doubling as vertex
// normals; each puff scales/translates these into world space.
static Vec3 stc_sphere[CLOUD_SPHERE_VERTS];
static int  stc_sphere_seeded = 0;

static void SeedSphere(void)
{
    if (stc_sphere_seeded)
        return;
    int k = 0;
    for (int i = 0; i <= CLOUD_SPHERE_RINGS; i++)
    {
        float theta = CLOUD_PI * (float)i / (float)CLOUD_SPHERE_RINGS; // 0..PI, top->bottom
        float st = sinf(theta), ct = cosf(theta);
        for (int j = 0; j <= CLOUD_SPHERE_SECTORS; j++)
        {
            float phi = 2.0f * CLOUD_PI * (float)j / (float)CLOUD_SPHERE_SECTORS;
            stc_sphere[k].X = st * cosf(phi);
            stc_sphere[k].Y = ct;
            stc_sphere[k].Z = st * sinf(phi);
            k++;
        }
    }
    stc_sphere_seeded = 1;
}

// Resolved deck world Y: the preset's absolute height, or a fraction up the OOB
// box when 0, plus the menu height offset.
static float DeckBaseY(StageNode *sn)
{
    float base = (stc_pre_height != 0.0f)
                     ? stc_pre_height
                     : sn->oob_min.Y + CLOUD_DECK_FRACTION * (sn->oob_max.Y - sn->oob_min.Y);
    return base + height_offsets[height_index];
}

// Roll a fresh cluster shape (puff offsets + radii) and per-cloud opacity variety.
static void SeedShape(Cloud *c)
{
    float scale = 1.0f + Weather_Randf2() * stc_size_var;
    if (scale < 0.4f)
        scale = 0.4f;
    float r = stc_base_size * size_factors[size_index] * scale * CLOUD_SIZE_SCALE;

    // Resolved variance -> smallest scattered puff as a fraction of r.
    float var = stc_puff_var * variance_factors[variance_index];
    if (var < 0.0f) var = 0.0f;
    if (var > 1.0f) var = 1.0f;
    float puff_min = 1.0f - var * CLOUD_PUFF_SPREAD;
    if (puff_min < CLOUD_PUFF_FLOOR)
        puff_min = CLOUD_PUFF_FLOOR;

    c->puff[0].off.X = c->puff[0].off.Y = c->puff[0].off.Z = 0.0f;
    c->puff[0].r = r;
    for (int p = 1; p < CLOUD_PUFFS; p++)
    {
        c->puff[p].off.X = Weather_Randf2() * r * CLOUD_SPREAD_H;
        c->puff[p].off.Z = Weather_Randf2() * r * CLOUD_SPREAD_H;
        c->puff[p].off.Y = Weather_Randf2() * r * CLOUD_SPREAD_V;
        c->puff[p].r = r * (puff_min + HSD_Randf() * (1.0f - puff_min));
    }

    c->alpha_scale = 0.82f + HSD_Randf() * 0.18f;
}

// Scatter the field across the OOB box at the deck height. Needs the stage loaded;
// if not ready it leaves stc_inited 0 to retry next frame.
static void Cloud_Arm(void)
{
    StageNode *sn = CloudStageNode();
    if (!sn)
        return;

    int want = (int)(stc_base_count * cover_factors[cover_index] + 0.5f);
    if (want > CLOUD_MAX)
        want = CLOUD_MAX;
    if (want < 0)
        want = 0;
    stc_count = want;

    float cx = 0.5f * (sn->oob_min.X + sn->oob_max.X);
    float cz = 0.5f * (sn->oob_min.Z + sn->oob_max.Z);
    float hx = 0.5f * (sn->oob_max.X - sn->oob_min.X);
    float hz = 0.5f * (sn->oob_max.Z - sn->oob_min.Z);

    for (int i = 0; i < stc_count; i++)
    {
        Cloud *c = &stc_clouds[i];
        c->pos.X = cx + Weather_Randf2() * hx;
        c->pos.Z = cz + Weather_Randf2() * hz;
        c->pos.Y = DeckBaseY(sn) + Weather_Randf2() * stc_height_var;
        SeedShape(c);
    }

    stc_inited = 1;
    OSReport("[Clouds] Armed %d clouds\n", stc_count);
}

static void Cloud_Ensure(void)
{
    if (stc_cloud_gobj)
        return;
    stc_cloud_gobj = WeatherGX_EnsureLayer(CLOUD_GOBJ_CLASS, CLOUD_GOBJ_PLINK, Cloud_GX,
                                           CLOUD_GX_LINK, CLOUD_GX_PRI,
                                           "[Clouds] Cloud deck layer installed");
}

// GX callback on the world camera link. Draws each cloud as a cluster of translucent
// spheroids on the XLU pass (pass 1): flat per-vertex color, alpha blend,
// depth-tested but not depth-writing so stage geometry occludes clouds behind it.
static void Cloud_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != 1)
        return;
    if (!stc_active || stc_count <= 0)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;
    StageNode *sn = CloudStageNode();
    if (!sn)
        return;

    SeedSphere();

    // Camera forward axis in world space (row 2 of the world->view rotation). Only
    // |n . fwd| is used, so a spheroid's front and back both stay opaque while its
    // silhouette (normals grazing the view) fades.
    float (*m)[4] = cam->view_mtx;
    Vec3 fwd = {m[2][0], m[2][1], m[2][2]};

    float minx = sn->oob_min.X, maxx = sn->oob_max.X;
    float minz = sn->oob_min.Z, maxz = sn->oob_max.Z;
    float opacity = opacity_factors[opacity_index];

    WeatherGX_BeginXlu(cam, 0, 0);

    for (int i = 0; i < stc_count; i++)
    {
        Cloud *c = &stc_clouds[i];

        // Horizontal OOB clearance -> edge fade; a fully faded cloud is skipped.
        float cl = c->pos.X - minx;
        float t = maxx - c->pos.X;
        if (t < cl) cl = t;
        t = c->pos.Z - minz;
        if (t < cl) cl = t;
        t = maxz - c->pos.Z;
        if (t < cl) cl = t;
        float ef = cl / CLOUD_EDGE_FADE;
        if (ef <= 0.0f)
            continue;
        if (ef > 1.0f)
            ef = 1.0f;

        float baseA = (float)stc_color.a * ef * c->alpha_scale * opacity;
        if (baseA <= 1.0f)
            continue;
        if (baseA > 255.0f)
            baseA = 255.0f;

        for (int p = 0; p < CLOUD_PUFFS; p++)
        {
            CloudPuff *pf = &c->puff[p];
            float ox = c->pos.X + pf->off.X;
            float oy = c->pos.Y + pf->off.Y;
            float oz = c->pos.Z + pf->off.Z;
            float rx = pf->r, ry = pf->r * CLOUD_FLATTEN, rz = pf->r;

            // One triangle strip per latitude band. Both faces draw (no cull), so
            // the spheroid still fills the view when the camera is inside it.
            for (int b = 0; b < CLOUD_SPHERE_RINGS; b++)
            {
                const Vec3 *row0 = &stc_sphere[b * (CLOUD_SPHERE_SECTORS + 1)];
                const Vec3 *row1 = &stc_sphere[(b + 1) * (CLOUD_SPHERE_SECTORS + 1)];

                GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, 2 * (CLOUD_SPHERE_SECTORS + 1));
                for (int j = 0; j <= CLOUD_SPHERE_SECTORS; j++)
                {
                    const Vec3 *n0 = &row0[j];
                    const Vec3 *n1 = &row1[j];

                    float d0 = n0->X * fwd.X + n0->Y * fwd.Y + n0->Z * fwd.Z;
                    if (d0 < 0.0f) d0 = -d0;
                    float d1 = n1->X * fwd.X + n1->Y * fwd.Y + n1->Z * fwd.Z;
                    if (d1 < 0.0f) d1 = -d1;
                    u8 a0 = (u8)(baseA * (CLOUD_RIM_MIN + (1.0f - CLOUD_RIM_MIN) * d0));
                    u8 a1 = (u8)(baseA * (CLOUD_RIM_MIN + (1.0f - CLOUD_RIM_MIN) * d1));

                    GXPosition3f32(ox + n0->X * rx, oy + n0->Y * ry, oz + n0->Z * rz);
                    GXColor4u8(stc_color.r, stc_color.g, stc_color.b, a0);
                    GXPosition3f32(ox + n1->X * rx, oy + n1->Y * ry, oz + n1->Z * rz);
                    GXColor4u8(stc_color.r, stc_color.g, stc_color.b, a1);
                }
            }
        }
    }

    HSD_StateInvalidate(-1);
}

// Latch the active preset's cloud config, resolving each 0 field to its module
// default and applying the menu Color override.
void Cloud_SetActive(const CloudDef *def)
{
    if (!def || !def->enabled || cover_factors[cover_index] <= 0.0f)
    {
        stc_active = 0;
        return;
    }
    stc_active = 1;

    stc_color = GXColor_Unpack(def->color ? def->color : CLOUD_DEF_COLOR);
    if (color_index > 0)
    {
        GXColor ov = GXColor_Unpack(color_overrides[color_index]);
        stc_color.r = ov.r;
        stc_color.g = ov.g;
        stc_color.b = ov.b;
    }

    stc_base_count = def->count > 0 ? def->count : CLOUD_DEF_COUNT;
    if (stc_base_count > CLOUD_MAX)
        stc_base_count = CLOUD_MAX;

    stc_base_size = def->size > 0.0f ? def->size : CLOUD_DEF_SIZE;
    stc_size_var = def->size_var > 0.0f ? def->size_var : CLOUD_DEF_SIZE_VAR;
    stc_puff_var = def->puff_var > 0.0f ? def->puff_var : CLOUD_DEF_PUFF_VAR;
    stc_pre_height = def->height; // 0 => derive from the OOB box at arm
    stc_height_var = def->height_var > 0.0f ? def->height_var : CLOUD_DEF_HEIGHT_VAR;

    stc_inited = 0;
    stc_count = 0;
}

void Cloud_Tick(void)
{
    if (!stc_active)
        return;

    if (!stc_inited)
        Cloud_Arm();
    Cloud_Ensure();
    if (stc_count <= 0)
        return;

    StageNode *sn = CloudStageNode();
    if (!sn)
        return;

    // Drift direction from the wind, speed damped/floored/capped. Calm wind gives
    // no direction, so fall back to a fixed heading at the floor speed.
    Vec3 w;
    Wind_GetVector(&w);
    float mag = sqrtf(w.X * w.X + w.Z * w.Z);
    float dirx, dirz, speed;
    if (mag > 1e-4f)
    {
        dirx = w.X / mag;
        dirz = w.Z / mag;
        speed = mag * CLOUD_WIND_FACTOR;
    }
    else
    {
        float rad = CLOUD_CALM_HEADING * CLOUD_DEG2RAD;
        dirx = sinf(rad);
        dirz = cosf(rad);
        speed = 0.0f;
    }
    if (speed < CLOUD_DRIFT_MIN)
        speed = CLOUD_DRIFT_MIN;
    if (speed > CLOUD_DRIFT_MAX)
        speed = CLOUD_DRIFT_MAX;
    float dx = dirx * speed;
    float dz = dirz * speed;

    float minx = sn->oob_min.X, maxx = sn->oob_max.X;
    float minz = sn->oob_min.Z, maxz = sn->oob_max.Z;

    for (int i = 0; i < stc_count; i++)
    {
        Cloud *c = &stc_clouds[i];
        c->pos.X += dx;
        c->pos.Z += dz;

        // Wrap to the upwind wall on exit, where the edge fade holds the cloud
        // invisible so re-rolling its shape/height there is hidden.
        int wrapped = 0;
        if (c->pos.X > maxx)
        {
            c->pos.X = minx;
            wrapped = 1;
        }
        else if (c->pos.X < minx)
        {
            c->pos.X = maxx;
            wrapped = 1;
        }
        if (c->pos.Z > maxz)
        {
            c->pos.Z = minz;
            wrapped = 1;
        }
        else if (c->pos.Z < minz)
        {
            c->pos.Z = maxz;
            wrapped = 1;
        }
        if (wrapped)
        {
            c->pos.Y = DeckBaseY(sn) + Weather_Randf2() * stc_height_var;
            SeedShape(c);
        }
    }
}

void Cloud_Reset(void)
{
    // The engine frees every world GObj on scene teardown; drop the cached handle
    // so the next active frame recreates it.
    stc_cloud_gobj = NULL;
    stc_inited = 0;
    stc_count = 0;
    stc_active = 0;
}

MenuDesc clouds_menu = {
    .option_num = 6,
    .options = {
        &(OptionDesc){
            .name = "Coverage",
            .description = "How many clouds fill the sky over every CT preset (Off disables clouds entirely)",
            .kind = OPTKIND_VALUE,
            .val = &cover_index,
            .value_num = CLOUD_COVER_NUM,
            .value_names = cover_names,
        },
        &(OptionDesc){
            .name = "Opacity",
            .description = "How thick the clouds are - how much they obscure vision when you fly through",
            .kind = OPTKIND_VALUE,
            .val = &opacity_index,
            .value_num = CLOUD_OPACITY_NUM,
            .value_names = opacity_names,
        },
        &(OptionDesc){
            .name = "Size",
            .description = "How large each cloud is (applies to clouds that form after the change)",
            .kind = OPTKIND_VALUE,
            .val = &size_index,
            .value_num = CLOUD_SIZE_NUM,
            .value_names = size_names,
        },
        &(OptionDesc){
            .name = "Variance",
            .description = "How much the puffs within each cloud vary in size (Uniform = even, Varied = lumpy)",
            .kind = OPTKIND_VALUE,
            .val = &variance_index,
            .value_num = CLOUD_VARIANCE_NUM,
            .value_names = variance_names,
        },
        &(OptionDesc){
            .name = "Height",
            .description = "Raise or lower the cloud deck from its default (about mid-height)",
            .kind = OPTKIND_VALUE,
            .val = &height_index,
            .value_num = CLOUD_HEIGHT_NUM,
            .value_names = height_names,
        },
        &(OptionDesc){
            .name = "Color",
            .description = "Override the cloud tint across every preset (Preset = each preset's own color)",
            .kind = OPTKIND_VALUE,
            .val = &color_index,
            .value_num = CLOUD_COLOR_NUM,
            .value_names = color_names,
        },
    },
};
