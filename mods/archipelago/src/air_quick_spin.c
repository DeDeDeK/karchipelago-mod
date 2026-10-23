#include "rider.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "settings_menu.h"
#include "air_quick_spin.h"

// The reinstated calls go through the base-ability quick-spin gates (0x801b7ec0,
// 0x801c05d4, 0x801c3f6c), so an AP-locked spin stays locked in the air.

// Hook at 0x801ac170, the dead cmpwi r3, 0 airControl (0x801ac128) left where the grounded
// logic runs the quick spin check. r31 = RiderData, r3 = Rider_IASACheck_TornadoSpin's
// result; skipping when the Tornado spin fired mirrors the grounded guard.
static void AirQuickSpin_TryAerialSpin(RiderData *rd, int tornado_fired)
{
    if (ap_menu_settings.air_quick_spin_enabled && !tornado_fired)
        RiderState_QuickSpinInterrupt(rd);
}

CODEPATCH_HOOKCREATE(0x801ac170,
    "mr 4,3\n\t"
    "mr 3,31\n\t",
    AirQuickSpin_TryAerialSpin,
    "",
    0)

// Hook at 0x801c2b28, the dead cmpwi r3, 0 in Rider_MetaKnight_AirControl (0x801c2b08)
// where his grounded states branch on the charge check. r31 = RiderData, r3 = the charge
// result. His airborne state never ticks the spin accumulators, so the tick comes too.
static void AirQuickSpin_TryAerialSpinMetaKnight(RiderData *rd, int charge_fired)
{
    if (!ap_menu_settings.air_quick_spin_enabled || charge_fired)
        return;

    Rider_UpdateQuickSpinTimers(rd);
    RiderState_MetaKnightQuickSpinInterrupt(rd);
}

CODEPATCH_HOOKCREATE(0x801c2b28,
    "mr 4,3\n\t"
    "mr 3,31\n\t",
    AirQuickSpin_TryAerialSpinMetaKnight,
    "",
    0)

// Replaces the bl Rider_UpdateQuickSpinTimers at 0x801bf560 in Rider_Dedede_AirControl
// (0x801bf534), the last call of its charge-free branch, where his grounded states run the
// quick spin check.
static void AirQuickSpin_DededeAerialSpin(RiderData *rd)
{
    Rider_UpdateQuickSpinTimers(rd);

    if (ap_menu_settings.air_quick_spin_enabled)
        RiderState_DededeQuickSpinInterrupt(rd);
}

void AirQuickSpin_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x801ac170);
    CODEPATCH_HOOKAPPLY(0x801c2b28);
    CODEPATCH_REPLACECALL(0x801bf560, AirQuickSpin_DededeAerialSpin);
    OSReport("[AirQuickSpin] Hooks installed\n");
}
