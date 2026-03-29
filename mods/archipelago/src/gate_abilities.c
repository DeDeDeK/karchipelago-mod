#include "game.h"
#include "enemy.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_abilities.h"
#include "gate_patches.h"
#include "gate_items.h"
#include "gate_boxes.h"
#include "textbox.h"
#include "traplink.h"

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

    // Traplink send: getting sleep from an enemy/item is a natural trap
    if (kind == COPYKIND_SLEEP && !Ply_CheckIfCPU(rd->ply))
        TrapLink_Send();

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
// The callers (randomAbility_aPress/autoSelect) originally called
// Rider_MarkCopyAbilityObtained after this function with the original wheel
// kind. We NOP those calls and do it ourselves with the (possibly substituted)
// kind so the obtained-abilities bitmask tracks correctly.
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
    Rider_MarkCopyAbilityObtained(rd->ply, kind);
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

// Enemy data table at 0x804b22b4. Each entry is {int data_index, int flags}.
// data_index identifies the enemy type (archive to load).
// flags selects the tier variant (0=base, 1=enhanced, 2/3/4=special).
#define ENEMY_DATA_TABLE ((int *)0x804b22b4)
// Maps enemy_id → ability theme (CopyKind). Used to filter ability-themed enemies
// from spawn tables when their ability is locked. The copy chance wheel is random
// for all enemies — this table is about visual theming, not actual granted abilities.
//
// Enemy ID table layout (0x804b22b4):
//   IDs 0-23:  Tier 0 (flags=0) — base enemies
//   IDs 24-47: Tier 1 (flags=1) — enhanced variants
//   IDs 48-71: Tier 2 (flags=1) — enhanced variants
//   IDs 72-78: Special enemies
// Each tier has 24 slots mapping to data_indices: 0,0,1,1,2,3,4,4,5,6,7,8,9,10,11,12,13,13,14,14,15,16,17,18
//
// Tier 1/2 enemies share the same archive as tier 0 but load different model/stat
// data via the flags field. Some tier 1/2 COPYKIND_NONE enemies become ability-themed
// variants (e.g., tier 1 Phan Phan = Heat PhanPhan, fire-themed).
#define T0(slot) (slot)
#define T1(slot) (24 + (slot))
#define T2(slot) (48 + (slot))
static const s8 enemy_id_to_copykind[ACTORID_NUM] = {
    // Tier 0 (IDs 0-23, base enemies)
    [T0(0)]  = COPYKIND_NONE,    // Broom Hatter
    [T0(1)]  = COPYKIND_NONE,    // Broom Hatter (dup)
    [T0(2)]  = COPYKIND_NONE,    // Bronto Burt
    [T0(3)]  = COPYKIND_NONE,    // Bronto Burt (dup)
    [T0(4)]  = COPYKIND_NONE,    // Scarfy
    [T0(5)]  = COPYKIND_SWORD,   // Sword Knight
    [T0(6)]  = COPYKIND_NONE,    // Cappy
    [T0(7)]  = COPYKIND_NONE,    // Cappy (flags=4)
    [T0(8)]  = COPYKIND_WHEEL,   // Wheelie
    [T0(9)]  = COPYKIND_NONE,    // Phan Phan
    [T0(10)] = COPYKIND_SLEEP,   // Noddy
    [T0(11)] = COPYKIND_ICE,     // Chilly
    [T0(12)] = COPYKIND_BIRD,    // Flappy
    [T0(13)] = COPYKIND_PLASMA,  // Plasma Wisp
    [T0(14)] = COPYKIND_NONE,    // Gordo
    [T0(15)] = COPYKIND_BOMB,    // Bombbone
    [T0(16)] = COPYKIND_NEEDLE,  // Pichikuri
    [T0(17)] = COPYKIND_NEEDLE,  // Pichikuri (dup)
    [T0(18)] = COPYKIND_FIRE,    // Dayl (fire enemy)
    [T0(19)] = COPYKIND_FIRE,    // Dayl (flags=4)
    [T0(20)] = COPYKIND_TORNADO, // Caller (internal: Shaturn)
    [T0(21)] = COPYKIND_MIC,     // Walky
    [T0(22)] = COPYKIND_NONE,    // Waddle Dee Truck
    [T0(23)] = COPYKIND_NONE,    // Waddle Dee

    // Tier 1 (IDs 24-47, enhanced variants)
    [T1(0)]  = COPYKIND_NONE,    // Broom Hatter
    [T1(1)]  = COPYKIND_NONE,    // Broom Hatter (dup)
    [T1(2)]  = COPYKIND_NONE,    // Bronto Burt
    [T1(3)]  = COPYKIND_NONE,    // Bronto Burt (dup)
    [T1(4)]  = COPYKIND_NONE,    // Scarfy
    [T1(5)]  = COPYKIND_SWORD,   // Sword Knight
    [T1(6)]  = COPYKIND_NONE,    // Cappy
    [T1(7)]  = COPYKIND_NONE,    // Cappy (flags=4)
    [T1(8)]  = COPYKIND_WHEEL,   // Wheelie
    [T1(9)]  = COPYKIND_FIRE,    // Heat PhanPhan (tier 1 Phan Phan)
    [T1(10)] = COPYKIND_SLEEP,   // Noddy
    [T1(11)] = COPYKIND_ICE,     // Chilly
    [T1(12)] = COPYKIND_BIRD,    // Flappy
    [T1(13)] = COPYKIND_PLASMA,  // Plasma Wisp
    [T1(14)] = COPYKIND_NONE,    // Gordo
    [T1(15)] = COPYKIND_BOMB,    // Bombbone
    [T1(16)] = COPYKIND_NEEDLE,  // Pichikuri
    [T1(17)] = COPYKIND_NEEDLE,  // Pichikuri (dup)
    [T1(18)] = COPYKIND_FIRE,    // Dayl
    [T1(19)] = COPYKIND_FIRE,    // Dayl (flags=4)
    [T1(20)] = COPYKIND_TORNADO, // Caller
    [T1(21)] = COPYKIND_MIC,     // Walky
    [T1(22)] = COPYKIND_NONE,    // Waddle Dee Truck
    [T1(23)] = COPYKIND_NONE,    // Waddle Dee

    // Tier 2 (IDs 48-71, enhanced variants)
    [T2(0)]  = COPYKIND_NONE,    // Broom Hatter
    [T2(1)]  = COPYKIND_NONE,    // Broom Hatter (dup)
    [T2(2)]  = COPYKIND_NONE,    // Bronto Burt
    [T2(3)]  = COPYKIND_NONE,    // Bronto Burt (dup)
    [T2(4)]  = COPYKIND_NONE,    // Scarfy
    [T2(5)]  = COPYKIND_SWORD,   // Sword Knight
    [T2(6)]  = COPYKIND_NONE,    // Cappy
    [T2(7)]  = COPYKIND_NONE,    // Cappy (flags=4)
    [T2(8)]  = COPYKIND_WHEEL,   // Wheelie
    [T2(9)]  = COPYKIND_FIRE,    // Heat PhanPhan (tier 2 Phan Phan)
    [T2(10)] = COPYKIND_SLEEP,   // Noddy
    [T2(11)] = COPYKIND_ICE,     // Chilly
    [T2(12)] = COPYKIND_BIRD,    // Flappy
    [T2(13)] = COPYKIND_PLASMA,  // Plasma Wisp
    [T2(14)] = COPYKIND_NONE,    // Gordo
    [T2(15)] = COPYKIND_BOMB,    // Bombbone
    [T2(16)] = COPYKIND_NEEDLE,  // Pichikuri
    [T2(17)] = COPYKIND_NEEDLE,  // Pichikuri (dup)
    [T2(18)] = COPYKIND_FIRE,    // Dayl
    [T2(19)] = COPYKIND_FIRE,    // Dayl (flags=4)
    [T2(20)] = COPYKIND_TORNADO, // Caller
    [T2(21)] = COPYKIND_MIC,     // Walky
    [T2(22)] = COPYKIND_NONE,    // Waddle Dee Truck
    [T2(23)] = COPYKIND_NONE,    // Waddle Dee

    // Special enemies (IDs 72-78)
    [72] = COPYKIND_NONE,    // Broom Hatter (flags=2)
    [73] = COPYKIND_SWORD,   // Sword Knight (flags=2)
    [74] = COPYKIND_NONE,    // Waddle Dee Truck (flags=2)
    [75] = COPYKIND_NONE,    // Gordo (flags=3)
    [76] = COPYKIND_NONE,    // TAC
    [77] = COPYKIND_NONE,    // Dyna Blade
    [78] = COPYKIND_NONE,    // Meteor
};

// Spawn data structure populated by Enemy_InitPositionData from stage .dat archive.
// Pointer stored at 0x805dd710.
//   +0x00: short spawn_count
//   +0x04: int*  spawn_entries (stride 0x38 per entry)
//   +0x10: int*  config (mode short at config+0x28; mode 1 = Air Ride)
// Per-entry layout (Air Ride / mode 1):
//   +0x1e: short enemy_ids[4]
//   +0x26: short weights[4]  (-1 terminated)
#define ENEMY_SPAWN_DATA (*(char **)0x805dd710)

// Check if enemy_id is themed around a locked ability. Returns 1 if locked, 0 if allowed.
static int IsEnemyAbilityLocked(int enemy_id)
{
    if (enemy_id < 0 || enemy_id >= ACTORID_NUM)
        return 0;
    CopyKind ck = (CopyKind)enemy_id_to_copykind[enemy_id];
    return (ck != COPYKIND_NONE && !IsAbilityUnlocked(ck));
}

// Filter a secondary sub-table: zero weights for locked-ability enemies.
// Returns 1 if any entry with positive weight remains, 0 if all zeroed.
static int FilterSecondarySubTable(short *sub_table)
{
    int has_valid = 0;
    for (int j = 0; sub_table[j * 2 + 1] != -1; j++)
    {
        if (IsEnemyAbilityLocked(sub_table[j * 2]))
            sub_table[j * 2 + 1] = 0;
        else if (sub_table[j * 2 + 1] > 0)
            has_valid = 1;
    }
    return has_valid;
}

// Wrapper for Enemy_SpawnActor called from Enemy_SpawnerDecide (Air Ride mode).
// Blocks meta-enemy variant spawns when ANY ability is locked, since variant enemies
// gain ability themes that we can't strip (the appearance/ability is determined by
// mechanisms beyond the variant byte passed to Enemy_SpawnActor).
static void GateAbilities_SpawnActor(int spawn_slot, int enemy_id_packed, int position_index)
{
    int base_id = enemy_id_packed & 0xFF;
    int variant = enemy_id_packed >> 8;

    OSReport("[SpawnActor] slot=%d id_packed=0x%04x base=%d variant=%d\n",
             spawn_slot, enemy_id_packed, base_id, variant);

    if (variant > 0 && save_data->ability_unlocked_mask != ((1 << COPYKIND_NUM) - 1))
    {
        OSReport("[SpawnActor] BLOCKED variant=%d (not all abilities unlocked, mask=0x%04x)\n",
                 variant, save_data->ability_unlocked_mask);
        // Not all abilities unlocked — skip this variant enemy spawn entirely.
        // Pass -1 to trigger respawn delay without creating an enemy.
        Enemy_SpawnActor(spawn_slot, -1, position_index);
        return;
    }

    Enemy_SpawnActor(spawn_slot, enemy_id_packed, position_index);
}

// Zero spawn weights for enemies whose copy ability is locked.
// Modifies the .dat data in-place; it is reloaded from disc each stage load.
static void FilterEnemySpawnWeights()
{
    char *spawn_data = ENEMY_SPAWN_DATA;
    if (!spawn_data)
        return;

    int *config = *(int **)(spawn_data + 0x10);
    if (!config)
        return;
    short mode = *(short *)((char *)config + 0x28);
    if (mode != 1)
        return;

    short spawn_count = *(short *)spawn_data;
    char *entries = (char *)(*(int *)(spawn_data + 4));
    if (!entries || spawn_count <= 0)
        return;

    char *secondary = (char *)(*(int *)(spawn_data + 0x0C));

    OSReport("[FilterEnemy] mode=%d spawn_count=%d secondary=%p mask=0x%04x\n",
             mode, spawn_count, secondary, save_data->ability_unlocked_mask);

    // Per-meta-enemy filter results: 1 = has valid enemies, 0 = all zeroed, -1 = not yet processed
    s8 meta_valid[15];
    for (int m = 0; m < 15; m++)
        meta_valid[m] = -1;

    for (int i = 0; i < spawn_count; i++)
    {
        char *entry = entries + i * 0x38;
        short *ids = (short *)(entry + 0x1e);
        short *weights = (short *)(entry + 0x26);

        for (int slot = 0; slot < 4; slot++)
        {
            if (weights[slot] == -1)
                break;

            int enemy_id = ids[slot];
            if (enemy_id < 0)
                continue;

            OSReport("[FilterEnemy] entry[%d] slot[%d]: id=%d weight=%d locked=%d\n",
                     i, slot, enemy_id, weights[slot], IsEnemyAbilityLocked(enemy_id));

            // Meta-enemy IDs (0x50-0x5E): filter secondary sub-table, then zero
            // primary weight if no valid enemies remain in the sub-table.
            if (enemy_id >= 0x50 && enemy_id <= 0x5E)
            {
                int meta = enemy_id - 0x50;
                if (meta_valid[meta] == -1)
                {
                    if (secondary)
                    {
                        short *sub_table = (short *)(*(int *)(secondary + meta * 4));
                        meta_valid[meta] = (sub_table) ? FilterSecondarySubTable(sub_table) : 0;
                    }
                    else
                    {
                        meta_valid[meta] = 0;
                    }
                }
                if (!meta_valid[meta])
                    weights[slot] = 0;
                continue;
            }

            // Normal enemy IDs: check ability directly.
            if (IsEnemyAbilityLocked(enemy_id))
                weights[slot] = 0;
        }
    }
}

void GateAbilities_On3DLoadEnd()
{
    if (Gm_IsInCity())
        return;

    FilterEnemySpawnWeights();
}

void GateAbilities_OnBoot()
{
    CODEPATCH_REPLACEFUNC(Rider_CheckAndGiveAbility, GateAbilities_CheckAndGiveAbility);
    CODEPATCH_REPLACEFUNC(randomAbility_giveAbility, GateAbilities_RandomGiveAbility);
    // NOP the Rider_MarkCopyAbilityObtained calls in randomAbility_aPress and
    // randomAbility_autoSelect — our replacement calls it with the correct
    // (possibly substituted) kind instead of the original wheel kind.
    CODEPATCH_REPLACEINSTRUCTION(0x801ae874, 0x60000000); // NOP: aPress bl MarkCopyAbilityObtained
    CODEPATCH_REPLACEINSTRUCTION(0x801ae910, 0x60000000); // NOP: autoSelect bl MarkCopyAbilityObtained
    CODEPATCH_REPLACECALL(0x800f1e14, GateAbilities_SpawnActor);
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
