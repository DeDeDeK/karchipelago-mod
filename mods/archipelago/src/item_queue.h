#ifndef ITEM_QUEUE_H
#define ITEM_QUEUE_H

#include "main.h"

int Item_Enqueue(uint item_id);
int Item_Dequeue(uint *item_id);
int ItemQueue_IsEmpty();
int ItemQueue_IsFull();
int ItemQueue_HandleItem(uint ap_item_id);
void ItemQueue_CheckMailbox();
void ItemQueueGive_PerFrame(GOBJ *g);
void ItemQueue_OnSceneChange();

#endif