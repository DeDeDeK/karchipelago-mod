#include "game.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "weather_control.h"

// Called from the hook in place of the vanilla random selection.
// r30 (grobj) is passed via the hook prologue.
void WeatherControl_OverrideSky(GrObj *grobj)
{
    int menu_val = hoshi_menu_settings.weather_control;

    int preset;
    if (menu_val == WEATHER_SHUFFLE)
    {
        // Random from all available presets (WEATHER_NUM - 1 excludes SHUFFLE itself).
        preset = HSD_Randi(WEATHER_NUM - 1);
    }
    else
    {
        // Menu value 1-17 maps directly to preset index 0-16.
        preset = menu_val - 1;
    }

    Sky_SetPresetIndex(grobj, preset);
}

// Hook at 0x8010f1a4: start of the City Trial random selection block in Sky_Init.
// At this point r30 = grobj, r31 = stage data block.
// We handle the entire sky selection and exit past the original setSkyIndex call.
CODEPATCH_HOOKCREATE(0x8010f1a4,
    "mr 3, 30\n\t",
    WeatherControl_OverrideSky,
    "", 0x8010f1d0);

void WeatherControl_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x8010f1a4);
    OSReport("Weather control hook installed at Sky_Init\n");
}
