#include "os.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"
#include "code_patch/code_patch.h"

#include "textbox.h"
#include "textbox_colors.h"

// 0x80009084 is the instruction right after the `bl TopRide_CustomRenderer` inside
// TopRide_PostRenderCallback (0x80009074), whose second render pass wipes the textbox.
static void TextBox_OnTopRidePostRender(void)
{
    TextBox_TopRideReRender();
}
CODEPATCH_HOOKCREATE(0x80009084, "", TextBox_OnTopRidePostRender, "", 0)

// Filled at OnBoot: an extern const GXColor is not a constant expression.
static TextBoxAPI api = {
    .Enqueue               = TextBox_Enqueue,
    .EnqueueSegments       = TextBox_EnqueueSegments,
    .EnqueueColoredNoun    = TextBox_EnqueueColoredNoun,
    .EnqueueColoredNounFmt = TextBox_EnqueueColoredNounFmt,
    .IsReady               = TextBox_IsReady,
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

static void OnChangeEnabled(int val)
{
    // Turning it off retires what is already on screen, or the stack keeps fading for another
    // minute after the player asked for it to stop.
    if (!val)
        TextBoxQueue_Flush();
    OSReport("[TextBox] Text box %s\n", val ? "enabled" : "disabled");
}

static void OnChangeTypewriter(int val)
{
    static const char *names[] = {"off", "slow", "medium", "fast"};
    OSReport("[TextBox] Typewriter %s\n", names[val]);
}

// RepositionAll reads the settings live, so this reflows what is already on screen instead of
// waiting for the next message.
static void OnChangeSpacing(int val)
{
    static const char *names[] = {"tight", "normal", "wide"};
    TextBoxQueue_RepositionAll();
    OSReport("[TextBox] Spacing %s\n", names[val]);
}

static void OnChangeCorner(int val)
{
    static const char *names[] = {"top-left", "top-right", "bottom-left", "bottom-right"};
    TextBoxQueue_RepositionAll();
    OSReport("[TextBox] Position %s\n", names[val]);
}

// The cap is otherwise only read at enqueue, so lowering it would strand a stack that is already
// over the new limit until the next message arrived and dropped several at once.
static void OnChangeMaxVisible(int val)
{
    static const char *names[] = {"3", "4", "6", "8"};
    TextBoxQueue_TrimToCap();
    OSReport("[TextBox] Max on screen %s\n", names[val]);
}

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
            .on_change = OnChangeEnabled,
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
            .description = "Font size for new messages; those already on screen keep theirs",
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
            .on_change = OnChangeMaxVisible,
        },
        &(OptionDesc){
            .name = "Display Time",
            .description = "How long the oldest message holds before it fades and the stack advances",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.display_time,
            .value_num = 3,
            .value_names = (char *[]){"Short", "Med", "Long"},
        },
        &(OptionDesc){
            .name = "Typewriter",
            .description = "Speed of the per-glyph reveal, or Off to show a message at once",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.typewriter,
            .value_num = 4,
            .value_names = (char *[]){"Off", "Slow", "Med", "Fast"},
            .on_change = OnChangeTypewriter,
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
    .version.major = TEXTBOX_API_MAJOR,
    .version.minor = TEXTBOX_API_MINOR,
    .option_desc = &ModSettings,
    .OnBoot = OnBoot,
    .OnSceneChange = TextBox_OnSceneChange,
};
