#ifndef AP_ITEM_HANDLER_H
#define AP_ITEM_HANDLER_H

#include "main.h"

int APItems_HandleItem(uint ap_item_id);
int APItems_CheckMailbox();
void APItems_PerFrame(GOBJ *g);
void APItems_OnSceneChange();

// Append an AP item ID to the unprocessed queue for normal handling on the
// next per-frame scan. Returns 1 on success, 0 if the queue is full.
int APItems_Queue(uint ap_item_id);

#endif
