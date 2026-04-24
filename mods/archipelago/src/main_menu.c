#include "os.h"
#include "rider.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "main_menu.h"

// The main menu's "demo player" setup at 0x8000d300 runs a series of Ply_Set*
// calls for player slot 0. Three of the `li r4, imm` operands control what
// the idle rider on the title screen is riding:
//   0x8000d340  li r4, 0  -> Ply_SetRiderKind(0, RDKIND_KIRBY)
//   0x8000d34c  li r4, 0  -> Ply_SetIsBike(0, 0)
//   0x8000d358  li r4, 0  -> Ply_SetMachineKind(0, VCKIND_WARP)
//
// Ply_SetMachineKind stores a class-relative index. For star-class
// (is_bike=0) the VCKIND_* value is used directly. For wheel-class the
// index is relative to VCKIND_WHEELNORMAL. The title screen's demo init
// calls MachineStateChange with hardcoded star-only state ids (82/89),
// so wheel-class machines crash here — keep is_bike=0 and use a star.
void MainMenu_OnBoot(void)
{
    CODEPATCH_REPLACEINSTRUCTION(0x8000d340, 0x38800000 | RDKIND_DEDEDE);
    CODEPATCH_REPLACEINSTRUCTION(0x8000d34c, 0x38800000 | 0);
    CODEPATCH_REPLACEINSTRUCTION(0x8000d358, 0x38800000 | VCKIND_WAGON);

    OSReport("[MainMenu] Hooks installed\n");
}
