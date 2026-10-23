#include <stddef.h>
#include <string.h>

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
#include "ap_announce.h"

// Callers bound kind to [0, COPYKIND_NUM).
static int IsAbilityUnlocked(CopyKind kind)
{
    return (ap_save->ability_unlocked_mask & (1 << kind)) != 0;
}

int GateAbilities_IsItemLocked(u8 it_kind)
{
    CopyKind ck = Ability_ItKindToCopyKind(it_kind);
    return ck != COPYKIND_NONE && !IsAbilityUnlocked(ck);
}

// Replaces RiderGObj_CheckAndGiveAbility (0x80192650), the pickup and enemy copy entry
// point. AP grants call Rider_GiveAbility directly and bypass it.
static int GateAbilities_CheckAndGiveAbility(GOBJ *gobj, int kind)
{
    RiderData *rd = gobj->userdata;
    if (rd->kind != RDKIND_KIRBY)
        return 0;

    if (kind >= 0 && kind < COPYKIND_NUM && !IsAbilityUnlocked(kind))
        return 0;

    int result = Rider_GiveAbility(rd, kind);

    // Rider_GiveAbility returns 0 in an unable state; only a real grant sends the trap.
    if (result && kind == COPYKIND_SLEEP && Ply_GetPKind(rd->ply) == PKIND_HMN)
        TrapLink_Send(TRAPLINK_KIND_SLEEP);

    return result;
}

// The kind the last wheel grant landed on, -1 when it granted nothing.
static int wheel_granted = -1;

// Replaces randomAbility_giveAbility (0x801a61d4): a locked wheel result is swapped for a
// random unlocked ability.
static int GateAbilities_RandomGiveAbility(RiderData *rd, int kind)
{
    if (kind >= 0 && kind < COPYKIND_NUM && !IsAbilityUnlocked(kind))
        kind = RandomBitInField(ap_save->ability_unlocked_mask & ((1u << COPYKIND_NUM) - 1));

    wheel_granted = -1;

    // Nothing unlocked: the post-swallow state exits only through a grant, so take the
    // engine's "nothing to give" exit or the rider is stranded there for the match.
    if (kind < 0 || kind >= COPYKIND_NUM)
    {
        Rider_AbilityRemoveModel(rd);
        Rider_AbilityClearQueued(rd);
        Rider_ResolveQueuedAbility(rd);
        return 0;
    }

    Rider_AbilityRemoveModel(rd);
    Rider_AbilityClearQueued(rd);
    Ply_RecordCopyAbility(rd->ply, kind);
    stc_ability_init_table[kind](rd);
    wheel_granted = kind;
    return 1;
}

// Replaces the bl Ply_MarkCopyAbilityObtained that follows the wheel grant in
// randomAbility_aPress (0x801ae7f4) and randomAbility_autoSelect (0x801ae890), so the mark
// names the kind actually granted. randomAbility_queuedGive (0x801aec60) grants without
// marking, as vanilla does.
static void GateAbilities_MarkWheelGrant(int ply, int kind)
{
    (void)kind;
    if (wheel_granted >= 0)
        Ply_MarkCopyAbilityObtained(ply, wheel_granted);
}

// Copy ability per enemy slot, shared by T0/T1/T2: it follows data_index, not the tier.
static const s8 enemy_slot_copykind[ENEMYKIND_ENEMIES_PER_TIER] = {
    COPYKIND_NONE,    // 0  Broom Hatter
    COPYKIND_NONE,    // 1  Broom Hatter (dup)
    COPYKIND_NONE,    // 2  Bronto Burt
    COPYKIND_NONE,    // 3  Bronto Burt (dup)
    COPYKIND_NONE,    // 4  Scarfy
    COPYKIND_SWORD,   // 5  Sword Knight
    COPYKIND_NONE,    // 6  Cappy
    COPYKIND_NONE,    // 7  Cappy (flags=4)
    COPYKIND_TIRE,    // 8  Wheelie
    COPYKIND_FIRE,    // 9  Phan Phan / Heat Phan-Phan
    COPYKIND_SLEEP,   // 10 Noddy
    COPYKIND_ICE,     // 11 Chilly
    COPYKIND_BIRD,    // 12 Flappy
    COPYKIND_PLASMA,  // 13 Plasma Wisp
    COPYKIND_NONE,    // 14 Gordo
    COPYKIND_BOMB,    // 15 Bomber
    COPYKIND_NEEDLE,  // 16 Pichikuri
    COPYKIND_NEEDLE,  // 17 Pichikuri (dup)
    COPYKIND_FIRE,    // 18 Dayl
    COPYKIND_FIRE,    // 19 Dayl (flags=4)
    COPYKIND_TORNADO, // 20 Caller (internal: Shaturn)
    COPYKIND_MIKE,    // 21 Walky
    COPYKIND_NONE,    // 22 Waddle Dee Truck
    COPYKIND_NONE,    // 23 Waddle Dee
};

// Specials are all NONE except SP Sword Knight.
static CopyKind EnemyIDToCopyKind(int enemy_id)
{
    if (enemy_id >= ENEMYKIND_TIER0_START && enemy_id < ENEMYKIND_SPECIAL_START)
        return enemy_slot_copykind[(enemy_id - ENEMYKIND_TIER0_START) % ENEMYKIND_ENEMIES_PER_TIER];
    if (enemy_id == ENEMYKIND_SP_SWORD_KNIGHT)
        return COPYKIND_SWORD;
    return COPYKIND_NONE;
}

static int IsEnemyAbilityLocked(int enemy_id)
{
    CopyKind ck = EnemyIDToCopyKind(enemy_id);
    return ck != COPYKIND_NONE && !IsAbilityUnlocked(ck);
}

// Returns 1 if any positive-weight entry remains after zeroing locked-ability enemies.
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

// Mode 1 (Air Ride courses) and mode 3 (STKIND_MELEE2): each entry carries ids[max_slots]
// and weights[max_slots] at the given offsets.
static void FilterMode1Or3(EnemySpawnData *data, int ids_offset, int weights_offset, int max_slots)
{
    if (!data->spawn_entries || data->spawn_count <= 0)
        return;

    // 1 = has valid enemies, 0 = all zeroed, -1 = not yet processed
    s8 meta_valid[ENEMY_META_NUM];
    memset(meta_valid, -1, sizeof(meta_valid));

    for (int i = 0; i < data->spawn_count; i++)
    {
        char *entry = (char *)&data->spawn_entries[i];
        short *ids = (short *)(entry + ids_offset);
        short *weights = (short *)(entry + weights_offset);

        for (int slot = 0; slot < max_slots; slot++)
        {
            if (weights[slot] == -1)
                break;

            int enemy_id = ids[slot];
            if (enemy_id < 0)
                continue;

            // Meta-enemy IDs select from a secondary sub-table.
            if (enemy_id >= ENEMY_META_ID_BASE && enemy_id < ENEMY_META_ID_BASE + ENEMY_META_NUM)
            {
                int meta = enemy_id - ENEMY_META_ID_BASE;
                if (meta_valid[meta] == -1)
                {
                    short *sub_table = data->secondary_table
                                       ? data->secondary_table[meta]
                                       : NULL;
                    meta_valid[meta] = sub_table ? FilterSecondarySubTable(sub_table) : 0;
                }
                if (!meta_valid[meta])
                    weights[slot] = 0;
                continue;
            }

            if (IsEnemyAbilityLocked(enemy_id))
                weights[slot] = 0;
        }
    }
}

// Mode 2 (STKIND_MELEE1): zeroes locked enemies in every category column, then drops the
// categories left empty. Enemy_SpawnerDecideMode2 indexes a column by its category's meta
// id - ENEMY_META_ID_BASE, not by its sub-table position.
static void FilterMode2(EnemySpawnData *data)
{
    if (!data->spawn_entries || data->spawn_count <= 0 || !data->secondary_table)
        return;

    short *sub_table = data->secondary_table[0];
    if (!sub_table)
        return;

    const int max_columns = GetElementsIn(data->spawn_entries->mode2.weight_columns);

    int columns[ENEMY_META_NUM];
    int num_categories = 0;
    while (num_categories < ENEMY_META_NUM && sub_table[num_categories * 2] != -1)
    {
        int col = sub_table[num_categories * 2] - ENEMY_META_ID_BASE;
        columns[num_categories] = (col >= 0 && col < max_columns) ? col : -1;
        num_categories++;
    }

    if (num_categories == 0)
        return;

    int zeroed_entries = 0;
    for (int i = 0; i < data->spawn_count; i++)
    {
        EnemySpawnEntry *entry = &data->spawn_entries[i];
        int enemy_id = entry->mode2.enemy_id;

        if (enemy_id < 0 || !IsEnemyAbilityLocked(enemy_id))
            continue;

        for (int cat = 0; cat < num_categories; cat++)
        {
            if (columns[cat] >= 0)
                entry->mode2.weight_columns[columns[cat]] = 0;
        }
        zeroed_entries++;
    }

    // The sub-table weights are ascending thresholds, so a 0 is never selected.
    int zeroed_categories = 0;
    for (int cat = 0; cat < num_categories; cat++)
    {
        if (columns[cat] < 0)
            continue;

        int has_valid = 0;
        for (int i = 0; i < data->spawn_count; i++)
        {
            if (data->spawn_entries[i].mode2.weight_columns[columns[cat]] > 0)
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
    if (zeroed_entries || zeroed_categories)
        OSReport("[GateAbilities] Mode 2: zeroed %d/%d entries, %d/%d categories\n",
                 zeroed_entries, data->spawn_count, zeroed_categories, num_categories);
}

// Zeroes locked-ability enemies in the stage .dat in place; it reloads each stage load.
// spawn_data is NULL in CT Free Run, Top Ride and any stage with enemies off.
void GateAbilities_On3DLoadEnd()
{
    EnemySpawnData *data = *stc_enemy_spawn_data;
    if (!data || !data->config)
        return;

    short mode = data->config->mode;
    switch (mode)
    {
        case 1:
            FilterMode1Or3(data, offsetof(EnemySpawnEntry, mode1.ids), offsetof(EnemySpawnEntry, mode1.weights),
                           GetElementsIn(data->spawn_entries->mode1.ids));
            break;
        case 2:
            FilterMode2(data);
            break;
        case 3:
            FilterMode1Or3(data, offsetof(EnemySpawnEntry, mode3.ids), offsetof(EnemySpawnEntry, mode3.weights),
                           GetElementsIn(data->spawn_entries->mode3.ids));
            break;
        default:
            OSReport("[GateAbilities] Unknown spawn mode %d, not filtered\n", mode);
            return;
    }
    OSReport("[GateAbilities] Enemy spawns filtered (mode=%d, entries=%d, mask = %s)\n",
             mode, data->spawn_count, MaskBits(ap_save->ability_unlocked_mask, COPYKIND_NUM));
}

void GateAbilities_OnBoot()
{
    CODEPATCH_REPLACEFUNC(RiderGObj_CheckAndGiveAbility, GateAbilities_CheckAndGiveAbility);
    CODEPATCH_REPLACEFUNC(randomAbility_giveAbility, GateAbilities_RandomGiveAbility);
    CODEPATCH_REPLACECALL(0x801ae874, GateAbilities_MarkWheelGrant); // in randomAbility_aPress
    CODEPATCH_REPLACECALL(0x801ae910, GateAbilities_MarkWheelGrant); // in randomAbility_autoSelect
    OSReport("[GateAbilities] Hooks installed\n");
}

int GateAbilities_UnlockAbility(CopyKind kind)
{
    if (kind >= COPYKIND_NUM)
        return 0;

    ap_save->ability_unlocked_mask |= (1 << kind);
    OSReport("[GateAbilities] Ability %d (%s) unlocked (mask = %s)\n",
             kind, CopyKind_Names[kind], MaskBits(ap_save->ability_unlocked_mask, COPYKIND_NUM));
    APAnnounce_Grant("Unlocked Ability: ", CopyKind_Names[kind], tb_api->AbilityColors[kind], NULL);
    return 1;
}
