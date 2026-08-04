#include "game.h"
#include "os.h"
#include "scene.h"
#include "hsd.h"
#include "obj.h"
#include "menu.h"
#include "rider.h"
#include "machine.h"
#include "code_patch/code_patch.h"
#include "hoshi/mod.h"

#include "main_menu.h"

static HSD_Archive *menu_archive = 0;

// Title file load (0x8000d2b4). Gm_LoadGameFile appends ".dat" and reads it from the
// disc overlay.
void MainMenu_OnTitleLoad(void)
{
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

void MainMenu_OnBoot(void)
{
    // The demo-player setup at 0x8000d300 picks the idle slot-0 rider's ride via three
    // `li r4` operands (RiderKind, IsBike, MachineKind). Must stay star-class
    // (is_bike=0) - the demo init uses hardcoded star-only state ids, so a wheel-class
    // machine crashes here.
    CODEPATCH_REPLACEINSTRUCTION(0x8000d340, 0x38800000 | RDKIND_DEDEDE);
    CODEPATCH_REPLACEINSTRUCTION(0x8000d34c, 0x38800000 | 0);
    CODEPATCH_REPLACEINSTRUCTION(0x8000d358, 0x38800000 | VCKIND_WAGON);

    CODEPATCH_HOOKAPPLY(0x8000d2b4);
    CODEPATCH_HOOKAPPLY(0x8017b5d8);

    OSReport("[MainMenu] Hooks installed\n");
}
