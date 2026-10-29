// World-space snow for custom_weather: a camera-following field of soft round
// flakes that fall slowly, flutter sideways, and drift with the wind.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "obj.h"
#include "gx.h"
#include "inline.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

#define SNOW_MAX        1000       // pool capacity; per-preset density clamps to this

// Defaults applied when a preset leaves the corresponding SnowDef field 0.
static const GXColor snow_def_color = {245, 248, 255, 230};  // soft white
#define SNOW_DEF_DENSITY     600
#define SNOW_DEF_FALL_SPEED  3.0f                       // world units/frame, downward
#define SNOW_DEF_FLUTTER     1.6f                       // sideways sway amplitude
#define SNOW_DEF_SIZE        4.0f                       // flake radius in world units

// Each flake sways along its own horizontal direction with an independent phase
// and speed, so the field shimmers instead of moving in lockstep.
#define SNOW_FLUTTER_FREQ_MIN  0.04f   // rad/frame
#define SNOW_FLUTTER_FREQ_MAX  0.11f
#define SNOW_SIZE_VAR          0.5f    // +/- fractional per-flake size spread

typedef struct Flake
{
    float size;     // multiplier on the base radius
    float phase;    // flutter phase offset
    float freq;     // flutter angular speed (rad/frame)
    float sway_x;   // unit horizontal sway direction
    float sway_z;
} Flake;

static GOBJ *stc_snow_gobj = NULL;

static int     stc_active = 0;
static float   stc_time = 0.0f;   // flutter clock, advanced each tick

static GXColor stc_color;
static int     stc_density;
static float   stc_fall;
static float   stc_flutter;
static float   stc_base_size;

// Seeded the first time snow falls in a round, for the full pool so any density up
// to the cap is valid.
static WeatherBox stc_box;
static Flake     *stc_flakes = NULL;

// Index 0 ("Preset") is the pass-through value on every knob below.
static const float intensity_factors[] = {1.0f, 0.0f, 0.5f, 1.5f};
static char *intensity_names[] = {"Preset", "Off", "Light", "Heavy"};
WEATHER_KNOB_CHECK(intensity_factors, intensity_names);
static int intensity_index = 0;

static const float fall_factors[] = {1.0f, 0.6f, 1.5f};
static char *fall_names[] = {"Preset", "Slow", "Fast"};
WEATHER_KNOB_CHECK(fall_factors, fall_names);
static int fall_index = 0;

static const float flutter_factors[] = {1.0f, 0.0f, 2.0f};
static char *flutter_names[] = {"Preset", "None", "Lively"};
WEATHER_KNOB_CHECK(flutter_factors, flutter_names);
static int flutter_index = 0;

static int snow_wind_slant = 1;

static void SeedFlakes(void)
{
    WeatherBox_Seed(&stc_box, SNOW_MAX);
    stc_flakes = HSD_MemAlloc(SNOW_MAX * sizeof(Flake));
    for (int i = 0; i < SNOW_MAX; i++)
    {
        Flake *f = &stc_flakes[i];
        f->size = 1.0f + Weather_Randf2() * SNOW_SIZE_VAR;
        f->phase = RandomAngle();
        f->freq = RandomInRangef(SNOW_FLUTTER_FREQ_MIN, SNOW_FLUTTER_FREQ_MAX);
        float az = RandomAngle();
        f->sway_x = cosf(az);
        f->sway_z = sinf(az);
    }
}

// GX callback on the world camera link, XLU pass. Each flake is a camera-facing
// soft dot, alpha-blended so it reads white over the world rather than glowing.
static void Snow_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != GXPASS_TEXEDGE)
        return;
    if (!stc_active || stc_density <= 0)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    Vec3 rightW, upW;
    COBJ_GetViewAxes(cam, &rightW, &upW);
    Vec3 eye;
    COBJ_GetViewEye(cam, &eye);

    float flutter = stc_flutter * flutter_factors[flutter_index];

    GX_BeginXlu(cam, RENDER_VERTEX, GX_BL_INVSRCALPHA);

    for (int i = 0; i < stc_density; i++)
    {
        Flake *f = &stc_flakes[i];
        Vec3 P;
        WeatherBox_Point(&stc_box, i, &eye, &P);
        float s = flutter * sinf(stc_time * f->freq + f->phase);
        P.X += s * f->sway_x;
        P.Z += s * f->sway_z;
        WeatherGX_SoftDot(&P, &rightW, &upW, stc_base_size * f->size, stc_color, stc_color.a);
    }

    HSD_StateInvalidate(HSD_STATE_ALL);
}

void Snow_SetActive(const SnowDef *snow)
{
    float intensity = intensity_factors[intensity_index];
    if (!snow || !snow->enabled || intensity <= 0.0f)
    {
        stc_active = 0;
        return;
    }
    stc_active = 1;

    stc_color = snow->color ? GXColor_Unpack(snow->color) : snow_def_color;
    stc_density = Weather_ScaleCount(snow->density ? snow->density : SNOW_DEF_DENSITY,
                                     intensity, SNOW_MAX);
    stc_fall = (snow->fall_speed > 0.0f ? snow->fall_speed : SNOW_DEF_FALL_SPEED)
               * fall_factors[fall_index];
    stc_flutter = snow->flutter > 0.0f ? snow->flutter : SNOW_DEF_FLUTTER;
    stc_base_size = snow->size > 0.0f ? snow->size : SNOW_DEF_SIZE;
}

void Snow_Tick(void)
{
    if (!stc_active)
        return;
    if (!stc_flakes)
        SeedFlakes();
    WeatherGX_EnsureLayer(&stc_snow_gobj, Snow_GX, "Snow");
    stc_time += 1.0f;

    // Read the wind fresh each frame so gusts carry the field.
    Vec3 wind = {0.0f, 0.0f, 0.0f};
    if (snow_wind_slant)
        Wind_GetVector(&wind);
    WeatherBox_Step(&stc_box, wind.X, -stc_fall, wind.Z);
}

void Snow_Reset(void)
{
    stc_snow_gobj = NULL;
    stc_active = 0;
    stc_time = 0.0f;
    stc_box.offset = NULL;
    stc_flakes = NULL;
}

MenuDesc snow_menu = {
    .option_num = 4,
    .options = {
        &(OptionDesc){
            .name = "Snow Intensity",
            .description = "Scales the flake count on presets with snow (Off disables snow entirely)",
            .kind = OPTKIND_VALUE,
            .val = &intensity_index,
            .value_num = GetElementsIn(intensity_factors),
            .value_names = intensity_names,
        },
        &(OptionDesc){
            .name = "Fall Speed",
            .description = "How fast the snow falls",
            .kind = OPTKIND_VALUE,
            .val = &fall_index,
            .value_num = GetElementsIn(fall_factors),
            .value_names = fall_names,
        },
        &(OptionDesc){
            .name = "Flutter",
            .description = "How much each flake sways sideways as it falls (None = straight down)",
            .kind = OPTKIND_VALUE,
            .val = &flutter_index,
            .value_num = GetElementsIn(flutter_factors),
            .value_names = flutter_names,
        },
        &(OptionDesc){
            .name = "Wind Slant",
            .description = "Let the global wind carry the snow (Off = falls straight down)",
            .kind = OPTKIND_VALUE,
            .val = &snow_wind_slant,
            .value_num = WEATHER_ONOFF_NUM,
            .value_names = weather_onoff_names,
        },
    },
};
