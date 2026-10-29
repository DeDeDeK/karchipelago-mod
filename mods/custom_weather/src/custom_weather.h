#ifndef CUSTOM_WEATHER_H
#define CUSTOM_WEATHER_H

#include "datatypes.h"
#include "structs.h"
#include "stage.h"

// The custom presets take the indices of the two event skies, which the extended
// table drops.
#define WEATHER_VANILLA_NUM  CITYSKY_METEOR

typedef enum WeatherKind
{
    WEATHER_BLOOD_RAIN = WEATHER_VANILLA_NUM,
    WEATHER_STORM,
    WEATHER_RAIN,
    WEATHER_HAILSTORM,
    WEATHER_SNOWSTORM,
    WEATHER_MOONLIGHT,
    WEATHER_COTTON_CANDY,
    WEATHER_TOXIC,
    WEATHER_BUBBLEGUM,
    WEATHER_VOLCANIC,
    WEATHER_TORNADO,
    WEATHER_EARTHQUAKE,
    WEATHER_TSUNAMI,
    WEATHER_BEACON,
    WEATHER_TOTAL,
} WeatherKind;

#define WEATHER_CUSTOM_NUM   (WEATHER_TOTAL - WEATHER_VANILLA_NUM)

// Falling line streaks. The horizontal slant comes from the global wind vector, not
// from here.
typedef struct RainDef
{
    int   enabled;     // 0 = no rain for this preset
    u32   color;       // RGBA8888 streak color; A = opacity
    int   density;     // drops drawn per camera (clamped to the pool cap)
    float fall_speed;  // downward speed in world units/frame
    int   line_width;  // GX line width in 1/6-pixel units
    float streak;      // streak length = per-frame velocity * this
} RainDef;

// Icy stones riding over each machine that chip 1 HP on contact. Requires rain.
typedef struct HailDef
{
    int   enabled;   // 0 = no hail for this preset
    float amount;    // density multiplier over the base stone count (1.0 = Normal)
} HailDef;

// Soft flakes that fall slowly, flutter sideways, and drift with the wind.
typedef struct SnowDef
{
    int   enabled;     // 0 = no snow for this preset
    u32   color;       // RGBA8888 flake color; A = opacity
    int   density;     // flakes drawn per camera (clamped to the pool cap)
    float fall_speed;  // downward speed in world units/frame
    float flutter;     // sideways sway amplitude in world units/frame
    float size;        // flake radius in world units
} SnowDef;

// Random lulls punctuated by a flash toward flash_color.
typedef struct LightningDef
{
    int enabled;       // 0 = no lightning for this preset
    u32 flash_color;   // RGBA strike color (fog/EFB/LOBJ tint at peak)
    int flash_frames;  // flash envelope length in frames
    int min_lull;      // minimum frames between strikes
    int max_lull;      // maximum frames between strikes
    int bolt;          // 1 = draw a visible bolt with every flash
} LightningDef;

// One global horizontal vector that slants precipitation, blows airborne items, and
// pushes gliding machines. Speed and heading evolve as smoothed random walks.
typedef struct WindDef
{
    int   enabled;     // 0 = no wind for this preset
    float speed;       // base wind speed, world units/frame
    float heading;     // base compass heading in degrees (90 = +X; write +Z as 360)
    float gustiness;   // 0..1, how much the speed pulses around the base
    float chaos;       // 0..1, how much the heading wanders over time
} WindDef;

// Roaming oval pools on flat ground that damp machines driving through them.
typedef struct PuddleDef
{
    int   enabled;      // 0 = no puddles for this preset
    u32   color;        // RGBA8888 disc color; A = center opacity
    int   count;        // number of pools scattered across the play area
    float radius;       // base pool radius in world units (ovals vary around it)
    float slow_factor;  // horizontal velocity multiplier/frame while inside (0,1)
} PuddleDef;

// A low deck of soft translucent spheroid clusters drifting with the wind, which
// riders fly through.
typedef struct CloudDef
{
    int   enabled;     // 0 = no clouds for this preset
    u32   color;       // RGBA8888 cloud color; A = base opacity
    int   count;       // number of clouds in the field (clamped to the cap)
    float height;      // absolute deck world Y; 0 = 35% up the OOB box
    float height_var;  // +/- world units of per-cloud height spread about the deck
    float size;        // base puff radius in world units
    float size_var;    // 0..1 fractional per-cloud size spread about `size`
    float puff_var;    // 0..1 size variance among the puffs within a cluster
} CloudDef;

// Indices line up with the shooting-star Frequency menu.
typedef enum ShootFreq
{
    SHOOT_FREQ_DEFAULT = 0,  // Occasional (the built-in cadence)
    SHOOT_FREQ_OFF,
    SHOOT_FREQ_RARE,
    SHOOT_FREQ_OCCASIONAL,
    SHOOT_FREQ_FREQUENT,
    SHOOT_FREQ_NUM,
} ShootFreq;

// Faint dots on the sky dome, drawn additively as twinkling glows.
typedef struct StarDef
{
    int   enabled;      // 0 = no stars for this preset
    u32   color;        // RGBA8888 star color; A = base brightness
    int   density;      // number of stars scattered on the dome
    float twinkle;      // 0..1 twinkle depth (brightness shimmer)
    float luminosity;   // overall brightness scalar
    float size;         // base star radius in world units at the reference distance
    float size_var;     // 0..1 fractional per-star size spread
    int   shoot;        // ShootFreq cadence
} StarDef;

// How much of the disc is lit and on which side. Full = 0, so a preset that leaves
// the field unset gets a full moon.
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
    MOON_PHASE_NUM,
} MoonPhase;

// A distant fog-free disc that crosses the sky over the round, synced to the match
// timer.
typedef struct MoonDef
{
    int   enabled;      // 0 = no moon for this preset
    u32   color;        // RGBA8888 disc color; A = opacity
    float size;         // disc radius in world units on the dome
    int   phase;        // MoonPhase
    float arc_height;   // peak elevation in degrees as it crosses the sky
    float rise_bearing; // compass bearing (deg) of the rise point
    int   light;        // 1 = cast a moonlight LOBJ and zero the secondary stage light
    u32   light_color;  // RGBA8888 moonlight color
} MoonDef;

// Which copy-ability projectiles an eruption flings. Chaos rolls a fresh theme for
// every projectile. The roster is limited to the kinds that survive an ownerless
// spawn, since a volcano projectile has no owner rider.
typedef enum VolcanoTheme
{
    VOLC_THEME_DEFAULT = 0,  // resolves to Fire
    VOLC_THEME_FIRE,         // Fire ability bullets - burst on any surface
    VOLC_THEME_PLASMA,       // plasma shots and spread shots
    VOLC_THEME_BOMB,         // bombs and sensor mines
    VOLC_THEME_STAR,         // charged spit stars
    VOLC_THEME_CHAOS,
} VolcanoTheme;

// The City Trial volcano periodically erupts, launching themed projectiles on
// ballistic arcs from its mouth, spread across the round by the match timer.
typedef struct VolcanoDef
{
    int   enabled;    // 0 = the volcano stays dormant for this preset
    int   theme;      // VolcanoTheme
    int   eruptions;  // eruptions per round, spread over the match timer
    int   duration;   // frames one eruption lasts
    int   interval;   // frames between volleys during an eruption
    int   burst;      // projectiles launched per volley
    float power;      // launch speed scalar
    float spread;     // 0..1 cone width off vertical
} VolcanoDef;

// A funnel that wanders the city, drawing loose items, breakable props and parked
// machines into an orbit around its core and dragging at ridden machines that stray
// too close.
typedef struct TornadoDef
{
    int   enabled;   // 0 = no tornado for this preset
    int   count;     // tornadoes per round, spread over the match timer
    int   duration;  // frames one tornado lasts
    float size;      // scales the core, reach, height and model
    float strength;  // pull/spin scalar; also how hard a caught rider is dragged
    float speed;     // how fast the funnel wanders, world units/frame
} TornadoDef;

// Quakes that rumble every view and tear cracks in the ground, which close again as
// the quake settles. Anything over an open crack falls through it.
typedef struct EarthquakeDef
{
    int   enabled;    // 0 = no earthquakes for this preset
    int   count;      // quakes per round, spread over the match timer
    int   duration;   // frames one quake lasts, cracks included
    int   cracks;     // cracks torn open per quake
    float size;       // scales crack length and width
    float intensity;  // shake amplitude scalar
} EarthquakeDef;

// Sets of translucent waves that roll in off the sea and across the city, carrying
// whatever they pass over and dissipating as they go.
typedef struct TsunamiDef
{
    int   enabled;   // 0 = no tsunamis for this preset
    u32   color;     // RGBA8888 water colour; A = opacity
    int   count;     // tsunamis per round, spread over the match timer
    int   waves;     // waves per tsunami
    float size;      // scales wave height, depth and front width
    float strength;  // how hard a wave carries what it hits
    float speed;     // world units/frame the waves travel
} TsunamiDef;

// A pool of light on the ground under each player's machine, over a city darkened to
// `ambient`. Both reach the map's own materials only; riders, machines and items keep
// their lighting.
typedef struct GlowDef
{
    int   enabled;    // 0 = no glow for this preset
    u32   ambient;    // RGB the unlit city is scaled to (255 = as baked); 0 = as baked
    u32   color;      // RGB glow colour; 0 = each player's colour
    float radius;     // pool radius in world units
    float intensity;  // brightness scalar at the pool's centre
} GlowDef;

// A point light at the star pole's star, shadowed by the terrain, on the map's own
// materials only. The darkness around it is the glow def's `ambient`.
typedef struct StarLightDef
{
    int   enabled;    // 0 = no star light for this preset
    u32   color;      // RGB light colour; 0 = gold
    float range;      // world units the light reaches
    float intensity;  // brightness scalar under the star
} StarLightDef;

// Per-custom-preset config. Colors are RGBA8888 packed u32 (high byte = R). The fog,
// sky and AreaLight fields are written into the preset entry as they are, 0 included;
// a 0 terrain light field keeps the stage light's own colour. In a nested layer def,
// enabled = 0 turns the layer off and a 0 numeric field takes the layer's default.
typedef struct CustomPresetDef
{
    int base_preset;             // vanilla WeatherKind whose entry the custom one copies

    // Fog also seeds the EFB clear color (the void past fog_end).
    u32   fog_color;             // RGB only; alpha ignored by GX
    float fog_start;             // near distance (vanilla range 1..1300)
    float fog_end;               // far distance

    u32   sky_color;             // RGB=tint; A=opacity (0=vanilla skybox visible, 255=fully replaced)

    // The stage's primary light chain: its INFINITE light (*stc_main_light) and its
    // ambient. They reach only the stage's few lit surfaces; the city's baked vertex
    // colours ignore every light.
    u32   terrain_diffuse;
    u32   terrain_specular;
    u32   terrain_ambient;
    u32   terrain_ambient_specular;

    // The AreaLight, the key light on riders and machines.
    u32   char_diffuse;
    u32   char_specular;
    struct Vec3 char_dir;
    int   char_dir_lit;          // 1=AreaLight directional shading active, 0=flat fill only

    // Screen overlay on lbfade slot 3: tints terrain/sky/fog after the world pass
    // but before chars/machines and the HUD.
    u32   screen_tint;           // RGB=tint, A=strength. 0=no overlay

    int   fog_type;              // GXFogType for HSD_Fog.type; 0 keeps the stage's linear fog

    RainDef rain;
    HailDef hail;
    SnowDef snow;
    LightningDef lightning;
    WindDef wind;
    PuddleDef puddles;
    CloudDef clouds;
    MoonDef moon;
    StarDef stars;
    VolcanoDef volcano;
    TornadoDef tornado;
    EarthquakeDef earthquake;
    TsunamiDef tsunami;
    GlowDef glow;
    StarLightDef starlight;
} CustomPresetDef;

const CustomPresetDef *CustomWeather_GetPresetDef(int weather_kind);
const char *CustomWeather_GetPresetName(int weather_kind);

// Global HSD_Fog.scale multiplier for every CT preset, vanilla and custom:
// >1 pushes the far fog wall out, <1 pulls it in. 1.0 = unchanged.
float CustomWeather_GetFogScale(void);

// The Backdrop Distance multiplier on every backdrop's root scale, Vanilla included.
float CustomBackdrop_GetDistance(void);

void CustomWeather_OnBoot(void);
void CustomWeatherRuntime_OnBoot(void);
void CustomBackdrop_OnBoot(void);
void EventSky_OnBoot(void);

// Run at each 3D round's load and exit.
void CustomWeatherRuntime_Reset(void);
void CustomBackdrop_On3DExit(void);

// Effect layers driven from the per-frame weather tick. SetActive latches the
// preset's config on a preset change; Tick advances the layer and lazily creates its
// render GObj; Reset drops per-round state at each round's load. Layers the menu can
// force on over a dormant preset (hail, moon, stars, volcano, tornado, earthquake,
// tsunami, glow) latch their config whether or not the preset enabled them.
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
// The wind speed, with its downwind unit direction in (dx, dz), or 0 with no direction.
float Wind_GetHeading(float *dx, float *dz);
void Wind_Reset(void);

// Trees have no per-preset config: the wind leans the intact CT forest trees
// downwind.
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

void Volcano_SetActive(const VolcanoDef *def);
void Volcano_Tick(void);
void Volcano_Reset(void);

void Tornado_SetActive(const TornadoDef *def);
void Tornado_Tick(void);
void Tornado_Reset(void);

void Earthquake_OnBoot(void);
void Earthquake_SetActive(const EarthquakeDef *def);
void Earthquake_Tick(void);
void Earthquake_Reset(void);

void Tsunami_SetActive(const TsunamiDef *def);
void Tsunami_Tick(void);
void Tsunami_Reset(void);

void Glow_SetActive(const GlowDef *def);
void Glow_Tick(void);
void Glow_Reset(void);
void Glow_OnBoot(void);

void StarLight_SetActive(const StarLightDef *def);
void StarLight_Tick(void);
void StarLight_Reset(void);
int  StarLight_IsActive(void);

// What the map draw needs from the star light while it is lit.
typedef struct StarDraw
{
    TOBJ *light;  // light map: RGB the light, alpha each column's lit height
    TOBJ *ramp;   // height ramp for the shadow test, NULL with shadows off
    int kcsel;    // konst colour selection that fades the light
    int hw;       // the light is the star's GX light on COLOR1, not the light map's RGB
} StarDraw;
// Fills `out` and returns 1 while the star is lit, 0 while it is dark.
int  StarLight_GetDraw(StarDraw *out);
// Loads the star's GX light for the camera being drawn.
void StarLight_LoadLight(void);
// COLOR1 lit by the star's GX light alone, through HSD's channel cache.
void StarLight_SetupChannel(void);
// The star pole's star joint, which keeps its own look under the light map; NULL once
// a busted star's pole is gone.
JOBJ *StarLight_StarJoint(void);

// Rebuilds the display lists of every rigid POBJ under `root` but `skip` with a normal
// per vertex. The POBJs given normals, or -1 when memory ran out partway.
int MapNormals_Add(JOBJ *root, JOBJ *skip);

#endif // CUSTOM_WEATHER_H
