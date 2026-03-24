#include "game.h"
#include "os.h"
#include "scene.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_topride_items.h"
#include "textbox.h"

// ItemMgr singleton pointer (set during Top Ride 3D scene init)
#define TOPRIDE_ITEMMGR_PTR (*(u32 **)0x805ddba4)

// Offset of the enabled items bitmask within ItemMgr (param_9[9] = 0x24)
#define ITEMMGR_ENABLED_MASK_OFFSET 0x24

static const char *topride_item_names[TRITEM_NUM] = {
    [TRITEM_MYSTERY]      = "TR Mystery",
    [TRITEM_HAMMER]       = "TR Hammer",
    [TRITEM_GROW]         = "TR Grow",
    [TRITEM_SPEEDUP]      = "TR Speed Up",
    [TRITEM_SPEEDDOWN]    = "TR Speed Down",
    [TRITEM_MISSILE]      = "TR Missile",
    [TRITEM_CHARGEBOOST]  = "TR Charge Boost",
    [TRITEM_INVINCIBLE]   = "TR Invincible Candy",
    [TRITEM_BUZZSAW]      = "TR Buzz Saw",
    [TRITEM_SPEAR]        = "TR Spear",
    [TRITEM_FREEZE]       = "TR Freeze",
    [TRITEM_MISSILE_ALT]  = "TR Missile Alt",
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
    u32 *mgr = TOPRIDE_ITEMMGR_PTR;
    if (!mgr)
        return;

    u32 *enabled = (u32 *)((u8 *)mgr + ITEMMGR_ENABLED_MASK_OFFSET);
    u32 before = *enabled;
    *enabled &= save_data->topride_item_unlocked_mask;
    OSReport("TopRide items: enabled mask 0x%08x -> 0x%08x (unlock mask 0x%08x)\n",
             before, *enabled, save_data->topride_item_unlocked_mask);
}

// Hook at 0x802db05c — right after TopRideItem_MgrInit (0x8034b5f4) returns
// in the Top Ride 3D scene setup function (zz_802dafb4_).
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
