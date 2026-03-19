#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_items.h"
#include "textbox.h"

#define LEGENDARY_DRAGOON_ENABLE_OFFSET 0x38
#define LEGENDARY_HYDRA_ENABLE_OFFSET   0x70
#define LEGENDARY_ENABLE_BIT            0x40

static const char *item_names[ITUNLOCK_NUM] = {
    [ITUNLOCK_ALLUP]            = "All Up",
    [ITUNLOCK_SPEEDMAX]         = "Speed Max",
    [ITUNLOCK_SPEEDMIN]         = "Speed Min",
    [ITUNLOCK_OFFENSEMAX]       = "Offense Max",
    [ITUNLOCK_DEFENSEMAX]       = "Defense Max",
    [ITUNLOCK_CHARGEMAX]        = "Charge Max",
    [ITUNLOCK_CHARGENONE]       = "No Charge",
    [ITUNLOCK_CANDY]            = "Candy",
    [ITUNLOCK_FOODMAXIMTOMATO]  = "Maxim Tomato",
    [ITUNLOCK_FOODENERGYDRINK]  = "Energy Drink",
    [ITUNLOCK_FOODICECREAM]     = "Ice Cream",
    [ITUNLOCK_FOODRICEBALL]     = "Rice Ball",
    [ITUNLOCK_FOODCHICKEN]      = "Chicken",
    [ITUNLOCK_FOODCURRY]        = "Curry",
    [ITUNLOCK_FOODRAMEN]        = "Ramen",
    [ITUNLOCK_FOODOMELET]       = "Omelet",
    [ITUNLOCK_FOODHAMBURGER]    = "Hamburger",
    [ITUNLOCK_FOODSUSHI]        = "Sushi",
    [ITUNLOCK_FOODHOTDOG]       = "Hot Dog",
    [ITUNLOCK_FOODAPPLE]        = "Apple",
    [ITUNLOCK_FIREWORKS]        = "Fireworks",
    [ITUNLOCK_PANICSPIN]        = "Panic Spin",
    [ITUNLOCK_TIMEBOMB]         = "Time Bomb",
    [ITUNLOCK_GORDO]            = "Gordo",
    [ITUNLOCK_HYDRA1]           = "Hydra Piece 1",
    [ITUNLOCK_HYDRA2]           = "Hydra Piece 2",
    [ITUNLOCK_HYDRA3]           = "Hydra Piece 3",
    [ITUNLOCK_DRAGOON1]         = "Dragoon Piece 1",
    [ITUNLOCK_DRAGOON2]         = "Dragoon Piece 2",
    [ITUNLOCK_DRAGOON3]         = "Dragoon Piece 3",
    [ITUNLOCK_HP]               = "HP Recovery",
};

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
        case ITKIND_TIMEBOMB:         return ITUNLOCK_TIMEBOMB;
        case ITKIND_GORDO:            return ITUNLOCK_GORDO;
        case ITKIND_HYDRA1:           return ITUNLOCK_HYDRA1;
        case ITKIND_HYDRA2:           return ITUNLOCK_HYDRA2;
        case ITKIND_HYDRA3:           return ITUNLOCK_HYDRA3;
        case ITKIND_DRAGOON1:         return ITUNLOCK_DRAGOON1;
        case ITKIND_DRAGOON2:         return ITUNLOCK_DRAGOON2;
        case ITKIND_DRAGOON3:         return ITUNLOCK_DRAGOON3;
        case ITKIND_HP:               return ITUNLOCK_HP;
        default:                      return -1;
    }
}

static void FilterItemsFromPool(u8 *pool_kinds, u8 *pool_chances, u8 *pool_num)
{
    u8 num = *pool_num;
    u8 write = 0;
    u32 mask = save_data->item_unlocked_mask;

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

// Disable legendary piece spawns when all pieces of a type are locked.
// Called via hook after LegendaryPieces_Init sets up spawn data.
void GateItems_FilterLegendaryPieces()
{
    LegendaryPieceData *lpd = *stc_legendary_piece_data;
    if (!lpd)
        return;

    u32 mask = save_data->item_unlocked_mask;

    u32 dragoon_bits = (1 << ITUNLOCK_DRAGOON1) | (1 << ITUNLOCK_DRAGOON2) | (1 << ITUNLOCK_DRAGOON3);
    if (!(mask & dragoon_bits))
    {
        u8 *status = (u8 *)lpd + LEGENDARY_DRAGOON_ENABLE_OFFSET;
        *status &= ~LEGENDARY_ENABLE_BIT;
        OSReport("Legendary Dragoon disabled (no pieces unlocked)\n");
    }

    u32 hydra_bits = (1 << ITUNLOCK_HYDRA1) | (1 << ITUNLOCK_HYDRA2) | (1 << ITUNLOCK_HYDRA3);
    if (!(mask & hydra_bits))
    {
        u8 *status = (u8 *)lpd + LEGENDARY_HYDRA_ENABLE_OFFSET;
        *status &= ~LEGENDARY_ENABLE_BIT;
        OSReport("Legendary Hydra disabled (no pieces unlocked)\n");
    }
}

// Hook after LegendaryPieces_Init returns in CityItemSpawn_Init.
// Clobbered instruction: lwz r3, 1552(r13) — restored after hook.
CODEPATCH_HOOKCREATE(0x800ec284,
    "",
    GateItems_FilterLegendaryPieces,
    "",
    0
)

void GateItems_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x800ec284);
    OSReport("Legendary piece gating hook installed\n");
}

int GateItems_UnlockItem(ItemUnlockKind kind)
{
    if (kind >= ITUNLOCK_NUM)
        return 0;

    save_data->item_unlocked_mask |= (1 << kind);
    OSReport("Item %d (%s) unlocked (mask = 0x%08x)\n",
             kind, item_names[kind], save_data->item_unlocked_mask);
    TextBox_Enqueue(item_names[kind]);
    return 1;
}
