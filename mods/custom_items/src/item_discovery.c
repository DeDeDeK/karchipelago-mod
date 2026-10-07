#include <string.h>

#include "game.h"
#include "os.h"
#include "hsd.h"

#include "fst/fst.h"

#include "custom_items.h"

#define CUSTOM_ITEM_DROPIN_DIR "items"
#define CUSTOM_ITEM_DROPIN_EXT ".dat"

// Prints why a descriptor is rejected. The .dat cannot change while the game runs,
// so only a file that passes here is registered and none is checked again.
static const CustomItemDesc *LoadDescriptor(char *path)
{
    HSD_Archive *arc = Archive_LoadFile(path);
    if (arc == NULL)
    {
        OSReport("[CustomItems] %s failed to load\n", path);
        return NULL;
    }

    const CustomItemDesc *desc = Archive_GetPublicAddress(arc, CUSTOM_ITEM_SYMBOL);
    if (desc == NULL)
        OSReport("[CustomItems] %s missing '%s' symbol\n", path, CUSTOM_ITEM_SYMBOL);
    else if (desc->magic != CUSTOM_ITEM_MAGIC)
        OSReport("[CustomItems] %s bad magic 0x%08x\n", path, desc->magic);
    else if (desc->version != CUSTOM_ITEM_DESC_VERSION)
        OSReport("[CustomItems] %s descriptor v%d, expected v%d\n",
                 path, desc->version, CUSTOM_ITEM_DESC_VERSION);
    else if (desc->name == NULL)
        OSReport("[CustomItems] %s has no name\n", path);
    else if (desc->base_kind < 0 || desc->base_kind >= ITKIND_NUM)
        OSReport("[CustomItems] %s base kind %d out of range\n", path, desc->base_kind);
    else
        return desc;
    return NULL;
}

static int CountClampedWeights(const CustomItemDesc *desc)
{
    int clamped = 0;
    for (int b = 0; b < BOXKIND_NUM; b++)
        clamped += desc->weight_box[b] > CUSTOM_ITEM_BOX_WEIGHT_MAX;
    for (int s = 0; s < CUSTOM_ITEM_EVSRC_NUM; s++)
        clamped += desc->weight_event[s] > CUSTOM_ITEM_EVENT_WEIGHT_MAX;
    return clamped;
}

static void IndexCb(int entrynum, void *args)
{
    int *skipped = args;

    // NULL when the path overflows FST's 128-byte buffer.
    char *path = FST_GetFilePathFromEntrynum(entrynum);
    if (path == NULL)
    {
        OSReport("[CustomItems] No FST path for entry %d, skipped\n", entrynum);
        return;
    }

    if (CustomItems_GetCount() >= CUSTOM_ITEM_MAX)
    {
        (*skipped)++;
        return;
    }

    // OnBoot loads into hoshi's persistent arena, so each archive is released once
    // read rather than holding every drop-in for the whole run.
    void *mark = HSD_ArenaMark();

    const CustomItemDesc *desc = LoadDescriptor(path);
    if (desc != NULL)
    {
        CustomItemEntry *e = CustomItems_AppendEntry();
        e->file_entrynum = entrynum;
        e->id_hash = (u32)hash_32_str(path);
        if (e->id_hash == 0) // 0 means "not found" to API callers
            e->id_hash = 1;
        strncpy(e->name, desc->name, CUSTOM_ITEM_NAME_MAX - 1);
        e->name[CUSTOM_ITEM_NAME_MAX - 1] = '\0';

        int clamped = CountClampedWeights(desc);
        if (clamped > 0)
            OSReport("[CustomItems] %s: %d weight(s) past the engine's range clamped\n",
                     path, clamped);
        OSReport("[CustomItems] Found %s -> '%s'\n", path, e->name);
    }

    HSD_ArenaRelease(mark);
}

int CustomItems_Discover(void)
{
    int skipped = 0;
    FST_ForEachInFolder(CUSTOM_ITEM_DROPIN_DIR, CUSTOM_ITEM_DROPIN_EXT, 0, IndexCb, &skipped);

    if (skipped > 0)
        OSReport("[CustomItems] %d file(s) in /%s past the cap of %d ignored\n",
                 skipped, CUSTOM_ITEM_DROPIN_DIR, CUSTOM_ITEM_MAX);

    return CustomItems_GetCount();
}
