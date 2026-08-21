// Drop-in discovery is two-pass (count, then index) so the cap warning is
// reported once before any entries are added.
//
// Each descriptor is read once here for its display name, which is the handle
// consumer mods bind an item by: it has to be known before the first round
// registers anything, and for an item held disabled it is never registered at all.
// Discovery runs at boot, before any scene exists, so the archive is read with a
// self-contained loader (DVD read into an HSD_MemAlloc buffer, then Archive_Init)
// rather than Archive_LoadFile, which allocates from a per-scene heap. Only the
// name is kept, so each read is bracketed in an arena mark/release - HSD_MemAlloc
// is hoshi's bump allocator for the whole of OnBoot, and holding an item archive
// would cost its full file size for the run.

#include "os.h"
#include "hsd.h"

#include "fst/fst.h"

#include "custom_items.h"

// FNV-1a 32-bit; a per-file identity independent of registry order.
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

static void FileLoadCallback(int result, void *arg)
{
    (void)result;
    *(volatile int *)arg = 1;
}

// HSD_MemAlloc is hoshi's bump allocator during OnBoot, so a mark is the arena's
// next address and a release rewinds to it. Only correct while nothing allocated
// since the mark is still held.
static void *ArenaMark(void)
{
    return *stc_hsd_heap_start;
}

static void ArenaRelease(void *mark)
{
    *stc_hsd_heap_start = (u8 *)mark;
}

static HSD_Archive *LoadArchiveAtBoot(char *path)
{
    int entrynum = DVDConvertPathToEntrynum(path);
    if (entrynum == -1)
        return NULL;

    int size = File_GetSize(path);
    if (size <= 0)
        return NULL;

    void *buffer = HSD_MemAlloc(OSRoundUp32B(size));
    if (buffer == NULL)
        return NULL;

    volatile int loaded = 0;
    File_Read(entrynum, 0, buffer, OSRoundUp32B(size), 0x21, 1, FileLoadCallback, (void *)&loaded);
    while (!loaded)
        ;

    HSD_Archive *archive = HSD_MemAlloc(sizeof(HSD_Archive));
    if (archive == NULL)
        return NULL;
    Archive_Init(archive, buffer, size);
    return archive;
}

// Leaves the provisional filename in place if the archive or its descriptor is
// unusable; the per-round registration reports why. The name is copied out, so the
// archive is dropped before the next one loads.
static void ReadDescriptorName(CustomItemEntry *e, char *path)
{
    void *mark = ArenaMark();
    HSD_Archive *arc = LoadArchiveAtBoot(path);
    if (arc == NULL)
    {
        ArenaRelease(mark);
        return;
    }

    const CustomItemDesc *desc =
        (const CustomItemDesc *)Archive_GetPublicAddress(arc, CUSTOM_ITEM_SYMBOL);
    if (desc != NULL && desc->magic == CUSTOM_ITEM_MAGIC &&
        desc->version <= CUSTOM_ITEM_DESC_VERSION && desc->name != NULL)
        CustomItems_CopyName(e->name, desc->name);

    ArenaRelease(mark);
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
    if (e == NULL) // registry full - already reported
        return;

    char *path = FST_GetFilePathFromEntrynum(entrynum);

    e->file_entrynum = entrynum;
    e->id_hash = CustomItems_HashPath(path);

    // Provisional name; the descriptor's own name supersedes it below.
    char *filename = FST_GetFilenameFromEntrynum(entrynum);
    CustomItems_CopyName(e->name, filename);

    // menu_label is never overwritten by the descriptor, so the per-item
    // toggle's save hash (keyed on the option name) stays stable across reboots.
    CustomItems_CopyName(e->menu_label, filename);
    int dot = -1;
    for (int i = 0; e->menu_label[i] != '\0'; i++)
    {
        if (e->menu_label[i] == '.')
            dot = i;
    }
    if (dot > 0) // keep a leading-dot name intact; strip only a real extension
        e->menu_label[dot] = '\0';

    if (path != NULL)
        ReadDescriptorName(e, path);
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
