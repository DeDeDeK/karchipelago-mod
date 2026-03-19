#ifndef ARCHIPELAGO_WEATHER_CONTROL_H
#define ARCHIPELAGO_WEATHER_CONTROL_H

typedef enum WeatherKind
{
    WEATHER_SHUFFLE = 0,    // Random from all presets
    WEATHER_DAY,            // Sky preset index 0  — Day
    WEATHER_MIDNIGHT,       // Sky preset index 1  — Midnight
    WEATHER_LIGHT_FOG,      // Sky preset index 2  — Light Fog
    WEATHER_DUSK_2,         // Sky preset index 3  — Dusk 2
    WEATHER_DUSKY_CLOUDS,   // Sky preset index 4  — Dusky Clouds
    WEATHER_DARK_VIGNETTE,  // Sky preset index 5  — Dark Vignette
    WEATHER_DAY_2,          // Sky preset index 6  — Day 2
    WEATHER_BLUE_SKY,       // Sky preset index 7  — Blue Sky
    WEATHER_PINK_SKY,       // Sky preset index 8  — Pink Sky
    WEATHER_DENSE_FOG,      // Sky preset index 9  — Dense Fog
    WEATHER_FOGGY,          // Sky preset index 10 — Foggy
    WEATHER_DUSK,           // Sky preset index 11 — Dusk
    WEATHER_NIGHT,          // Sky preset index 12 — Night
    WEATHER_GRAY_SKY,       // Sky preset index 13 — Gray Sky
    WEATHER_DARK_PURPLE,    // Sky preset index 14 — Dark Purple
    WEATHER_RED_VIGNETTE,   // Sky preset index 15 — Red Vignette
    WEATHER_DARK_LOW_VIS,   // Sky preset index 16 — Dark Low Vis
    WEATHER_NUM,
} WeatherKind;

void WeatherControl_OnBoot();

#endif // ARCHIPELAGO_WEATHER_CONTROL_H
