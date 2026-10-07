#include "game.h"
#include "os.h"
#include "audio.h"
#include "scene.h"
#include "hsd.h"
#include "obj.h"
#include "menu.h"
#include "text.h"
#include "rider.h"
#include "machine.h"
#include "code_patch/code_patch.h"
#include "hoshi/mod.h"
#include "hoshi/func.h"
#include "hoshi/screen_cam.h"

#include "main.h"
#include "ap_title.h"
#include "version.h"
#include "gate_ap_star.h"

static HSD_Archive *menu_archive = 0;
static void (*title_exit_vanilla)(void *data) = 0;
static void (*title_think_vanilla)(void) = 0;
static float demo_idle_floor = 0.0f;
static int demo_idle_floor_saved = 0;
static Text *version_text = 0;

// The demo ride as a star-class slot; the Archipelago Star once it is registered.
static int demo_star_slot = VCKIND_WAGON;
static int demo_rider = RDKIND_DEDEDE;

// SceneLoad_TitleScreen (0x8000d26c) sets the demo ride through li r4 operands: RiderKind
// at 0x8000d340, class slot at 0x8000d358. IsBike at 0x8000d34c stays 0: the demo init uses
// star-only state ids, and a wheel crashes it. Re-applied per title entry, since the
// registry resolves after every mod boots.
static void APTitle_SelectDemoMachine(void)
{
    int kind = GateApStar_MachineKind();

    if (kind >= 0)
    {
        int is_bike;
        int slot = CustomMachines_ClassIndexOf(cm_api, (MachineKind)kind, &is_bike);
        if (!is_bike)
        {
            demo_star_slot = slot;
            demo_rider = RDKIND_KIRBY;
        }
    }

    CODEPATCH_REPLACEINSTRUCTION(0x8000d340, 0x38800000 | demo_rider);     // li r4, demo_rider
    CODEPATCH_REPLACEINSTRUCTION(0x8000d358, 0x38800000 | demo_star_slot); // li r4, demo_star_slot
}

// Hook at 0x8000d2b4 in SceneLoad_TitleScreen, ahead of the demo-ride operands it patches.
static void APTitle_OnTitleLoad(void)
{
    APTitle_SelectDemoMachine();
    Gm_LoadGameFile(&menu_archive, "MnTitleKarchi");
}
CODEPATCH_HOOKCREATE(0x8000d2b4, "", APTitle_OnTitleLoad, "", 0)

// The vanilla "AIR RIDE" subtitle (text and blue box), foreground joint 14 in
// GObj_GetJObjIndex's depth-first order.
#define VANILLA_SUBTITLE_JOINT 14

// MenuElement_AddData allocates the userdata the render callback derefs; a static model
// needs no proc, but the userdata must exist.
static void APTitle_OnTitleCreate(void)
{
    GOBJ *fg;
    JOBJSet **set;
    GOBJ *element;

    if (menu_archive == 0)
        return;

    fg = Gm_GetMenuData()->ScMenTitleFg_gobj;
    JObj_SetFlagsAll(GObj_GetJObjIndex(fg, VANILLA_SUBTITLE_JOINT), JOBJ_HIDDEN);

    set = Archive_GetPublicAddress(menu_archive, "karchiTitleFg_scene_models");
    element = MenuElement_Create(set[0]->jobj);
    MenuElement_AddData(element, 99);
}
// Hook at 0x8017b5d8 in TitleScreen_CreateForegroundElements (0x8017b4c0), after both
// title element GObjs exist.
CODEPATCH_HOOKCREATE(0x8017b5d8, "", APTitle_OnTitleCreate, "", 0)

// The demo machine is never registered in PlayerData.
static GOBJ *APTitle_GetMachines(void)
{
    return (*stc_gobj_lookup)[GAMEPLINK_MACHINE];
}

static MachineAudioParams *APTitle_GetDemoAudioParams(void)
{
    if (*stc_machineAudioParams == 0 || (*stc_machineAudioParams)->params[0] == 0)
        return 0;

    return &(*stc_machineAudioParams)->params[0][demo_star_slot];
}

// Bottom-right version stamp, created from the title's think and destroyed from its cb_Exit:
// scene teardown does not reliably reclaim a Text, and one left behind draws over the next
// scene.
#define VERSION_MARGIN   12.0f
#define VERSION_SCALE    0.30f
#define VERSION_PAD      12.0f

static void APTitle_CreateVersionText(void)
{
    Text *t = Hoshi_CreateScreenText();

    if (t == 0)
        return;

    t->kerning = 1;
    t->viewport_scale = (Vec2){VERSION_SCALE, VERSION_SCALE};
    // Both are captured into the subtext at Text_AddSubtext time, so they precede it.
    t->color = (GXColor){255, 255, 255, 255};
    t->viewport_color = (GXColor){0, 0, 0, 100};

    Text_AddSubtext(t, VERSION_PAD, 0, "v" KARCHIPELAGO_VERSION);

    // Measured in pre-viewport-scale units, and excluding the subtext's own POS offset.
    float w = 0.0f, h = 0.0f;
    Text_GetWidthAndHeight(t, 0, &w, &h);

    t->aspect = (Vec2){w + 2.0f * VERSION_PAD, h};
    t->trans = (Vec3){TEXT_CANVAS_W - VERSION_MARGIN - t->aspect.X * VERSION_SCALE,
                      TEXT_CANVAS_H - VERSION_MARGIN - t->aspect.Y * VERSION_SCALE,
                      0};

    version_text = t;
}

// Title minor cb_ThinkPreGObjProc wrapper. The audio record loads partway through cb_Load
// (vcLoadCommon), so it is read once the scene runs.
static void APTitle_Think(void)
{
    // The Wagon Star's idle floor (20.0) clamps to full volume, so the demo hums where the
    // Warp Star is silent; 0.0 matches the Warp Star. Its engine loop starts at volume 0.0,
    // so no audible frame gets through.
    if (!demo_idle_floor_saved)
    {
        MachineAudioParams *params = APTitle_GetDemoAudioParams();

        if (params != 0)
        {
            demo_idle_floor = params->engine_idle_floor;
            params->engine_idle_floor = 0.0f;
            demo_idle_floor_saved = 1;
        }
    }

    // The boot cinematic shares this minor without the title foreground. Text_CreateText
    // faults on an empty canvas list, which hoshi rebuilds on scene change.
    if (Gm_GetMenuData()->ScMenTitleFg_gobj != 0)
    {
        if (version_text == 0 && *stc_textcanvas_first != 0)
            APTitle_CreateVersionText();
    }
    else if (version_text != 0)
    {
        Text_Destroy(version_text);
        version_text = 0;
    }

    title_think_vanilla();
}

// Title minor cb_Exit wrapper. Restores the shared audio record, and returns the demo
// machine's FGM loops and Audio3D slots, which vanilla leaks: teardown skips
// Machine_Destroy.
static void APTitle_Exit(void *data)
{
    GOBJ *gobj = APTitle_GetMachines();
    MachineAudioParams *params = APTitle_GetDemoAudioParams();

    if (version_text != 0)
    {
        Text_Destroy(version_text);
        version_text = 0;
    }

    if (demo_idle_floor_saved && params != 0)
    {
        params->engine_idle_floor = demo_idle_floor;
        demo_idle_floor_saved = 0;
    }

    while (gobj != 0)
    {
        MachineData *md = gobj->userdata;

        if (md != 0)
        {
            if (md->audio.surface_loop_fgm != -1)
                FGM_Stop(md->audio.surface_loop_fgm);
            if (md->audio.engine_loop_fgm != -1)
                FGM_Stop(md->audio.engine_loop_fgm);

            Machine_FreeAudioEmitter(md);
        }

        gobj = gobj->next;
    }

    title_exit_vanilla(data);
}

void APTitle_OnBoot(void)
{
    MinorSceneDesc *minor_descs = Hoshi_GetMinorScenes();

    title_exit_vanilla = minor_descs[MNRKIND_TITLESCREEN].cb_Exit;
    minor_descs[MNRKIND_TITLESCREEN].cb_Exit = APTitle_Exit;

    title_think_vanilla = minor_descs[MNRKIND_TITLESCREEN].cb_ThinkPreGObjProc;
    minor_descs[MNRKIND_TITLESCREEN].cb_ThinkPreGObjProc = APTitle_Think;

    CODEPATCH_HOOKAPPLY(0x8000d2b4);
    CODEPATCH_HOOKAPPLY(0x8017b5d8);

    OSReport("[APTitle] Hooks installed\n");
}
