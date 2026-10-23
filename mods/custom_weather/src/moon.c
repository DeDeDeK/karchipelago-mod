// Moon for custom_weather: a phased disc crossing the City Trial sky over the
// round, optionally casting a directional moonlight.

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

// Anchored along the sky direction from the camera eye, so there is no parallax
// swim and apparent elevation is consistent.
#define MOON_MAX_DIST   1800.0f  // desired anchor distance from the eye
#define MOON_FAR_FRAC   0.85f    // never exceed this fraction of the camera far plane
#define MOON_DOME_FRAC  0.82f    // keep the moon within this fraction of the dome distance

#define MOON_BANDS        28
#define MOON_COLS         6
#define MOON_CRATERS      13
#define MOON_CRATER_SEGS  9

// Radial fraction of the disc where the soft edge begins; alpha fades to 0 at the rim.
#define MOON_RIM_FADE     0.85f

// Defaults applied when a preset leaves the matching MoonDef field 0; the color
// alpha is the disc's opacity.
static const GXColor moon_def_color = {232, 234, 240, 255};
static const GXColor moon_def_light_color = {120, 140, 195, 255};
#define MOON_DEF_SIZE         92.0f   // disc radius in world units on the dome
#define MOON_DEF_ARC          26.0f   // peak elevation degrees over the round
#define MOON_DEF_BEARING      95.0f   // rise compass bearing (deg); ~east

#define MOON_CRATER_SHADE  0.58f  // crater RGB = disc RGB * this
#define MOON_CRATER_ALPHA  95     // crater opacity over the disc

static const GXColor black = {0, 0, 0, 0xFF};

static GOBJ *stc_moon_gobj = NULL;
static int   stc_active = 0;

// Resolved per-preset config (MoonDef + defaults), before the menu scalars.
static GXColor stc_color;
static float   stc_size;
static int     stc_phase;
static float   stc_arc;
static float   stc_bearing;
static int     stc_light;
static GXColor stc_light_color;

// Crater shape in unit-disc coords (X, Y in [-1,1]; Z = radius as a fraction of
// the disc radius), seeded once a round, and which of them fit the lit interior of
// the round's phase.
static Vec3 stc_crater[MOON_CRATERS];
static int  stc_crater_fits[MOON_CRATERS];
static int  stc_crater_seeded = 0;

// Index 0 ("Preset") is the pass-through value on every knob below.
static int   show_index = 0;

static const float size_factors[] = {1.0f, 0.7f, 1.4f};
static char *size_names[] = {"Preset", "Small", "Large"};
WEATHER_KNOB_CHECK(size_factors, size_names);
static int size_index = 0;

// Scales the disc and moonlight RGB; the disc's opacity stays the color's alpha.
static const float bright_factors[] = {1.0f, 0.6f, 1.3f};
static char *bright_names[] = {"Preset", "Dim", "Bright"};
WEATHER_KNOB_CHECK(bright_factors, bright_names);
static int bright_index = 0;

// Index 0 = Preset; 1.. map to the MoonPhase enum in order.
static char *phase_names[] = {"Preset", "Full", "Waxing Crescent", "First Quarter",
                              "Waxing Gibbous", "Waning Gibbous", "Last Quarter",
                              "Waning Crescent", "New"};
_Static_assert(GetElementsIn(phase_names) == MOON_PHASE_NUM + 1,
               "phase_names must carry Preset plus one name per MoonPhase");
static int phase_index = 0;

// Peak-elevation override in degrees.
static const float arc_degs[] = {0.0f, 15.0f, 26.0f, 42.0f, 62.0f};
static char *arc_names[] = {"Preset", "Low", "Mid", "High", "Overhead"};
WEATHER_KNOB_CHECK(arc_degs, arc_names);
static int arc_index = 0;

// Index 0 keeps the per-preset RGB; the rest force an RGB, keeping the preset's
// opacity.
static const u32 color_overrides[] = {0, RGBA(242, 244, 250, 255), RGBA(210, 214, 224, 255),
                                      RGBA(150, 175, 230, 255), RGBA(240, 214, 158, 255)};
static char *color_names[] = {"Preset", "White", "Silver", "Blue", "Amber"};
WEATHER_KNOB_CHECK(color_overrides, color_names);
static int color_index = 0;

static int   light_index = 0;

// Sky direction at the current progress: rises at `stc_bearing`, peaks at `stc_arc`
// elevation mid-round, sets at the opposite bearing. Elevation is 0 at both ends of
// the round and through the intro, where the moon and its light are hidden.
static void MoonDirection(Vec3 *out)
{
    float p = Weather_RoundProgress();
    if (p < 0.0f)
        p = 0.0f;
    float el = MTXDegToRad(stc_arc * sinf(p * M_PI));
    float az = MTXDegToRad(stc_bearing + 180.0f * p);
    float ce = cosf(el), se = sinf(el);
    out->X = ce * sinf(az);
    out->Y = se;
    out->Z = ce * cosf(az);
}

static int MoonPhaseNow(void)
{
    return (phase_index > 0) ? (phase_index - 1) : stc_phase;
}

// Phase -> terminator param k in [-1,1] (|k| = terminator ellipse half-width as
// a fraction of the disc) and lit side (+1 camera-right, -1 camera-left).
static void PhaseParams(int phase, float *k, int *side)
{
    switch (phase)
    {
    case MOON_WAXING_CRESCENT: *k = -0.5f; *side = +1; break;
    case MOON_FIRST_QUARTER:   *k =  0.0f; *side = +1; break;
    case MOON_WAXING_GIBBOUS:  *k =  0.5f; *side = +1; break;
    case MOON_WANING_GIBBOUS:  *k =  0.5f; *side = -1; break;
    case MOON_LAST_QUARTER:    *k =  0.0f; *side = -1; break;
    case MOON_WANING_CRESCENT: *k = -0.5f; *side = -1; break;
    case MOON_NEW:             *k = -1.0f; *side = +1; break;
    case MOON_FULL:
    default:                   *k =  1.0f; *side = +1; break;
    }
}

// Lit horizontal extent [uL,uR] at billboard height v, disc half-width
// w = sqrt(r^2 - v^2): lit-right spans [-k*w, w], lit-left spans [-w, k*w].
static void LitExtent(float v, float r, float k, int side, float *uL, float *uR)
{
    float w = r * r - v * v;
    w = (w > 0.0f) ? sqrtf(w) : 0.0f;
    if (side > 0)
    {
        *uL = -k * w;
        *uR = w;
    }
    else
    {
        *uL = -w;
        *uR = k * w;
    }
    if (*uR < *uL)
        *uR = *uL; // empty scanline (deep crescent): collapse to zero width
}

// Radial soft-edge factor: 1 in the interior, fading to 0 at the disc rim.
static float MoonRimFade(float u, float v, float r)
{
    float d = sqrtf(u * u + v * v) / r;
    if (d <= MOON_RIM_FADE)
        return 1.0f;
    float t = (d - MOON_RIM_FADE) / (1.0f - MOON_RIM_FADE);
    if (t > 1.0f)
        t = 1.0f;
    return 1.0f - t;
}

// Whether a crater circle sits entirely inside the opaque interior and on the lit
// side of the terminator, sampled around its rim, on the unit disc.
static int CraterFits(const Vec3 *c, float k, int side)
{
    if (sqrtf(c->X * c->X + c->Y * c->Y) + c->Z > MOON_RIM_FADE)
        return 0;
    for (int s = 0; s < MOON_CRATER_SEGS; s++)
    {
        float a = 2.0f * M_PI * (float)s / (float)MOON_CRATER_SEGS;
        float pu = c->X + cosf(a) * c->Z;
        float pv = c->Y + sinf(a) * c->Z;
        float uL, uR;
        LitExtent(pv, 1.0f, k, side, &uL, &uR);
        if (pu < uL || pu > uR)
            return 0;
    }
    return 1;
}

static void SeedCraters(void)
{
    if (stc_crater_seeded)
        return;
    for (int i = 0; i < MOON_CRATERS; i++)
    {
        float ang = RandomAngle();
        float rad = sqrtf(HSD_Randf()) * 0.62f; // uniform over the inner disc
        stc_crater[i].X = cosf(ang) * rad;
        stc_crater[i].Y = sinf(ang) * rad;
        stc_crater[i].Z = RandomInRangef(0.08f, 0.18f);
    }
    stc_crater_seeded = 1;
}

// GX callback on the world camera link, XLU pass. The disc draws fog-free so it is
// not washed to the fog color at this distance.
static void Moon_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != GXPASS_TEXEDGE)
        return;
    if (!stc_active)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    Vec3 dir;
    MoonDirection(&dir);
    if (dir.Y <= 0.0f) // on the horizon
        return;

    int phase = MoonPhaseNow();
    if (phase == MOON_NEW)
        return;
    float k;
    int side;
    PhaseParams(phase, &k, &side);

    Vec3 rightW, upW;
    COBJ_GetViewAxes(cam, &rightW, &upW);

    Vec3 eye;
    COBJ_GetViewEye(cam, &eye);
    float e2 = VECSquareMag(&eye);

    Vec3 P;
    float dist = WeatherGX_PlaceOnDome(&dir, &eye, e2, MOON_MAX_DIST, MOON_DOME_FRAC,
                                       cam->far * MOON_FAR_FRAC, &P);

    // Apparent size stays constant as the distance is clamped.
    float r = stc_size * size_factors[size_index] * (dist / WEATHER_DOME_REF_DIST);
    if (r <= 0.0f)
        return;

    GXColor dc = Weather_ScaleRGB(stc_color, bright_factors[bright_index]);

    WeatherGX_BeginSky(cam, GX_BL_INVSRCALPHA);

    // Scanline bands over the lit extent, each split into columns so the radial
    // soft-edge alpha blends the rim.
    for (int b = 0; b < MOON_BANDS; b++)
    {
        float v0 = -r + (2.0f * r) * (float)b / (float)MOON_BANDS;
        float v1 = -r + (2.0f * r) * (float)(b + 1) / (float)MOON_BANDS;
        float uL0, uR0, uL1, uR1;
        LitExtent(v0, r, k, side, &uL0, &uR0);
        LitExtent(v1, r, k, side, &uL1, &uR1);
        if (uR0 - uL0 <= 0.0f && uR1 - uL1 <= 0.0f)
            continue;

        GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, 2 * (MOON_COLS + 1));
        for (int c = 0; c <= MOON_COLS; c++)
        {
            float f = (float)c / (float)MOON_COLS;
            float u0 = lerp(uL0, uR0, f);
            float u1 = lerp(uL1, uR1, f);
            u8 a0 = (u8)(dc.a * MoonRimFade(u0, v0, r));
            u8 a1 = (u8)(dc.a * MoonRimFade(u1, v1, r));
            GX_BillboardVert(&P, &rightW, &upW, u0, v0, dc, a0);
            GX_BillboardVert(&P, &rightW, &upW, u1, v1, dc, a1);
        }
    }

    GXColor kc = Weather_ScaleRGB(dc, MOON_CRATER_SHADE);
    u8 ka = (dc.a < MOON_CRATER_ALPHA) ? dc.a : MOON_CRATER_ALPHA;
    for (int i = 0; i < MOON_CRATERS; i++)
    {
        if (!stc_crater_fits[i])
            continue;
        float cu = stc_crater[i].X * r;
        float cv = stc_crater[i].Y * r;
        float crad = stc_crater[i].Z * r;

        GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, MOON_CRATER_SEGS + 2);
        GX_BillboardVert(&P, &rightW, &upW, cu, cv, kc, ka);
        for (int s = 0; s <= MOON_CRATER_SEGS; s++)
        {
            float a = 2.0f * M_PI * (float)s / (float)MOON_CRATER_SEGS;
            GX_BillboardVert(&P, &rightW, &upW, cu + cosf(a) * crad,
                             cv + sinf(a) * crad, kc, ka);
        }
    }

    WeatherGX_EndSky();
}

// Directional (INFINITE) moonlight. An INFINITE LOBJ uses only its position vector
// as the direction origin, so it is pointed along the moon direction each frame.
static WOBJDesc s_moon_pos_desc = {
    .class_name = 0,
    .pos = {0.0f, 1500.0f, 0.0f}, // overwritten each frame
    .robjdesc = 0,
    .next = 0,
};
static LObjDesc s_moon_lobj_desc = {
    .class_name = 0,
    .next = 0,
    .flags = LOBJ_INFINITE | LOBJ_DIFFUSE | LOBJ_SPECULAR,
    .attnflags = 0,
    .color = {0, 0, 0, 0xFF},
    .position = &s_moon_pos_desc,
    .interest = 0,
    .u = {.p = 0},
};
static LOBJ *s_moon_lobj = 0;

// City Trial carries two distant INFINITE lights. *stc_main_light is left alone -
// the weather runtime's terrain tint owns it - so this module owns only the
// secondary, zeroing it while the moonlight is on and caching it for restore.
static LOBJ  *s_sup_lobj = 0;
static GXColor s_sup_color, s_sup_hw;

// The HW slot table lags a frame, so this may resolve nothing and retry.
static void Moon_SuppressStageLight(void)
{
    if (s_sup_lobj)
    {
        Weather_SetLightColor(s_sup_lobj, black);
        return;
    }
    LOBJ *primary = *stc_main_light;
    for (int s = 0; s < HSD_LOBJ_HW_SLOT_AMBIENT; s++)
    {
        LOBJ *l = stc_lobj_hw_slot_table[s];
        if (!l || l == primary || l == s_moon_lobj)
            continue;
        if ((l->flags & LOBJ_TYPE_MASK) != LOBJ_INFINITE)
            continue;
        s_sup_lobj = l;
        s_sup_color = l->color;
        s_sup_hw = l->hw_color;
        Weather_SetLightColor(l, black);
        break;
    }
}

static void Moon_RestoreStageLight(void)
{
    if (s_sup_lobj)
    {
        s_sup_lobj->color = s_sup_color;
        s_sup_lobj->hw_color = s_sup_hw;
    }
    s_sup_lobj = 0;
}

static void MoonLight_Tick(void)
{
    Vec3 dir;
    MoonDirection(&dir);

    // A new moon draws no disc and a moon on the horizon is hidden, so neither casts
    // light; the normal sun lights the scene.
    if (!WeatherToggle(light_index, stc_light) || MoonPhaseNow() == MOON_NEW || dir.Y <= 0.0f)
    {
        Weather_SetLightColor(s_moon_lobj, black);
        Moon_RestoreStageLight();
        return;
    }

    if (!s_moon_lobj)
        s_moon_lobj = Weather_LoadLight(&s_moon_lobj_desc);
    if (s_moon_lobj)
    {
        Vec3 lp = {dir.X * 1500.0f, dir.Y * 1500.0f, dir.Z * 1500.0f};
        LObj_SetPosition(s_moon_lobj, &lp);
        Weather_SetLightColor(s_moon_lobj,
                              Weather_ScaleRGB(stc_light_color, bright_factors[bright_index]));
    }
    Moon_SuppressStageLight();
}

void Moon_SetActive(const MoonDef *def)
{
    if (!WeatherToggle(show_index, def && def->enabled))
    {
        stc_active = 0;
        Weather_SetLightColor(s_moon_lobj, black);
        Moon_RestoreStageLight();
        return;
    }
    stc_active = 1;

    stc_color = (def && def->color) ? GXColor_Unpack(def->color) : moon_def_color;
    Weather_OverrideRGB(&stc_color, color_overrides[color_index]);

    stc_size = (def && def->size > 0.0f) ? def->size : MOON_DEF_SIZE;
    stc_phase = def ? def->phase : MOON_FULL;
    stc_arc = (def && def->arc_height > 0.0f) ? def->arc_height : MOON_DEF_ARC;
    if (arc_index > 0)
        stc_arc = arc_degs[arc_index];
    stc_bearing = (def && def->rise_bearing != 0.0f) ? def->rise_bearing : MOON_DEF_BEARING;
    stc_light = def ? def->light : 0;
    stc_light_color = (def && def->light_color) ? GXColor_Unpack(def->light_color)
                                                : moon_def_light_color;

    // The craters' fit depends only on the phase, which holds for the preset.
    SeedCraters();
    float k;
    int side;
    PhaseParams(MoonPhaseNow(), &k, &side);
    for (int i = 0; i < MOON_CRATERS; i++)
        stc_crater_fits[i] = CraterFits(&stc_crater[i], k, side);

    // Drop the old preset's suppressed light so the new one re-resolves it.
    Moon_RestoreStageLight();
}

void Moon_Tick(void)
{
    if (!stc_active)
        return;
    WeatherGX_EnsureLayer(&stc_moon_gobj, Moon_GX, "Moon");
    MoonLight_Tick();
}

void Moon_Reset(void)
{
    // The stage LOBJs are freed with the scene, so the suppressed light is dropped
    // rather than restored.
    stc_moon_gobj = NULL;
    s_moon_lobj = 0;
    s_sup_lobj = 0;
    stc_active = 0;
    stc_crater_seeded = 0;
}

MenuDesc moon_menu = {
    .option_num = 7,
    .options = {
        &(OptionDesc){
            .name = "Moon",
            .description = "Show the moon: Preset = only presets that set it, Off = never, On = every CT preset",
            .kind = OPTKIND_VALUE,
            .val = &show_index,
            .value_num = WEATHER_TOGGLE_NUM,
            .value_names = weather_toggle_names,
        },
        &(OptionDesc){
            .name = "Size",
            .description = "How large the moon disc is",
            .kind = OPTKIND_VALUE,
            .val = &size_index,
            .value_num = GetElementsIn(size_factors),
            .value_names = size_names,
        },
        &(OptionDesc){
            .name = "Brightness",
            .description = "How bright the moon disc and its moonlight are",
            .kind = OPTKIND_VALUE,
            .val = &bright_index,
            .value_num = GetElementsIn(bright_factors),
            .value_names = bright_names,
        },
        &(OptionDesc){
            .name = "Phase",
            .description = "Which lunar phase the moon shows (Preset = each preset's own)",
            .kind = OPTKIND_VALUE,
            .val = &phase_index,
            .value_num = GetElementsIn(phase_names),
            .value_names = phase_names,
        },
        &(OptionDesc){
            .name = "Arc Height",
            .description = "How high the moon climbs as it crosses the sky over the round",
            .kind = OPTKIND_VALUE,
            .val = &arc_index,
            .value_num = GetElementsIn(arc_degs),
            .value_names = arc_names,
        },
        &(OptionDesc){
            .name = "Color",
            .description = "Override the moon tint across every preset (Preset = each preset's own)",
            .kind = OPTKIND_VALUE,
            .val = &color_index,
            .value_num = GetElementsIn(color_overrides),
            .value_names = color_names,
        },
        &(OptionDesc){
            .name = "Moonlight",
            .description = "Let the moon cast light and zero the secondary stage light: Preset / Off / On",
            .kind = OPTKIND_VALUE,
            .val = &light_index,
            .value_num = WEATHER_TOGGLE_NUM,
            .value_names = weather_toggle_names,
        },
    },
};
