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
    // Custom presets (appended at runtime)
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

// Per-preset world-space rain config (CustomPresetDef.rain), drawn by rain.c.
// Numeric fields take 0 = rain.c module default. The horizontal slant is not set
// here - rain.c reads the global wind vector each frame. The global Rain menu
// scales a master intensity over the density (Off disables rain).
typedef struct RainDef
{
    int   enabled;     // 0 = no rain for this preset
    u32   color;       // RGBA8888 streak color; A = opacity. 0 = default
    int   density;     // drops drawn per camera (clamped to the pool cap). 0 = default
    float fall_speed;  // downward speed in world units/frame. 0 = default
    int   line_width;  // GX line width in 1/6-pixel units. 0 = default
    float streak;      // streak length = per-frame velocity * this. 0 = default
} RainDef;

// Per-preset hail config (CustomPresetDef.hail), driven by hail.c: a per-machine
// cloud of icy stones that ride over each machine while the preset's rain is
// active, chipping 1 HP on honest contact with an exposed machine. Hail rides on
// the rain layer, so a hail preset must also enable rain. The global Hail menu's
// "Preset" index resolves to this; the other indices force a global amount.
typedef struct HailDef
{
    int   enabled;   // 0 = no hail for this preset
    float amount;    // density multiplier over the base stone count (1.0 = Normal). 0 = default
} HailDef;

// Per-preset snow config (CustomPresetDef.snow), driven by snow.c: a camera-
// following field of soft round flakes that fall slowly, flutter sideways, and
// drift with the wind (riders pass through). Numeric fields take 0 = snow.c module
// default. The global Snow menu scales a master intensity over the density (Off
// disables snow).
typedef struct SnowDef
{
    int   enabled;     // 0 = no snow for this preset
    u32   color;       // RGBA8888 flake color; A = opacity. 0 = default
    int   density;     // flakes drawn per camera (clamped to the pool cap). 0 = default
    float fall_speed;  // downward speed in world units/frame. 0 = default
    float flutter;     // sideways sway amplitude in world units/frame. 0 = default
    float size;        // flake radius in world units. 0 = default
} SnowDef;

// Visible-bolt mode for a lightning preset (LightningDef.bolt). The flash lights
// terrain; the bolt is GX geometry plus a midpoint point light for nearby riders.
typedef enum LightningBoltMode
{
    LTNG_BOLT_OFF = 0,   // no bolt geometry; the screen flash only (default)
    LTNG_BOLT_AUGMENT,   // draw a bolt AND keep the screen flash
    LTNG_BOLT_REPLACE,   // draw a bolt INSTEAD of the screen flash (terrain stays dim)
} LightningBoltMode;

// Per-preset lightning config (CustomPresetDef.lightning), driven by lightning.c
// as a strike loop (random lulls punctuated by a flash toward flash_color).
// Numeric fields take 0 = lightning.c module default. `bolt` opts into visible
// GX bolts; the global Lightning Bolts menu can force it on/off across presets.
typedef struct LightningDef
{
    int enabled;       // 0 = no lightning for this preset
    u32 flash_color;   // RGBA strike color (fog/EFB/LOBJ tint at peak). 0 = default
    int flash_frames;  // flash envelope length in frames. 0 = default
    int min_lull;      // minimum frames between strikes. 0 = default
    int max_lull;      // maximum frames between strikes. 0 = default
    int bolt;          // LightningBoltMode: 0 = off (default), 1 = augment, 2 = replace
} LightningDef;

// Per-preset wind config (CustomPresetDef.wind): one global horizontal vector
// (wind.c) that slants the rain/hail, blows airborne items, and pushes gliding
// machines. Numeric fields take 0 = wind.c module default. The vector evolves -
// speed pulses (gustiness) and heading wanders (chaos) as smoothed random walks.
typedef struct WindDef
{
    int   enabled;     // 0 = no wind for this preset
    float speed;       // base wind speed, world units/frame. 0 = default
    float heading;     // base compass heading in degrees (0 = +Z, 90 = +X)
    float gustiness;   // 0..1, how much the speed pulses around the base (0 = steady)
    float chaos;       // 0..1, how much the heading wanders over time (0 = fixed)
} WindDef;

// Per-preset ground-puddle config (CustomPresetDef.puddles), driven by puddle.c:
// a field of roaming oval pools on flat ground that damp any grounded machine
// driving through them. Numeric fields take 0 = puddle.c module default. The
// global Puddles menu scales slowdown/frequency/size, toggles roaming, and can
// hide the discs.
typedef struct PuddleDef
{
    int   enabled;      // 0 = no puddles for this preset
    u32   color;        // RGBA8888 disc color; A = center opacity. 0 = default
    int   count;        // number of pools scattered across the play area. 0 = default
    float radius;       // base pool radius in world units (ovals vary around it). 0 = default
    float slow_factor;  // horizontal velocity multiplier/frame while inside (0,1). 0 = default
} PuddleDef;

// Per-preset high cloud-deck config (CustomPresetDef.clouds), driven by clouds.c:
// a low deck of soft translucent spheroid clusters drifting with the wind over
// the map (riders fly through; vision inside is obscured). Numeric fields take
// 0 = clouds.c module default. The global Clouds menu scales coverage/opacity/
// size/variance, offsets the deck height, and can override the tint.
typedef struct CloudDef
{
    int   enabled;     // 0 = no clouds for this preset
    u32   color;       // RGBA8888 cloud color; A = base opacity. 0 = default
    int   count;       // number of clouds in the field (clamped to the cap). 0 = default
    float height;      // absolute deck world Y. 0 = derive from the OOB box (a low deck)
    float height_var;  // +/- world units of per-cloud height spread about the deck. 0 = default
    float size;        // base puff radius in world units. 0 = default
    float size_var;    // 0..1 fractional per-cloud size spread about `size`. 0 = default
    float puff_var;    // 0..1 size variance among the puffs within a cluster. 0 = default
} CloudDef;

// Per-preset starfield config (CustomPresetDef.stars), driven by stars.c: faint
// camera-anchored dots scattered over the sky dome (celestial, like the moon), drawn
// additively as soft glows with per-star size/brightness variance and independent
// twinkling. Numeric fields take 0 = stars.c module default. The global Stars menu
// can force it on/off and override density/twinkle/luminosity/variance/color.
// Shooting-star cadence for a preset (StarDef.shoot). The values map 1:1 onto the
// Shooting Stars menu's forced levels (Off/Rare/Occasional/Frequent); DEFAULT
// leaves a preset with stars but no cadence at the built-in Occasional rate. The
// menu's "Preset" index resolves to whichever level the active preset sets here.
typedef enum ShootFreq
{
    SHOOT_FREQ_DEFAULT = 0,  // Occasional (the built-in cadence)
    SHOOT_FREQ_OFF,
    SHOOT_FREQ_RARE,
    SHOOT_FREQ_OCCASIONAL,
    SHOOT_FREQ_FREQUENT,
} ShootFreq;

typedef struct StarDef
{
    int   enabled;      // 0 = no stars for this preset
    u32   color;        // RGBA8888 star color; A = base brightness. 0 = default
    int   density;      // number of stars scattered on the dome. 0 = default
    float twinkle;      // 0..1 twinkle depth (brightness shimmer). 0 = default
    float luminosity;   // overall brightness scalar. 0 = default
    float size;         // base star radius in world units at the reference distance. 0 = default
    float size_var;     // 0..1 fractional per-star size spread. 0 = default
    int   shoot;        // ShootFreq cadence. 0 = Default (Occasional)
} StarDef;

// Moon phase, selecting how much of the disc is lit and on which side. The
// terminator is a half-ellipse; k = +1 (full) .. 0 (half) .. -1 (new). Full = 0
// so a preset that leaves MoonDef.phase 0 gets a full moon.
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

// Per-preset moon config (CustomPresetDef.moon), driven by moon.c: a distant
// fog-free disc on the sky dome that crosses the sky over the round (synced to
// the City Trial match timer), showing craters and the selected phase. When
// `light` is set it also casts a moonlight LOBJ and suppresses the leftover
// distant stage light so the moon is the dominant directional light. Numeric
// fields take 0 = moon.c module default. The global Moon menu can force it
// on/off and override size/brightness/phase/arc/color.
typedef struct MoonDef
{
    int   enabled;      // 0 = no moon for this preset
    u32   color;        // RGBA8888 disc color; A = opacity. 0 = default
    float size;         // disc radius in world units on the dome. 0 = default
    int   phase;        // MoonPhase. 0 = Full (the default)
    float arc_height;   // peak elevation in degrees as it crosses the sky. 0 = default
    float rise_bearing; // compass bearing (deg) of the rise point. 0 = default
    int   light;        // 1 = cast moonlight LOBJ + suppress the distant sun. 0 = off
    u32   light_color;  // RGBA8888 moonlight color. 0 = default
} MoonDef;

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

    //  Hail. Per-preset icy stones that ride on the rain layer and chip HP.
    // hail.enabled = 0 means no hail for this preset (rain must be enabled too).
    HailDef hail;

    //  World-space snow. Per-preset field of soft flakes that fall slowly and
    // flutter. snow.enabled = 0 means no snow for this preset.
    SnowDef snow;

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

    //  Clouds. Per-preset deck of soft clouds drifting at a low deck over the
    // map. clouds.enabled = 0 means no clouds for this preset.
    CloudDef clouds;

    //  Moon. Per-preset distant disc that crosses the sky over the round and
    // (optionally) lights the scene. moon.enabled = 0 means no moon.
    MoonDef moon;

    //  Stars. Per-preset field of faint twinkling dots on the sky dome.
    // stars.enabled = 0 means no stars.
    StarDef stars;
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

// Driven from the per-frame weather tick: SetActive latches the preset's config
// (NULL or enabled == 0 = off), Tick advances the fall and lazily creates the
// render GObj, Reset drops the cached GObj handle on CT teardown.
void Rain_SetActive(const RainDef *rain);
void Rain_Tick(void);
void Rain_Reset(void);

// Whether rain is active for the live preset; hail.c gates on this.
int Rain_IsActive(void);

// Driven from the per-frame weather tick: SetActive latches the preset's config
// (NULL or enabled == 0 = off), Tick advances the fall/flutter and lazily creates
// the render GObj, Reset drops the cached GObj handle on CT teardown.
void Snow_SetActive(const SnowDef *snow);
void Snow_Tick(void);
void Snow_Reset(void);

// Per-machine clouds of world-space hailstones that ride over each machine while
// rain is active and hail is on, dealing 1 damage on honest contact (a sheltered
// machine's cloud is suppressed). SetActive latches the preset's hail config that
// the Hail menu's "Preset" index resolves to; Tick advances every cloud, Reset
// clears them on CT teardown.
void Hail_SetActive(const HailDef *def);
void Hail_Tick(void);
void Hail_Reset(void);

// Driven from the per-frame weather tick: SetActive latches the preset's config
// (NULL or enabled == 0 = off), Tick advances the strike loop and lerps the
// passed HSD_Fog toward the flash color during a strike, Reset re-arms the timer
// on CT teardown.
void Lightning_SetActive(const LightningDef *def);
void Lightning_Tick(HSD_Fog *fog);
void Lightning_Reset(void);

// The single global wind vector other systems read. Driven from the tick:
// SetActive latches the preset's config (NULL or enabled == 0 = calm) and seeds
// the gust/heading walks, Tick evolves them and applies wind to items and gliding
// machines, Reset clears the wind. Wind_GetVector returns the current horizontal
// vector (Y = 0) for the rain/hail slant.
void Wind_SetActive(const WindDef *def);
void Wind_Tick(void);
void Wind_GetVector(struct Vec3 *out);
void Wind_Reset(void);

// Wind bends the CT forest trees (yakumono desc_id 34). Driven from the tick
// after Wind_Tick: Tree_Tick lazily enumerates the tree joints, caches their
// authored rotations, and leans the intact ones downwind (calm = rigid);
// Tree_Reset drops the cache on CT teardown.
void Tree_Tick(void);
void Tree_Reset(void);

// Driven from the per-frame weather tick: SetActive latches the preset's config
// (NULL or enabled == 0 = off) and arms a fresh round, Tick lazily scatters the
// pools on flat ground, creates the render GObj, and damps machines inside a
// pool, Reset drops the cached GObj handle on CT teardown.
void Puddle_SetActive(const PuddleDef *def);
void Puddle_Tick(void);
void Puddle_Reset(void);

// Driven from the per-frame weather tick after Wind_Tick: SetActive latches the
// preset's config (NULL or enabled == 0 = off), Tick lazily scatters the deck
// over the OOB box and drifts/wraps each cloud (the GX callback draws the
// spheroid clusters), Reset drops the cached GObj handle on CT teardown.
void Cloud_SetActive(const CloudDef *def);
void Cloud_Tick(void);
void Cloud_Reset(void);

// Driven from the per-frame weather tick: SetActive latches the preset's config
// (NULL or enabled == 0 = off) and resolves the menu overrides, Tick creates the
// disc render GObj and drives the moonlight LOBJ (position from the timer-synced
// sky arc; suppresses the leftover distant sun when the moonlight is on), Reset
// drops the cached GObj/LOBJ handles on CT teardown. The GX callback draws the
// fog-free phase disc + craters.
void Moon_SetActive(const MoonDef *def);
void Moon_Tick(void);
void Moon_Reset(void);

// Driven from the per-frame weather tick: SetActive latches the preset's config
// (NULL or enabled == 0 = off) and resolves the menu overrides, Tick lazily scatters
// the field over the sky cap and creates the render GObj (the GX callback draws the
// fog-free additive twinkling dots), Reset drops the cached GObj handle on CT
// teardown.
void Star_SetActive(const StarDef *def);
void Star_Tick(void);
void Star_Reset(void);

#endif // CUSTOM_WEATHER_H
