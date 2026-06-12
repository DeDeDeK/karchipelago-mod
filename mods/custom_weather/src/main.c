#include "game.h"
#include "os.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"

#include "custom_weather.h"

extern MenuDesc weather_menu;
extern MenuDesc backdrop_menu;

static void OnBoot(void)
{
    CustomWeather_OnBoot();
    CustomWeatherAnim_OnBoot();
    CustomBackdrop_OnBoot();
}

static MenuDesc top_menu = {
    .option_num = 2,
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
