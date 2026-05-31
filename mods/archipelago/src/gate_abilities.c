#include "game.h"
#include "enemy.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_abilities.h"
#include "ability_item.h"
#include "textbox_api.h"
#include "traplink.h"
#include "inline.h"

// Caller-side bounds checks ensure kind is in [0, COPYKIND_NUM).
static int IsAbilityUnlocked(CopyKind kind)
{
    return (ap_save->ability_unlocked_mask & (1 << kind)) != 0;
}

// Returns 1 if the item kind maps to a locked copy ability.
static int IsCopyItemLocked(u8 it_kind)
{
    CopyKind ck = Ability_ItKindToCopyKind(it_kind);
    return ck != COPYKIND_NONE && !IsAbilityUnlocked(ck);
}

// Remove locked copy items from a spawn pool in-place.
static void FilterCopyItemsFromPool(u8 *pool_kinds, u8 *pool_chances, u8 *pool_num)
{
    u8 num = *pool_num;
    u8 write = 0;

    for (u8 read = 0; read < num; read++)
    {
        if (IsCopyItemLocked(pool_kinds[read]))
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

// Removes locked copy abilities from the event-source drop table.
// Zeroes all drop chances (misc/tac/meteor/pilar/chamber/ufo) for any entry
// whose item kind maps to a locked ability. Sole caller: item_spawn_filter.c.
void GateAbilities_FilterEventDropTables()
{
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (!info || !info->item_desc)
        return;

    for (int i = 0; i < info->item_desc->event_source_drop_num; i++)
    {
        if (IsCopyItemLocked(info->item_desc->event_source_drop[i].it_kind))
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

// Removes locked copy abilities from all box spawn pools.
// Sole caller: item_spawn_filter.c.
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

    int result = Rider_GiveAbility(rd, kind);

    // Traplink send: getting sleep from an enemy/item is a natural trap.
    // Only send on a successful grant — Rider_GiveAbility can return 0 if the
    // rider is in an unable state, and we don't want to send phantom traps.
    if (result && kind == COPYKIND_SLEEP && !Ply_CheckIfCPU(rd->ply))
        TrapLink_Send(TRAPLINK_KIND_SLEEP);

    return result;
}

// Pick a random unlocked ability. Returns -1 if none are unlocked.
static int RandomUnlockedAbility()
{
    int unlocked[COPYKIND_NUM];
    int count = 0;

    for (int i = 0; i < COPYKIND_NUM; i++)
    {
        if (IsAbilityUnlocked(i))
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
    if (kind < 0 || kind >= COPYKIND_NUM)
        return 0;

    if (!IsAbilityUnlocked(kind))
        kind = RandomUnlockedAbility();

    if (kind == -1)
        return 0;

    Rider_AbilityRemoveModel(rd);
    Rider_AbilityRemoveUnk(rd);
    Rider_RecordCopyAbility(rd->ply, kind);
    Rider_MarkCopyAbilityObtained(rd->ply, kind);
    stc_ability_init_table[kind](rd);
    return 1;
}

// Per-slot copy ability theme. T0/T1/T2 share the same 24-slot mapping because
// the copy ability is tied to the archive (data_index), not the tier flags —
// e.g., T1 Heat Phan-Phan is visually distinct but shares Phan-Phan's Fire
// archive. Used to zero spawn weights for ability-themed enemies when their
// ability is locked. The copy chance wheel is random regardless; this table is
// about visual theming.
static const s8 enemy_slot_copykind[ACTORID_ENEMIES_PER_TIER] = {
    COPYKIND_NONE,    // 0  Broom Hatter
    COPYKIND_NONE,    // 1  Broom Hatter (dup)
    COPYKIND_NONE,    // 2  Bronto Burt
    COPYKIND_NONE,    // 3  Bronto Burt (dup)
    COPYKIND_NONE,    // 4  Scarfy
    COPYKIND_SWORD,   // 5  Sword Knight
    COPYKIND_NONE,    // 6  Cappy
    COPYKIND_NONE,    // 7  Cappy (flags=4)
    COPYKIND_WHEEL,   // 8  Wheelie
    COPYKIND_FIRE,    // 9  Phan Phan / Heat Phan-Phan
    COPYKIND_SLEEP,   // 10 Noddy
    COPYKIND_FREEZE,  // 11 Chilly
    COPYKIND_BIRD,    // 12 Flappy
    COPYKIND_PLASMA,  // 13 Plasma Wisp
    COPYKIND_NONE,    // 14 Gordo
    COPYKIND_BOMB,    // 15 Bomber
    COPYKIND_NEEDLE,  // 16 Pichikuri
    COPYKIND_NEEDLE,  // 17 Pichikuri (dup)
    COPYKIND_FIRE,    // 18 Dayl
    COPYKIND_FIRE,    // 19 Dayl (flags=4)
    COPYKIND_TORNADO, // 20 Caller (internal: Shaturn)
    COPYKIND_MIC,     // 21 Walky
    COPYKIND_NONE,    // 22 Waddle Dee Truck
    COPYKIND_NONE,    // 23 Waddle Dee
};

// Map an enemy_id to its CopyKind theme. T0/T1/T2 fold to the same slot;
// specials are all NONE except SP Sword Knight (0x49).
static CopyKind EnemyIDToCopyKind(int enemy_id)
{
    if (enemy_id >= ACTORID_TIER0_START && enemy_id < ACTORID_SPECIAL_START)
        return enemy_slot_copykind[(enemy_id - ACTORID_TIER0_START) % ACTORID_ENEMIES_PER_TIER];
    if (enemy_id == ACTORID_SP_SWORD_KNIGHT)
        return COPYKIND_SWORD;
    return COPYKIND_NONE;
}

// Check if enemy_id is themed around a locked ability. Returns 1 if locked, 0 if allowed.
static int IsEnemyAbilityLocked(int enemy_id)
{
    CopyKind ck = EnemyIDToCopyKind(enemy_id);
    return ck != COPYKIND_NONE && !IsAbilityUnlocked(ck);
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

// Mode 1 (Air Ride courses) / Mode 3 (STKIND_MELEE2):
// Per-entry has ids[max_slots] and weights[max_slots] at known offsets.
// Zero weights for locked-ability enemies, filter meta-enemy sub-tables.
static void FilterMode1Or3(EnemySpawnData *data, int ids_offset, int weights_offset, int max_slots)
{
    if (!data->spawn_entries || data->spawn_count <= 0)
        return;

    // Per-meta-enemy filter results: 1 = has valid enemies, 0 = all zeroed, -1 = not yet processed
    s8 meta_valid[15];
    for (int m = 0; m < 15; m++)
        meta_valid[m] = -1;

    for (int i = 0; i < data->spawn_count; i++)
    {
        char *entry = data->spawn_entries + i * 0x38;
        short *ids = (short *)(entry + ids_offset);
        short *weights = (short *)(entry + weights_offset);

        for (int slot = 0; slot < max_slots; slot++)
        {
            if (weights[slot] == -1)
                break;

            int enemy_id = ids[slot];
            if (enemy_id < 0)
                continue;

            // Meta-enemy IDs (0x50-0x5E): filter secondary sub-table, then zero
            // primary weight if no valid enemies remain in the sub-table.
            if (enemy_id >= 0x50 && enemy_id <= 0x5E)
            {
                int meta = enemy_id - 0x50;
                if (meta_valid[meta] == -1)
                {
                    short *sub_table = data->secondary_table
                                       ? (short *)data->secondary_table[meta]
                                       : NULL;
                    meta_valid[meta] = sub_table ? FilterSecondarySubTable(sub_table) : 0;
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

// Mode 2 (STKIND_MELEE1):
// Two-stage selection: first picks a meta-enemy category from secondary_table[0],
// then picks an individual enemy from that category's weight column in the entries.
// Per-entry: enemy_id at +0x06, weight columns at +0x08 (one per category).
// Zero all weight columns for entries whose enemy has a locked ability.
// Also filter secondary_table[0] to remove categories where all enemies are locked.
static void FilterMode2(EnemySpawnData *data)
{
    if (!data->spawn_entries || data->spawn_count <= 0 || !data->secondary_table)
        return;

    short *sub_table = (short *)data->secondary_table[0];
    if (!sub_table)
        return;

    // Count meta-enemy categories in secondary[0]
    int num_categories = 0;
    while (sub_table[num_categories * 2] != -1)
        num_categories++;

    if (num_categories == 0)
        return;

    // Zero all weight columns for entries with locked-ability enemies
    int zeroed_entries = 0;
    for (int i = 0; i < data->spawn_count; i++)
    {
        char *entry = data->spawn_entries + i * 0x38;
        int enemy_id = *(short *)(entry + 0x06);

        if (enemy_id < 0)
            continue;

        if (IsEnemyAbilityLocked(enemy_id))
        {
            short *weights = (short *)(entry + 0x08);
            for (int col = 0; col < num_categories; col++)
                weights[col] = 0;
            zeroed_entries++;
        }
    }

    // Filter secondary[0] sub-table: zero category weight if ALL entries in that
    // category's column have been zeroed (no valid enemies remain).
    int zeroed_categories = 0;
    for (int cat = 0; cat < num_categories; cat++)
    {
        int has_valid = 0;
        for (int i = 0; i < data->spawn_count; i++)
        {
            short *weights = (short *)(data->spawn_entries + i * 0x38 + 0x08);
            if (weights[cat] > 0)
            {
                has_valid = 1;
                break;
            }
        }
        if (!has_valid)
        {
            sub_table[cat * 2 + 1] = 0;
            zeroed_categories++;
        }
    }
    OSReport("[GateAbilities] Mode2: zeroed %d/%d entries, %d/%d categories\n",
             zeroed_entries, data->spawn_count, zeroed_categories, num_categories);
}

// Zero spawn weights for enemies whose copy ability is locked.
// Modifies the .dat data in-place; it is reloaded from disc each stage load.
// Early-exits when spawn_data is NULL (CT Free Run, Top Ride, any mode where
// the per-stage "enemies enabled" flag is off).
void GateAbilities_On3DLoadEnd()
{
    EnemySpawnData *data = *stc_enemy_spawn_data;
    if (!data || !data->config)
        return;

    short mode = data->config->mode;
    OSReport("[GateAbilities] Filtering enemy spawns (mode=%d, entries=%d, ability_mask=%s)\n",
             mode, data->spawn_count, MaskBits(ap_save->ability_unlocked_mask, 16));

    switch (mode)
    {
        case 1: FilterMode1Or3(data, 0x1e, 0x26, 4); break;
        case 2: FilterMode2(data); break;
        case 3: FilterMode1Or3(data, 0x06, 0x10, 5); break;
        default:
            OSReport("[GateAbilities] Unknown spawn mode %d — skipping\n", mode);
            break;
    }
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
    OSReport("[GateAbilities] Copy ability gating hooks installed\n");
}

int GateAbilities_UnlockAbility(CopyKind kind)
{
    if (kind >= COPYKIND_NUM)
        return 0;

    ap_save->ability_unlocked_mask |= (1 << kind);
    OSReport("[GateAbilities] Ability %d (%s) unlocked (mask = %s)\n",
             kind, CopyKind_Names[kind], MaskBits(ap_save->ability_unlocked_mask, 16));
    tb_api->EnqueueColoredNoun("Unlocked Ability: ", CopyKind_Names[kind], tb_api->AbilityColors[kind], NULL);
    return 1;
}
