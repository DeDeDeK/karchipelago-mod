#ifndef CUSTOM_ITEMS_API_H
#define CUSTOM_ITEMS_API_H

#include "datatypes.h"

// New City Trial item kinds loaded from .dat archives in the FST items/ folder.

#define CUSTOM_ITEMS_MOD_NAME  "custom_items"
#define CUSTOM_ITEMS_API_MAJOR 3
#define CUSTOM_ITEMS_API_MINOR 0

// Invoked when a rider collects a custom item; `player` is the 0..4 slot.
typedef void (*CustomItemPickupFn)(u32 id_hash, const char *name, int player);

// Published via Hoshi_ExportMod. Items are addressed by their id hash (derived
// from the .dat's FST path), stable across reboots and folder reordering.
typedef struct CustomItemsAPI
{
    // Number of discovered custom items.
    int (*GetCount)(void);

    // Id hash of the index-th item (0 if out of range).
    u32 (*GetIdHash)(int index);

    // Display name of the index-th item (NULL if out of range).
    const char *(*GetName)(int index);

    // Open or close the item's spawn gate. One gate per item, shared by every
    // consumer, and it defaults open, so an item nobody calls this on spawns
    // freely and two consumers gating the same item is last-writer-wins.
    void (*SetEnabled)(u32 id_hash, int enabled);

    // ItemKind assigned this round, or -1 if not registered yet this scene.
    int (*GetAssignedKind)(u32 id_hash);

    // Subscribe a pickup handler; every registered handler runs on each pickup.
    // A no-op if already present or the table is full.
    void (*AddPickupHandler)(CustomItemPickupFn handler);
} CustomItemsAPI;

#endif // CUSTOM_ITEMS_API_H
