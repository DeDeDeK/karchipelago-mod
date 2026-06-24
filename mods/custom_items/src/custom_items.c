// Custom Items - boot, registry storage, and the exported API.

#include "os.h"
#include "hsd.h"
#include "hoshi/mod.h"

#include "custom_items.h"
#include "custom_items_api.h"

// Master toggle. Off gates every custom item out of spawning regardless of the
// per-item flags.
int custom_items_enabled = 1;

static CustomItemEntry stc_registry[CUSTOM_ITEM_MAX];
static int stc_registry_count;

// Handler fired by the pickup hook (item_registry.c) when a custom item is
// collected. NULL until a consumer registers one via the API.
static CustomItemPickupFn stc_pickup_handler;

int CustomItems_GetCount(void)
{
    return stc_registry_count;
}

CustomItemEntry *CustomItems_GetEntry(int index)
{
    if (index < 0 || index >= stc_registry_count)
        return NULL;
    return &stc_registry[index];
}

CustomItemEntry *CustomItems_FindByHash(u32 id_hash)
{
    for (int i = 0; i < stc_registry_count; i++)
    {
        if (stc_registry[i].id_hash == id_hash)
            return &stc_registry[i];
    }
    return NULL;
}

CustomItemEntry *CustomItems_AppendEntry(void)
{
    if (stc_registry_count >= CUSTOM_ITEM_MAX)
        return NULL;

    CustomItemEntry *e = &stc_registry[stc_registry_count++];
    e->file_entrynum = -1;
    e->id_hash = 0;
    e->name[0] = '\0';
    e->enabled = 1;
    e->assigned_kind = -1;
    return e;
}

void CustomItems_CopyName(char *dst, const char *src)
{
    int i = 0;
    if (src != NULL)
    {
        for (; src[i] != '\0' && i < CUSTOM_ITEM_NAME_MAX - 1; i++)
            dst[i] = src[i];
    }
    dst[i] = '\0';
}

// --- Exported API ---

static int Api_GetCount(void)
{
    return stc_registry_count;
}

static u32 Api_GetIdHash(int index)
{
    CustomItemEntry *e = CustomItems_GetEntry(index);
    return e != NULL ? e->id_hash : 0;
}

static const char *Api_GetName(int index)
{
    CustomItemEntry *e = CustomItems_GetEntry(index);
    return e != NULL ? e->name : NULL;
}

static int Api_IsEnabled(u32 id_hash)
{
    CustomItemEntry *e = CustomItems_FindByHash(id_hash);
    return (custom_items_enabled && e != NULL && e->enabled) ? 1 : 0;
}

static void Api_SetEnabled(u32 id_hash, int enabled)
{
    CustomItemEntry *e = CustomItems_FindByHash(id_hash);
    if (e != NULL)
        e->enabled = enabled ? 1 : 0;
}

static int Api_GetAssignedKind(u32 id_hash)
{
    CustomItemEntry *e = CustomItems_FindByHash(id_hash);
    return e != NULL ? e->assigned_kind : -1;
}

static void Api_SetPickupHandler(CustomItemPickupFn handler)
{
    stc_pickup_handler = handler;
}

// Invoked by the pickup hook (item_registry.c) on each custom-item collection.
void CustomItems_FirePickup(u32 id_hash, const char *name, int player)
{
    if (stc_pickup_handler != NULL)
        stc_pickup_handler(id_hash, name, player);
}

static const CustomItemsAPI stc_api = {
    .GetCount         = Api_GetCount,
    .GetIdHash        = Api_GetIdHash,
    .GetName          = Api_GetName,
    .IsEnabled        = Api_IsEnabled,
    .SetEnabled       = Api_SetEnabled,
    .GetAssignedKind  = Api_GetAssignedKind,
    .SetPickupHandler = Api_SetPickupHandler,
};

void CustomItems_OnBoot(void)
{
    int n = CustomItems_Discover();

    // Wire the per-round itData/spawn-weight splice only when there is something
    // to register, so a build with no custom items leaves the game untouched.
    if (n > 0)
        CustomItemRegistry_InstallHook();

    Hoshi_ExportMod((void *)&stc_api);

    OSReport("[CustomItems] Initialized (%d custom item%s discovered, master %s)\n",
             n, n == 1 ? "" : "s", custom_items_enabled ? "enabled" : "disabled");
}
