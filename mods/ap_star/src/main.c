#include "os.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"

#include "ap_star.h"
#include "ap_star_pieces.h"
#include "ap_star_shot.h"

// Hoshi's Mod_CopyFromSave overwrites these later if a saved hash exists.
ApStarSettings ap_star_settings = {
    .shot_enabled = 1,
};

static const char *stc_off_on[] = {"Off", "On"};

static void OnToggleShot(int val)
{
    OSReport("[ApStar] Star Shot toggled %s\n", stc_off_on[val]);
}

static MenuDesc stc_top_menu = {
    .option_num = 1,
    .options = {
        &(OptionDesc){
            .name = "Star Shot",
            .description = "Archipelago Star fires one of its spheres on a full-charge release",
            .kind = OPTKIND_VALUE,
            .val = &ap_star_settings.shot_enabled,
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleShot,
        },
    },
};

static OptionDesc ModSettings = {
    .name = "Archipelago Star",
    .description = "The Archipelago Star machine and its City Trial assembly",
    .kind = OPTKIND_MENU,
    .menu_ptr = &stc_top_menu,
};

static void OnBoot(void)
{
    ApStarPieces_OnBoot();
    ApStarShot_OnBoot();
    ApStar_ExportApi();
}

static void On3DLoadStart(void)
{
    ApStarPieces_On3DLoadStart();
}

static void On3DLoadEnd(void)
{
    ApStarPieces_On3DLoadEnd();
    ApStarShot_On3DLoadEnd();
}

ModDesc mod_desc = {
    .name = "ap_star",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .affects_gameplay = 1,
    .option_desc = &ModSettings,
    .OnBoot = OnBoot,
    .On3DLoadStart = On3DLoadStart,
    .On3DLoadEnd = On3DLoadEnd,
    .OnFrameStart = ApStarPieces_OnFrameStart,
};
