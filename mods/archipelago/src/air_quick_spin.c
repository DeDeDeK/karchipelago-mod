#include "rider.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "settings_menu.h"
#include "air_quick_spin.h"

// Vanilla runs the L/R-flick quick spin check only in the grounded machine-riding
// states; the airborne state stops short of it. With the menu toggle on, this
// reinstates the check airborne for all three rider characters, each of which has
// its own airborne state callback and its own quick-spin check. The reinstated
// calls funnel through the base-ability quick-spin gates (0x801b7ec0, 0x801c05d4,
// 0x801c3f6c), so an AP-locked spin stays locked in the air too.

// Hook body at 0x801ac170, the dead `cmpwi r3,0` airControl left where
// groundLogic calls the quick spin check. r31 = RiderData, r3 = Tornado-spin
// result. Skipping when Tornado fired mirrors groundLogic's own guard.
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

// Hook body at 0x801c2b28, the dead `cmpwi r3,0` Rider_MetaKnight_AirControl left
// where his grounded states branch on the charge check. r31 = RiderData, r3 =
// charge-check result. Unlike Kirby and Dedede his airborne state never ticks the
// spin accumulators, so the tick has to come along with the check.
void AirQuickSpin_TryAerialSpinMetaKnight(RiderData *rd, int charge_fired)
{
    if (!ap_menu_settings.air_quick_spin_enabled || charge_fired)
        return;

    Rider_UpdateQuickSpinTimers(rd);
    Rider_MetaKnight_IASACheck_QuickSpin(rd);
}

CODEPATCH_HOOKCREATE(0x801c2b28,
    "mr 4,3\n\t"    // charge_fired
    "mr 3,31\n\t",  // rd
    AirQuickSpin_TryAerialSpinMetaKnight,
    "",
    0)

// Replaces the bl Rider_UpdateQuickSpinTimers at 0x801bf560 in
// Rider_Dedede_AirControl - his airborne state has no dead slot to hook, but the
// tick is already the last call of its charge-free branch, exactly where his
// grounded states run the quick spin check.
void AirQuickSpin_DededeAerialSpin(RiderData *rd)
{
    Rider_UpdateQuickSpinTimers(rd);

    if (ap_menu_settings.air_quick_spin_enabled)
        Rider_Dedede_IASACheck_QuickSpin(rd);
}

void AirQuickSpin_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x801ac170);
    CODEPATCH_HOOKAPPLY(0x801c2b28);
    CODEPATCH_REPLACECALL(0x801bf560, AirQuickSpin_DededeAerialSpin);
    OSReport("[AirQuickSpin] Air quick spin hooks installed\n");
}
