#ifndef GATE_TOPRIDE_ITEMS_H
#define GATE_TOPRIDE_ITEMS_H

#include "item.h"
#include "rider.h"

void GateTopRideItems_OnBoot(void);
int GateTopRideItems_UnlockItem(TopRideItemKind kind, int announce);

// Marks the three "New Item" checklist rewards received, which is the only way the engine
// enables Chickie, Who? Paint and Lantern.
void GateTopRideItems_MarkNewItemRewardsReceived(void);

// Applies the item to every human kirby. Needs a Top Ride race in progress; returns how
// many kirbys took it.
int GateTopRideItems_GiveItem(TopRideItemKind kind);

// A copy ability's Top Ride item analog, or -1 if it has none.
int GateTopRideItems_AbilityToItem(CopyKind ability);

// The four items Top Ride has in place of copy abilities: the copy ability whose unlock is
// an alternative key to the item's own, and the kirby's state vtable while its power is on.
typedef struct TopRideAbilityItem
{
    TopRideItemKind item;
    CopyKind ability;
    void *power_vt;
} TopRideAbilityItem;

#define TR_ABILITY_ITEM_NUM 4
extern const TopRideAbilityItem topride_ability_items[TR_ABILITY_ITEM_NUM];

#endif
