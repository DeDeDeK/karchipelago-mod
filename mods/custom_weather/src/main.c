#include "game.h"
#include "os.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"

#include "custom_weather.h"

extern MenuDesc weather_menu;
extern MenuDesc backdrop_menu;
extern MenuDesc rain_menu;
extern MenuDesc wind_menu;
extern MenuDesc lightning_menu;
extern MenuDesc puddle_menu;
extern OptionDesc event_sky_option;

static void OnBoot(void)
{
    CustomWeather_OnBoot();
    CustomWeatherRuntime_OnBoot();
    CustomBackdrop_OnBoot();
    EventSky_OnBoot();
}

static MenuDesc top_menu = {
    .option_num = 7,
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
};
