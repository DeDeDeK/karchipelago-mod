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

// Entity class / p_link high enough to avoid the engine's own.
#define RAIN_GOBJ_CLASS  201
#define RAIN_GOBJ_PLINK  25

// Cached only to avoid recreating the GObj every frame; never dereferenced.
static GOBJ *stc_rain_gobj = NULL;

static int     stc_active = 0;

// vel_y is negative (falling); vel_x/z are wind.
static GXColor stc_color = {165, 180, 215, 120};
static int     stc_density = RAIN_DEF_DENSITY;
static float   stc_vel_x = 0.0f, stc_vel_y = -RAIN_DEF_FALL_SPEED, stc_vel_z = 0.0f;
static int     stc_line_width = RAIN_DEF_LINE_WIDTH;
static float   stc_streak = RAIN_DEF_STREAK;

// Index 0 ("Preset") is the pass-through value on every knob below.
static const float rain_intensity_factors[] = {1.0f, 0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
static char *rain_intensity_names[] = {"Preset", "Off", "Light", "Normal", "Heavy", "Downpour"};
#define RAIN_INTENSITY_NUM (sizeof(rain_intensity_factors) / sizeof(rain_intensity_factors[0]))
static int rain_intensity_index = 0;

// Scales the fall velocity only; streak length tracks velocity, so faster rain
// also streaks longer.
static const float rain_fall_factors[] = {1.0f, 0.6f, 1.0f, 1.5f};
static char *rain_fall_names[] = {"Preset", "Slow", "Normal", "Fast"};
#define RAIN_FALL_NUM (sizeof(rain_fall_factors) / sizeof(rain_fall_factors[0]))
static int rain_fall_index = 0;

static int rain_wind_slant = 1;

static float RainIntensity(void)
{
    return rain_intensity_factors[rain_intensity_index];
}

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

// Clamp a per-frame velocity so |v| < RAIN_BOX, the bound the single-subtract wrap
// in Weather_WrapStep / Rain_GX relies on.
static float ClampSpeed(float v)
{
    float lim = RAIN_BOX - 1.0f;
    if (v > lim)
        return lim;
    if (v < -lim)
        return -lim;
    return v;
}

// GX callback on the world camera link, XLU pass.
static void Rain_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != 1)
        return;
    if (!stc_active || stc_density <= 0)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    Vec3 eye;
    WeatherGX_CameraEye(cam, &eye);

    float sx = stc_vel_x * stc_streak;
    float sy = stc_vel_y * stc_streak;
    float sz = stc_vel_z * stc_streak;

    WeatherGX_BeginXlu(cam, 0, stc_line_width);

    GXBegin(GX_LINES, GX_VTXFMT0, stc_density * 2);
    for (int i = 0; i < stc_density; i++)
    {
        // Offset and drift are each in [0, RAIN_BOX), so one subtract folds their
        // sum back into that range; -HALF then centers the box on the eye.
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
    stc_rain_gobj = WeatherGX_EnsureLayer(RAIN_GOBJ_CLASS, RAIN_GOBJ_PLINK, Rain_GX, "Rain");
}

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
    stc_vel_y = -ClampSpeed(fall);
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

    // Read the wind fresh each frame so gusts visibly bend the rain; ClampSpeed
    // keeps the drift within the single-subtract wrap bound.
    if (rain_wind_slant)
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
    stc_drift.X = Weather_WrapStep(stc_drift.X, stc_vel_x, RAIN_BOX);
    stc_drift.Y = Weather_WrapStep(stc_drift.Y, stc_vel_y, RAIN_BOX);
    stc_drift.Z = Weather_WrapStep(stc_drift.Z, stc_vel_z, RAIN_BOX);
}

void Rain_Reset(void)
{
    stc_rain_gobj = NULL;
    stc_active = 0;
    stc_drift.X = stc_drift.Y = stc_drift.Z = 0.0f;
}

static void OnRainIntensityChange(int val)
{
    OSReport("[Rain] Intensity %s\n", rain_intensity_names[val]);
}

// Surfaced in this submenu because hail only falls on an active rain layer.
extern OptionDesc hail_option;

MenuDesc rain_menu = {
    .option_num = 4,
    .options = {
        &(OptionDesc){
            .name = "Rain Intensity",
            .description = "Master rain amount over every CT preset, scaling its drop count (Off disables rain, and hail with it)",
            .kind = OPTKIND_VALUE,
            .val = &rain_intensity_index,
            .value_num = RAIN_INTENSITY_NUM,
            .value_names = rain_intensity_names,
            .on_change = OnRainIntensityChange,
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
            .description = "Let the global wind bend the rain (Off = rain falls straight down)",
            .kind = OPTKIND_VALUE,
            .val = &rain_wind_slant,
            .value_num = 2,
            .value_names = weather_onoff_names,
        },
        &hail_option,
    },
};
