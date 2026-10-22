// Starfield for custom_weather: faint twinkling dots scattered over the City Trial
// sky dome, plus occasional shooting stars.

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

#define STAR_MAX   220  // field capacity; resolved density clamps to this

// Each star sits at P = eye + skydir*dist, so panning sweeps a world-fixed field
// with no parallax swim.
#define STAR_MAX_DIST   6000.0f  // desired anchor distance (always clamped smaller)
#define STAR_FAR_FRAC   0.9f     // never exceed this fraction of the camera far plane
#define STAR_DOME_FRAC  0.9f     // keep stars within this fraction of the dome distance

// Only seed stars above this elevation so they read as sky, not horizon haze.
#define STAR_MIN_ELEV_DEG  10.0f

// Per-star base brightness floor: some stars are dim, none fully dark.
#define STAR_BRIGHT_MIN  0.35f

// Twinkle: brightness is multiplied by 1 + depth*sin(t*speed + phase), where `t`
// advances one unit per tick.
#define STAR_TW_DEPTH      0.7f
#define STAR_TW_SPEED_MIN  0.05f  // rad/frame (~2s shimmer)
#define STAR_TW_SPEED_MAX  0.14f  // rad/frame (~0.75s shimmer)

// Defaults applied when a preset leaves the matching StarDef field 0; the color
// alpha is the base brightness before luminosity/twinkle.
static const GXColor star_def_color = {228, 234, 248, 170};
#define STAR_DEF_DENSITY   120
#define STAR_DEF_TWINKLE   0.5f
#define STAR_DEF_LUM       1.0f
#define STAR_DEF_SIZE      5.5f
#define STAR_DEF_SIZE_VAR  0.5f

// Shooting stars: fast meteors that streak a short sky arc with a fading trail,
// spawned on a random lull and drawn along with the starfield.
#define SHOOT_MAX          4     // concurrent meteor pool
#define SHOOT_TRAIL_SEGS   8     // trail line-strip segments behind the head
#define SHOOT_LINE_WIDTH   12    // trail width in 1/6-pixel units (~2 px)
#define SHOOT_ARC_MIN      0.4f  // shortest sky arc a meteor crosses (radians)
#define SHOOT_ARC_MAX      1.0f
#define SHOOT_LIFE_MIN     26    // frames alive (fast: ~0.4..0.8s)
#define SHOOT_LIFE_MAX     46
#define SHOOT_TRAIL_SPAN   0.15f // trail length as a fraction of the path behind the head
#define SHOOT_HEAD_SIZE    9.0f  // head glow radius at the reference distance
#define SHOOT_BRIGHT       235   // additive peak brightness of the head
#define SHOOT_FADE_IN      4     // brighten-in frames
#define SHOOT_FADE_OUT     12    // fade-out frames

typedef struct Star
{
    Vec3  dir;       // world sky direction (celestial), seeded once
    float size;      // world radius at WEATHER_DOME_REF_DIST
    float bright;    // per-star base brightness, 0..1
    float tw_phase;  // twinkle phase offset
    float tw_speed;  // twinkle angular speed (rad/frame)
} Star;

typedef struct Shoot
{
    int   active;
    int   age;    // frames since spawn
    int   life;   // total lifetime in frames
    Vec3  d0;     // path start direction (unit)
    Vec3  t;      // path tangent (unit, perp to d0); head(p) = d0*cos(arc*p) + t*sin(arc*p)
    float arc;    // angular length of the path (radians)
} Shoot;

static GOBJ *stc_star_gobj = NULL;

static int   stc_active = 0;
static int   stc_inited = 0;
static float stc_time = 0.0f;   // twinkle clock, advanced each tick

static Star stc_stars[STAR_MAX];
static int  stc_count = 0;

static Shoot stc_shoot[SHOOT_MAX];
static int   stc_shoot_timer = 0;                     // frames until the next meteor
static int   stc_shoot_level = SHOOT_FREQ_OCCASIONAL; // active preset's cadence

// Resolved per-preset config (StarDef + defaults), before the menu scalars.
static GXColor stc_color;
static int     stc_base_density;
static float   stc_twinkle;
static float   stc_lum;
static float   stc_base_size;
static float   stc_size_var;

// Index 0 ("Preset") is the pass-through value on every starfield knob.
static int   show_index = 0;

static const float density_factors[] = {1.0f, 0.5f, 1.7f};
static char *density_names[] = {"Preset", "Sparse", "Dense"};
WEATHER_KNOB_CHECK(density_factors, density_names);
static int density_index = 0;

static const float twinkle_factors[] = {1.0f, 0.0f, 2.0f};
static char *twinkle_names[] = {"Preset", "None", "Lively"};
WEATHER_KNOB_CHECK(twinkle_factors, twinkle_names);
static int twinkle_index = 0;

static const float lum_factors[] = {1.0f, 0.6f, 1.4f};
static char *lum_names[] = {"Preset", "Dim", "Bright"};
WEATHER_KNOB_CHECK(lum_factors, lum_names);
static int lum_index = 0;

// Master scalar over the preset's per-star size spread (resolved var clamped to 1).
static const float variance_factors[] = {1.0f, 0.2f, 1.8f};
static char *variance_names[] = {"Preset", "Uniform", "Varied"};
WEATHER_KNOB_CHECK(variance_factors, variance_names);
static int variance_index = 0;

// Index 0 keeps the per-preset RGB; the rest force an RGB, leaving the base
// brightness from the preset color's alpha.
static const u32 color_overrides[] = {0, RGBA(255, 255, 255, 255), RGBA(255, 240, 214, 255),
                                      RGBA(210, 224, 255, 255)};
static char *color_names[] = {"Preset", "White", "Warm", "Cool"};
WEATHER_KNOB_CHECK(color_overrides, color_names);
static int color_index = 0;

// Random lull range (frames) between meteors, indexed by ShootFreq. Default and Off
// are resolved before a lull is drawn, so their slots are never read.
static const int shoot_lull_min[] = {0, 0, 1200, 600, 240};
static const int shoot_lull_max[] = {0, 0, 3000, 1500, 600};
static char *shoot_names[] = {"Preset", "Off", "Rare", "Occasional", "Frequent"};
_Static_assert(GetElementsIn(shoot_names) == SHOOT_FREQ_NUM, "shoot_names must cover ShootFreq");
WEATHER_KNOB_CHECK(shoot_lull_min, shoot_names);
WEATHER_KNOB_CHECK(shoot_lull_max, shoot_names);
static int shoot_index = 0;

// Effective cadence; Preset (0) resolves to the preset's latched level.
static int ShootLevel(void)
{
    return (shoot_index == SHOOT_FREQ_DEFAULT) ? stc_shoot_level : shoot_index;
}

// The meteor knobs have no per-preset field behind them, so none has a Preset value.
static const float shoot_size_factors[] = {0.65f, 1.0f, 1.5f};
static char *shoot_size_names[] = {"Small", "Normal", "Large"};
WEATHER_KNOB_CHECK(shoot_size_factors, shoot_size_names);
static int shoot_size_index = 1;

// Life multiplier: a slower meteor lives longer, crossing its arc more slowly.
static const float shoot_speed_factors[] = {1.6f, 1.0f, 0.6f};
static char *shoot_speed_names[] = {"Slow", "Normal", "Fast"};
WEATHER_KNOB_CHECK(shoot_speed_factors, shoot_speed_names);
static int shoot_speed_index = 1;

// Additive peak-brightness multiplier over SHOOT_BRIGHT.
static const float shoot_bright_factors[] = {0.6f, 1.0f, 1.5f};
static char *shoot_bright_names[] = {"Dim", "Normal", "Bright"};
WEATHER_KNOB_CHECK(shoot_bright_factors, shoot_bright_names);
static int shoot_bright_index = 1;

// Index 0 follows the resolved starfield color; the rest force an RGB.
static const u32 shoot_color_overrides[] = {0, RGBA(255, 255, 255, 255), RGBA(255, 236, 200, 255),
                                            RGBA(200, 224, 255, 255)};
static char *shoot_color_names[] = {"Star", "White", "Warm", "Cool"};
WEATHER_KNOB_CHECK(shoot_color_overrides, shoot_color_names);
static int shoot_color_index = 0;

// Seed one star: a direction uniformly over the sky cap above the min elevation,
// a size scaled by the resolved variance, and a random twinkle.
static void SeedStar(Star *s, float sin_min, float var)
{
    float z = RandomInRangef(sin_min, 1.0f);
    float rh = 1.0f - z * z;
    rh = (rh > 0.0f) ? sqrtf(rh) : 0.0f;
    float az = RandomAngle();
    s->dir.X = rh * cosf(az);
    s->dir.Y = z;
    s->dir.Z = rh * sinf(az);

    s->size = stc_base_size * Weather_RandScale(var, 0.3f);
    s->bright = RandomInRangef(STAR_BRIGHT_MIN, 1.0f);
    s->tw_phase = RandomAngle();
    s->tw_speed = RandomInRangef(STAR_TW_SPEED_MIN, STAR_TW_SPEED_MAX);
}

// No stage dependency, so this always succeeds.
static void Star_Arm(void)
{
    float var = stc_size_var * variance_factors[variance_index];
    if (var > 1.0f)
        var = 1.0f;

    stc_count = Weather_ScaleCount(stc_base_density, density_factors[density_index], STAR_MAX);

    float sin_min = sinf(MTXDegToRad(STAR_MIN_ELEV_DEG));
    for (int i = 0; i < stc_count; i++)
        SeedStar(&stc_stars[i], sin_min, var);

    stc_inited = 1;
    OSReport("[Stars] Armed %d stars\n", stc_count);
}

static float PlaceOnDome(const Vec3 *dir, const Vec3 *eye, float e2, float maxd, Vec3 *P)
{
    return WeatherGX_PlaceOnDome(dir, eye, e2, STAR_MAX_DIST, STAR_DOME_FRAC, maxd, P);
}

// The unit direction `p` of the way along a meteor's great-circle path.
static void ShootDir(const Shoot *sh, float p, Vec3 *out)
{
    float a = sh->arc * p;
    float ca = cosf(a), sa = sinf(a);
    out->X = sh->d0.X * ca + sh->t.X * sa;
    out->Y = sh->d0.Y * ca + sh->t.Y * sa;
    out->Z = sh->d0.Z * ca + sh->t.Z * sa;
}

// GX callback on the world camera link, XLU pass. Stars and meteors draw additively
// and fog-free so the distant dots are not washed to the fog color.
static void Star_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != GXPASS_TEXEDGE)
        return;
    if (!stc_active || stc_count <= 0)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    Vec3 rightW, upW;
    COBJ_GetViewAxes(cam, &rightW, &upW);

    Vec3 eye;
    COBJ_GetViewEye(cam, &eye);
    float e2 = VECSquareMag(&eye);
    float maxd = cam->far * STAR_FAR_FRAC;

    float tw = stc_twinkle * twinkle_factors[twinkle_index];
    if (tw > 1.0f)
        tw = 1.0f;
    float lum = stc_lum * lum_factors[lum_index];

    WeatherGX_BeginSky(cam, GX_BL_ONE); // additive: dots glow, never darken the sky

    for (int i = 0; i < stc_count; i++)
    {
        Star *s = &stc_stars[i];

        float f = 1.0f + tw * STAR_TW_DEPTH * sinf(stc_time * s->tw_speed + s->tw_phase);
        float a = (float)stc_color.a * lum * s->bright * f;
        if (a <= 1.0f)
            continue;
        if (a > 255.0f)
            a = 255.0f;

        Vec3 P;
        float dist = PlaceOnDome(&s->dir, &eye, e2, maxd, &P);
        WeatherGX_SoftDot(&P, &rightW, &upW, s->size * (dist / WEATHER_DOME_REF_DIST),
                          stc_color, (u8)a);
    }

    // Meteors: the trail samples the great-circle path just behind the head and
    // fades to transparent at the tail.
    if (ShootLevel() != SHOOT_FREQ_OFF)
    {
        GXColor sc = stc_color;
        Weather_OverrideRGB(&sc, shoot_color_overrides[shoot_color_index]);
        float peak = (float)SHOOT_BRIGHT * shoot_bright_factors[shoot_bright_index];
        if (peak > 255.0f)
            peak = 255.0f;
        float head_size = SHOOT_HEAD_SIZE * shoot_size_factors[shoot_size_index];
        int lw = (int)(SHOOT_LINE_WIDTH * shoot_size_factors[shoot_size_index]);
        if (lw < 1)
            lw = 1;
        GXSetLineWidth((u8)lw, GX_TO_ONE);

        for (int i = 0; i < SHOOT_MAX; i++)
        {
            Shoot *sh = &stc_shoot[i];
            if (!sh->active)
                continue;

            float p = (float)sh->age / (float)sh->life;
            float env;
            if (sh->age < SHOOT_FADE_IN)
                env = (float)sh->age / (float)SHOOT_FADE_IN;
            else if (sh->age > sh->life - SHOOT_FADE_OUT)
                env = (float)(sh->life - sh->age) / (float)SHOOT_FADE_OUT;
            else
                env = 1.0f;
            if (env <= 0.0f)
                continue;

            GXBegin(GX_LINESTRIP, GX_VTXFMT0, SHOOT_TRAIL_SEGS + 1);
            for (int seg = 0; seg <= SHOOT_TRAIL_SEGS; seg++)
            {
                float f = (float)seg / (float)SHOOT_TRAIL_SEGS; // 0 tail .. 1 head
                float pp = p - SHOOT_TRAIL_SPAN * (1.0f - f);
                if (pp < 0.0f)
                    pp = 0.0f;
                Vec3 d, TP;
                ShootDir(sh, pp, &d);
                PlaceOnDome(&d, &eye, e2, maxd, &TP);
                GXPosition3f32(TP.X, TP.Y, TP.Z);
                GXColor4u8(sc.r, sc.g, sc.b, (u8)(peak * env * f));
            }

            Vec3 hd, HP;
            ShootDir(sh, p, &hd);
            float hdist = PlaceOnDome(&hd, &eye, e2, maxd, &HP);
            WeatherGX_SoftDot(&HP, &rightW, &upW, head_size * (hdist / WEATHER_DOME_REF_DIST),
                              sc, (u8)(peak * env));
        }
    }

    WeatherGX_EndSky();
}

static int RandLull(void)
{
    int lvl = ShootLevel();
    if (lvl == SHOOT_FREQ_OFF) // the seed delay of a field that never launches
        lvl = SHOOT_FREQ_OCCASIONAL;
    return RandomInRange(shoot_lull_min[lvl], shoot_lull_max[lvl]);
}

// Launch a meteor into a free pool slot: a start direction high in the sky and a
// downward-biased tangent, over a random-length arc.
static void Shoot_Spawn(void)
{
    Shoot *sh = NULL;
    for (int i = 0; i < SHOOT_MAX; i++)
    {
        if (!stc_shoot[i].active)
        {
            sh = &stc_shoot[i];
            break;
        }
    }
    if (!sh)
        return; // pool full; skip this launch

    float el = MTXDegToRad(RandomInRangef(25.0f, 75.0f));
    float az = RandomAngle();
    float ce = cosf(el), se = sinf(el);
    Vec3 d0 = {ce * sinf(az), se, ce * cosf(az)};

    // Orthonormal tangent basis to d0 (e1 horizontal, e2 completing it), combined at
    // a random roll, then flipped downward so meteors descend.
    Vec3 e1, e2;
    VEC_CrossNormalizeSnap(&d0, &(Vec3){0.0f, 1.0f, 0.0f}, &e1);
    VECCrossProduct(&d0, &e1, &e2);
    float roll = RandomAngle();
    float cr = cosf(roll), sr = sinf(roll);
    Vec3 t = {e1.X * cr + e2.X * sr, e1.Y * cr + e2.Y * sr, e1.Z * cr + e2.Z * sr};
    if (t.Y > 0.0f)
    {
        t.X = -t.X; t.Y = -t.Y; t.Z = -t.Z;
    }

    sh->d0 = d0;
    sh->t = t;
    sh->arc = RandomInRangef(SHOOT_ARC_MIN, SHOOT_ARC_MAX);
    int base_life = RandomInRange(SHOOT_LIFE_MIN, SHOOT_LIFE_MAX);
    sh->life = (int)(base_life * shoot_speed_factors[shoot_speed_index]);
    if (sh->life < 1)
        sh->life = 1;
    sh->age = 0;
    sh->active = 1;
}

// Clear the meteor pool and re-seed the spawn timer.
static void Shoot_Reset(void)
{
    for (int i = 0; i < SHOOT_MAX; i++)
        stc_shoot[i].active = 0;
    stc_shoot_timer = RandLull();
}

static void Shoot_Tick(void)
{
    for (int i = 0; i < SHOOT_MAX; i++)
    {
        Shoot *sh = &stc_shoot[i];
        if (!sh->active)
            continue;
        sh->age++;
        if (sh->age >= sh->life)
            sh->active = 0;
    }
    if (ShootLevel() == SHOOT_FREQ_OFF)
        return;
    if (stc_shoot_timer > 0)
    {
        stc_shoot_timer--;
        return;
    }
    Shoot_Spawn();
    stc_shoot_timer = RandLull();
}

void Star_SetActive(const StarDef *def)
{
    if (!WeatherToggle(show_index, def && def->enabled))
    {
        stc_active = 0;
        Shoot_Reset();
        return;
    }
    stc_active = 1;

    stc_color = (def && def->color) ? GXColor_Unpack(def->color) : star_def_color;
    Weather_OverrideRGB(&stc_color, color_overrides[color_index]);

    stc_base_density = (def && def->density > 0) ? def->density : STAR_DEF_DENSITY;
    stc_twinkle = (def && def->twinkle > 0.0f) ? def->twinkle : STAR_DEF_TWINKLE;
    stc_lum = (def && def->luminosity > 0.0f) ? def->luminosity : STAR_DEF_LUM;
    stc_base_size = (def && def->size > 0.0f) ? def->size : STAR_DEF_SIZE;
    stc_size_var = (def && def->size_var > 0.0f) ? def->size_var : STAR_DEF_SIZE_VAR;

    // A preset's ShootFreq maps 1:1 onto the menu cadence levels; Default (0)
    // leaves the built-in Occasional cadence.
    stc_shoot_level = (def && def->shoot) ? def->shoot : SHOOT_FREQ_OCCASIONAL;

    stc_inited = 0;
    stc_count = 0;
    Shoot_Reset();
}

void Star_Tick(void)
{
    if (!stc_active)
        return;
    if (!stc_inited)
        Star_Arm();
    WeatherGX_EnsureLayer(&stc_star_gobj, Star_GX, "Stars");
    stc_time += 1.0f;
    Shoot_Tick();
}

void Star_Reset(void)
{
    stc_star_gobj = NULL;
    stc_inited = 0;
    stc_count = 0;
    stc_active = 0;
    stc_time = 0.0f;
    Shoot_Reset();
}

static MenuDesc shooting_menu = {
    .option_num = 5,
    .options = {
        &(OptionDesc){
            .name = "Frequency",
            .description = "How often meteors streak across the sky (Preset = each preset's own cadence, Off disables them)",
            .kind = OPTKIND_VALUE,
            .val = &shoot_index,
            .value_num = GetElementsIn(shoot_names),
            .value_names = shoot_names,
        },
        &(OptionDesc){
            .name = "Size",
            .description = "How large the meteor heads and trails are",
            .kind = OPTKIND_VALUE,
            .val = &shoot_size_index,
            .value_num = GetElementsIn(shoot_size_factors),
            .value_names = shoot_size_names,
        },
        &(OptionDesc){
            .name = "Speed",
            .description = "How fast the meteors streak across the sky",
            .kind = OPTKIND_VALUE,
            .val = &shoot_speed_index,
            .value_num = GetElementsIn(shoot_speed_factors),
            .value_names = shoot_speed_names,
        },
        &(OptionDesc){
            .name = "Brightness",
            .description = "How bright the meteors glow",
            .kind = OPTKIND_VALUE,
            .val = &shoot_bright_index,
            .value_num = GetElementsIn(shoot_bright_factors),
            .value_names = shoot_bright_names,
        },
        &(OptionDesc){
            .name = "Color",
            .description = "Meteor tint (Star = follow the starfield color)",
            .kind = OPTKIND_VALUE,
            .val = &shoot_color_index,
            .value_num = GetElementsIn(shoot_color_overrides),
            .value_names = shoot_color_names,
        },
    },
};

MenuDesc stars_menu = {
    .option_num = 7,
    .options = {
        &(OptionDesc){
            .name = "Stars",
            .description = "Show stars: Preset = only presets that set them, Off = never, On = every CT preset",
            .kind = OPTKIND_VALUE,
            .val = &show_index,
            .value_num = WEATHER_TOGGLE_NUM,
            .value_names = weather_toggle_names,
        },
        &(OptionDesc){
            .name = "Density",
            .description = "Scales the star count on presets with stars",
            .kind = OPTKIND_VALUE,
            .val = &density_index,
            .value_num = GetElementsIn(density_factors),
            .value_names = density_names,
        },
        &(OptionDesc){
            .name = "Twinkle",
            .description = "How much the stars shimmer in brightness (None = steady)",
            .kind = OPTKIND_VALUE,
            .val = &twinkle_index,
            .value_num = GetElementsIn(twinkle_factors),
            .value_names = twinkle_names,
        },
        &(OptionDesc){
            .name = "Luminosity",
            .description = "How bright the stars glow",
            .kind = OPTKIND_VALUE,
            .val = &lum_index,
            .value_num = GetElementsIn(lum_factors),
            .value_names = lum_names,
        },
        &(OptionDesc){
            .name = "Size Variance",
            .description = "How much the stars vary in size (Uniform = even, Varied = mixed)",
            .kind = OPTKIND_VALUE,
            .val = &variance_index,
            .value_num = GetElementsIn(variance_factors),
            .value_names = variance_names,
        },
        &(OptionDesc){
            .name = "Color",
            .description = "Override the star tint across every preset (Preset = each preset's own)",
            .kind = OPTKIND_VALUE,
            .val = &color_index,
            .value_num = GetElementsIn(color_overrides),
            .value_names = color_names,
        },
        &(OptionDesc){
            .name = "Shooting Stars",
            .description = "Meteors that streak across the sky: frequency, size, speed, brightness, color",
            .kind = OPTKIND_MENU,
            .menu_ptr = &shooting_menu,
        },
    },
};
