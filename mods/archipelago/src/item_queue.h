#include "main.h"

#define ITEM_QUEUE_SIZE 50
int Item_Enqueue(APItem item);
int Item_Dequeue(APItem *item);
int ItemQueue_IsEmpty();
int ItemQueue_IsFull();
void ItemQueueGive_PerFrame(GOBJ *g);
void ItemQueue_OnSceneChange();