// World-space rain for custom_weather: a camera-following, depth-tested field of
// translucent GX line streaks drawn in the City Trial world.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "obj.h"
#include "gx.h"
#include "inline.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

#define RAIN_MAX_DROPS   1600      // pool capacity; per-preset density clamps to this

// Defaults applied when a preset leaves the corresponding RainDef field 0. Density
// is the performance lever: every drop is re-emitted once per camera.
static const GXColor rain_def_color = {165, 180, 215, 120};  // pale blue-gray streaks
#define RAIN_DEF_DENSITY     900
#define RAIN_DEF_FALL_SPEED  26.0f                     // world units/frame, downward
#define RAIN_DEF_LINE_WIDTH  10                        // 1/6-pixel units (~1.7px)
#define RAIN_DEF_STREAK      1.5f

static GOBJ *stc_rain_gobj = NULL;

static int     stc_active = 0;

// Seeded the first time rain falls in a round, for the full pool so any density up
// to the cap has offsets.
static WeatherBox stc_box;

static GXColor stc_color;
static int     stc_density;
static float   stc_fall;
static float   stc_vel_x, stc_vel_z;  // the wind slant, refreshed every tick
static int     stc_line_width;
static float   stc_streak;

// Index 0 ("Preset") is the pass-through value on every knob below.
static const float rain_intensity_factors[] = {1.0f, 0.0f, 0.5f, 1.5f, 2.0f};
static char *rain_intensity_names[] = {"Preset", "Off", "Light", "Heavy", "Downpour"};
WEATHER_KNOB_CHECK(rain_intensity_factors, rain_intensity_names);
static int rain_intensity_index = 0;

// Scales the fall velocity only; streak length tracks velocity, so faster rain
// also streaks longer.
static const float rain_fall_factors[] = {1.0f, 0.6f, 1.5f};
static char *rain_fall_names[] = {"Preset", "Slow", "Fast"};
WEATHER_KNOB_CHECK(rain_fall_factors, rain_fall_names);
static int rain_fall_index = 0;

static int rain_wind_slant = 1;

int Rain_IsActive(void)
{
    return stc_active;
}

// GX callback on the world camera link, XLU pass.
static void Rain_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != GXPASS_TEXEDGE)
        return;
    if (!stc_active || stc_density <= 0)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    Vec3 eye;
    COBJ_GetViewEye(cam, &eye);

    float sx = stc_vel_x * stc_streak;
    float sy = -stc_fall * stc_streak;
    float sz = stc_vel_z * stc_streak;

    GX_BeginXlu(cam, RENDER_VERTEX, GX_BL_INVSRCALPHA);
    GXSetLineWidth((u8)stc_line_width, GX_TO_ONE);

    GXBegin(GX_LINES, GX_VTXFMT0, stc_density * 2);
    for (int i = 0; i < stc_density; i++)
    {
        Vec3 p;
        WeatherBox_Point(&stc_box, i, &eye, &p);
        GXPosition3f32(p.X, p.Y, p.Z);
        GXColor4u8(stc_color.r, stc_color.g, stc_color.b, stc_color.a);
        GXPosition3f32(p.X + sx, p.Y + sy, p.Z + sz);
        GXColor4u8(stc_color.r, stc_color.g, stc_color.b, stc_color.a);
    }
    HSD_StateInvalidate(HSD_STATE_ALL);
}

void Rain_SetActive(const RainDef *rain)
{
    float intensity = rain_intensity_factors[rain_intensity_index];
    if (!rain || !rain->enabled || intensity <= 0.0f)
    {
        stc_active = 0;
        return;
    }
    stc_active = 1;

    stc_color = rain->color ? GXColor_Unpack(rain->color) : rain_def_color;
    stc_density = Weather_ScaleCount(rain->density ? rain->density : RAIN_DEF_DENSITY,
                                     intensity, RAIN_MAX_DROPS);
    stc_fall = (rain->fall_speed > 0.0f ? rain->fall_speed : RAIN_DEF_FALL_SPEED)
               * rain_fall_factors[rain_fall_index];
    stc_line_width = rain->line_width ? rain->line_width : RAIN_DEF_LINE_WIDTH;
    stc_streak = rain->streak > 0.0f ? rain->streak : RAIN_DEF_STREAK;
}

void Rain_Tick(void)
{
    if (!stc_active)
        return;
    if (!stc_box.offset)
        WeatherBox_Seed(&stc_box, RAIN_MAX_DROPS);
    WeatherGX_EnsureLayer(&stc_rain_gobj, Rain_GX, "Rain");

    // Read the wind fresh each frame so gusts visibly bend the rain.
    Vec3 wind = {0.0f, 0.0f, 0.0f};
    if (rain_wind_slant)
        Wind_GetVector(&wind);
    stc_vel_x = wind.X;
    stc_vel_z = wind.Z;

    WeatherBox_Step(&stc_box, stc_vel_x, -stc_fall, stc_vel_z);
}

void Rain_Reset(void)
{
    stc_rain_gobj = NULL;
    stc_active = 0;
    stc_box.offset = NULL;
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
            .description = "Scales the drop count on presets with rain (Off disables rain, and hail with it)",
            .kind = OPTKIND_VALUE,
            .val = &rain_intensity_index,
            .value_num = GetElementsIn(rain_intensity_factors),
            .value_names = rain_intensity_names,
            .on_change = OnRainIntensityChange,
        },
        &(OptionDesc){
            .name = "Fall Speed",
            .description = "How fast the rain falls and streaks",
            .kind = OPTKIND_VALUE,
            .val = &rain_fall_index,
            .value_num = GetElementsIn(rain_fall_factors),
            .value_names = rain_fall_names,
        },
        &(OptionDesc){
            .name = "Wind Slant",
            .description = "Let the global wind bend the rain (Off = rain falls straight down)",
            .kind = OPTKIND_VALUE,
            .val = &rain_wind_slant,
            .value_num = WEATHER_ONOFF_NUM,
            .value_names = weather_onoff_names,
        },
        &hail_option,
    },
};
