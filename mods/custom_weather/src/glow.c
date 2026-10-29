// Machine glow for custom_weather: a pool of light on the ground under each player's
// machine, over a city darkened to the preset's ambient. Both live in one world-space
// light map that HSD's shadow-texture path projects straight down onto every map
// material. The star light's map and shadow test ride the same path.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "gx.h"
#include "collision.h"
#include "inline.h"
#include "code_patch/code_patch.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

// The light map spans the out-of-bounds box, ~20 world units a texel over City Trial's.
#define GLOW_TEX_DIM  128
#define GLOW_TEX_SIZE (GLOW_TEX_DIM * GLOW_TEX_DIM * 4)  // RGBA8

// The map draw runs the shadow stage at GX_CS_SCALE_2, so a texel scales the surface by
// twice its value and 128 leaves it as baked.
#define GLOW_TEXEL_UNIT 127.5f

#define GLOW_DEF_RADIUS    180.0f
#define GLOW_DEF_INTENSITY 1.0f
// Brightness a full-intensity pool adds at its centre, over the ambient.
#define GLOW_PEAK 1.4f

// The pool fades out, widening, as the machine climbs this far above the ground.
#define GLOW_FADE_HEIGHT 300.0f
#define GLOW_CAST_LIFT   10.0f
#define GLOW_CAST_DEPTH  1500.0f

// Per controller slot, pale enough to read as light.
static const GXColor glow_ply_colors[PLY_NUM] = {
    {255, 120, 110, 255},
    {120, 160, 255, 255},
    {255, 225, 110, 255},
    {130, 240, 140, 255},
    {255, 255, 255, 255},
};

static int stc_active = 0;  // the pools; the darkness also runs under the star light

// The live preset's GlowDef with the defaults and menu overrides folded in, latched by
// Glow_SetActive.
static GXColor stc_ambient;
static u32     stc_color;  // 0 = each player's colour
static float   stc_radius;
static float   stc_intensity;

static TOBJ          *stc_tobj = NULL;
static _HSD_ImageDesc stc_image;
// Written on alternate frames, so the CPU never fills the one the GPU may still read.
static u8            *stc_img[2];
static int            stc_img_next = 0;
static u8             stc_fill_count = 0;
static int            stc_ready = 0;   // map callback wrapped, terrain flagged, TObj built
static int            stc_failed = 0;  // setup failed this round
static int            stc_lit = 0;     // the light map is current and the map draw applies it
static int            stc_drawing = 0; // inside the map's draw, with the TObjs on the shadow list

// The star light for this draw; its light is NULL when the star is dark.
static StarDraw stc_star;

// Light map placement: world X/Z of its corner and the size of one texel.
static float stc_x0, stc_z0, stc_cell_x, stc_cell_z;

// Menu overrides. Index 0 is "Preset", the pass-through value, on every knob.
static int show_index = 0;

static const u32 darkness_values[] = {
    0,
    RGBA(255, 255, 255, 255),
    RGBA(170, 165, 180, 255),
    RGBA(85, 95, 135, 255),
    RGBA(30, 34, 52, 255),
};
static char *darkness_names[] = {"Preset", "None", "Dusk", "Night", "Pitch Black"};
WEATHER_KNOB_CHECK(darkness_values, darkness_names);
static int darkness_index = 0;

static const float size_factors[] = {1.0f, 0.6f, 1.5f, 2.2f};
static char *size_names[] = {"Preset", "Small", "Large", "Huge"};
WEATHER_KNOB_CHECK(size_factors, size_names);
static int size_index = 0;

static const float brightness_factors[] = {1.0f, 0.5f, 1.6f};
static char *brightness_names[] = {"Preset", "Soft", "Bright"};
WEATHER_KNOB_CHECK(brightness_factors, brightness_names);
static int brightness_index = 0;

// Player resolves to 0, each player's own colour.
static const u32 color_values[] = {0, 0, RGBA(255, 255, 255, 255), RGBA(255, 200, 130, 255)};
static char *color_names[] = {"Preset", "Player", "White", "Warm"};
WEATHER_KNOB_CHECK(color_values, color_names);
static int color_index = 0;

typedef struct GlowFlagCount
{
    int lit;
    int skinned;
    JOBJ *skip;  // the star pole's star
} GlowFlagCount;

// The projection runs vertex positions through PNMTX0 alone, which a skinned POBJ's
// per-vertex matrices do not follow, so those DObjs keep their own look.
static int DObjIsSkinned(DOBJ *d)
{
    for (POBJ *p = d->pobj; p != NULL; p = p->next)
    {
        if ((p->flags & POBJ_TYPE_MASK) == POBJ_ENVELOPE)
            return 1;
    }
    return 0;
}

static void FlagJoint(JOBJ *j, void *arg)
{
    GlowFlagCount *n = arg;
    if ((j->flags & (JOBJ_SPLINE | JOBJ_PTCL)) || j == n->skip)
        return;
    for (DOBJ *d = j->dobj; d != NULL; d = d->next)
    {
        if (!d->mobj)
            continue;
        if (DObjIsSkinned(d))
        {
            n->skinned++;
            continue;
        }
        HSD_MObjSetFlags(d->mobj, RENDER_SHADOW);
        n->lit++;
    }
}

// A tree's root and every joint below it, but not the root's siblings.
static void FlagTree(JOBJ *root, GlowFlagCount *n)
{
    FlagJoint(root, n);
    if (root->child)
        JObj_ForEachJoint(root->child, FlagJoint, n);
}

// The map draw's shadow stages. The material's colour is in PREV; each stage takes its
// stage, coord and map from HSD's own. Alone, the light map scales PREV by twice its
// texel. Under the star light the stages run star, ramp, light map, then one more:
//   star:      C2 = star light x fade, A2 = the column's lit height (1 with shadows off)
//   ramp:      A2 = (this pixel's height > A2) ? 1 : 0
//   light map: C2 = light map texel + C2 x A2
//   apply:     PREV = PREV x C2 x 2
// The star light is the star's light map texel, or COLOR1 lit by its GX light.
#define TEV_STAGE(...) {.flags = 1, .chan = GX_COLOR_NULL, .clr_op = GX_TEV_ADD,      \
                        .clr_a = GX_CC_ZERO, .clr_b = GX_CC_ZERO, .clr_c = GX_CC_ZERO, \
                        .clr_d = GX_CC_CPREV, .clr_clamp = 1, .clr_out_reg = GX_TEVPREV,  \
                        .alpha_op = GX_TEV_ADD, .alpha_a = GX_CA_ZERO,                   \
                        .alpha_b = GX_CA_ZERO, .alpha_c = GX_CA_ZERO,                    \
                        .alpha_d = GX_CA_APREV, .alpha_clamp = 1,                        \
                        .alpha_out_reg = GX_TEVPREV, __VA_ARGS__}

static const HSD_TevDesc tev_glow = TEV_STAGE(
    .clr_b = GX_CC_CPREV, .clr_c = GX_CC_TEXC, .clr_d = GX_CC_ZERO, .clr_scale = GX_CS_SCALE_2);

static const HSD_TevDesc tev_star = TEV_STAGE(
    .clr_b = GX_CC_TEXC, .clr_c = GX_CC_KONST, .clr_d = GX_CC_ZERO, .clr_out_reg = GX_TEVREG2,
    .alpha_d = GX_CA_TEXA, .alpha_out_reg = GX_TEVREG2);

static const HSD_TevDesc tev_star_hw = TEV_STAGE(
    .chan = GX_COLOR1A1, .clr_b = GX_CC_RASC, .clr_c = GX_CC_KONST, .clr_d = GX_CC_ZERO,
    .clr_out_reg = GX_TEVREG2, .alpha_d = GX_CA_TEXA, .alpha_out_reg = GX_TEVREG2);

static const HSD_TevDesc tev_star_shadow = TEV_STAGE(
    .alpha_op = GX_TEV_COMP_A8_GT, .alpha_a = GX_CA_TEXA, .alpha_b = GX_CA_A2,
    .alpha_c = GX_CA_KONST, .alpha_d = GX_CA_ZERO, .alpha_out_reg = GX_TEVREG2,
    .kasel = GX_TEV_KASEL_1);

static const HSD_TevDesc tev_star_glow = TEV_STAGE(
    .clr_b = GX_CC_C2, .clr_c = GX_CC_A2, .clr_d = GX_CC_TEXC, .clr_out_reg = GX_TEVREG2);

static const HSD_TevDesc tev_star_apply = TEV_STAGE(
    .clr_b = GX_CC_CPREV, .clr_c = GX_CC_C2, .clr_d = GX_CC_ZERO, .clr_scale = GX_CS_SCALE_2,
    .coord = GX_TEXCOORD_NULL, .map = GX_TEXMAP_NULL);

static void SetupStage(const HSD_TevDesc *src, const HSD_TevDesc *hsd, int kcsel)
{
    HSD_TevDesc d = *src;
    d.stage = hsd->stage;
    d.coord = hsd->coord;
    d.map = hsd->map;
    d.kcsel = kcsel;
    HSD_SetupTevStage(&d);
}

// Stands in for HSD_SetupTevStage in HSD_TObjSetupVolatileTev's shadow loop
// (0x803f5f50), which passes the shared template with this stage's resources filled in.
// A TObj is matched by its texmap, which HSD assigns per material before the loop.
static void Glow_SetupShadowStage(HSD_TevDesc *desc)
{
    if (!stc_drawing)
    {
        HSD_SetupTevStage(desc);
        return;
    }

    if (stc_star.light && desc->map == stc_star.light->id)
    {
        HSD_TevDesc star = stc_star.hw ? tev_star_hw : tev_star;
        if (!stc_star.ramp)
        {
            star.alpha_d = GX_CA_KONST;
            star.kasel = GX_TEV_KASEL_1;
        }
        SetupStage(&star, desc, stc_star.kcsel);
    }
    else if (stc_star.ramp && desc->map == stc_star.ramp->id)
        SetupStage(&tev_star_shadow, desc, GX_TEV_KCSEL_1);
    else if (desc->map == stc_tobj->id && stc_star.light)
    {
        SetupStage(&tev_star_glow, desc, GX_TEV_KCSEL_1);
        HSD_TevDesc apply = tev_star_apply;
        apply.stage = HSD_StateAssignTev();
        HSD_SetupTevStage(&apply);
    }
    else if (desc->map == stc_tobj->id)
        SetupStage(&tev_glow, desc, GX_TEV_KCSEL_1);
    else
        HSD_SetupTevStage(desc);
}

// Stands in for HSD_SetupChannelMode, the last call of HSD_SetupRenderModeWithCustomPE
// (0x803f82a4). Under the star's GX light a receiver also gets COLOR1, which its star
// stage reads; only a specular material already uses COLOR1.
static void Glow_SetupChannelMode(u32 rendermode)
{
    HSD_SetupChannelMode(rendermode);
    if (stc_drawing && stc_star.light && stc_star.hw && (rendermode & RENDER_SHADOW)
        && !(rendermode & RENDER_SPECULAR))
        StarLight_SetupChannel();
}

// The light map reaches only what draws inside the map's own callback, and the shared
// shadow list is vanilla again once it returns. Added last, the star's TObjs lead the
// list, since each add prepends.
static void Glow_MapGX(GOBJ *g, int pass)
{
    if (!stc_lit)
    {
        Map_GX(g, pass);
        return;
    }

    HSD_MObjAddShadowTexture(stc_tobj);
    if (StarLight_GetDraw(&stc_star))
    {
        if (stc_star.ramp)
            HSD_MObjAddShadowTexture(stc_star.ramp);
        HSD_MObjAddShadowTexture(stc_star.light);
        if (stc_star.hw)
            StarLight_LoadLight();
    }
    else
        stc_star.light = NULL;
    GXInvalidateTexAll();

    stc_drawing = 1;
    Map_GX(g, pass);
    stc_drawing = 0;

    if (stc_star.light)
    {
        HSD_MObjDeleteShadowTexture(stc_star.light);
        if (stc_star.ramp)
            HSD_MObjDeleteShadowTexture(stc_star.ramp);
    }
    HSD_MObjDeleteShadowTexture(stc_tobj);
}

static int GlowFail(const char *why)
{
    OSReport("[MachineGlow] Not started, %s\n", why);
    stc_failed = 1;
    return 0;
}

// Once per round: the light map buffers, its TObj, the receiver flags on the terrain and
// the map callback wrap.
static int GlowSetup(GrObj *gr, StageNode *sn)
{
    if (stc_ready)
        return 1;
    if (stc_failed)
        return 0;

    GOBJ *map = gr->gobj;
    JOBJ *terrain = gr->joint_table ? gr->joint_table[0].jobj : NULL;
    if (!map || map->gx_cb != Map_GX || !terrain)
        return GlowFail("map draw not found");

    // HSD_MemAlloc aligns to 32 bytes, as a GX texture needs.
    for (int i = 0; i < 2; i++)
    {
        stc_img[i] = HSD_MemAlloc(GLOW_TEX_SIZE);
        if (!stc_img[i])
            return GlowFail("no memory for the light map");
    }

    TOBJ *t = HSD_TObjAlloc();
    if (!t)
        return GlowFail("no TObj");
    t->flags = TEX_COORD_SHADOW | TEX_LIGHTMAP_SHADOW | TEX_COLORMAP_MODULATE | TEX_ALPHAMAP_PASS;
    t->wrap_s = GX_CLAMP;
    t->wrap_t = GX_CLAMP;
    t->repeat_s = 1;
    t->repeat_t = 1;
    t->magFilt = GX_LINEAR;
    t->imagedesc = &stc_image;

    stc_image = (_HSD_ImageDesc){
        .img_ptr = stc_img[0],
        .width = GLOW_TEX_DIM,
        .height = GLOW_TEX_DIM,
        .format = GX_TF_RGBA8,
    };

    // World (x, z) -> (s, t) over the box, q = 1.
    float w = sn->oob_max.X - sn->oob_min.X;
    float d = sn->oob_max.Z - sn->oob_min.Z;
    stc_x0 = sn->oob_min.X;
    stc_z0 = sn->oob_min.Z;
    stc_cell_x = w / GLOW_TEX_DIM;
    stc_cell_z = d / GLOW_TEX_DIM;
    for (int r = 0; r < 3; r++)
    {
        for (int c = 0; c < 4; c++)
            t->mtx[r][c] = 0.0f;
    }
    t->mtx[0][0] = 1.0f / w;
    t->mtx[0][3] = -stc_x0 / w;
    t->mtx[1][2] = 1.0f / d;
    t->mtx[1][3] = -stc_z0 / d;
    t->mtx[2][3] = 1.0f;
    stc_tobj = t;

    GlowFlagCount n = {0, 0, StarLight_StarJoint()};
    FlagTree(terrain, &n);
    map->gx_cb = Glow_MapGX;
    stc_ready = 1;

    OSReport("[MachineGlow] Light map on %d terrain materials, %d skinned skipped\n",
             n.lit, n.skinned);
    return 1;
}

static inline u8 TexelFromLight(u8 c)
{
    return c >> 1;
}

static void GlowFill(u8 *img)
{
    u32 r = TexelFromLight(stc_ambient.r);
    u32 g = TexelFromLight(stc_ambient.g);
    u32 b = TexelFromLight(stc_ambient.b);
    // The shadow stage passes the previous alpha through, so the texture's alpha carries a
    // fill count instead: Dolphin's default texture cache hashes only a sample of the
    // texels, and would keep a stale light map while the pools move between them.
    u32 a = stc_fill_count++;
    // An RGBA8 4x4 tile is 16 (A, R) pairs, then 16 (G, B) pairs.
    u32 ar = (a << 24) | (r << 16) | (a << 8) | r;
    u32 gb = (g << 24) | (b << 16) | (g << 8) | b;

    u32 *w = (u32 *)img;
    for (int blk = 0; blk < GLOW_TEX_SIZE / 64; blk++)
    {
        for (int i = 0; i < 8; i++)
            *w++ = ar;
        for (int i = 0; i < 8; i++)
            *w++ = gb;
    }
}

static inline u8 *TexelAR(u8 *img, int x, int y)
{
    int block = (y >> 2) * (GLOW_TEX_DIM >> 2) + (x >> 2);
    int i = ((y & 3) << 2) | (x & 3);
    return img + block * 64 + i * 2;
}

static inline u8 AddTexel(u8 v, float add)
{
    float f = v + add;
    return (f > 255.0f) ? 255 : (u8)f;
}

static int ClampTexel(int v)
{
    // The border stays ambient, so clamped sampling past the box does too.
    if (v < 1)
        return 1;
    if (v > GLOW_TEX_DIM - 2)
        return GLOW_TEX_DIM - 2;
    return v;
}

// Adds a pool at world (x, z), falling off as (1 - d^2/r^2)^2.
static void GlowAddPool(u8 *img, float x, float z, float radius, GXColor c, float amp)
{
    float u = (x - stc_x0) / stc_cell_x - 0.5f;
    float v = (z - stc_z0) / stc_cell_z - 0.5f;
    int u0 = ClampTexel((int)(u - radius / stc_cell_x));
    int u1 = ClampTexel((int)(u + radius / stc_cell_x) + 1);
    int v0 = ClampTexel((int)(v - radius / stc_cell_z));
    int v1 = ClampTexel((int)(v + radius / stc_cell_z) + 1);

    float inv_r2 = 1.0f / (radius * radius);
    float k = amp * GLOW_TEXEL_UNIT / 255.0f;
    float kr = k * c.r, kg = k * c.g, kb = k * c.b;

    for (int ty = v0; ty <= v1; ty++)
    {
        float dz = (ty - v) * stc_cell_z;
        for (int tx = u0; tx <= u1; tx++)
        {
            float dx = (tx - u) * stc_cell_x;
            float d2 = (dx * dx + dz * dz) * inv_r2;
            if (d2 >= 1.0f)
                continue;
            float f = (1.0f - d2) * (1.0f - d2);
            u8 *ar = TexelAR(img, tx, ty);
            ar[1] = AddTexel(ar[1], kr * f);
            ar[32] = AddTexel(ar[32], kg * f);
            ar[33] = AddTexel(ar[33], kb * f);
        }
    }
}

void Glow_SetActive(const GlowDef *def)
{
    int preset_on = def && def->enabled;
    stc_active = WeatherToggle(show_index, preset_on);

    // The preset's darkness holds with the pools off, for the star light.
    u32 ambient = RGBA(255, 255, 255, 255);
    if (darkness_index > 0)
        ambient = darkness_values[darkness_index];
    else if (def && def->ambient)
        ambient = def->ambient;
    stc_ambient = GXColor_Unpack(ambient);

    stc_color = (color_index > 0) ? color_values[color_index] : (preset_on ? def->color : 0);

    float radius = (def && def->radius > 0.0f) ? def->radius : GLOW_DEF_RADIUS;
    stc_radius = radius * size_factors[size_index];
    float intensity = (def && def->intensity > 0.0f) ? def->intensity : GLOW_DEF_INTENSITY;
    stc_intensity = intensity * brightness_factors[brightness_index];
}

void Glow_Tick(void)
{
    GrObj *gr = *stc_grobj;
    StageNode *sn = Weather_StageNode();
    if (!(stc_active || StarLight_IsActive()) || !gr || !sn || !GlowSetup(gr, sn))
    {
        stc_lit = 0;
        return;
    }

    u8 *img = stc_img[stc_img_next];
    stc_img_next ^= 1;
    GlowFill(img);

    for (int ply = 0; stc_active && ply < PLY_NUM; ply++)
    {
        MachineData *md = Ply_GetMachineData(ply);
        if (md == NULL || md->is_dead)
            continue;

        Vec3 start = {md->pos.X, md->pos.Y + GLOW_CAST_LIFT, md->pos.Z};
        Vec3 end = {md->pos.X, md->pos.Y - GLOW_CAST_DEPTH, md->pos.Z};
        Vec3 hit;
        if (Raycast_Ground(&start, &end, &hit) < 0)
            continue;
        float h = (md->pos.Y - hit.Y) / GLOW_FADE_HEIGHT;
        if (h >= 1.0f)
            continue;
        if (h < 0.0f)
            h = 0.0f;

        GXColor c = stc_color ? GXColor_Unpack(stc_color) : glow_ply_colors[ply];
        GlowAddPool(img, md->pos.X, md->pos.Z, stc_radius * (1.0f + 0.5f * h), c,
                    stc_intensity * GLOW_PEAK * (1.0f - h));
    }

    DCFlushRange(img, GLOW_TEX_SIZE);
    stc_image.img_ptr = img;
    stc_lit = 1;
}

void Glow_Reset(void)
{
    stc_tobj = NULL;
    stc_star.light = NULL;
    stc_img[0] = NULL;
    stc_img[1] = NULL;
    stc_img_next = 0;
    stc_ready = 0;
    stc_failed = 0;
    stc_lit = 0;
}

void Glow_OnBoot(void)
{
    CODEPATCH_REPLACECALL(0x803f5f50, Glow_SetupShadowStage);
    CODEPATCH_REPLACECALL(0x803f82a4, Glow_SetupChannelMode);
    OSReport("[MachineGlow] Hooks installed\n");
}

static void OnGlowShowChange(int val)
{
    OSReport("[MachineGlow] Machine glow %s\n", weather_toggle_names[val]);
}

static void OnGlowDarknessChange(int val)
{
    OSReport("[MachineGlow] Darkness %s\n", darkness_names[val]);
}

static void OnGlowSizeChange(int val)
{
    OSReport("[MachineGlow] Size %s\n", size_names[val]);
}

static void OnGlowBrightnessChange(int val)
{
    OSReport("[MachineGlow] Brightness %s\n", brightness_names[val]);
}

static void OnGlowColorChange(int val)
{
    OSReport("[MachineGlow] Color %s\n", color_names[val]);
}

MenuDesc glow_menu = {
    .option_num = 5,
    .options = {
        &(OptionDesc){
            .name = "Machine Glow",
            .description = "Light a pool on the ground under every machine: Preset = only presets that set it, Off = never, On = all presets",
            .kind = OPTKIND_VALUE,
            .val = &show_index,
            .value_num = WEATHER_TOGGLE_NUM,
            .value_names = weather_toggle_names,
            .on_change = OnGlowShowChange,
        },
        &(OptionDesc){
            .name = "Darkness",
            .description = "How dark the city is away from the glow",
            .kind = OPTKIND_VALUE,
            .val = &darkness_index,
            .value_num = GetElementsIn(darkness_values),
            .value_names = darkness_names,
            .on_change = OnGlowDarknessChange,
        },
        &(OptionDesc){
            .name = "Glow Size",
            .description = "How wide each machine's pool of light spreads",
            .kind = OPTKIND_VALUE,
            .val = &size_index,
            .value_num = GetElementsIn(size_factors),
            .value_names = size_names,
            .on_change = OnGlowSizeChange,
        },
        &(OptionDesc){
            .name = "Brightness",
            .description = "How bright the centre of each pool gets",
            .kind = OPTKIND_VALUE,
            .val = &brightness_index,
            .value_num = GetElementsIn(brightness_factors),
            .value_names = brightness_names,
            .on_change = OnGlowBrightnessChange,
        },
        &(OptionDesc){
            .name = "Glow Color",
            .description = "Each player's colour, or one colour for every machine",
            .kind = OPTKIND_VALUE,
            .val = &color_index,
            .value_num = GetElementsIn(color_values),
            .value_names = color_names,
            .on_change = OnGlowColorChange,
        },
    },
};
