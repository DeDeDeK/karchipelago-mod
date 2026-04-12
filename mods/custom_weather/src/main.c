#include "game.h"
#include "os.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"

#include "custom_weather.h"

extern MenuDesc weather_menu;

static void OnBoot(void)
{
    CustomWeather_OnBoot();
}

OptionDesc ModSettings = {
    .name = "City Trial Weather",
    .description = "Toggle which sky/lighting presets can appear in City Trial",
    .kind = OPTKIND_MENU,
    .menu_ptr = &weather_menu,
};

ModDesc mod_desc = {
    .name = "custom_weather",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .option_desc = &ModSettings,
    .OnBoot = OnBoot,
};
