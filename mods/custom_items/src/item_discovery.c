#include "os.h"
#include "hsd.h"

#include "fst/fst.h"

#include "custom_items.h"

// FNV-1a 32-bit; a per-file identity independent of registry order.
static u32 HashPath(const char *path)
{
    u32 h = 0x811c9dc5u;
    while (*path != '\0')
    {
        h ^= (u8)*path++;
        h *= 0x01000193u;
    }
    return h;
}

// Leaves the provisional filename in place if the descriptor is unusable.
static void ReadDescriptorName(CustomItemEntry *e)
{
    void *mark = HSD_ArenaMark();

    const CustomItemDesc *desc = CustomItems_LoadDescriptor(e->file_entrynum, 1);
    if (desc != NULL && desc->name != NULL)
        CustomItems_CopyName(e->name, desc->name);

    HSD_ArenaRelease(mark);
}

static void CountCb(int entrynum, void *args)
{
    (void)entrynum;
    int *count = (int *)args;
    (*count)++;
}

static void IndexCb(int entrynum, void *args)
{
    (void)args;

    // Without a path there is no id hash, and 0 is the consumers' "not found" value.
    char *path = FST_GetFilePathFromEntrynum(entrynum);
    if (path == NULL)
    {
        OSReport("[CustomItems] No FST path for entry %d - skipped\n", entrynum);
        return;
    }

    CustomItemEntry *e = CustomItems_AppendEntry();
    if (e == NULL) // registry full - already reported
        return;

    e->file_entrynum = entrynum;
    e->id_hash = HashPath(path);
    CustomItems_CopyName(e->name, FST_GetFilenameFromEntrynum(entrynum));
    ReadDescriptorName(e);
    OSReport("[CustomItems] Found %s -> '%s'\n", path, e->name);
}

int CustomItems_Discover(void)
{
    int found = 0;
    FST_ForEachInFolder(CUSTOM_ITEM_DROPIN_DIR, CUSTOM_ITEM_DROPIN_EXT, 0, CountCb, &found);

    if (found == 0)
        return 0;

    if (found > CUSTOM_ITEM_MAX)
        OSReport("[CustomItems] %d files in /%s exceeds cap %d - extra files ignored\n",
                 found, CUSTOM_ITEM_DROPIN_DIR, CUSTOM_ITEM_MAX);

    FST_ForEachInFolder(CUSTOM_ITEM_DROPIN_DIR, CUSTOM_ITEM_DROPIN_EXT, 0, IndexCb, NULL);
    return CustomItems_GetCount();
}
