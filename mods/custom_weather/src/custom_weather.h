#ifndef CUSTOM_WEATHER_H
#define CUSTOM_WEATHER_H

#include "datatypes.h"
#include "structs.h"

#define WEATHER_VANILLA_NUM  17
#define WEATHER_CUSTOM_NUM   13
#define WEATHER_TOTAL        (WEATHER_VANILLA_NUM + WEATHER_CUSTOM_NUM)

// Preset indices: 0-16 = vanilla (from stage file), 17+ = custom (appended at runtime)
typedef enum WeatherKind
{
    // Vanilla presets
    WEATHER_DAY = 0,
    WEATHER_MIDNIGHT,
    WEATHER_LIGHT_FOG,
    WEATHER_DUSK_2,
    WEATHER_DUSKY_CLOUDS,
    WEATHER_DARK_VIGNETTE,
    WEATHER_DAY_2,
    WEATHER_BLUE_SKY,
    WEATHER_PINK_SKY,
    WEATHER_DENSE_FOG,
    WEATHER_FOGGY,
    WEATHER_DUSK,
    WEATHER_NIGHT,
    WEATHER_GRAY_SKY,
    WEATHER_DARK_PURPLE,
    WEATHER_RED_VIGNETTE,
    WEATHER_DARK_LOW_VIS,
    WEATHER_DEEP_BLUE,
    WEATHER_GOLDEN_HOUR,
    WEATHER_BLOOD_RED,
    WEATHER_WHITEOUT,
    WEATHER_TOXIC_GREEN,
    WEATHER_NEON,
    WEATHER_COTTON_CANDY,
    WEATHER_FROZEN_DAWN,
    WEATHER_VOID,
    WEATHER_STORM,
    WEATHER_RAIN,
    WEATHER_BLOOD_RAIN,
    WEATHER_PUDDLES,
} WeatherKind;

// Per-preset world-space rain config (CustomPresetDef.rain). Drawn by rain.c as
// a depth-tested field of GX line streaks that follows the camera, so the rain
// lives in the stage rather than as a flat overlay. The appearance/motion
// fields take 0 = "use the rain.c module default", so a preset can set just
// { enabled, color } and inherit sensible motion. The horizontal slant is not
// configured here - rain.c reads the global wind vector (wind.c) each frame, so
// a preset's WindDef drives both the rain's slant and the rest of the weather.
// The global "City Trial Sky > Rain" menu scales a master intensity over this
// density across every preset (Off disables rain) and can gate the wind slant.
typedef struct RainDef
{
    int   enabled;     // 0 = no rain for this preset
    u32   color;       // RGBA8888 streak color; A = opacity. 0 = default
    int   density;     // drops drawn per camera (clamped to the pool cap). 0 = default
    float fall_speed;  // downward speed in world units/frame. 0 = default
    int   line_width;  // GX line width in 1/6-pixel units. 0 = default
    float streak;      // streak length = per-frame velocity * this. 0 = default
} RainDef;

// Visible-bolt mode for a lightning preset (LightningDef.bolt). The flash (fog/
// EFB/global-light punch) lights terrain; the bolt is GX line geometry plus a
// point light at its midpoint that lights nearby riders. Bolts are occluded by
// stage geometry (depth-tested, like the rain).
typedef enum LightningBoltMode
{
    LTNG_BOLT_OFF = 0,   // no bolt geometry; the screen flash only (default)
    LTNG_BOLT_AUGMENT,   // draw a bolt AND keep the screen flash
    LTNG_BOLT_REPLACE,   // draw a bolt INSTEAD of the screen flash (terrain stays dim)
} LightningBoltMode;

// Per-preset lightning config (CustomPresetDef.lightning). Driven by
// lightning.c as a strike loop: long random lulls punctuated by a brief flash
// that punches the fog/EFB color (and a spare LOBJ) toward flash_color, lighting
// the world. A per-preset layer that composes freely with any preset's
// lighting/fog. The non-enabled fields take 0 = "use the lightning.c module
// default", so a preset can set just { enabled } and inherit a storm cadence.
//
// `bolt` opts the preset into visible bolts (default off = flash only). When on,
// each strike also draws a jagged GX bolt themed to flash_color and lights a
// point LOBJ at its midpoint. The global "Lightning Bolts" menu setting can
// force this on/off across every preset.
typedef struct LightningDef
{
    int enabled;       // 0 = no lightning for this preset
    u32 flash_color;   // RGBA strike color (fog/EFB/LOBJ tint at peak). 0 = default
    int flash_frames;  // flash envelope length in frames. 0 = default
    int min_lull;      // minimum frames between strikes. 0 = default
    int max_lull;      // maximum frames between strikes. 0 = default
    int bolt;          // LightningBoltMode: 0 = off (default), 1 = augment, 2 = replace
} LightningDef;

// Per-preset wind config (CustomPresetDef.wind). Wind is a single global
// horizontal vector owned by wind.c that several systems read each frame: it
// slants the rain (rain.c), blows airborne items sideways, and pushes gliding
// machines. The non-enabled fields take 0 = "use the wind.c module default", so
// a preset can set just { enabled } and inherit a default breeze.
//
// The vector evolves over time: its speed pulses (gustiness) and its heading
// wanders (chaos), both as smoothed random walks around the configured base.
// If the global "Randomize Direction" toggle is on, the base heading is rolled
// at random when the preset activates instead of using `heading`.
typedef struct WindDef
{
    int   enabled;     // 0 = no wind for this preset
    float speed;       // base wind speed, world units/frame. 0 = default
    float heading;     // base compass heading in degrees (0 = +Z, 90 = +X)
    float gustiness;   // 0..1, how much the speed pulses around the base (0 = steady)
    float chaos;       // 0..1, how much the heading wanders over time (0 = fixed)
} WindDef;

// Per-preset ground-puddle config (CustomPresetDef.puddles). Driven by puddle.c:
// the preset runs a field of up to `count` oval pools that fade in and out at
// random spots over time (each placed by raycasting down to find flat ground),
// drawn as depth-tested translucent discs on the world XLU pass; every frame the
// horizontal velocity of any grounded machine inside one is damped by
// slow_factor (scaled by that pool's current opacity) - a self-correcting drag,
// so speed recovers on exit. The non-enabled fields take 0 = "use the puddle.c
// module default", and the global "City Trial Sky > Puddles" menu scales the
// slowdown/frequency/size, toggles roaming, and can hide the discs on top.
typedef struct PuddleDef
{
    int   enabled;      // 0 = no puddles for this preset
    u32   color;        // RGBA8888 disc color; A = center opacity. 0 = default
    int   count;        // number of pools scattered across the play area. 0 = default
    float radius;       // base pool radius in world units (ovals vary around it). 0 = default
    float slow_factor;  // horizontal velocity multiplier/frame while inside (0,1). 0 = default
} PuddleDef;

// Per-custom-preset config. Fields are grouped by what they affect on screen,
// not by the underlying engine mechanism. Color fields are RGBA8888 packed u32
// (high byte=R).
typedef struct CustomPresetDef
{
    int base_preset;             // Vanilla WeatherKind (0..16) to clone unset fields from

    //  Fog: per-pixel distance fog over all world geometry. Also seeds the
    // EFB clear color (the void past fog_end).
    u32   fog_color;             // RGB only; alpha ignored by GX
    float fog_start;             // near distance (vanilla range 1..1300)
    float fog_end;               // far distance

    //  Skybox tint blended over the sky dome.
    u32   sky_color;             // RGB=tint; A=opacity (0=vanilla skybox visible, 255=fully replaced)

    //  Terrain shading (TEV-baked stage geometry, lit by stc_main_light).
    // 0 = inherit from base preset.
    u32   terrain_diffuse;
    u32   terrain_specular;

    //  Character & machine shading (HSD-lit dynamic geometry - riders, vehicles).
    // The AreaLight is the directional key light; the slot-8 ambient LOBJ is the fill.
    u32   char_diffuse;          // AreaLight diffuse
    u32   char_specular;         // AreaLight specular highlight
    struct Vec3 char_dir;        // AreaLight direction
    int   char_dir_lit;          // 1=AreaLight directional shading active, 0=flat fill only
    u32   char_ambient;          // 0=inherit. Slot-8 fill light color
    u32   char_ambient_specular; // 0=inherit. Slot-8 fill specular

    //  Screen overlay (lbfade slot 3, gxlink 3). Tints terrain/sky/fog AFTER
    // the world pass but BEFORE chars/machines (gxlink 5/6) and HUD (gxlink 21).
    // To darken chars/machines, use char_diffuse/char_ambient instead.
    u32   screen_tint;           // RGB=tint, A=strength. 0=no overlay

    //  Fog curve. Selects the GX fog density falloff (HSD_Fog.type). The
    // engine only ever ships linear; exp/exp2 back-load the density (clearer
    // near/mid field, the wall only forms close to fog_end), the reverse
    // variants make fog densest at the camera. 0 = inherit engine default.
    u32   fog_curve;             // WeatherFogCurve

    //  World-space rain. Per-preset config (color, density, speed, wind, etc).
    // rain.enabled = 0 means no rain for this preset.
    RainDef rain;

    //  Lightning. Per-preset strike loop (flash color + cadence).
    // lightning.enabled = 0 means no lightning for this preset.
    LightningDef lightning;

    //  Wind. Per-preset global wind vector (speed/heading/gust/chaos) that
    // slants the rain, blows airborne items, and pushes gliding machines.
    // wind.enabled = 0 means no wind for this preset.
    WindDef wind;

    //  Puddles. Per-preset field of ground pools that drag machines driving
    // over them. puddles.enabled = 0 means no puddles for this preset.
    PuddleDef puddles;
} CustomPresetDef;

// Per-preset fog density curve. Maps to a GXFogType value applied to
// HSD_Fog.type; Sky_Update never touches type, so a single write per preset
// change holds. FOG_CURVE_INHERIT keeps whatever the stage loaded (linear).
typedef enum WeatherFogCurve
{
    FOG_CURVE_INHERIT = 0,
    FOG_CURVE_LINEAR,    // GX_FOG_PERSP_LIN
    FOG_CURVE_EXP,       // GX_FOG_PERSP_EXP
    FOG_CURVE_EXP2,      // GX_FOG_PERSP_EXP2
    FOG_CURVE_REVEXP,    // GX_FOG_PERSP_REVEXP
    FOG_CURVE_REVEXP2,   // GX_FOG_PERSP_REVEXP2
} WeatherFogCurve;

const CustomPresetDef *CustomWeather_GetPresetDef(int weather_kind);
const char *CustomWeather_GetPresetName(int weather_kind);

// Global "Fog Distance" multiplier (HSD_Fog.scale). Applies to every CT
// preset, vanilla and custom: >1 pushes the far fog wall out (clearer), <1
// pulls it in (denser). 1.0 = unchanged. Driven by the settings menu.
float CustomWeather_GetFogScale(void);

void CustomWeather_OnBoot(void);
void CustomWeatherRuntime_OnBoot(void);
void CustomBackdrop_OnBoot(void);
void EventSky_OnBoot(void);

// Driven from the per-frame weather anim tick. Rain_SetActive latches the
// active preset's rain config (NULL or rain->enabled == 0 = off); Rain_Tick
// advances the fall each frame and lazily creates the render GObj; Rain_Reset
// drops the cached GObj handle on City Trial teardown (the engine frees the
// GObj itself).
void Rain_SetActive(const RainDef *rain);
void Rain_Tick(void);
void Rain_Reset(void);

// Whether rain is currently active for the live preset (preset rain enabled and
// the master Rain Intensity not Off). hail.c reads this to gate the hail layer.
int Rain_IsActive(void);

// A per-machine cloud of real world-space hailstones that ride above each
// machine while rain is active and the "Hail" menu (under Rain) is on. Driven
// from the per-frame weather tick: Hail_Tick reads the menu live, seeds/advances
// each machine's stones, and deals 1 damage to a machine on honest stone-vs-body
// contact - unless the machine is sheltered (stage geometry overhead, found by a
// downward cover raycast), in which case its cloud is suppressed. Hail_Reset
// clears the clouds and drops the cached render GObj on City Trial teardown (the
// engine frees the GObj itself). There is no Hail_SetActive - hail holds no
// per-preset state, only the global menu knob.
void Hail_Tick(void);
void Hail_Reset(void);

// Driven from the per-frame weather anim tick. Lightning_SetActive latches the
// active preset's lightning config (NULL or def->enabled == 0 = off);
// Lightning_Tick advances the strike loop each frame, lerping the passed
// HSD_Fog toward the flash color during a strike and lazily creating its LOBJ;
// Lightning_Reset re-arms the timer and drops the cached LOBJ handle on City
// Trial teardown (the engine frees the GObj itself).
void Lightning_SetActive(const LightningDef *def);
void Lightning_Tick(HSD_Fog *fog);
void Lightning_Reset(void);

// The single global wind vector other systems read. Driven from the per-frame
// weather anim tick. Wind_SetActive latches the active preset's wind config
// (NULL or def->enabled == 0 = calm) and seeds the gust/heading state, rolling
// a random base heading if the global "Randomize Direction" toggle is on.
// Wind_Tick advances the gust/heading random walks and applies wind to airborne
// items and gliding machines each frame. Wind_GetVector returns the current
// horizontal wind vector (Y = 0); rain.c reads it for slant. Wind_Reset clears
// the wind on City Trial teardown.
void Wind_SetActive(const WindDef *def);
void Wind_Tick(void);
void Wind_GetVector(struct Vec3 *out);
void Wind_Reset(void);

// Driven from the per-frame weather anim tick. Puddle_SetActive latches the
// active preset's puddle config (NULL or def->enabled == 0 = off) and arms a
// fresh placement for the round; Puddle_Tick lazily scatters the pools on the
// first active frame (raycasting down to flat ground), creates its render GObj,
// and damps the horizontal velocity of every grounded machine inside a pool;
// Puddle_Reset drops the cached GObj handle and the placement on City Trial
// teardown (the engine frees the GObj itself).
void Puddle_SetActive(const PuddleDef *def);
void Puddle_Tick(void);
void Puddle_Reset(void);

#endif // CUSTOM_WEATHER_H
