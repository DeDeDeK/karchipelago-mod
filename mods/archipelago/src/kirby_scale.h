#ifndef KIRBY_SCALE_H
#define KIRBY_SCALE_H

#include "main.h"

// Returns an APItemResult (AP_ITEM_APPLIED / AP_ITEM_RETRY).
int KirbyScale_HandleItem(uint ap_item_id);

void KirbyScale_On3DLoadEnd(void);
void KirbyScale_OnTopRideLoadEnd(void);
void KirbyScale_OnSceneChange(void);

#endif
