#include "game.h"
#include "os.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"

#include "custom_weather.h"

extern MenuDesc weather_menu;
extern MenuDesc backdrop_menu;
extern MenuDesc rain_menu;
extern MenuDesc snow_menu;
extern MenuDesc wind_menu;
extern MenuDesc lightning_menu;
extern MenuDesc puddle_menu;
extern MenuDesc tree_menu;
extern MenuDesc clouds_menu;
extern MenuDesc moon_menu;
extern MenuDesc stars_menu;
extern MenuDesc volcano_menu;
extern MenuDesc tornado_menu;
extern OptionDesc event_sky_option;

static void OnBoot(void)
{
    CustomWeather_OnBoot();
    CustomWeatherRuntime_OnBoot();
    CustomBackdrop_OnBoot();
    EventSky_OnBoot();
}

static void OnFrameEnd(void)
{
    Tornado_OnFrameEnd();
}

static MenuDesc top_menu = {
    .option_num = 14,
    .options = {
        &(OptionDesc){
            .name = "Weather Presets",
            .description = "Toggle which sky/lighting presets can appear in City Trial",
            .kind = OPTKIND_MENU,
            .menu_ptr = &weather_menu,
        },
        &(OptionDesc){
            .name = "Backdrops",
            .description = "Toggle which 3D skybox backdrops can appear in City Trial",
            .kind = OPTKIND_MENU,
            .menu_ptr = &backdrop_menu,
        },
        &(OptionDesc){
            .name = "Rain",
            .description = "Master rain intensity and wind slant for City Trial presets",
            .kind = OPTKIND_MENU,
            .menu_ptr = &rain_menu,
        },
        &(OptionDesc){
            .name = "Snow",
            .description = "Master snow intensity, fall speed, and flutter for City Trial presets",
            .kind = OPTKIND_MENU,
            .menu_ptr = &snow_menu,
        },
        &(OptionDesc){
            .name = "Wind",
            .description = "Wind strength, random direction, and what it affects in City Trial",
            .kind = OPTKIND_MENU,
            .menu_ptr = &wind_menu,
        },
        &(OptionDesc){
            .name = "Lightning",
            .description = "Visible lightning bolts in storm presets (Auto / Off / Force)",
            .kind = OPTKIND_MENU,
            .menu_ptr = &lightning_menu,
        },
        &(OptionDesc){
            .name = "Puddles",
            .description = "Puddle slowdown strength, frequency, size, and disc visibility (Puddles preset)",
            .kind = OPTKIND_MENU,
            .menu_ptr = &puddle_menu,
        },
        &(OptionDesc){
            .name = "Trees",
            .description = "Let wind lean the City Trial forest trees",
            .kind = OPTKIND_MENU,
            .menu_ptr = &tree_menu,
        },
        &(OptionDesc){
            .name = "Clouds",
            .description = "Cloud deck coverage, opacity, size, height, and tint for City Trial presets",
            .kind = OPTKIND_MENU,
            .menu_ptr = &clouds_menu,
        },
        &(OptionDesc){
            .name = "Moon",
            .description = "Moon disc size, brightness, phase, arc, color, and moonlight for City Trial presets",
            .kind = OPTKIND_MENU,
            .menu_ptr = &moon_menu,
        },
        &(OptionDesc){
            .name = "Stars",
            .description = "Starfield density, twinkle, luminosity, size variance, and tint for City Trial presets",
            .kind = OPTKIND_MENU,
            .menu_ptr = &stars_menu,
        },
        &(OptionDesc){
            .name = "Volcano",
            .description = "How often the City Trial volcano erupts, how long it lasts, and what it flings",
            .kind = OPTKIND_MENU,
            .menu_ptr = &volcano_menu,
        },
        &(OptionDesc){
            .name = "Tornado",
            .description = "How often a tornado sweeps City Trial, how long it lasts, and how big and strong it is",
            .kind = OPTKIND_MENU,
            .menu_ptr = &tornado_menu,
        },
        &event_sky_option,
    },
};

OptionDesc ModSettings = {
    .name = "City Trial Sky",
    .description = "Custom weather presets and 3D backdrops for City Trial",
    .kind = OPTKIND_MENU,
    .menu_ptr = &top_menu,
};

ModDesc mod_desc = {
    .name = "custom_weather",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .option_desc = &ModSettings,
    .OnBoot = OnBoot,
    .OnFrameEnd = OnFrameEnd,
};
