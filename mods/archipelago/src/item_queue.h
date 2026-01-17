#ifndef ITEM_QUEUE_H
#define ITEM_QUEUE_H

#include "main.h"

int Item_Enqueue(APItem item);
int Item_Dequeue(APItem *item);
int ItemQueue_IsEmpty();
int ItemQueue_IsFull();
void ItemQueueGive_PerFrame(GOBJ *g);
void ItemQueue_OnSceneChange();

#endif