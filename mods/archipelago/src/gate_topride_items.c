#include "game.h"
#include "os.h"
#include "topride.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_topride_items.h"
#include "textbox_api.h"
#include "inline.h"
#include "ap_announce.h"

// One bit per Top Ride item kind whose blocked spawn has been reported this round.
static u32 stc_blocked_reported;

// TR items whose copy ability unlock is an alternative key to their own TR unlock.
static const struct { TopRideItemKind item; CopyKind ability; } ability_items[] = {
    { TRITEM_FREEZE_FAN, COPYKIND_FREEZE },
    { TRITEM_FIRE,       COPYKIND_FIRE },
    { TRITEM_BOMB,       COPYKIND_BOMB },
    { TRITEM_WALKY,      COPYKIND_MIC },
};

// Applies the unlock mask to the ItemMgr's enabled bitmask.
void GateTopRideItems_ApplyMask()
{
    TopRideItemMgr *mgr = *stc_topride_itemmgr;
    if (!mgr)
        return;

    u32 before = mgr->enabled_mask;

    // Fold the ability-derived bits in so either key enables the item, but only while
    // ability gating is on: an ungated world holds an all-1s ability mask, which would
    // free the four items outright and leave their TR item unlocks with nothing to do.
    u32 allowed = ap_save->topride_item_unlocked_mask;
    u16 ability_mask = ap_save->ability_unlocked_mask;
    if (ap_save->options.ability_gating_enabled)
    {
        for (int i = 0; i < (int)(sizeof(ability_items) / sizeof(ability_items[0])); i++)
        {
            if (ability_mask & (1 << ability_items[i].ability))
                allowed |= (1 << ability_items[i].item);
        }
    }

    mgr->enabled_mask &= allowed;

    // Slot 12 (TRITEM_PARTY_BALL_ALT, KirbyKusdama) is the engine's twin Party Ball
    // variant. AP exposes only slot 21, so bit 21's state is mirrored onto bit 12;
    // without this the kusdama variant never spawns.
    if (mgr->enabled_mask & (1 << TRITEM_PARTY_BALL))
        mgr->enabled_mask |= (1 << TRITEM_PARTY_BALL_ALT);
    else
        mgr->enabled_mask &= ~(1 << TRITEM_PARTY_BALL_ALT);

    stc_blocked_reported = 0;
    OSReport("[GateTopRideItems] Enabled mask %s -> %s (item %s, ability %s)\n",
             MaskBits(before, TRITEM_NUM), MaskBits(mgr->enabled_mask, TRITEM_NUM),
             MaskBits(ap_save->topride_item_unlocked_mask, TRITEM_NUM),
             MaskBits(ability_mask, 16));
}

// Hook at 0x802db05c, right after TopRideItem_MgrInit (0x8034b5f4) returns in
// TopRide_KirbyMgrInit (0x802dafb4).
CODEPATCH_HOOKCREATE(0x802db05c,
    "",
    GateTopRideItems_ApplyMask,
    "",
    0
)

// Hook at entry of TopRideItem_SpawnAtPosition (0x8034bf50). Returns 1 to block the
// spawn (locked item, mask bit clear), 0 to let it through; the block path returns to
// the original caller via the function's epilogue blr at 0x8034c12c.
int GateTopRideItems_FilterSpawn(TopRideItemMgr *mgr, int item_kind,
                                 Vec3 *pos, Vec3 *orient,
                                 unsigned int flag1, unsigned int flag2)
{
    if (!mgr)
        return 0;
    // TopRideItem_PartyBallUpdate (frame 0xFF) picks via weighted random. With
    // every TR item locked, sum == 0 and the pick loop falls out at TRITEM_NUM;
    // letting that through makes TopRideItem_Create read past the descriptor
    // table at 0x804ea2fc and crash on a garbage model-name pointer.
    if (item_kind < 0 || item_kind >= TRITEM_NUM)
    {
        OSReport("[GateTopRideItems] Blocked spawn of out-of-range kind %d\n", item_kind);
        return 1;
    }
    if (mgr->enabled_mask & (1 << item_kind))
        return 0;

    // Party balls re-roll a locked kind repeatedly, so say it once per kind.
    if (!(stc_blocked_reported & (1u << item_kind)))
    {
        stc_blocked_reported |= (1u << item_kind);
        OSReport("[GateTopRideItems] Blocked spawn of locked kind %d (%s)\n",
                 item_kind, TopRideItemKind_Names[item_kind]);
    }
    return 1;
}

// Saves r3-r8 (the original SpawnAtPosition args) across the bl into the filter, since
// the return value clobbers r3 and the function immediately derefs it (lwz r3, 4(r3) at
// 0x8034bf68). Proceed path: restore args + LR + frame, then `b 0x1c` past the
// block-path tail and the macro's cmpwi/bne, landing on the clobbered instruction with
// r3 = mgr. Block path: restore LR + frame, set r3 = 1 so the macro branches to the alt
// addr 0x8034c12c via the saved LR.
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

// The Party Ball burst (TopRideItem_PartyBallUpdate, 0x80356dac, frame 0xFF) runs a
// weighted-random picker over all 22 items with no enabled_mask check, reading each
// weight via `bl TopRideItem_GetDataByIndex` then `lfs f0, 16(r3)`. Redirecting those
// two bl's here returns a weight-0 stub for locked kinds.
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
    CODEPATCH_REPLACECALL(0x803574a4, GateTopRideItems_GetDataGated); // burst sum loop
    CODEPATCH_REPLACECALL(0x803574d0, GateTopRideItems_GetDataGated); // burst pick loop
    OSReport("[GateTopRideItems] Top Ride item gating hooks installed\n");
}

// Chickie / Who? Paint / Lantern are unreachable through the unlock mask alone:
// TopRideItem_MgrInit (0x8034b5f4) clears enabled-mask bits 20/18/15 unless
// ClearChecker_CheckUnlocked(GMMODE_TOPRIDE, reward 8/9/10) passes, and ApplyMask only
// ANDs. Marking the reward received is the only way to enable them - the received bit
// only, since an is_unlocked / clear[] write would badge the cell and send a spurious
// check.
static void MarkNewItemRewardReceived(TopRideItemKind kind)
{
    u8 reward_index;
    switch (kind)
    {
        case TRITEM_CHICKIE:   reward_index = 8;  break;
        case TRITEM_WHO_PAINT: reward_index = 9;  break;
        case TRITEM_LANTERN:   reward_index = 10; break;
        default: return;
    }

    ap_save->received_checklist_rewards[GMMODE_TOPRIDE] |= (1ULL << reward_index);
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

    // TopRide_KirbyApplyItem dereferences kirby+0x7c (held item GObj), which is only
    // populated once the race is active; round_state == 2 doubles as the "kirby is
    // fully wired up" gate.
    if (kirby_mgr->round_state != 2)
        return 0;

    // Deliberately not gated on kirby->is_active: that bit is only set during a Race
    // round, never in Time Attack or Free Run, even while the human is playing.
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
        OSReport("[GateTopRideItems] Applied TR item %d (%s) to player %d\n",
                 kind, TopRideItemKind_Names[kind], i);
    }
    return applied;
}
