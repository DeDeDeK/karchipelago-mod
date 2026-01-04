#include "game.h"
#include "inline.h"

#include "item_queue.h"
#include "textbox.h"
#include "city_trial_event.h"
#include "ability_item.h"
#include "patch_item.h"


int Item_Enqueue(APItem APItem) {
    if (ItemQueue_IsFull()) {
        OSReport("Item_Enqueue: Queue is full!\n");
        return 0;
    }
    archipelago_data->item_queue[archipelago_data->item_queue_tail] = APItem;
    archipelago_data->item_queue_tail = (archipelago_data->item_queue_tail + 1) % ITEM_QUEUE_SIZE;
    archipelago_data->item_queue_count++;
    OSReport("Item kind: %d queued.\n", APItem.kind);
    return 1;
}

int Item_Dequeue(APItem *APItem) {
    if (ItemQueue_IsEmpty()) {
        return 0;
    }
    *APItem = archipelago_data->item_queue[archipelago_data->item_queue_head];
    archipelago_data->item_queue_head = (archipelago_data->item_queue_head + 1) % ITEM_QUEUE_SIZE;
    archipelago_data->item_queue_count--;
    OSReport("Item kind: %d dequeued.\n", APItem->kind);
    return 1;
}

int ItemQueue_IsEmpty() {
    return archipelago_data->item_queue_count == 0;
}

int ItemQueue_IsFull() {
    return archipelago_data->item_queue_count >= ITEM_QUEUE_SIZE;
}


// initialize the GOBJ that will check the item queue on every frame
void ItemQueue_OnSceneChange() {
    GOBJ_EZCreator(0, 0, 0, 0, HSD_Free, HSD_OBJKIND_NONE, 0, ItemQueueGive_PerFrame, 0, 0, 0, 0);
}

// check for an item in the queue, and apply ONE item per frame.
// increment item_received_index only if the item was successfully applied.
void ItemQueueGive_PerFrame(GOBJ *g) {
    if (ItemQueue_IsEmpty()) {
        return;
    }

    APItem item = archipelago_data->item_queue[archipelago_data->item_queue_head];

    // apply items that do not require the player to be in a map
    switch (item.kind) {
        case ITEM_KIND_CHECKBOX_FILLER:
            Item_Dequeue(&item);
            return;
        case ITEM_KIND_PATCH_CAP_INCREASE:
            Item_Dequeue(&item);
            return;
        case ITEM_KIND_PROGRESSIVE_STADIUM:
            Item_Dequeue(&item);
            return;
        default:
            break;
    }

    // apply items that require the player to be in city trial or a stadium
    GroundKind gk = Gm_GetCurrentGrKind();
    if (gk == GRKIND_CITY1) {
        switch (item.kind) {
            case ITEM_KIND_CITY_TRIAL_EVENT:
                int event_kind = HSD_Randi(EVKIND_NUM -1);
                if (Event_GiveItem(event_kind)) {
                    save_data->item_received_index++;
                    Item_Dequeue(&item);
                    return;
                } 
            case ITEM_KIND_EFFECT:
                Item_Dequeue(&item);
                return;
            case ITEM_KIND_FILLER:
                Item_Dequeue(&item);
                return;
            case ITEM_KIND_PATCH:
                int patch_kind = HSD_Randi(PATCHKIND_NUM - 1);
                if (Patch_GiveItem(patch_kind, 1)) {
                    save_data->item_received_index++;
                    Item_Dequeue(&item);
                    return;
                }
                // if (Patch_AllUp_GiveItem(1)) {
                //     save_data->item_received_index++;
                //     Item_Dequeue(&item);
                //     return;
                // }
            case ITEM_KIND_ABILITY:
                int ability_kind = HSD_Randi(COPYKIND_NUM - 1);
                if (Ability_GiveItem(ability_kind)) {
                    save_data->item_received_index++;
                    Item_Dequeue(&item);
                    return;
                }
                break;
            default:
                OSReport("Unknown ItemKind: %d\n", item.kind);
                Item_Dequeue(&item);
                return;
        }
    }
}