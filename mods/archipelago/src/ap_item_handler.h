#ifndef AP_ITEM_HANDLER_H
#define AP_ITEM_HANDLER_H

#include "main.h"

// Outcome of attempting to apply one queued AP item. The per-frame scan keeps
// RETRY items and removes APPLIED/DROP ones. Numeric values are load-bearing:
// gate handlers that return plain 1/0 map onto APPLIED/RETRY directly.
typedef enum APItemResult
{
    AP_ITEM_RETRY   = 0, // Can't apply yet (wrong scene, event busy) — keep, retry next frame.
    AP_ITEM_APPLIED = 1, // Applied — remove from the unprocessed queue.
    AP_ITEM_DROP    = 2, // Unrecognized / out-of-range ID — remove without applying.
} APItemResult;

int APItems_HandleItem(uint ap_item_id);
int APItems_CheckMailbox();
void APItems_PerFrame(GOBJ *g);
void APItems_OnSceneChange();

// Append an AP item ID to the unprocessed queue for normal handling on the
// next per-frame scan. Returns 1 on success, 0 if the queue is full.
int APItems_Queue(uint ap_item_id);

#endif
