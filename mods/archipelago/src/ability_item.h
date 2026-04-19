#ifndef ABILITY_ITEM_H
#define ABILITY_ITEM_H

#include "game.h"
#include "item.h"

int Ability_GiveItem(CopyKind copy_kind);

// Map an ITKIND_COPY* value to the matching CopyKind.
// Returns COPYKIND_NONE for non-copy-ability ItemKinds.
CopyKind Ability_ItKindToCopyKind(ItemKind it_kind);

#endif