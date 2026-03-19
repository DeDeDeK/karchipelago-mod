#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_abilities.h"
#include "gate_patches.h"
#include "gate_items.h"
#include "gate_boxes.h"
#include "textbox.h"

static const char *ability_names[COPYKIND_NUM] = {
    [COPYKIND_FIRE]    = "Fire",
    [COPYKIND_WHEEL]   = "Wheel",
    [COPYKIND_SLEEP]   = "Sleep",
    [COPYKIND_SWORD]   = "Sword",
    [COPYKIND_BOMB]    = "Bomb",
    [COPYKIND_PLASMA]  = "Plasma",
    [COPYKIND_NEEDLE]  = "Needle",
    [COPYKIND_MIC]     = "Mic",
    [COPYKIND_ICE]     = "Ice",
    [COPYKIND_TORNADO] = "Tornado",
    [COPYKIND_BIRD]    = "Wing",
};

static int IsAbilityUnlocked(CopyKind kind)
{
    if (kind < 0 || kind >= COPYKIND_NUM)
        return 1;
    return (save_data->ability_unlocked_mask & (1 << kind)) != 0;
}

// Map ITKIND_COPY* to CopyKind. Returns COPYKIND_NONE for non-copy items.
static CopyKind ItemKindToCopyKind(u8 it_kind)
{
    switch (it_kind)
    {
        case ITKIND_COPYFIRE:    return COPYKIND_FIRE;
        case ITKIND_COPYTIRE:    return COPYKIND_WHEEL;
        case ITKIND_COPYSLEEP:   return COPYKIND_SLEEP;
        case ITKIND_COPYSWORD:   return COPYKIND_SWORD;
        case ITKIND_COPYBOMB:    return COPYKIND_BOMB;
        case ITKIND_COPYPLASMA:  return COPYKIND_PLASMA;
        case ITKIND_COPYSPIKE:   return COPYKIND_NEEDLE;
        case ITKIND_COPYMIC:     return COPYKIND_MIC;
        case ITKIND_COPYICE:     return COPYKIND_ICE;
        case ITKIND_COPYTORNADO: return COPYKIND_TORNADO;
        case ITKIND_COPYBIRD:    return COPYKIND_BIRD;
        default:                 return COPYKIND_NONE;
    }
}

// Remove locked copy items from a spawn pool in-place.
static void FilterCopyItemsFromPool(u8 *pool_kinds, u8 *pool_chances, u8 *pool_num)
{
    u8 num = *pool_num;
    u8 write = 0;

    for (u8 read = 0; read < num; read++)
    {
        CopyKind ck = ItemKindToCopyKind(pool_kinds[read]);
        if (ck != COPYKIND_NONE && !IsAbilityUnlocked(ck))
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

// Called after CityItemSpawn_InitItemFallChances and CityEvent_ModifyItemFallDesc
// populate the spawn tables. Removes locked copy abilities from all item pools.
void GateAbilities_FilterSpawnTables()
{
    grBoxGeneObj *obj = *stc_grBoxGeneObj;
    if (!obj)
        return;

    for (int box = 0; box < BOXKIND_NUM; box++)
    {
        FilterCopyItemsFromPool(
            obj->item_group_spawn[box].it_kind,
            obj->item_group_spawn[box].chance,
            &obj->item_group_spawn[box].num);
    }

    FilterCopyItemsFromPool(
        obj->sameitem_it_kind,
        obj->sameitem_chance,
        &obj->sameitem_num);

    FilterCopyItemsFromPool(
        obj->subsequent_it_kind,
        obj->subsequent_chance,
        &obj->subsequent_num);

    // Chain to other spawn table filters that share these hook points
    GatePatches_FilterSpawnTables();
    GateItems_FilterSpawnTables();

    // After all filtering, update which box types still have valid items
    GateBoxes_UpdateItemAvailability();
}

// Replacement for Rider_CheckAndGiveAbility (0x80192650).
// Gates copy abilities from item pickups and enemy interactions.
// Our mod's Ability_GiveItem calls Rider_GiveAbility directly, bypassing this gate.
int GateAbilities_CheckAndGiveAbility(GOBJ *gobj, int kind)
{
    RiderData *rd = gobj->userdata;
    if (rd->kind != RDKIND_KIRBY)
        return 0;

    if (kind >= 0 && kind < COPYKIND_NUM && !IsAbilityUnlocked(kind))
        return 0;

    return Rider_GiveAbility(rd, kind);
}

// Pick a random unlocked ability. Returns -1 if none are unlocked.
static int RandomUnlockedAbility()
{
    int unlocked[COPYKIND_NUM];
    int count = 0;

    for (int i = 0; i < COPYKIND_NUM; i++)
    {
        if (IsAbilityUnlocked(i) && stc_ability_init_table[i] != NULL)
            unlocked[count++] = i;
    }

    if (count == 0)
        return -1;

    return unlocked[HSD_Randi(count)];
}

// Replacement for randomAbility_giveAbility (0x801a61d4).
// Gates copy abilities from the copy chance wheel. If the wheel lands on a
// locked ability, picks a random unlocked one instead.
int GateAbilities_RandomGiveAbility(RiderData *rd, int kind)
{
    if (kind == -1)
        return 0;

    if (kind >= 0 && kind < COPYKIND_NUM && !IsAbilityUnlocked(kind))
        kind = RandomUnlockedAbility();

    if (kind == -1 || stc_ability_init_table[kind] == NULL)
        return 0;

    Rider_AbilityRemoveModel(rd);
    Rider_AbilityRemoveUnk(rd);
    Rider_RecordCopyAbility(rd->ply, kind);
    stc_ability_init_table[kind](rd);
    return 1;
}

// Hook at end of CityItemSpawn_InitItemFallChances (0x800eb558).
// Clobbered instruction: lwz r0, 0x34(r1)
CODEPATCH_HOOKCREATE(0x800eb558,
    "",
    GateAbilities_FilterSpawnTables,
    "",
    0
)

// Hook at end of CityEvent_ModifyItemFallDesc (0x800ed7f0).
// Clobbered instruction: lwz r0, 0x14(r1)
CODEPATCH_HOOKCREATE(0x800ed7f0,
    "",
    GateAbilities_FilterSpawnTables,
    "",
    0
)

void GateAbilities_OnBoot()
{
    CODEPATCH_REPLACEFUNC(Rider_CheckAndGiveAbility, GateAbilities_CheckAndGiveAbility);
    CODEPATCH_REPLACEFUNC(randomAbility_giveAbility, GateAbilities_RandomGiveAbility);
    CODEPATCH_HOOKAPPLY(0x800eb558);
    CODEPATCH_HOOKAPPLY(0x800ed7f0);
    OSReport("Copy ability gating hooks installed\n");
}

int GateAbilities_UnlockAbility(CopyKind kind)
{
    if (kind >= COPYKIND_NUM)
        return 0;

    save_data->ability_unlocked_mask |= (1 << kind);
    OSReport("Ability %d (%s) unlocked (mask = 0x%04x)\n",
             kind, ability_names[kind], save_data->ability_unlocked_mask);
    TextBox_Enqueue(ability_names[kind]);
    return 1;
}
