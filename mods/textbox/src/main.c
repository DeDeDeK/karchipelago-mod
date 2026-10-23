#include "os.h"
#include "obj.h"
#include "text.h"
#include "hoshi/mod.h"
#include "code_patch/code_patch.h"

#include "textbox.h"
#include "textbox_colors.h"

// TopRide_CustomRenderer's second HSD_StartRender pass overdraws the EFB and wipes the screen
// canvas, so each canvas cam is re-issued on top of it.
static void TextBox_TopRideReRender(void)
{
    for (TextCanvas *canvas = *stc_textcanvas_first; canvas != NULL; canvas = canvas->next)
    {
        if (canvas->cam_gobj != NULL)
            CObjThink_Common(canvas->cam_gobj);
    }
}

// 0x80009084 is the instruction right after the `bl TopRide_CustomRenderer` inside
// TopRide_PostRenderCallback (0x80009074).
CODEPATCH_HOOKCREATE(0x80009084, "", TextBox_TopRideReRender, "", 0)

// The scalar colors are filled at OnBoot: an extern const GXColor is not a constant expression.
static TextBoxAPI api = {
    .Enqueue               = TextBox_Enqueue,
    .EnqueueSegments       = TextBox_EnqueueSegments,
    .EnqueueColoredNoun    = TextBox_EnqueueColoredNoun,
    .EnqueueColoredNounFmt = TextBox_EnqueueColoredNounFmt,
    .AbilityColors         = TextBox_AbilityColors,
    .KirbyColors           = TextBox_KirbyColors,
    .ModeColors            = TextBox_ModeColors,
    .PatchColors           = TextBox_PatchColors,
    .BoxColors             = TextBox_BoxColors,
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

    Hoshi_ExportMod((void *)&api);

    CODEPATCH_HOOKAPPLY(0x80009084);

    OSReport("[TextBox] API exported (v%d.%d), TR post-render hook installed\n",
             TEXTBOX_API_MAJOR, TEXTBOX_API_MINOR);
}

ModDesc mod_desc = {
    .name = "textbox",
    .author = "DeDeDK",
    .version.major = TEXTBOX_API_MAJOR,
    .version.minor = TEXTBOX_API_MINOR,
    .option_desc = &TextBox_ModSettings,
    .OnBoot = OnBoot,
    .OnSceneChange = TextBox_OnSceneChange,
};
