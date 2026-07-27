// World lightning for custom_weather: a per-preset strike loop - a screen/EFB
// flash (and an opt-in jagged GX bolt) punctuating long random lulls.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "gx.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

// Defaults applied when a preset leaves the matching LightningDef field 0.
#define LTNG_DEF_FLASH_COLOR   RGBA(255, 250, 240, 255) // near-white strike
#define LTNG_DEF_FLASH_FRAMES  18                       // flash envelope length
#define LTNG_DEF_MIN_LULL      180                      // 3s at 60fps
#define LTNG_DEF_MAX_LULL      420                      // 7s at 60fps
#define LTNG_INITIAL_LULL      30                       // first strike after 0.5s

// Per-strike flicker ranges, rolled fresh each strike so no two flashes read alike.
#define LTNG_INTENSITY_MIN    0.55f  // dimmest strike; 1.0 = full brilliant strike
#define LTNG_LEN_MIN_SCALE    0.65f  // envelope length rolled around the preset len
#define LTNG_LEN_MAX_SCALE    1.35f
#define LTNG_STROBE_ON_MIN    1      // frames held full per strobe cycle
#define LTNG_STROBE_ON_MAX    4
#define LTNG_STROBE_GAP_MIN   1      // frames held at the dim floor per cycle
#define LTNG_STROBE_GAP_MAX   4
#define LTNG_STROBE_FLOOR_MIN 0.08f  // sharp near-dark flicker
#define LTNG_STROBE_FLOOR_MAX 0.45f  // shallow shimmer

// Bolt geometry (world units): a jagged channel from sky to ground with one fork.
#define BOLT_TOP_Y        820.0f
#define BOLT_GROUND_Y     -40.0f
#define BOLT_SEGMENTS     13              // main channel segment count
#define BOLT_FORK_SEGS    4              // offshoot length
#define BOLT_MAX_SEG      (BOLT_SEGMENTS + BOLT_FORK_SEGS)
#define BOLT_JITTER       42.0f          // max horizontal wander per main step
#define BOLT_FORK_JITTER  60.0f
#define BOLT_SPREAD       1000.0f        // fallback scatter half-width (used only if no OOB box)
#define BOLT_DEF_COLOR    RGBA(210, 225, 255, 255) // fallback blue-white
#define BOLT_GLOW_WIDTH   44             // wide dim glow pass (1/6-px units)
#define BOLT_CORE_WIDTH   14             // thin bright core pass
#define BOLT_GLOW_ALPHA   0.5f           // glow alpha relative to the core

// Omnidirectional point light at the bolt midpoint, with computed attenuation
// (ref_br at ref_dist) so the engine derives the GX coefficients.
#define BOLT_LIGHT_REF_DIST  520.0f
#define BOLT_LIGHT_REF_BR    0.5f
#define GX_SPOT_OFF          0           // GXSpotFn GX_SP_OFF (omnidirectional)
#define GX_DIST_MEDIUM       2           // GXDistAttnFn GX_DA_MEDIUM

// Bolt render GObj: an entity class / p_link high enough to avoid the engine's own,
// on the world camera's gx_link 0, XLU sub-pass.
#define BOLT_GOBJ_CLASS   202
#define BOLT_GOBJ_PLINK   26
#define BOLT_GX_LINK      0
#define BOLT_GX_PRI       0

// Light-carrier GObjs (the overhead flash LOBJ and the bolt-midpoint point LOBJ).
#define LTNG_LOBJ_GOBJ_CLASS  38
#define LTNG_LOBJ_GOBJ_PLINK  32

// Overhead INFINITE flash LOBJ (the global rider flash). For INFINITE (flags & 3
// == 1) LObjLoad allocates a WObj from `position` and ignores the union; the WObj
// pos vector is the direction, shining toward the origin from that point.
static WOBJDesc s_flash_pos_desc = {
    .class_name = 0,
    .pos = { 0.0f, 1500.0f, 0.0f }, // overhead
    .robjdesc = 0,
    .next = 0,
};
static LObjDesc s_flash_lobj_desc = {
    .class_name = 0,
    .next = 0,
    .flags = LOBJ_INFINITE | LOBJ_DIFFUSE | LOBJ_SPECULAR, // 0x0D
    .attnflags = 0,
    .color = { 0, 0, 0, 0xFF },
    .position = (struct _HSD_WObjDesc *)&s_flash_pos_desc,
    .interest = 0,
    .u = { .p = 0 },
};

// POINT LOBJ at the bolt midpoint. For POINT (flags & 3 == 2) LObjLoad loads
// `position` as the world point; a clear attnflags bit 0 selects the computed
// ref-brightness/ref-distance falloff.
static WOBJDesc s_bolt_pos_desc = {
    .class_name = 0,
    .pos = { 0.0f, 400.0f, 0.0f }, // repositioned to the bolt midpoint each strike
    .robjdesc = 0,
    .next = 0,
};
static struct _HSD_LightPointDesc s_bolt_point = {
    .cutoff = 1.0f,
    .point_func = GX_SPOT_OFF,
    .ref_br = BOLT_LIGHT_REF_BR,
    .ref_dist = BOLT_LIGHT_REF_DIST,
    .dist_func = GX_DIST_MEDIUM,
};
static LObjDesc s_bolt_lobj_desc = {
    .class_name = 0,
    .next = 0,
    .flags = LOBJ_POINT | LOBJ_DIFFUSE | LOBJ_SPECULAR, // 0x0E
    .attnflags = 0,        // computed (ref_br/ref_dist) attenuation, not raw
    .color = { 0, 0, 0, 0xFF },
    .position = (struct _HSD_WObjDesc *)&s_bolt_pos_desc,
    .interest = 0,
    .u = { .point = &s_bolt_point },
};

static LOBJ *s_flash_lobj = 0;
static LOBJ *s_bolt_lobj = 0;
static GOBJ *s_bolt_render = 0;

static int stc_active = 0;

// Resolved appearance/timing for the active preset (LightningDef + defaults).
static GXColor stc_flash_color = {255, 250, 240, 255};
static GXColor stc_bolt_color = {210, 225, 255, 255};
static int     stc_flash_len = LTNG_DEF_FLASH_FRAMES;
static int     stc_min_lull = LTNG_DEF_MIN_LULL;
static int     stc_max_lull = LTNG_DEF_MAX_LULL;
static int     stc_preset_bolt = LTNG_BOLT_OFF; // this preset's bolt mode

// Strike state machine: counts down lull, then counts down a flash envelope.
static int s_lull_frames = LTNG_INITIAL_LULL;
static int s_flash_frames = 0;

// Per-strike appearance, rolled when a strike fires.
static int   s_strike_len = LTNG_DEF_FLASH_FRAMES; // this strike's envelope length
static float s_strike_intensity = 1.0f;            // this strike's peak brightness
static int   s_strike_on = LTNG_STROBE_ON_MAX;     // frames at full per strobe cycle
static int   s_strike_gap = LTNG_STROBE_GAP_MAX;   // frames at the dim floor per cycle
static float s_strike_floor = LTNG_STROBE_FLOOR_MIN; // this strike's dim floor level

// Bolt geometry (world space), generated per strike. Segments are point pairs so
// the fork is just extra entries past the main channel.
static Vec3 s_seg_a[BOLT_MAX_SEG];
static Vec3 s_seg_b[BOLT_MAX_SEG];
static int  s_seg_count = 0;
static Vec3 s_bolt_mid = {0.0f, 0.0f, 0.0f};

static char *bolt_override_names[] = {"Auto", "Off", "Force"};
static int   bolt_override_index = 0; // 0=Auto (honor preset), 1=Off, 2=Force

// Effective bolt mode from the preset's setting and the menu override. Force lifts
// an off/augment preset to augment, but honors a preset that asked to replace the
// flash.
static int EffectiveBoltMode(void)
{
    switch (bolt_override_index)
    {
    case 1: return LTNG_BOLT_OFF;
    case 2: return (stc_preset_bolt == LTNG_BOLT_REPLACE) ? LTNG_BOLT_REPLACE : LTNG_BOLT_AUGMENT;
    default: return stc_preset_bolt;
    }
}

// Strobe brightness in [0,1] for the current frame of the flash window: a few sharp
// pulses decaying to nothing, driving the flash and the bolt alike.
static float FlashBrightness(void)
{
    if (s_flash_frames <= 0 || s_strike_len <= 0)
        return 0.0f;
    int elapsed = s_strike_len - s_flash_frames; // 0 .. len-1
    float decay = (float)s_flash_frames / (float)s_strike_len;
    int phase = elapsed % (s_strike_on + s_strike_gap);
    float strobe = (phase < s_strike_on) ? 1.0f : s_strike_floor;
    return decay * strobe * s_strike_intensity;
}

// Ground anchor for a strike: a uniform random XZ inside the stage's out-of-bounds
// box. Without one, a random active rider's XZ plus a wide scatter, else the origin.
static void StrikeAnchor(float *ax, float *az)
{
    GrObj *gr = *stc_grobj;
    if (gr && gr->gr_data && gr->gr_data->stage_node)
    {
        StageNode *sn = gr->gr_data->stage_node;
        *ax = sn->oob_min.X + HSD_Randf() * (sn->oob_max.X - sn->oob_min.X);
        *az = sn->oob_min.Z + HSD_Randf() * (sn->oob_max.Z - sn->oob_min.Z);
        return;
    }

    GOBJ *riders[WEATHER_PLAYER_SLOTS];
    int count = 0;
    for (int i = 0; i < WEATHER_PLAYER_SLOTS; i++)
    {
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (rg)
            riders[count++] = rg;
    }

    float cx = 0.0f, cz = 0.0f;
    if (count > 0)
    {
        RiderData *rd = (RiderData *)riders[HSD_Randi(count)]->userdata;
        if (rd)
        {
            cx = rd->pos.X;
            cz = rd->pos.Z;
        }
    }

    *ax = cx + Weather_Randf2() * BOLT_SPREAD;
    *az = cz + Weather_Randf2() * BOLT_SPREAD;
}

static void SetLightColor(LOBJ *l, u8 r, u8 g, u8 b)
{
    if (!l)
        return;
    l->color.r = r;
    l->color.g = g;
    l->color.b = b;
    l->color.a = 0xFF;
    l->hw_color = l->color;
}

static void SetLightColorScaled(LOBJ *l, GXColor c, float s)
{
    SetLightColor(l, (u8)(c.r * s), (u8)(c.g * s), (u8)(c.b * s));
}

static void EnsureFlashLight(void)
{
    if (s_flash_lobj)
        return;
    GOBJ *gobj = GObj_Create(LTNG_LOBJ_GOBJ_CLASS, LTNG_LOBJ_GOBJ_PLINK, 0);
    if (!gobj)
        return;
    s_flash_lobj = LObj_LoadDesc(&s_flash_lobj_desc);
    GObj_AddObject(gobj, HSD_OBJKIND_LOBJ, s_flash_lobj);
    GObj_AddGXLink(gobj, LObj_GX, 0, 0);
}

static void EnsureBoltLight(void)
{
    if (s_bolt_lobj)
        return;
    GOBJ *gobj = GObj_Create(LTNG_LOBJ_GOBJ_CLASS, LTNG_LOBJ_GOBJ_PLINK, 0);
    if (!gobj)
        return;
    s_bolt_lobj = LObj_LoadDesc(&s_bolt_lobj_desc);
    GObj_AddObject(gobj, HSD_OBJKIND_LOBJ, s_bolt_lobj);
    GObj_AddGXLink(gobj, LObj_GX, 0, 0);
}

// Build a fresh jagged bolt at a random anchor and move the midpoint point light
// onto it. Called once when a strike fires.
static void GenerateBolt(void)
{
    float x, z;
    StrikeAnchor(&x, &z);
    float y = BOLT_TOP_Y;
    float dy = (BOLT_TOP_Y - BOLT_GROUND_Y) / (float)BOLT_SEGMENTS;

    Vec3 prev = { x, y, z };
    Vec3 fork_origin = prev;
    int mid = BOLT_SEGMENTS / 2;
    int fork_at = BOLT_SEGMENTS / 3;
    int n = 0;

    for (int i = 1; i <= BOLT_SEGMENTS; i++)
    {
        y -= dy;
        x += Weather_Randf2() * BOLT_JITTER;
        z += Weather_Randf2() * BOLT_JITTER;
        Vec3 cur = { x, y, z };
        s_seg_a[n] = prev;
        s_seg_b[n] = cur;
        n++;
        if (i == mid)
            s_bolt_mid = cur;
        if (i == fork_at)
            fork_origin = cur;
        prev = cur;
    }

    // One offshoot branching from the upper third, veering sideways as it falls.
    float fx = fork_origin.X, fy = fork_origin.Y, fz = fork_origin.Z;
    float fdy = dy * 1.1f;
    Vec3 fprev = fork_origin;
    for (int i = 0; i < BOLT_FORK_SEGS && n < BOLT_MAX_SEG; i++)
    {
        fy -= fdy;
        fx += Weather_Randf2() * BOLT_FORK_JITTER + 25.0f;
        fz += Weather_Randf2() * BOLT_FORK_JITTER;
        Vec3 fcur = { fx, fy, fz };
        s_seg_a[n] = fprev;
        s_seg_b[n] = fcur;
        n++;
        fprev = fcur;
    }
    s_seg_count = n;

    if (s_bolt_lobj && s_bolt_lobj->position)
        s_bolt_lobj->position->pos = s_bolt_mid;
}

// Draw the bolt as GX line segments: flat per-vertex color, additive blend so the
// core glows, depth-tested but not depth-writing so stage geometry occludes it.
static void DrawBoltPass(COBJ *cam, GXColor col, int width, u8 alpha)
{
    WeatherGX_BeginXlu(cam, 1, width); // additive

    GXBegin(GX_LINES, GX_VTXFMT0, s_seg_count * 2);
    for (int i = 0; i < s_seg_count; i++)
    {
        GXPosition3f32(s_seg_a[i].X, s_seg_a[i].Y, s_seg_a[i].Z);
        GXColor4u8(col.r, col.g, col.b, alpha);
        GXPosition3f32(s_seg_b[i].X, s_seg_b[i].Y, s_seg_b[i].Z);
        GXColor4u8(col.r, col.g, col.b, alpha);
    }
    HSD_StateInvalidate(-1);
}

static const GXColor s_bolt_core = {255, 255, 255, 255}; // white-hot core

static void Bolt_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != 1)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    if (!stc_active || s_flash_frames <= 0 || s_seg_count <= 0)
        return;
    if (EffectiveBoltMode() == LTNG_BOLT_OFF)
        return;

    float bright = FlashBrightness();
    if (bright <= 0.0f)
        return;

    // Wide dim glow, then a thin white-hot core, both strobing with the flash.
    DrawBoltPass(cam, stc_bolt_color, BOLT_GLOW_WIDTH, (u8)(stc_bolt_color.a * BOLT_GLOW_ALPHA * bright));
    DrawBoltPass(cam, s_bolt_core, BOLT_CORE_WIDTH, (u8)(255.0f * bright));
}

static void EnsureBoltRender(void)
{
    if (s_bolt_render)
        return;
    s_bolt_render = WeatherGX_EnsureLayer(BOLT_GOBJ_CLASS, BOLT_GOBJ_PLINK, Bolt_GX,
                                          BOLT_GX_LINK, BOLT_GX_PRI,
                                          "[Lightning] Bolt render layer installed");
}

// Latch the active preset's lightning config, resolving each 0 field to its module
// default and re-arming the strike timers.
void Lightning_SetActive(const LightningDef *def)
{
    if (!def || !def->enabled)
    {
        stc_active = 0;
        return;
    }
    stc_active = 1;

    stc_flash_color = GXColor_Unpack(def->flash_color ? def->flash_color : LTNG_DEF_FLASH_COLOR);
    // The bolt glow inherits the preset's flash color; the core is always white-hot.
    stc_bolt_color = GXColor_Unpack(def->flash_color ? def->flash_color : BOLT_DEF_COLOR);
    stc_flash_len = def->flash_frames > 0 ? def->flash_frames : LTNG_DEF_FLASH_FRAMES;
    stc_min_lull = def->min_lull > 0 ? def->min_lull : LTNG_DEF_MIN_LULL;
    stc_max_lull = def->max_lull > 0 ? def->max_lull : LTNG_DEF_MAX_LULL;
    if (stc_max_lull < stc_min_lull)
        stc_max_lull = stc_min_lull;
    stc_preset_bolt = def->bolt;

    s_lull_frames = LTNG_INITIAL_LULL;
    s_flash_frames = 0;
}

// Lerp the per-frame fog/EFB color toward the flash color by `bright` and pull the
// fog wall in so the brightness reaches near terrain. This is what lights the
// LOBJ-blind stage geometry on a strike.
static void ApplyScreenFlash(HSD_Fog *fog, float bright)
{
    u8 fr = stc_flash_color.r, fg = stc_flash_color.g, fb = stc_flash_color.b;

    if (fog)
    {
        u8 br = fog->color.r, bg = fog->color.g, bb = fog->color.b;
        fog->color.r = (u8)(br + (int)((fr - br) * bright));
        fog->color.g = (u8)(bg + (int)((fg - bg) * bright));
        fog->color.b = (u8)(bb + (int)((fb - bb) * bright));
        fog->start = fog->start * (1.0f - bright);
    }

    u32 efb_base = *stc_global_fog_color;
    u8 br = (efb_base >> 24) & 0xFF;
    u8 bg = (efb_base >> 16) & 0xFF;
    u8 bb = (efb_base >> 8) & 0xFF;
    u8 ba = efb_base & 0xFF;
    u8 nr = (u8)(br + (int)((fr - br) * bright));
    u8 ng = (u8)(bg + (int)((fg - bg) * bright));
    u8 nb = (u8)(bb + (int)((fb - bb) * bright));
    *stc_global_fog_color = RGBA(nr, ng, nb, ba);
}

// Per-frame strike driver. `fog` is the active HSD_Fog, already written this frame
// by Sky_Update, lerped toward the flash color during a strike.
void Lightning_Tick(HSD_Fog *fog)
{
    if (!stc_active)
        return;
    EnsureFlashLight();
    EnsureBoltLight();
    EnsureBoltRender();

    int mode = EffectiveBoltMode();

    if (s_flash_frames > 0)
    {
        float bright = FlashBrightness();

        // Screen flash + global rider light, unless a bolt fully replaces them.
        if (mode != LTNG_BOLT_REPLACE)
        {
            ApplyScreenFlash(fog, bright);
            SetLightColorScaled(s_flash_lobj, stc_flash_color, bright);
        }
        else
        {
            SetLightColor(s_flash_lobj, 0, 0, 0);
        }

        // Localized point light at the bolt midpoint pulses with the bolt.
        if (mode != LTNG_BOLT_OFF)
            SetLightColorScaled(s_bolt_lobj, stc_bolt_color, bright);
        else
            SetLightColor(s_bolt_lobj, 0, 0, 0);

        s_flash_frames--;
    }
    else
    {
        SetLightColor(s_flash_lobj, 0, 0, 0);
        SetLightColor(s_bolt_lobj, 0, 0, 0);
        s_lull_frames--;
        if (s_lull_frames <= 0)
        {
            float lenscale = LTNG_LEN_MIN_SCALE + HSD_Randf() * (LTNG_LEN_MAX_SCALE - LTNG_LEN_MIN_SCALE);
            s_strike_len = (int)(stc_flash_len * lenscale);
            if (s_strike_len < 1)
                s_strike_len = 1;
            s_strike_intensity = LTNG_INTENSITY_MIN + HSD_Randf() * (1.0f - LTNG_INTENSITY_MIN);
            s_strike_on = LTNG_STROBE_ON_MIN + HSD_Randi(LTNG_STROBE_ON_MAX - LTNG_STROBE_ON_MIN + 1);
            s_strike_gap = LTNG_STROBE_GAP_MIN + HSD_Randi(LTNG_STROBE_GAP_MAX - LTNG_STROBE_GAP_MIN + 1);
            s_strike_floor = LTNG_STROBE_FLOOR_MIN + HSD_Randf() * (LTNG_STROBE_FLOOR_MAX - LTNG_STROBE_FLOOR_MIN);
            s_flash_frames = s_strike_len;

            int span = stc_max_lull - stc_min_lull;
            s_lull_frames = stc_min_lull + (span > 0 ? HSD_Randi(span) : 0);
            GenerateBolt();
            OSReport("[Lightning] Strike (len %d, %d%% peak, %don/%doff, next in %d frames)\n",
                     s_strike_len, (int)(s_strike_intensity * 100.0f),
                     s_strike_on, s_strike_gap, s_lull_frames);
        }
    }
}

void Lightning_Reset(void)
{
    // The engine frees every world GObj on scene teardown; drop the cached handles
    // so the next active frame recreates them.
    s_flash_lobj = 0;
    s_bolt_lobj = 0;
    s_bolt_render = 0;
    s_seg_count = 0;
    s_lull_frames = LTNG_INITIAL_LULL;
    s_flash_frames = 0;
    stc_active = 0;
}

MenuDesc lightning_menu = {
    .option_num = 1,
    .options = {
        &(OptionDesc){
            .name = "Lightning Bolts",
            .description = "Visible bolts in storms: Auto = per-preset, Off = flash only, Force = bolts on every lightning preset",
            .kind = OPTKIND_VALUE,
            .val = &bolt_override_index,
            .value_num = 3,
            .value_names = bolt_override_names,
        },
    },
};
