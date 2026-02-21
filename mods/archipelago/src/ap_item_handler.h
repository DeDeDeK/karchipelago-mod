#ifndef AP_ITEM_HANDLER_H
#define AP_ITEM_HANDLER_H

#include "main.h"

int APItems_HandleItem(uint ap_item_id);
void APItems_CheckMailbox();
void APItems_PerFrame(GOBJ *g);
void APItems_OnSceneChange();

#endif
