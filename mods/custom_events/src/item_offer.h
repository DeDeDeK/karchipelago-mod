#ifndef ITEM_OFFER_H
#define ITEM_OFFER_H

#include "item.h"

// Whether the round offers the kind: a box pool lists it or an event source weighs it.
// A mod that locks an item kind takes it out of both.
int ItemOffer_IsOffered(ItemKind kind);

#endif // ITEM_OFFER_H
