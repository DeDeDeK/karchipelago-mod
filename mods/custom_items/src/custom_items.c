#include "os.h"
#include "hoshi/mod.h"

#include "custom_items.h"

static CustomItemEntry stc_registry[CUSTOM_ITEM_MAX];
static int stc_registry_count;

#define CUSTOM_ITEM_PICKUP_HANDLERS_MAX 4
static CustomItemPickupFn stc_pickup_handlers[CUSTOM_ITEM_PICKUP_HANDLERS_MAX];

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

static CustomItemEntry *FindByHash(u32 id_hash)
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
    CustomItemEntry *e = &stc_registry[stc_registry_count++];
    e->api_enabled = 1;
    e->assigned_kind = -1;
    return e;
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

static void Api_SetEnabled(u32 id_hash, int enabled)
{
    CustomItemEntry *e = FindByHash(id_hash);
    if (e != NULL)
        e->api_enabled = enabled ? 1 : 0;
}

static int Api_GetAssignedKind(u32 id_hash)
{
    CustomItemEntry *e = FindByHash(id_hash);
    return e != NULL ? e->assigned_kind : -1;
}

// Handlers are never removed, so the table stays packed from slot 0.
static void Api_AddPickupHandler(CustomItemPickupFn handler)
{
    for (int i = 0; i < CUSTOM_ITEM_PICKUP_HANDLERS_MAX; i++)
    {
        if (stc_pickup_handlers[i] == handler)
            return;
        if (stc_pickup_handlers[i] == NULL)
        {
            stc_pickup_handlers[i] = handler;
            return;
        }
    }
    OSReport("[CustomItems] Pickup handler table full (max %d)\n",
             CUSTOM_ITEM_PICKUP_HANDLERS_MAX);
}

void CustomItems_FirePickup(u32 id_hash, int player)
{
    for (int i = 0; i < CUSTOM_ITEM_PICKUP_HANDLERS_MAX && stc_pickup_handlers[i] != NULL; i++)
        stc_pickup_handlers[i](id_hash, player);
}

static const CustomItemsAPI stc_api = {
    .GetCount         = CustomItems_GetCount,
    .GetIdHash        = Api_GetIdHash,
    .GetName          = Api_GetName,
    .SetEnabled       = Api_SetEnabled,
    .GetAssignedKind  = Api_GetAssignedKind,
    .AddPickupHandler = Api_AddPickupHandler,
    .GetItemKind      = CustomItemRegistry_GetItemKind,
};

// Assignments are per scene; GetAssignedKind answers -1 until this scene registers.
void CustomItems_On3DLoadStart(void)
{
    for (int i = 0; i < stc_registry_count; i++)
        stc_registry[i].assigned_kind = -1;
    CustomItemRegistry_ResetScene();
}

void CustomItems_OnBoot(void)
{
    int n = CustomItems_Discover();

    // With nothing to register, install no hooks so vanilla play is untouched.
    if (n > 0)
        CustomItemRegistry_InstallHooks();

    Hoshi_ExportMod((void *)&stc_api);

    OSReport("[CustomItems] Initialized (%d custom item%s discovered), API exported\n",
             n, n == 1 ? "" : "s");
}
