#ifndef AP_ITEM_HANDLER_H
#define AP_ITEM_HANDLER_H

#include "main.h"

// The gate unlockers return their plain 1 / 0 as APPLIED / RETRY, so the values are fixed.
typedef enum APItemResult
{
    AP_ITEM_RETRY   = 0, // Can't apply yet; stays queued
    AP_ITEM_APPLIED = 1, // Applied; leaves the queue
    AP_ITEM_DROP    = 2, // Unrecognized or never applicable; leaves the queue unapplied
} APItemResult;

// Applies one AP item by ID and returns an APItemResult.
int APItems_HandleItem(uint ap_item_id);
void APItems_OnFrameStart(void);

// Appends an item the mod raises itself, such as an Energy Link purchase. Returns 1 on
// success, 0 if the queue is full.
int APItems_Queue(uint ap_item_id);

// Spawns one item just ahead of a player's machine, to be driven into. box_kind and size
// are the box fields, -1 each for anything else. Returns 0 if that player has no machine.
// City Trial only: the item data tables must be loaded.
int APItems_SpawnForward(int ply, ItemKind kind, int box_kind, int size);

#endif
