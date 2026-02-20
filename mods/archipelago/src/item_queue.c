#include "game.h"
#include "inline.h"

#include "item_queue.h"
#include "textbox.h"
#include "city_trial_event.h"
#include "ability_item.h"
#include "patch_item.h"
#include "spawn_item.h"

int ItemQueue_IsEmpty() {
    return archipelago_data->item_queue_head == archipelago_data->item_queue_tail;
}

int ItemQueue_IsFull() {
    uint next_tail = (archipelago_data->item_queue_tail + 1) % ITEM_QUEUE_SIZE;
    return next_tail == archipelago_data->item_queue_head;
}

int Item_Enqueue(uint item_id) {
    if (ItemQueue_IsFull()) {
        OSReport("Item_Enqueue: Queue is full!\n");
        return 0;
    }
    archipelago_data->item_queue[archipelago_data->item_queue_tail] = item_id;
    archipelago_data->item_queue_tail = (archipelago_data->item_queue_tail + 1) % ITEM_QUEUE_SIZE;
    OSReport("AP item ID %d queued.\n", item_id);
    return 1;
}

int Item_Dequeue(uint *item_id) {
    if (ItemQueue_IsEmpty()) {
        return 0;
    }
    *item_id = archipelago_data->item_queue[archipelago_data->item_queue_head];
    archipelago_data->item_queue_head = (archipelago_data->item_queue_head + 1) % ITEM_QUEUE_SIZE;
    OSReport("AP item ID %d dequeued.\n", *item_id);
    return 1;
}

// Check the mailbox for an incoming item from the AP client.
// If present, enqueue it internally, then clear the mailbox.
void ItemQueue_CheckMailbox() {
    uint incoming = archipelago_data->incoming_item_id;
    if (incoming == 0) {
        return;
    }

    Item_Enqueue(incoming);

    // Clear the mailbox so the client can write the next item
    archipelago_data->incoming_item_id = 0;
}

// Called after an item is successfully applied.
// Increments both the live mirror and the persistent save copy.
static void ItemReceived_Increment() {
    save_data->item_received_index++;
    archipelago_data->item_received_index = save_data->item_received_index;
}

// Handle an AP item by its raw ID. Maps the ID directly to game behavior.
// Returns 1 if the item was successfully applied, 0 if it can't be applied yet
// (e.g., player is not in the right scene).
int ItemQueue_HandleItem(uint ap_item_id) {
    // Items that apply immediately (no map required)
    switch (ap_item_id) {
        case AP_ITEM_CHECKBOX_FILLER:
        case AP_ITEM_PROGRESSIVE_STADIUM:
        case AP_ITEM_PATCH_CAP_INCREASE:
            return 1;
        default:
            break;
    }

    // All remaining items require City Trial or a stadium
    if (Gm_GetCurrentGrKind() < GRKIND_CITY1) {
        return 0;
    }

    // Permanent +1 patches (AP_PERM_PATCH_BASE + PatchKind)
    if (ap_item_id >= AP_PERM_PATCH_BASE && ap_item_id < AP_PERM_PATCH_BASE + PATCHKIND_NUM) {
        PatchKind kind = ap_item_id - AP_PERM_PATCH_BASE;
        return Patch_GiveItem(kind, 1);
    }

    // Permanent +1 All Up
    if (ap_item_id == AP_ITEM_PERM_ALL_UP) {
        return Patch_AllUp_GiveItem(1);
    }

    // City Trial events (AP_EVENT_BASE + EventKind)
    if (ap_item_id >= AP_EVENT_BASE && ap_item_id < AP_EVENT_BASE + EVKIND_NUM) {
        EventKind kind = ap_item_id - AP_EVENT_BASE;
        return Event_GiveItem(kind);
    }

    // Direct ITKIND items (AP_ITKIND_BASE + ItemKind)
    if (ap_item_id >= AP_ITKIND_BASE && ap_item_id < AP_ITKIND_BASE + ITKIND_NUM) {
        ItemKind it_kind = ap_item_id - AP_ITKIND_BASE;
        SpawnItemHumans(it_kind);
        return 1;
    }

    // HP Trap
    if (ap_item_id == AP_ITEM_HP_TRAP) {
        // TODO: implement HP trap behavior (damage player by 1 HP)
        return 1;
    }

    OSReport("Unknown AP item ID: %d\n", ap_item_id);
    return 1;
}

// Initialize the GOBJ that will check the item queue on every frame
void ItemQueue_OnSceneChange() {
    GOBJ_EZCreator(0, 0, 0, 0, HSD_Free, HSD_OBJKIND_NONE, 0, ItemQueueGive_PerFrame, 0, 0, 0, 0);
}

// Check for an item in the queue, and apply ONE item per frame.
// Increment item_received_index for every item that is dequeued.
void ItemQueueGive_PerFrame(GOBJ *g) {
    // Pull from mailbox into internal queue
    ItemQueue_CheckMailbox();

    if (ItemQueue_IsEmpty()) {
        return;
    }

    // Peek at the head item
    uint item_id = archipelago_data->item_queue[archipelago_data->item_queue_head];

    if (ItemQueue_HandleItem(item_id)) {
        Item_Dequeue(&item_id);
        ItemReceived_Increment();
    }
}
