#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_items.h"
#include "textbox_api.h"
#include "inline.h"

// 1-to-1 inverse of ItemKindToUnlockBit. Lets us reuse hoshi's ItemKind_Names
// for display rather than maintaining a parallel name table here.
static const ItemKind itunlock_to_itkind[ITUNLOCK_NUM] = {
    [ITUNLOCK_ALLUP]            = ITKIND_ALLUP,
    [ITUNLOCK_SPEEDMAX]         = ITKIND_SPEEDMAX,
    [ITUNLOCK_SPEEDMIN]         = ITKIND_SPEEDMIN,
    [ITUNLOCK_OFFENSEMAX]       = ITKIND_OFFENSEMAX,
    [ITUNLOCK_DEFENSEMAX]       = ITKIND_DEFENSEMAX,
    [ITUNLOCK_CHARGEMAX]        = ITKIND_CHARGEMAX,
    [ITUNLOCK_CHARGENONE]       = ITKIND_CHARGENONE,
    [ITUNLOCK_CANDY]            = ITKIND_CANDY,
    [ITUNLOCK_FOODMAXIMTOMATO]  = ITKIND_FOODMAXIMTOMATO,
    [ITUNLOCK_FOODENERGYDRINK]  = ITKIND_FOODENERGYDRINK,
    [ITUNLOCK_FOODICECREAM]     = ITKIND_FOODICECREAM,
    [ITUNLOCK_FOODRICEBALL]     = ITKIND_FOODRICEBALL,
    [ITUNLOCK_FOODCHICKEN]      = ITKIND_FOODCHICKEN,
    [ITUNLOCK_FOODCURRY]        = ITKIND_FOODCURRY,
    [ITUNLOCK_FOODRAMEN]        = ITKIND_FOODRAMEN,
    [ITUNLOCK_FOODOMELET]       = ITKIND_FOODOMELET,
    [ITUNLOCK_FOODHAMBURGER]    = ITKIND_FOODHAMBURGER,
    [ITUNLOCK_FOODSUSHI]        = ITKIND_FOODSUSHI,
    [ITUNLOCK_FOODHOTDOG]       = ITKIND_FOODHOTDOG,
    [ITUNLOCK_FOODAPPLE]        = ITKIND_FOODAPPLE,
    [ITUNLOCK_FIREWORKS]        = ITKIND_FIREWORKS,
    [ITUNLOCK_PANICSPIN]        = ITKIND_PANICSPIN,
    [ITUNLOCK_SENSORBOMB]       = ITKIND_SENSORBOMB,
    [ITUNLOCK_GORDO]            = ITKIND_GORDO,
    [ITUNLOCK_HYDRA1]           = ITKIND_HYDRA1,
    [ITUNLOCK_HYDRA2]           = ITKIND_HYDRA2,
    [ITUNLOCK_HYDRA3]           = ITKIND_HYDRA3,
    [ITUNLOCK_DRAGOON1]         = ITKIND_DRAGOON1,
    [ITUNLOCK_DRAGOON2]         = ITKIND_DRAGOON2,
    [ITUNLOCK_DRAGOON3]         = ITKIND_DRAGOON3,
};

static const char *ItemUnlockName(ItemUnlockKind kind)
{
    return ItemKind_Names[itunlock_to_itkind[kind]];
}

// Map non-patch, non-copy ItemKinds to their individual unlock bit.
// Returns -1 for items not gated by this system.
static int ItemKindToUnlockBit(u8 it_kind)
{
    switch (it_kind)
    {
        case ITKIND_ALLUP:            return ITUNLOCK_ALLUP;
        case ITKIND_SPEEDMAX:         return ITUNLOCK_SPEEDMAX;
        case ITKIND_SPEEDMIN:         return ITUNLOCK_SPEEDMIN;
        case ITKIND_OFFENSEMAX:       return ITUNLOCK_OFFENSEMAX;
        case ITKIND_DEFENSEMAX:       return ITUNLOCK_DEFENSEMAX;
        case ITKIND_CHARGEMAX:        return ITUNLOCK_CHARGEMAX;
        case ITKIND_CHARGENONE:       return ITUNLOCK_CHARGENONE;
        case ITKIND_CANDY:            return ITUNLOCK_CANDY;
        case ITKIND_FOODMAXIMTOMATO:  return ITUNLOCK_FOODMAXIMTOMATO;
        case ITKIND_FOODENERGYDRINK:  return ITUNLOCK_FOODENERGYDRINK;
        case ITKIND_FOODICECREAM:     return ITUNLOCK_FOODICECREAM;
        case ITKIND_FOODRICEBALL:     return ITUNLOCK_FOODRICEBALL;
        case ITKIND_FOODCHICKEN:      return ITUNLOCK_FOODCHICKEN;
        case ITKIND_FOODCURRY:        return ITUNLOCK_FOODCURRY;
        case ITKIND_FOODRAMEN:        return ITUNLOCK_FOODRAMEN;
        case ITKIND_FOODOMELET:       return ITUNLOCK_FOODOMELET;
        case ITKIND_FOODHAMBURGER:    return ITUNLOCK_FOODHAMBURGER;
        case ITKIND_FOODSUSHI:        return ITUNLOCK_FOODSUSHI;
        case ITKIND_FOODHOTDOG:       return ITUNLOCK_FOODHOTDOG;
        case ITKIND_FOODAPPLE:        return ITUNLOCK_FOODAPPLE;
        case ITKIND_FIREWORKS:        return ITUNLOCK_FIREWORKS;
        case ITKIND_PANICSPIN:        return ITUNLOCK_PANICSPIN;
        case ITKIND_SENSORBOMB:       return ITUNLOCK_SENSORBOMB;
        case ITKIND_GORDO:            return ITUNLOCK_GORDO;
        case ITKIND_HYDRA1:           return ITUNLOCK_HYDRA1;
        case ITKIND_HYDRA2:           return ITUNLOCK_HYDRA2;
        case ITKIND_HYDRA3:           return ITUNLOCK_HYDRA3;
        case ITKIND_DRAGOON1:         return ITUNLOCK_DRAGOON1;
        case ITKIND_DRAGOON2:         return ITUNLOCK_DRAGOON2;
        case ITKIND_DRAGOON3:         return ITUNLOCK_DRAGOON3;
        default:                      return -1;
    }
}

// Per-source weights for the All-Up entry injected into spawn pools when
// All-Up is unlocked. Tuned so All-Up appears alongside existing +1 patches in
// each pool (rather than dominating them) - Max Stats Insanity's weight bias
// runs on top of this and amplifies further. These values were chosen to match
// the mid-range of vanilla +1 patch weights in each pool family.
#define ALLUP_BOX_POOL_CHANCE      8
#define ALLUP_CHANCE_DESTRUCTIBLE  16
#define ALLUP_CHANCE_DYNA          4

// Ensure pool[] contains it_kind. If absent, append with `weight` (if there's
// room). If already present, leave it alone - vanilla weight is preserved.
static void EnsureItemInPool(u8 *kinds, u8 *chances, u8 *num, u8 max_entries,
                             u8 it_kind, u8 weight)
{
    for (u8 i = 0; i < *num; i++)
    {
        if (kinds[i] == it_kind)
            return;
    }
    if (*num >= max_entries)
        return;
    kinds[*num] = it_kind;
    chances[*num] = weight;
    *num += 1;
}

// Make sure All-Up is reachable from every patch spawn source when the Max
// Stats Insanity goal is active and All-Up is unlocked: the three box pools
// (also used for sky and ground drops), the "Same Item" event pool, the
// blue-box subsequent pool, the chance_destructible column (star pole, event
// pillar, volcano walls), and Dyna Blade. Vanilla already places All-Up in
// the UFO / Tac / Meteor / Chamber columns; we leave those alone.
//
// Restricted to the Max Stats goal because in other modes, broadcasting All-Up
// across every source would make individual-patch unlocks pointless and skew
// the drop economy. Max Stats genuinely needs the saturation: 127 on every
// stat in ~7 minutes is only feasible if All-Up is plentiful.
//
// Called from FilterAllSpawnTables before the gate filters run.
void GateItems_EnsureAllUpInSpawnPools()
{
    if (ap_save->options.goal[GMMODE_CITYTRIAL] != GOAL_MAX_STATS_CT)
        return;
    if (!(ap_save->item_unlocked_mask & (1U << ITUNLOCK_ALLUP)))
        return;

    grBoxGeneObj *obj = *stc_grBoxGeneObj;
    if (obj)
    {
        for (int box = 0; box < BOXKIND_NUM; box++)
        {
            EnsureItemInPool(
                obj->item_group_spawn[box].it_kind,
                obj->item_group_spawn[box].chance,
                &obj->item_group_spawn[box].num,
                ITKIND_NUM - 1,
                ITKIND_ALLUP, ALLUP_BOX_POOL_CHANCE);
        }
        EnsureItemInPool(obj->sameitem_it_kind, obj->sameitem_chance,
                         &obj->sameitem_num, ITKIND_NUM - 1,
                         ITKIND_ALLUP, ALLUP_BOX_POOL_CHANCE);
        EnsureItemInPool(obj->subsequent_it_kind, obj->subsequent_chance,
                         &obj->subsequent_num, 40,
                         ITKIND_ALLUP, ALLUP_BOX_POOL_CHANCE);
    }

    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (info && info->item_desc)
    {
        for (int i = 0; i < info->item_desc->event_source_drop_num; i++)
        {
            if (info->item_desc->event_source_drop[i].it_kind != ITKIND_ALLUP)
                continue;
            if (info->item_desc->event_source_drop[i].chance_destructible == 0)
                info->item_desc->event_source_drop[i].chance_destructible = ALLUP_CHANCE_DESTRUCTIBLE;
            if (info->item_desc->event_source_drop[i].chance_dyna == 0)
                info->item_desc->event_source_drop[i].chance_dyna = ALLUP_CHANCE_DYNA;
            break;
        }
    }
}

static void FilterItemsFromPool(u8 *pool_kinds, u8 *pool_chances, u8 *pool_num)
{
    u8 num = *pool_num;
    u8 write = 0;
    u32 mask = ap_save->item_unlocked_mask;

    for (u8 read = 0; read < num; read++)
    {
        int bit = ItemKindToUnlockBit(pool_kinds[read]);
        if (bit >= 0 && !(mask & (1 << bit)))
            continue;

        if (write != read)
        {
            pool_kinds[write] = pool_kinds[read];
            pool_chances[write] = pool_chances[read];
        }
        write++;
    }

    *pool_num = write;
}

void GateItems_FilterSpawnTables()
{
    grBoxGeneObj *obj = *stc_grBoxGeneObj;
    if (!obj)
        return;

    for (int box = 0; box < BOXKIND_NUM; box++)
    {
        FilterItemsFromPool(
            obj->item_group_spawn[box].it_kind,
            obj->item_group_spawn[box].chance,
            &obj->item_group_spawn[box].num);
    }

    FilterItemsFromPool(
        obj->sameitem_it_kind,
        obj->sameitem_chance,
        &obj->sameitem_num);

    FilterItemsFromPool(
        obj->subsequent_it_kind,
        obj->subsequent_chance,
        &obj->subsequent_num);
}

void GateItems_FilterEventDropTables()
{
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (!info || !info->item_desc)
        return;

    u32 mask = ap_save->item_unlocked_mask;

    for (int i = 0; i < info->item_desc->event_source_drop_num; i++)
    {
        int bit = ItemKindToUnlockBit(info->item_desc->event_source_drop[i].it_kind);
        if (bit >= 0 && !(mask & (1 << bit)))
        {
            info->item_desc->event_source_drop[i].chance_dyna = 0;
            info->item_desc->event_source_drop[i].chance_tac = 0;
            info->item_desc->event_source_drop[i].chance_meteor = 0;
            info->item_desc->event_source_drop[i].chance_destructible = 0;
            info->item_desc->event_source_drop[i].chance_chamber = 0;
            info->item_desc->event_source_drop[i].chance_ufo = 0;
        }
    }
}

// Disable legendary piece spawns when all pieces of a type are locked.
// Called via hook after LegendaryPieces_Init sets up spawn data.
static void GateItems_FilterLegendaryPieces()
{
    LegendaryPieceData *lpd = *stc_legendary_piece_data;
    if (!lpd)
        return;

    u32 mask = ap_save->item_unlocked_mask;

    u32 dragoon_bits = (1 << ITUNLOCK_DRAGOON1) | (1 << ITUNLOCK_DRAGOON2) | (1 << ITUNLOCK_DRAGOON3);
    if (!(mask & dragoon_bits))
    {
        lpd->machine[0].is_enabled = 0;
        OSReport("[GateItems] Legendary Dragoon disabled (no pieces unlocked)\n");
    }

    u32 hydra_bits = (1 << ITUNLOCK_HYDRA1) | (1 << ITUNLOCK_HYDRA2) | (1 << ITUNLOCK_HYDRA3);
    if (!(mask & hydra_bits))
    {
        lpd->machine[1].is_enabled = 0;
        OSReport("[GateItems] Legendary Hydra disabled (no pieces unlocked)\n");
    }
}

// Hook after LegendaryPieces_Init returns in CityItemSpawn_Init.
// Clobbered instruction: lwz r3, 1552(r13) - restored after hook.
CODEPATCH_HOOKCREATE(0x800ec284,
    "",
    GateItems_FilterLegendaryPieces,
    "",
    0
)

// Wrapper for LegendaryPiece_MarkAsSpawned. Patched in via REPLACECALL at the
// two `bl` sites inside CityItemSpawn_SpawnLegendaryPiece (Dragoon at
// 0x800ed41c, Hydra at 0x800ed49c). If the piece's ITKIND is locked, we skip
// the call and the spawner box keeps its default forced_item (-1 = random
// pool roll). The legendary spawn slot is "consumed" (next_piece_index still
// advances in the caller), so the player won't be able to collect that piece
// until the AP unlock arrives in a later round.
static void GateItems_MarkAsSpawnedGated(int spawner, int item_kind)
{
    int bit = ItemKindToUnlockBit(item_kind);
    if (bit >= 0 && !(ap_save->item_unlocked_mask & (1 << bit)))
    {
        OSReport("[GateItems] Legendary piece %d (%s) locked - skipping spawn\n",
                 item_kind, ItemUnlockName(bit));
        return;
    }
    LegendaryPiece_MarkAsSpawned(spawner, item_kind);
}

void GateItems_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x800ec284);
    CODEPATCH_REPLACECALL(0x800ed41c, GateItems_MarkAsSpawnedGated); // Dragoon piece bl
    CODEPATCH_REPLACECALL(0x800ed49c, GateItems_MarkAsSpawnedGated); // Hydra piece bl
    OSReport("[GateItems] Legendary piece gating hooks installed\n");
}

int GateItems_UnlockItem(ItemUnlockKind kind)
{
    if (kind >= ITUNLOCK_NUM)
        return 0;

    ap_save->item_unlocked_mask |= (1 << kind);
    OSReport("[GateItems] Item %d (%s) unlocked (mask = %s)\n",
             kind, ItemUnlockName(kind), MaskBits(ap_save->item_unlocked_mask, 32));
    tb_api->EnqueueColoredNoun("Unlocked Item: ", ItemUnlockName(kind), tb_api->ItemColor, NULL);
    return 1;
}
