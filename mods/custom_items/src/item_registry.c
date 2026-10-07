#include <string.h>

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "item.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "fst/fst.h"

#include "custom_items.h"

#define CUSTOM_KIND_CEILING (ITKIND_NUM + CUSTOM_ITEM_MAX)
_Static_assert(CUSTOM_KIND_CEILING <= 0xff, "box pools store the kind as a u8");

// Vanilla entries (re-snapshotted each round) plus the appended custom ones.
// Static so it outlives the per-scene heap; the engine is repointed here.
static itData stc_ext_itdata[CUSTOM_KIND_CEILING];

// itData.model must point at a descriptor whose parts[] counts are zero:
// Item_InitPartsModel (0x80252824) asserts each is <= 11.
static ItemModelDesc stc_model_pair[CUSTOM_ITEM_MAX];
static ItemCommonAttr stc_custom_attr[CUSTOM_ITEM_MAX];

// Only ITKIND_ALLUP has two anim slots; every other kind's array holds one.
#define CUSTOM_ITEM_ANIM_SLOTS 2
static ItemAnimEntry stc_custom_anim[CUSTOM_ITEM_MAX][CUSTOM_ITEM_ANIM_SLOTS];

// Indexed by kind - ITKIND_NUM.
static int stc_base_kind[CUSTOM_ITEM_MAX];
static u8 stc_box_weight[CUSTOM_ITEM_MAX][BOXKIND_NUM];
static int stc_active_count;

// The stage's event_source_drop rows, at most one per vanilla kind, plus one row
// per custom kind; repointed into item_desc and read directly by the picker.
static ItemEventSourceDrop stc_ext_event_drop[CUSTOM_KIND_CEILING];

static u16 ClampWeight(u16 weight, u16 max)
{
    return weight > max ? max : weight;
}

// stc_item_state_tbl has ITKIND_NUM entries and is indexed by ItemData.kind, so a
// custom kind takes its base kind's there; rendering still reads its own itData.
static void CustomItemRegistry_ClampInstanceKind(ItemData *item_data)
{
    if (item_data->kind >= ITKIND_NUM)
        item_data->kind = stc_base_kind[item_data->kind - ITKIND_NUM];
}

// Every caller runs right after the pools are rebuilt, so the kind is never
// already present. A full pool drops it.
static void PoolAppend(grBoxGeneObj *g, int box, int kind, u8 weight)
{
    if (weight == 0 || g->item_group_spawn[box].num >= sizeof(g->item_group_spawn[box].it_kind))
        return;
    u8 i = g->item_group_spawn[box].num++;
    g->item_group_spawn[box].it_kind[i] = kind;
    g->item_group_spawn[box].chance[i] = weight;
}

static void CustomItemRegistry_AppendPools(void)
{
    grBoxGeneObj *g = *stc_grBoxGeneObj;
    for (int n = 0; n < stc_active_count; n++)
    {
        for (int b = 0; b < BOXKIND_NUM; b++)
            PoolAppend(g, b, ITKIND_NUM + n, stc_box_weight[n][b]);
    }
}

void CustomItemRegistry_ResetScene(void)
{
    stc_active_count = 0;
}

static void CustomItemRegistry_RegisterAll(void)
{
    itCommonDataAll *all = *stc_it_common_data;
    memcpy(stc_ext_itdata, all->itData, ITKIND_NUM * sizeof(itData));

    // Only City Trial's stage has an item_desc; the stadiums' pools come from pool_a/b.
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    int ev_base = 0;
    if (info->item_desc != NULL)
    {
        ev_base = info->item_desc->event_source_drop_num;
        memcpy(stc_ext_event_drop, info->item_desc->event_source_drop,
               ev_base * sizeof(ItemEventSourceDrop));
    }
    int ev_num = ev_base;

    int n = 0;
    for (int i = 0; i < CustomItems_GetCount(); i++)
    {
        CustomItemEntry *e = CustomItems_GetEntry(i);
        if (!e->api_enabled)
            continue;

        // Validated at discovery. Outside OnBoot the archive is per-scene, so it is
        // reloaded every round.
        const CustomItemDesc *desc = Archive_GetPublicAddress(
            Archive_LoadFile(FST_GetFilePathFromEntrynum(e->file_entrynum)), CUSTOM_ITEM_SYMBOL);

        int base = desc->base_kind;
        int kind = ITKIND_NUM + n;
        itData *itd = &stc_ext_itdata[kind];
        *itd = stc_ext_itdata[base];

        if (desc->model != NULL)
        {
            stc_model_pair[n].j = desc->model;
            stc_model_pair[n].flag = desc->model_flag;
            itd->model = &stc_model_pair[n];
        }

        int drop_mat_anim = (desc->flags & CUSTOM_ITEM_FLAG_NO_MAT_ANIM) != 0;
        if (drop_mat_anim || desc->joint_anim != NULL || desc->mat_anim != NULL)
        {
            int slots = (base == ITKIND_ALLUP) ? CUSTOM_ITEM_ANIM_SLOTS : 1;
            for (int a = 0; a < slots; a++)
            {
                ItemAnimEntry *anim = &stc_custom_anim[n][a];
                *anim = itd->anim_data[a];
                if (drop_mat_anim)
                    anim->mat_anim = NULL;
                else if (desc->mat_anim != NULL)
                    anim->mat_anim = desc->mat_anim;
                if (desc->joint_anim != NULL)
                    anim->joint_anim = desc->joint_anim;
            }
            itd->anim_data = stc_custom_anim[n];
        }

        int want_scale = (desc->scale > 0.0f && desc->scale != 1.0f);
        if (desc->effect_info != NULL || want_scale)
        {
            stc_custom_attr[n] = *itd->attr;
            if (desc->effect_info != NULL)
                stc_custom_attr[n].effect_info = desc->effect_info;
            if (want_scale)
                stc_custom_attr[n].scale_factor *= desc->scale;
            itd->attr = &stc_custom_attr[n];
        }

        for (int b = 0; b < BOXKIND_NUM; b++)
            stc_box_weight[n][b] = ClampWeight(desc->weight_box[b], CUSTOM_ITEM_BOX_WEIGHT_MAX);

        if (info->item_desc != NULL)
        {
            const u16 *w = desc->weight_event;
            ItemEventSourceDrop *row = &stc_ext_event_drop[ev_num];
            row->it_kind = kind;
            row->chance_dyna = ClampWeight(w[CUSTOM_ITEM_EVSRC_DYNABLADE], CUSTOM_ITEM_EVENT_WEIGHT_MAX);
            row->chance_tac = ClampWeight(w[CUSTOM_ITEM_EVSRC_TAC], CUSTOM_ITEM_EVENT_WEIGHT_MAX);
            row->chance_meteor = ClampWeight(w[CUSTOM_ITEM_EVSRC_METEOR], CUSTOM_ITEM_EVENT_WEIGHT_MAX);
            row->chance_destructible = ClampWeight(w[CUSTOM_ITEM_EVSRC_DESTRUCTIBLE], CUSTOM_ITEM_EVENT_WEIGHT_MAX);
            row->chance_chamber = ClampWeight(w[CUSTOM_ITEM_EVSRC_CHAMBER], CUSTOM_ITEM_EVENT_WEIGHT_MAX);
            row->chance_ufo = ClampWeight(w[CUSTOM_ITEM_EVSRC_UFO], CUSTOM_ITEM_EVENT_WEIGHT_MAX);

            int any = 0;
            for (int s = 0; s < CUSTOM_ITEM_EVSRC_NUM; s++)
                any |= w[s];
            if (any)
                ev_num++;
        }

        stc_base_kind[n] = base;
        e->assigned_kind = kind;
        n++;
    }

    stc_active_count = n;
    if (n == 0)
        return;

    all->itData = stc_ext_itdata;
    CustomItemRegistry_AppendPools();

    if (ev_num > ev_base)
    {
        info->item_desc->event_source_drop = stc_ext_event_drop;
        info->item_desc->event_source_drop_num = ev_num;
    }

    OSReport("[CustomItems] Registered %d custom kind%s this round (%d event-drop row%s)\n",
             n, n == 1 ? "" : "s", ev_num - ev_base, (ev_num - ev_base) == 1 ? "" : "s");
}

// Replaces the call in CityEvent_RestoreItemFallDesc (0x800ed800), which rebuilds
// the box pools from the stage table when an event ends.
static void CustomItemRegistry_RestoreItemFallChances(int stadium_group)
{
    CityItemSpawn_InitItemFallChances(stadium_group);
    CustomItemRegistry_AppendPools();
}

// A custom item's ItemData.kind holds its base kind, but the clamp leaves itData
// pointing at the custom kind's own entry.
int CustomItemRegistry_GetItemKind(ItemData *item)
{
    itData *itd = item->itData;
    if (itd >= &stc_ext_itdata[ITKIND_NUM] && itd < &stc_ext_itdata[CUSTOM_KIND_CEILING])
        return itd - stc_ext_itdata;
    return item->kind;
}

// Machine_OnTouchItem (0x801db34c) reaches its one call of ItemGObj_BeginPatchToss
// only when the touch collects the item. An ability or power-up the rider cannot
// take returns before it, and the item stays to be touched again next frame.
static void CustomItemRegistry_BeginPatchToss(GOBJ *item_gobj, GOBJ *toucher, int is_machine)
{
    int kind = CustomItemRegistry_GetItemKind(item_gobj->userdata);
    // 5 for a riderless machine, which only a direct grant can touch with.
    int player = Machine_GetRiderPly(toucher->userdata);
    if (kind >= ITKIND_NUM && player < PLY_NUM)
    {
        for (int i = 0; i < CustomItems_GetCount(); i++)
        {
            CustomItemEntry *e = CustomItems_GetEntry(i);
            if (e->assigned_kind == kind)
            {
                CustomItems_FirePickup(e->id_hash, player);
                break;
            }
        }
    }

    ItemGObj_BeginPatchToss(item_gobj, toucher, is_machine);
}

// CityItemSpawn_Init (0x800ebf70) epilogue, once per scene with item boxes: the
// pools are filled, item data is loaded, and no spawn tick has run.
CODEPATCH_HOOKCREATE(0x800ec348, "", CustomItemRegistry_RegisterAll, "", 0);

// CityItem_InitData (0x8024eaf4) just after it writes ItemData.kind; r31 holds
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

void CustomItemRegistry_InstallHooks(void)
{
    // CityItem_Create (0x8024eef4) asserts the kind is below this bound:
    // cmpwi r4,69 -> cmpwi r4,CUSTOM_KIND_CEILING.
    CODEPATCH_REPLACEINSTRUCTION(0x8024efb4, 0x2c040000 | CUSTOM_KIND_CEILING);
    CODEPATCH_HOOKAPPLY(0x8024eb44);
    CODEPATCH_HOOKAPPLY(0x800ec348);
    CODEPATCH_REPLACECALL(0x800ed878, CustomItemRegistry_RestoreItemFallChances);
    CODEPATCH_REPLACECALL(0x801dba48, CustomItemRegistry_BeginPatchToss);
}
