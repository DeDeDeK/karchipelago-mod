#include "os.h"
#include "game.h"
#include "hsd.h"
#include "item.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "fst/fst.h"

#include "custom_items.h"

// Custom kinds occupy indices [ITKIND_NUM, ITKIND_NUM + CUSTOM_ITEM_MAX).
#define CUSTOM_KIND_CEILING (ITKIND_NUM + CUSTOM_ITEM_MAX)

// Vanilla entries (re-snapshotted each round) plus the appended custom ones.
// Static so it outlives the per-scene heap; the engine is repointed here.
static itData stc_ext_itdata[CUSTOM_KIND_CEILING];

// itData.model must point at a descriptor whose parts[] counts are zero:
// Item_InitPartsModel (0x80252824) asserts each is <= 11.
static ItemModelDesc stc_model_pair[CUSTOM_ITEM_MAX];
static ItemCommonAttr stc_custom_attr[CUSTOM_ITEM_MAX];

// Anim slots cloned from the base kind when a descriptor overrides an animation.
// Only ITKIND_ALLUP has two; every other kind's array holds one entry.
#define CUSTOM_ITEM_ANIM_SLOTS 2
static ItemAnimEntry stc_custom_anim[CUSTOM_ITEM_MAX][CUSTOM_ITEM_ANIM_SLOTS];

// kind -> base_kind map for the clamp hook, indexed by (kind - ITKIND_NUM).
static int stc_base_kind[CUSTOM_ITEM_MAX];
static int stc_active_count;

// Remembered so the per-event re-bias hook can re-append without reloading archives.
static u16 stc_box_weight[CUSTOM_ITEM_MAX][BOXKIND_NUM];

// The stage's event_source_drop rows (re-snapshotted each round) plus appended
// custom rows; repointed into item_desc and read directly by the picker.
static ItemEventSourceDrop stc_ext_event_drop[CUSTOM_KIND_CEILING];
static int stc_event_drop_active; // 1 if we repointed event_source_drop this round
static int stc_event_drop_num;    // row count we repointed it to

const CustomItemDesc *CustomItems_LoadDescriptor(int file_entrynum, int report)
{
    char *path = FST_GetFilePathFromEntrynum(file_entrynum);
    if (path == NULL)
        return NULL;

    HSD_Archive *arc = Archive_LoadFile(path);
    if (arc == NULL)
    {
        if (report)
            OSReport("[CustomItems] Archive_LoadFile(%s) failed\n", path);
        return NULL;
    }

    const CustomItemDesc *desc =
        (const CustomItemDesc *)Archive_GetPublicAddress(arc, CUSTOM_ITEM_SYMBOL);
    if (desc == NULL)
    {
        if (report)
            OSReport("[CustomItems] %s missing '%s' symbol\n", path, CUSTOM_ITEM_SYMBOL);
        return NULL;
    }
    if (desc->magic != CUSTOM_ITEM_MAGIC)
    {
        if (report)
            OSReport("[CustomItems] %s bad magic 0x%08x\n", path, desc->magic);
        return NULL;
    }
    if (desc->version != CUSTOM_ITEM_DESC_VERSION)
    {
        if (report)
            OSReport("[CustomItems] %s descriptor v%d, expected v%d\n",
                     path, desc->version, CUSTOM_ITEM_DESC_VERSION);
        return NULL;
    }

    return desc;
}

// Base kind a custom kind impersonates for state/category lookups; falls back
// to a benign pickup kind for unmapped values.
static int ResolveBaseKind(int kind)
{
    int idx = kind - ITKIND_NUM;
    if (idx >= 0 && idx < stc_active_count)
        return stc_base_kind[idx];
    return ITKIND_ACCEL;
}

// Rewrite the instance kind to the base kind. The 69-entry state-handler table at
// 0x804b6088 is indexed by this field, so a custom kind would read past it.
static void CustomItemRegistry_ClampInstanceKind(ItemData *item_data)
{
    if (item_data->kind >= ITKIND_NUM)
        item_data->kind = ResolveBaseKind(item_data->kind);
}

// Append (kind, weight) to one box pool, or update the weight if already there.
// The 68-wide pools are sparsely filled.
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

void CustomItemRegistry_ResetScene(void)
{
    stc_active_count = 0;
    stc_event_drop_active = 0;
    stc_event_drop_num = 0;
}

void CustomItemRegistry_RegisterAll(void)
{
    int count = CustomItems_GetCount();
    for (int i = 0; i < count; i++)
    {
        CustomItemEntry *e = CustomItems_GetEntry(i);
        if (e != NULL)
            e->assigned_kind = -1;
    }
    CustomItemRegistry_ResetScene();

    itCommonDataAll *all = *stc_it_common_data;
    if (all == NULL || all->itData == NULL)
        return; // item data not loaded - not a City Trial round with items

    itData *vanilla = all->itData;
    for (int k = 0; k < ITKIND_NUM; k++)
        stc_ext_itdata[k] = vanilla[k];

    // Snapshot the stage's rows so custom ones can be appended without moving
    // the vanilla rows, which are referenced by index.
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    ItemEventSourceDrop *ev_src = NULL;
    int ev_base = 0, ev_num = 0;
    if (info != NULL && info->item_desc != NULL && info->item_desc->event_source_drop != NULL)
    {
        ev_src = info->item_desc->event_source_drop;
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
        if (e == NULL || !e->api_enabled)
            continue;

        const CustomItemDesc *desc = CustomItems_LoadDescriptor(e->file_entrynum, !e->load_reported);
        if (desc == NULL)
        {
            e->load_reported = 1;
            continue;
        }

        if (desc->name != NULL)
            CustomItems_CopyName(e->name, desc->name);

        int base = desc->base_kind;
        if (base < 0 || base >= ITKIND_NUM)
            base = ITKIND_ACCEL;

        int kind = ITKIND_NUM + n;

        stc_ext_itdata[kind] = stc_ext_itdata[base];

        if (desc->model != NULL)
        {
            stc_model_pair[n].j = (JOBJ *)desc->model;
            stc_model_pair[n].flag = desc->model_flag;
            stc_ext_itdata[kind].model = &stc_model_pair[n];
        }

        void *joint_anim = desc->joint_anim;
        void *mat_anim = desc->mat_anim;
        int drop_mat_anim = (desc->flags & CUSTOM_ITEM_FLAG_NO_MAT_ANIM) != 0;
        if ((drop_mat_anim || joint_anim != NULL || mat_anim != NULL) &&
            stc_ext_itdata[base].anim_data != NULL)
        {
            int slots = (base == ITKIND_ALLUP) ? CUSTOM_ITEM_ANIM_SLOTS : 1;
            for (int a = 0; a < slots; a++)
            {
                stc_custom_anim[n][a] = stc_ext_itdata[base].anim_data[a];
                if (drop_mat_anim)
                    stc_custom_anim[n][a].mat_anim = NULL;
                else if (mat_anim != NULL)
                    stc_custom_anim[n][a].mat_anim = (MatAnimJointDesc *)mat_anim;
                if (joint_anim != NULL)
                    stc_custom_anim[n][a].joint_anim = (AnimJointDesc *)joint_anim;
            }
            stc_ext_itdata[kind].anim_data = stc_custom_anim[n];
        }

        // A 0 or 1.0 scale means inherit the base's native size.
        int want_effect = (desc->effect_info != NULL);
        int want_scale = (desc->scale > 0.0f && desc->scale != 1.0f);
        if ((want_effect || want_scale) && stc_ext_itdata[base].attr != NULL)
        {
            stc_custom_attr[n] = *stc_ext_itdata[base].attr;
            if (want_effect)
                stc_custom_attr[n].effect_info = (PatchEffectInfo *)desc->effect_info;
            if (want_scale)
                stc_custom_attr[n].scale_factor *= desc->scale;
            stc_ext_itdata[kind].attr = &stc_custom_attr[n];
        }

        stc_base_kind[n] = base;
        e->assigned_kind = kind;

        // The engine's pool chance is a u8, so PoolAppend saturates at 255.
        int clamped = 0;
        for (int b = 0; b < BOXKIND_NUM; b++)
        {
            if (desc->weight_box[b] > 255)
                clamped++;
            stc_box_weight[n][b] = desc->weight_box[b];
            if (g != NULL)
                PoolAppend(g, b, kind, desc->weight_box[b]);
        }
        if (clamped && !e->load_reported)
        {
            e->load_reported = 1;
            OSReport("[CustomItems] %s: %d box weight(s) over 255, clamped (weights are relative)\n",
                     e->name, clamped);
        }

        if (ev_src != NULL && ev_num < CUSTOM_KIND_CEILING)
        {
            ItemEventSourceDrop *row = &stc_ext_event_drop[ev_num];
            row->it_kind = kind;
            row->chance_dyna = desc->weight_event[CUSTOM_ITEM_EVSRC_DYNABLADE];
            row->chance_tac = desc->weight_event[CUSTOM_ITEM_EVSRC_TAC];
            row->chance_meteor = desc->weight_event[CUSTOM_ITEM_EVSRC_METEOR];
            row->chance_destructible = desc->weight_event[CUSTOM_ITEM_EVSRC_DESTRUCTIBLE];
            row->chance_chamber = desc->weight_event[CUSTOM_ITEM_EVSRC_CHAMBER];
            row->chance_ufo = desc->weight_event[CUSTOM_ITEM_EVSRC_UFO];

            int any = 0;
            for (int s = 0; s < CUSTOM_ITEM_EVSRC_NUM; s++)
                any |= desc->weight_event[s];
            if (any)
                ev_num++;
        }

        n++;
    }

    stc_active_count = n;
    if (n > 0)
        all->itData = stc_ext_itdata;

    if (ev_src != NULL && ev_num > ev_base)
    {
        info->item_desc->event_source_drop = stc_ext_event_drop;
        info->item_desc->event_source_drop_num = ev_num;
        stc_event_drop_active = 1;
        stc_event_drop_num = ev_num;
    }

    if (n > 0)
        OSReport("[CustomItems] Registered %d custom kind%s this round (%d event-drop row%s)\n",
                 n, n == 1 ? "" : "s", ev_num - ev_base, (ev_num - ev_base) == 1 ? "" : "s");
}

// The per-event re-bias wipes the box/sky pools and the event_source_drop
// repoint, so both are re-applied here (PoolAppend is idempotent).
static void CustomItemRegistry_ReinjectPools(void)
{
    if (stc_active_count == 0)
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
            info->item_desc->event_source_drop = stc_ext_event_drop;
            info->item_desc->event_source_drop_num = stc_event_drop_num;
        }
    }
}

// CityItemSpawn_Init epilogue (0x800ec348): the spawn pools are filled and item
// data is loaded, and the first spawn tick has not run.
CODEPATCH_HOOKCREATE(0x800ec348, "", CustomItemRegistry_RegisterAll, "", 0);

// CityItem_InitData (0x8024eaf4) just after ItemData+0x1c is written; r31 holds
// ItemData. r0 (loop count) and r6 (threshold table pointer) are loaded before
// 0x8024eb44 and read after it, so both are carried across the call.
CODEPATCH_HOOKCREATE(0x8024eb44,
                     "stwu 1,-0x20(1)\n\t"
                     "stw 0,0x10(1)\n\t"
                     "stw 6,0x14(1)\n\t"
                     "mr 3,31\n\t",
                     CustomItemRegistry_ClampInstanceKind,
                     "lwz 0,0x10(1)\n\t"
                     "lwz 6,0x14(1)\n\t"
                     "addi 1,1,0x20\n\t",
                     0);

// Shared exit of CityEvent_ModifyItemFallDesc (0x800ed784), reached both after a
// re-bias and by its early-out, so the re-append has to tolerate no re-bias.
CODEPATCH_HOOKCREATE(0x800ed7f0, "", CustomItemRegistry_ReinjectPools, "", 0);

// Recovers the custom kind from the instance's itData pointer, which the +0x1c
// clamp leaves alone. Returns -1 for vanilla items.
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

static void CustomItemRegistry_OnTouchItem(MachineData *md, ItemData *id)
{
    int kind = CustomKindFromItemData(id);
    if (kind < 0)
        return;

    // Machine_GetRiderPly returns 5 for a riderless machine; nobody collected it.
    int player = Machine_GetRiderPly(md);
    if (player < 0 || player > 4)
        return;

    int count = CustomItems_GetCount();
    for (int i = 0; i < count; i++)
    {
        CustomItemEntry *e = CustomItems_GetEntry(i);
        if (e != NULL && e->assigned_kind == kind)
        {
            CustomItems_FirePickup(e->id_hash, e->name, player);
            return;
        }
    }
}
// Entry of Machine_OnTouchItem (0x801db34c); r3=MachineData, r4=ItemData. The
// trampoline doesn't preserve registers across the C call, so r3/r4/LR - all
// consumed by the vanilla body - are saved and restored around it.
CODEPATCH_HOOKCREATE(0x801db34c,
                     "stwu 1,-0x20(1)\n\t"
                     "stw 3,0x10(1)\n\t"
                     "stw 4,0x14(1)\n\t"
                     "mflr 0\n\t"
                     "stw 0,0x18(1)\n\t",
                     CustomItemRegistry_OnTouchItem,
                     "lwz 3,0x10(1)\n\t"
                     "lwz 4,0x14(1)\n\t"
                     "lwz 0,0x18(1)\n\t"
                     "mtlr 0\n\t"
                     "addi 1,1,0x20\n\t",
                     0);

void CustomItemRegistry_InstallHooks(void)
{
    // Lift the kind ceiling in CityItem_Create: cmpwi r4,69 -> cmpwi r4,CEILING.
    CODEPATCH_REPLACEINSTRUCTION(0x8024efb4, 0x2c040000 | CUSTOM_KIND_CEILING);
    CODEPATCH_HOOKAPPLY(0x8024eb44);
    CODEPATCH_HOOKAPPLY(0x800ec348);
    CODEPATCH_HOOKAPPLY(0x800ed7f0);
    CODEPATCH_HOOKAPPLY(0x801db34c);
}
