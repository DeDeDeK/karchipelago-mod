#include "game.h"
#include "os.h"
#include "audio.h"
#include "scene.h"
#include "hsd.h"
#include "obj.h"
#include "menu.h"
#include "rider.h"
#include "machine.h"
#include "code_patch/code_patch.h"
#include "hoshi/mod.h"
#include "hoshi/func.h"

#include "main.h"
#include "main_menu.h"
#include "gate_ap_star.h"

static HSD_Archive *menu_archive = 0;
static void (*title_exit_vanilla)(void *data) = 0;
static void (*title_think_vanilla)(void) = 0;
static float demo_idle_floor = 0.0f;
static int demo_idle_floor_saved = 0;

// The demo ride, as a star-class slot. Resolves to the Archipelago Star once
// custom_machines has registered it, which is the point: the title screen shows the
// machine the goal awards, before it is earned.
static int demo_star_slot = VCKIND_WAGON;
static int demo_rider = RDKIND_DEDEDE;

// The demo-player setup at 0x8000d300 picks the idle slot-0 rider's ride through three
// `li r4` operands (RiderKind, IsBike, class slot). Must stay star-class (is_bike = 0) -
// the demo init uses hardcoded star-only state ids, so a wheel-class machine crashes.
// Re-applied per title entry because the registry only resolves after every mod boots.
static void MainMenu_SelectDemoMachine(void)
{
    int kind = GateApStar_MachineKind();

    if (kind >= 0)
    {
        int is_bike;
        int slot = MachineKind_ClassIndexOf((MachineKind)kind, &is_bike);
        if (!is_bike)
        {
            demo_star_slot = slot;
            demo_rider = RDKIND_KIRBY;
        }
    }

    CODEPATCH_REPLACEINSTRUCTION(0x8000d340, 0x38800000 | demo_rider);
    CODEPATCH_REPLACEINSTRUCTION(0x8000d34c, 0x38800000 | 0);
    CODEPATCH_REPLACEINSTRUCTION(0x8000d358, 0x38800000 | demo_star_slot);
}

// Title file load (0x8000d2b4). Gm_LoadGameFile appends ".dat" and reads it from the
// disc overlay.
void MainMenu_OnTitleLoad(void)
{
    MainMenu_SelectDemoMachine();
    Gm_LoadGameFile(&menu_archive, "MnTitleKarchi");
}
CODEPATCH_HOOKCREATE(0x8000d2b4, "", MainMenu_OnTitleLoad, "", 0)

// The vanilla "AIR RIDE" subtitle (text + blue box) is foreground joint 14 in
// GObj_GetJObjIndex depth-first order; JObj_SetFlagsAll hides its whole subtree.
#define VANILLA_SUBTITLE_JOINT 14

// Title scene create (0x8017b5d8). MenuElement_AddData allocates the element userdata
// the render callback derefs and sets its is_visible flag - a static model needs no
// proc, but the userdata must exist.
void MainMenu_OnTitleCreate(void)
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
CODEPATCH_HOOKCREATE(0x8017b5d8, "", MainMenu_OnTitleCreate, "", 0)

// The title demo machine is never registered in PlayerData, so it is reached through the
// machine GObj list.
static GOBJ *MainMenu_GetMachines(void)
{
    return (*stc_gobj_lookup)[GAMEPLINK_MACHINE];
}

// The Wagon Star's engine loop holds an idle volume floor of 20.0, which clamps to full
// volume, so the demo machine hums constantly where the vanilla Warp Star is silent.
// Zeroing the floor makes its volume arithmetic identical to the Warp Star's.
// Machine_UpdateEngineLoop re-reads the record every frame, and the loop is only ever
// created at volume 0.0 and ramped up from there, so this never lets an audible frame
// through. Kinds whose floor is already 0.0 pass through unchanged.
static MachineAudioParams *MainMenu_GetDemoAudioParams(void)
{
    if (*stc_machineAudioParams == 0)
        return 0;

    return &(*stc_machineAudioParams)->params[0][demo_star_slot];
}

// Title minor cb_ThinkPreGObjProc, wrapped around the vanilla one. vcLoadCommon runs partway
// through the title cb_Load, so the record is only guaranteed resident once the scene is
// running; the demo machine existing at all proves it is.
static void MainMenu_TitleThink(void)
{
    if (!demo_idle_floor_saved)
    {
        MachineAudioParams *params = MainMenu_GetDemoAudioParams();

        if (params != 0)
        {
            demo_idle_floor = params->engine_idle_floor;
            params->engine_idle_floor = 0.0f;
            demo_idle_floor_saved = 1;
        }
    }

    title_think_vanilla();
}

// Title minor cb_Exit, wrapped around the vanilla one. The record is shared game data, so it
// goes back before any other scene reads it. Scene teardown then reclaims the demo machine as
// raw memory without running Machine_Destroy, leaving its two silent loops holding FGM
// instances and its tracks and emitter holding static Audio3D slots; vanilla leaks all of
// these, and this is the last point at which the machine is still alive enough to return them.
static void MainMenu_TitleExit(void *data)
{
    GOBJ *gobj = MainMenu_GetMachines();
    MachineAudioParams *params = MainMenu_GetDemoAudioParams();

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
            if (md->audio.x860 != -1)
                FGM_Stop(md->audio.x860);
            if (md->audio.x87c_fgm_instance != -1)
                FGM_Stop(md->audio.x87c_fgm_instance);

            Machine_FreeAudioEmitter(md);
        }

        gobj = gobj->next;
    }

    title_exit_vanilla(data);
}

void MainMenu_OnBoot(void)
{
    MinorSceneDesc *minor_descs = Hoshi_GetMinorScenes();

    title_exit_vanilla = minor_descs[MNRKIND_TITLESCREEN].cb_Exit;
    minor_descs[MNRKIND_TITLESCREEN].cb_Exit = MainMenu_TitleExit;

    title_think_vanilla = minor_descs[MNRKIND_TITLESCREEN].cb_ThinkPreGObjProc;
    minor_descs[MNRKIND_TITLESCREEN].cb_ThinkPreGObjProc = MainMenu_TitleThink;

    MainMenu_SelectDemoMachine();

    CODEPATCH_HOOKAPPLY(0x8000d2b4);
    CODEPATCH_HOOKAPPLY(0x8017b5d8);

    OSReport("[MainMenu] Hooks installed\n");
}
