// Drop-in discovery: walk the FST `items/` folder and record each *.dat as a
// registry entry. Two-pass (count, then index) so the cap warning is reported
// once before any entries are added.

#include "os.h"

#include "fst/fst.h"

#include "custom_items.h"

// FNV-1a 32-bit over the full FST path - a stable per-file identity independent
// of registry order.
u32 CustomItems_HashPath(const char *path)
{
    u32 h = 0x811c9dc5u;
    if (path == NULL)
        return 0;
    while (*path != '\0')
    {
        h ^= (u8)*path++;
        h *= 0x01000193u;
    }
    return h;
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

    CustomItemEntry *e = CustomItems_AppendEntry();
    if (e == NULL) // registry full - cap already reported in CustomItems_Discover
        return;

    e->file_entrynum = entrynum;
    e->id_hash = CustomItems_HashPath(FST_GetFilePathFromEntrynum(entrynum));

    // Provisional display name = filename. The descriptor's own name supersedes
    // this once the archive is loaded at registration time.
    CustomItems_CopyName(e->name, FST_GetFilenameFromEntrynum(entrynum));
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
