#ifndef WEATHER_FX_H
#define WEATHER_FX_H

#include "structs.h"
#include "stage.h"
#include "hsd.h"
#include "obj.h"
#include "camera.h"
#include "gx.h"
#include "inline.h"
#include "hoshi/settings.h"

// Every drawn layer is a proc-less GObj on the world camera's GX link 0, XLU sub-pass.
// The class and p_link sit past every engine bucket, so no bucket walker sees them.
#define WEATHER_GOBJ_CLASS 201
#define WEATHER_GOBJ_PLINK 36
#define WEATHER_GX_LINK    0
#define WEATHER_GX_PRI     0

// City Trial's backdrop is a depth-writing sphere at the world origin: 2856 units in the
// model times the 0.70 stage scale, at 100% Backdrop Distance. A celestial layer anchored
// beyond it would be occluded, so moon and stars clamp inside it. Apparent size is
// referenced to WEATHER_DOME_REF_DIST so it survives the clamp.
#define WEATHER_DOME_R_BASE   2000.0f
#define WEATHER_DOME_REF_DIST 1800.0f

// Downward ground casts start this far above the out-of-bounds ceiling.
#define WEATHER_CAST_MARGIN 50.0f

// Edge of the camera-following rain and snow volume (a cube).
#define WEATHER_BOX 1000.0f

// Shared menu value-name tables.
extern char *weather_toggle_names[];  // {"Preset", "Off", "On"}
extern char *weather_onoff_names[];   // {"Off", "On"}
extern char *weather_enable_names[];  // {"Disabled", "Enabled"}
#define WEATHER_TOGGLE_NUM 3
#define WEATHER_ONOFF_NUM  2

// A knob's per-value table must line up with its value names.
#define WEATHER_KNOB_CHECK(values, names) \
    _Static_assert(GetElementsIn(values) == GetElementsIn(names), \
                   #values " and " #names " differ in length")

// Resolve a {Preset, Off, On} menu index to a boolean: Preset (0) yields
// `preset_default`, Off (1) = 0, On (2) = 1.
static inline int WeatherToggle(int idx, int preset_default)
{
    return (idx == 0) ? preset_default : (idx == 2);
}

// Symmetric random in [-1, 1).
float Weather_Randf2(void);
// 1 + Weather_Randf2() * var, floored at `min`.
float Weather_RandScale(float var, float min);

// `base * factor` rounded to the nearest count, at most `max`.
int Weather_ScaleCount(int base, float factor, int max);

// RGB scaled by `s` and clamped; alpha kept.
GXColor Weather_ScaleRGB(GXColor c, float s);
// Replace the RGB of `c` with a packed override's, keeping its alpha; 0 leaves `c` alone.
void Weather_OverrideRGB(GXColor *c, u32 rgb);

// Normalized City Trial round progress, 0 (start) .. 1 (end), from the match timer.
// -1 when there is no live round (menus, match intro).
float Weather_RoundProgress(void);

// NULL until the 3D scene is built; the layers that need stage extents retry.
StageNode *Weather_StageNode(void);

// Cast down at (x, z) from above the out-of-bounds ceiling to `floor_y`. Returns the hit
// triangle, or -1 when nothing is below.
int Weather_CastDown(StageNode *sn, float x, float z, float floor_y, Vec3 *hit);

// One-shot stage sound at a world position, attenuated by its distance from the
// listener; skipped when out of earshot or no emitter is free.
void Weather_PlaySoundAt(int fgm_id, const Vec3 *pos);
// A particle effect (id below 240000) at a world position, uniformly scaled. Particles
// expire on their own.
void Weather_SpawnParticle(int effect_id, const Vec3 *pos, float scale);

// Camera shake through one constant quake source held in `*src` (0 = none), created on
// the first call and retuned on every later one. view NULL shakes every view. A view
// applies only its highest-priority source, whatever that source's offset, so a live
// source mutes every lower-priority shake on the views it covers.
void Weather_ShakeSet(u32 *src, CamShakeRec *view, int priority, float right, float up);
// Quake_Init frees the pool at every 3D load, so across a scene change the caller zeroes
// `*src` instead of stopping it.
void Weather_ShakeStop(u32 *src);

// Center and XZ half-extents of the out-of-bounds box.
void Weather_PlayBox(StageNode *sn, float *cx, float *cz, float *hx, float *hz);
// A uniform XZ point within `frac` of the out-of-bounds box's half-extents.
void Weather_RandBoxPoint(StageNode *sn, float frac, float *x, float *z);

// Events spread over a round, one per equal slice with jitter inside it. The plan is
// drawn on the first call and holds for the rest of the round, so a later preset change
// cannot replay one.
#define WEATHER_SCHEDULE_MAX 12

typedef struct WeatherSchedule
{
    float at[WEATHER_SCHEDULE_MAX];  // round progress each event starts at
    int   num;
    int   next;                      // next unfired entry
    int   planned;
} WeatherSchedule;

// Plans `num` events at progress `p` on the first call, skipping any already behind it.
// Returns 1, consuming the entry, when the next event is due.
int WeatherSchedule_Due(WeatherSchedule *s, int num, float p);

// A camera-following volume: each element has a fixed offset in [0, WEATHER_BOX) per
// axis, and all of them share one drift advanced by the fall and wind velocity, so the
// field re-centres on every camera and can never be outrun.
typedef struct WeatherBox
{
    Vec3 *offset;  // per-element offsets, from the scene heap
    Vec3  drift;
} WeatherBox;

// Allocates and seeds `num` offsets.
void WeatherBox_Seed(WeatherBox *b, int num);
void WeatherBox_Step(WeatherBox *b, float vx, float vy, float vz);

// Element `i` in world space, in the box centred on `eye`. Offset and drift are each in
// [0, WEATHER_BOX), so one subtract folds their sum back into that range.
static inline void WeatherBox_Point(const WeatherBox *b, int i, const Vec3 *eye, Vec3 *out)
{
    float tx = b->offset[i].X + b->drift.X;
    float ty = b->offset[i].Y + b->drift.Y;
    float tz = b->offset[i].Z + b->drift.Z;
    if (tx >= WEATHER_BOX)
        tx -= WEATHER_BOX;
    if (ty >= WEATHER_BOX)
        ty -= WEATHER_BOX;
    if (tz >= WEATHER_BOX)
        tz -= WEATHER_BOX;
    out->X = eye->X + tx - WEATHER_BOX * 0.5f;
    out->Y = eye->Y + ty - WEATHER_BOX * 0.5f;
    out->Z = eye->Z + tz - WEATHER_BOX * 0.5f;
}

// An Enable All / Disable All pair over a pool of 0/1 toggles, reached through the
// action's user_data.
typedef struct WeatherPool
{
    int *enabled;
    int  num;
    void (*on_change)(int val);  // run after either action, or NULL
} WeatherPool;

int WeatherPool_EnableAll(OptionDesc *self);
int WeatherPool_DisableAll(OptionDesc *self);
u32 WeatherPool_Mask(const WeatherPool *pool);

// Create the layer GObj into `*slot` with `cb` as its GX callback. A failure leaves it
// NULL for the caller to retry and warns once.
void WeatherGX_EnsureLayer(GOBJ **slot, void *cb, const char *tag);

// Celestial layers draw fog-free, so distance does not wash them to the fog colour.
void WeatherGX_BeginSky(COBJ *cam, GXBlendFactor dst);
void WeatherGX_EndSky(void);

// Anchor a unit sky direction at P = eye + dir * dist, with dist the smallest of `want`,
// `dome_frac` of the eye-to-dome distance, and `maxd`. Returns that dist, which the
// caller scales apparent size by.
float WeatherGX_PlaceOnDome(const Vec3 *dir, const Vec3 *eye, float e2,
                            float want, float dome_frac, float maxd, Vec3 *P);

// A camera-facing dot of radius `r`: alpha `a` at the centre, fading to 0 at the rim.
void WeatherGX_SoftDot(const Vec3 *P, const Vec3 *R, const Vec3 *U, float r, GXColor rgb, u8 a);

// Load an LOBJ onto a carrier GObj drawn by LObj_GX. NULL on failure.
LOBJ *Weather_LoadLight(LObjDesc *desc);
// Set both colour fields of `l`, if it exists.
void Weather_SetLightColor(LOBJ *l, GXColor c);

#endif // WEATHER_FX_H
