#ifndef AP_ITEM_HANDLER_H
#define AP_ITEM_HANDLER_H

#include "main.h"

// Outcome of attempting to apply one queued AP item. Numeric values are
// load-bearing: gate handlers that return plain 1/0 map onto APPLIED/RETRY.
typedef enum APItemResult
{
    AP_ITEM_RETRY   = 0, // Can't apply yet - keep and retry next frame
    AP_ITEM_APPLIED = 1, // Applied - remove from the unprocessed queue
    AP_ITEM_DROP    = 2, // Unrecognized / out-of-range ID - remove without applying
} APItemResult;

int APItems_HandleItem(uint ap_item_id);
int APItems_CheckMailbox();
void APItems_PerFrame(GOBJ *g);
void APItems_OnSceneChange();

// Append an AP item ID to the unprocessed queue. Returns 1 on success, 0 if full.
int APItems_Queue(uint ap_item_id);

#endif
