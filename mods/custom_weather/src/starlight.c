// Star light for custom_weather: one point light at the City Trial star pole's star,
// shadowed by the terrain. A world-space light map carries, per column, the lowest height
// the star reaches, and the map draw lights a pixel only above that height. The light
// itself is a GX hardware light on the map's added normals, or the light map's own RGB.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "gx.h"
#include "collision.h"
#include "yakumono.h"
#include "inline.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

// The light map spans the out-of-bounds box, ~20 world units a texel over City Trial's.
#define STAR_DIM      128
#define STAR_CELLS    (STAR_DIM * STAR_DIM)
#define STAR_TEX_SIZE (STAR_CELLS * 4)  // RGBA8

// I8 with texel x holding x: the map draw reads each pixel's height off it.
#define STAR_RAMP_W    256
#define STAR_RAMP_H    4
#define STAR_RAMP_SIZE (STAR_RAMP_W * STAR_RAMP_H)

// Height steps a column's lit height is lowered by, so no surface shadows itself.
#define STAR_SHADOW_BIAS 2

#define STAR_COLUMNS_PER_FRAME 512
#define STAR_ROWS_PER_FRAME    16
// Longest march, in texels, from the star to a corner of the map.
#define STAR_MAX_STEPS (STAR_DIM * 3 / 2)

#define STAR_DEF_COLOR     RGBA(255, 200, 95, 255)
#define STAR_DEF_RANGE     1800.0f
#define STAR_DEF_INTENSITY 1.0f
// Brightness a full-intensity light adds right under the star, over the ambient.
#define STAR_PEAK 1.6f
// Share of the light a surface turned away from the star still gets.
#define STAR_FACING_MIN 0.35f

// The GX light falls off as peak / (1 + (d / d0)^2), d0 the reach over STAR_HW_FALLOFF.
#define STAR_HW_LIGHT   GX_LIGHT7
#define STAR_HW_PEAK    1.0f
#define STAR_HW_FALLOFF 3.5f

// Casts pass through moving triangles (the gondolas) and the barrier ring, which cast
// no shadow.
#define STAR_MAX_CASTS      4
#define STAR_NO_GROUND      -1.0e9f

// The light fades through the TEV's fixed konst fractions, 1/8 to 8/8.
#define STAR_FADE_STEPS  8
#define STAR_FADE_FRAMES 3

typedef enum StarBake
{
    STAR_BAKE_HEIGHTS,
    STAR_BAKE_SHADOWS,
    STAR_BAKE_DONE,
} StarBake;

static int stc_active = 0;

// The live preset's StarLightDef with the defaults and menu overrides folded in.
static GXColor stc_color;
static float   stc_range;
static float   stc_intensity;
static int     stc_dirty = 0;  // the latched config differs from the composed map
static int     stc_hw = 0;      // light from the GX light, not the light map's RGB
static int     stc_shadows = 1;

static int    stc_ready = 0;
static int    stc_failed = 0;
static JOBJ  *stc_star_jobj = NULL;  // the star, hidden for good once it is busted
static Vec3   stc_star;
static int    stc_up = 0;
static int    stc_fade = 0;
static int    stc_fade_timer = 0;
static u8     stc_compose_parity = 0;  // kept across rounds
static int    stc_normals = 0;         // the map's display lists carry normals this round

static GXLightObj stc_hw_light;
// COLOR1 for the star alone: black ambient, white material, so the light's colour and
// falloff are the whole result.
static HSD_Chan stc_hw_chan = {
    .chan = GX_COLOR1,
    .amb_color = {0, 0, 0, 255},
    .mat_color = {255, 255, 255, 255},
    .enable = 1,
    .amb_src = Register,
    .mat_src = Register,
    .light_mask = STAR_HW_LIGHT,
    .diff_fn = GX_DF_CLAMP,
    .attn_fn = GX_AF_SPOT,
};

static StarBake stc_bake;
static int      stc_cursor;
static int      stc_bake_frames;
static float    stc_h_min, stc_h_max;
static float    stc_y_lo, stc_y_span;

static float *stc_height;  // top surface per column, STAR_NO_GROUND where none
static u8    *stc_facing;  // N.L share of the light at that surface, 0..255
static u8    *stc_lit_h;   // lowest height the star reaches per column, quantized

static float stc_inv_step[STAR_MAX_STEPS];

static TOBJ          *stc_light_tobj = NULL;
static TOBJ          *stc_ramp_tobj = NULL;
static _HSD_ImageDesc stc_light_image;
static _HSD_ImageDesc stc_ramp_image;
static u8            *stc_light_img;
static u8            *stc_ramp_img;

// Light map placement: world X/Z of its corner and the size of one texel.
static float stc_x0, stc_z0, stc_cell_x, stc_cell_z;

// Menu overrides. Index 0 is "Preset", the pass-through value, on every knob.
static int show_index = 0;

static const u32 color_values[] = {
    0,
    RGBA(255, 200, 95, 255),
    RGBA(255, 248, 230, 255),
    RGBA(255, 110, 60, 255),
    RGBA(120, 170, 255, 255),
};
static char *color_names[] = {"Preset", "Gold", "White", "Ember", "Ice"};
WEATHER_KNOB_CHECK(color_values, color_names);
static int color_index = 0;

static const float reach_factors[] = {1.0f, 0.6f, 1.4f, 2.0f};
static char *reach_names[] = {"Preset", "Short", "Long", "Huge"};
WEATHER_KNOB_CHECK(reach_factors, reach_names);
static int reach_index = 0;

static const float brightness_factors[] = {1.0f, 0.6f, 1.5f};
static char *brightness_names[] = {"Preset", "Soft", "Bright"};
WEATHER_KNOB_CHECK(brightness_factors, brightness_names);
static int brightness_index = 0;

static int bust_index = 1;  // On: busting the star puts the light out

static char *shading_names[] = {"Normals", "Light Map"};
static int shading_index = 0;
static int shadows_index = 1;

static int StarPoleKind(YakuKind kind)
{
    return kind == YAKUKIND_STARPOLE;
}

// The star pole's one record: the star itself, a static joint atop the terrain's pole.
// Its GObj destroys itself once the break effects end, but the record and joint last the
// stage's lifetime, and a busted star stays hidden for the rest of the round.
static GrCollRecord *StarPoleRecord(void)
{
    GOBJ *pole;
    if (Yaku_CollectGObjs(StarPoleKind, &pole, 1) != 1)
        return NULL;
    return (GrCollRecord *)Yaku_GetData(pole)->region_audio_arr;
}

JOBJ *StarLight_StarJoint(void)
{
    GrCollRecord *rec = StarPoleRecord();
    return rec ? rec->jobj : NULL;
}

static int StarIsUp(void)
{
    return !bust_index || !(stc_star_jobj->flags & JOBJ_HIDDEN);
}

static int StarFail(const char *why)
{
    OSReport("[StarLight] Not started, %s\n", why);
    stc_failed = 1;
    return 0;
}

static TOBJ *StarTObj(_HSD_ImageDesc *image, u8 *img, int w, int h, int format, int filter)
{
    TOBJ *t = HSD_TObjAlloc();
    if (!t)
        return NULL;
    t->flags = TEX_COORD_SHADOW | TEX_LIGHTMAP_SHADOW | TEX_COLORMAP_MODULATE | TEX_ALPHAMAP_PASS;
    t->wrap_s = GX_CLAMP;
    t->wrap_t = GX_CLAMP;
    t->repeat_s = 1;
    t->repeat_t = 1;
    t->magFilt = filter;
    t->imagedesc = image;
    *image = (_HSD_ImageDesc){
        .img_ptr = img,
        .width = w,
        .height = h,
        .format = format,
    };
    return t;
}

// Once per round: find the star, allocate the bake and the textures, and start the bake.
static int StarSetup(StageNode *sn)
{
    if (stc_ready)
        return 1;
    if (stc_failed)
        return 0;

    GrCollRecord *rec = StarPoleRecord();
    if (!rec || !rec->jobj)
        return StarFail("no star pole on this stage");
    stc_star_jobj = rec->jobj;
    stc_star = rec->aabb_center;

    float w = sn->oob_max.X - sn->oob_min.X;
    float d = sn->oob_max.Z - sn->oob_min.Z;
    stc_x0 = sn->oob_min.X;
    stc_z0 = sn->oob_min.Z;
    stc_cell_x = w / STAR_DIM;
    stc_cell_z = d / STAR_DIM;
    if (stc_star.X <= sn->oob_min.X || stc_star.X >= sn->oob_max.X
        || stc_star.Z <= sn->oob_min.Z || stc_star.Z >= sn->oob_max.Z)
        return StarFail("star outside the stage box");

    // HSD_MemAlloc aligns to 32 bytes, as a GX texture needs.
    stc_height = HSD_MemAlloc(STAR_CELLS * sizeof(float));
    stc_facing = HSD_MemAlloc(STAR_CELLS);
    stc_lit_h = HSD_MemAlloc(STAR_CELLS);
    stc_light_img = HSD_MemAlloc(STAR_TEX_SIZE);
    stc_ramp_img = HSD_MemAlloc(STAR_RAMP_SIZE);
    if (!stc_height || !stc_facing || !stc_lit_h || !stc_light_img || !stc_ramp_img)
        return StarFail("no memory for the light map");

    stc_light_tobj = StarTObj(&stc_light_image, stc_light_img, STAR_DIM, STAR_DIM,
                              GX_TF_RGBA8, GX_LINEAR);
    stc_ramp_tobj = StarTObj(&stc_ramp_image, stc_ramp_img, STAR_RAMP_W, STAR_RAMP_H,
                             GX_TF_I8, GX_NEAR);
    if (!stc_light_tobj || !stc_ramp_tobj)
        return StarFail("no TObj");

    // World (x, z) -> (s, t) over the box, q = 1. HSD_TObjAlloc zeroes the matrix.
    stc_light_tobj->mtx[0][0] = 1.0f / w;
    stc_light_tobj->mtx[0][3] = -stc_x0 / w;
    stc_light_tobj->mtx[1][2] = 1.0f / d;
    stc_light_tobj->mtx[1][3] = -stc_z0 / d;
    stc_light_tobj->mtx[2][3] = 1.0f;

    // An I8 tile is 8x4 texels, one byte each, so a 4-row strip lays out x in order.
    for (int i = 0; i < STAR_RAMP_SIZE; i++)
        stc_ramp_img[i] = (u8)(((i >> 5) << 3) | (i & 7));
    DCFlushRange(stc_ramp_img, STAR_RAMP_SIZE);

    for (int k = 1; k < STAR_MAX_STEPS; k++)
        stc_inv_step[k] = 1.0f / k;

    stc_normals = 0;
    if (stc_hw)
    {
        GrObj *gr = *stc_grobj;
        JOBJ *terrain = gr->joint_table ? gr->joint_table[0].jobj : NULL;
        stc_normals = terrain && MapNormals_Add(terrain, stc_star_jobj) > 0;
        if (!stc_normals)
            OSReport("[StarLight] No map normals, lighting from the light map\n");
    }

    stc_bake = STAR_BAKE_HEIGHTS;
    stc_cursor = 0;
    stc_bake_frames = 0;
    stc_h_min = 1.0e9f;
    stc_h_max = -1.0e9f;
    stc_up = StarIsUp();
    stc_ready = 1;

    OSReport("[StarLight] Star at (%.0f, %.0f, %.0f), baking the light map\n",
             stc_star.X, stc_star.Y, stc_star.Z);
    return 1;
}

// Each column's top surface, and how squarely it faces the star.
static void BakeHeights(StageNode *sn)
{
    GrCollTri *tris = Gr_GetCollTris();
    int end = stc_cursor + STAR_COLUMNS_PER_FRAME;
    if (end > STAR_CELLS)
        end = STAR_CELLS;

    for (int i = stc_cursor; i < end; i++)
    {
        float x = stc_x0 + ((i % STAR_DIM) + 0.5f) * stc_cell_x;
        float z = stc_z0 + ((i / STAR_DIM) + 0.5f) * stc_cell_z;
        Vec3 start = {x, sn->oob_max.Y + WEATHER_CAST_MARGIN, z};
        Vec3 bottom = {x, sn->oob_min.Y, z};
        stc_height[i] = STAR_NO_GROUND;
        stc_facing[i] = 0;

        for (int c = 0; c < STAR_MAX_CASTS; c++)
        {
            Vec3 hit;
            int id = Raycast_Ground(&start, &bottom, &hit);
            if (id < 0)
                break;
            GrCollTri *t = &tris[id];
            int ground = (t->kind & GRCOLL_KIND_GROUNDTYPE_MASK) >> GRCOLL_KIND_GROUNDTYPE_SHIFT;
            if ((t->flags & GRCOLL_FLAG_MOVING) || ground == GRCOLL_GROUNDTYPE_BARRIER)
            {
                start.Y = hit.Y - 1.0f;
                continue;
            }

            float lx = stc_star.X - x;
            float ly = stc_star.Y - hit.Y;
            float lz = stc_star.Z - z;
            float len = sqrtf(lx * lx + ly * ly + lz * lz);
            float nl = 1.0f;
            if (len > 1.0f)
                nl = (t->normal.X * lx + t->normal.Y * ly + t->normal.Z * lz) / len;
            if (nl < 0.0f)
                nl = 0.0f;

            stc_height[i] = hit.Y;
            stc_facing[i] = (u8)(255.0f * lerp(STAR_FACING_MIN, 1.0f, nl));
            if (hit.Y < stc_h_min)
                stc_h_min = hit.Y;
            if (hit.Y > stc_h_max)
                stc_h_max = hit.Y;
            break;
        }
    }
    stc_cursor = end;
}

static int QuantizeHeight(float y)
{
    int q = (int)((y - stc_y_lo) * 256.0f / stc_y_span);
    if (q < 0)
        return 0;
    if (q > 255)
        return 255;
    return q;
}

// The lowest height the star reaches in each column: above the column's own surface and
// above the steepest line from the star over the columns between them.
static void BakeShadows(void)
{
    float su = (stc_star.X - stc_x0) / stc_cell_x;
    float sv = (stc_star.Z - stc_z0) / stc_cell_z;
    float sy = stc_star.Y;
    int end = stc_cursor + STAR_ROWS_PER_FRAME;
    if (end > STAR_DIM)
        end = STAR_DIM;

    for (int row = stc_cursor; row < end; row++)
    {
        for (int col = 0; col < STAR_DIM; col++)
        {
            int i = row * STAR_DIM + col;
            float h = stc_height[i];
            if (h == STAR_NO_GROUND)
            {
                stc_lit_h[i] = 0;
                continue;
            }

            float du = col + 0.5f - su;
            float dv = row + 0.5f - sv;
            float dist = sqrtf(du * du + dv * dv);
            int steps = (int)dist;
            // Max (height - star) per texel of run, over every column the line crosses.
            float best = -1.0e9f;
            if (steps > 2)
            {
                float inv = 65536.0f / dist;
                s32 step_u = (s32)(du * inv);
                s32 step_v = (s32)(dv * inv);
                s32 fu = (s32)(su * 65536.0f) + 2 * step_u;
                s32 fv = (s32)(sv * 65536.0f) + 2 * step_v;
                for (int k = 2; k < steps; k++, fu += step_u, fv += step_v)
                {
                    float m = (stc_height[(fv >> 16) * STAR_DIM + (fu >> 16)] - sy)
                              * stc_inv_step[k];
                    if (m > best)
                        best = m;
                }
            }
            float lit = sy + best * dist;
            stc_lit_h[i] = QuantizeHeight(lit > h ? lit : h);
        }
    }
    stc_cursor = end;
}

static inline u8 *TexelAR(u8 *img, int x, int y)
{
    int block = (y >> 2) * (STAR_DIM >> 2) + (x >> 2);
    int i = ((y & 3) << 2) | (x & 3);
    return img + block * 64 + i * 2;
}

static inline u8 LightTexel(float v)
{
    return (v > 255.0f) ? 255 : (u8)v;
}

// RGB is the light reaching each column's surface, alpha its lit height. The texel
// takes the lower lit height of itself and the column a step toward the star, so a wall
// facing the star takes the ground in front of it instead of the roof behind.
static void Compose(void)
{
    float su = (stc_star.X - stc_x0) / stc_cell_x;
    float sv = (stc_star.Z - stc_z0) / stc_cell_z;
    float inv_r2 = 1.0f / (stc_range * stc_range);
    float k = stc_intensity * STAR_PEAK * 127.5f / (255.0f * 255.0f);
    float kr = k * stc_color.r, kg = k * stc_color.g, kb = k * stc_color.b;
    // Dolphin's default texture cache hashes only a sparse sample of a texture's bytes
    // and reuses its decode while those match. Red's low bit alternates per compose, so a
    // recompose in the same buffer never matches the last one.
    stc_compose_parity ^= 1;

    for (int row = 0; row < STAR_DIM; row++)
    {
        for (int col = 0; col < STAR_DIM; col++)
        {
            int i = row * STAR_DIM + col;
            u8 *ar = TexelAR(stc_light_img, col, row);
            float h = stc_height[i];
            if (h == STAR_NO_GROUND)
            {
                ar[0] = ar[32] = ar[33] = 0;
                ar[1] = stc_compose_parity;
                continue;
            }

            float dx = stc_x0 + (col + 0.5f) * stc_cell_x - stc_star.X;
            float dy = h - stc_star.Y;
            float dz = stc_z0 + (row + 0.5f) * stc_cell_z - stc_star.Z;
            float t = (dx * dx + dy * dy + dz * dz) * inv_r2;
            float f = (t < 1.0f) ? (1.0f - t) * (1.0f - t) * stc_facing[i] : 0.0f;
            ar[1] = (LightTexel(kr * f) & ~1) | stc_compose_parity;
            ar[32] = LightTexel(kg * f);
            ar[33] = LightTexel(kb * f);

            float du = col + 0.5f - su;
            float dv = row + 0.5f - sv;
            float dist = sqrtf(du * du + dv * dv);
            int lit = stc_lit_h[i];
            if (dist > 1.0f)
            {
                int nc = (int)(col + 0.5f - du / dist);
                int nr = (int)(row + 0.5f - dv / dist);
                if (nc >= 0 && nc < STAR_DIM && nr >= 0 && nr < STAR_DIM)
                {
                    int n = stc_lit_h[nr * STAR_DIM + nc];
                    if (n < lit)
                        lit = n;
                }
            }
            lit -= STAR_SHADOW_BIAS;
            ar[0] = (lit < 0) ? 0 : (u8)lit;
        }
    }
    DCFlushRange(stc_light_img, STAR_TEX_SIZE);
    stc_dirty = 0;
}

static void StarBakeStep(StageNode *sn)
{
    stc_bake_frames++;
    if (stc_bake == STAR_BAKE_HEIGHTS)
    {
        BakeHeights(sn);
        if (stc_cursor < STAR_CELLS)
            return;
        if (stc_h_max < stc_h_min)
        {
            StarFail("no ground under the light map");
            return;
        }
        // The ramp spans every surface and the star; the map draw reads a pixel's height
        // as s over it.
        stc_y_lo = stc_h_min - 1.0f;
        stc_y_span = ((stc_h_max > stc_star.Y) ? stc_h_max : stc_star.Y) + 1.0f - stc_y_lo;
        stc_ramp_tobj->mtx[0][1] = 1.0f / stc_y_span;
        stc_ramp_tobj->mtx[0][3] = -stc_y_lo / stc_y_span;
        stc_ramp_tobj->mtx[1][3] = 0.5f;
        stc_ramp_tobj->mtx[2][3] = 1.0f;
        stc_bake = STAR_BAKE_SHADOWS;
        stc_cursor = 0;
        return;
    }

    BakeShadows();
    if (stc_cursor < STAR_DIM)
        return;
    Compose();
    stc_bake = STAR_BAKE_DONE;
    OSReport("[StarLight] Light map baked in %d frames, surfaces %.0f to %.0f\n",
             stc_bake_frames, stc_h_min, stc_h_max);
}

void StarLight_SetActive(const StarLightDef *def)
{
    int preset_on = def && def->enabled;
    stc_active = WeatherToggle(show_index, preset_on);

    u32 color = STAR_DEF_COLOR;
    if (color_index > 0)
        color = color_values[color_index];
    else if (preset_on && def->color)
        color = def->color;
    stc_color = GXColor_Unpack(color);

    float range = (def && def->range > 0.0f) ? def->range : STAR_DEF_RANGE;
    stc_range = range * reach_factors[reach_index];
    float intensity = (def && def->intensity > 0.0f) ? def->intensity : STAR_DEF_INTENSITY;
    stc_intensity = intensity * brightness_factors[brightness_index];
    stc_dirty = 1;

    stc_hw = (shading_index == 0);
    stc_shadows = shadows_index;
    float d0 = stc_range / STAR_HW_FALLOFF;
    float k0 = 1.0f / (STAR_HW_PEAK * stc_intensity);
    GXInitLightColor(&stc_hw_light, &stc_color);
    GXInitLightAttn(&stc_hw_light, 1.0f, 0.0f, 0.0f, k0, 0.0f, k0 / (d0 * d0));
}

void StarLight_Tick(void)
{
    StageNode *sn = Weather_StageNode();
    if (!stc_active || !*stc_grobj || !sn || !StarSetup(sn))
    {
        stc_fade = 0;
        return;
    }
    if (stc_bake != STAR_BAKE_DONE)
    {
        StarBakeStep(sn);
        return;
    }
    if (stc_dirty)
        Compose();

    int up = StarIsUp();
    if (up != stc_up)
    {
        stc_up = up;
        OSReport("[StarLight] Star %s\n", up ? "back up, light on" : "busted, light out");
    }
    int target = up ? STAR_FADE_STEPS : 0;
    if (stc_fade != target && ++stc_fade_timer >= STAR_FADE_FRAMES)
    {
        stc_fade_timer = 0;
        stc_fade += (stc_fade < target) ? 1 : -1;
    }
}

int StarLight_IsActive(void)
{
    return stc_active;
}

int StarLight_GetDraw(StarDraw *out)
{
    if (stc_fade <= 0)
        return 0;
    out->light = stc_light_tobj;
    out->ramp = stc_shadows ? stc_ramp_tobj : NULL;
    out->kcsel = GX_TEV_KCSEL_8_8 + (STAR_FADE_STEPS - stc_fade);
    out->hw = stc_hw && stc_normals;
    return 1;
}

// GX lights sit in eye space, so the star moves into each camera's view.
void StarLight_LoadLight(void)
{
    Vec3 eye;
    MTXMultVec(&COBJ_GetCurrent()->view_mtx, &stc_star, &eye);
    GXInitLightPos(&stc_hw_light, eye.X, eye.Y, eye.Z);
    GXLoadLightObjImm(&stc_hw_light, STAR_HW_LIGHT);
}

void StarLight_SetupChannel(void)
{
    HSD_SetupChannel(&stc_hw_chan);
    HSD_StateSetNumChans(2);
}

void StarLight_Reset(void)
{
    stc_ready = 0;
    stc_failed = 0;
    stc_star_jobj = NULL;
    stc_normals = 0;
    stc_fade = 0;
    stc_fade_timer = 0;
    stc_light_tobj = NULL;
    stc_ramp_tobj = NULL;
    stc_height = NULL;
    stc_facing = NULL;
    stc_lit_h = NULL;
    stc_light_img = NULL;
    stc_ramp_img = NULL;
}

static void OnStarShowChange(int val)
{
    OSReport("[StarLight] Star light %s\n", weather_toggle_names[val]);
}

static void OnStarColorChange(int val)
{
    OSReport("[StarLight] Color %s\n", color_names[val]);
}

static void OnStarReachChange(int val)
{
    OSReport("[StarLight] Reach %s\n", reach_names[val]);
}

static void OnStarBrightnessChange(int val)
{
    OSReport("[StarLight] Brightness %s\n", brightness_names[val]);
}

static void OnStarBustChange(int val)
{
    OSReport("[StarLight] Bust puts the light out %s\n", weather_onoff_names[val]);
}

static void OnStarShadingChange(int val)
{
    OSReport("[StarLight] Shading %s\n", shading_names[val]);
}

static void OnStarShadowsChange(int val)
{
    OSReport("[StarLight] Shadows %s\n", weather_onoff_names[val]);
}

MenuDesc starlight_menu = {
    .option_num = 7,
    .options = {
        &(OptionDesc){
            .name = "Star Light",
            .description = "Light the city from the star pole's star, with shadows: Preset = only presets that set it, Off = never, On = all presets",
            .kind = OPTKIND_VALUE,
            .val = &show_index,
            .value_num = WEATHER_TOGGLE_NUM,
            .value_names = weather_toggle_names,
            .on_change = OnStarShowChange,
        },
        &(OptionDesc){
            .name = "Star Color",
            .description = "The colour of the star's light",
            .kind = OPTKIND_VALUE,
            .val = &color_index,
            .value_num = GetElementsIn(color_values),
            .value_names = color_names,
            .on_change = OnStarColorChange,
        },
        &(OptionDesc){
            .name = "Star Reach",
            .description = "How far across the city the star's light carries",
            .kind = OPTKIND_VALUE,
            .val = &reach_index,
            .value_num = GetElementsIn(reach_factors),
            .value_names = reach_names,
            .on_change = OnStarReachChange,
        },
        &(OptionDesc){
            .name = "Star Brightness",
            .description = "How bright the star's light is",
            .kind = OPTKIND_VALUE,
            .val = &brightness_index,
            .value_num = GetElementsIn(brightness_factors),
            .value_names = brightness_names,
            .on_change = OnStarBrightnessChange,
        },
        &(OptionDesc){
            .name = "Bust Puts It Out",
            .description = "Busting the star pole's star puts its light out for the rest of the round",
            .kind = OPTKIND_VALUE,
            .val = &bust_index,
            .value_num = WEATHER_ONOFF_NUM,
            .value_names = weather_onoff_names,
            .on_change = OnStarBustChange,
        },
        &(OptionDesc){
            .name = "Star Shading",
            .description = "Normals = a hardware light on normals added to the map, Light Map = light baked per column from above",
            .kind = OPTKIND_VALUE,
            .val = &shading_index,
            .value_num = GetElementsIn(shading_names),
            .value_names = shading_names,
            .on_change = OnStarShadingChange,
        },
        &(OptionDesc){
            .name = "Star Shadows",
            .description = "Buildings and hills block the star's light",
            .kind = OPTKIND_VALUE,
            .val = &shadows_index,
            .value_num = WEATHER_ONOFF_NUM,
            .value_names = weather_onoff_names,
            .on_change = OnStarShadowsChange,
        },
    },
};
