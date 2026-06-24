// Registration / engine splice: turn discovered custom-item .dats into spawnable
// ItemKinds for the current City Trial round - grow itData[], lift the kind
// ceiling, clamp custom instances to a vanilla base kind for behavior, and inject
// box/sky + event-source spawn weights. Re-run every round because the engine's
// item tables live in per-scene memory (loaded archives, and thus the descriptor
// model/effect pointers, are reloaded each round too).

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "item.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "fst/fst.h"

#include "custom_items.h"
#include "custom_items_api.h"

// Custom kinds occupy indices [ITKIND_NUM, ITKIND_NUM + CUSTOM_ITEM_MAX).
#define CUSTOM_KIND_CEILING (ITKIND_NUM + CUSTOM_ITEM_MAX)

// Persistent extended itData array: the ITKIND_NUM vanilla entries (re-snapshotted each
// round) plus up to CUSTOM_ITEM_MAX appended custom entries. Static so it
// outlives the per-scene heap; the engine is repointed here each round.
static itData stc_ext_itdata[CUSTOM_KIND_CEILING];

// Per-custom-item synthesized model descriptors, refilled each round. itData.model
// must point at a full-width, zero-filled descriptor (not a bare j/flag pair):
// CityItem_Create's part setup reads part-counts at +0x8/+0xc/+0x10 and asserts
// each <= 11, so the trailing fields must stay zero. Only j and flag are written.
static struct ItemModelDesc { void *j; int flag; int rest[14]; } stc_model_pair[CUSTOM_ITEM_MAX];
static ItemCommonAttr stc_custom_attr[CUSTOM_ITEM_MAX];

// kind -> base_kind map for the clamp hook, indexed by (kind - ITKIND_NUM).
static int stc_base_kind[CUSTOM_ITEM_MAX];
static int stc_active_count;

// Box/sky weights remembered per active custom item, so the per-event re-bias
// hook can re-append them without reloading the archives.
static u16 stc_box_weight[CUSTOM_ITEM_MAX][BOXKIND_NUM];

// One event_source_drop[] row: the it_kind followed by the six per-source
// u16 chance columns (dyna, tac, meteor, destructible, chamber, ufo). Mirrors
// the anonymous struct in grBoxGeneInfo->item_desc (stride 0x10).
typedef struct EventSourceDropRow
{
    int it_kind;
    u16 chance[CUSTOM_ITEM_EVSRC_NUM];
} EventSourceDropRow;

// Persistent extended event_source_drop table: the stage's rows (re-snapshotted
// each round) plus appended custom rows. Repointed into item_desc and read
// directly by the picker.
static EventSourceDropRow stc_ext_event_drop[CUSTOM_KIND_CEILING];
static int stc_event_drop_active; // 1 if we repointed event_source_drop this round
static int stc_event_drop_num;    // row count we repointed it to

const CustomItemDesc *CustomItems_LoadDescriptor(int file_entrynum, HSD_Archive **out_archive)
{
    if (out_archive != NULL)
        *out_archive = NULL;

    char *path = FST_GetFilePathFromEntrynum(file_entrynum);
    if (path == NULL)
        return NULL;

    HSD_Archive *arc = Archive_LoadFile(path);
    if (arc == NULL)
    {
        OSReport("[CustomItems] Archive_LoadFile(%s) failed\n", path);
        return NULL;
    }

    const CustomItemDesc *desc =
        (const CustomItemDesc *)Archive_GetPublicAddress(arc, CUSTOM_ITEM_SYMBOL);
    if (desc == NULL)
    {
        OSReport("[CustomItems] %s missing '%s' symbol\n", path, CUSTOM_ITEM_SYMBOL);
        return NULL;
    }
    if (desc->magic != CUSTOM_ITEM_MAGIC)
    {
        OSReport("[CustomItems] %s bad magic 0x%08x\n", path, desc->magic);
        return NULL;
    }
    if (desc->version > CUSTOM_ITEM_DESC_VERSION)
    {
        OSReport("[CustomItems] %s descriptor v%d newer than supported v%d\n",
                 path, desc->version, CUSTOM_ITEM_DESC_VERSION);
        return NULL;
    }

    if (out_archive != NULL)
        *out_archive = arc;
    return desc;
}

// Base kind a custom kind impersonates for state/category lookups. Used by the
// clamp hook. Falls back to a benign pickup kind for unmapped values.
static int ResolveBaseKind(int kind)
{
    int idx = kind - ITKIND_NUM;
    if (idx >= 0 && idx < stc_active_count)
        return stc_base_kind[idx];
    return ITKIND_ACCEL;
}

// Hook body: CityItem_InitData has just written the instance's raw kind to
// ItemData+0x1c. For a custom kind, rewrite it to the base kind so the
// state-class ptr table (0x804b6088) and threshold table (0x804b5f18), both
// indexed by this field, stay in bounds.
static void ClampInstanceKind(void *item_data)
{
    int *kindp = (int *)((u8 *)item_data + 0x1c);
    if (*kindp >= ITKIND_NUM)
        *kindp = ResolveBaseKind(*kindp);
}

// Append (kind, weight) to one box pool's parallel arrays in place (or update the
// weight if the kind is already present). The 68-wide pools are sparsely filled.
static void PoolAppend(grBoxGeneObj *g, int box, int kind, int weight)
{
    if (weight <= 0)
        return;

    u8 *it_kind = g->item_group_spawn[box].it_kind;
    u8 *chance = g->item_group_spawn[box].chance;
    u8 *num = &g->item_group_spawn[box].num;
    u8 w = (u8)(weight > 255 ? 255 : weight);

    for (int i = 0; i < *num; i++)
    {
        if (it_kind[i] == (u8)kind)
        {
            chance[i] = w;
            return;
        }
    }
    if (*num >= ITKIND_NUM - 1)
        return; // pool full
    it_kind[*num] = (u8)kind;
    chance[*num] = w;
    (*num)++;
}

int CustomItemRegistry_RegisterAll(void)
{
    int count = CustomItems_GetCount();
    for (int i = 0; i < count; i++)
    {
        CustomItemEntry *e = CustomItems_GetEntry(i);
        if (e != NULL)
            e->assigned_kind = -1;
    }
    stc_active_count = 0;
    stc_event_drop_active = 0;

    if (!custom_items_enabled)
        return 0;

    itCommonDataAll *all = *stc_it_common_data;
    if (all == NULL || all->itData == NULL)
        return 0; // item data not loaded - not a City Trial round with items

    // Snapshot the (per-scene) vanilla entries into the persistent array.
    itData *vanilla = all->itData;
    for (int k = 0; k < ITKIND_NUM; k++)
        stc_ext_itdata[k] = vanilla[k];

    // Snapshot the stage's event_source_drop[] rows so custom rows can be
    // appended without disturbing the (index-referenced) vanilla rows.
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    EventSourceDropRow *ev_src = NULL;
    int ev_base = 0, ev_num = 0;
    if (info != NULL && info->item_desc != NULL && info->item_desc->event_source_drop != NULL)
    {
        ev_src = (EventSourceDropRow *)info->item_desc->event_source_drop;
        ev_base = info->item_desc->event_source_drop_num;
        if (ev_base < 0)
            ev_base = 0;
        if (ev_base > CUSTOM_KIND_CEILING)
            ev_base = CUSTOM_KIND_CEILING;
        for (int r = 0; r < ev_base; r++)
            stc_ext_event_drop[r] = ev_src[r];
        ev_num = ev_base;
    }

    grBoxGeneObj *g = *stc_grBoxGeneObj;
    int n = 0;

    for (int i = 0; i < count && n < CUSTOM_ITEM_MAX; i++)
    {
        CustomItemEntry *e = CustomItems_GetEntry(i);
        if (e == NULL || !e->enabled)
            continue;

        const CustomItemDesc *desc = CustomItems_LoadDescriptor(e->file_entrynum, NULL);
        if (desc == NULL)
            continue;

        // Adopt the descriptor's display name (supersedes the discovery filename).
        if (desc->name != NULL)
            CustomItems_CopyName(e->name, desc->name);

        int base = desc->base_kind;
        if (base < 0 || base >= ITKIND_NUM)
            base = ITKIND_ACCEL;

        int kind = ITKIND_NUM + n;

        // Clone the base kind's itData, then override model and effect.
        stc_ext_itdata[kind] = stc_ext_itdata[base];

        if (desc->model != NULL)
        {
            // The render flag travels with the model (v2+). v1 descriptors carry
            // no flag, so assume the flat-panel value the carver originally used.
            // rest[] (the part-counts and trailing fields) stays zero - matching
            // every vanilla model descriptor.
            u32 flag = (desc->version >= 2) ? desc->model_flag : 0x02000000u;
            stc_model_pair[n].j = desc->model;
            stc_model_pair[n].flag = (int)flag;
            stc_ext_itdata[kind].model = (void *)&stc_model_pair[n];
        }
        if (desc->effect_info != NULL && stc_ext_itdata[base].attr != NULL)
        {
            stc_custom_attr[n] = *stc_ext_itdata[base].attr;
            stc_custom_attr[n].effect_info = (PatchEffectInfo *)desc->effect_info;
            stc_ext_itdata[kind].attr = &stc_custom_attr[n];
        }

        stc_base_kind[n] = base;
        e->assigned_kind = kind;

        // Box/sky weights: apply now and remember for the per-event re-inject.
        for (int b = 0; b < BOXKIND_NUM; b++)
        {
            stc_box_weight[n][b] = desc->weight_box[b];
            if (g != NULL)
                PoolAppend(g, b, kind, desc->weight_box[b]);
        }

        // Event-source weights: append one row if any source is nonzero.
        if (ev_src != NULL && ev_num < CUSTOM_KIND_CEILING)
        {
            EventSourceDropRow *row = &stc_ext_event_drop[ev_num];
            int any = 0;
            row->it_kind = kind;
            for (int s = 0; s < CUSTOM_ITEM_EVSRC_NUM; s++)
            {
                row->chance[s] = desc->weight_event[s];
                any |= desc->weight_event[s];
            }
            if (any)
                ev_num++;
        }

        n++;
    }

    stc_active_count = n;
    if (n > 0)
        all->itData = stc_ext_itdata; // repoint the engine at the grown array

    // Repoint event_source_drop[] at the grown table only if we added rows.
    if (ev_src != NULL && ev_num > ev_base)
    {
        info->item_desc->event_source_drop = (void *)stc_ext_event_drop;
        info->item_desc->event_source_drop_num = ev_num;
        stc_event_drop_active = 1;
        stc_event_drop_num = ev_num;
    }

    OSReport("[CustomItems] Registered %d custom kind%s this round (%d event-drop row%s)\n",
             n, n == 1 ? "" : "s", ev_num - ev_base, (ev_num - ev_base) == 1 ? "" : "s");
    return n;
}

// Re-apply custom kinds after the per-event re-bias wipes the box/sky pools
// (PoolAppend is idempotent); re-assert the event_source_drop[] repoint too.
void CustomItemRegistry_ReinjectPools(void)
{
    if (!custom_items_enabled || stc_active_count == 0)
        return;

    grBoxGeneObj *g = *stc_grBoxGeneObj;
    if (g != NULL)
    {
        for (int n = 0; n < stc_active_count; n++)
        {
            int kind = ITKIND_NUM + n;
            for (int b = 0; b < BOXKIND_NUM; b++)
                PoolAppend(g, b, kind, stc_box_weight[n][b]);
        }
    }

    if (stc_event_drop_active)
    {
        grBoxGeneInfo *info = *stc_grBoxGeneInfo;
        if (info != NULL && info->item_desc != NULL)
        {
            info->item_desc->event_source_drop = (void *)stc_ext_event_drop;
            info->item_desc->event_source_drop_num = stc_event_drop_num;
        }
    }
}

// Per-round registration: CityItemSpawn_Init epilogue (0x800ec348), after the
// spawn pools are filled and item data is loaded, before the first spawn tick.
static void RegisterAllHook(void)
{
    CustomItemRegistry_RegisterAll();
}
CODEPATCH_HOOKCREATE(0x800ec348, "", RegisterAllHook, "", 0);

// Kind clamp: CityItem_InitData, just after ItemData+0x1c is written (the
// clobbered `li r4,-1` at 0x8024eb44 is replayed). r31 holds ItemData.
CODEPATCH_HOOKCREATE(0x8024eb44, "mr 3,31\n\t", ClampInstanceKind, "", 0x8024eb48);

// Per-event re-inject: CityEvent_ModifyItemFallDesc epilogue (0x800ed7f0), whose
// re-bias rebuilds the box/sky pools and drops our appended kinds.
static void ReinjectPoolsHook(void)
{
    CustomItemRegistry_ReinjectPools();
}
CODEPATCH_HOOKCREATE(0x800ed7f0, "", ReinjectPoolsHook, "", 0);

// Recover the custom kind of an item instance from its itData pointer (ItemData+0x2c,
// pointed at our grown array for custom items). Survives the +0x1c clamp, which only
// rewrites the behavior-kind field. Returns -1 for vanilla items.
static int CustomKindFromItemData(ItemData *id)
{
    if (id == NULL)
        return -1;
    itData *itd = id->itData;
    if (itd < stc_ext_itdata || itd >= &stc_ext_itdata[CUSTOM_KIND_CEILING])
        return -1;
    int kind = (int)(itd - stc_ext_itdata);
    return (kind >= ITKIND_NUM && kind < CUSTOM_KIND_CEILING) ? kind : -1;
}

// Pickup hook: Machine_OnTouchItem(md, id) fires when a rider collects an item.
// For a custom item, look up its registry entry and invoke the pickup handler with
// the collecting player's slot, so a consumer (e.g. hypernova) can react.
static void OnTouchItem(MachineData *md, ItemData *id)
{
    int kind = CustomKindFromItemData(id);
    if (kind < 0)
        return;

    int count = CustomItems_GetCount();
    for (int i = 0; i < count; i++)
    {
        CustomItemEntry *e = CustomItems_GetEntry(i);
        if (e != NULL && e->assigned_kind == kind)
        {
            CustomItems_FirePickup(e->id_hash, e->name, Machine_GetRiderPly(md));
            return;
        }
    }
}
// Entry of Machine_OnTouchItem (0x801db34c); r3=MachineData, r4=ItemData. hoshi's
// trampoline doesn't preserve registers across the C call, so the prologue/epilogue
// save and restore r3/r4/LR (all three consumed by the vanilla body) around it.
CODEPATCH_HOOKCREATE(0x801db34c,
                     "stwu 1,-0x20(1)\n\t"
                     "stw 3,0x10(1)\n\t"
                     "stw 4,0x14(1)\n\t"
                     "mflr 0\n\t"
                     "stw 0,0x18(1)\n\t",
                     OnTouchItem,
                     "lwz 3,0x10(1)\n\t"
                     "lwz 4,0x14(1)\n\t"
                     "lwz 0,0x18(1)\n\t"
                     "mtlr 0\n\t"
                     "addi 1,1,0x20\n\t",
                     0);

void CustomItemRegistry_InstallHook(void)
{
    // Lift the kind ceiling in CityItem_Create: cmpwi r4,69 -> cmpwi r4,CEILING.
    CODEPATCH_REPLACEINSTRUCTION(0x8024efb4, 0x2c040000 | CUSTOM_KIND_CEILING);
    CODEPATCH_HOOKAPPLY(0x8024eb44);
    CODEPATCH_HOOKAPPLY(0x800ec348);
    CODEPATCH_HOOKAPPLY(0x800ed7f0);
    CODEPATCH_HOOKAPPLY(0x801db34c);
    OSReport("[CustomItems] Engine splice installed (kind ceiling %d)\n", CUSTOM_KIND_CEILING);
}
