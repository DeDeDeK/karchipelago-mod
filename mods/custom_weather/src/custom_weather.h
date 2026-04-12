#ifndef CUSTOM_WEATHER_H
#define CUSTOM_WEATHER_H

#define WEATHER_VANILLA_NUM  17
#define WEATHER_CUSTOM_NUM   9
#define WEATHER_TOTAL        (WEATHER_VANILLA_NUM + WEATHER_CUSTOM_NUM)

// Preset indices: 0-16 = vanilla (from stage file), 17-22 = custom (appended at runtime)
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
} WeatherKind;

void CustomWeather_OnBoot();

#endif // CUSTOM_WEATHER_H
