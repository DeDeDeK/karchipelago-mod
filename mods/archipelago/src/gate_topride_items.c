#include "game.h"
#include "os.h"
#include "scene.h"
#include "topride.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_topride_items.h"
#include "gate_abilities.h"
#include "textbox.h"
#include "inline.h"

// Top Ride items that correspond to copy abilities.
// These are additionally gated by ability_unlocked_mask.
static const struct { TopRideItemKind item; CopyKind ability; } ability_items[] = {
    { TRITEM_FREEZE, COPYKIND_ICE },
    { TRITEM_FIRE,   COPYKIND_FIRE },
    { TRITEM_NEEDLE, COPYKIND_NEEDLE },
    { TRITEM_BOMB,   COPYKIND_BOMB },
    { TRITEM_MIKE,   COPYKIND_MIC },
};


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
    [TRITEM_LANTERN]      = "TR Lantern",
    [TRITEM_MIKE]         = "TR Mike",
    [TRITEM_CRACKER]      = "TR Cracker",
    [TRITEM_WHO_PAINT]    = "TR Who? Paint",
    [TRITEM_SMOKESCREEN]  = "TR Smoke Screen",
    [TRITEM_CHICKIE]      = "TR Chickie",
    [TRITEM_BACKWARD]     = "TR Backward",
};

// Bitmask of ability-themed TR items — these are not gated by topride_item_unlocked_mask.
#define ABILITY_ITEM_BITS ( \
    (1 << TRITEM_FREEZE) | (1 << TRITEM_FIRE) | (1 << TRITEM_NEEDLE) | \
    (1 << TRITEM_BOMB) | (1 << TRITEM_MIKE))

// Apply the unlock mask to the ItemMgr's enabled bitmask.
// Ability-themed items bypass the TR item mask and are gated only by ability_unlocked_mask.
// Called via hook right after TopRideItem_MgrInit returns.
void GateTopRideItems_ApplyMask()
{
    TopRideItemMgr *mgr = *stc_topride_itemmgr;
    if (!mgr)
        return;

    u32 before = mgr->enabled_mask;

    // Gate non-ability items by TR item unlock mask
    mgr->enabled_mask &= ap_save->topride_item_unlocked_mask | ABILITY_ITEM_BITS;

    // Gate ability-themed items by ability unlock mask only
    u16 ability_mask = ap_save->ability_unlocked_mask;
    for (int i = 0; i < (int)(sizeof(ability_items) / sizeof(ability_items[0])); i++)
    {
        if (!(ability_mask & (1 << ability_items[i].ability)))
            mgr->enabled_mask &= ~(1 << ability_items[i].item);
    }

    OSReport("[TopRideItems] TopRide items: enabled mask %s -> %s (item mask %s, ability mask %s)\n",
             MaskBits(before, TRITEM_NUM), MaskBits(mgr->enabled_mask, TRITEM_NUM), MaskBits(ap_save->topride_item_unlocked_mask, TRITEM_NUM), MaskBits(ability_mask, 16));
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

// Bypass flag so our own callers (GateTopRideItems_GiveItem, traplink) can
// spawn any kind regardless of the mask. Most vanilla spawn paths go through
// TopRideItem_SpawnAtPosition; the cracker burst also ends up there, but it
// picks its kind with a weighted random across all 22 items that ignores
// enabled_mask entirely — so post-filtering here just drops spawns without
// preventing the locked pick. See GateTopRideItems_GetDataGated below for
// the picker-side fix that zero-weights locked items.
static volatile int topride_spawn_bypass;

// Hook at entry of TopRideItem_SpawnAtPosition (0x8034bf50). Returns 1 to
// block the spawn (locked item, mask bit clear), 0 to let it through.
// The hook trampoline saves LR around the bl to our C function so the
// "block" path can return cleanly to the original caller via the blr at
// 0x8034c12c (the function's epilogue blr).
int GateTopRideItems_FilterSpawn(TopRideItemMgr *mgr, int item_kind,
                                 Vec3 *pos, Vec3 *orient,
                                 unsigned int flag1, unsigned int flag2)
{
    if (topride_spawn_bypass)
        return 0;
    if (!mgr)
        return 0;
    if (item_kind < 0 || item_kind >= TRITEM_NUM)
        return 0;
    if (mgr->enabled_mask & (1 << item_kind))
        return 0;
    OSReport("[TopRideItems] Blocked spawn of locked kind %d (%s)\n",
             item_kind, topride_item_names[item_kind]);
    return 1;
}

// Mini stack frame around the bl so LR is preserved. If filter returns 1,
// the framework branches to 0x8034c12c (the function's final blr) which
// then returns to the caller using the LR we just restored.
CODEPATCH_HOOKCONDITIONALCREATE(0x8034bf50,
    "stwu 1, -16(1)\n\t"
    "mflr 0\n\t"
    "stw 0, 0x8(1)\n\t",
    GateTopRideItems_FilterSpawn,
    "lwz 0, 0x8(1)\n\t"
    "mtlr 0\n\t"
    "addi 1, 1, 16\n\t",
    0,
    0x8034c12c)

// Cracker burst (zz_80356dac_) runs a weighted-random picker across all 22
// items with no enabled_mask check. Both loops read the per-item weight
// through `bl TopRideItem_GetDataByIndex` then `lfs f0, 16(r3)`. Redirecting
// those two bl's to this wrapper yields a stub with weight=0 for locked
// kinds, so the total excludes them and the picker can never land on one.
static const float locked_item_stub[8] = {0}; // offset +0x10 (index 4) = 0.0

const void *GateTopRideItems_GetDataGated(int kind)
{
    TopRideItemMgr *mgr = *stc_topride_itemmgr;
    if (mgr && (unsigned)kind < TRITEM_NUM &&
        !(mgr->enabled_mask & (1u << kind)))
        return locked_item_stub;
    return TopRideItem_GetDataByIndex(kind);
}

void GateTopRideItems_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x802db05c);
    CODEPATCH_HOOKAPPLY(0x8034bf50);
    // Redirect the cracker burst's two weight-lookup bl's inside zz_80356dac_
    // to the gated wrapper. Sum loop + pick loop, in that order.
    CODEPATCH_REPLACECALL(0x803574a4, GateTopRideItems_GetDataGated);
    CODEPATCH_REPLACECALL(0x803574d0, GateTopRideItems_GetDataGated);
    OSReport("[TopRideItems] Top Ride item gating hooks installed\n");
}

int GateTopRideItems_UnlockItem(TopRideItemKind kind)
{
    if (kind >= TRITEM_NUM)
        return 0;

    ap_save->topride_item_unlocked_mask |= (1 << kind);
    OSReport("[TopRideItems] Top Ride item %d (%s) unlocked (mask = %s)\n",
             kind, topride_item_names[kind], MaskBits(ap_save->topride_item_unlocked_mask, TRITEM_NUM));
    TextBox_Enqueue(topride_item_names[kind]);
    return 1;
}

int GateTopRideItems_GiveItem(TopRideItemKind kind)
{
    if (kind >= TRITEM_NUM)
        return 0;

    TopRideItemMgr *item_mgr = *stc_topride_itemmgr;
    TopRideKirbyMgr *kirby_mgr = *stc_topride_kirbymgr;
    if (!item_mgr || !kirby_mgr)
        return 0;

    int spawned = 0;
    // Debug/traplink give should spawn any kind, locked or not — bypass the
    // mask filter hook while we call into the spawner.
    topride_spawn_bypass = 1;
    for (int i = 0; i < 4; i++)
    {
        TopRideKirby *k = kirby_mgr->kirbys[i];
        if (!k || !k->is_active || k->cpu_level != 0)
            continue;

        // charge.position is the in-world kirby position; TopRideKirby.position
        // (0x4C) is the spawn/default and would put the item at course start.
        Vec3 pos = k->charge.position;
        Vec3 orient = k->charge.facing_dir;
        TopRideItem_SpawnAtPosition(item_mgr, kind, &pos, &orient, 0, 1);
        spawned = 1;
        OSReport("[TopRideItems] Spawned TR item %d (%s) at player %d\n",
                 kind, topride_item_names[kind], i);
    }
    topride_spawn_bypass = 0;
    return spawned;
}
