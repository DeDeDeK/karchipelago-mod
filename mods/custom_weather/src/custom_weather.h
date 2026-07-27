#ifndef CUSTOM_WEATHER_H
#define CUSTOM_WEATHER_H

#include "datatypes.h"
#include "structs.h"

#define WEATHER_VANILLA_NUM  17
#define WEATHER_CUSTOM_NUM   9
#define WEATHER_TOTAL        (WEATHER_VANILLA_NUM + WEATHER_CUSTOM_NUM)

// Preset indices: 0-16 = vanilla (from stage file), 17+ = custom (appended at runtime)
typedef enum WeatherKind
{
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
    WEATHER_BLOOD_RAIN,
    WEATHER_STORM,
    WEATHER_RAIN,
    WEATHER_HAILSTORM,
    WEATHER_SNOWSTORM,
    WEATHER_MOONLIGHT,
    WEATHER_COTTON_CANDY,
    WEATHER_TOXIC,
    WEATHER_BUBBLEGUM,
} WeatherKind;

// Per-preset rain (CustomPresetDef.rain). Numeric fields take 0 = module default;
// the horizontal slant comes from the global wind vector, not from here.
typedef struct RainDef
{
    int   enabled;     // 0 = no rain for this preset
    u32   color;       // RGBA8888 streak color; A = opacity
    int   density;     // drops drawn per camera (clamped to the pool cap)
    float fall_speed;  // downward speed in world units/frame
    int   line_width;  // GX line width in 1/6-pixel units
    float streak;      // streak length = per-frame velocity * this
} RainDef;

// Per-preset hail (CustomPresetDef.hail): icy stones riding over each machine that
// chip 1 HP on contact with an exposed machine. Requires rain.enabled.
typedef struct HailDef
{
    int   enabled;   // 0 = no hail for this preset
    float amount;    // density multiplier over the base stone count (1.0 = Normal)
} HailDef;

// Per-preset snow (CustomPresetDef.snow): soft flakes that fall slowly, flutter
// sideways, and drift with the wind. Numeric fields take 0 = module default.
typedef struct SnowDef
{
    int   enabled;     // 0 = no snow for this preset
    u32   color;       // RGBA8888 flake color; A = opacity
    int   density;     // flakes drawn per camera (clamped to the pool cap)
    float fall_speed;  // downward speed in world units/frame
    float flutter;     // sideways sway amplitude in world units/frame
    float size;        // flake radius in world units
} SnowDef;

// Visible-bolt mode for a lightning preset (LightningDef.bolt). The flash lights
// terrain; the bolt is GX geometry plus a midpoint point light for nearby riders.
typedef enum LightningBoltMode
{
    LTNG_BOLT_OFF = 0,   // no bolt geometry; the screen flash only (default)
    LTNG_BOLT_AUGMENT,   // draw a bolt AND keep the screen flash
    LTNG_BOLT_REPLACE,   // draw a bolt INSTEAD of the screen flash (terrain stays dim)
} LightningBoltMode;

// Per-preset lightning (CustomPresetDef.lightning): random lulls punctuated by a
// flash toward flash_color. Numeric fields take 0 = module default.
typedef struct LightningDef
{
    int enabled;       // 0 = no lightning for this preset
    u32 flash_color;   // RGBA strike color (fog/EFB/LOBJ tint at peak)
    int flash_frames;  // flash envelope length in frames
    int min_lull;      // minimum frames between strikes
    int max_lull;      // maximum frames between strikes
    int bolt;          // LightningBoltMode: 0 = off (default), 1 = augment, 2 = replace
} LightningDef;

// Per-preset wind (CustomPresetDef.wind): one global horizontal vector that slants
// precipitation, blows airborne items, and pushes gliding machines. Speed and
// heading evolve as smoothed random walks. Numeric fields take 0 = module default.
typedef struct WindDef
{
    int   enabled;     // 0 = no wind for this preset
    float speed;       // base wind speed, world units/frame
    float heading;     // base compass heading in degrees (0 = +Z, 90 = +X)
    float gustiness;   // 0..1, how much the speed pulses around the base (0 = steady)
    float chaos;       // 0..1, how much the heading wanders over time (0 = fixed)
} WindDef;

// Per-preset puddles (CustomPresetDef.puddles): roaming oval pools on flat ground
// that damp grounded machines driving through them. 0 = module default.
typedef struct PuddleDef
{
    int   enabled;      // 0 = no puddles for this preset
    u32   color;        // RGBA8888 disc color; A = center opacity
    int   count;        // number of pools scattered across the play area
    float radius;       // base pool radius in world units (ovals vary around it)
    float slow_factor;  // horizontal velocity multiplier/frame while inside (0,1)
} PuddleDef;

// Per-preset cloud deck (CustomPresetDef.clouds): a low deck of soft translucent
// spheroid clusters drifting with the wind, which riders fly through. Numeric
// fields take 0 = module default.
typedef struct CloudDef
{
    int   enabled;     // 0 = no clouds for this preset
    u32   color;       // RGBA8888 cloud color; A = base opacity
    int   count;       // number of clouds in the field (clamped to the cap)
    float height;      // absolute deck world Y. 0 = derive from the OOB box (a low deck)
    float height_var;  // +/- world units of per-cloud height spread about the deck
    float size;        // base puff radius in world units
    float size_var;    // 0..1 fractional per-cloud size spread about `size`
    float puff_var;    // 0..1 size variance among the puffs within a cluster
} CloudDef;

// Shooting-star cadence for a preset (StarDef.shoot).
typedef enum ShootFreq
{
    SHOOT_FREQ_DEFAULT = 0,  // Occasional (the built-in cadence)
    SHOOT_FREQ_OFF,
    SHOOT_FREQ_RARE,
    SHOOT_FREQ_OCCASIONAL,
    SHOOT_FREQ_FREQUENT,
} ShootFreq;

// Per-preset starfield (CustomPresetDef.stars): faint camera-anchored dots on the
// sky dome, drawn additively as soft twinkling glows. 0 = module default.
typedef struct StarDef
{
    int   enabled;      // 0 = no stars for this preset
    u32   color;        // RGBA8888 star color; A = base brightness
    int   density;      // number of stars scattered on the dome
    float twinkle;      // 0..1 twinkle depth (brightness shimmer)
    float luminosity;   // overall brightness scalar
    float size;         // base star radius in world units at the reference distance
    float size_var;     // 0..1 fractional per-star size spread
    int   shoot;        // ShootFreq cadence. 0 = Default (Occasional)
} StarDef;

// Moon phase (MoonDef.phase): how much of the disc is lit and on which side.
// Full = 0, so a preset that leaves the field unset gets a full moon.
typedef enum MoonPhase
{
    MOON_FULL = 0,
    MOON_WAXING_CRESCENT,   // thin sliver, lit on the right
    MOON_FIRST_QUARTER,     // right half lit
    MOON_WAXING_GIBBOUS,    // most lit, dark crescent on the left
    MOON_WANING_GIBBOUS,    // most lit, dark crescent on the right
    MOON_LAST_QUARTER,      // left half lit
    MOON_WANING_CRESCENT,   // thin sliver, lit on the left
    MOON_NEW,               // fully dark (not drawn)
} MoonPhase;

// Per-preset moon (CustomPresetDef.moon): a distant fog-free disc on the sky dome
// that crosses the sky over the round, synced to the City Trial match timer.
// Numeric fields take 0 = module default.
typedef struct MoonDef
{
    int   enabled;      // 0 = no moon for this preset
    u32   color;        // RGBA8888 disc color; A = opacity
    float size;         // disc radius in world units on the dome
    int   phase;        // MoonPhase. 0 = Full (the default)
    float arc_height;   // peak elevation in degrees as it crosses the sky
    float rise_bearing; // compass bearing (deg) of the rise point
    int   light;        // 1 = cast moonlight LOBJ + suppress the distant sun. 0 = off
    u32   light_color;  // RGBA8888 moonlight color
} MoonDef;

// Per-custom-preset config. Color fields are RGBA8888 packed u32 (high byte = R).
typedef struct CustomPresetDef
{
    int base_preset;             // Vanilla WeatherKind (0..16) to clone unset fields from

    // Fog also seeds the EFB clear color (the void past fog_end).
    u32   fog_color;             // RGB only; alpha ignored by GX
    float fog_start;             // near distance (vanilla range 1..1300)
    float fog_end;               // far distance

    u32   sky_color;             // RGB=tint; A=opacity (0=vanilla skybox visible, 255=fully replaced)

    // Terrain shading: TEV-baked stage geometry lit by stc_main_light.
    u32   terrain_diffuse;       // 0 = inherit from base preset
    u32   terrain_specular;      // 0 = inherit from base preset

    // Character/machine shading: the AreaLight is the directional key light,
    // the slot-8 ambient LOBJ is the fill.
    u32   char_diffuse;          // AreaLight diffuse
    u32   char_specular;         // AreaLight specular highlight
    struct Vec3 char_dir;        // AreaLight direction
    int   char_dir_lit;          // 1=AreaLight directional shading active, 0=flat fill only
    u32   char_ambient;          // 0=inherit. Slot-8 fill light color
    u32   char_ambient_specular; // 0=inherit. Slot-8 fill specular

    // Screen overlay on lbfade slot 3 (gxlink 3): tints terrain/sky/fog after the
    // world pass but before chars/machines (gxlink 5/6) and the HUD (gxlink 21).
    u32   screen_tint;           // RGB=tint, A=strength. 0=no overlay

    u32   fog_curve;             // WeatherFogCurve. 0 = inherit engine default

    RainDef rain;
    HailDef hail;
    SnowDef snow;
    LightningDef lightning;
    WindDef wind;
    PuddleDef puddles;
    CloudDef clouds;
    MoonDef moon;
    StarDef stars;
} CustomPresetDef;

// Per-preset fog density curve, applied to HSD_Fog.type as a GXFogType.
// FOG_CURVE_INHERIT keeps whatever the stage loaded (linear).
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

// Global HSD_Fog.scale multiplier for every CT preset, vanilla and custom:
// >1 pushes the far fog wall out, <1 pulls it in. 1.0 = unchanged.
float CustomWeather_GetFogScale(void);

void CustomWeather_OnBoot(void);
void CustomWeatherRuntime_OnBoot(void);
void CustomBackdrop_OnBoot(void);
void EventSky_OnBoot(void);

// Effect layers driven from the per-frame weather tick: SetActive latches the
// preset's config (NULL or enabled == 0 = off), Tick advances the layer and
// lazily creates its render GObj, Reset drops per-stage state on CT teardown.
void Rain_SetActive(const RainDef *rain);
void Rain_Tick(void);
void Rain_Reset(void);

// Whether rain is active for the live preset; hail requires it.
int Rain_IsActive(void);

void Snow_SetActive(const SnowDef *snow);
void Snow_Tick(void);
void Snow_Reset(void);

void Hail_SetActive(const HailDef *def);
void Hail_Tick(void);
void Hail_Reset(void);

// Lightning_Tick lerps the passed HSD_Fog toward the flash color during a strike.
void Lightning_SetActive(const LightningDef *def);
void Lightning_Tick(HSD_Fog *fog);
void Lightning_Reset(void);

void Wind_SetActive(const WindDef *def);
void Wind_Tick(void);
// The current horizontal wind vector (Y = 0) that the other layers read.
void Wind_GetVector(struct Vec3 *out);
void Wind_Reset(void);

// Trees have no per-preset config: the wind leans the intact CT forest trees
// (yakumono desc_id 34) downwind.
void Tree_Tick(void);
void Tree_Reset(void);

void Puddle_SetActive(const PuddleDef *def);
void Puddle_Tick(void);
void Puddle_Reset(void);

void Cloud_SetActive(const CloudDef *def);
void Cloud_Tick(void);
void Cloud_Reset(void);

void Moon_SetActive(const MoonDef *def);
void Moon_Tick(void);
void Moon_Reset(void);

void Star_SetActive(const StarDef *def);
void Star_Tick(void);
void Star_Reset(void);

#endif // CUSTOM_WEATHER_H
