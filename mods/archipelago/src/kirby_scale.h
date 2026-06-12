#ifndef KIRBY_SCALE_H
#define KIRBY_SCALE_H

#include "main.h"

// Big Kirby (AP_ITEM_BIG_KIRBY) / Small Kirby (AP_ITEM_SMALL_KIRBY) cosmetic
// filler: scale every human Kirby model up / down, live in all three modes,
// resetting on scene change. See kirby_scale.c for the lifetime contract.

// Receive handler for the two scale items. Returns an APItemResult
// (AP_ITEM_APPLIED / AP_ITEM_RETRY). Call from APItems_HandleItem.
int KirbyScale_HandleItem(uint ap_item_id);

// Lifecycle hooks, mirroring the other per-frame subsystems.
void KirbyScale_On3DLoadEnd(void);
void KirbyScale_OnTopRideLoadEnd(void);
void KirbyScale_OnSceneChange(void);

#endif
