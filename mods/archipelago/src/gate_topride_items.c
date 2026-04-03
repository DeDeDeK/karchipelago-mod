#include "game.h"
#include "os.h"
#include "scene.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_topride_items.h"
#include "textbox.h"


static const char *topride_item_names[TRITEM_NUM] = {
    [TRITEM_HAMMER]       = "TR Hammer",
    [TRITEM_GROW]         = "TR Grow",
    [TRITEM_SPEEDUP]      = "TR Speed Up",
    [TRITEM_SPEEDDOWN]    = "TR Speed Down",
    [TRITEM_BOOST_SAW]    = "TR Boost Saw",
    [TRITEM_CHARGEBOOST]  = "TR Charge Boost",
    [TRITEM_INVINCIBLE]   = "TR Invincible Candy",
    [TRITEM_BUZZSAW]      = "TR Buzz Saw",
    [TRITEM_SPEAR]        = "TR Spear",
    [TRITEM_FREEZE]       = "TR Freeze",
    [TRITEM_MISSILE]      = "TR Missile",
    [TRITEM_FIRE]         = "TR Fire",
    [TRITEM_NEEDLE]       = "TR Needle",
    [TRITEM_BOMB]         = "TR Bomb",
    [TRITEM_LANDMINE]     = "TR Land Mine",
    [TRITEM_SENSORBOMB]   = "TR Sensor Bomb",
    [TRITEM_MIKE]         = "TR Mike",
    [TRITEM_CRACKER]      = "TR Cracker",
    [TRITEM_METAKNIGHT]   = "TR Meta Knight",
    [TRITEM_SMOKESCREEN]  = "TR Smoke Screen",
    [TRITEM_DIZZY]        = "TR Dizzy",
    [TRITEM_BACKWARD]     = "TR Backward",
};

// Apply the unlock mask to the ItemMgr's enabled bitmask.
// Called via hook right after TopRideItem_MgrInit returns.
void GateTopRideItems_ApplyMask()
{
    TopRideItemMgr *mgr = *stc_topride_itemmgr;
    if (!mgr)
        return;

    u32 before = mgr->enabled_mask;
    mgr->enabled_mask &= save_data->topride_item_unlocked_mask;
    OSReport("TopRide items: enabled mask 0x%08x -> 0x%08x (unlock mask 0x%08x)\n",
             before, mgr->enabled_mask, save_data->topride_item_unlocked_mask);
}

// Hook at 0x802db05c — right after TopRideItem_MgrInit (0x8034b5f4) returns
// in TopRide_FielderInit (0x802dafb4).
// Clobbered instruction: lwz r6, 4(r30)
CODEPATCH_HOOKCREATE(0x802db05c,
    "",
    GateTopRideItems_ApplyMask,
    "",
    0
)

void GateTopRideItems_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x802db05c);
    OSReport("Top Ride item gating hook installed\n");
}

int GateTopRideItems_UnlockItem(TopRideItemKind kind)
{
    if (kind >= TRITEM_NUM)
        return 0;

    save_data->topride_item_unlocked_mask |= (1 << kind);
    OSReport("Top Ride item %d (%s) unlocked (mask = 0x%08x)\n",
             kind, topride_item_names[kind], save_data->topride_item_unlocked_mask);
    TextBox_Enqueue(topride_item_names[kind]);
    return 1;
}
