// Moon for custom_weather: a phased disc crossing the City Trial sky over the
// round, optionally casting a directional moonlight.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "gx.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

#define MOON_PI      3.14159265358979f
#define MOON_DEG2RAD (MOON_PI / 180.0f)

// Anchored along the sky direction from the camera eye (no parallax), with the
// distance clamped inside the backdrop dome - a depth-writing sphere at the world
// origin that would otherwise occlude the moon. Apparent size is referenced to
// MOON_REF_DIST so it holds constant as the distance is clamped.
#define MOON_MAX_DIST   1800.0f  // desired anchor distance from the eye
#define MOON_REF_DIST   1800.0f  // r == stc_size at this distance (sets apparent size)
#define MOON_FAR_FRAC   0.85f    // never exceed this fraction of the camera far plane
#define MOON_DOME_R     2500.0f  // CT backdrop dome radius (geometry ~2856 * stage scale)
#define MOON_DOME_FRAC  0.82f    // keep the moon within this fraction of the dome distance

#define MOON_BANDS        28
#define MOON_COLS         6
#define MOON_CRATERS      13
#define MOON_CRATER_SEGS  9

// Radial fraction of the disc where the soft edge begins; alpha fades to 0 at the rim.
#define MOON_RIM_FADE     0.85f

// Defaults applied when a preset leaves the matching MoonDef field 0; the color
// alpha is the base opacity before the menu Brightness scalar.
#define MOON_DEF_COLOR        RGBA(232, 234, 240, 255)
#define MOON_DEF_SIZE         92.0f   // disc radius in world units on the dome
#define MOON_DEF_ARC          26.0f   // peak elevation degrees over the round
#define MOON_DEF_BEARING      95.0f   // rise compass bearing (deg); ~east
#define MOON_DEF_LIGHT_COLOR  RGBA(120, 140, 195, 255)

// Global multiplier over the resolved size and the menu Size scalar.
#define MOON_SIZE_SCALE  1.0f

#define MOON_CRATER_SHADE  0.58f  // crater RGB = disc RGB * this
#define MOON_CRATER_ALPHA  95     // crater opacity over the disc

// Disc render GObj, drawn first on the XLU sub-pass so the cloud deck blends over it.
#define MOON_GOBJ_CLASS  207
#define MOON_GOBJ_PLINK  31
#define MOON_GX_LINK     0
#define MOON_GX_PRI      0

#define MOON_LOBJ_GOBJ_CLASS  39
#define MOON_LOBJ_GOBJ_PLINK  33

static GOBJ *stc_moon_gobj = NULL;   // disc render layer (never dereferenced)
static int   stc_active = 0;

// Resolved per-preset config (MoonDef + defaults), before the menu scalars.
static GXColor stc_color = {232, 234, 240, 255};
static float   stc_size = MOON_DEF_SIZE;
static int     stc_phase = MOON_FULL;
static float   stc_arc = MOON_DEF_ARC;
static float   stc_bearing = MOON_DEF_BEARING;
static int     stc_light = 0;
static GXColor stc_light_color = {120, 140, 195, 255};

// Crater shape in unit-disc coords (X, Y in [-1,1]; Z = radius as a fraction of
// the disc radius), seeded once and scaled into world space each frame.
static Vec3 stc_crater[MOON_CRATERS];
static int  stc_crater_seeded = 0;

// Menu knobs layered over the active preset. Moon {Preset,Off,On} gates the whole
// feature; the rest override appearance/arc/phase.
static char *show_names[] = {"Preset", "Off", "On"};
static int   show_index = 0;

static const float size_factors[] = {1.0f, 0.7f, 1.0f, 1.4f};
static char *size_names[] = {"Preset", "Small", "Normal", "Large"};
#define MOON_SIZE_NUM ((int)(sizeof(size_factors) / sizeof(size_factors[0])))
static int size_index = 0;

static const float bright_factors[] = {1.0f, 0.6f, 1.0f, 1.3f};
static char *bright_names[] = {"Preset", "Dim", "Normal", "Bright"};
#define MOON_BRIGHT_NUM ((int)(sizeof(bright_factors) / sizeof(bright_factors[0])))
static int bright_index = 0;

// Index 0 = Preset; 1..8 map to the MoonPhase enum in order.
static char *phase_names[] = {"Preset", "Full", "Waxing Crescent", "First Quarter",
                              "Waxing Gibbous", "Waning Gibbous", "Last Quarter",
                              "Waning Crescent", "New"};
#define MOON_PHASE_NUM ((int)(sizeof(phase_names) / sizeof(phase_names[0])))
static int phase_index = 0;

// Peak-elevation override in degrees.
static const float arc_degs[] = {0.0f, 15.0f, 26.0f, 42.0f, 62.0f};
static char *arc_names[] = {"Preset", "Low", "Mid", "High", "Overhead"};
#define MOON_ARC_NUM ((int)(sizeof(arc_degs) / sizeof(arc_degs[0])))
static int arc_index = 0;

// Index 0 keeps the per-preset RGB; the rest force an RGB, leaving the alpha from
// the preset opacity * the Brightness scalar.
static const u32 color_overrides[] = {0, RGBA(242, 244, 250, 255), RGBA(210, 214, 224, 255),
                                      RGBA(150, 175, 230, 255), RGBA(240, 214, 158, 255)};
static char *color_names[] = {"Preset", "White", "Silver", "Blue", "Amber"};
#define MOON_COLOR_NUM ((int)(sizeof(color_overrides) / sizeof(color_overrides[0])))
static int color_index = 0;

static char *light_names[] = {"Preset", "Off", "On"};
static int   light_index = 0;

static void Moon_GX(GOBJ *g, int pass);

static HSD_Fog *MoonLiveFog(void)
{
    GrObj *gr = *stc_grobj;
    if (!gr || !gr->sky_gobj)
        return NULL;
    return (HSD_Fog *)gr->sky_gobj->hsd_object;
}

// Normalized round progress 0 (start) .. 1 (end) from the CT match timer; no timer
// (menus, non-city, match intro) holds the moon at its rise point.
static float MoonProgress(void)
{
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (!info)
        return 0.0f;
    if (info->flags_x2a8 & 0x40) // is_match_intro
        return 0.0f;
    float p = info->match_progress;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}

// Sky direction at the current progress: rises at `stc_bearing`, peaks at `stc_arc`
// elevation mid-round, sets at the opposite bearing. Below the horizon dir.Y <= 0.
static void MoonDirection(Vec3 *out)
{
    float p = MoonProgress();
    float el = stc_arc * sinf(p * MOON_PI) * MOON_DEG2RAD;
    float az = (stc_bearing + 180.0f * p) * MOON_DEG2RAD;
    float ce = cosf(el), se = sinf(el);
    out->X = ce * sinf(az);
    out->Y = se;
    out->Z = ce * cosf(az);
}

// Effective phase (menu override wins over the preset).
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
// side of the terminator, sampled around its rim.
static int CraterFits(float cu, float cv, float crad, float r, float k, int side)
{
    if (sqrtf(cu * cu + cv * cv) + crad > r * MOON_RIM_FADE)
        return 0;
    for (int s = 0; s < MOON_CRATER_SEGS; s++)
    {
        float a = 2.0f * MOON_PI * (float)s / (float)MOON_CRATER_SEGS;
        float pu = cu + cosf(a) * crad;
        float pv = cv + sinf(a) * crad;
        float uL, uR;
        LitExtent(pv, r, k, side, &uL, &uR);
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
        float ang = HSD_Randf() * 2.0f * MOON_PI;
        float rad = sqrtf(HSD_Randf()) * 0.62f; // uniform over the inner disc
        stc_crater[i].X = cosf(ang) * rad;
        stc_crater[i].Y = sinf(ang) * rad;
        stc_crater[i].Z = 0.08f + HSD_Randf() * 0.10f;
    }
    stc_crater_seeded = 1;
}

// Emit one billboard vertex: P + u*right + v*up, flat color.
static void MoonVert(const Vec3 *P, const Vec3 *R, const Vec3 *U, float u, float v,
                     u8 cr, u8 cg, u8 cb, u8 ca)
{
    GXPosition3f32(P->X + u * R->X + v * U->X,
                   P->Y + u * R->Y + v * U->Y,
                   P->Z + u * R->Z + v * U->Z);
    GXColor4u8(cr, cg, cb, ca);
}

// GX callback on the world camera link, XLU pass. Draws a camera-facing disc
// fog-free (bracketed HSD_FogSet) so the distant disc isn't washed to fog color,
// depth-tested but not depth-writing so terrain occludes it.
static void Moon_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != 1)
        return;
    if (!stc_active)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    Vec3 dir;
    MoonDirection(&dir);
    if (dir.Y <= 0.0f) // below the horizon
        return;

    int phase = MoonPhaseNow();
    if (phase == MOON_NEW)
        return;
    float k;
    int side;
    PhaseParams(phase, &k, &side);

    // Rows 0/1 of the world->view rotation are the camera axes in world space,
    // giving the billboard basis.
    float (*m)[4] = cam->view_mtx;
    Vec3 rightW = {m[0][0], m[0][1], m[0][2]};
    Vec3 upW = {m[1][0], m[1][1], m[1][2]};

    // Camera eye in world space, eye = -R^T * t.
    Vec3 eye = {
        -(m[0][0] * m[0][3] + m[1][0] * m[1][3] + m[2][0] * m[2][3]),
        -(m[0][1] * m[0][3] + m[1][1] * m[1][3] + m[2][1] * m[2][3]),
        -(m[0][2] * m[0][3] + m[1][2] * m[1][3] + m[2][2] * m[2][3]),
    };

    // Distance from the eye to the backdrop dome (sphere at the origin) along the
    // sky direction.
    float edotd = eye.X * dir.X + eye.Y * dir.Y + eye.Z * dir.Z;
    float e2 = eye.X * eye.X + eye.Y * eye.Y + eye.Z * eye.Z;
    float disc = edotd * edotd + MOON_DOME_R * MOON_DOME_R - e2;
    float t_dome = (disc > 0.0f) ? (-edotd + sqrtf(disc)) : MOON_DOME_R;

    // Anchor at the smallest of the desired distance, a fraction of the dome
    // distance, and a fraction of the far plane.
    float dist = MOON_MAX_DIST;
    float lim = MOON_DOME_FRAC * t_dome;
    if (dist > lim)
        dist = lim;
    float maxd = cam->far * MOON_FAR_FRAC;
    if (dist > maxd)
        dist = maxd;
    Vec3 P = {eye.X + dir.X * dist, eye.Y + dir.Y * dist, eye.Z + dir.Z * dist};

    // Apparent size stays constant as the distance is clamped.
    float r = stc_size * size_factors[size_index] * MOON_SIZE_SCALE * (dist / MOON_REF_DIST);

    float bf = bright_factors[bright_index];
    int rr = (int)(stc_color.r * bf); if (rr > 255) rr = 255;
    int gg = (int)(stc_color.g * bf); if (gg > 255) gg = 255;
    int bb = (int)(stc_color.b * bf); if (bb > 255) bb = 255;
    u8 dR = (u8)rr, dG = (u8)gg, dB = (u8)bb, dA = stc_color.a;

    HSD_Fog *fog = MoonLiveFog();

    WeatherGX_BeginXlu(cam, 0, 0);
    if (fog)
        HSD_FogSet(NULL); // draw the distant moon fog-free

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
            float u0 = uL0 + (uR0 - uL0) * f;
            float u1 = uL1 + (uR1 - uL1) * f;
            u8 a0 = (u8)(dA * MoonRimFade(u0, v0, r));
            u8 a1 = (u8)(dA * MoonRimFade(u1, v1, r));
            MoonVert(&P, &rightW, &upW, u0, v0, dR, dG, dB, a0);
            MoonVert(&P, &rightW, &upW, u1, v1, dR, dG, dB, a1);
        }
    }

    u8 kR = (u8)(dR * MOON_CRATER_SHADE);
    u8 kG = (u8)(dG * MOON_CRATER_SHADE);
    u8 kB = (u8)(dB * MOON_CRATER_SHADE);
    u8 kA = (dA < MOON_CRATER_ALPHA) ? dA : MOON_CRATER_ALPHA;
    for (int i = 0; i < MOON_CRATERS; i++)
    {
        float cu = stc_crater[i].X * r;
        float cv = stc_crater[i].Y * r;
        float crad = stc_crater[i].Z * r;
        if (!CraterFits(cu, cv, crad, r, k, side))
            continue;

        GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, MOON_CRATER_SEGS + 2);
        MoonVert(&P, &rightW, &upW, cu, cv, kR, kG, kB, kA);
        for (int s = 0; s <= MOON_CRATER_SEGS; s++)
        {
            float a = 2.0f * MOON_PI * (float)s / (float)MOON_CRATER_SEGS;
            MoonVert(&P, &rightW, &upW, cu + cosf(a) * crad, cv + sinf(a) * crad,
                     kR, kG, kB, kA);
        }
    }

    if (fog)
        HSD_FogSet(fog); // restore world fog for later geometry on this pass
    HSD_StateInvalidate(-1);
}

static void Moon_EnsureRender(void)
{
    if (stc_moon_gobj)
        return;
    stc_moon_gobj = WeatherGX_EnsureLayer(MOON_GOBJ_CLASS, MOON_GOBJ_PLINK, Moon_GX,
                                          MOON_GX_LINK, MOON_GX_PRI,
                                          "[Moon] Moon layer");
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
    .flags = LOBJ_INFINITE | LOBJ_DIFFUSE | LOBJ_SPECULAR, // 0x0D
    .attnflags = 0,
    .color = {0, 0, 0, 0xFF},
    .position = (struct _HSD_WObjDesc *)&s_moon_pos_desc,
    .interest = 0,
    .u = {.p = 0},
};
static LOBJ *s_moon_lobj = 0;

// The secondary INFINITE stage light, zeroed while the moonlight is on so the moon
// dominates (the primary sun *stc_main_light drives the weather runtime's terrain
// tint). Cached so it can be restored.
static LOBJ  *s_sup_lobj = 0;
static GXColor s_sup_color, s_sup_hw;

static int MoonLightOn(void)
{
    if (light_index == 1) return 0; // menu Off
    if (light_index == 2) return 1; // menu On (force)
    return stc_light;               // Preset
}

static void MoonLight_Ensure(void)
{
    if (s_moon_lobj)
        return;
    GOBJ *gobj = GObj_Create(MOON_LOBJ_GOBJ_CLASS, MOON_LOBJ_GOBJ_PLINK, 0);
    if (!gobj)
        return;
    s_moon_lobj = LObj_LoadDesc(&s_moon_lobj_desc);
    if (!s_moon_lobj)
    {
        OSReport("[Moon] Moonlight LOBJ failed to load\n");
        return;
    }
    GObj_AddObject(gobj, HSD_OBJKIND_LOBJ, s_moon_lobj);
    GObj_AddGXLink(gobj, LObj_GX, 0, 0);
}

static void MoonLight_Zero(void)
{
    if (!s_moon_lobj)
        return;
    s_moon_lobj->color.r = s_moon_lobj->color.g = s_moon_lobj->color.b = 0;
    s_moon_lobj->hw_color.r = s_moon_lobj->hw_color.g = s_moon_lobj->hw_color.b = 0;
}

// Zero the secondary INFINITE stage light, found via the HW slot table and cached
// on first touch. The slot table lags a frame, so this may resolve nothing and retry.
static void SuppressSecondary(void)
{
    if (s_sup_lobj)
    {
        s_sup_lobj->color.r = s_sup_lobj->color.g = s_sup_lobj->color.b = 0;
        s_sup_lobj->hw_color.r = s_sup_lobj->hw_color.g = s_sup_lobj->hw_color.b = 0;
        return;
    }
    LOBJ *primary = *stc_main_light;
    for (int s = 0; s < HSD_LOBJ_HW_SLOT_AMBIENT; s++)
    {
        LOBJ *l = stc_lobj_hw_slot_table[s];
        if (!l || l == primary || l == s_moon_lobj)
            continue;
        if ((l->flags & 3) != LOBJ_INFINITE)
            continue;
        s_sup_lobj = l;
        s_sup_color = l->color;
        s_sup_hw = l->hw_color;
        l->color.r = l->color.g = l->color.b = 0;
        l->hw_color.r = l->hw_color.g = l->hw_color.b = 0;
        break;
    }
}

// Restore the suppressed light to its cached original; no-op if nothing was suppressed.
static void RestoreSecondary(void)
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
    if (!MoonLightOn())
    {
        MoonLight_Zero();
        RestoreSecondary();
        return;
    }
    MoonLight_Ensure();

    Vec3 dir;
    MoonDirection(&dir);
    if (dir.Y <= 0.0f) // moon down: let the normal sun light the scene
    {
        MoonLight_Zero();
        RestoreSecondary();
        return;
    }

    Vec3 lp = {dir.X * 1500.0f, dir.Y * 1500.0f, dir.Z * 1500.0f};
    if (s_moon_lobj)
    {
        LObj_SetPosition(s_moon_lobj, &lp);
        float bf = bright_factors[bright_index];
        int rr = (int)(stc_light_color.r * bf); if (rr > 255) rr = 255;
        int gg = (int)(stc_light_color.g * bf); if (gg > 255) gg = 255;
        int bb = (int)(stc_light_color.b * bf); if (bb > 255) bb = 255;
        s_moon_lobj->color.r = (u8)rr;
        s_moon_lobj->color.g = (u8)gg;
        s_moon_lobj->color.b = (u8)bb;
        s_moon_lobj->color.a = 0xFF;
        s_moon_lobj->hw_color = s_moon_lobj->color;
    }
    SuppressSecondary();
}

// Latch the active preset's moon config, resolving each 0 field to its module
// default and applying the menu overrides.
void Moon_SetActive(const MoonDef *def)
{
    if (show_index == 1) // menu Off
    {
        stc_active = 0;
        MoonLight_Zero();
        RestoreSecondary();
        return;
    }
    int on = (def && def->enabled) || (show_index == 2);
    if (!on)
    {
        stc_active = 0;
        MoonLight_Zero();
        RestoreSecondary();
        return;
    }
    stc_active = 1;

    stc_color = GXColor_Unpack((def && def->color) ? def->color : MOON_DEF_COLOR);
    if (color_index > 0)
    {
        GXColor ov = GXColor_Unpack(color_overrides[color_index]);
        stc_color.r = ov.r;
        stc_color.g = ov.g;
        stc_color.b = ov.b;
    }

    stc_size = (def && def->size > 0.0f) ? def->size : MOON_DEF_SIZE;
    stc_phase = def ? def->phase : MOON_FULL;
    stc_arc = (def && def->arc_height > 0.0f) ? def->arc_height : MOON_DEF_ARC;
    if (arc_index > 0)
        stc_arc = arc_degs[arc_index];
    stc_bearing = (def && def->rise_bearing != 0.0f) ? def->rise_bearing : MOON_DEF_BEARING;
    stc_light = def ? def->light : 0;
    stc_light_color = GXColor_Unpack((def && def->light_color) ? def->light_color
                                                              : MOON_DEF_LIGHT_COLOR);

    // Restore the old preset's suppressed light and drop the cache so the new
    // preset re-resolves it next tick.
    RestoreSecondary();
}

void Moon_Tick(void)
{
    if (!stc_active)
        return;
    SeedCraters();
    Moon_EnsureRender();
    MoonLight_Tick();
}

void Moon_Reset(void)
{
    // The engine frees every world GObj (and the stage LOBJs) on scene teardown;
    // drop the cached handles so the next active frame recreates them, and never
    // write through them after.
    stc_moon_gobj = NULL;
    s_moon_lobj = 0;
    s_sup_lobj = 0;
    stc_active = 0;
}

MenuDesc moon_menu = {
    .option_num = 7,
    .options = {
        &(OptionDesc){
            .name = "Moon",
            .description = "Show the moon: Preset = only presets that set it, Off = never, On = every CT preset",
            .kind = OPTKIND_VALUE,
            .val = &show_index,
            .value_num = 3,
            .value_names = show_names,
        },
        &(OptionDesc){
            .name = "Size",
            .description = "How large the moon disc is",
            .kind = OPTKIND_VALUE,
            .val = &size_index,
            .value_num = MOON_SIZE_NUM,
            .value_names = size_names,
        },
        &(OptionDesc){
            .name = "Brightness",
            .description = "How bright the moon disc and its moonlight are",
            .kind = OPTKIND_VALUE,
            .val = &bright_index,
            .value_num = MOON_BRIGHT_NUM,
            .value_names = bright_names,
        },
        &(OptionDesc){
            .name = "Phase",
            .description = "Which lunar phase the moon shows (Preset = each preset's own)",
            .kind = OPTKIND_VALUE,
            .val = &phase_index,
            .value_num = MOON_PHASE_NUM,
            .value_names = phase_names,
        },
        &(OptionDesc){
            .name = "Arc Height",
            .description = "How high the moon climbs as it crosses the sky over the round",
            .kind = OPTKIND_VALUE,
            .val = &arc_index,
            .value_num = MOON_ARC_NUM,
            .value_names = arc_names,
        },
        &(OptionDesc){
            .name = "Color",
            .description = "Override the moon tint across every preset (Preset = each preset's own)",
            .kind = OPTKIND_VALUE,
            .val = &color_index,
            .value_num = MOON_COLOR_NUM,
            .value_names = color_names,
        },
        &(OptionDesc){
            .name = "Moonlight",
            .description = "Let the moon cast light and dim the distant sun: Preset / Off / On",
            .kind = OPTKIND_VALUE,
            .val = &light_index,
            .value_num = 3,
            .value_names = light_names,
        },
    },
};
