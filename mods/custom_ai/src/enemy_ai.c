#include "custom_ai.h"
#include "enemy_ai.h"

// Menu selection, bound to the settings menu (see main.c). May hold
// ENEMY_AI_RANDOM; resolve to a concrete preset with EnemyAI_Resolve.
int enemy_ai_preset = ENEMY_AI_DEFAULT;

// Preset tuning table. Multipliers stack on top of each enemy's stock
// behavioral params; 1.0 keeps the vanilla value. First-pass numbers - expect
// to retune once the spawn hook that applies them exists.
static const EnemyAIPresetDef enemy_presets[ENEMY_AI_PRESET_NUM] = {
    [ENEMY_AI_DEFAULT] = {
        .name = "Default",
        .description = "Vanilla enemy behavior - no changes",
        .detect_range_mult = 1.0f,
        .chase_range_mult  = 1.0f,
        .move_speed_mult   = 1.0f,
        .target_pref       = ENEMY_TARGET_VANILLA,
    },
    [ENEMY_AI_AGGRESSIVE] = {
        .name = "Aggressive",
        .description = "Spots players from farther, chases relentlessly, attacks on sight",
        .detect_range_mult = 2.0f,
        .chase_range_mult  = 2.0f,
        .move_speed_mult   = 1.35f,
        .target_pref       = ENEMY_TARGET_PLAYER,
    },
    [ENEMY_AI_ITEM_HOARDER] = {
        .name = "Item Hoarder",
        .description = "Ignores players to grab item boxes and dropped patches",
        .detect_range_mult = 1.5f,
        .chase_range_mult  = 1.5f,
        .move_speed_mult   = 1.2f,
        .target_pref       = ENEMY_TARGET_ITEM,
    },
    [ENEMY_AI_COWARD] = {
        .name = "Coward",
        .description = "Flees from nearby players instead of engaging",
        .detect_range_mult = 1.75f,
        .chase_range_mult  = 1.0f,
        .move_speed_mult   = 1.25f,
        .target_pref       = ENEMY_TARGET_FLEE,
    },
    [ENEMY_AI_ERRATIC] = {
        .name = "Erratic",
        .description = "Unpredictable - switches targets often and wanders",
        .detect_range_mult = 1.25f,
        .chase_range_mult  = 0.75f,
        .move_speed_mult   = 1.1f,
        .target_pref       = ENEMY_TARGET_VANILLA,
    },
};

const EnemyAIPresetDef *EnemyAI_GetPresetDef(int preset)
{
    if (preset < 0 || preset >= ENEMY_AI_PRESET_NUM)
        preset = ENEMY_AI_DEFAULT;
    return &enemy_presets[preset];
}

const char *EnemyAI_GetPresetName(int preset)
{
    return EnemyAI_GetPresetDef(preset)->name;
}

const char *EnemyAI_GetSelectionName(int selection)
{
    if (selection == ENEMY_AI_RANDOM)
        return "Random";
    return EnemyAI_GetPresetName(selection);
}

int EnemyAI_Resolve(int selection)
{
    // Roll among the non-default presets so "Random" always changes behavior.
    if (selection == ENEMY_AI_RANDOM)
        return ENEMY_AI_DEFAULT + 1 + CustomAI_RollRandom(ENEMY_AI_PRESET_NUM - 1);
    if (selection < 0 || selection >= ENEMY_AI_PRESET_NUM)
        return ENEMY_AI_DEFAULT;
    return selection;
}
