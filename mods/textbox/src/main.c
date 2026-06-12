#include "os.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"
#include "code_patch/code_patch.h"

#include "textbox.h"
#include "textbox_colors.h"

// Hook installed at 0x80009084 - the instruction right after the `bl
// TopRide_CustomRenderer` inside TopRide_PostRenderCallback. TR's post-render
// kicks off a second HSD_StartRender pass that overdraws the EFB after the
// standard frame render, wiping anything the screen canvas drew. Re-issuing
// the canvas's render after that pass returns puts the textbox back on top.
static void Hook_TopRidePostRender(void)
{
    TextBox_TopRideReRender();
}
CODEPATCH_HOOKCREATE(0x80009084, "", Hook_TopRidePostRender, "", 0)

// Populated at boot from textbox_colors.c. The struct fields can't be
// initialized statically from extern const GXColor symbols (not constants
// in C), so we copy them in at OnBoot before exporting.
static TextBoxAPI api = {
    .Enqueue               = TextBox_Enqueue,
    .EnqueueSegments       = TextBox_EnqueueSegments,
    .EnqueueColoredNoun    = TextBox_EnqueueColoredNoun,
    .EnqueueColoredNounFmt = TextBox_EnqueueColoredNounFmt,
};

static void OnBoot(void)
{
    api.DefaultColor     = TextBox_DefaultColor;
    api.MachineColor     = TextBox_MachineColor;
    api.EventColor       = TextBox_EventColor;
    api.StadiumColor     = TextBox_StadiumColor;
    api.StageColor       = TextBox_StageColor;
    api.TopRideItemColor = TextBox_TopRideItemColor;
    api.ItemColor        = TextBox_ItemColor;
    api.TrapColor        = TextBox_TrapColor;
    api.DeathColor       = TextBox_DeathColor;
    api.EnergyColor      = TextBox_EnergyColor;
    api.CheckColor       = TextBox_CheckColor;
    api.GoalColor        = TextBox_GoalColor;
    api.RewardColor      = TextBox_RewardColor;
    api.ShopColor        = TextBox_ShopColor;
    api.FillerColor      = TextBox_FillerColor;
    api.AbilityColors    = TextBox_AbilityColors;
    api.KirbyColors      = TextBox_KirbyColors;
    api.ModeColors       = TextBox_ModeColors;
    api.PatchColors      = TextBox_PatchColors;
    api.BoxColors        = TextBox_BoxColors;

    Hoshi_ExportMod((void *)&api);

    CODEPATCH_HOOKAPPLY(0x80009084);

    OSReport("[TextBox] API exported (v%d.%d), TR post-render hook installed\n",
             TEXTBOX_API_MAJOR, TEXTBOX_API_MINOR);
}

static const char *stc_off_on[] = {"Off", "On"};

static void OnToggleEnabled(int val)
{
    OSReport("[TextBox] Enabled toggled %s\n", stc_off_on[val]);
}

static void OnToggleTypewriter(int val)
{
    OSReport("[TextBox] Typewriter toggled %s\n", stc_off_on[val]);
}

// RepositionAll reads the spacing multiplier live each call, so a refresh here
// reflows whatever's already on screen instead of waiting for the next message.
static void OnChangeSpacing(int val)
{
    (void)val;
    TextBoxQueue_RepositionAll();
}

static void OnChangeCorner(int val)
{
    (void)val;
    TextBoxQueue_RepositionAll();
}

static MenuDesc typewriter_menu = {
    .option_num = 2,
    .options = {
        &(OptionDesc){
            .name = "Enabled",
            .description = "Reveal textbox messages gradually instead of all at once",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.typewriter_enabled,
            .value_num = 2,
            .value_names = (char *[]){"Off", "On"},
            .on_change = OnToggleTypewriter,
        },
        &(OptionDesc){
            .name = "Speed",
            .description = "How fast the typewriter reveals each glyph",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.typewriter_speed,
            .value_num = 3,
            .value_names = (char *[]){"Slow", "Med", "Fast"},
        },
    },
};

static MenuDesc top_menu = {
    .option_num = 9,
    .options = {
        &(OptionDesc){
            .name = "Enabled",
            .description = "Enable or disable the in-game textbox",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.enabled,
            .value_num = 2,
            .value_names = (char *[]){"Off", "On"},
            .on_change = OnToggleEnabled,
        },
        &(OptionDesc){
            .name = "Position",
            .description = "Which corner of the screen the textbox stack anchors to",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.corner,
            .value_num = 4,
            .value_names = (char *[]){"Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right"},
            .on_change = OnChangeCorner,
        },
        &(OptionDesc){
            .name = "Font Size",
            .description = "Size of the textbox font",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.font_size,
            .value_num = 3,
            .value_names = (char *[]){"Small", "Med", "Large"},
        },
        &(OptionDesc){
            .name = "Colored Names",
            .description = "Color item, machine, event names, etc. by category",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.colored_names,
            .value_num = 2,
            .value_names = (char *[]){"Off", "On"},
        },
        &(OptionDesc){
            .name = "Background",
            .description = "Background panel opacity behind the text",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.background_opacity,
            .value_num = 3,
            .value_names = (char *[]){"Off", "Dim", "Solid"},
        },
        &(OptionDesc){
            .name = "Spacing",
            .description = "Vertical gap between stacked messages",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.message_spacing,
            .value_num = 3,
            .value_names = (char *[]){"Tight", "Normal", "Wide"},
            .on_change = OnChangeSpacing,
        },
        &(OptionDesc){
            .name = "Max On Screen",
            .description = "Maximum number of messages visible at once",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.max_visible,
            .value_num = 4,
            .value_names = (char *[]){"3", "4", "6", "8"},
        },
        &(OptionDesc){
            .name = "Display Time",
            .description = "How long a message stays at full opacity before fading out",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.display_time,
            .value_num = 3,
            .value_names = (char *[]){"Short", "Med", "Long"},
        },
        &(OptionDesc){
            .name = "Typewriter",
            .description = "Per-glyph reveal animation",
            .kind = OPTKIND_MENU,
            .menu_ptr = &typewriter_menu,
        },
    },
};

static OptionDesc ModSettings = {
    .name = "Text Box",
    .description = "Configure the in-game textbox",
    .kind = OPTKIND_MENU,
    .menu_ptr = &top_menu,
};

ModDesc mod_desc = {
    .name = "textbox",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .option_desc = &ModSettings,
    .OnBoot = OnBoot,
    .OnSceneChange = CreateTextBox_OnSceneChange,
};
