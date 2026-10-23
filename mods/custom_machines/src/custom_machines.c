#include <string.h>

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "menu.h"
#include "rider.h"
#include "particle.h"
#include "hoshi/mod.h"

#include "fst/fst.h"

#include "custom_machines.h"

// A weight past this is taken for garbage rather than a machine meant to crowd out the field.
#define SPAWN_WEIGHT_MAX 1000.0f

static CustomMachineEntry stc_entries[CUSTOM_MACHINE_MAX];
static int stc_count;
static int stc_class_count[2];  // appended slots handed out per class
static int stc_character_count;
static int stc_generator_count; // generator ids handed out past CUSTOM_MACHINE_GENERATOR_BASE
static int stc_skipped;         // candidates found once the registry was full

int CustomMachines_GetCount(void)
{
    return stc_count;
}

int CustomMachines_GetClassCount(int is_bike)
{
    return stc_class_count[is_bike != 0];
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
    return CustomMachines_GetEntry(machine_kind - VCKIND_NUM);
}

CustomMachineEntry *CustomMachines_FindByClassSlot(int is_bike, int class_slot)
{
    is_bike = is_bike != 0;
    if (class_slot < CustomMachines_VanillaSlotNum(is_bike))
        return NULL;

    for (int i = 0; i < stc_count; i++)
    {
        if (stc_entries[i].is_bike == is_bike && stc_entries[i].class_slot == class_slot)
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

static JOBJ *Api_GetMachineJoint(MachineData *md, int joint_index)
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

int CustomMachines_SideCarPath(char *dst, int max, const char *src, const char *ext)
{
    int n = strlen(src);
    int e = strlen(ext);

    if (n >= max || n < e || src[n - e] != '.')
        return 0;
    memcpy(dst, src, n - e);
    memcpy(dst + n - e, ext, e + 1);
    return 1;
}

static int FindKindByName(const char *name)
{
    if (name == NULL)
        return -1;
    for (int i = 0; i < stc_count; i++)
    {
        if (strcmp(stc_entries[i].name, name) == 0)
            return stc_entries[i].machine_kind;
    }
    return -1;
}

// Whether a string exists and fits a registry buffer whole; one cut short would name a
// different file or symbol, or a name no consumer could look up.
static int Fits(const char *s, int max)
{
    return s != NULL && strlen(s) < (size_t)max;
}

// Take one candidate's descriptor into the registry.
static void TakeDescriptor(char *path, int entrynum, HSD_Archive *arc, CustomMachineDesc *desc)
{
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
    if (desc->version != CUSTOM_MACHINE_DESC_VERSION)
    {
        OSReport("[CustomMachines] %s descriptor v%d, expected v%d\n",
                 path, desc->version, CUSTOM_MACHINE_DESC_VERSION);
        return;
    }
    if (desc->is_bike != 0 && desc->is_bike != 1)
    {
        OSReport("[CustomMachines] %s is_bike %d names no machine class\n", path, desc->is_bike);
        return;
    }

    const char *name = desc->name != NULL ? desc->name : FST_GetFilenameFromEntrynum(entrynum);
    if (!Fits(path, CUSTOM_MACHINE_PATH_MAX) || !Fits(name, CUSTOM_MACHINE_NAME_MAX) ||
        !Fits(desc->symbol, CUSTOM_MACHINE_NAME_MAX))
    {
        OSReport("[CustomMachines] %s path, name or vcData symbol is missing or too long\n", path);
        return;
    }
    // The name is what a consumer binds a machine by, so a second one under it could
    // never be found.
    if (FindKindByName(name) >= 0)
    {
        OSReport("[CustomMachines] %s is named '%s', which is already registered\n", path, name);
        return;
    }

    vcData *vc = (vcData *)Archive_GetPublicAddress(arc, (char *)desc->symbol);
    if (vc == NULL || vc->attr == NULL || vc->model == NULL || vc->model->model_root == NULL ||
        vc->unk_collision_group == NULL || vc->coll_attr == NULL || vc->coll_sphere == NULL ||
        vc->handling_attr == NULL || vc->anim == NULL)
    {
        OSReport("[CustomMachines] %s vcData public '%s' is missing or incomplete\n",
                 path, desc->symbol);
        return;
    }
    if (desc->audio_kind < 0 || desc->audio_kind >= VCKIND_NUM ||
        MachineKind_IsBike(desc->audio_kind) != desc->is_bike)
    {
        OSReport("[CustomMachines] %s audio_kind %d is not a %s kind\n",
                 path, desc->audio_kind, CustomMachines_ClassName(desc->is_bike));
        return;
    }
    if (desc->stat_rows == NULL || desc->cpu == NULL)
    {
        OSReport("[CustomMachines] %s descriptor is missing its stat or CPU rows\n", path);
        return;
    }
    if (desc->wants_character && (desc->rider_kind < 0 || desc->rider_kind >= RDKIND_NUM))
    {
        OSReport("[CustomMachines] %s rider_kind %d is not a RiderKind\n", path, desc->rider_kind);
        return;
    }

    CustomMachineEntry *e = &stc_entries[stc_count];
    CustomMachines_CopyStr(e->path, path, CUSTOM_MACHINE_PATH_MAX);
    CustomMachines_CopyStr(e->symbol, desc->symbol, CUSTOM_MACHINE_NAME_MAX);
    CustomMachines_CopyStr(e->name, name, CUSTOM_MACHINE_NAME_MAX);
    CustomMachines_CopyStr(e->description, desc->description, CUSTOM_MACHINE_DESCRIPTION_MAX);
    e->machine_kind = VCKIND_NUM + stc_count;
    e->is_bike = desc->is_bike;
    e->class_slot = CustomMachines_VanillaSlotNum(e->is_bike) + stc_class_count[e->is_bike];
    e->character_kind = desc->wants_character ? CKIND_NUM + stc_character_count++ : -1;
    e->rider_kind = desc->rider_kind;
    e->audio_kind = desc->audio_kind;
    e->blip_height = desc->blip_height;
    e->radar_drop = desc->radar_drop;

    // A negative weight would eat into every other kind's share of the roll.
    e->spawn_weight = desc->spawn_weight;
    if (!(e->spawn_weight >= 0.0f && e->spawn_weight <= SPAWN_WEIGHT_MAX))
    {
        OSReport("[CustomMachines] '%s' spawn_weight is not a weight, it never spawns loose\n",
                 e->name);
        e->spawn_weight = 0.0f;
    }

    int rows = e->is_bike ? CUSTOM_MACHINE_BIKE_STAT_ROW_NUM : CUSTOM_MACHINE_STAT_ROW_NUM;
    for (int i = 0; i < rows; i++)
    {
        e->stat_rows[i][0] = desc->stat_rows[i * 2];
        e->stat_rows[i][1] = desc->stat_rows[i * 2 + 1];
    }
    e->cpu = *desc->cpu;

    // Generator ids are positional, so a program that fails the check keeps its id with
    // nothing installed behind it. Every program ends on a 0xfe or PTCL_OP_END terminator.
    e->generator_count = desc->generators != NULL ? desc->generator_count : 0;
    if (e->generator_count < 0)
        e->generator_count = 0;
    if (e->generator_count > CUSTOM_MACHINE_GENERATOR_MAX)
    {
        OSReport("[CustomMachines] '%s' generators clamped to %d\n",
                 e->name, CUSTOM_MACHINE_GENERATOR_MAX);
        e->generator_count = CUSTOM_MACHINE_GENERATOR_MAX;
    }
    e->generator_base = CUSTOM_MACHINE_GENERATOR_BASE + stc_generator_count;
    for (int i = 0; i < e->generator_count; i++)
    {
        const CustomMachineGenerator *g = &desc->generators[i];

        e->generator_size[i] = 0;
        if (g->desc == NULL || g->size <= sizeof(struct PtclDesc) ||
            g->size > CUSTOM_MACHINE_GENERATOR_SIZE || g->desc[g->size - 1] < 0xfe)
        {
            OSReport("[CustomMachines] '%s' generator %d dropped\n", e->name, i);
            continue;
        }
        e->generator_size[i] = g->size;
        memcpy(e->generator[i], g->desc, g->size);
    }

    // The ids every loaded copy of the animation bank is given. Below the base is the
    // bank's own generator, shared with every machine naming it.
    int particle_num;
    int *slots = CustomMachines_ParticleSlots(e->is_bike, vc->anim, &particle_num);
    for (int i = 0; i < particle_num; i++)
    {
        int id = slots[i];
        int own = id - CUSTOM_MACHINE_GENERATOR_BASE;

        if (id < -1 || own >= e->generator_count)
        {
            OSReport("[CustomMachines] '%s' animation bank names generator %d, which it does not bring\n",
                     e->name, id);
            id = -1;
        }
        else if (own >= 0)
            id = e->generator_base + own;
        e->particle[i] = id;
    }

    // A descriptor naming neither string asks for no cutscene; one naming only one of
    // them, or one too long, gets none.
    e->cine_machine_index = -1;
    if (Fits(desc->cine_file, CUSTOM_MACHINE_NAME_MAX) &&
        Fits(desc->cine_symbol, CUSTOM_MACHINE_NAME_MAX))
    {
        e->cine_machine_index = desc->cine_machine_index != 0 ? 1 : 0;
        CustomMachines_CopyStr(e->cine_file, desc->cine_file, CUSTOM_MACHINE_NAME_MAX);
        CustomMachines_CopyStr(e->cine_symbol, desc->cine_symbol, CUSTOM_MACHINE_NAME_MAX);
    }
    else if (desc->cine_file != NULL || desc->cine_symbol != NULL)
        OSReport("[CustomMachines] '%s' cutscene strings are incomplete or too long, no cutscene\n",
                 e->name);

    stc_generator_count += e->generator_count;
    stc_class_count[e->is_bike]++;
    stc_count++;
    OSReport("[CustomMachines] %s -> '%s' (kind %d, %s slot %d, character %d)\n",
             path, e->name, e->machine_kind, CustomMachines_ClassName(e->is_bike), e->class_slot,
             e->character_kind);
}

static void IndexCb(int entrynum, void *args)
{
    (void)args;
    if (stc_count >= CUSTOM_MACHINE_MAX)
    {
        stc_skipped++;
        return;
    }

    char *path = FST_GetFilePathFromEntrynum(entrynum);
    if (path == NULL)
        return;

    // Archive_LoadFile allocates out of hoshi's boot arena, which is rewound once the
    // descriptor is copied out: the engine loads its own copy later, by filename.
    void *mark = HSD_ArenaMark();
    HSD_Archive *arc = Archive_LoadFile(path);
    if (arc == NULL)
    {
        OSReport("[CustomMachines] %s failed to load\n", path);
        HSD_ArenaRelease(mark);
        return;
    }

    TakeDescriptor(path, entrynum, arc,
                   (CustomMachineDesc *)Archive_GetPublicAddress(arc, CUSTOM_MACHINE_SYMBOL));
    HSD_ArenaRelease(mark);
}

static void Discover(void)
{
    FST_ForEachInFolder((char *)CUSTOM_MACHINE_DROPIN_DIR, (char *)CUSTOM_MACHINE_DROPIN_EXT,
                        0, IndexCb, NULL);

    if (stc_count == 0)
        OSReport("[CustomMachines] No machines found in /%s\n", CUSTOM_MACHINE_DROPIN_DIR);
    if (stc_skipped > 0)
        OSReport("[CustomMachines] %d file(s) in /%s skipped, the registry holds %d\n",
                 stc_skipped, CUSTOM_MACHINE_DROPIN_DIR, CUSTOM_MACHINE_MAX);
}

int CustomMachines_KindFromClassIndex(int is_bike, int class_index)
{
    CustomMachineEntry *e = CustomMachines_FindByClassSlot(is_bike, class_index);
    return e != NULL ? e->machine_kind : MachineKind_FromClassIndex(is_bike, class_index);
}

int CustomMachines_ClassIndexFromKind(int kind, int *out_is_bike)
{
    CustomMachineEntry *e = CustomMachines_FindByKind(kind);
    if (e != NULL)
    {
        *out_is_bike = e->is_bike;
        return e->class_slot;
    }
    *out_is_bike = MachineKind_IsBike(kind);
    return MachineKind_ClassIndex(kind);
}

static u8 *Api_GetGenerator(int kind, int index, int *out_size)
{
    CustomMachineEntry *e = CustomMachines_FindByKind(kind);
    if (e == NULL || index < 0 || index >= e->generator_count || e->generator_size[index] == 0)
        return NULL;
    if (out_size != NULL)
        *out_size = e->generator_size[index];
    return e->generator[index];
}

static const CustomMachinesAPI stc_api = {
    .GetCount = CustomMachines_GetCount,
    .GetKindCeiling = CustomMachines_GetKindCeiling,
    .GetCharacterKindCeiling = CustomMachines_GetCharacterKindCeiling,
    .KindFromClassIndex = CustomMachines_KindFromClassIndex,
    .ClassIndexFromKind = CustomMachines_ClassIndexFromKind,
    .FindKindByName = FindKindByName,
    .SetAvailabilityFilter = CustomMachineSelectScreen_SetAvailabilityFilter,
    .SetSpawnWeightFilter = CustomMachineSpawn_SetWeightFilter,
    .AddDeathHandler = CustomMachineStats_AddDeathHandler,
    .SetInitHandler = CustomMachineRegistry_SetInitHandler,
    .SetThinkHandler = CustomMachineRegistry_SetThinkHandler,
    .SetAnimHandler = CustomMachineRegistry_SetAnimHandler,
    .GetMachineJoint = Api_GetMachineJoint,
    .GetGenerator = Api_GetGenerator,
    .StartAssembly = CustomMachineCinematic_Start,
    .MountMachine = CustomMachineMount_Queue,
    .GetRespawnKind = CustomMachineMount_GetRespawnKind,
};

void CustomMachines_On3DLoadStart(void)
{
    CustomMachineCinematic_On3DLoadStart();
    CustomMachineStats_On3DLoadStart();
}

void CustomMachines_OnBoot(void)
{
    Discover();

    // Installed, and the API exported, with nothing registered: the select screens' packing,
    // the spawn roll, the KO seam and the legendary cutscenes are what a consumer's filters
    // and handlers reach.
    CustomMachineSelectScreen_OnBoot();
    CustomMachineSpawn_OnBoot();
    CustomMachineStats_OnBoot();
    CustomMachineCinematic_OnBoot();

    if (stc_count > 0)
    {
        CustomMachineRegistry_OnBoot();
        CustomMachineStatScaling_OnBoot();
        CustomMachineCharacterRegistry_OnBoot();
        CustomMachineSelectText_OnBoot();
        CustomMachineAudio_OnBoot();
        CustomMachineTrailBank_OnBoot();
        CustomMachineHud_OnBoot();
        CustomMachineCpu_OnBoot();
    }

    CustomMachineUiFrames_OnBoot();
    Hoshi_ExportMod((void *)&stc_api);
}
