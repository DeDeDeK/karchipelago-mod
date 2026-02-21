#include "game.h"
#include "inline.h"

#include "ap_item_handler.h"
#include "textbox.h"
#include "city_trial_event.h"
#include "ability_item.h"
#include "patch_item.h"
#include "spawn_item.h"

// Check the mailbox for an incoming item from the AP client.
// Store it in the persistent received list, add to unprocessed list,
// and immediately acknowledge receipt.
void APItems_CheckMailbox() {
    uint incoming = archipelago_data->incoming_item_id;
    if (incoming == 0) {
        return;
    }

    uint idx = save_data->item_received_count;
    if (idx >= MAX_RECEIVED_ITEMS) {
        OSReport("APItems_CheckMailbox: received list is full!\n");
        archipelago_data->incoming_item_id = 0;
        return;
    }

    // Store in persistent received list
    save_data->received_items[idx] = incoming;
    save_data->item_received_count++;

    // Add to unprocessed list
    save_data->unprocessed_items[save_data->unprocessed_count] = incoming;
    save_data->unprocessed_count++;

    // Sync received count to shared memory so AP client can read it
    archipelago_data->item_received_index = save_data->item_received_count;

    OSReport("AP item ID %d received (index %d).\n", incoming, idx);

    // Clear the mailbox so the client can write the next item
    archipelago_data->incoming_item_id = 0;
}

// Handle an AP item by its raw ID. Maps the ID directly to game behavior.
// Returns 1 if the item was successfully applied, 0 if it can't be applied yet
// (e.g., player is not in the right scene, or an event is already active).
int APItems_HandleItem(uint ap_item_id) {
    // Items that apply immediately (no map required)
    switch (ap_item_id) {
        case AP_ITEM_CHECKBOX_FILLER:
        case AP_ITEM_PROGRESSIVE_STADIUM:
        case AP_ITEM_PATCH_CAP_INCREASE:
            return 1;
        default:
            break;
    }

    // All remaining items require being in an actual 3D game scene
    // with the intro/countdown finished
    MajorKind major = Scene_GetCurrentMajor();
    if (major != MJRKIND_CITY && major != MJRKIND_AIR && major != MJRKIND_TOP) {
        return 0;
    }
    if (Gm_GetIntroState() != GMINTRO_END) {
        return 0;
    }

    // Permanent +1 patches (AP_PERM_PATCH_BASE + PatchKind)
    if (ap_item_id >= AP_PERM_PATCH_BASE && ap_item_id < AP_PERM_PATCH_BASE + PATCHKIND_NUM) {
        PatchKind kind = ap_item_id - AP_PERM_PATCH_BASE;
        return Patch_GiveItem(kind, 1);
    }

    // Permanent +1 All Up
    if (ap_item_id == AP_ITEM_PERM_PATCH_ALL_UP) {
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

    // Non-permanent All Up (spawns ITKIND_ALLUP for all human players)
    if (ap_item_id == AP_ITEM_ALL_UP) {
        SpawnItemHumans(ITKIND_ALLUP);
        return 1;
    }

    // HP Trap
    if (ap_item_id == AP_ITEM_1_HP_TRAP) {
        // TODO: implement HP trap behavior (damage player by 1 HP)
        return 1;
    }

    OSReport("Unknown AP item ID: %d\n", ap_item_id);
    return 1;
}

// Initialize the GOBJ that will process received items every frame
void APItems_OnSceneChange() {
    GOBJ_EZCreator(0, 0, 0, 0, HSD_Free, HSD_OBJKIND_NONE, 0, APItems_PerFrame, 0, 0, 0, 0);
}

// Scan the unprocessed list for the first item that can be applied.
// Processes one item per frame. Items that can't apply yet (e.g., blocked events)
// are skipped, allowing items behind them to process out of order.
void APItems_PerFrame(GOBJ *g) {
    // Pull from mailbox into persistent list
    APItems_CheckMailbox();

    // Scan unprocessed items for one we can handle
    for (uint i = 0; i < save_data->unprocessed_count; i++) {
        uint item_id = save_data->unprocessed_items[i];
        if (APItems_HandleItem(item_id)) {
            OSReport("AP item ID %d applied.\n", item_id);
            // Remove by swapping with last element
            save_data->unprocessed_count--;
            save_data->unprocessed_items[i] = save_data->unprocessed_items[save_data->unprocessed_count];
            return;
        }
    }
}
