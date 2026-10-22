// World lightning for custom_weather: a per-preset strike loop - a screen/EFB
// flash (and an opt-in jagged GX bolt) punctuating long random lulls.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "gx.h"
#include "inline.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

// Defaults applied when a preset leaves the matching LightningDef field 0.
static const GXColor ltng_def_flash_color = {255, 250, 240, 255}; // near-white strike
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
static const GXColor bolt_def_color = {210, 225, 255, 255}; // fallback blue-white
static const GXColor bolt_core_color = {255, 255, 255, 255}; // white-hot core
#define BOLT_GLOW_WIDTH   44             // wide dim glow pass (1/6-px units)
#define BOLT_CORE_WIDTH   14             // thin bright core pass
#define BOLT_GLOW_ALPHA   0.5f           // glow alpha relative to the core

// Omnidirectional point light at the bolt midpoint, with computed attenuation
// (ref_br at ref_dist) so the engine derives the GX coefficients.
#define BOLT_LIGHT_REF_DIST  520.0f
#define BOLT_LIGHT_REF_BR    0.5f

// Overhead INFINITE flash LOBJ (the global rider flash). For INFINITE LObjLoad allocates a WObj from `position` and ignores the union; the WObj
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
    .flags = LOBJ_INFINITE | LOBJ_DIFFUSE | LOBJ_SPECULAR,
    .attnflags = 0,
    .color = { 0, 0, 0, 0xFF },
    .position = &s_flash_pos_desc,
    .interest = 0,
    .u = { .p = 0 },
};

// POINT LOBJ at the bolt midpoint. For POINT LObjLoad loads
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
    .point_func = GX_SP_OFF,
    .ref_br = BOLT_LIGHT_REF_BR,
    .ref_dist = BOLT_LIGHT_REF_DIST,
    .dist_func = GX_DA_MEDIUM,
};
static LObjDesc s_bolt_lobj_desc = {
    .class_name = 0,
    .next = 0,
    .flags = LOBJ_POINT | LOBJ_DIFFUSE | LOBJ_SPECULAR,
    .attnflags = 0,        // computed (ref_br/ref_dist) attenuation, not raw
    .color = { 0, 0, 0, 0xFF },
    .position = &s_bolt_pos_desc,
    .interest = 0,
    .u = { .point = &s_bolt_point },
};

static const GXColor black = {0, 0, 0, 0xFF};

static LOBJ *s_flash_lobj = 0;
static LOBJ *s_bolt_lobj = 0;
static GOBJ *s_bolt_render = 0;

static int stc_active = 0;

// Resolved appearance/timing for the active preset (LightningDef + defaults).
static GXColor stc_flash_color;
static GXColor stc_bolt_color;
static int     stc_flash_len;
static int     stc_min_lull;
static int     stc_max_lull;
static int     stc_preset_bolt;  // this preset's LightningDef.bolt

// Strike state machine: counts down lull, then counts down a flash envelope.
static int s_lull_frames = LTNG_INITIAL_LULL;
static int s_flash_frames = 0;

// This frame's flash brightness, latched by the tick so the bolt draws in step.
static float s_bright = 0.0f;

// Per-strike appearance, rolled when a strike fires.
static int   s_strike_len;        // this strike's envelope length
static float s_strike_intensity;  // this strike's peak brightness
static int   s_strike_on;         // frames at full per strobe cycle
static int   s_strike_gap;        // frames at the dim floor per cycle
static float s_strike_floor;      // this strike's dim floor level

// Bolt geometry (world space), generated per strike. Segments are point pairs so
// the fork is just extra entries past the main channel.
static Vec3 s_seg_a[BOLT_MAX_SEG];
static Vec3 s_seg_b[BOLT_MAX_SEG];
static int  s_seg_count = 0;
static Vec3 s_bolt_mid = {0.0f, 0.0f, 0.0f};

// Preset / Off / On, where On adds bolts to every preset with lightning.
static int bolt_override_index = 0;

static int BoltsOn(void)
{
    return WeatherToggle(bolt_override_index, stc_preset_bolt);
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

// Build a fresh jagged bolt at a random point in the out-of-bounds box and move the
// midpoint point light onto it. Before the scene is built there is no box, and the
// strike flashes without a bolt.
static void GenerateBolt(void)
{
    StageNode *sn = Weather_StageNode();
    if (!sn)
    {
        s_seg_count = 0;
        return;
    }
    float x, z;
    Weather_RandBoxPoint(sn, 1.0f, &x, &z);
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

    if (s_bolt_lobj)
        LObj_SetPosition(s_bolt_lobj, &s_bolt_mid);
}

static void DrawBoltPass(COBJ *cam, GXColor col, int width, u8 alpha)
{
    GX_BeginXlu(cam, RENDER_VERTEX, GX_BL_ONE); // additive so the core glows
    GXSetLineWidth((u8)width, GX_TO_ONE);

    GXBegin(GX_LINES, GX_VTXFMT0, s_seg_count * 2);
    for (int i = 0; i < s_seg_count; i++)
    {
        GXPosition3f32(s_seg_a[i].X, s_seg_a[i].Y, s_seg_a[i].Z);
        GXColor4u8(col.r, col.g, col.b, alpha);
        GXPosition3f32(s_seg_b[i].X, s_seg_b[i].Y, s_seg_b[i].Z);
        GXColor4u8(col.r, col.g, col.b, alpha);
    }
    HSD_StateInvalidate(HSD_STATE_ALL);
}

static void Bolt_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != GXPASS_TEXEDGE)
        return;
    if (!stc_active || s_bright <= 0.0f || s_seg_count <= 0 || !BoltsOn())
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    // Wide dim glow, then a thin white-hot core, both strobing with the flash.
    DrawBoltPass(cam, stc_bolt_color, BOLT_GLOW_WIDTH,
                 (u8)(stc_bolt_color.a * BOLT_GLOW_ALPHA * s_bright));
    DrawBoltPass(cam, bolt_core_color, BOLT_CORE_WIDTH, (u8)(255.0f * s_bright));
}

// Also re-arms the strike timers.
void Lightning_SetActive(const LightningDef *def)
{
    if (!def || !def->enabled)
    {
        // Only Lightning_Tick clears the lights, and it early-outs while inactive,
        // so a preset change mid-flash would strand them lit.
        Weather_SetLightColor(s_flash_lobj, black);
        Weather_SetLightColor(s_bolt_lobj, black);
        s_flash_frames = 0;
        s_bright = 0.0f;
        stc_active = 0;
        return;
    }
    stc_active = 1;

    stc_flash_color = def->flash_color ? GXColor_Unpack(def->flash_color) : ltng_def_flash_color;
    // The bolt glow inherits the preset's flash color; the core is always white-hot.
    stc_bolt_color = def->flash_color ? GXColor_Unpack(def->flash_color) : bolt_def_color;
    stc_flash_len = def->flash_frames > 0 ? def->flash_frames : LTNG_DEF_FLASH_FRAMES;
    stc_min_lull = def->min_lull > 0 ? def->min_lull : LTNG_DEF_MIN_LULL;
    stc_max_lull = def->max_lull > 0 ? def->max_lull : LTNG_DEF_MAX_LULL;
    if (stc_max_lull < stc_min_lull)
        stc_max_lull = stc_min_lull;
    stc_preset_bolt = def->bolt;

    s_lull_frames = LTNG_INITIAL_LULL;
    s_flash_frames = 0;
    s_bright = 0.0f;
}

// This is what lights the LOBJ-blind stage geometry on a strike: lerp the fog/EFB
// color toward the flash and pull the fog wall in so it reaches near terrain.
static void ApplyScreenFlash(HSD_Fog *fog, float bright)
{
    GXColor flash = stc_flash_color;
    if (fog)
    {
        flash.a = fog->color.a;
        GXColor_Lerp(&fog->color, &flash, &fog->color, bright);
        fog->start = fog->start * (1.0f - bright);
    }

    flash.a = stc_global_fog_color->a;
    GXColor_Lerp(stc_global_fog_color, &flash, stc_global_fog_color, bright);
}

// Per-frame strike driver. `fog` is the active HSD_Fog, already written this frame
// by Sky_Update, lerped toward the flash color during a strike.
void Lightning_Tick(HSD_Fog *fog)
{
    if (!stc_active)
        return;
    if (!s_flash_lobj)
        s_flash_lobj = Weather_LoadLight(&s_flash_lobj_desc);
    int bolts = BoltsOn();
    if (bolts)
    {
        if (!s_bolt_lobj)
            s_bolt_lobj = Weather_LoadLight(&s_bolt_lobj_desc);
        WeatherGX_EnsureLayer(&s_bolt_render, Bolt_GX, "Lightning bolt");
    }

    if (s_flash_frames > 0)
    {
        s_bright = FlashBrightness();
        ApplyScreenFlash(fog, s_bright);
        Weather_SetLightColor(s_flash_lobj, Weather_ScaleRGB(stc_flash_color, s_bright));
        // The point light at the bolt midpoint pulses with the bolt.
        Weather_SetLightColor(s_bolt_lobj,
                              bolts ? Weather_ScaleRGB(stc_bolt_color, s_bright) : black);
        s_flash_frames--;
        return;
    }

    s_bright = 0.0f;
    Weather_SetLightColor(s_flash_lobj, black);
    Weather_SetLightColor(s_bolt_lobj, black);
    if (--s_lull_frames > 0)
        return;

    float lenscale = RandomInRangef(LTNG_LEN_MIN_SCALE, LTNG_LEN_MAX_SCALE);
    s_strike_len = (int)(stc_flash_len * lenscale);
    if (s_strike_len < 1)
        s_strike_len = 1;
    s_strike_intensity = RandomInRangef(LTNG_INTENSITY_MIN, 1.0f);
    s_strike_on = RandomInRange(LTNG_STROBE_ON_MIN, LTNG_STROBE_ON_MAX);
    s_strike_gap = RandomInRange(LTNG_STROBE_GAP_MIN, LTNG_STROBE_GAP_MAX);
    s_strike_floor = RandomInRangef(LTNG_STROBE_FLOOR_MIN, LTNG_STROBE_FLOOR_MAX);
    s_flash_frames = s_strike_len;

    s_lull_frames = RandomInRange(stc_min_lull, stc_max_lull);
    GenerateBolt();
}

void Lightning_Reset(void)
{
    s_flash_lobj = 0;
    s_bolt_lobj = 0;
    s_bolt_render = 0;
    s_seg_count = 0;
    s_lull_frames = LTNG_INITIAL_LULL;
    s_flash_frames = 0;
    s_bright = 0.0f;
    stc_active = 0;
}

MenuDesc lightning_menu = {
    .option_num = 1,
    .options = {
        &(OptionDesc){
            .name = "Lightning Bolts",
            .description = "Visible bolts in storms: Preset = per-preset, Off = flash only, On = bolts on every lightning preset",
            .kind = OPTKIND_VALUE,
            .val = &bolt_override_index,
            .value_num = WEATHER_TOGGLE_NUM,
            .value_names = weather_toggle_names,
        },
    },
};
