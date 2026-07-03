#include "rider.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "settings_menu.h"
#include "air_quick_spin.h"

// The airborne machine-riding state (airControl, 0x801ac128) runs the same
// interrupt checks as the grounded state (groundLogic) except the L/R-flick
// quick spin: grounded calls Rider_IASACheck_QuickSpin after the Tornado spin
// check returns 0, airborne stops at the Tornado check. That is why only the
// Tornado copy ability spins mid-air in vanilla. With the menu toggle on this
// restores the missing call so the normal quick spin works airborne too, in Air
// Ride and City Trial (airControl is the 3D machine-riding state; Top Ride is a
// separate system). The call funnels through the base-ability quick-spin gate at
// 0x801b7ec0, so an AP-locked quick spin stays locked in the air as well.

// Hook body at 0x801ac170 - the dead `cmpwi r3,0` airControl left where
// groundLogic would call the quick spin check. r31 holds the RiderData across
// airControl; r3 holds the Tornado-spin (groundSpin2_) result. Skip when Tornado
// already fired, mirroring groundLogic's `if (tornado_spin() == 0)` guard.
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
