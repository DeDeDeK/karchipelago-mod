// World-space snow for custom_weather: a camera-following field of soft round
// flakes that fall slowly, flutter sideways, and drift with the wind.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "obj.h"
#include "gx.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

#define SNOW_PI  3.14159265358979f

#define SNOW_MAX        1000       // pool capacity; per-preset density clamps to this
#define SNOW_BOX        1000.0f    // edge of the camera-following volume (cube)
#define SNOW_BOX_HALF   (SNOW_BOX * 0.5f)
#define SNOW_SEGS       6          // rim vertices of each soft flake (coarse circle)

// Defaults applied when a preset leaves the corresponding SnowDef field 0.
#define SNOW_DEF_COLOR       RGBA(245, 248, 255, 230)  // soft white
#define SNOW_DEF_DENSITY     600
#define SNOW_DEF_FALL_SPEED  3.0f                       // world units/frame, downward
#define SNOW_DEF_FLUTTER     1.6f                       // sideways sway amplitude
#define SNOW_DEF_SIZE        4.0f                       // flake radius in world units

// Each flake sways along its own horizontal direction with an independent phase
// and speed, so the field shimmers instead of moving in lockstep.
#define SNOW_TW_SPEED_MIN  0.04f   // rad/frame
#define SNOW_TW_SPEED_MAX  0.11f
#define SNOW_SIZE_VAR      0.5f    // +/- fractional per-flake size spread

// Entity class / p_link high enough to avoid the engine's own.
#define SNOW_GOBJ_CLASS  209
#define SNOW_GOBJ_PLINK  34

typedef struct Flake
{
    float size;     // world radius
    float phase;    // flutter phase offset
    float freq;     // flutter angular speed (rad/frame)
    float sway_x;   // unit horizontal sway direction
    float sway_z;
} Flake;

// Cached only to avoid recreating the GObj every frame; never dereferenced.
static GOBJ *stc_snow_gobj = NULL;

static int     stc_active = 0;
static float   stc_time = 0.0f;   // flutter clock, advanced each tick

static GXColor stc_color = {245, 248, 255, 230};
static int     stc_density = SNOW_DEF_DENSITY;
static float   stc_fall = SNOW_DEF_FALL_SPEED;
static float   stc_flutter = SNOW_DEF_FLUTTER;
static float   stc_base_size = SNOW_DEF_SIZE;

// Shared fall+wind drift (world units), advanced once per frame and wrapped into
// [0, SNOW_BOX); vel_y is negative (downward), vel_x/z track the wind.
static float   stc_vel_x = 0.0f, stc_vel_y = -SNOW_DEF_FALL_SPEED, stc_vel_z = 0.0f;
static Vec3    stc_drift = {0.0f, 0.0f, 0.0f};

// Per-flake fixed offsets in [0, SNOW_BOX) per axis and flutter params, seeded once
// for the full pool so any per-preset density up to the cap is valid.
static Vec3    stc_offset[SNOW_MAX];
static Flake   stc_flakes[SNOW_MAX];
static int     stc_seeded = 0;

// Index 0 ("Preset") is the pass-through value on every knob below.
static const float intensity_factors[] = {1.0f, 0.0f, 0.5f, 1.0f, 1.5f};
static char *intensity_names[] = {"Preset", "Off", "Light", "Normal", "Heavy"};
#define SNOW_INTENSITY_NUM (sizeof(intensity_factors) / sizeof(intensity_factors[0]))
static int intensity_index = 0;

static const float fall_factors[] = {1.0f, 0.6f, 1.0f, 1.5f};
static char *fall_names[] = {"Preset", "Slow", "Normal", "Fast"};
#define SNOW_FALL_NUM (sizeof(fall_factors) / sizeof(fall_factors[0]))
static int fall_index = 0;

static const float flutter_factors[] = {1.0f, 0.0f, 1.0f, 2.0f};
static char *flutter_names[] = {"Preset", "None", "Gentle", "Lively"};
#define SNOW_FLUTTER_NUM (sizeof(flutter_factors) / sizeof(flutter_factors[0]))
static int flutter_index = 0;

static int wind_slant_index = 1;

static float SnowIntensity(void)
{
    return intensity_factors[intensity_index];
}

static void SeedField(void)
{
    if (stc_seeded)
        return;
    for (int i = 0; i < SNOW_MAX; i++)
    {
        stc_offset[i].X = HSD_Randf() * SNOW_BOX;
        stc_offset[i].Y = HSD_Randf() * SNOW_BOX;
        stc_offset[i].Z = HSD_Randf() * SNOW_BOX;

        Flake *f = &stc_flakes[i];
        float scale = 1.0f + Weather_Randf2() * SNOW_SIZE_VAR;
        if (scale < 0.3f)
            scale = 0.3f;
        f->size = scale;   // base size folded in at draw time
        f->phase = HSD_Randf() * 2.0f * SNOW_PI;
        f->freq = SNOW_TW_SPEED_MIN + HSD_Randf() * (SNOW_TW_SPEED_MAX - SNOW_TW_SPEED_MIN);
        float az = HSD_Randf() * 2.0f * SNOW_PI;
        f->sway_x = cosf(az);
        f->sway_z = sinf(az);
    }
    stc_seeded = 1;
}

// GX callback on the world camera link, XLU pass. Each flake is a camera-facing
// soft dot, alpha-blended so it reads white over the world rather than glowing.
static void Snow_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != 1)
        return;
    if (!stc_active || stc_density <= 0)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    // Rows 0/1 of the world->view rotation are the billboard basis.
    float (*m)[4] = cam->view_mtx;
    Vec3 rightW = {m[0][0], m[0][1], m[0][2]};
    Vec3 upW = {m[1][0], m[1][1], m[1][2]};
    Vec3 eye;
    WeatherGX_CameraEye(cam, &eye);

    float flutter = stc_flutter * flutter_factors[flutter_index];

    WeatherGX_BeginXlu(cam, 0, 0);

    for (int i = 0; i < stc_density; i++)
    {
        float tx = stc_offset[i].X + stc_drift.X;
        if (tx >= SNOW_BOX)
            tx -= SNOW_BOX;
        float ty = stc_offset[i].Y + stc_drift.Y;
        if (ty >= SNOW_BOX)
            ty -= SNOW_BOX;
        float tz = stc_offset[i].Z + stc_drift.Z;
        if (tz >= SNOW_BOX)
            tz -= SNOW_BOX;

        Flake *f = &stc_flakes[i];
        float s = flutter * sinf(stc_time * f->freq + f->phase);
        Vec3 P = {
            eye.X + tx - SNOW_BOX_HALF + s * f->sway_x,
            eye.Y + ty - SNOW_BOX_HALF,
            eye.Z + tz - SNOW_BOX_HALF + s * f->sway_z,
        };
        float r = stc_base_size * f->size;

        GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, SNOW_SEGS + 2);
        WeatherGX_BillboardVert(&P, &rightW, &upW, 0.0f, 0.0f,
                                stc_color.r, stc_color.g, stc_color.b, stc_color.a);
        for (int sgm = 0; sgm <= SNOW_SEGS; sgm++)
        {
            float ang = 2.0f * SNOW_PI * (float)sgm / (float)SNOW_SEGS;
            WeatherGX_BillboardVert(&P, &rightW, &upW, cosf(ang) * r, sinf(ang) * r,
                                    stc_color.r, stc_color.g, stc_color.b, 0);
        }
    }

    HSD_StateInvalidate(-1);
}

static void Snow_Ensure(void)
{
    if (stc_snow_gobj)
        return;
    stc_snow_gobj = WeatherGX_EnsureLayer(SNOW_GOBJ_CLASS, SNOW_GOBJ_PLINK, Snow_GX, "Snow");
}

void Snow_SetActive(const SnowDef *snow)
{
    float intensity = SnowIntensity();
    if (!snow || !snow->enabled || intensity <= 0.0f)
    {
        stc_active = 0;
        return;
    }
    stc_active = 1;

    stc_color = GXColor_Unpack(snow->color ? snow->color : SNOW_DEF_COLOR);

    int base_density = snow->density ? snow->density : SNOW_DEF_DENSITY;
    stc_density = (int)(base_density * intensity);
    if (stc_density > SNOW_MAX)
        stc_density = SNOW_MAX;
    if (stc_density < 0)
        stc_density = 0;

    stc_fall = (snow->fall_speed > 0.0f ? snow->fall_speed : SNOW_DEF_FALL_SPEED)
               * fall_factors[fall_index];
    stc_flutter = snow->flutter > 0.0f ? snow->flutter : SNOW_DEF_FLUTTER;
    stc_base_size = snow->size > 0.0f ? snow->size : SNOW_DEF_SIZE;
}

void Snow_Tick(void)
{
    if (!stc_active)
        return;
    SeedField();
    Snow_Ensure();
    stc_time += 1.0f;

    // Read the wind fresh each frame so gusts carry the field.
    if (wind_slant_index)
    {
        Vec3 wind;
        Wind_GetVector(&wind);
        stc_vel_x = wind.X;
        stc_vel_z = wind.Z;
    }
    else
    {
        stc_vel_x = 0.0f;
        stc_vel_z = 0.0f;
    }
    stc_vel_y = -stc_fall;

    // The per-flake wrap in Snow_GX recycles any flake that leaves the box.
    stc_drift.X = Weather_WrapStep(stc_drift.X, stc_vel_x, SNOW_BOX);
    stc_drift.Y = Weather_WrapStep(stc_drift.Y, stc_vel_y, SNOW_BOX);
    stc_drift.Z = Weather_WrapStep(stc_drift.Z, stc_vel_z, SNOW_BOX);
}

void Snow_Reset(void)
{
    stc_snow_gobj = NULL;
    stc_active = 0;
    stc_time = 0.0f;
    stc_drift.X = stc_drift.Y = stc_drift.Z = 0.0f;
}

MenuDesc snow_menu = {
    .option_num = 4,
    .options = {
        &(OptionDesc){
            .name = "Snow Intensity",
            .description = "Master snow amount over every CT preset, scaling its flake count (Off disables snow entirely)",
            .kind = OPTKIND_VALUE,
            .val = &intensity_index,
            .value_num = SNOW_INTENSITY_NUM,
            .value_names = intensity_names,
        },
        &(OptionDesc){
            .name = "Fall Speed",
            .description = "How fast the snow falls across every CT preset",
            .kind = OPTKIND_VALUE,
            .val = &fall_index,
            .value_num = SNOW_FALL_NUM,
            .value_names = fall_names,
        },
        &(OptionDesc){
            .name = "Flutter",
            .description = "How much each flake sways sideways as it falls (None = straight down)",
            .kind = OPTKIND_VALUE,
            .val = &flutter_index,
            .value_num = SNOW_FLUTTER_NUM,
            .value_names = flutter_names,
        },
        &(OptionDesc){
            .name = "Wind Slant",
            .description = "Let the global wind carry the snow (Off = falls straight down)",
            .kind = OPTKIND_VALUE,
            .val = &wind_slant_index,
            .value_num = 2,
            .value_names = weather_onoff_names,
        },
    },
};
