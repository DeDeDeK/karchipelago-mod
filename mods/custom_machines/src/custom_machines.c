// Registry for drop-in machine archives found in the FST machines/ folder.
//
// Discovery runs at boot, before any scene heap exists, so descriptors are read
// with a self-contained loader (DVD read into an HSD_MemAlloc buffer, then
// Archive_Init) rather than Archive_LoadFile. Everything the registry keeps is
// copied out of the descriptor and the archive freed again; the engine loads its
// own copy later, by filename, through the widened name table.

#include "os.h"
#include "hsd.h"
#include "obj.h"
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

int CustomMachines_GetKindCeiling(void)
{
    return VCKIND_NUM + stc_count;
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

static JOBJDesc *DescAtIndex(JOBJDesc *desc, int *countdown)
{
    for (; desc != NULL; desc = desc->next)
    {
        if ((*countdown)-- == 0)
            return desc;

        JOBJDesc *hit = DescAtIndex(desc->child, countdown);
        if (hit != NULL)
            return hit;
    }
    return NULL;
}

static JOBJ *JointForDesc(JOBJ *jobj, JOBJDesc *desc)
{
    for (; jobj != NULL; jobj = jobj->sibling)
    {
        if (jobj->desc == desc)
            return jobj;

        JOBJ *hit = JointForDesc(jobj->child, desc);
        if (hit != NULL)
            return hit;
    }
    return NULL;
}

JOBJ *CustomMachines_GetMachineJoint(MachineData *md, int joint_index)
{
    if (md == NULL || joint_index < 0 || md->gobj == NULL || md->vcData == NULL ||
        md->vcData->model == NULL)
        return NULL;

    int countdown = joint_index;
    JOBJDesc *desc = DescAtIndex(md->vcData->model->model_root, &countdown);
    if (desc == NULL)
        return NULL;

    return JointForDesc((JOBJ *)md->gobj->hsd_object, desc);
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

// Boot-safe archive load. HSD_MemAlloc is code-patched to hoshi's bump allocator
// for the whole of OnBoot, so the storage is persistent and there is no free -
// a caller that wants it back brackets the load in a mark/release pair.
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

void *CustomMachines_ArenaMark(void)
{
    return *stc_hsd_heap_start;
}

// Gives back everything allocated since the mark, so it is only correct while
// nothing allocated in between is still held.
void CustomMachines_ArenaRelease(void *mark)
{
    *stc_hsd_heap_start = (u8 *)mark;
}

int CustomMachines_SideCarPath(char *dst, int max, const char *src, const char *ext)
{
    int n = 0;
    int e = 0;

    while (src[n] != '\0')
    {
        if (n + 1 >= max)
            return 0;
        dst[n] = src[n];
        n++;
    }
    while (ext[e] != '\0')
        e++;
    if (n < e || dst[n - e] != '.')
        return 0;
    for (int i = 0; i < e; i++)
        dst[n - e + i] = ext[i];
    dst[n] = '\0';
    return 1;
}

static void CountCb(int entrynum, void *args)
{
    (void)entrynum;
    (*(int *)args)++;
}

// Take one candidate's descriptor into the registry. Returns 1 if it registered,
// and only reads through `desc` - the caller frees the archive on the way out.
static int TakeDescriptor(char *path, int entrynum, CustomMachineDesc *desc)
{
    if (desc == NULL)
    {
        OSReport("[CustomMachines] %s missing '%s' symbol\n", path, CUSTOM_MACHINE_SYMBOL);
        return 0;
    }
    if (desc->magic != CUSTOM_MACHINE_MAGIC)
    {
        OSReport("[CustomMachines] %s bad magic 0x%08x\n", path, desc->magic);
        return 0;
    }
    if (desc->version > CUSTOM_MACHINE_DESC_VERSION)
    {
        OSReport("[CustomMachines] %s descriptor v%d newer than supported v%d\n",
                 path, desc->version, CUSTOM_MACHINE_DESC_VERSION);
        return 0;
    }
    if (desc->is_bike)
    {
        OSReport("[CustomMachines] %s is a bike; only the star class can be widened\n", path);
        return 0;
    }
    if (desc->symbol == NULL)
    {
        OSReport("[CustomMachines] %s descriptor has no vcData symbol\n", path);
        return 0;
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

    // The material cycle arrived in v3. The colors are copied rather than pointed
    // at, because the archive they sit in goes away with this call.
    e->palette_joint = -1;
    if (desc->version >= 3 && desc->palette_joint >= 0 && desc->palette_count > 0 &&
        desc->palette != NULL && desc->palette_period > 0.0f)
    {
        int count = desc->palette_count;
        if (count > CUSTOM_MACHINE_PALETTE_MAX)
        {
            OSReport("[CustomMachines] '%s' palette of %d clamped to %d\n",
                     e->name, count, CUSTOM_MACHINE_PALETTE_MAX);
            count = CUSTOM_MACHINE_PALETTE_MAX;
        }
        e->palette_joint = desc->palette_joint;
        e->palette_period = desc->palette_period;
        e->palette_count = count;
        for (int i = 0; i < count; i++)
            e->palette[i] = desc->palette[i];
    }

    // The trail tint rides the same cycle, so it only means anything on a machine
    // that asked for a palette. It took its present shape in v5; a v4 descriptor
    // laid the field out differently and is read as asking for none.
    e->trail_count = 0;
    if (desc->version >= 5 && e->palette_joint >= 0 &&
        desc->trail_count > 0 && desc->trail_count <= 8)
    {
        e->trail_count = desc->trail_count;
        for (int i = 0; i < desc->trail_count; i++)
        {
            e->trail_gen[i] = desc->trail_gen[i];
            e->trail_rgb[i] = desc->trail_rgb[i];
        }
    }

    // Clones are only worth installing for a machine that goes on to tint them.
    e->trail_clone_count = 0;
    if (desc->version >= 6 && e->trail_count > 0 &&
        desc->trail_clone_count > 0 && desc->trail_clone_count <= 4)
    {
        e->trail_clone_count = desc->trail_clone_count;
        for (int i = 0; i < desc->trail_clone_count; i++)
        {
            e->trail_clone_src[i] = desc->trail_clone_src[i];
            e->trail_clone_dst[i] = desc->trail_clone_dst[i];
        }
    }

    // The assembly cinematic arrived in v7. Both archives are needed - one carries
    // the parts that fly in, the other the streaks they ride and the camera - so a
    // descriptor naming only one asks for none.
    e->cine_machine_index = -1;
    if (desc->version >= 7 && desc->cine_glow_file != NULL && desc->cine_parts_file != NULL &&
        desc->cine_glow_symbol != NULL && desc->cine_cam_symbol != NULL &&
        desc->cine_parts_symbol != NULL)
    {
        e->cine_machine_index = desc->cine_machine_index != 0 ? 1 : 0;
        CustomMachines_CopyStr(e->cine_glow_file, desc->cine_glow_file, CUSTOM_MACHINE_NAME_MAX);
        CustomMachines_CopyStr(e->cine_glow_symbol, desc->cine_glow_symbol, CUSTOM_MACHINE_NAME_MAX);
        CustomMachines_CopyStr(e->cine_cam_symbol, desc->cine_cam_symbol, CUSTOM_MACHINE_NAME_MAX);
        CustomMachines_CopyStr(e->cine_parts_file, desc->cine_parts_file, CUSTOM_MACHINE_NAME_MAX);
        CustomMachines_CopyStr(e->cine_parts_symbol, desc->cine_parts_symbol,
                               CUSTOM_MACHINE_NAME_MAX);
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
    return 1;
}

static void IndexCb(int entrynum, void *args)
{
    (void)args;
    if (stc_count >= CUSTOM_MACHINE_MAX)
        return;

    char *path = FST_GetFilePathFromEntrynum(entrynum);
    if (path == NULL)
        return;

    // The descriptor is copied out whole, so the archive is dropped before the next
    // one loads rather than holding every machine's file for the run.
    void *mark = CustomMachines_ArenaMark();
    HSD_Archive *arc = CustomMachines_LoadArchiveAtBoot(path);
    if (arc == NULL)
    {
        OSReport("[CustomMachines] %s failed to load\n", path);
        CustomMachines_ArenaRelease(mark);
        return;
    }

    TakeDescriptor(path, entrynum,
                   (CustomMachineDesc *)Archive_GetPublicAddress(arc, CUSTOM_MACHINE_SYMBOL));
    CustomMachines_ArenaRelease(mark);
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
    return CustomMachines_GetKindCeiling();
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

static int Api_SetStarInitHandler(int kind, CustomMachineStarHandler fn)
{
    return CustomMachineRegistry_SetStarHandler(0, kind, fn);
}

static int Api_SetStarThinkHandler(int kind, CustomMachineStarHandler fn)
{
    return CustomMachineRegistry_SetStarHandler(1, kind, fn);
}

static JOBJ *Api_GetMachineJoint(MachineData *md, int joint_index)
{
    return CustomMachines_GetMachineJoint(md, joint_index);
}

static const u32 *Api_GetPalette(int kind, int *out_count)
{
    CustomMachineEntry *e = CustomMachines_FindByKind(kind);
    if (e == NULL || e->palette_joint < 0)
        return NULL;
    if (out_count != NULL)
        *out_count = e->palette_count;
    return e->palette;
}

static int Api_AddCityPreload(const char *path)
{
    return CustomMachinePreload_Add(path);
}

static const CustomMachinesAPI stc_api = {
    .GetGridCols = CustomMachineCharacter_GetGridCols,
    .GetGridSentinel = CustomMachineCharacter_GetSentinel,
    .GetSelectIconMax = CustomMachineSelect_GetIconMax,
    .SetAirRideRowSplit = CustomMachineSelect_SetAirRideRowSplit,
    .SetAvailabilityFilter = CustomMachineSelect_SetAvailabilityFilter,
    .SetSpawnWeightFilter = CustomMachineSpawn_SetWeightFilter,
    .GetCount = CustomMachines_GetCount,
    .GetKindCeiling = Api_GetKindCeiling,
    .GetCharacterKindCeiling = CustomMachines_GetCharacterKindCeiling,
    .KindFromClassIndex = Api_KindFromClassIndex,
    .ClassIndexFromKind = Api_ClassIndexFromKind,
    .GetName = Api_GetName,
    .FindKindByName = Api_FindKindByName,
    .GetSpawnWeight = Api_GetSpawnWeight,
    .SetStarInitHandler = Api_SetStarInitHandler,
    .SetStarThinkHandler = Api_SetStarThinkHandler,
    .GetMachineJoint = Api_GetMachineJoint,
    .GetPalette = Api_GetPalette,
    .StartAssembly = CustomMachineCinematic_Start,
    .IsAssemblyRunning = CustomMachineCinematic_IsRunning,
    .AddCityPreload = Api_AddCityPreload,
};

void CustomMachines_On3DLoadStart(void)
{
    CustomMachineCinematic_On3DLoadStart();
    CustomMachineStats_On3DLoadStart();
}

void CustomMachines_OnBoot(void)
{
    // Ahead of discovery: the widened select screens ship with this mod whether or
    // not a drop-in machine is present, and the engine has to agree with them either
    // way. This also takes over both screens' packing, which stays the engine's own
    // roster until a consumer sets an availability filter.
    CustomMachineSelect_OnBoot();

    // Owned on the same terms: the engine's own selection loop is VCKIND_NUM wide
    // and cannot reach a registered kind, and a consumer that gates machines needs
    // the weight filter whether or not one is present.
    CustomMachineSpawn_OnBoot();

    if (CustomMachines_Discover() == 0)
        OSReport("[CustomMachines] No machines found in /%s\n", CUSTOM_MACHINE_DROPIN_DIR);
    else
    {
        CustomMachineRegistry_OnBoot();
        CustomMachineCharacter_OnBoot();
        CustomMachineText_OnBoot();
        CustomMachineAudio_OnBoot();
        CustomMachineTrail_OnBoot();
        CustomMachinePalette_OnBoot();
        CustomMachineCinematic_OnBoot();
        CustomMachineBlip_OnBoot();
        CustomMachineStats_OnBoot();
    }

    // After discovery, so each machine's own art side-car is in hand, and after
    // the cinematic, which registers the archives it wants preloaded.
    CustomMachineUiFrames_OnBoot();
    CustomMachinePreload_OnBoot();

    // Exported even with nothing registered: this mod owns the select-screen packing
    // either way, so a consumer that gates characters still needs the filter.
    Hoshi_ExportMod((void *)&stc_api);
}
