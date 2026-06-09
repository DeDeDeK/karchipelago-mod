#ifndef CUSTOM_WEATHER_H
#define CUSTOM_WEATHER_H

#include "datatypes.h"

#define WEATHER_VANILLA_NUM  17
#define WEATHER_CUSTOM_NUM   12
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
    // Custom presets (appended to stage file array at runtime)
    WEATHER_DEEP_BLUE,
    WEATHER_GOLDEN_HOUR,
    WEATHER_BLOOD_RED,
    WEATHER_WHITEOUT,
    WEATHER_TOXIC_GREEN,
    WEATHER_NEON,
    WEATHER_COTTON_CANDY,
    WEATHER_FROZEN_DAWN,
    WEATHER_VOID,
    // Animated prototypes — exercise terrain re-tint, fog modulation, extra LOBJ.
    WEATHER_STORM,
    WEATHER_AURORA,
    WEATHER_INFERNO,
} WeatherKind;

// Per-preset animation kind. Applied each frame by CustomWeatherAnim_PerFrame
// using params packed into anim_param.
typedef enum WeatherAnimKind
{
    ANIM_NONE = 0,
    // Lightning: fog/EFB-clear flash plus extra LOBJ pulse.
    //   anim_param = (rgba << 0) — flash color; period/duration hardcoded.
    ANIM_LIGHTNING,
    // Aurora: extra LOBJ slowly cycles through a hue range overhead.
    //   anim_param unused; the cycle is fixed green→cyan→violet.
    ANIM_AURORA,
    // Pulse fog: sinusoidal modulation of fog_start/end around the preset values.
    //   anim_param = amplitude in fixed-point (e.g. 80 = ±80 distance units).
    ANIM_PULSE_FOG,
} WeatherAnimKind;

// Per-custom-preset config. Fields are grouped by what they affect on screen,
// not by the underlying engine mechanism. Color fields are RGBA8888 packed u32
// (high byte=R).
typedef struct CustomPresetDef
{
    int base_preset;             // Vanilla WeatherKind (0..16) to clone unset fields from

    // ── Fog: per-pixel distance fog over all world geometry. Also seeds the
    // EFB clear color (the void past fog_end).
    u32   fog_color;             // RGB only; alpha ignored by GX
    float fog_start;             // near distance (vanilla range 1..1300)
    float fog_end;               // far distance

    // ── Skybox tint blended over the sky dome.
    u32   sky_color;             // RGB=tint; A=opacity (0=vanilla skybox visible, 255=fully replaced)

    // ── Terrain shading (TEV-baked stage geometry, lit by stc_main_light).
    // 0 = inherit from base preset.
    u32   terrain_diffuse;
    u32   terrain_specular;

    // ── Character & machine shading (HSD-lit dynamic geometry — riders, vehicles).
    // The AreaLight is the directional key light; the slot-8 ambient LOBJ is the fill.
    u32   char_diffuse;          // AreaLight diffuse
    u32   char_specular;         // AreaLight specular highlight
    struct Vec3 char_dir;        // AreaLight direction
    int   char_dir_lit;          // 1=AreaLight directional shading active, 0=flat fill only
    u32   char_ambient;          // 0=inherit. Slot-8 fill light color
    u32   char_ambient_specular; // 0=inherit. Slot-8 fill specular

    // ── Screen overlay (lbfade slot 3, gxlink 3). Tints terrain/sky/fog AFTER
    // the world pass but BEFORE chars/machines (gxlink 5/6) and HUD (gxlink 21).
    // To darken chars/machines, use char_diffuse/char_ambient instead.
    u32   screen_tint;           // RGB=tint, A=strength. 0=no overlay

    // ── Per-frame animation
    u32   anim_kind;             // WeatherAnimKind
    u32   anim_param;            // Animation-specific packed param
} CustomPresetDef;

const CustomPresetDef *CustomWeather_GetPresetDef(int weather_kind);
const char *CustomWeather_GetPresetName(int weather_kind);

void CustomWeather_OnBoot(void);
void CustomWeatherAnim_OnBoot(void);
void CustomBackdrop_OnBoot(void);

#endif // CUSTOM_WEATHER_H
