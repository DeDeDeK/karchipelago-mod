#include "os.h"
#include "rider.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "main_menu.h"

// The title-screen "demo player" setup at 0x8000d300 picks what the idle
// slot-0 rider rides via three `li r4` operands (RiderKind, IsBike,
// MachineKind). We swap them for Dedede on a Wagon. Must stay star-class
// (is_bike=0): the demo init calls MachineStateChange with hardcoded
// star-only state ids, so a wheel-class machine crashes here.
void MainMenu_OnBoot(void)
{
    CODEPATCH_REPLACEINSTRUCTION(0x8000d340, 0x38800000 | RDKIND_DEDEDE);
    CODEPATCH_REPLACEINSTRUCTION(0x8000d34c, 0x38800000 | 0);
    CODEPATCH_REPLACEINSTRUCTION(0x8000d358, 0x38800000 | VCKIND_WAGON);

    OSReport("[MainMenu] Hooks installed\n");
}
