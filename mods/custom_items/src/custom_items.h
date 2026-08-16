#ifndef CUSTOM_ITEMS_H
#define CUSTOM_ITEMS_H

#include "datatypes.h"
#include "hsd.h"

#include "custom_items_api.h"

// Registry cap. The engine's box/event spawn-weight arrays hold ITKIND_NUM-1
// (68) entries, so only a few new kinds fit without growing them.
#define CUSTOM_ITEM_MAX       16
#define CUSTOM_ITEM_NAME_MAX  32

typedef struct CustomItemEntry
{
    int  file_entrynum;             // FST entry of the .dat (re-openable across scenes)
    u32  id_hash;                   // stable identity = hash of the full FST path
    char name[CUSTOM_ITEM_NAME_MAX]; // descriptor's display name, read at discovery; filename if unreadable
    char menu_label[CUSTOM_ITEM_NAME_MAX]; // filename minus extension; never overwritten, so the toggle's save hash is stable
    int  enabled;                   // the menu toggle, owned by the player and persisted by hoshi
    int  api_enabled;               // consumer-mod gate, default 1; ANDed with enabled to spawn
    int  assigned_kind;             // ItemKind in the extended itData[] this scene; -1 until registered
} CustomItemEntry;

// Master toggle; off gates every custom item out of spawning.
extern int custom_items_enabled;

void CustomItems_OnBoot(void);

int              CustomItems_GetCount(void);
CustomItemEntry *CustomItems_GetEntry(int index);
CustomItemEntry *CustomItems_FindByHash(u32 id_hash);
CustomItemEntry *CustomItems_AppendEntry(void); // NULL if registry full
void             CustomItems_CopyName(char *dst, const char *src);

// Fires every subscribed pickup handler (no-op if none).
void             CustomItems_FirePickup(u32 id_hash, const char *name, int player);

int CustomItems_Discover(void);                 // FST scan; fills the registry, returns count
u32 CustomItems_HashPath(const char *path);     // FNV-1a 32-bit over the full path

// Loads and validates a candidate .dat, returning its descriptor (and the
// archive via out_archive) or NULL. Both are valid only for the current scene.
const CustomItemDesc *CustomItems_LoadDescriptor(int file_entrynum, HSD_Archive **out_archive);

void CustomItemRegistry_InstallHook(void);   // install the engine splice hooks (once at boot)
int  CustomItemRegistry_RegisterAll(void);   // per-round: load + validate + splice itData/weights
void CustomItemRegistry_ReinjectPools(void); // re-append custom kinds after a per-event pool re-bias

#endif // CUSTOM_ITEMS_H
