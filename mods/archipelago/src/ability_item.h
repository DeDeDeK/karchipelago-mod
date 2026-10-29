#ifndef ABILITY_ITEM_H
#define ABILITY_ITEM_H

#include "game.h"
#include "item.h"

// Gives the ability straight to every human Kirby on a machine; no ITKIND_COPY* item is
// spawned, so no item data tables are needed. GiveItem also announces. Both return how
// many riders took it.
int Ability_GiveHumans(CopyKind copy_kind);
int Ability_GiveItem(CopyKind copy_kind);

// Returns COPYKIND_NONE for non-copy-ability ItemKinds.
CopyKind Ability_ItKindToCopyKind(ItemKind it_kind);

#endif // ABILITY_ITEM_H
