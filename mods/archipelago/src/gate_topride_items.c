#include "game.h"
#include "os.h"
#include "topride.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_topride_items.h"
#include "textbox_api.h"
#include "inline.h"
#include "ap_announce.h"

// One bit per TRITEM kind whose blocked spawn was reported this round, plus
// TR_BLOCKED_OUT_OF_RANGE.
#define TR_BLOCKED_OUT_OF_RANGE 0x80000000u
static u32 blocked_reported;

const TopRideAbilityItem topride_ability_items[TR_ABILITY_ITEM_NUM] = {
    { TRITEM_FREEZE_FAN, COPYKIND_ICE,  TR_ITEMPOWER_VT_FREEZE_FAN },
    { TRITEM_FIRE,       COPYKIND_FIRE, TR_ITEMPOWER_VT_FIRE },
    { TRITEM_BOMB,       COPYKIND_BOMB, TR_ITEMPOWER_VT_BOMB },
    { TRITEM_WALKY,      COPYKIND_MIKE, TR_ITEMPOWER_VT_WALKY },
};

// The checklist reward each "New Item" unlocks through. TopRideItem_MgrInit (0x8034b5f4)
// keeps their enabled bits only once that reward is received.
static const struct { TopRideItemKind item; u8 reward_index; } new_item_rewards[] = {
    { TRITEM_CHICKIE,   8 },
    { TRITEM_WHO_PAINT, 9 },
    { TRITEM_LANTERN,   10 },
};

static void GateTopRideItems_ApplyMask(void)
{
    TopRideItemMgr *mgr = *stc_topride_itemmgr;
    if (!mgr)
        return;

    u32 before = mgr->enabled_mask;

    // Either key enables an ability item, but only while ability gating is on: an ungated
    // world's all-ones ability mask would free the four outright.
    u32 allowed = ap_save->topride_item_unlocked_mask;
    u16 ability_mask = ap_save->ability_unlocked_mask;
    if (ap_save->options.ability_gating_enabled)
    {
        for (int i = 0; i < TR_ABILITY_ITEM_NUM; i++)
        {
            if (ability_mask & (1 << topride_ability_items[i].ability))
                allowed |= (1 << topride_ability_items[i].item);
        }
    }

    mgr->enabled_mask &= allowed;

    // TRITEM_PARTY_BALL_ALT is the engine's twin Party Ball. Only slot 21 is shipped, so
    // its state is mirrored onto slot 12.
    if (mgr->enabled_mask & (1 << TRITEM_PARTY_BALL))
        mgr->enabled_mask |= (1 << TRITEM_PARTY_BALL_ALT);
    else
        mgr->enabled_mask &= ~(1 << TRITEM_PARTY_BALL_ALT);

    blocked_reported = 0;
    OSReport("[GateTopRideItems] Enabled mask %s -> %s (item %s, ability %s)\n",
             MaskBits(before, TRITEM_NUM), MaskBits(mgr->enabled_mask, TRITEM_NUM),
             MaskBits(ap_save->topride_item_unlocked_mask, TRITEM_NUM),
             MaskBits(ability_mask, COPYKIND_NUM));
}

// Right after TopRideItem_MgrInit (0x8034b5f4) returns in TopRide_KirbyMgrInit
// (0x802dafb4).
CODEPATCH_HOOKCREATE(0x802db05c,
    "",
    GateTopRideItems_ApplyMask,
    "",
    0
)

// Entry of TopRideItem_SpawnAtPosition (0x8034bf50). Returns 1 to block the spawn.
static int GateTopRideItems_FilterSpawn(TopRideItemMgr *mgr, int item_kind,
                                        Vec3 *pos, Vec3 *orient,
                                        unsigned int flag1, unsigned int flag2)
{
    if (!mgr)
        return 0;
    // With every TR item locked the Party Ball picker falls out at TRITEM_NUM, and
    // TopRideItem_Create would read past its descriptor table (0x804ea2fc).
    if (item_kind < 0 || item_kind >= TRITEM_NUM)
    {
        if (!(blocked_reported & TR_BLOCKED_OUT_OF_RANGE))
        {
            blocked_reported |= TR_BLOCKED_OUT_OF_RANGE;
            OSReport("[GateTopRideItems] Blocked spawn of out-of-range kind %d\n", item_kind);
        }
        return 1;
    }
    if (mgr->enabled_mask & (1 << item_kind))
        return 0;

    // Party balls re-roll a locked kind repeatedly, so say it once per kind.
    if (!(blocked_reported & (1u << item_kind)))
    {
        blocked_reported |= (1u << item_kind);
        OSReport("[GateTopRideItems] Blocked spawn of locked kind %d (%s)\n",
                 item_kind, TopRideItemKind_Names[item_kind]);
    }
    return 1;
}

// Saves r3-r8 across the filter, since the function derefs r3 at 0x8034bf68. Proceed:
// restore them and b 0x18 onto the clobbered stwu r1, -288(r1). Block: r3 = 1 takes the
// alt exit 0x8034c12c, a bare blr.
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
    "b 0x18\n\t"
    "1:\n\t"
    "lwz 0, 0x8(1)\n\t"
    "mtlr 0\n\t"
    "addi 1, 1, 48\n\t"
    "li 3, 1\n\t",
    0,
    0x8034c12c)

// TopRideItem_PartyBallUpdate (0x80356dac) weighs all 22 items with no enabled_mask check,
// through two bl TopRideItem_GetDataByIndex; locked kinds get a zero-weight stub.
static const TopRideItemData locked_item_stub;

static const TopRideItemData *GateTopRideItems_GetDataGated(int kind)
{
    TopRideItemMgr *mgr = *stc_topride_itemmgr;
    if (mgr && (unsigned)kind < TRITEM_NUM && !(mgr->enabled_mask & (1u << kind)))
        return &locked_item_stub;
    return TopRideItem_GetDataByIndex(kind);
}

void GateTopRideItems_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x802db05c);
    CODEPATCH_HOOKAPPLY(0x8034bf50);
    CODEPATCH_REPLACECALL(0x803574a4, GateTopRideItems_GetDataGated); // burst sum loop
    CODEPATCH_REPLACECALL(0x803574d0, GateTopRideItems_GetDataGated); // burst pick loop
    OSReport("[GateTopRideItems] Hooks installed\n");
}

// Only the received bit: an is_unlocked / clear[] write would badge the cell and send a
// spurious check.
static void MarkNewItemRewardReceived(TopRideItemKind kind)
{
    for (int i = 0; i < (int)GetElementsIn(new_item_rewards); i++)
    {
        if (new_item_rewards[i].item == kind)
            ap_save->received_checklist_rewards[GMMODE_TOPRIDE] |= 1ULL << new_item_rewards[i].reward_index;
    }
}

void GateTopRideItems_MarkNewItemRewardsReceived(void)
{
    for (int i = 0; i < (int)GetElementsIn(new_item_rewards); i++)
        ap_save->received_checklist_rewards[GMMODE_TOPRIDE] |= 1ULL << new_item_rewards[i].reward_index;
}

int GateTopRideItems_UnlockItem(TopRideItemKind kind, int announce)
{
    if ((unsigned)kind >= TRITEM_NUM)
        return 0;

    ap_save->topride_item_unlocked_mask |= (1 << kind);
    MarkNewItemRewardReceived(kind);
    if (!ap_regrant_quiet)
        OSReport("[GateTopRideItems] Top Ride item %d (%s) unlocked (mask = %s)\n",
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
        APAnnounce_GrantSegments(segs, 5);
    }
    return 1;
}

int GateTopRideItems_AbilityToItem(CopyKind ability)
{
    for (int i = 0; i < TR_ABILITY_ITEM_NUM; i++)
        if (topride_ability_items[i].ability == ability)
            return topride_ability_items[i].item;
    return -1;
}

int GateTopRideItems_GiveItem(TopRideItemKind kind)
{
    if ((unsigned)kind >= TRITEM_NUM)
        return 0;

    TopRideKirbyMgr *kirby_mgr = *stc_topride_kirbymgr;
    if (!kirby_mgr)
        return 0;

    // TopRide_KirbyApplyItem derefs the held-item GObj, set only once the race is active.
    if (kirby_mgr->round_state != 2)
        return 0;

    // Not gated on kirby->standing: the solo modes never rank it.
    int applied = 0;
    for (int i = 0; i < 4; i++)
    {
        TopRideKirby *k = kirby_mgr->kirbys[i];
        if (!k)
            continue;
        if (TopRide_GetPlayerKind(k->player_slot) != TR_PKIND_HMN)
            continue;

        TopRide_KirbyApplyItem(k, kind);
        applied++;
    }

    if (applied)
        OSReport("[GateTopRideItems] Applied TR item %d (%s) to %d player(s)\n",
                 kind, TopRideItemKind_Names[kind], applied);
    return applied;
}
