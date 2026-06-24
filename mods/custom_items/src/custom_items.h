#ifndef CUSTOM_ITEMS_H
#define CUSTOM_ITEMS_H

#include "datatypes.h"
#include "hsd.h"

#include "custom_items_api.h"

// Maximum number of custom items the registry tracks in one boot. The engine's
// box/event spawn-weight arrays are sized to ITKIND_NUM-1 (68), so only a small
// number of new kinds can coexist before those arrays must be grown - this cap
// reflects that practical ceiling, not an arbitrary limit.
#define CUSTOM_ITEM_MAX       16
#define CUSTOM_ITEM_NAME_MAX  32

// One discovered drop-in item. Populated at boot from the FST scan; the
// descriptor-derived fields (name override, assigned kind) are filled when the
// archive is loaded per scene.
typedef struct CustomItemEntry
{
    int  file_entrynum;             // FST entry of the .dat (re-openable across scenes)
    u32  id_hash;                   // stable identity = hash of the full FST path
    char name[CUSTOM_ITEM_NAME_MAX]; // display name (filename at discovery; descriptor name once loaded)
    int  enabled;                   // per-item spawn gate (menu / API)
    int  assigned_kind;             // ItemKind in the extended itData[] this scene; -1 until registered
} CustomItemEntry;

// Menu-backed master toggle (defined in custom_items.c).
extern int custom_items_enabled;

// --- Lifecycle (custom_items.c) ---
void CustomItems_OnBoot(void);

// --- Registry storage (custom_items.c) ---
int              CustomItems_GetCount(void);
CustomItemEntry *CustomItems_GetEntry(int index);
CustomItemEntry *CustomItems_FindByHash(u32 id_hash);
CustomItemEntry *CustomItems_AppendEntry(void); // NULL if registry full
void             CustomItems_CopyName(char *dst, const char *src);

// Invoke the registered pickup handler for a collected custom item (no-op if none).
void             CustomItems_FirePickup(u32 id_hash, const char *name, int player);

// --- Discovery (item_discovery.c) ---
int CustomItems_Discover(void);                 // FST scan; fills the registry, returns count
u32 CustomItems_HashPath(const char *path);     // FNV-1a 32-bit over the full path

// --- Registration / engine splice (item_registry.c) ---
// Validates a candidate .dat and returns its CustomItemDesc (and, via
// out_archive, the loaded archive). Both are valid only for the current scene -
// Archive_LoadFile allocates from the per-scene heap. Returns NULL on any
// failure (load, missing symbol, bad magic/version).
const CustomItemDesc *CustomItems_LoadDescriptor(int file_entrynum, HSD_Archive **out_archive);

void CustomItemRegistry_InstallHook(void);   // install the engine splice hooks (once at boot)
int  CustomItemRegistry_RegisterAll(void);   // per-round: load + validate + splice itData/weights
void CustomItemRegistry_ReinjectPools(void); // re-append custom kinds after a per-event pool re-bias

#endif // CUSTOM_ITEMS_H
