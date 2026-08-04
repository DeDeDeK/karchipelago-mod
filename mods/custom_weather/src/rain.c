// World-space rain for custom_weather: a camera-following, depth-tested field of
// translucent GX line streaks drawn in the City Trial world.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "obj.h"
#include "gx.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

#define RAIN_MAX_DROPS   1600      // pool capacity; per-preset density clamps to this
#define RAIN_BOX         1000.0f   // edge of the camera-following volume (cube)
#define RAIN_BOX_HALF    (RAIN_BOX * 0.5f)

// Defaults applied when a preset leaves the corresponding RainDef field 0. Density
// is the performance lever: every drop is re-emitted once per camera.
#define RAIN_DEF_COLOR       RGBA(165, 180, 215, 120) // pale blue-gray streaks
#define RAIN_DEF_DENSITY     900
#define RAIN_DEF_FALL_SPEED  26.0f                     // world units/frame, downward
#define RAIN_DEF_LINE_WIDTH  10                        // 1/6-pixel units (~1.7px)
#define RAIN_DEF_STREAK      1.5f

// Overlay GObj: an entity class / p_link high enough to avoid the engine's own, on
// the world camera's gx_link 0, XLU sub-pass.
#define RAIN_GOBJ_CLASS  201
#define RAIN_GOBJ_PLINK  25
#define RAIN_GX_LINK     0
#define RAIN_GX_PRI      0

// Cached only to avoid recreating the GObj every frame; never dereferenced.
static GOBJ *stc_rain_gobj = NULL;

static int     stc_active = 0;

// Resolved appearance/motion for the active preset (RainDef + defaults applied).
// vel_y is negative (falling), vel_x/z are wind, streak scales the velocity into
// the drawn segment length.
static GXColor stc_color = {165, 180, 215, 120};
static int     stc_density = RAIN_DEF_DENSITY;
static float   stc_vel_x = 0.0f, stc_vel_y = -RAIN_DEF_FALL_SPEED, stc_vel_z = 0.0f;
static int     stc_line_width = RAIN_DEF_LINE_WIDTH;
static float   stc_streak = RAIN_DEF_STREAK;

// Menu knobs layered over the active preset. Intensity scales the preset's drop
// count; Off disables rain for every preset.
static const float rain_intensity_factors[] = {1.0f, 0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
static char *rain_intensity_names[] = {"Preset", "Off", "Light", "Normal", "Heavy", "Downpour"};
#define RAIN_INTENSITY_NUM (sizeof(rain_intensity_factors) / sizeof(rain_intensity_factors[0]))
static int rain_intensity_index = 0;

// Scales the per-preset fall velocity only (the wind slant is unaffected); streak
// length tracks velocity, so faster rain also streaks longer.
static const float rain_fall_factors[] = {1.0f, 0.6f, 1.0f, 1.5f};
static char *rain_fall_names[] = {"Preset", "Slow", "Normal", "Fast"};
#define RAIN_FALL_NUM (sizeof(rain_fall_factors) / sizeof(rain_fall_factors[0]))
static int rain_fall_index = 0;

// Preset follows the global wind vector.
static char *rain_toggle_names[] = {"Preset", "Off", "On"};
static int rain_wind_slant = 0;

static float RainIntensity(void)
{
    return rain_intensity_factors[rain_intensity_index];
}

// Whether rain is falling for the active preset (preset rain enabled and the
// master Rain Intensity not Off). Hail only falls while this is true.
int Rain_IsActive(void)
{
    return stc_active;
}

// Per-drop fixed offsets in [0, RAIN_BOX) on each axis, seeded once for the full
// pool so any per-preset density up to the cap has valid offsets.
static Vec3 stc_offset[RAIN_MAX_DROPS];
static int  stc_seeded = 0;

// Shared drift in [0, RAIN_BOX) per axis; advanced once per frame.
static Vec3 stc_drift = {0.0f, 0.0f, 0.0f};

static void SeedOffsets(void)
{
    if (stc_seeded)
        return;
    for (int i = 0; i < RAIN_MAX_DROPS; i++)
    {
        stc_offset[i].X = HSD_Randf() * RAIN_BOX;
        stc_offset[i].Y = HSD_Randf() * RAIN_BOX;
        stc_offset[i].Z = HSD_Randf() * RAIN_BOX;
    }
    stc_seeded = 1;
}

// Advance one drift axis by v, wrapping into [0, RAIN_BOX). |v| < RAIN_BOX, so a
// single add/subtract suffices.
static float WrapStep(float d, float v)
{
    d += v;
    if (d >= RAIN_BOX)
        d -= RAIN_BOX;
    else if (d < 0.0f)
        d += RAIN_BOX;
    return d;
}

// Clamp a per-frame velocity so |v| < RAIN_BOX, the bound the single-subtract wrap
// in WrapStep / Rain_GX relies on.
static float ClampSpeed(float v)
{
    float lim = RAIN_BOX - 1.0f;
    if (v > lim)
        return lim;
    if (v < -lim)
        return -lim;
    return v;
}

// Camera world position from its view matrix. The view matrix is the rigid
// world->view transform [R | t], so the eye is -R^T * t.
static void CameraEye(COBJ *c, Vec3 *out)
{
    float (*m)[4] = c->view_mtx;
    float tx = m[0][3], ty = m[1][3], tz = m[2][3];
    out->X = -(m[0][0] * tx + m[1][0] * ty + m[2][0] * tz);
    out->Y = -(m[0][1] * tx + m[1][1] * ty + m[2][1] * tz);
    out->Z = -(m[0][2] * tx + m[1][2] * ty + m[2][2] * tz);
}

// GX callback on the world camera link. Draws the rain field on the XLU pass
// (pass 1) so it blends over opaque world geometry; a no-op otherwise.
static void Rain_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != 1)
        return;
    if (!stc_active)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    Vec3 eye;
    CameraEye(cam, &eye);

    float sx = stc_vel_x * stc_streak;
    float sy = stc_vel_y * stc_streak;
    float sz = stc_vel_z * stc_streak;

    WeatherGX_BeginXlu(cam, 0, stc_line_width);

    GXBegin(GX_LINES, GX_VTXFMT0, stc_density * 2);
    for (int i = 0; i < stc_density; i++)
    {
        // World pos = eye + center(offset + drift): offset and drift are each in
        // [0, RAIN_BOX), so one subtract folds their sum back into that range,
        // then -HALF centers the box on the eye.
        float tx = stc_offset[i].X + stc_drift.X;
        if (tx >= RAIN_BOX)
            tx -= RAIN_BOX;
        float ty = stc_offset[i].Y + stc_drift.Y;
        if (ty >= RAIN_BOX)
            ty -= RAIN_BOX;
        float tz = stc_offset[i].Z + stc_drift.Z;
        if (tz >= RAIN_BOX)
            tz -= RAIN_BOX;

        float wx = eye.X + tx - RAIN_BOX_HALF;
        float wy = eye.Y + ty - RAIN_BOX_HALF;
        float wz = eye.Z + tz - RAIN_BOX_HALF;

        GXPosition3f32(wx, wy, wz);
        GXColor4u8(stc_color.r, stc_color.g, stc_color.b, stc_color.a);
        GXPosition3f32(wx + sx, wy + sy, wz + sz);
        GXColor4u8(stc_color.r, stc_color.g, stc_color.b, stc_color.a);
    }
    HSD_StateInvalidate(-1);
}

static void Rain_Ensure(void)
{
    if (stc_rain_gobj)
        return;
    stc_rain_gobj = WeatherGX_EnsureLayer(RAIN_GOBJ_CLASS, RAIN_GOBJ_PLINK, Rain_GX,
                                          RAIN_GX_LINK, RAIN_GX_PRI,
                                          "[Rain] World-space rain layer installed");
}

// Latch the active preset's rain config, resolving each 0 field to its module default.
void Rain_SetActive(const RainDef *rain)
{
    float intensity = RainIntensity();
    if (!rain || !rain->enabled || intensity <= 0.0f)
    {
        stc_active = 0;
        return;
    }
    stc_active = 1;

    stc_color = GXColor_Unpack(rain->color ? rain->color : RAIN_DEF_COLOR);

    int base_density = rain->density ? rain->density : RAIN_DEF_DENSITY;
    stc_density = (int)(base_density * intensity);
    if (stc_density > RAIN_MAX_DROPS)
        stc_density = RAIN_MAX_DROPS;
    if (stc_density < 0)
        stc_density = 0;

    float fall = (rain->fall_speed > 0.0f ? rain->fall_speed : RAIN_DEF_FALL_SPEED)
                 * rain_fall_factors[rain_fall_index];
    stc_vel_y = -ClampSpeed(fall);            // negative = downward
    // The horizontal slant is refreshed every frame in Rain_Tick, not latched here.

    stc_line_width = rain->line_width ? rain->line_width : RAIN_DEF_LINE_WIDTH;
    stc_streak = rain->streak > 0.0f ? rain->streak : RAIN_DEF_STREAK;
}

void Rain_Tick(void)
{
    if (!stc_active)
        return;
    SeedOffsets();
    Rain_Ensure();

    // The slant reads the global wind fresh each frame so gusts visibly bend the
    // rain; ClampSpeed keeps the drift within the single-subtract wrap bound.
    if (WeatherToggle(rain_wind_slant, 1))
    {
        Vec3 wind;
        Wind_GetVector(&wind);
        stc_vel_x = ClampSpeed(wind.X);
        stc_vel_z = ClampSpeed(wind.Z);
    }
    else
    {
        stc_vel_x = 0.0f;
        stc_vel_z = 0.0f;
    }

    // All drops share the drift, so they fall coherently; the per-drop wrap in
    // Rain_GX recycles any drop that leaves the box.
    stc_drift.X = WrapStep(stc_drift.X, stc_vel_x);
    stc_drift.Y = WrapStep(stc_drift.Y, stc_vel_y);
    stc_drift.Z = WrapStep(stc_drift.Z, stc_vel_z);
}

void Rain_Reset(void)
{
    // The engine frees every world GObj on scene teardown; drop the cached handle
    // so the next active frame recreates it.
    stc_rain_gobj = NULL;
    stc_active = 0;
}

// Surfaced in this submenu because hail only falls on an active rain layer.
extern OptionDesc hail_option;

MenuDesc rain_menu = {
    .option_num = 4,
    .options = {
        &(OptionDesc){
            .name = "Rain Intensity",
            .description = "Master rain amount over every CT preset, scaling its drop count (Off disables rain entirely)",
            .kind = OPTKIND_VALUE,
            .val = &rain_intensity_index,
            .value_num = RAIN_INTENSITY_NUM,
            .value_names = rain_intensity_names,
        },
        &(OptionDesc){
            .name = "Fall Speed",
            .description = "How fast the rain falls and streaks across every CT preset",
            .kind = OPTKIND_VALUE,
            .val = &rain_fall_index,
            .value_num = RAIN_FALL_NUM,
            .value_names = rain_fall_names,
        },
        &(OptionDesc){
            .name = "Wind Slant",
            .description = "Let the global wind bend the rain (Preset = follow wind, Off = rain falls straight down)",
            .kind = OPTKIND_VALUE,
            .val = &rain_wind_slant,
            .value_num = 3,
            .value_names = rain_toggle_names,
        },
        &hail_option,
    },
};
