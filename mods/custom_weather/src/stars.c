// Starfield for custom_weather: faint twinkling dots scattered over the City Trial
// sky dome, plus occasional shooting stars.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "gx.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

#define STAR_PI      3.14159265358979f
#define STAR_DEG2RAD (STAR_PI / 180.0f)

#define STAR_MAX   220  // field capacity; resolved density clamps to this
#define STAR_SEGS  6    // rim vertices of each soft dot (a coarse circle is plenty)

// Each star sits at P = eye + skydir*dist (no parallax), with dist clamped inside
// the backdrop dome - a depth-writing sphere at the origin that would otherwise
// occlude it - and inside the far plane. Apparent size is referenced to
// STAR_REF_DIST so it holds constant as the distance is clamped.
#define STAR_MAX_DIST   6000.0f  // desired anchor distance (always clamped smaller)
#define STAR_REF_DIST   1800.0f  // world radius == star.size at this distance
#define STAR_FAR_FRAC   0.9f     // never exceed this fraction of the camera far plane
#define STAR_DOME_R     2500.0f  // CT backdrop dome radius (geometry ~2856 * stage scale)
#define STAR_DOME_FRAC  0.9f     // keep stars within this fraction of the dome distance

// Only seed stars above this elevation so they read as sky, not horizon haze.
#define STAR_MIN_ELEV_DEG  10.0f

// Global baseline star-size multiplier over every preset's resolved size.
#define STAR_SIZE_SCALE  1.0f

// Per-star base brightness floor: some stars are dim, none fully dark.
#define STAR_BRIGHT_MIN  0.35f

// Twinkle: brightness is multiplied by 1 + depth*sin(t*speed + phase), where `t`
// advances one unit per tick.
#define STAR_TW_DEPTH      0.7f
#define STAR_TW_SPEED_MIN  0.05f  // rad/frame (~2s shimmer)
#define STAR_TW_SPEED_MAX  0.14f  // rad/frame (~0.75s shimmer)

// Defaults applied when a preset leaves the matching StarDef field 0; the color
// alpha is the base brightness before luminosity/twinkle.
#define STAR_DEF_COLOR     RGBA(228, 234, 248, 170)
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

// Render GObj drawn first on the XLU sub-pass (farthest back) so the moon and the
// cloud deck blend over the starfield.
#define STAR_GOBJ_CLASS  208
#define STAR_GOBJ_PLINK  32
#define STAR_GX_LINK     0
#define STAR_GX_PRI      0

typedef struct Star
{
    Vec3  dir;       // world sky direction (celestial), seeded once
    float size;      // world radius at STAR_REF_DIST
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

// Cached only to avoid recreating the GObj every frame; never dereferenced.
static GOBJ *stc_star_gobj = NULL;

static int   stc_active = 0;
static int   stc_inited = 0;
static float stc_time = 0.0f;   // twinkle clock, advanced each tick

static Star stc_stars[STAR_MAX];
static int  stc_count = 0;

static Shoot stc_shoot[SHOOT_MAX];
static int   stc_shoot_timer = 0;   // frames until the next meteor
static int   stc_shoot_level = 3;   // active preset's cadence level (1 Off..4 Frequent)

// Resolved per-preset config (StarDef + defaults), before the menu scalars.
static GXColor stc_color = {228, 234, 248, 170};
static int     stc_base_density = STAR_DEF_DENSITY;
static float   stc_twinkle = STAR_DEF_TWINKLE;
static float   stc_lum = STAR_DEF_LUM;
static float   stc_base_size = STAR_DEF_SIZE;
static float   stc_size_var = STAR_DEF_SIZE_VAR;

// Menu knobs layered over the active preset. Stars {Preset,Off,On} gates the whole
// feature; the rest override density/twinkle/luminosity/variance/tint.
static char *show_names[] = {"Preset", "Off", "On"};
static int   show_index = 0;

static const float density_factors[] = {1.0f, 0.5f, 1.0f, 1.7f};
static char *density_names[] = {"Preset", "Sparse", "Normal", "Dense"};
#define STAR_DENSITY_NUM ((int)(sizeof(density_factors) / sizeof(density_factors[0])))
static int density_index = 0;

static const float twinkle_factors[] = {1.0f, 0.0f, 1.0f, 2.0f};
static char *twinkle_names[] = {"Preset", "None", "Gentle", "Lively"};
#define STAR_TWINKLE_NUM ((int)(sizeof(twinkle_factors) / sizeof(twinkle_factors[0])))
static int twinkle_index = 0;

static const float lum_factors[] = {1.0f, 0.6f, 1.0f, 1.4f};
static char *lum_names[] = {"Preset", "Dim", "Normal", "Bright"};
#define STAR_LUM_NUM ((int)(sizeof(lum_factors) / sizeof(lum_factors[0])))
static int lum_index = 0;

// Master scalar over the preset's per-star size spread (resolved var clamped 0..1).
static const float variance_factors[] = {1.0f, 0.2f, 1.0f, 1.8f};
static char *variance_names[] = {"Preset", "Uniform", "Normal", "Varied"};
#define STAR_VARIANCE_NUM ((int)(sizeof(variance_factors) / sizeof(variance_factors[0])))
static int variance_index = 0;

// Index 0 keeps the per-preset RGB; the rest force an RGB, leaving the base
// brightness from the preset color's alpha.
static const u32 color_overrides[] = {0, RGBA(255, 255, 255, 255), RGBA(255, 240, 214, 255),
                                      RGBA(210, 224, 255, 255)};
static char *color_names[] = {"Preset", "White", "Warm", "Cool"};
#define STAR_COLOR_NUM ((int)(sizeof(color_overrides) / sizeof(color_overrides[0])))
static int color_index = 0;

// Random lull range (frames) between meteors, indexed by cadence level 1 (Off) ..
// 4 (Frequent). The index-0 range is never used (Preset resolves to a real level)
// but is kept so the arrays line up with shoot_names.
static const int shoot_lull_min[] = {600, 0, 1200, 600, 240};
static const int shoot_lull_max[] = {1500, 0, 3000, 1500, 600};
static char *shoot_names[] = {"Preset", "Off", "Rare", "Occasional", "Frequent"};
#define STAR_SHOOT_NUM ((int)(sizeof(shoot_names) / sizeof(shoot_names[0])))
static int shoot_index = 0;

// Effective cadence level 1..4; Preset (0) resolves to the preset's latched level.
static int ShootLevel(void)
{
    return (shoot_index == 0) ? stc_shoot_level : shoot_index;
}

// Meteor head/trail size multiplier.
static const float shoot_size_factors[] = {1.0f, 0.65f, 1.0f, 1.5f};
static char *shoot_size_names[] = {"Preset", "Small", "Normal", "Large"};
#define SHOOT_SIZE_NUM ((int)(sizeof(shoot_size_factors) / sizeof(shoot_size_factors[0])))
static int shoot_size_index = 0;

// Life multiplier: a slower meteor lives longer, crossing its arc more slowly.
static const float shoot_speed_factors[] = {1.0f, 1.6f, 1.0f, 0.6f};
static char *shoot_speed_names[] = {"Preset", "Slow", "Normal", "Fast"};
#define SHOOT_SPEED_NUM ((int)(sizeof(shoot_speed_factors) / sizeof(shoot_speed_factors[0])))
static int shoot_speed_index = 0;

// Additive peak-brightness multiplier over SHOOT_BRIGHT.
static const float shoot_bright_factors[] = {1.0f, 0.6f, 1.0f, 1.5f};
static char *shoot_bright_names[] = {"Preset", "Dim", "Normal", "Bright"};
#define SHOOT_BRIGHT_NUM ((int)(sizeof(shoot_bright_factors) / sizeof(shoot_bright_factors[0])))
static int shoot_bright_index = 0;

// Index 0 follows the resolved starfield color; the rest force an RGB.
static const u32 shoot_color_overrides[] = {0, RGBA(255, 255, 255, 255), RGBA(255, 236, 200, 255),
                                            RGBA(200, 224, 255, 255)};
static char *shoot_color_names[] = {"Star", "White", "Warm", "Cool"};
#define SHOOT_COLOR_NUM ((int)(sizeof(shoot_color_overrides) / sizeof(shoot_color_overrides[0])))
static int shoot_color_index = 0;

static void Star_GX(GOBJ *g, int pass);

static HSD_Fog *StarLiveFog(void)
{
    GrObj *gr = *stc_grobj;
    if (!gr || !gr->sky_gobj)
        return NULL;
    return (HSD_Fog *)gr->sky_gobj->hsd_object;
}

// Seed one star: a direction uniformly over the sky cap above the min elevation,
// a size scaled by the resolved variance, and a random twinkle.
static void SeedStar(Star *s, float sin_min, float var)
{
    float z = sin_min + HSD_Randf() * (1.0f - sin_min);
    float rh = 1.0f - z * z;
    rh = (rh > 0.0f) ? sqrtf(rh) : 0.0f;
    float az = HSD_Randf() * 2.0f * STAR_PI;
    s->dir.X = rh * cosf(az);
    s->dir.Y = z;
    s->dir.Z = rh * sinf(az);

    float scale = 1.0f + Weather_Randf2() * var;
    if (scale < 0.3f)
        scale = 0.3f;
    s->size = stc_base_size * STAR_SIZE_SCALE * scale;

    s->bright = STAR_BRIGHT_MIN + HSD_Randf() * (1.0f - STAR_BRIGHT_MIN);
    s->tw_phase = HSD_Randf() * 2.0f * STAR_PI;
    s->tw_speed = STAR_TW_SPEED_MIN + HSD_Randf() * (STAR_TW_SPEED_MAX - STAR_TW_SPEED_MIN);
}

// Scatter the field over the sky cap. No stage dependency, so this always succeeds.
static void Star_Arm(void)
{
    float var = stc_size_var * variance_factors[variance_index];
    if (var < 0.0f) var = 0.0f;
    if (var > 1.0f) var = 1.0f;

    int want = (int)(stc_base_density * density_factors[density_index] + 0.5f);
    if (want > STAR_MAX)
        want = STAR_MAX;
    if (want < 0)
        want = 0;
    stc_count = want;

    float sin_min = sinf(STAR_MIN_ELEV_DEG * STAR_DEG2RAD);
    for (int i = 0; i < stc_count; i++)
        SeedStar(&stc_stars[i], sin_min, var);

    stc_inited = 1;
    OSReport("[Stars] Armed %d stars\n", stc_count);
}

static void Star_Ensure(void)
{
    if (stc_star_gobj)
        return;
    stc_star_gobj = WeatherGX_EnsureLayer(STAR_GOBJ_CLASS, STAR_GOBJ_PLINK, Star_GX,
                                          STAR_GX_LINK, STAR_GX_PRI,
                                          "[Stars] Starfield layer");
}

// Emit one billboard vertex: P + u*right + v*up, flat color.
static void StarVert(const Vec3 *P, const Vec3 *R, const Vec3 *U, float u, float v,
                     u8 cr, u8 cg, u8 cb, u8 ca)
{
    GXPosition3f32(P->X + u * R->X + v * U->X,
                   P->Y + u * R->Y + v * U->Y,
                   P->Z + u * R->Z + v * U->Z);
    GXColor4u8(cr, cg, cb, ca);
}

// Anchor a unit sky direction onto the backdrop dome: P = eye + dir*dist, dist
// clamped inside the dome and the far plane. Returns the chosen distance.
static float PlaceOnDome(const Vec3 *dir, const Vec3 *eye, float e2, float maxd, Vec3 *P)
{
    float edotd = eye->X * dir->X + eye->Y * dir->Y + eye->Z * dir->Z;
    float disc = edotd * edotd + STAR_DOME_R * STAR_DOME_R - e2;
    float t_dome = (disc > 0.0f) ? (-edotd + sqrtf(disc)) : STAR_DOME_R;
    float dist = STAR_MAX_DIST;
    float lim = STAR_DOME_FRAC * t_dome;
    if (dist > lim)
        dist = lim;
    if (dist > maxd)
        dist = maxd;
    P->X = eye->X + dir->X * dist;
    P->Y = eye->Y + dir->Y * dist;
    P->Z = eye->Z + dir->Z * dist;
    return dist;
}

// GX callback on the world camera link, XLU pass. Draws each star as a camera-facing
// additive glow, fog-free (bracketed HSD_FogSet) so the distant dots aren't washed to
// fog color, depth-tested but not depth-writing so terrain occludes them.
static void Star_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != 1)
        return;
    if (!stc_active || stc_count <= 0)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

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
    float e2 = eye.X * eye.X + eye.Y * eye.Y + eye.Z * eye.Z;
    float maxd = cam->far * STAR_FAR_FRAC;

    float tw = stc_twinkle * twinkle_factors[twinkle_index];
    if (tw < 0.0f) tw = 0.0f;
    if (tw > 1.0f) tw = 1.0f;
    float lum = stc_lum * lum_factors[lum_index];

    HSD_Fog *fog = StarLiveFog();

    WeatherGX_BeginXlu(cam, 1, 0); // additive: dots glow, never darken the sky
    if (fog)
        HSD_FogSet(NULL);

    for (int i = 0; i < stc_count; i++)
    {
        Star *s = &stc_stars[i];

        float f = 1.0f + tw * STAR_TW_DEPTH * sinf(stc_time * s->tw_speed + s->tw_phase);
        float a = (float)stc_color.a * lum * s->bright * f;
        if (a <= 1.0f)
            continue;
        if (a > 255.0f)
            a = 255.0f;
        u8 A = (u8)a;

        Vec3 P;
        float dist = PlaceOnDome(&s->dir, &eye, e2, maxd, &P);
        float r = s->size * (dist / STAR_REF_DIST);

        GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, STAR_SEGS + 2);
        StarVert(&P, &rightW, &upW, 0.0f, 0.0f, stc_color.r, stc_color.g, stc_color.b, A);
        for (int sgm = 0; sgm <= STAR_SEGS; sgm++)
        {
            float ang = 2.0f * STAR_PI * (float)sgm / (float)STAR_SEGS;
            StarVert(&P, &rightW, &upW, cosf(ang) * r, sinf(ang) * r,
                     stc_color.r, stc_color.g, stc_color.b, 0);
        }
    }

    // Meteors: the trail samples the great-circle path just behind the head and
    // fades to transparent at the tail.
    if (ShootLevel() != 1) // 1 = Off
    {
        GXColor sc = stc_color;
        if (shoot_color_index > 0)
        {
            GXColor ov = GXColor_Unpack(shoot_color_overrides[shoot_color_index]);
            sc.r = ov.r;
            sc.g = ov.g;
            sc.b = ov.b;
        }
        float peak = (float)SHOOT_BRIGHT * shoot_bright_factors[shoot_bright_index];
        if (peak > 255.0f)
            peak = 255.0f;
        float head_size = SHOOT_HEAD_SIZE * shoot_size_factors[shoot_size_index];
        int lw = (int)(SHOOT_LINE_WIDTH * shoot_size_factors[shoot_size_index]);
        if (lw < 1)
            lw = 1;
        GXSetLineWidth((u8)lw, 5);

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
                float a = sh->arc * pp;
                float ca = cosf(a), sa = sinf(a);
                Vec3 d = {sh->d0.X * ca + sh->t.X * sa,
                          sh->d0.Y * ca + sh->t.Y * sa,
                          sh->d0.Z * ca + sh->t.Z * sa};
                Vec3 TP;
                PlaceOnDome(&d, &eye, e2, maxd, &TP);
                u8 A = (u8)(peak * env * f);
                GXPosition3f32(TP.X, TP.Y, TP.Z);
                GXColor4u8(sc.r, sc.g, sc.b, A);
            }

            float ah = sh->arc * p;
            float ch = cosf(ah), shh = sinf(ah);
            Vec3 hd = {sh->d0.X * ch + sh->t.X * shh,
                       sh->d0.Y * ch + sh->t.Y * shh,
                       sh->d0.Z * ch + sh->t.Z * shh};
            Vec3 HP;
            float hdist = PlaceOnDome(&hd, &eye, e2, maxd, &HP);
            float hr = head_size * (hdist / STAR_REF_DIST);
            u8 HA = (u8)(peak * env);
            GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, STAR_SEGS + 2);
            StarVert(&HP, &rightW, &upW, 0.0f, 0.0f, sc.r, sc.g, sc.b, HA);
            for (int sgm = 0; sgm <= STAR_SEGS; sgm++)
            {
                float ang = 2.0f * STAR_PI * (float)sgm / (float)STAR_SEGS;
                StarVert(&HP, &rightW, &upW, cosf(ang) * hr, sinf(ang) * hr,
                         sc.r, sc.g, sc.b, 0);
            }
        }
    }

    if (fog)
        HSD_FogSet(fog); // restore world fog for later geometry on this pass
    HSD_StateInvalidate(-1);
}

static int RandLull(void)
{
    int lvl = ShootLevel();
    if (lvl == 1) // Off falls back to Occasional for the (unused) seed delay
        lvl = 3;
    int lo = shoot_lull_min[lvl];
    int hi = shoot_lull_max[lvl];
    return lo + HSD_Randi(hi - lo + 1);
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

    float el = (25.0f + HSD_Randf() * 50.0f) * STAR_DEG2RAD; // start 25..75 deg up
    float az = HSD_Randf() * 2.0f * STAR_PI;
    float ce = cosf(el), se = sinf(el);
    Vec3 d0 = {ce * sinf(az), se, ce * cosf(az)};

    // Orthonormal tangent basis to d0 (e1 horizontal, e2 completing it), combined at
    // a random roll, then flipped downward so meteors descend.
    Vec3 e1 = {-d0.Z, 0.0f, d0.X};
    float e1len = sqrtf(e1.X * e1.X + e1.Z * e1.Z);
    if (e1len < 1e-4f)
        e1len = 1e-4f;
    e1.X /= e1len; e1.Z /= e1len;
    Vec3 e2 = {d0.Y * e1.Z - d0.Z * e1.Y,
               d0.Z * e1.X - d0.X * e1.Z,
               d0.X * e1.Y - d0.Y * e1.X};
    float roll = HSD_Randf() * 2.0f * STAR_PI;
    float cr = cosf(roll), sr = sinf(roll);
    Vec3 t = {e1.X * cr + e2.X * sr, e1.Y * cr + e2.Y * sr, e1.Z * cr + e2.Z * sr};
    if (t.Y > 0.0f)
    {
        t.X = -t.X; t.Y = -t.Y; t.Z = -t.Z;
    }

    sh->d0 = d0;
    sh->t = t;
    sh->arc = SHOOT_ARC_MIN + HSD_Randf() * (SHOOT_ARC_MAX - SHOOT_ARC_MIN);
    int base_life = SHOOT_LIFE_MIN + HSD_Randi(SHOOT_LIFE_MAX - SHOOT_LIFE_MIN + 1);
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

// Advance live meteors and, per the menu cadence, launch new ones.
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
    if (ShootLevel() == 1)
        return; // Off: let live meteors finish, launch none
    if (stc_shoot_timer > 0)
    {
        stc_shoot_timer--;
        return;
    }
    Shoot_Spawn();
    stc_shoot_timer = RandLull();
}

// Latch the active preset's star config, resolving each 0 field to its module
// default and applying the menu overrides.
void Star_SetActive(const StarDef *def)
{
    if (show_index == 1) // menu Off
    {
        stc_active = 0;
        Shoot_Reset();
        return;
    }
    int on = (def && def->enabled) || (show_index == 2);
    if (!on)
    {
        stc_active = 0;
        Shoot_Reset();
        return;
    }
    stc_active = 1;

    stc_color = GXColor_Unpack((def && def->color) ? def->color : STAR_DEF_COLOR);
    if (color_index > 0)
    {
        GXColor ov = GXColor_Unpack(color_overrides[color_index]);
        stc_color.r = ov.r;
        stc_color.g = ov.g;
        stc_color.b = ov.b;
    }

    stc_base_density = (def && def->density > 0) ? def->density : STAR_DEF_DENSITY;
    if (stc_base_density > STAR_MAX)
        stc_base_density = STAR_MAX;
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
    Star_Ensure();
    stc_time += 1.0f;
    Shoot_Tick();
}

void Star_Reset(void)
{
    // The engine frees every world GObj on scene teardown; drop the cached handle so
    // the next active frame recreates it.
    stc_star_gobj = NULL;
    stc_inited = 0;
    stc_count = 0;
    stc_active = 0;
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
            .value_num = STAR_SHOOT_NUM,
            .value_names = shoot_names,
        },
        &(OptionDesc){
            .name = "Size",
            .description = "How large the meteor heads and trails are",
            .kind = OPTKIND_VALUE,
            .val = &shoot_size_index,
            .value_num = SHOOT_SIZE_NUM,
            .value_names = shoot_size_names,
        },
        &(OptionDesc){
            .name = "Speed",
            .description = "How fast the meteors streak across the sky",
            .kind = OPTKIND_VALUE,
            .val = &shoot_speed_index,
            .value_num = SHOOT_SPEED_NUM,
            .value_names = shoot_speed_names,
        },
        &(OptionDesc){
            .name = "Brightness",
            .description = "How bright the meteors glow",
            .kind = OPTKIND_VALUE,
            .val = &shoot_bright_index,
            .value_num = SHOOT_BRIGHT_NUM,
            .value_names = shoot_bright_names,
        },
        &(OptionDesc){
            .name = "Color",
            .description = "Meteor tint (Star = follow the starfield color)",
            .kind = OPTKIND_VALUE,
            .val = &shoot_color_index,
            .value_num = SHOOT_COLOR_NUM,
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
            .value_num = 3,
            .value_names = show_names,
        },
        &(OptionDesc){
            .name = "Density",
            .description = "How many stars fill the sky over every CT preset",
            .kind = OPTKIND_VALUE,
            .val = &density_index,
            .value_num = STAR_DENSITY_NUM,
            .value_names = density_names,
        },
        &(OptionDesc){
            .name = "Twinkle",
            .description = "How much the stars shimmer in brightness (None = steady)",
            .kind = OPTKIND_VALUE,
            .val = &twinkle_index,
            .value_num = STAR_TWINKLE_NUM,
            .value_names = twinkle_names,
        },
        &(OptionDesc){
            .name = "Luminosity",
            .description = "How bright the stars glow",
            .kind = OPTKIND_VALUE,
            .val = &lum_index,
            .value_num = STAR_LUM_NUM,
            .value_names = lum_names,
        },
        &(OptionDesc){
            .name = "Size Variance",
            .description = "How much the stars vary in size (Uniform = even, Varied = mixed)",
            .kind = OPTKIND_VALUE,
            .val = &variance_index,
            .value_num = STAR_VARIANCE_NUM,
            .value_names = variance_names,
        },
        &(OptionDesc){
            .name = "Color",
            .description = "Override the star tint across every preset (Preset = each preset's own)",
            .kind = OPTKIND_VALUE,
            .val = &color_index,
            .value_num = STAR_COLOR_NUM,
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
