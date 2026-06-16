#include "game.h"
#include "os.h"
#include "topride.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_topride_items.h"
#include "textbox_api.h"
#include "inline.h"

// Top Ride items that correspond to copy abilities.
// These are additionally gated by ability_unlocked_mask.
static const struct { TopRideItemKind item; CopyKind ability; } ability_items[] = {
    { TRITEM_FREEZE_FAN, COPYKIND_FREEZE },
    { TRITEM_FIRE,       COPYKIND_FIRE },
    { TRITEM_BOMB,       COPYKIND_BOMB },
    { TRITEM_WALKY,      COPYKIND_MIC },
};

// Bitmask of ability-themed TR items - these are not gated by topride_item_unlocked_mask.
#define ABILITY_ITEM_BITS ( \
    (1 << TRITEM_FREEZE_FAN) | (1 << TRITEM_FIRE) | \
    (1 << TRITEM_BOMB) | (1 << TRITEM_WALKY))

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

    // Slot 12 (TRITEM_PARTY_BALL_ALT, KirbyKusdama) is the engine's twin
    // Party Ball variant. AP exposes only one Party Ball (slot 21,
    // TRITEM_PARTY_BALL, KirbyUshiroyurerun), so mirror bit 21's state
    // onto bit 12 here. Without this, the kusdama variant never spawns
    // under the AP mod.
    if (mgr->enabled_mask & (1 << TRITEM_PARTY_BALL))
        mgr->enabled_mask |= (1 << TRITEM_PARTY_BALL_ALT);
    else
        mgr->enabled_mask &= ~(1 << TRITEM_PARTY_BALL_ALT);

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

// Hook at 0x802db05c - right after TopRideItem_MgrInit (0x8034b5f4) returns
// in TopRide_KirbyMgrInit (0x802dafb4).
// Clobbered instruction: lwz r6, 4(r30)
CODEPATCH_HOOKCREATE(0x802db05c,
    "",
    GateTopRideItems_ApplyMask,
    "",
    0
)

// Hook at entry of TopRideItem_SpawnAtPosition (0x8034bf50). Returns 1 to
// block the spawn (locked item, mask bit clear), 0 to let it through.
// The hook trampoline saves LR around the bl to our C function so the
// "block" path can return cleanly to the original caller via the blr at
// 0x8034c12c (the function's epilogue blr).
int GateTopRideItems_FilterSpawn(TopRideItemMgr *mgr, int item_kind,
                                 Vec3 *pos, Vec3 *orient,
                                 unsigned int flag1, unsigned int flag2)
{
    if (!mgr)
        return 0;
    // TopRideItem_PartyBallUpdate (frame 0xFF: Party Ball open) sums per-item
    // weights then picks via weighted random. When every TR item is locked,
    // sum == 0 and the pick loop falls out with its loop counter at
    // TRITEM_NUM. Letting that through makes TopRideItem_Create read past the
    // descriptor table at 0x804ea2fc and crash on a garbage model-name pointer.
    if (item_kind < 0 || item_kind >= TRITEM_NUM)
    {
        OSReport("[TopRideItems] Blocked spawn of out-of-range kind %d\n", item_kind);
        return 1;
    }
    if (mgr->enabled_mask & (1 << item_kind))
        return 0;
    OSReport("[TopRideItems] Blocked spawn of locked kind %d (%s)\n",
             item_kind, TopRideItemKind_Names[item_kind]);
    return 1;
}

// Save r3-r8 (the original args to TopRideItem_SpawnAtPosition) across the
// bl into our C filter, since the filter's return value clobbers r3 and the
// function's first instructions deref r3 (lwz r3, 4(r3) at 0x8034bf68 - DSI
// crash if r3 is left at 0 from a "proceed" return).
//
// Proceed path: restore args + LR + frame, then `b 0x1c` to skip past the
// block-path tail (4 instr) plus the macro's cmpwi+bne (8 bytes), landing
// directly on the clobbered instruction. Bypassing the macro's cmpwi is
// necessary because we need r3 = mgr (non-zero) here, not the filter result.
//
// Block path: restore LR + frame, set r3 = 1 so the macro's cmpwi/bne sends
// us to the alt addr 0x8034c12c (the function's blr) using our saved LR.
CODEPATCH_HOOKCONDITIONALCREATE(0x8034bf50,
    "stwu 1, -48(1)\n\t"
    "mflr 0\n\t"
    "stw 0, 0x8(1)\n\t"
    "stw 3, 0x10(1)\n\t"
    "stw 4, 0x14(1)\n\t"
    "stw 5, 0x18(1)\n\t"
    "stw 6, 0x1c(1)\n\t"
    "stw 7, 0x20(1)\n\t"
    "stw 8, 0x24(1)\n\t",
    GateTopRideItems_FilterSpawn,
    "cmpwi 3, 0\n\t"
    "bne 1f\n\t"
    "lwz 3, 0x10(1)\n\t"
    "lwz 4, 0x14(1)\n\t"
    "lwz 5, 0x18(1)\n\t"
    "lwz 6, 0x1c(1)\n\t"
    "lwz 7, 0x20(1)\n\t"
    "lwz 8, 0x24(1)\n\t"
    "lwz 0, 0x8(1)\n\t"
    "mtlr 0\n\t"
    "addi 1, 1, 48\n\t"
    "b 0x1c\n\t"
    "1:\n\t"
    "lwz 0, 0x8(1)\n\t"
    "mtlr 0\n\t"
    "addi 1, 1, 48\n\t"
    "li 3, 1\n\t",
    0,
    0x8034c12c)

// Party Ball burst (TopRideItem_PartyBallUpdate, 0x80356dac, at frame 0xFF)
// runs a weighted-random picker across all 22 items with no enabled_mask
// check. Both loops read the per-item weight through
// `bl TopRideItem_GetDataByIndex` then `lfs f0, 16(r3)`. Redirecting those
// two bl's to this wrapper yields a stub with weight=0 for locked kinds, so
// the total excludes them and the picker can never land on one.
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
    // Redirect the Party Ball burst's two weight-lookup bl's inside
    // TopRideItem_PartyBallUpdate to the gated wrapper. Sum loop + pick loop.
    CODEPATCH_REPLACECALL(0x803574a4, GateTopRideItems_GetDataGated);
    CODEPATCH_REPLACECALL(0x803574d0, GateTopRideItems_GetDataGated);
    OSReport("[TopRideItems] Top Ride item gating hooks installed\n");
}

int GateTopRideItems_UnlockItem(TopRideItemKind kind, int announce)
{
    if ((unsigned)kind >= TRITEM_NUM)
        return 0;

    ap_save->topride_item_unlocked_mask |= (1 << kind);
    OSReport("[TopRideItems] Top Ride item %d (%s) unlocked (mask = %s)\n",
             kind, TopRideItemKind_Names[kind], MaskBits(ap_save->topride_item_unlocked_mask, TRITEM_NUM));
    if (announce)
    {
        TextSegment segs[5] = {
            {"Unlocked Item: ",           tb_api->DefaultColor},
            {TopRideItemKind_Names[kind], tb_api->TopRideItemColor},
            {" (",                        tb_api->DefaultColor},
            {"Top Ride",                  tb_api->ModeColors[GMMODE_TOPRIDE]},
            {")",                         tb_api->DefaultColor},
        };
        tb_api->EnqueueSegments(segs, 5);
    }
    return 1;
}

int GateTopRideItems_AbilityToItem(CopyKind ability)
{
    for (int i = 0; i < (int)(sizeof(ability_items) / sizeof(ability_items[0])); i++)
        if (ability_items[i].ability == ability)
            return ability_items[i].item;
    return -1;
}

int GateTopRideItems_GiveItem(TopRideItemKind kind)
{
    if ((unsigned)kind >= TRITEM_NUM)
        return 0;

    TopRideKirbyMgr *kirby_mgr = *stc_topride_kirbymgr;
    if (!kirby_mgr)
        return 0;

    // TopRide_KirbyApplyItem dereferences kirby+0x7c (held item GObj) which
    // is only populated once the race is active. round_state == 2 doubles as
    // the "kirby is fully wired up" gate.
    if (kirby_mgr->round_state != 2)
        return 0;

    // Don't gate on kirby->is_active - that bit is only set during a Race
    // round, never in Time Attack or Free Run, even while the human is
    // actively playing. round_state == 2 already covers "fully wired up";
    // matches EnergyLink_TopRidePerFrame which uses the same non-null + HMN
    // pair without is_active.
    int applied = 0;
    for (int i = 0; i < 4; i++)
    {
        TopRideKirby *k = kirby_mgr->kirbys[i];
        if (!k)
            continue;
        if (TopRide_GetPlayerKind(k->player_slot) != TR_PKIND_HMN)
            continue;

        TopRide_KirbyApplyItem(k, kind);
        applied = 1;
        OSReport("[TopRideItems] Applied TR item %d (%s) to player %d\n",
                 kind, TopRideItemKind_Names[kind], i);
    }
    return applied;
}
