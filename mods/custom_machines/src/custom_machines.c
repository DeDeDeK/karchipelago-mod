// Registry for drop-in machine archives found in the FST machines/ folder.
//
// Discovery runs at boot, before any scene exists, so descriptors are read with
// a self-contained loader (DVD read into an HSD_MemAlloc buffer, then
// Archive_Init) rather than Archive_LoadFile, which allocates from a per-scene
// heap. Only the descriptor is kept; the archive the engine actually renders
// from is loaded later, by filename, through the widened name table.

#include "os.h"
#include "hsd.h"
#include "menu.h"
#include "hoshi/mod.h"
#include "code_patch/code_patch.h"

#include "fst/fst.h"

#include "custom_machines.h"

static CustomMachineEntry stc_entries[CUSTOM_MACHINE_MAX];
static int stc_count;
static int stc_character_count;

int CustomMachines_GetCount(void)
{
    return stc_count;
}

int CustomMachines_GetCharacterKindCeiling(void)
{
    return CKIND_NUM + stc_character_count;
}

CustomMachineEntry *CustomMachines_GetEntry(int index)
{
    if (index < 0 || index >= stc_count)
        return NULL;
    return &stc_entries[index];
}

CustomMachineEntry *CustomMachines_FindByKind(int machine_kind)
{
    for (int i = 0; i < stc_count; i++)
    {
        if (stc_entries[i].machine_kind == machine_kind)
            return &stc_entries[i];
    }
    return NULL;
}

CustomMachineEntry *CustomMachines_FindByCharacterKind(int character_kind)
{
    for (int i = 0; i < stc_count; i++)
    {
        if (stc_entries[i].character_kind == character_kind)
            return &stc_entries[i];
    }
    return NULL;
}

void CustomMachines_CopyStr(char *dst, const char *src, int max)
{
    int i = 0;
    if (src != NULL)
    {
        for (; i < max - 1 && src[i] != '\0'; i++)
            dst[i] = src[i];
    }
    dst[i] = '\0';
}

// Rewrite the `lis rD, hi` / `addi rD, rS, lo` pair an accessor uses to form a
// table address. Both instructions keep their register fields; only the
// immediates change.
void CustomMachines_RepointTable(u32 lis_addr, u32 addi_addr, const void *table)
{
    u32 addr = (u32)table;
    u32 lo = addr & 0xFFFF;
    u32 hi = (addr >> 16) + ((lo & 0x8000) ? 1 : 0); // addi sign-extends its immediate

    CODEPATCH_REPLACEINSTRUCTION(lis_addr, (*(u32 *)lis_addr & 0xFFFF0000) | hi);
    CODEPATCH_REPLACEINSTRUCTION(addi_addr, (*(u32 *)addi_addr & 0xFFFF0000) | lo);
}

static void FileLoadCallback(int result, void *arg)
{
    (void)result;
    *(volatile int *)arg = 1;
}

// Boot-safe archive load. The buffer and HSD_Archive are never freed: the
// registry holds only strings copied out of them, but the descriptor is read
// before the copy and freeing here would need a heap the boot path lacks.
// ui_frames.c relies on that for the whole run - the menu archives it patches
// point into its side-car.
HSD_Archive *CustomMachines_LoadArchiveAtBoot(char *path)
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

static void CountCb(int entrynum, void *args)
{
    (void)entrynum;
    (*(int *)args)++;
}

static void IndexCb(int entrynum, void *args)
{
    (void)args;
    if (stc_count >= CUSTOM_MACHINE_MAX)
        return;

    char *path = FST_GetFilePathFromEntrynum(entrynum);
    if (path == NULL)
        return;

    HSD_Archive *arc = CustomMachines_LoadArchiveAtBoot(path);
    if (arc == NULL)
    {
        OSReport("[CustomMachines] %s failed to load\n", path);
        return;
    }

    CustomMachineDesc *desc =
        (CustomMachineDesc *)Archive_GetPublicAddress(arc, CUSTOM_MACHINE_SYMBOL);
    if (desc == NULL)
    {
        OSReport("[CustomMachines] %s missing '%s' symbol\n", path, CUSTOM_MACHINE_SYMBOL);
        return;
    }
    if (desc->magic != CUSTOM_MACHINE_MAGIC)
    {
        OSReport("[CustomMachines] %s bad magic 0x%08x\n", path, desc->magic);
        return;
    }
    if (desc->version > CUSTOM_MACHINE_DESC_VERSION)
    {
        OSReport("[CustomMachines] %s descriptor v%d newer than supported v%d\n",
                 path, desc->version, CUSTOM_MACHINE_DESC_VERSION);
        return;
    }
    if (desc->is_bike)
    {
        OSReport("[CustomMachines] %s is a bike; only the star class can be widened\n", path);
        return;
    }
    if (desc->symbol == NULL)
    {
        OSReport("[CustomMachines] %s descriptor has no vcData symbol\n", path);
        return;
    }

    CustomMachineEntry *e = &stc_entries[stc_count];
    CustomMachines_CopyStr(e->path, path, CUSTOM_MACHINE_PATH_MAX);
    CustomMachines_CopyStr(e->symbol, desc->symbol, CUSTOM_MACHINE_NAME_MAX);
    CustomMachines_CopyStr(e->name, desc->name != NULL ? desc->name
                                                       : FST_GetFilenameFromEntrynum(entrynum),
                           CUSTOM_MACHINE_NAME_MAX);
    // The description arrived in v2; a v1 descriptor ends where the field starts.
    CustomMachines_CopyStr(e->description, desc->version >= 2 ? desc->description : NULL,
                           CUSTOM_MACHINE_DESCRIPTION_MAX);
    e->star_slot = VCSTAR_NUM + stc_count;
    e->machine_kind = VCKIND_NUM + stc_count;
    e->character_kind = -1;
    e->rider_kind = desc->rider_kind;
    e->clone_kind = desc->clone_kind;
    e->spawn_weight = desc->spawn_weight;

    // The material cycle arrived in v3, and its palette is read for the rest of
    // the run out of this archive, which the boot loader never frees.
    e->palette_joint = -1;
    if (desc->version >= 3 && desc->palette_joint >= 0 && desc->palette_count > 0 &&
        desc->palette != NULL && desc->palette_period > 0.0f)
    {
        e->palette_joint = desc->palette_joint;
        e->palette_period = desc->palette_period;
        e->palette_count = desc->palette_count;
        e->palette = desc->palette;
    }

    if (desc->wants_character)
    {
        if (CKIND_NUM + stc_character_count < CustomMachineSelect_GetIconMax())
            e->character_kind = CKIND_NUM + stc_character_count++;
        else
            OSReport("[CustomMachines] %s gets no character: the select screens hold %d icons\n",
                     path, CustomMachineSelect_GetIconMax());
    }

    stc_count++;
    OSReport("[CustomMachines] %s -> '%s' (kind %d, star slot %d, character %d)\n",
             path, e->name, e->machine_kind, e->star_slot, e->character_kind);
}

int CustomMachines_Discover(void)
{
    int found = 0;
    FST_ForEachInFolder((char *)CUSTOM_MACHINE_DROPIN_DIR, (char *)CUSTOM_MACHINE_DROPIN_EXT,
                        0, CountCb, &found);
    if (found == 0)
        return 0;

    if (found > CUSTOM_MACHINE_MAX)
        OSReport("[CustomMachines] %d files in /%s exceeds cap %d - extra files ignored\n",
                 found, CUSTOM_MACHINE_DROPIN_DIR, CUSTOM_MACHINE_MAX);

    FST_ForEachInFolder((char *)CUSTOM_MACHINE_DROPIN_DIR, (char *)CUSTOM_MACHINE_DROPIN_EXT,
                        0, IndexCb, NULL);
    return stc_count;
}

static int Api_GetKindCeiling(void)
{
    return VCKIND_NUM + stc_count;
}

static int Api_KindFromClassIndex(int is_bike, int class_index)
{
    if (!is_bike)
    {
        for (int i = 0; i < stc_count; i++)
        {
            if (stc_entries[i].star_slot == class_index)
                return stc_entries[i].machine_kind;
        }
    }
    return MachineKind_FromClassIndex(is_bike, class_index);
}

static int Api_ClassIndexFromKind(int kind, int *out_is_bike)
{
    CustomMachineEntry *e = CustomMachines_FindByKind(kind);
    if (e != NULL)
    {
        if (out_is_bike != NULL)
            *out_is_bike = 0;
        return e->star_slot;
    }
    if (out_is_bike != NULL)
        *out_is_bike = MachineKind_IsBike(kind);
    return MachineKind_ClassIndex(kind);
}

static const char *Api_GetName(int kind)
{
    CustomMachineEntry *e = CustomMachines_FindByKind(kind);
    return e != NULL ? e->name : NULL;
}

static int Api_FindKindByName(const char *name)
{
    if (name == NULL)
        return -1;
    for (int i = 0; i < stc_count; i++)
    {
        const char *a = stc_entries[i].name;
        const char *b = name;
        while (*a != '\0' && *a == *b)
        {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
            return stc_entries[i].machine_kind;
    }
    return -1;
}

static float Api_GetSpawnWeight(int kind)
{
    CustomMachineEntry *e = CustomMachines_FindByKind(kind);
    return e != NULL ? e->spawn_weight : 0.0f;
}

static const CustomMachinesAPI stc_api = {
    .GetGridCols = CustomMachineCharacter_GetGridCols,
    .GetGridSentinel = CustomMachineCharacter_GetSentinel,
    .GetSelectIconMax = CustomMachineSelect_GetIconMax,
    .SetAirRideRowSplit = CustomMachineSelect_SetAirRideRowSplit,
    .SetAvailabilityFilter = CustomMachineSelect_SetAvailabilityFilter,
    .GetCount = CustomMachines_GetCount,
    .GetKindCeiling = Api_GetKindCeiling,
    .GetCharacterKindCeiling = CustomMachines_GetCharacterKindCeiling,
    .KindFromClassIndex = Api_KindFromClassIndex,
    .ClassIndexFromKind = Api_ClassIndexFromKind,
    .GetName = Api_GetName,
    .FindKindByName = Api_FindKindByName,
    .GetSpawnWeight = Api_GetSpawnWeight,
};

void CustomMachines_OnBoot(void)
{
    // Ahead of discovery: the widened art banks ship with this mod whether or not
    // a drop-in machine is present, and the engine has to agree with them either way.
    // This also takes over both select screens' packing, which stays the engine's own
    // roster until a consumer sets an availability filter.
    CustomMachineUiFrames_OnBoot();
    CustomMachineSelect_OnBoot();

    if (CustomMachines_Discover() == 0)
        OSReport("[CustomMachines] No machines found in /%s\n", CUSTOM_MACHINE_DROPIN_DIR);
    else
    {
        CustomMachineRegistry_OnBoot();
        CustomMachineCharacter_OnBoot();
        CustomMachineText_OnBoot();
        CustomMachineAudio_OnBoot();
        CustomMachinePalette_OnBoot();
    }

    // Exported even with nothing registered: this mod owns the select-screen packing
    // either way, so a consumer that gates characters still needs the filter.
    Hoshi_ExportMod((void *)&stc_api);
}
