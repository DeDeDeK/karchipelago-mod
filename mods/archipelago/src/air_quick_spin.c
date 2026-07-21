#include "rider.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "settings_menu.h"
#include "air_quick_spin.h"

// Vanilla runs the L/R-flick quick spin check only in the grounded machine-riding
// state; the airborne state (airControl, 0x801ac128) stops at the Tornado spin
// check. With the menu toggle on, this reinstates the quick spin check airborne
// (Air Ride / City Trial). The reinstated call funnels through the base-ability
// quick-spin gate at 0x801b7ec0, so an AP-locked spin stays locked in the air too.

// Hook body at 0x801ac170 - the dead `cmpwi r3,0` airControl left where groundLogic
// calls the quick spin check. r31 = RiderData, r3 = Tornado-spin result. Skip when
// Tornado already fired, mirroring groundLogic's `if (tornado_spin() == 0)` guard.
void AirQuickSpin_TryAerialSpin(RiderData *rd, int tornado_fired)
{
    if (ap_menu_settings.air_quick_spin_enabled && !tornado_fired)
        Rider_IASACheck_QuickSpin(rd);
}

CODEPATCH_HOOKCREATE(0x801ac170,
    "mr 4,3\n\t"    // tornado_fired = groundSpin2_ result
    "mr 3,31\n\t",  // rd
    AirQuickSpin_TryAerialSpin,
    "",
    0)

void AirQuickSpin_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x801ac170);
    OSReport("[AirQuickSpin] Air quick spin hook installed\n");
}
